#pragma once

#include "CoreMinimal.h"
#include "MqttTransportTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UObject/Object.h"

#include "MqttTransportClient.generated.h"

class FMqttConnection;

/**
 * Blueprint-facing MQTT client.
 *
 * Wraps FMqttConnection and re-broadcasts its worker-thread delegates on the
 * game thread, so Blueprint handlers and UObject access are always safe.
 */
UCLASS(BlueprintType)
class MQTTTRANSPORT_API UMqttTransportClient : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FMqttConnectedSignature, EMqttConnectReturnCode, ReturnCode);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMqttDisconnectedSignature);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FMqttMessageSignature, const FMqttTransportMessage&, Message);

	/** Fires once per connection attempt, with the broker's CONNACK code. */
	UPROPERTY(BlueprintAssignable, Category = "MQTT")
	FMqttConnectedSignature OnConnected;

	/** Fires when an established session drops, for any reason. */
	UPROPERTY(BlueprintAssignable, Category = "MQTT")
	FMqttDisconnectedSignature OnDisconnected;

	UPROPERTY(BlueprintAssignable, Category = "MQTT")
	FMqttMessageSignature OnMessage;

	/** Opens the connection and starts the worker thread. Reconnects automatically unless disabled. */
	UFUNCTION(BlueprintCallable, Category = "MQTT")
	void Connect(const FMqttConnectionOptions& InOptions);

	/** Graceful shutdown: sends DISCONNECT, which suppresses the will. */
	UFUNCTION(BlueprintCallable, Category = "MQTT")
	void Disconnect();

	/** Publishes raw bytes. This is the path Sparkplug B protobuf payloads take. */
	UFUNCTION(BlueprintCallable, Category = "MQTT")
	bool PublishBytes(
		const FString& Topic,
		const TArray<uint8>& Payload,
		EMqttQoS QoS = EMqttQoS::AtMostOnce,
		bool bRetain = false);

	/** Convenience for text payloads. Encodes as UTF-8. */
	UFUNCTION(BlueprintCallable, Category = "MQTT")
	bool PublishString(
		const FString& Topic,
		const FString& Payload,
		EMqttQoS QoS = EMqttQoS::AtMostOnce,
		bool bRetain = false);

	UFUNCTION(BlueprintCallable, Category = "MQTT")
	bool Subscribe(const FString& TopicFilter, EMqttQoS QoS = EMqttQoS::AtMostOnce);

	UFUNCTION(BlueprintCallable, Category = "MQTT")
	bool Unsubscribe(const FString& TopicFilter);

	UFUNCTION(BlueprintPure, Category = "MQTT")
	bool IsConnected() const;

	UFUNCTION(BlueprintPure, Category = "MQTT")
	EMqttConnectionState GetConnectionState() const;

	UFUNCTION(BlueprintPure, Category = "MQTT")
	FString GetClientId() const;

	//~ UObject
	virtual void BeginDestroy() override;
	//~ End UObject

private:
	void BindConnectionDelegates();

	TSharedPtr<FMqttConnection, ESPMode::ThreadSafe> Connection;
	FMqttConnectionOptions Options;
};

/** Owns the live clients so they survive garbage collection for the session's lifetime. */
UCLASS()
class MQTTTRANSPORT_API UMqttTransportSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Creates a client. The subsystem keeps it alive until Destroy or shutdown. */
	UFUNCTION(BlueprintCallable, Category = "MQTT", meta = (DisplayName = "Create MQTT Client"))
	UMqttTransportClient* CreateClient();

	UFUNCTION(BlueprintCallable, Category = "MQTT")
	void DestroyClient(UMqttTransportClient* Client);

	//~ USubsystem
	virtual void Deinitialize() override;
	//~ End USubsystem

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMqttTransportClient>> Clients;
};
