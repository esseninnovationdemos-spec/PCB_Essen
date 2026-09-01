#pragma once

#include "CoreMinimal.h"
#include "FactorySimTypes.h"
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

	// --- External control -------------------------------------------------

	/**
	 * Switches who sequences the stations, propagating it to every machine.
	 *
	 * Also published per station as the `control_mode` metric, so an operator
	 * screen never has to infer the mode from behaviour.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin|Control")
	void SetControlMode(EFactoryControlMode NewMode);

	UFUNCTION(BlueprintPure, Category = "Factory Twin|Control")
	EFactoryControlMode GetControlMode() const { return ControlMode; }

	/** True when the followed PLC has been heard from inside the timeout. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin|Control")
	bool IsPlcOnline() const;

	/** Edge node id being followed, or empty when following nothing. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin|Control")
	FString GetPlcEdgeNodeId() const { return PlcEdgeNodeId; }

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

	/** DCMD aimed at one of our devices, from a Sparkplug primary application. */
	UFUNCTION()
	void HandleDeviceCommand(const FString& DeviceId, const FSparkplugPayload& Payload);

	/** DATA from the followed PLC edge node. */
	UFUNCTION()
	void HandleForeignNodeData(
		ESparkplugMessageType MessageType,
		const FString& EdgeNodeId,
		const FString& DeviceId,
		const FSparkplugPayload& Payload);

	/**
	 * Applies one command metric to one station.
	 *
	 * @param TargetKey      Edge-bookkeeping key, unique per station+command.
	 * @param bEdgeSensitive True when reading a followed PLC state stream, where
	 *                       the same tag set is republished every scan and only
	 *                       a change means anything. False for DCMD, where each
	 *                       message is itself a discrete command -- two
	 *                       identical writes are two triggers, and treating them
	 *                       as an edge would silently drop the second.
	 * @param bSeedOnly      True for BIRTH: record the value but do not act on
	 *                       it, so a birth certificate carrying Trigger=1 does
	 *                       not fire a cycle on every reconnect.
	 */
	void ApplyStationCommand(
		UFactoryMachineComponent* Machine,
		const FString& TargetKey,
		const FString& CommandName,
		const FSparkplugMetric& Metric,
		bool bEdgeSensitive,
		bool bSeedOnly);

	/**
	 * Splits a followed PLC tag name into the station it addresses and the
	 * command it carries.
	 *
	 * Needed because a PLC publishes flat tags and cannot name them freely: PAC
	 * Control permits only alphanumerics and underscore, so a groov EPIC sends
	 * `Line1_PIN_INSERTION_trigger` where a broker-native controller would send
	 * `Line1/PIN_INSERTION/trigger`. Splitting on the separator cannot tell
	 * those apart, because the station ids contain underscores themselves --
	 * `PIN_INSERTION` would arrive as two segments.
	 *
	 * So the name is read from the right: strip a recognised command word off
	 * the end, match what remains against the station ids actually registered,
	 * longest first so `PIN_CHECK` is not shadowed by a shorter id ending the
	 * same way -- then require what is *still* left to name that station's edge
	 * node. That last step is not optional here: this plant runs three lines
	 * with identical station names, so `LOADER` alone matches three machines and
	 * a `Line1_` tag would be as likely to cycle Line3.
	 *
	 * @param OutMachine   The station addressed, or null for a line-level
	 *                     command such as new_material.
	 * @param OutTargetKey Edge-bookkeeping key, qualified by edge node so the
	 *                     three lines' identically named stations do not share
	 *                     one trigger history.
	 * @return False when the name carries no command word at all, which is the
	 *         normal case for a PLC also publishing ordinary process tags.
	 */
	bool ResolveCommandTarget(
		const FString& MetricName,
		UFactoryMachineComponent*& OutMachine,
		FString& OutTargetKey,
		FString& OutCommand) const;

	/** Applies a node-level command: new material, or a mode change. */
	void ApplyLineCommand(
		const FString& CommandName,
		const FSparkplugMetric& Metric,
		bool bEdgeSensitive);

	/** Starts or stops following the configured PLC node, per current settings. */
	void RefreshPlcFollowing();

	/** Drops the line back to local takt when the PLC has gone quiet. */
	void CheckPlcWatchdog();

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

	/** Who currently sequences the stations. */
	EFactoryControlMode ControlMode = EFactoryControlMode::Local;

	/** Peer edge node being followed. Empty means DCMD-only. */
	FString PlcEdgeNodeId;

	/** Platform seconds when the PLC last spoke. Zero means never. */
	double LastPlcMessageSeconds = 0.0;

	/**
	 * Last value seen per "<device>|<command>".
	 *
	 * Triggers are edge-sensitive, not level-sensitive: a PLC republishing its
	 * whole tag set every scan would otherwise re-fire every station on every
	 * message.
	 */
	TMap<FString, int64> LastCommandValues;

	FTimerHandle PlcWatchdogTimer;

	FTimerHandle AutoProductionTimer;
	float AutoProductionInterval = 0.0f;
	int32 BoardsReleased = 0;
};
