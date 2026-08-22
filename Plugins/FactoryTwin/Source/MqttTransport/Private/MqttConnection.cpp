#include "MqttConnection.h"

#include "Common/TcpSocketBuilder.h"
#include "HAL/RunnableThread.h"
#include "SocketSubsystem.h"
#include "Sockets.h"

namespace
{
	/** How long the worker blocks waiting for readable data before looping. */
	constexpr double PollIntervalSeconds = 0.05;

	/** Reads per pump, so a busy socket cannot starve the rest of the loop. */
	constexpr int32 MaxReadsPerPump = 32;

	constexpr int32 ReadChunkSize = 8192;
}

TSharedRef<FMqttConnection, ESPMode::ThreadSafe> FMqttConnection::Create(const FMqttConnectionOptions& InOptions)
{
	return MakeShareable(new FMqttConnection(InOptions));
}

FMqttConnection::FMqttConnection(const FMqttConnectionOptions& InOptions)
	: Options(InOptions)
{
	if (Options.ClientId.IsEmpty())
	{
		// Broker rejects duplicate client ids, so make it unique per process.
		Options.ClientId = FString::Printf(TEXT("ue-%s-%u"),
			FPlatformProcess::ComputerName(), FPlatformProcess::GetCurrentProcessId());
	}
	CurrentReconnectDelay = Options.ReconnectDelayMinSeconds;
}

FMqttConnection::~FMqttConnection()
{
	Close();
}

void FMqttConnection::Open()
{
	if (Thread != nullptr)
	{
		return;
	}

	bStopRequested = false;
	State.store(EMqttConnectionState::Connecting, std::memory_order_relaxed);

	Thread = FRunnableThread::Create(
		this, TEXT("MqttConnection"), 128 * 1024, TPri_BelowNormal);

	if (Thread == nullptr)
	{
		UE_LOG(LogMqttTransport, Error, TEXT("Failed to create MQTT worker thread"));
		State.store(EMqttConnectionState::Disconnected, std::memory_order_relaxed);
	}
}

void FMqttConnection::Close()
{
	if (Thread == nullptr)
	{
		return;
	}

	// The worker owns the socket, so it performs the final flush and sends
	// DISCONNECT itself (see Run). Writing from this thread would race it, and
	// stopping the worker first would drop anything still queued.
	bStopRequested = true;
	Thread->WaitForCompletion();
	delete Thread;
	Thread = nullptr;
}

void FMqttConnection::CloseUngracefully()
{
	// Skipping DISCONNECT is the entire difference: MQTT 3.1.1 section 3.14
	// says the will is published whenever the network connection closes without
	// a DISCONNECT packet having been received.
	bSuppressDisconnectPacket = true;
	Close();
}

bool FMqttConnection::Init()
{
	SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (SocketSubsystem == nullptr)
	{
		UE_LOG(LogMqttTransport, Error, TEXT("No socket subsystem available"));
		return false;
	}
	return true;
}

uint32 FMqttConnection::Run()
{
	while (!bStopRequested)
	{
		if (!EstablishSession())
		{
			if (bStopRequested || !Options.bAutoReconnect)
			{
				break;
			}

			State.store(EMqttConnectionState::Reconnecting, std::memory_order_relaxed);
			UE_LOG(LogMqttTransport, Warning,
				TEXT("MQTT connect failed; retrying in %.1fs"), CurrentReconnectDelay);

			// Sleep in slices so a shutdown request is picked up promptly.
			const double WakeTime = FPlatformTime::Seconds() + CurrentReconnectDelay;
			while (!bStopRequested && FPlatformTime::Seconds() < WakeTime)
			{
				FPlatformProcess::Sleep(0.1f);
			}

			CurrentReconnectDelay = FMath::Min(
				CurrentReconnectDelay * 2.0f, Options.ReconnectDelayMaxSeconds);
			continue;
		}

		CurrentReconnectDelay = Options.ReconnectDelayMinSeconds;

		// Session loop: pump until the socket dies or we are asked to stop.
		while (!bStopRequested)
		{
			if (!FlushOutbound() || !PumpInbound())
			{
				break;
			}

			const double Now = FPlatformTime::Seconds();

			if (Options.KeepAliveSeconds > 0)
			{
				// Ping at half the keepalive so we are comfortably inside the
				// broker's 1.5x grace window.
				const double PingAfter = Options.KeepAliveSeconds * 0.5;
				if (Now - LastSendTime >= PingAfter)
				{
					if (!SendRaw(MqttPacket::BuildPingReq()))
					{
						break;
					}
				}

				// No traffic at all for 2x keepalive means the link is dead even
				// if the socket has not reported an error yet.
				const double DeadAfter = Options.KeepAliveSeconds * 2.0;
				if (Now - LastReceiveTime >= DeadAfter)
				{
					UE_LOG(LogMqttTransport, Warning, TEXT("MQTT keepalive timeout"));
					break;
				}
			}

			if (Socket != nullptr)
			{
				Socket->Wait(ESocketWaitConditions::WaitForRead,
					FTimespan::FromSeconds(PollIntervalSeconds));
			}
		}

		// Drain whatever is still queued before saying goodbye. Sparkplug
		// publishes DDEATH for every device and then NDEATH during shutdown, and
		// those enqueue just before Close() is called -- stopping here without a
		// final flush would silently drop the death certificates.
		if (bSessionActive && Socket != nullptr)
		{
			FlushOutbound();

			// Sending DISCONNECT tells the broker this was deliberate, so it
			// discards the will. Suppressing it is how a crash is simulated.
			if (!bSuppressDisconnectPacket)
			{
				SendRaw(MqttPacket::BuildDisconnect());
			}
		}

		const bool bWasActive = bSessionActive;
		bSessionActive = false;
		TeardownSocket();

		if (bWasActive)
		{
			State.store(EMqttConnectionState::Disconnected, std::memory_order_relaxed);
			DisconnectedEvent.Broadcast();
		}

		if (bStopRequested || !Options.bAutoReconnect)
		{
			break;
		}
	}

	State.store(EMqttConnectionState::Disconnected, std::memory_order_relaxed);
	return 0;
}

