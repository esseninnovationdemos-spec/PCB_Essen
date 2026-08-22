#include "SparkplugEdgeNode.h"

#include "MqttTransportClient.h"
#include "SparkplugProto.h"

namespace
{
	/** Metric name the spec reserves for forcing a re-announce. */
	const FString RebirthMetricName = TEXT("Node Control/Rebirth");
	const FString BirthDeathSequenceMetricName = TEXT("bdSeq");
}

void USparkplugEdgeNode::RegisterDevice(
	const FString& DeviceId, const TArray<FSparkplugMetric>& BirthMetrics)
{
	if (DeviceId.IsEmpty())
	{
		UE_LOG(LogSparkplugB, Warning, TEXT("Refusing to register a device with an empty id"));
		return;
	}

	if (!Devices.Contains(DeviceId))
	{
		DeviceOrder.Add(DeviceId);
	}
	Devices.Add(DeviceId, BirthMetrics);

	// Registering while live announces immediately; otherwise the DBIRTH goes
	// out as part of the normal birth sequence after NBIRTH.
	if (bOnline)
	{
		PublishDeviceBirth(DeviceId);
	}
}

void USparkplugEdgeNode::UnregisterDevice(const FString& DeviceId)
{
	if (Devices.Remove(DeviceId) > 0)
	{
		DeviceOrder.Remove(DeviceId);

		if (bOnline)
		{
			FSparkplugPayload Payload;
			Payload.Timestamp = SparkplugUtils::UtcNowMilliseconds();
			PublishPayload(ESparkplugMessageType::DDEATH, DeviceId, Payload, EMqttQoS::AtLeastOnce);
		}
	}
}

void USparkplugEdgeNode::Connect(const FSparkplugEdgeNodeConfig& InConfig)
{
	if (Client != nullptr)
	{
		UE_LOG(LogSparkplugB, Warning, TEXT("Edge node is already connected"));
		return;
	}

	Config = InConfig;

	// A new session gets a new bdSeq, and NBIRTH and NDEATH must agree on it.
	++BirthDeathSequence;

	Client = NewObject<UMqttTransportClient>(this);
	Client->OnConnected.AddDynamic(this, &USparkplugEdgeNode::HandleMqttConnected);
	Client->OnDisconnected.AddDynamic(this, &USparkplugEdgeNode::HandleMqttDisconnected);
	Client->OnMessage.AddDynamic(this, &USparkplugEdgeNode::HandleMqttMessage);

	FMqttConnectionOptions Options = Config.Mqtt;

	// The death certificate must be registered as the will *before* connecting;
	// that is the whole reason this project has its own MQTT client.
	Options.Will.bEnabled = true;
	Options.Will.Topic = BuildTopic(ESparkplugMessageType::NDEATH, FString());
	Options.Will.Payload = BuildDeathPayload();
	Options.Will.QoS = EMqttQoS::AtLeastOnce;
	Options.Will.bRetain = false;

	UE_LOG(LogSparkplugB, Log, TEXT("Edge node '%s/%s' connecting to %s:%d (bdSeq %lld)"),
		*Config.GroupId, *Config.EdgeNodeId, *Options.Host, Options.Port, BirthDeathSequence);

	Client->Connect(Options);
}

void USparkplugEdgeNode::Disconnect()
{
	if (Client == nullptr)
	{
		return;
	}

	if (bOnline)
	{
		// Announce each device's death, then the node's, before going quiet.
		for (const FString& DeviceId : DeviceOrder)
		{
			FSparkplugPayload DevicePayload;
			DevicePayload.Timestamp = SparkplugUtils::UtcNowMilliseconds();
			PublishPayload(
				ESparkplugMessageType::DDEATH, DeviceId, DevicePayload, EMqttQoS::AtLeastOnce);
		}

		Client->PublishBytes(
			BuildTopic(ESparkplugMessageType::NDEATH, FString()),
			BuildDeathPayload(),
			EMqttQoS::AtLeastOnce,
			false);
	}

	bOnline = false;
	OnOnlineStateChanged.Broadcast(false);

	// Unbind first: callbacks already marshalled toward the game thread would
	// otherwise still arrive and act on a session that no longer exists.
	Client->OnConnected.RemoveDynamic(this, &USparkplugEdgeNode::HandleMqttConnected);
	Client->OnDisconnected.RemoveDynamic(this, &USparkplugEdgeNode::HandleMqttDisconnected);
	Client->OnMessage.RemoveDynamic(this, &USparkplugEdgeNode::HandleMqttMessage);

	// Graceful: sends DISCONNECT, which tells the broker to discard the will so
	// it does not duplicate the NDEATH we just published ourselves.
	Client->Disconnect();
	Client = nullptr;
}

