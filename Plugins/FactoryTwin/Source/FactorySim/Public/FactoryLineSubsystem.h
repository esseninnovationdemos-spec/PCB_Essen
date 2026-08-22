#pragma once

#include "CoreMinimal.h"
#include "SparkplugEdgeNode.h"
#include "Subsystems/WorldSubsystem.h"

#include "FactoryLineSubsystem.generated.h"

class UFactoryMachineComponent;

/**
 * Owns the Sparkplug edge node for the world and the registry of machines
 * attached to it.
 *
 * Machines register themselves on BeginPlay, so the set of devices announced in
 * the birth sequence is whatever is actually in the level.
 */
UCLASS()
class FACTORYSIM_API UFactoryLineSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFactoryLineOnlineSignature, bool, bOnline);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFactoryNewMaterialSignature, const FString&, LotId);

	/** Fires true once NBIRTH is out and devices may publish. */
	UPROPERTY(BlueprintAssignable, Category = "Factory Twin")
	FFactoryLineOnlineSignature OnLineOnlineChanged;

	/** Fires when a controller sends the `new_material` command. */
	UPROPERTY(BlueprintAssignable, Category = "Factory Twin")
	FFactoryNewMaterialSignature OnNewMaterialRequested;

	/** Connects using project settings. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void StartLine();

	/** Connects with an explicit config, overriding project settings. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void StartLineWithConfig(const FSparkplugEdgeNodeConfig& InConfig);

	/**
	 * Connects using broker details supplied at runtime, keeping the rest of the
	 * settings as configured.
	 *
	 * Mirrors the signature of the legacy `factory_init` Python node so the
	 * existing UI, which collects host/port/user/password from the operator,
	 * keeps working unchanged. Credentials stay out of config this way.
	 *
	 * @return True if a connection attempt was started.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	bool StartLineWithBroker(
		const FString& BrokerHost,
		int32 BrokerPort,
		const FString& BrokerUsername,
		const FString& BrokerPassword);

	/**
	 * Publishes an event on a registered device by its Sparkplug device id.
	 *
	 * Replaces the legacy `factory_publish_event(machine, event)` node for
	 * callers that are not themselves a machine, such as the process manager
	 * reporting line-level events.
	 *
	 * @return True if a machine with that device id was found.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	bool PublishDeviceEvent(const FString& DeviceId, const FString& EventType);

	/** Publishes the death certificates and disconnects. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void StopLine();

	UFUNCTION(BlueprintPure, Category = "Factory Twin")
	bool IsOnline() const;

	/** Current lot / batch identifier, stamped on published material. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin")
	FString GetLotId() const { return LotId; }

	/** Starts a new lot and returns its id. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	FString StartNewLot();

	UFUNCTION(BlueprintPure, Category = "Factory Twin")
	USparkplugEdgeNode* GetEdgeNode() const { return EdgeNode; }

	/** Called by machine components on BeginPlay. */
	void RegisterMachine(UFactoryMachineComponent* Machine);
	void UnregisterMachine(UFactoryMachineComponent* Machine);

	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	/**
	 * Auto-start happens here rather than in Initialize.
	 *
	 * Subsystem Initialize runs before any actor's BeginPlay, so starting there
	 * announced the node with zero devices registered. OnWorldBeginPlay fires
	 * after every actor has begun play, by which point the machine components
	 * have registered and the birth sequence describes the real line.
	 */
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	//~ End USubsystem

private:
	UFUNCTION()
	void HandleEdgeNodeOnline(bool bOnline);

	UFUNCTION()
	void HandleNodeCommand(const FSparkplugPayload& Payload);

	/** Announces every registered machine as a Sparkplug device. */
	void RegisterDevicesWithEdgeNode();

	UPROPERTY(Transient)
	TObjectPtr<USparkplugEdgeNode> EdgeNode;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UFactoryMachineComponent>> Machines;

	FString LotId;
};
