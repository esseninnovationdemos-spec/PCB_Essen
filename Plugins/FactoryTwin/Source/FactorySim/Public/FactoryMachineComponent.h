#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "FactorySimTypes.h"
#include "SparkplugTypes.h"

#include "FactoryMachineComponent.generated.h"

class UFactoryMachineInstance;
class UFactoryLineSubsystem;

/** Per-metric mutable state carried between samples. */
USTRUCT()
struct FFactoryMetricRuntime
{
	GENERATED_BODY()

	/** Last emitted value. */
	double CurrentValue = 0.0;

	/**
	 * Value held when the current state was entered, and the point a Warmup or
	 * Cooldown ramp interpolates from.
	 *
	 * Ramping from a fixed nominal or idle value instead would make the metric
	 * jump the moment the state changed -- an oven that faulted at its fault
	 * value would snap to the middle of its nominal band before starting to cool.
	 */
	double RampStartValue = 0.0;

	/** Accumulated wear offset. */
	double Drift = 0.0;

	/** Seconds remaining in an active spike; zero when not spiking. */
	double SpikeRemaining = 0.0;

	/** Countdown to the next spike, drawn from an exponential distribution. */
	double NextSpikeIn = 0.0;

	/** Phase accumulator for motion profiles. */
	double MotionTime = 0.0;
};

/**
 * Drives one simulated machine and publishes it as a Sparkplug device.
 *
 * Drop this on a machine Blueprint, point it at an instance asset, and the
 * machine is fully instrumented -- no per-machine code. This is the surface the
 * archetype library is built on.
 *
 * Unreal is the master clock. Running never times out by itself; the owning
 * Blueprint calls StartCycle and CompleteCycle in step with its animation. That
 * mirrors the deliberate design of the Python sim_engine it replaces.
 */
UCLASS(ClassGroup = (FactoryTwin), meta = (BlueprintSpawnableComponent),
	HideCategories = (Variable, Tags, ComponentReplication, Activation, Cooking, AssetUserData))
class FACTORYSIM_API UFactoryMachineComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFactoryMachineComponent();

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FFactoryStateChangedSignature,
		EFactoryMachineState, OldState,
		EFactoryMachineState, NewState);

	/** Which machine this is. Without it the component does nothing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Factory Twin")
	TObjectPtr<UFactoryMachineInstance> Instance;

	/** Announce and publish automatically once the line comes online. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Factory Twin")
	bool bAutoRegister = true;

	UPROPERTY(BlueprintAssignable, Category = "Factory Twin")
	FFactoryStateChangedSignature OnStateChanged;

	// --- Blueprint control surface ---------------------------------------
	// These replace the factory_* Python nodes one-for-one.

	/** Begins a cycle: enters Warmup, or Running when the archetype has no warmup. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void StartCycle();

	/**
	 * Ends a cycle. Publishes the cycle-complete metrics, then enters Cooldown,
	 * or Idle when the archetype has no cooldown.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void CompleteCycle();

	/** Publishes a DDATA sample stamped with this event name. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void PublishEvent(const FString& EventType);

	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void SetMachineState(EFactoryMachineState NewState);

	UFUNCTION(BlueprintPure, Category = "Factory Twin")
	EFactoryMachineState GetMachineState() const { return State; }

	/** Records a pass/fail result, bumping fail_counter on a failure. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void ReportInspection(bool bPassed);

	/** Rolls pass/fail from the configured fail rate and reports it. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	bool RollInspection();

	/** Sets the value published as part_id / current_part_id. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void SetPartId(const FString& InPartId);

	/** Sets the value published as conveyor_id. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin")
	void SetCarrierId(const FString& InCarrierId);

	/** Current value of a metric, for driving visuals from the same numbers. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin")
	double GetMetricValue(const FString& MetricName) const;

	/** Seconds elapsed in the current cycle. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin")
	float GetCycleElapsedSeconds() const;

	/** The Sparkplug birth metric set, establishing this device's alias map. */
	TArray<FSparkplugMetric> BuildBirthMetrics() const;

	//~ UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(
		float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

private:
	/** Samples every due metric and publishes one DDATA. */
	void PublishSample(const FString& EventType, bool bIncludeCycleCompleteMetrics);

	/** Advances degradation and ramp state without publishing. */
	void AdvanceRuntime(float DeltaTime);

	double SampleMetric(const FFactoryMetricDefinition& Definition, FFactoryMetricRuntime& Runtime) const;
	double ApplyDegradation(
		const FFactoryMetricDefinition& Definition, FFactoryMetricRuntime& Runtime, double Value) const;

	/** Builds a Sparkplug metric with the instance's pinned alias attached. */
	FSparkplugMetric MakeMetric(const FString& MetricName, ESparkplugDataType DataType) const;

	UFactoryLineSubsystem* GetLineSubsystem() const;

	UFUNCTION()
	void HandleLineOnline(bool bOnline);

	UPROPERTY(Transient)
	EFactoryMachineState State = EFactoryMachineState::Idle;

	/**
	 * Archetype metrics plus this instance's additions, resolved once at
	 * BeginPlay.
	 *
	 * Cached because the definition list is immutable once play starts, and
	 * rebuilding it per tick meant deep-copying FString-bearing structs for
	 * every machine, every frame.
	 */
	TArray<FFactoryMetricDefinition> EffectiveMetrics;

	/** Per-metric runtime, keyed by metric name. */
	TMap<FString, FFactoryMetricRuntime> MetricRuntime;

	/** Seconds until the next scheduled DDATA. */
	float PublishCountdown = 0.0f;

	/** Seconds spent in the current state, driving warmup and cooldown ramps. */
	float StateElapsed = 0.0f;

	/** Seconds since StartCycle. */
	float CycleElapsed = 0.0f;

	/** Value the current cycle reports as its duration. */
	float LastCycleSeconds = 0.0f;

	FString CurrentPartId;
	FString CurrentCarrierId;
	float CarrierElapsed = 0.0f;
	int32 CarrierIndex = 0;
	int32 PartIndex = 0;

	FString LastInspectionResult;
	int32 FailCounter = 0;
	/** Set once a result is reported, so CompleteCycle does not overwrite it. */
	bool bInspectionReportedThisCycle = false;

	/** True between StartCycle and CompleteCycle; guards stale cycle times. */
	bool bCycleInProgress = false;

	bool bRegistered = false;
};