TArray<uint8> USparkplugEdgeNode::BuildDeathPayload() const
{
	FSparkplugPayload Payload;
	Payload.Timestamp = SparkplugUtils::UtcNowMilliseconds();
	// Tahu 5.3: NDEATH carries no sequence number.
	Payload.bHasSeq = false;

	FSparkplugMetric Metric = FSparkplugMetric::MakeUInt64(
		BirthDeathSequenceMetricName, 0, static_cast<uint64>(BirthDeathSequence));
	Metric.Timestamp = 0;
	Payload.Metrics.Add(MoveTemp(Metric));

	return SparkplugProto::EncodePayload(Payload);
}

void USparkplugEdgeNode::HandleMqttConnected(const EMqttConnectReturnCode ReturnCode)
{
	// The transport marshals this from its worker thread, so the session can
	// already have been torn down by the time it arrives -- BP_MQTT_Manager does
	// exactly that, calling shutdown then init to force a reconnect. Bail rather
	// than dereferencing a client that Disconnect has released.
	if (Client == nullptr)
	{
		UE_LOG(LogSparkplugB, Verbose,
			TEXT("Ignoring a connect callback that arrived after disconnect"));
		return;
	}

	if (ReturnCode != EMqttConnectReturnCode::Accepted)
	{
		UE_LOG(LogSparkplugB, Error,
			TEXT("Edge node could not connect, CONNACK code %d"), static_cast<int32>(ReturnCode));
		return;
	}

	// Subscribe before announcing, so a controller reacting to our NBIRTH cannot
	// send a command we would miss.
	Client->Subscribe(BuildTopic(ESparkplugMessageType::NCMD, FString()), EMqttQoS::AtLeastOnce);
	Client->Subscribe(
		BuildTopic(ESparkplugMessageType::DCMD, TEXT("+")), EMqttQoS::AtLeastOnce);

	PublishRebirth();

	bOnline = true;
	OnOnlineStateChanged.Broadcast(true);
}

void USparkplugEdgeNode::HandleMqttDisconnected()
{
	if (bOnline)
	{
		bOnline = false;
		OnOnlineStateChanged.Broadcast(false);
	}
	UE_LOG(LogSparkplugB, Warning, TEXT("Edge node session dropped"));
}

void USparkplugEdgeNode::PublishRebirth()
{
	// Sequence restarts at 0 with NBIRTH; consumers use the reset to detect a
	// new session and rebuild their alias maps.
	Sequence = 0;

	PublishNodeBirth();
	for (const FString& DeviceId : DeviceOrder)
	{
		PublishDeviceBirth(DeviceId);
	}
}

void USparkplugEdgeNode::PublishNodeBirth()
{
	FSparkplugPayload Payload;
	Payload.Timestamp = SparkplugUtils::UtcNowMilliseconds();

	// bdSeq must be first and must match the NDEATH registered as the will.
	Payload.Metrics.Add(FSparkplugMetric::MakeUInt64(
		BirthDeathSequenceMetricName, 0, static_cast<uint64>(BirthDeathSequence)));
	// Advertising Rebirth tells controllers they may ask us to re-announce.
	Payload.Metrics.Add(FSparkplugMetric::MakeBool(RebirthMetricName, 0, false));

	PublishPayload(ESparkplugMessageType::NBIRTH, FString(), Payload, EMqttQoS::AtLeastOnce);
}

void USparkplugEdgeNode::PublishDeviceBirth(const FString& DeviceId)
{
	const TArray<FSparkplugMetric>* BirthMetrics = Devices.Find(DeviceId);
	if (BirthMetrics == nullptr)
	{
		return;
	}

	FSparkplugPayload Payload;
	Payload.Timestamp = SparkplugUtils::UtcNowMilliseconds();
	Payload.Metrics = *BirthMetrics;

	PublishPayload(ESparkplugMessageType::DBIRTH, DeviceId, Payload, EMqttQoS::AtLeastOnce);
}

