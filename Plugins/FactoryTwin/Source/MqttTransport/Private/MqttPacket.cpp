#include "MqttPacket.h"

// ---------------------------------------------------------------------------
// FMqttPacketWriter
// ---------------------------------------------------------------------------

void FMqttPacketWriter::WriteByte(const uint8 Value)
{
	Buffer.Add(Value);
}

void FMqttPacketWriter::WriteUInt16(const uint16 Value)
{
	// Big-endian on the wire regardless of host order.
	Buffer.Add(static_cast<uint8>((Value >> 8) & 0xFF));
	Buffer.Add(static_cast<uint8>(Value & 0xFF));
}

void FMqttPacketWriter::WriteString(const FString& Value)
{
	const FTCHARToUTF8 Converted(*Value);
	const int32 Length = Converted.Length();
	checkf(Length <= 0xFFFF, TEXT("MQTT string exceeds 65535 bytes"));

	WriteUInt16(static_cast<uint16>(Length));
	Buffer.Append(reinterpret_cast<const uint8*>(Converted.Get()), Length);
}

void FMqttPacketWriter::WriteBinary(const TArray<uint8>& Value)
{
	checkf(Value.Num() <= 0xFFFF, TEXT("MQTT binary field exceeds 65535 bytes"));

	WriteUInt16(static_cast<uint16>(Value.Num()));
	Buffer.Append(Value);
}

void FMqttPacketWriter::WriteRaw(const TArray<uint8>& Value)
{
	Buffer.Append(Value);
}

// ---------------------------------------------------------------------------
// FMqttPacketReader
// ---------------------------------------------------------------------------

bool FMqttPacketReader::ReadByte(uint8& OutValue)
{
	if (Remaining() < 1)
	{
		return false;
	}
	OutValue = Data[Position++];
	return true;
}

bool FMqttPacketReader::ReadUInt16(uint16& OutValue)
{
	if (Remaining() < 2)
	{
		return false;
	}
	OutValue = static_cast<uint16>((Data[Position] << 8) | Data[Position + 1]);
	Position += 2;
	return true;
}

bool FMqttPacketReader::ReadString(FString& OutValue)
{
	uint16 Length = 0;
	if (!ReadUInt16(Length))
	{
		return false;
	}
	if (Remaining() < Length)
	{
		return false;
	}

	// The bytes are not null-terminated on the wire. Note that
	// FString(const TCHAR*, int32) treats its second argument as extra slack,
	// not a length, so it would read past the end here; construct from an
	// explicit pointer + size instead.
	const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Data + Position), Length);
	OutValue = FString::ConstructFromPtrSize(Converted.Get(), Converted.Length());
	Position += Length;
	return true;
}

bool FMqttPacketReader::ReadRemaining(TArray<uint8>& OutValue)
{
	const int32 Left = Remaining();
	OutValue.Reset(Left);
	if (Left > 0)
	{
		OutValue.Append(Data + Position, Left);
		Position += Left;
	}
	return true;
}

// ---------------------------------------------------------------------------
// Packet construction
// ---------------------------------------------------------------------------

namespace MqttPacket
{
	int32 EncodeRemainingLength(uint32 Length, uint8 OutBytes[MaxRemainingLengthBytes])
	{
		if (Length > MaxPayloadSize)
		{
			return 0;
		}

		int32 Count = 0;
		do
		{
			uint8 Digit = static_cast<uint8>(Length % 128);
			Length /= 128;
			// Continuation bit signals another length byte follows.
			if (Length > 0)
			{
				Digit |= 0x80;
			}
			OutBytes[Count++] = Digit;
		}
		while (Length > 0 && Count < MaxRemainingLengthBytes);

		return Count;
	}

