#pragma once

#include "CoreMinimal.h"
#include "SparkplugTypes.h"

#include "FactorySimTypes.generated.h"

FACTORYSIM_API DECLARE_LOG_CATEGORY_EXTERN(LogFactorySim, Log, All);

/**
 * Machine state.
 *
 * The numeric values are published as the `state_code` metric and are consumed
 * downstream, so they must keep matching the legend the Python layer
 * established: 0=IDLE 1=RUNNING 2=FAULT 3=BLOCKED 4=WARMUP 5=COOLDOWN.
 * Do not reorder.
 */
UENUM(BlueprintType)
enum class EFactoryMachineState : uint8
{
	Idle     = 0,
	Running  = 1,
	Fault    = 2,
	Blocked  = 3,
	Warmup   = 4,
	Cooldown = 5
};

/** Which behaviour drives state transitions. */
UENUM(BlueprintType)
enum class EFactoryStateModel : uint8
{
	/**
	 * Unreal animation is the clock. Running never times out on its own; it
	 * exits only when the Blueprint calls CompleteCycle. Preserves the deliberate
	 * design of the Python sim_engine (dwell of 86400s on Idle and Running).
	 */
	Automated,

	/**
	 * Operator-paced station, e.g. the PCB cleaner and solder paste station.
	 * Longer and more variable cycles, no warmup/cooldown ramp, and typically a
	 * reduced tag set.
	 */
	Manual,

	/** Continuous transport. Runs whenever the line runs. */
	Conveyor,

	/** Accumulation buffer. */
	Buffer
};

/** Shape applied to a value while Running, for axes that sweep rather than settle. */
UENUM(BlueprintType)
enum class EFactoryMotionProfile : uint8
{
	None,
	Sine,
	Sawtooth
};

/** When a metric is sampled and sent. */
UENUM(BlueprintType)
enum class EFactoryPublishTrigger : uint8
{
	/** Every tick interval. */
	Tick,
	/** Only when a cycle finishes, e.g. cycle_time_sec. */
	CycleComplete
};

/** Where a spike drives the value. */
UENUM(BlueprintType)
enum class EFactorySpikeTarget : uint8
{
	FaultValue,
	AbsoluteMax,
	AbsoluteMin
};

USTRUCT(BlueprintType)
struct FACTORYSIM_API FFactoryRange
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Range")
	double Min = 0.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Range")
	double Max = 0.0;

	FFactoryRange() = default;
	FFactoryRange(const double InMin, const double InMax) : Min(InMin), Max(InMax) {}

	double Clamp(const double Value) const { return FMath::Clamp(Value, Min, Max); }
	double Lerp(const double Alpha) const { return FMath::Lerp(Min, Max, Alpha); }
	double Midpoint() const { return (Min + Max) * 0.5; }
};

/** Gaussian noise around the current value. */
USTRUCT(BlueprintType)
struct FACTORYSIM_API FFactoryFluctuation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation")
	bool bEnabled = false;

	/** Standard deviation as a percentage of the nominal span. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation",
		meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	double NoisePercent = 1.0;
};

/** Slow monotonic wander, modelling wear. */
USTRUCT(BlueprintType)
struct FACTORYSIM_API FFactoryDrift
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation")
	bool bEnabled = false;

	/** Units of drift accumulated per hour of running time. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation",
		meta = (EditCondition = "bEnabled"))
	double RatePerHour = 0.0;

	/** Drift saturates here. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation",
		meta = (EditCondition = "bEnabled"))
	double MaxDrift = 0.0;

	/** Clears accumulated drift whenever the machine returns to Idle. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation",
		meta = (EditCondition = "bEnabled"))
	bool bResetOnIdle = false;
};

/** Poisson-distributed excursions. */
USTRUCT(BlueprintType)
struct FACTORYSIM_API FFactorySpike
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation")
	bool bEnabled = false;

	/** Mean gap between spikes. Drives a Poisson process, so gaps vary. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation",
		meta = (EditCondition = "bEnabled", ClampMin = "0.01"))
	double MeanMinutesBetween = 30.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation",
		meta = (EditCondition = "bEnabled", ClampMin = "0.0"))
	double DurationSeconds = 5.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation",
		meta = (EditCondition = "bEnabled"))
	EFactorySpikeTarget SpikeTo = EFactorySpikeTarget::FaultValue;

	/** Also forces the machine into Fault for the duration. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Degradation",
		meta = (EditCondition = "bEnabled"))
	bool bForcesState = false;
};

/**
 * One tag a machine publishes.
 *
 * Lives on the archetype, so it carries no alias: aliases are per-placement and
 * are pinned on the instance instead.
 */
USTRUCT(BlueprintType)
struct FACTORYSIM_API FFactoryMetricDefinition
{
	GENERATED_BODY()

