#pragma once

#include "CoreMinimal.h"

#include "MqttTransportTypes.generated.h"

MQTTTRANSPORT_API DECLARE_LOG_CATEGORY_EXTERN(LogMqttTransport, Log, All);

/**
 * Supplies a will payload immediately before each CONNECT, reconnects included.
 * Invoked on the transport worker thread.
 *
 * Exists because Sparkplug requires a fresh bdSeq per MQTT session and the will
 * is registered during the handshake, so a payload captured once would go stale
 * the first time the client reconnected.
 */
DECLARE_DELEGATE_RetVal(TArray<uint8>, FMqttWillPayloadProvider);

/** MQTT 3.1.1 quality of service. */
UENUM(BlueprintType)
enum class EMqttQoS : uint8
{
	/** Fire and forget. Used for Sparkplug DDATA. */
	AtMostOnce = 0 UMETA(DisplayName = "At Most Once (0)"),
	/** Acknowledged delivery, may duplicate. Used for Sparkplug BIRTH/DEATH. */
	AtLeastOnce = 1 UMETA(DisplayName = "At Least Once (1)"),
	/** Exactly-once four-way handshake. */
	ExactlyOnce = 2 UMETA(DisplayName = "Exactly Once (2)")
};

UENUM(BlueprintType)
enum class EMqttConnectionState : uint8
{
	Disconnected,
	Connecting,
	Connected,
	/** Waiting out the backoff before another connect attempt. */
	Reconnecting
};

/** CONNACK return codes, MQTT 3.1.1 section 3.2.2.3. */
UENUM(BlueprintType)
enum class EMqttConnectReturnCode : uint8
{
	Accepted = 0,
	UnacceptableProtocolVersion = 1,
	IdentifierRejected = 2,
	ServerUnavailable = 3,
	BadUsernameOrPassword = 4,
	NotAuthorized = 5,
	/** Never sent by a broker; set locally when the socket never came up. */
	ConnectionFailed = 200
};

/**
 * Last Will and Testament, attached to the CONNECT packet.
 *
 * This is the reason this module exists rather than using the engine's MQTT
 * plugin: Sparkplug B requires the NDEATH certificate to be registered as the
 * will at connect time, and it is a binary protobuf payload. The engine plugin
 * exposes no will at all, and its internal one is an FString.
 */
USTRUCT(BlueprintType)
struct MQTTTRANSPORT_API FMqttWill
{
	GENERATED_BODY()

	/** When false, no will flag is set on CONNECT. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT|Will")
	bool bEnabled = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT|Will")
	FString Topic;

	/** Raw bytes. Never round-trip this through FString: protobuf contains embedded nulls. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT|Will")
	TArray<uint8> Payload;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT|Will")
	EMqttQoS QoS = EMqttQoS::AtLeastOnce;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT|Will")
	bool bRetain = false;
};

USTRUCT(BlueprintType)
struct MQTTTRANSPORT_API FMqttConnectionOptions
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	FString Host = TEXT("localhost");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	int32 Port = 1883;

	/** Must be unique on the broker. Empty generates one from the machine name. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	FString ClientId;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	FString Username;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	FString Password;

	/** Broker drops the connection after 1.5x this with no traffic. 0 disables. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT", meta = (ClampMin = "0"))
	int32 KeepAliveSeconds = 60;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	bool bCleanSession = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	FMqttWill Will;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT|Reconnect", meta = (ClampMin = "0.1"))
	float ReconnectDelayMinSeconds = 1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT|Reconnect", meta = (ClampMin = "0.1"))
	float ReconnectDelayMaxSeconds = 30.0f;

	/** False stops the client retrying after a drop. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT|Reconnect")
	bool bAutoReconnect = true;
};

USTRUCT(BlueprintType)
struct MQTTTRANSPORT_API FMqttTransportMessage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	FString Topic;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	TArray<uint8> Payload;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	EMqttQoS QoS = EMqttQoS::AtMostOnce;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "MQTT")
	bool bRetain = false;

	FMqttTransportMessage() = default;

	FMqttTransportMessage(FString InTopic, TArray<uint8> InPayload, const EMqttQoS InQoS, const bool bInRetain)
		: Topic(MoveTemp(InTopic))
		, Payload(MoveTemp(InPayload))
		, QoS(InQoS)
		, bRetain(bInRetain)
	{
	}

	/** Interprets the payload as UTF-8. Only safe for text payloads, never for protobuf. */
	FString GetPayloadAsString() const;
};