bool USparkplugEdgeNode::PublishDeviceData(
	const FString& DeviceId, const TArray<FSparkplugMetric>& Metrics)
{
	if (!bOnline)
	{
		return false;
	}
	if (!Devices.Contains(DeviceId))
	{
		UE_LOG(LogSparkplugB, Warning,
			TEXT("DDATA for unregistered device '%s' dropped; register it first so a DBIRTH "
				 "establishes its alias map"), *DeviceId);
		return false;
	}

	FSparkplugPayload Payload;
	Payload.Timestamp = SparkplugUtils::UtcNowMilliseconds();
	Payload.Metrics = Metrics;

	// DDATA is QoS 0: it is high rate and superseded by the next sample.
	return PublishPayload(ESparkplugMessageType::DDATA, DeviceId, Payload, EMqttQoS::AtMostOnce);
}

bool USparkplugEdgeNode::PublishNodeData(const TArray<FSparkplugMetric>& Metrics)
{
	if (!bOnline)
	{
		return false;
	}

	FSparkplugPayload Payload;
	Payload.Timestamp = SparkplugUtils::UtcNowMilliseconds();
	Payload.Metrics = Metrics;

	return PublishPayload(ESparkplugMessageType::NDATA, FString(), Payload, EMqttQoS::AtMostOnce);
}

bool USparkplugEdgeNode::PublishRawString(
	const FString& Topic, const FString& Payload, const EMqttQoS QoS, const bool bRetain)
{
	if (Client == nullptr)
	{
		return false;
	}
	return Client->PublishString(Topic, Payload, QoS, bRetain);
}

bool USparkplugEdgeNode::PublishPayload(
	const ESparkplugMessageType MessageType,
	const FString& DeviceId,
	FSparkplugPayload& Payload,
	const EMqttQoS QoS)
{
	if (Client == nullptr)
	{
		return false;
	}

	Payload.bHasSeq = true;
	Payload.Seq = (MessageType == ESparkplugMessageType::NBIRTH)
		? 0                       // NBIRTH is always seq 0 and resets the run
		: NextSequence();

	if (MessageType == ESparkplugMessageType::NBIRTH)
	{
		// Consume the 0 so the next message is 1.
		Sequence = 1;
	}

	return Client->PublishBytes(
		BuildTopic(MessageType, DeviceId),
		SparkplugProto::EncodePayload(Payload),
		QoS,
		false);
}

uint8 USparkplugEdgeNode::NextSequence()
{
	const uint8 Current = static_cast<uint8>(Sequence & 0xFF);
	Sequence = (Sequence + 1) & 0xFF;
	return Current;
}

void USparkplugEdgeNode::HandleMqttMessage(const FMqttTransportMessage& Message)
{
	if (Client == nullptr)
	{
		return;
	}

	FSparkplugPayload Payload;
	if (!SparkplugProto::DecodePayload(Message.Payload, Payload))
	{
		UE_LOG(LogSparkplugB, Warning,
			TEXT("Could not decode inbound payload on '%s' (%d bytes)"),
			*Message.Topic, Message.Payload.Num());
		return;
	}

	// Topic shape: <ns>/<group>/<verb>/<node>[/<device>]
	TArray<FString> Segments;
	Message.Topic.ParseIntoArray(Segments, TEXT("/"), false);
	if (Segments.Num() < 4)
	{
		return;
	}

	const FString& Verb = Segments[2];

	if (Verb == TEXT("NCMD"))
	{
		// Honour Node Control/Rebirth before handing the payload on.
		for (const FSparkplugMetric& Metric : Payload.Metrics)
		{
			if (Metric.Name == RebirthMetricName && Metric.IntValue != 0)
			{
				UE_LOG(LogSparkplugB, Log, TEXT("Rebirth requested by controller"));
				PublishRebirth();
				break;
			}
		}
		OnNodeCommand.Broadcast(Payload);
	}
	else if (Verb == TEXT("DCMD") && Segments.Num() >= 5)
	{
		OnDeviceCommand.Broadcast(Segments[4], Payload);
	}
}

FString USparkplugEdgeNode::BuildTopic(
	const ESparkplugMessageType MessageType, const FString& DeviceId) const
{
	const FString Verb = SparkplugUtils::MessageTypeToString(MessageType);

	if (DeviceId.IsEmpty())
	{
		return FString::Printf(TEXT("%s/%s/%s/%s"),
			*Config.Namespace, *Config.GroupId, *Verb, *Config.EdgeNodeId);
	}

	return FString::Printf(TEXT("%s/%s/%s/%s/%s"),
		*Config.Namespace, *Config.GroupId, *Verb, *Config.EdgeNodeId, *DeviceId);
}

void USparkplugEdgeNode::BeginDestroy()
{
	if (Client != nullptr)
	{
		Client->Disconnect();
		Client = nullptr;
	}
	Super::BeginDestroy();
}
