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

	/**
	 * Requests one board be started: opens a lot, stamps NEW_MATERIAL on every
	 * machine, and broadcasts OnNewMaterialRequested.
	 *
	 * Both triggers funnel through here -- an inbound NCMD `new_material` and the
	 * auto-production timer -- so a Blueprint only has to bind
	 * OnNewMaterialRequested once to serve both.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void RequestNewMaterial();

	/**
	 * Begins releasing boards on a timer.
	 *
	 * @param IntervalSeconds Gap between releases. Zero or less uses the
	 *                        configured default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin|Auto Production")
	void StartAutoProduction(float IntervalSeconds = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Factory Twin|Auto Production")
	void StopAutoProduction();

	UFUNCTION(BlueprintPure, Category = "Factory Twin|Auto Production")
	bool IsAutoProductionRunning() const;

	/** Interval currently in use. Zero when auto production is stopped. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin|Auto Production")
	float GetAutoProductionInterval() const { return AutoProductionInterval; }

	/** Boards released since auto production last started. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin|Auto Production")
	int32 GetBoardsReleased() const { return BoardsReleased; }

	/**
	 * The first edge node, for callers that predate the per-line split.
	 *
	 * With one node per production line this is no longer "the" node; anything
	 * publishing on a specific device wants FindEdgeNodeForMachine instead.
	 */
	UFUNCTION(BlueprintPure, Category = "Factory Twin")
	USparkplugEdgeNode* GetEdgeNode() const;

	/** Every edge node this world has brought up, one per ISA-95 work centre. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin")
	TArray<USparkplugEdgeNode*> GetEdgeNodes() const;

	/** The edge node carrying this machine's traffic, or null if it has none. */
	USparkplugEdgeNode* FindEdgeNodeForMachine(const UFactoryMachineComponent* Machine) const;

	/**
	 * The registered machine matching a device id or a full UNS path.
	 *
	 * Anything containing a '/' is matched against the UNS path, everything else
	 * against the Sparkplug device id. Both are needed: a device id is unique
	 * only within its edge node, so once several lines run the same station
	 * names, "ReflowOven" alone is ambiguous and the caller must qualify it as
	 * InnoLab/Essen/SMT/Line2/ReflowOven.
	 *
	 * @return The machine, or null if nothing matches.
	 */
	UFUNCTION(BlueprintPure, Category = "Factory Twin")
	UFactoryMachineComponent* FindMachine(const FString& DeviceIdOrUnsPath) const;

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

	/** Brings up one edge node per work centre and announces its devices. */
	void BeginEdgeNodeSession();

	/**
	 * Which edge node a machine belongs on: "<group>|<node>".
	 *
	 * Machines with no ISA-95 path fall back to the configured group and node,
	 * so a level that has not been reseeded still comes up on a single node
	 * exactly as it did before.
	 */
	FString GetEdgeNodeKey(const UFactoryMachineComponent* Machine) const;

	/** Config for one node, derived from the template plus the key's identity. */
	FSparkplugEdgeNodeConfig BuildConfigForKey(const FString& Key) const;

	/** Recomputes the aggregate online state and broadcasts it if it changed. */
	void RefreshOnlineState();

	/** True when this edge node already carries a device with that id. */
	bool HasDeviceId(const FString& Key, const FString& DeviceId) const;

	/** Keyed by "<group>|<node>". One per ISA-95 work centre in the level. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<USparkplugEdgeNode>> EdgeNodes;

	/** Last aggregate state broadcast, so the delegate only fires on change. */
	bool bWasOnline = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UFactoryMachineComponent>> Machines;

	FString LotId;

	/** Held between StartLineWithConfig and the deferred BeginEdgeNodeSession. */
	FSparkplugEdgeNodeConfig PendingConfig;

	FTimerHandle AutoProductionTimer;
	float AutoProductionInterval = 0.0f;
	int32 BoardsReleased = 0;
};