	bool DecodeRemainingLength(
		const uint8* Data, const int32 Available, uint32& OutLength, int32& OutBytesConsumed)
	{
		uint32 Multiplier = 1;
		uint32 Value = 0;
		int32 Index = 0;

		while (Index < Available && Index < MaxRemainingLengthBytes)
		{
			const uint8 Digit = Data[Index++];
			Value += static_cast<uint32>(Digit & 0x7F) * Multiplier;

			if ((Digit & 0x80) == 0)
			{
				OutLength = Value;
				OutBytesConsumed = Index;
				return true;
			}
			Multiplier *= 128;
		}

		// Either we ran out of buffer (caller should wait for more) or the field
		// is malformed at 4 bytes with the continuation bit still set.
		return false;
	}

	TArray<uint8> Frame(const EMqttPacketType Type, const uint8 Flags, const TArray<uint8>& Body)
	{
		uint8 LengthBytes[MaxRemainingLengthBytes];
		const int32 LengthCount = EncodeRemainingLength(static_cast<uint32>(Body.Num()), LengthBytes);

		TArray<uint8> Packet;
		Packet.Reserve(1 + LengthCount + Body.Num());
		Packet.Add(static_cast<uint8>((static_cast<uint8>(Type) << 4) | (Flags & 0x0F)));
		Packet.Append(LengthBytes, LengthCount);
		Packet.Append(Body);
		return Packet;
	}

	TArray<uint8> BuildConnect(const FMqttConnectionOptions& Options)
	{
		FMqttPacketWriter Writer;

		// Variable header: protocol name and level (3.1.1 == 4).
		Writer.WriteString(TEXT("MQTT"));
		Writer.WriteByte(0x04);

		uint8 ConnectFlags = 0;
		if (Options.bCleanSession)
		{
			ConnectFlags |= 0x02;
		}
		if (Options.Will.bEnabled)
		{
			ConnectFlags |= 0x04;
			ConnectFlags |= static_cast<uint8>((static_cast<uint8>(Options.Will.QoS) & 0x03) << 3);
			if (Options.Will.bRetain)
			{
				ConnectFlags |= 0x20;
			}
		}
		if (!Options.Username.IsEmpty())
		{
			ConnectFlags |= 0x80;
			// Password is only meaningful alongside a username.
			if (!Options.Password.IsEmpty())
			{
				ConnectFlags |= 0x40;
			}
		}
		Writer.WriteByte(ConnectFlags);
		Writer.WriteUInt16(static_cast<uint16>(FMath::Clamp(Options.KeepAliveSeconds, 0, 65535)));

		// Payload, strictly ordered: client id, will, username, password.
		Writer.WriteString(Options.ClientId);
		if (Options.Will.bEnabled)
		{
			Writer.WriteString(Options.Will.Topic);
			// Binary, not string: the Sparkplug NDEATH payload is protobuf.
			Writer.WriteBinary(Options.Will.Payload);
		}
		if (!Options.Username.IsEmpty())
		{
			Writer.WriteString(Options.Username);
			if (!Options.Password.IsEmpty())
			{
				Writer.WriteString(Options.Password);
			}
		}

		return Frame(EMqttPacketType::Connect, 0x00, Writer.GetBuffer());
	}

	TArray<uint8> BuildPublish(
		const FString& Topic,
		const TArray<uint8>& Payload,
		const EMqttQoS QoS,
		const bool bRetain,
		const uint16 PacketId,
		const bool bDuplicate)
	{
		FMqttPacketWriter Writer;
		Writer.WriteString(Topic);

		// Packet identifier is present only for QoS 1 and 2.
		if (QoS != EMqttQoS::AtMostOnce)
		{
			Writer.WriteUInt16(PacketId);
		}

		// Body runs to the end of the packet with no length prefix, which is what
		// makes arbitrary binary payloads work.
		Writer.WriteRaw(Payload);

		uint8 Flags = static_cast<uint8>((static_cast<uint8>(QoS) & 0x03) << 1);
		if (bRetain)
		{
			Flags |= 0x01;
		}
		if (bDuplicate)
		{
			Flags |= 0x08;
		}

		return Frame(EMqttPacketType::Publish, Flags, Writer.GetBuffer());
	}

