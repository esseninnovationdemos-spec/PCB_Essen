#pragma once

#include "CoreMinimal.h"
#include "MqttTransportTypes.h"
#include "SparkplugTypes.h"
#include "UObject/Object.h"

#include <atomic>

#include "SparkplugEdgeNode.generated.h"

class UMqttTransportClient;

/** Identity and connection settings for one Sparkplug edge node. */
USTRUCT(BlueprintType)
struct SPARKPLUGB_API FSparkplugEdgeNodeConfig
{
	GENERATED_BODY()

	/** Topic namespace. Always "spBv1.0" for Sparkplug B. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	FString Namespace = TEXT("spBv1.0");

	/** Wire-visible: changing this breaks every downstream consumer. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	FString GroupId = TEXT("SMT_Line");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	FString EdgeNodeId = TEXT("Cluj");

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Sparkplug")
	FMqttConnectionOptions Mqtt;
};

/**
 * One Sparkplug B edge node: owns the MQTT session, the sequence numbers, and
 * the birth/death certificate lifecycle.
 *
 * Session order is mandated by the spec and enforced here:
 *   1. allocate bdSeq
 *   2. register NDEATH (carrying that bdSeq) as the MQTT will  <- before connect
 *   3. connect
 *   4. reset seq, publish NBIRTH with seq=0
 *   5. publish one DBIRTH per registered device
 *   6. subscribe NCMD/DCMD
 *   7. stream DDATA, seq incrementing mod 256
 *
 * Register devices before calling Connect so their DBIRTHs go out in the right
 * order; registering later is allowed and publishes a DBIRTH immediately.
 */
UCLASS(BlueprintType)
class SPARKPLUGB_API USparkplugEdgeNode : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FSparkplugNodeCommandSignature, const FSparkplugPayload&, Payload);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FSparkplugDeviceCommandSignature, const FString&, DeviceId, const FSparkplugPayload&, Payload);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FSparkplugOnlineSignature, bool, bOnline);

	/** NCMD received on this node's command topic. */
	UPROPERTY(BlueprintAssignable, Category = "Sparkplug")
	FSparkplugNodeCommandSignature OnNodeCommand;

	/** DCMD received for one of this node's devices. */
	UPROPERTY(BlueprintAssignable, Category = "Sparkplug")
	FSparkplugDeviceCommandSignature OnDeviceCommand;

	/** Fires true once NBIRTH has gone out, false when the session drops. */
	UPROPERTY(BlueprintAssignable, Category = "Sparkplug")
	FSparkplugOnlineSignature OnOnlineStateChanged;

	/**
	 * Declares a device and the metrics its DBIRTH advertises.
	 * The birth metric list establishes the name/alias map every later DDATA
	 * refers to, so it must cover every metric the device will ever publish.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sparkplug")
	void RegisterDevice(const FString& DeviceId, const TArray<FSparkplugMetric>& BirthMetrics);

	UFUNCTION(BlueprintCallable, Category = "Sparkplug")
	void UnregisterDevice(const FString& DeviceId);

	UFUNCTION(BlueprintCallable, Category = "Sparkplug")
	void Connect(const FSparkplugEdgeNodeConfig& InConfig);

	/** Clean shutdown: publishes DDEATH for each device then NDEATH, and suppresses the will. */
	UFUNCTION(BlueprintCallable, Category = "Sparkplug")
	void Disconnect();

	/** Publishes DDATA for a registered device. */
	UFUNCTION(BlueprintCallable, Category = "Sparkplug")
	bool PublishDeviceData(const FString& DeviceId, const TArray<FSparkplugMetric>& Metrics);

	/** Publishes NDATA for the node itself. */
	UFUNCTION(BlueprintCallable, Category = "Sparkplug")
	bool PublishNodeData(const TArray<FSparkplugMetric>& Metrics);

	/** Re-sends NBIRTH and every DBIRTH. Triggered by a Node Control/Rebirth command. */
	UFUNCTION(BlueprintCallable, Category = "Sparkplug")
	void PublishRebirth();

	/**
	 * Publishes an arbitrary payload on this node's MQTT connection, bypassing
	 * the Sparkplug framing entirely.
	 *
	 * Exists so the project's separate UNS stream -- plain JSON on its own topic
	 * tree, which predates this plugin -- can share one broker connection
	 * instead of opening a second. Do not use it for Sparkplug traffic; that
	 * must go through the publish methods above so sequence numbers stay intact.
	 */
	UFUNCTION(BlueprintCallable, Category = "Sparkplug|Raw")
	bool PublishRawString(
		const FString& Topic,
		const FString& Payload,
		EMqttQoS QoS = EMqttQoS::AtMostOnce,
		bool bRetain = false);

	UFUNCTION(BlueprintPure, Category = "Sparkplug")
	bool IsOnline() const { return bOnline; }

	/** Current sequence number, 0-255. */
	UFUNCTION(BlueprintPure, Category = "Sparkplug")
	int32 GetSequence() const { return Sequence; }

	/** Birth/death sequence for this session, matching NBIRTH and NDEATH. */
	UFUNCTION(BlueprintPure, Category = "Sparkplug")
	int64 GetBirthDeathSequence() const { return BirthDeathSequence.load(std::memory_order_relaxed); }

	/** Builds a topic for this node, e.g. spBv1.0/SMT_Line/DDATA/Cluj/REFLOW_OVEN. */
	UFUNCTION(BlueprintPure, Category = "Sparkplug")
	FString BuildTopic(ESparkplugMessageType MessageType, const FString& DeviceId) const;

	//~ UObject
	virtual void BeginDestroy() override;
	//~ End UObject

private:
	UFUNCTION()
	void HandleMqttConnected(EMqttConnectReturnCode ReturnCode);

	UFUNCTION()
	void HandleMqttDisconnected();

	UFUNCTION()
	void HandleMqttMessage(const FMqttTransportMessage& Message);

	/** Consumes and returns the next sequence number, wrapping at 256. */
	uint8 NextSequence();

	TArray<uint8> BuildDeathPayload() const;

	/** Bumps bdSeq and builds the will. Called by the transport before each CONNECT. */
	TArray<uint8> BuildSessionDeathPayload();
	void PublishNodeBirth();
	void PublishDeviceBirth(const FString& DeviceId);
	bool PublishPayload(
		ESparkplugMessageType MessageType,
		const FString& DeviceId,
		FSparkplugPayload& Payload,
		EMqttQoS QoS);

	UPROPERTY(Transient)
	TObjectPtr<UMqttTransportClient> Client;

	FSparkplugEdgeNodeConfig Config;

	/** DeviceId -> the metric set its DBIRTH advertises. */
	TMap<FString, TArray<FSparkplugMetric>> Devices;
	/** Registration order, so DBIRTHs go out deterministically. */
	TArray<FString> DeviceOrder;

	int32 Sequence = 0;
	/** Atomic: bumped on the transport worker, read when NBIRTH is built. */
	std::atomic<int64> BirthDeathSequence{ 0 };
	bool bOnline = false;
};