void FMqttConnection::Stop()
{
	bStopRequested = true;
}

void FMqttConnection::Exit()
{
	TeardownSocket();
}

bool FMqttConnection::EstablishSession()
{
	TeardownSocket();
	State.store(EMqttConnectionState::Connecting, std::memory_order_relaxed);

	// Resolve the host. GetAddressInfo handles both literals and DNS names.
	const FAddressInfoResult AddressResult = SocketSubsystem->GetAddressInfo(
		*Options.Host, nullptr, EAddressInfoFlags::Default, NAME_None, ESocketType::SOCKTYPE_Streaming);

	if (AddressResult.ReturnCode != SE_NO_ERROR || AddressResult.Results.Num() == 0)
	{
		UE_LOG(LogMqttTransport, Warning, TEXT("Could not resolve MQTT host '%s'"), *Options.Host);
		return false;
	}

	const TSharedRef<FInternetAddr> Address = AddressResult.Results[0].Address->Clone();
	Address->SetPort(Options.Port);

	Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MqttConnection"), Address->GetProtocolType());
	if (Socket == nullptr)
	{
		UE_LOG(LogMqttTransport, Warning, TEXT("Could not create MQTT socket"));
		return false;
	}

	Socket->SetNonBlocking(false);
	// Latency matters more than packet efficiency for small telemetry frames.
	Socket->SetNoDelay(true);

	if (!Socket->Connect(*Address))
	{
		UE_LOG(LogMqttTransport, Warning,
			TEXT("Could not connect to %s:%d"), *Options.Host, Options.Port);
		TeardownSocket();
		return false;
	}

	ReceiveBuffer.Reset();
	InFlight.Reset();

	// The will rides on this packet; that is the whole point of the module.
	if (!SendRaw(MqttPacket::BuildConnect(Options)))
	{
		TeardownSocket();
		return false;
	}

	if (Options.Will.bEnabled)
	{
		UE_LOG(LogMqttTransport, Verbose, TEXT("CONNECT carried will for '%s' (%d bytes)"),
			*Options.Will.Topic, Options.Will.Payload.Num());
	}

	// Wait for CONNACK before declaring the session up.
	const double Deadline = FPlatformTime::Seconds() + 10.0;
	LastReceiveTime = FPlatformTime::Seconds();

	while (!bStopRequested && FPlatformTime::Seconds() < Deadline)
	{
		Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(PollIntervalSeconds));
		if (!PumpInbound())
		{
			TeardownSocket();
			return false;
		}
		if (bSessionActive)
		{
			State.store(EMqttConnectionState::Connected, std::memory_order_relaxed);
			return true;
		}
	}

	UE_LOG(LogMqttTransport, Warning, TEXT("Timed out waiting for CONNACK"));
	TeardownSocket();
	return false;
}