	TArray<uint8> BuildAck(const EMqttPacketType Type, const uint16 PacketId)
	{
		FMqttPacketWriter Writer;
		Writer.WriteUInt16(PacketId);

		// PUBREL reserves flag bits 0010; the rest use 0000.
		const uint8 Flags = (Type == EMqttPacketType::PubRel) ? 0x02 : 0x00;
		return Frame(Type, Flags, Writer.GetBuffer());
	}

	TArray<uint8> BuildSubscribe(
		const uint16 PacketId, const TArray<TPair<FString, EMqttQoS>>& Subscriptions)
	{
		FMqttPacketWriter Writer;
		Writer.WriteUInt16(PacketId);
		for (const TPair<FString, EMqttQoS>& Subscription : Subscriptions)
		{
			Writer.WriteString(Subscription.Key);
			Writer.WriteByte(static_cast<uint8>(Subscription.Value) & 0x03);
		}

		// SUBSCRIBE reserves flag bits 0010.
		return Frame(EMqttPacketType::Subscribe, 0x02, Writer.GetBuffer());
	}

	TArray<uint8> BuildUnsubscribe(const uint16 PacketId, const TArray<FString>& Topics)
	{
		FMqttPacketWriter Writer;
		Writer.WriteUInt16(PacketId);
		for (const FString& Topic : Topics)
		{
			Writer.WriteString(Topic);
		}

		// UNSUBSCRIBE reserves flag bits 0010.
		return Frame(EMqttPacketType::Unsubscribe, 0x02, Writer.GetBuffer());
	}

	TArray<uint8> BuildPingReq()
	{
		return Frame(EMqttPacketType::PingReq, 0x00, TArray<uint8>());
	}

	TArray<uint8> BuildDisconnect()
	{
		return Frame(EMqttPacketType::Disconnect, 0x00, TArray<uint8>());
	}

	// -----------------------------------------------------------------------
	// Parsing
	// -----------------------------------------------------------------------

	bool ParseConnAck(
		const TArray<uint8>& Body, bool& bOutSessionPresent, EMqttConnectReturnCode& OutReturnCode)
	{
		FMqttPacketReader Reader(Body);

		uint8 AckFlags = 0;
		uint8 ReturnCode = 0;
		if (!Reader.ReadByte(AckFlags) || !Reader.ReadByte(ReturnCode))
		{
			return false;
		}

		bOutSessionPresent = (AckFlags & 0x01) != 0;
		OutReturnCode = (ReturnCode <= 5)
			? static_cast<EMqttConnectReturnCode>(ReturnCode)
			: EMqttConnectReturnCode::ConnectionFailed;
		return true;
	}

	bool ParsePublish(
		const TArray<uint8>& Body, const uint8 Flags, FMqttTransportMessage& OutMessage, uint16& OutPacketId)
	{
		FMqttPacketReader Reader(Body);

		if (!Reader.ReadString(OutMessage.Topic))
		{
			return false;
		}

		const uint8 QoSBits = static_cast<uint8>((Flags >> 1) & 0x03);
		if (QoSBits > 2)
		{
			return false;
		}
		OutMessage.QoS = static_cast<EMqttQoS>(QoSBits);
		OutMessage.bRetain = (Flags & 0x01) != 0;

		OutPacketId = 0;
		if (OutMessage.QoS != EMqttQoS::AtMostOnce && !Reader.ReadUInt16(OutPacketId))
		{
			return false;
		}

		return Reader.ReadRemaining(OutMessage.Payload);
	}

	bool ParseAck(const TArray<uint8>& Body, uint16& OutPacketId)
	{
		FMqttPacketReader Reader(Body);
		return Reader.ReadUInt16(OutPacketId);
	}

	bool ParseSubAck(const TArray<uint8>& Body, uint16& OutPacketId, TArray<uint8>& OutReturnCodes)
	{
		FMqttPacketReader Reader(Body);
		if (!Reader.ReadUInt16(OutPacketId))
		{
			return false;
		}
		return Reader.ReadRemaining(OutReturnCodes);
	}
}
