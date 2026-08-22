#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "MqttPacket.h"
#include "MqttTransportTypes.h"

#include <atomic>

class FRunnableThread;
class FSocket;
class ISocketSubsystem;

/**
 * MQTT 3.1.1 client over a raw FSocket, driven by its own worker thread.
 *
 * Delegates fire on the worker thread. Callers that need the game thread must
 * marshal themselves; UMqttTransportClient does this for the Blueprint layer.
 */
class MQTTTRANSPORT_API FMqttConnection final
	: public FRunnable
	, public TSharedFromThis<FMqttConnection, ESPMode::ThreadSafe>
{
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnConnectedSignature, EMqttConnectReturnCode);
	DECLARE_MULTICAST_DELEGATE(FOnDisconnectedSignature);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnMessageSignature, const FMqttTransportMessage&);

	static TSharedRef<FMqttConnection, ESPMode::ThreadSafe> Create(const FMqttConnectionOptions& InOptions);

	virtual ~FMqttConnection() override;

	/** Spins up the worker thread and begins connecting. */
	void Open();

	/** Sends DISCONNECT if connected, then joins the worker thread. Safe to call twice. */
	void Close();

	/**
	 * Drops the connection WITHOUT sending DISCONNECT, so the broker treats it as
	 * an abnormal termination and publishes the will.
	 *
	 * This models a crash. It is what makes the Sparkplug NDEATH certificate
	 * testable without actually killing the process.
	 */
	void CloseUngracefully();

	bool Publish(const FString& Topic, const TArray<uint8>& Payload, EMqttQoS QoS, bool bRetain);
	bool Subscribe(const TArray<TPair<FString, EMqttQoS>>& Subscriptions);
	bool Unsubscribe(const TArray<FString>& Topics);

	EMqttConnectionState GetState() const { return State.load(std::memory_order_relaxed); }
	bool IsConnected() const { return GetState() == EMqttConnectionState::Connected; }
	const FMqttConnectionOptions& GetOptions() const { return Options; }

	FOnConnectedSignature& OnConnected() { return ConnectedEvent; }
	FOnDisconnectedSignature& OnDisconnected() { return DisconnectedEvent; }
	FOnMessageSignature& OnMessage() { return MessageEvent; }

	/** See FMqttWillPayloadProvider. Runs on the worker thread. */
	void SetWillPayloadProvider(const FMqttWillPayloadProvider& Provider)
	{
		WillPayloadProvider = Provider;
	}

	//~ FRunnable
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;
	//~ End FRunnable

private:
	explicit FMqttConnection(const FMqttConnectionOptions& InOptions);

	/** Resolves the host and completes the MQTT handshake. Returns true once CONNACK is accepted. */
	bool EstablishSession();
	void TeardownSocket();

	/** Drains the outbound queue. Returns false on socket error. */
	bool FlushOutbound();
	/** Reads whatever is available and dispatches any complete packets. Returns false on socket error. */
	bool PumpInbound();
	/** Extracts complete packets out of ReceiveBuffer. */
	bool ProcessReceiveBuffer();
	void HandlePacket(EMqttPacketType Type, uint8 Flags, const TArray<uint8>& Body);

	bool SendRaw(const TArray<uint8>& Bytes);
	void EnqueueRaw(TArray<uint8>&& Bytes);
	uint16 NextPacketId();

	FMqttConnectionOptions Options;

	FSocket* Socket = nullptr;
	ISocketSubsystem* SocketSubsystem = nullptr;
	FRunnableThread* Thread = nullptr;

	std::atomic<EMqttConnectionState> State{ EMqttConnectionState::Disconnected };
	FThreadSafeBool bStopRequested{ false };
	/** Set once CONNACK is accepted so Close() knows to send DISCONNECT. */
	FThreadSafeBool bSessionActive{ false };

	/** When set, Close() skips DISCONNECT so the broker fires the will. */
	FThreadSafeBool bSuppressDisconnectPacket{ false };

	/** Outbound frames, written by any thread, drained by the worker. */
	TQueue<TArray<uint8>, EQueueMode::Mpsc> OutboundQueue;

	/** Partial inbound bytes awaiting a complete frame. Worker thread only. */
	TArray<uint8> ReceiveBuffer;

	/** Redelivers everything still unacknowledged. Called after a reconnect. */
	void ResendInFlight();

	/**
	 * QoS>0 sends awaiting acknowledgement, keyed by packet id. Written by any
	 * publishing thread and drained by the worker, so it needs the lock.
	 */
	TMap<uint16, TArray<uint8>> InFlight;
	mutable FCriticalSection InFlightLock;

	FMqttWillPayloadProvider WillPayloadProvider;

	FThreadSafeCounter PacketIdCounter{ 0 };

	double LastSendTime = 0.0;
	double LastReceiveTime = 0.0;
	/** Backoff for the next reconnect attempt, grows on each consecutive failure. */
	float CurrentReconnectDelay = 0.0f;

	FOnConnectedSignature ConnectedEvent;
	FOnDisconnectedSignature DisconnectedEvent;
	FOnMessageSignature MessageEvent;
};