void FMqttConnection::TeardownSocket()
{
	if (Socket != nullptr)
	{
		Socket->Close();
		if (SocketSubsystem != nullptr)
		{
			SocketSubsystem->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
	bSessionActive = false;
}

bool FMqttConnection::FlushOutbound()
{
	TArray<uint8> Frame;
	while (OutboundQueue.Dequeue(Frame))
	{
		if (!SendRaw(Frame))
		{
			return false;
		}
	}
	return true;
}

bool FMqttConnection::PumpInbound()
{
	if (Socket == nullptr)
	{
		return false;
	}

	uint8 Chunk[ReadChunkSize];

	for (int32 Read = 0; Read < MaxReadsPerPump; ++Read)
	{
		uint32 Pending = 0;
		if (!Socket->HasPendingData(Pending) || Pending == 0)
		{
			break;
		}

		int32 BytesRead = 0;
		const int32 ToRead = FMath::Min(static_cast<int32>(Pending), ReadChunkSize);
		if (!Socket->Recv(Chunk, ToRead, BytesRead, ESocketReceiveFlags::None) || BytesRead <= 0)
		{
			UE_LOG(LogMqttTransport, Warning, TEXT("MQTT socket read failed"));
			return false;
		}

		ReceiveBuffer.Append(Chunk, BytesRead);
		LastReceiveTime = FPlatformTime::Seconds();
	}

	return ProcessReceiveBuffer();
}

bool FMqttConnection::ProcessReceiveBuffer()
{
	// Peel off as many complete frames as the buffer holds.
	while (ReceiveBuffer.Num() >= 2)
	{
		const uint8 Header = ReceiveBuffer[0];
		const EMqttPacketType Type = static_cast<EMqttPacketType>((Header >> 4) & 0x0F);
		const uint8 Flags = Header & 0x0F;

		uint32 RemainingLength = 0;
		int32 LengthBytes = 0;
		if (!MqttPacket::DecodeRemainingLength(
			ReceiveBuffer.GetData() + 1, ReceiveBuffer.Num() - 1, RemainingLength, LengthBytes))
		{
			// Either incomplete (wait for more) or malformed past 4 bytes.
			if (ReceiveBuffer.Num() - 1 >= MqttPacket::MaxRemainingLengthBytes)
			{
				UE_LOG(LogMqttTransport, Error, TEXT("Malformed MQTT remaining length; dropping link"));
				return false;
			}
			break;
		}

		const int32 FrameSize = 1 + LengthBytes + static_cast<int32>(RemainingLength);
		if (ReceiveBuffer.Num() < FrameSize)
		{
			break;
		}

		TArray<uint8> Body;
		if (RemainingLength > 0)
		{
			Body.Append(ReceiveBuffer.GetData() + 1 + LengthBytes, static_cast<int32>(RemainingLength));
		}

		HandlePacket(Type, Flags, Body);
		ReceiveBuffer.RemoveAt(0, FrameSize, EAllowShrinking::No);
	}

	return true;
}

void FMqttConnection::HandlePacket(
	const EMqttPacketType Type, const uint8 Flags, const TArray<uint8>& Body)
{
	switch (Type)
	{
	case EMqttPacketType::ConnAck:
	{
		bool bSessionPresent = false;
		EMqttConnectReturnCode ReturnCode = EMqttConnectReturnCode::ConnectionFailed;
		if (!MqttPacket::ParseConnAck(Body, bSessionPresent, ReturnCode))
		{
			UE_LOG(LogMqttTransport, Error, TEXT("Malformed CONNACK"));
			break;
		}

		if (ReturnCode == EMqttConnectReturnCode::Accepted)
		{
			bSessionActive = true;
			LastSendTime = FPlatformTime::Seconds();
			UE_LOG(LogMqttTransport, Log, TEXT("MQTT connected to %s:%d as '%s'"),
				*Options.Host, Options.Port, *Options.ClientId);
		}
		else
		{
			UE_LOG(LogMqttTransport, Error,
				TEXT("Broker refused connection, code %d"), static_cast<int32>(ReturnCode));
		}
		ConnectedEvent.Broadcast(ReturnCode);
		break;
	}

	case EMqttPacketType::Publish:
	{
		FMqttTransportMessage Message;
		uint16 PacketId = 0;
		if (!MqttPacket::ParsePublish(Body, Flags, Message, PacketId))
		{
			UE_LOG(LogMqttTransport, Error, TEXT("Malformed PUBLISH"));
			break;
		}

		// Acknowledge before dispatching so a slow handler cannot stall the broker.
		if (Message.QoS == EMqttQoS::AtLeastOnce)
		{
			EnqueueRaw(MqttPacket::BuildAck(EMqttPacketType::PubAck, PacketId));
		}
		else if (Message.QoS == EMqttQoS::ExactlyOnce)
		{
			EnqueueRaw(MqttPacket::BuildAck(EMqttPacketType::PubRec, PacketId));
		}

		MessageEvent.Broadcast(Message);
		break;
	}

	case EMqttPacketType::PubAck:
	case EMqttPacketType::PubComp:
	{
		uint16 PacketId = 0;
		if (MqttPacket::ParseAck(Body, PacketId))
		{
			InFlight.Remove(PacketId);
		}
		break;
	}

	case EMqttPacketType::PubRec:
	{
		// QoS 2 outbound, step 2: broker received it, release it.
		uint16 PacketId = 0;
		if (MqttPacket::ParseAck(Body, PacketId))
		{
			EnqueueRaw(MqttPacket::BuildAck(EMqttPacketType::PubRel, PacketId));
		}
		break;
	}

	case EMqttPacketType::PubRel:
	{
		// QoS 2 inbound, step 3: complete the handshake.
		uint16 PacketId = 0;
		if (MqttPacket::ParseAck(Body, PacketId))
		{
			EnqueueRaw(MqttPacket::BuildAck(EMqttPacketType::PubComp, PacketId));
		}
		break;
	}

	case EMqttPacketType::SubAck:
	{
		uint16 PacketId = 0;
		TArray<uint8> ReturnCodes;
		if (MqttPacket::ParseSubAck(Body, PacketId, ReturnCodes))
		{
			for (const uint8 Code : ReturnCodes)
			{
				// 0x80 is the broker refusing that particular filter.
				if (Code == 0x80)
				{
					UE_LOG(LogMqttTransport, Warning, TEXT("Broker rejected a subscription"));
				}
			}
		}
		break;
	}

	case EMqttPacketType::UnsubAck:
	case EMqttPacketType::PingResp:
		break;

	default:
		UE_LOG(LogMqttTransport, Verbose,
			TEXT("Ignoring inbound packet type %d"), static_cast<int32>(Type));
		break;
	}
}

bool FMqttConnection::SendRaw(const TArray<uint8>& Bytes)
{
	if (Socket == nullptr || Bytes.Num() == 0)
	{
		return Socket != nullptr;
	}

	int32 TotalSent = 0;
	while (TotalSent < Bytes.Num())
	{
		int32 Sent = 0;
		if (!Socket->Send(Bytes.GetData() + TotalSent, Bytes.Num() - TotalSent, Sent) || Sent <= 0)
		{
			UE_LOG(LogMqttTransport, Warning, TEXT("MQTT socket write failed"));
			return false;
		}
		TotalSent += Sent;
	}

	LastSendTime = FPlatformTime::Seconds();
	return true;
}

void FMqttConnection::EnqueueRaw(TArray<uint8>&& Bytes)
{
	OutboundQueue.Enqueue(MoveTemp(Bytes));
}

uint16 FMqttConnection::NextPacketId()
{
	// Packet id 0 is reserved, so skip it on wrap.
	for (;;)
	{
		const int32 Raw = PacketIdCounter.Increment();
		const uint16 Id = static_cast<uint16>(Raw & 0xFFFF);
		if (Id != 0)
		{
			return Id;
		}
	}
}

bool FMqttConnection::Publish(
	const FString& Topic, const TArray<uint8>& Payload, const EMqttQoS QoS, const bool bRetain)
{
	if (Topic.IsEmpty())
	{
		UE_LOG(LogMqttTransport, Warning, TEXT("Refusing to publish to an empty topic"));
		return false;
	}
	if (static_cast<uint32>(Payload.Num()) > MqttPacket::MaxPayloadSize)
	{
		UE_LOG(LogMqttTransport, Error, TEXT("Payload exceeds MQTT maximum"));
		return false;
	}

	const uint16 PacketId = (QoS == EMqttQoS::AtMostOnce) ? 0 : NextPacketId();
	EnqueueRaw(MqttPacket::BuildPublish(Topic, Payload, QoS, bRetain, PacketId));
	return true;
}

bool FMqttConnection::Subscribe(const TArray<TPair<FString, EMqttQoS>>& Subscriptions)
{
	if (Subscriptions.Num() == 0)
	{
		return false;
	}
	EnqueueRaw(MqttPacket::BuildSubscribe(NextPacketId(), Subscriptions));
	return true;
}

bool FMqttConnection::Unsubscribe(const TArray<FString>& Topics)
{
	if (Topics.Num() == 0)
	{
		return false;
	}
	EnqueueRaw(MqttPacket::BuildUnsubscribe(NextPacketId(), Topics));
	return true;
}
