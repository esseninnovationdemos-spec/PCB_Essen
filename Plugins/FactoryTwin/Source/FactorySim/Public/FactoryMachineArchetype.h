#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FactorySimTypes.h"

#include "FactoryMachineArchetype.generated.h"

/**
 * A reusable machine type: what it measures and how it behaves.
 *
 * Deliberately carries no identity. No device id, no aliases, no world position.
 * That split is what lets one archetype back many placements -- AOI and SPI are
 * both VisionInspection, differing only in their tags and ranges -- and is the
 * foundation of the archetype library.
 *
 * Adding a new machine type should mean authoring one of these, not writing C++.
 */
UCLASS(BlueprintType)
class FACTORYSIM_API UFactoryMachineArchetype : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Human-readable type name, e.g. "ThermalProcess". */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Archetype")
	FString ArchetypeName;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Archetype", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Archetype")
	EFactoryStateModel StateModel = EFactoryStateModel::Automated;

	/** Publish cadence. Instances may override. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Archetype",
		meta = (ClampMin = "0.0", Units = "s"))
	float DefaultTickIntervalSeconds = 1.0f;

	/** The tags this type publishes, excluding the synthetic ones. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Archetype")
	TArray<FFactoryMetricDefinition> Metrics;

	/** Emit `inspection_result` (PASS/FAIL) and `fail_counter`. Inspection types only. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Archetype|Inspection")
	bool bIsInspectionStation = false;

	/** Probability a cycle fails. Instances may override. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Archetype|Inspection",
		meta = (EditCondition = "bIsInspectionStation", ClampMin = "0.0", ClampMax = "1.0"))
	float DefaultFailRate = 0.02f;

	/** Newton-cooling constant for metrics flagged bThermal. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Archetype|Ramp", meta = (ClampMin = "0.01"))
	float ThermalRampConstant = 3.0f;

	/** How long Warmup lasts before entering Running. Zero skips Warmup. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Archetype|Ramp",
		meta = (ClampMin = "0.0", Units = "s"))
	float WarmupSeconds = 0.0f;

	/** How long Cooldown lasts before returning to Idle. Zero skips Cooldown. */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Archetype|Ramp",
		meta = (ClampMin = "0.0", Units = "s"))
	float CooldownSeconds = 0.0f;

	/** Finds a metric definition by wire name. */
	const FFactoryMetricDefinition* FindMetric(const FString& MetricName) const;

	/**
	 * Every metric name this archetype can emit, configured tags first and then
	 * the synthetic extras, in the order aliases were historically allocated.
	 * Used to validate an instance's alias map and to generate the UNS export.
	 */
	UFUNCTION(BlueprintCallable, Category = "Archetype")
	TArray<FString> GetAllMetricNames() const;

	//~ UPrimaryDataAsset
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
	//~ End UPrimaryDataAsset

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
