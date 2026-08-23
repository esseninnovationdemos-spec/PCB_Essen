#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FactoryIsa95.h"
#include "FactorySimTypes.h"

#include "FactoryMachineInstance.generated.h"

class UFactoryMachineArchetype;

/**
 * One placed machine: identity, wire aliases, tuning overrides, and where it
 * sits on the 2D floor plan.
 *
 * Aliases are pinned explicitly rather than auto-assigned. The Python layer
 * allocated them from a single global counter in config order, so inserting a
 * machine renumbered everything downstream of it and broke the ClickHouse
 * bridge's mapping. Pinning them here makes the numbering stable under edits;
 * the existing seven devices must keep the numbers they already have.
 */
UCLASS(BlueprintType)
class FACTORYSIM_API UFactoryMachineInstance : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance")
	TObjectPtr<UFactoryMachineArchetype> Archetype;

	/**
	 * Where this machine sits in the equipment hierarchy.
	 *
	 * The identity of record. Both the Sparkplug topic and the UNS path are
	 * derived from it, so they cannot disagree -- see GetDeviceId/GetUnsPath.
	 * Leave it empty only on assets predating the ISA-95 model, which fall back
	 * to the legacy fields below.
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Identity")
	FFactoryIsa95Path Isa95;

	/**
	 * Legacy free-text Sparkplug device id.
	 *
	 * Superseded by Isa95.WorkUnit and consulted only when Isa95 is not filled
	 * in. Kept so assets seeded before the hierarchy existed keep publishing
	 * under the device id their consumers already subscribe to, rather than
	 * silently going quiet on load.
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Legacy")
	FString DeviceId;

	/**
	 * Metric name to wire alias. Must cover every name the archetype can emit,
	 * including the synthetic extras.
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Wire")
	TMap<FString, int64> MetricAliases;

	/** Overrides the archetype cadence. Zero or less uses the archetype value. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance",
		meta = (ClampMin = "0.0", Units = "s"))
	float TickIntervalSecondsOverride = 0.0f;

	/** Negative uses the archetype value. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Inspection",
		meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float FailRateOverride = -1.0f;

	/** Per-instance nominal band overrides, keyed by metric name. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Overrides")
	TMap<FString, FFactoryRange> NominalOverrides;

	/**
	 * Tags this placement publishes on top of its archetype's.
	 *
	 * Lets one archetype cover machines that share behaviour but measure
	 * different things: AOI and SPI are both VisionInspection -- same state
	 * model, same pass/fail handling, same cycle_time_sec -- yet one reports
	 * comp_position_offset_mm / solder_quality_score and the other area / volume.
	 * Without this the library would need one archetype per machine, which is
	 * the flat model the archetype split exists to avoid.
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Overrides")
	TArray<FFactoryMetricDefinition> AdditionalMetrics;

	/** Archetype metrics followed by this instance's additional ones. */
	TArray<FFactoryMetricDefinition> GetEffectiveMetrics() const;

	/** Every metric name this placement can emit, synthetic extras included. */
	UFUNCTION(BlueprintCallable, Category = "Instance")
	TArray<FString> GetAllMetricNames() const;

	/** Values cycled through the `part_id` / `current_part_id` metrics. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Identity")
	TArray<FString> PartIds;

	/** Values cycled through the `conveyor_id` metric. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Identity")
	TArray<FString> CarrierIds;

	/** How long one carrier occupies the station before the id advances. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Identity",
		meta = (ClampMin = "0.0", Units = "s"))
	float CarrierDwellSeconds = 12.0f;

	/**
	 * Legacy free-text UNS path.
	 *
	 * Superseded by Isa95.ToUnsPath(). Same reasoning as DeviceId above: this
	 * is a fallback for assets seeded before the hierarchy existed, not
	 * somewhere to type a new path. Maintaining it by hand alongside the topic
	 * is what let the two drift apart in the first place.
	 */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Legacy")
	FString UnsPath;

	/**
	 * Sparkplug device id: the ISA-95 work unit, or the legacy field when no
	 * hierarchy is set.
	 */
	UFUNCTION(BlueprintPure, Category = "Instance|Identity")
	FString GetDeviceId() const;

	/** Sparkplug group id: Enterprise:Site:Area. Empty without a hierarchy. */
	UFUNCTION(BlueprintPure, Category = "Instance|Identity")
	FString GetGroupId() const;

	/** Sparkplug edge node id: the work centre. Empty without a hierarchy. */
	UFUNCTION(BlueprintPure, Category = "Instance|Identity")
	FString GetEdgeNodeId() const;

	/** Hierarchical UNS path, derived from the ISA-95 levels where present. */
	UFUNCTION(BlueprintPure, Category = "Instance|Identity")
	FString GetUnsPath() const;

	/**
	 * A name for the actor placed for this instance, e.g. "Line1_ReflowOven".
	 *
	 * Not the device id: a device id only has to be unique on its own edge
	 * node, so once several lines run the same stations they all answer to
	 * "ReflowOven" -- fine on the wire, but actor names share one namespace per
	 * level and would collide. Qualifying by work centre keeps them distinct
	 * while still reading as the same station on each line.
	 */
	UFUNCTION(BlueprintPure, Category = "Instance|Identity")
	FString GetLevelLabel() const;

	/** Top-down position in metres, for the 2D floor plan. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Layout")
	FVector2D LayoutPosition = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Layout")
	float LayoutRotationDegrees = 0.0f;

	/** Footprint in metres, width by depth. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Instance|Layout")
	FVector2D LayoutFootprint = FVector2D(1.0, 1.0);

	/** Resolved tick interval, honouring the override. */
	UFUNCTION(BlueprintPure, Category = "Instance")
	float GetTickIntervalSeconds() const;

	/** Resolved fail rate, honouring the override. */
	UFUNCTION(BlueprintPure, Category = "Instance")
	float GetFailRate() const;

	/** Alias for a metric, or 0 when unmapped. */
	UFUNCTION(BlueprintPure, Category = "Instance")
	int64 GetAlias(const FString& MetricName) const;

	/** Nominal band for a metric, honouring any per-instance override. */
	FFactoryRange GetNominalRange(const FFactoryMetricDefinition& Definition) const;

	//~ UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