	/** Wire-visible metric name, e.g. "oven_temp_c". */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	FString Name;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	ESparkplugDataType DataType = ESparkplugDataType::Float;

	/** Engineering unit. Documentation and the UNS export; not sent on the wire. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	FString Unit;

	/** Band the value occupies during normal running. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	FFactoryRange Nominal;

	/** Hard limits; generated values are clamped to this. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	FFactoryRange Absolute;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	double IdleValue = 0.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	double FaultValue = 0.0;

	/** Value held during Warmup. Ignored unless bUseWarmupValue. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	bool bUseWarmupValue = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric",
		meta = (EditCondition = "bUseWarmupValue"))
	double WarmupValue = 0.0;

	/**
	 * Ramp exponentially rather than linearly during Warmup and Cooldown.
	 * Newton cooling; correct for oven and laser temperatures.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	bool bThermal = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	EFactoryMotionProfile MotionProfile = EFactoryMotionProfile::None;

	/** Period of the motion profile sweep. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric",
		meta = (EditCondition = "MotionProfile != EFactoryMotionProfile::None", ClampMin = "0.01"))
	double MotionPeriodSeconds = 2.0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric")
	EFactoryPublishTrigger PublishOn = EFactoryPublishTrigger::Tick;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric|Degradation")
	FFactoryFluctuation Fluctuation;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric|Degradation")
	FFactoryDrift Drift;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Metric|Degradation")
	FFactorySpike Spike;
};

/**
 * The synthetic metrics every device publishes alongside its configured tags.
 * Names are wire-visible and must not drift.
 */
namespace FactorySyntheticMetrics
{
	FACTORYSIM_API extern const FString StateCode;
	FACTORYSIM_API extern const FString EventType;
	FACTORYSIM_API extern const FString InspectionResult;
	FACTORYSIM_API extern const FString FailCounter;
	FACTORYSIM_API extern const FString ConveyorId;
	FACTORYSIM_API extern const FString CurrentPartId;
	FACTORYSIM_API extern const FString PartId;

	/** In the order the Python layer allocated aliases, which must be preserved. */
	FACTORYSIM_API TArray<FString> GetAll();
}

/**
 * Who decides when a station cycles.
 *
 * Wire-visible: published as the `control_mode` metric so an operator screen can
 * show which one is in force without inferring it from behaviour.
 */
UENUM(BlueprintType)
enum class EFactoryControlMode : uint8
{
	/** The line runs itself on takt. A controller may still gate stations. */
	Local = 0,
	/** Stations cycle only when an external controller triggers them. */
	External = 1
};

/**
 * Metrics describing this station's controllability, published alongside the
 * process metrics so a controller can close a handshake.
 *
 * Deliberately absent from FactorySyntheticMetrics::GetAll(): that list fixes
 * the historical alias allocation order, and adding to it would renumber every
 * alias after the insertion point and break the downstream mapping. These carry
 * alias 0, which the encoder omits, so they travel by name only.
 */
namespace FactoryControlMetrics
{
	/** Idle, enabled, unheld and unfaulted -- a trigger would be accepted now. */
	FACTORYSIM_API extern const FString Ready;
	/** A cycle is in progress. */
	FACTORYSIM_API extern const FString Busy;
	FACTORYSIM_API extern const FString StationEnabled;
	FACTORYSIM_API extern const FString ControlMode;
}

/**
 * Command metric names honoured on an inbound DCMD, and recognised as the
 * trailing segment of a followed PLC's tag names.
 *
 * The "Station Control/" prefix mirrors the spec's own "Node Control/" reserved
 * names, so a controller browsing the birth certificate can tell commands from
 * process data at a glance.
 */
namespace FactoryControlCommands
{
	/** Rising edge starts one cycle. Bool pulse or monotonic counter both work. */
	FACTORYSIM_API extern const FString Trigger;
	/** Zero blocks the station; non-zero releases it. */
	FACTORYSIM_API extern const FString Enable;
	/** Non-zero holds the station after the current cycle finishes. */
	FACTORYSIM_API extern const FString Hold;
	/** Rising edge clears a fault. */
	FACTORYSIM_API extern const FString Reset;
	/** Node-level: releases one board to the line. */
	FACTORYSIM_API extern const FString NewMaterial;
	/** Node-level: "local" or "external". */
	FACTORYSIM_API extern const FString Mode;
}

/** Event names published in the `event_type` metric. */
namespace FactoryEventTypes
{
	FACTORYSIM_API extern const FString Idle;
	FACTORYSIM_API extern const FString PhaseStarted;
	FACTORYSIM_API extern const FString CycleStarted;
	FACTORYSIM_API extern const FString PhaseRunning;
	FACTORYSIM_API extern const FString CycleComplete;
	FACTORYSIM_API extern const FString NewMaterial;
}
