#include "FactoryMachineComponent.h"

#include "FactoryLineSubsystem.h"
#include "FactoryMachineArchetype.h"
#include "FactoryMachineInstance.h"
#include "SparkplugEdgeNode.h"

namespace
{
	/** Box-Muller normal deviate, mean 0 stddev 1. */
	double SampleNormal()
	{
		// Guard against log(0).
		const double U1 = FMath::Max(FMath::FRand(), UE_DOUBLE_SMALL_NUMBER);
		const double U2 = FMath::FRand();
		return FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(2.0 * UE_DOUBLE_PI * U2);
	}

	/** Exponential deviate with the given mean; models Poisson gaps. */
	double SampleExponential(const double Mean)
	{
		if (Mean <= 0.0)
		{
			return TNumericLimits<double>::Max();
		}
		const double U = FMath::Max(FMath::FRand(), UE_DOUBLE_SMALL_NUMBER);
		return -Mean * FMath::Loge(U);
	}
}

UFactoryMachineComponent::UFactoryMachineComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Telemetry cadence is at most a few Hz, so there is no reason to tick in
	// the physics group.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UFactoryMachineComponent::BeginPlay()
{
	Super::BeginPlay();

	if (Instance == nullptr || Instance->Archetype == nullptr)
	{
		// No instance with auto-register off is a deliberate disable, so stay
		// quiet -- otherwise an actor spawned per board would log once each time.
		if (bAutoRegister)
		{
			UE_LOG(LogFactorySim, Warning,
				TEXT("%s has a FactoryMachineComponent with no instance or archetype; "
					 "it will not publish"),
				*GetNameSafe(GetOwner()));
		}
		SetComponentTickEnabled(false);
		return;
	}

	// Resolve the definition list once: it is immutable for the lifetime of play,
	// and rebuilding it per tick deep-copied FString-bearing structs every frame.
	EffectiveMetrics = Instance->GetEffectiveMetrics();

	// Seed per-metric runtime at the idle value so the first ramp starts sensibly.
	for (const FFactoryMetricDefinition& Definition : EffectiveMetrics)
	{
		FFactoryMetricRuntime Runtime;
		Runtime.CurrentValue = Definition.IdleValue;
		Runtime.RampStartValue = Definition.IdleValue;
		Runtime.NextSpikeIn = Definition.Spike.bEnabled
			? SampleExponential(Definition.Spike.MeanMinutesBetween * 60.0)
			: TNumericLimits<double>::Max();
		MetricRuntime.Add(Definition.Name, Runtime);
	}

	if (!Instance->PartIds.IsEmpty())
	{
		CurrentPartId = Instance->PartIds[0];
	}
	if (!Instance->CarrierIds.IsEmpty())
	{
		CurrentCarrierId = Instance->CarrierIds[0];
	}

	PublishCountdown = Instance->GetTickIntervalSeconds();

	if (bAutoRegister)
	{
		if (UFactoryLineSubsystem* Line = GetLineSubsystem())
		{
			Line->RegisterMachine(this);
			Line->OnLineOnlineChanged.AddDynamic(this, &UFactoryMachineComponent::HandleLineOnline);
			bRegistered = true;
		}
	}
}

void UFactoryMachineComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered)
	{
		if (UFactoryLineSubsystem* Line = GetLineSubsystem())
		{
			Line->OnLineOnlineChanged.RemoveDynamic(this, &UFactoryMachineComponent::HandleLineOnline);
			Line->UnregisterMachine(this);
		}
		bRegistered = false;
	}

	Super::EndPlay(EndPlayReason);
}

UFactoryLineSubsystem* UFactoryMachineComponent::GetLineSubsystem() const
{
	const UWorld* World = GetWorld();
	return World != nullptr ? World->GetSubsystem<UFactoryLineSubsystem>() : nullptr;
}

void UFactoryMachineComponent::HandleLineOnline(const bool bOnline)
{
	if (bOnline)
	{
		// Seed the stream so consumers see a value before the first cycle.
		PublishEvent(FactoryEventTypes::Idle);
	}
}

void UFactoryMachineComponent::TickComponent(
	const float DeltaTime, const ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (Instance == nullptr || Instance->Archetype == nullptr)
	{
		return;
	}

	StateElapsed += DeltaTime;
	if (State == EFactoryMachineState::Running || State == EFactoryMachineState::Warmup)
	{
		CycleElapsed += DeltaTime;
	}

	AdvanceRuntime(DeltaTime);

	const UFactoryMachineArchetype* Archetype = Instance->Archetype;

	// Ramp states are the only ones that advance on their own. Running never
	// self-terminates: Unreal drives that via CompleteCycle.
	if (State == EFactoryMachineState::Warmup
		&& StateElapsed >= Archetype->WarmupSeconds)
	{
		SetMachineState(EFactoryMachineState::Running);
	}
	else if (State == EFactoryMachineState::Cooldown
		&& StateElapsed >= Archetype->CooldownSeconds)
	{
		SetMachineState(EFactoryMachineState::Idle);
	}

	// Advance the carrier id on its dwell timer.
	if (!Instance->CarrierIds.IsEmpty() && Instance->CarrierDwellSeconds > 0.0f)
	{
		CarrierElapsed += DeltaTime;
		if (CarrierElapsed >= Instance->CarrierDwellSeconds)
		{
			CarrierElapsed = 0.0f;
			CarrierIndex = (CarrierIndex + 1) % Instance->CarrierIds.Num();
			CurrentCarrierId = Instance->CarrierIds[CarrierIndex];
		}
	}

	// Idle machines stay quiet, matching the Python auto-tick which skips them.
	if (State == EFactoryMachineState::Idle)
	{
		return;
	}

	const float Interval = Instance->GetTickIntervalSeconds();
	if (Interval <= 0.0f)
	{
		// Event-only device, e.g. the line aggregate.
		return;
	}

	PublishCountdown -= DeltaTime;
	if (PublishCountdown <= 0.0f)
	{
		PublishCountdown += Interval;
		// Guard against a long hitch queueing a burst of catch-up publishes.
		PublishCountdown = FMath::Max(PublishCountdown, 0.0f);

		PublishSample(FactoryEventTypes::PhaseRunning, false);
	}
}

void UFactoryMachineComponent::AdvanceRuntime(const float DeltaTime)
{
	for (const FFactoryMetricDefinition& Definition : EffectiveMetrics)
	{
		FFactoryMetricRuntime* Runtime = MetricRuntime.Find(Definition.Name);
		if (Runtime == nullptr)
		{
			continue;
		}

		if (Definition.MotionProfile != EFactoryMotionProfile::None)
		{
			Runtime->MotionTime += DeltaTime;
		}

		// Wear only accumulates while the machine is actually working.
		if (Definition.Drift.bEnabled && State == EFactoryMachineState::Running)
		{
			const double PerSecond = Definition.Drift.RatePerHour / 3600.0;
			Runtime->Drift = FMath::Clamp(
				Runtime->Drift + PerSecond * DeltaTime,
				-FMath::Abs(Definition.Drift.MaxDrift),
				FMath::Abs(Definition.Drift.MaxDrift));
		}
		else if (Definition.Drift.bEnabled
			&& Definition.Drift.bResetOnIdle
			&& State == EFactoryMachineState::Idle)
		{
			Runtime->Drift = 0.0;
		}

		if (Definition.Spike.bEnabled)
		{
			if (Runtime->SpikeRemaining > 0.0)
			{
				Runtime->SpikeRemaining -= DeltaTime;
				if (Runtime->SpikeRemaining <= 0.0)
				{
					Runtime->NextSpikeIn =
						SampleExponential(Definition.Spike.MeanMinutesBetween * 60.0);
				}
			}
			else if (State == EFactoryMachineState::Running)
			{
				Runtime->NextSpikeIn -= DeltaTime;
				if (Runtime->NextSpikeIn <= 0.0)
				{
					Runtime->SpikeRemaining = Definition.Spike.DurationSeconds;
					if (Definition.Spike.bForcesState)
					{
						SetMachineState(EFactoryMachineState::Fault);
					}
				}
			}
		}
	}
}

double UFactoryMachineComponent::SampleMetric(
	const FFactoryMetricDefinition& Definition, FFactoryMetricRuntime& Runtime) const
{
	const UFactoryMachineArchetype* Archetype = Instance->Archetype;
	const FFactoryRange Nominal = Instance->GetNominalRange(Definition);

	double Value = Runtime.CurrentValue;

	switch (State)
	{
	case EFactoryMachineState::Idle:
		Value = Definition.IdleValue;
		break;

	case EFactoryMachineState::Fault:
		Value = Definition.FaultValue;
		break;

	case EFactoryMachineState::Blocked:
		// Blocked holds whatever it had; the line has stopped, not the machine.
		break;

	case EFactoryMachineState::Warmup:
	{
		const double Target = Definition.bUseWarmupValue
			? Definition.WarmupValue
			: Nominal.Midpoint();
		const double Alpha = (Archetype->WarmupSeconds > 0.0f)
			? FMath::Clamp(StateElapsed / Archetype->WarmupSeconds, 0.0f, 1.0f)
			: 1.0;

		Value = Definition.bThermal
			// Newton cooling: fast at first, asymptotic at the target.
			? Target + (Runtime.RampStartValue - Target)
				* FMath::Exp(-Archetype->ThermalRampConstant * Alpha)
			: FMath::Lerp(Runtime.RampStartValue, Target, Alpha);
		break;
	}

	case EFactoryMachineState::Cooldown:
	{
		const double Alpha = (Archetype->CooldownSeconds > 0.0f)
			? FMath::Clamp(StateElapsed / Archetype->CooldownSeconds, 0.0f, 1.0f)
			: 1.0;
		// Cool from wherever the metric actually was, not from a nominal value:
		// a machine that faulted high must decay from the fault value.
		const double Start = Runtime.RampStartValue;

		Value = Definition.bThermal
			? Definition.IdleValue + (Start - Definition.IdleValue)
				* FMath::Exp(-Archetype->ThermalRampConstant * Alpha)
			: FMath::Lerp(Start, Definition.IdleValue, Alpha);
		break;
	}

	case EFactoryMachineState::Running:
	default:
		switch (Definition.MotionProfile)
		{
		case EFactoryMotionProfile::Sine:
		{
			const double Phase = (Definition.MotionPeriodSeconds > 0.0)
				? Runtime.MotionTime / Definition.MotionPeriodSeconds
				: 0.0;
			Value = Nominal.Lerp(0.5 + 0.5 * FMath::Sin(2.0 * UE_DOUBLE_PI * Phase));
			break;
		}
		case EFactoryMotionProfile::Sawtooth:
		{
			const double Phase = (Definition.MotionPeriodSeconds > 0.0)
				? Runtime.MotionTime / Definition.MotionPeriodSeconds
				: 0.0;
			Value = Nominal.Lerp(FMath::Frac(Phase));
			break;
		}
		default:
			// No profile: sample uniformly across the nominal band, which is what
			// the Python reference does for plain process values.
			Value = Nominal.Lerp(FMath::FRand());
			break;
		}
		break;
	}

	return ApplyDegradation(Definition, Runtime, Value);
}

double UFactoryMachineComponent::ApplyDegradation(
	const FFactoryMetricDefinition& Definition,
	FFactoryMetricRuntime& Runtime,
	double Value) const
{
	// An active spike overrides the process value entirely.
	if (Definition.Spike.bEnabled && Runtime.SpikeRemaining > 0.0)
	{
		switch (Definition.Spike.SpikeTo)
		{
		case EFactorySpikeTarget::AbsoluteMax: Value = Definition.Absolute.Max; break;
		case EFactorySpikeTarget::AbsoluteMin: Value = Definition.Absolute.Min; break;
		default:                               Value = Definition.FaultValue;   break;
		}
		return Definition.Absolute.Clamp(Value);
	}

	Value += Runtime.Drift;

	if (Definition.Fluctuation.bEnabled)
	{
		const FFactoryRange Nominal = Instance->GetNominalRange(Definition);
		// Noise is a percentage of the nominal span, so it scales with the
		// metric's own units rather than needing per-metric tuning.
		const double Span = FMath::Max(Nominal.Max - Nominal.Min, UE_DOUBLE_SMALL_NUMBER);
		Value += SampleNormal() * (Definition.Fluctuation.NoisePercent / 100.0) * Span;
	}

	return Definition.Absolute.Clamp(Value);
}

FSparkplugMetric UFactoryMachineComponent::MakeMetric(
	const FString& MetricName, const ESparkplugDataType DataType) const
{
	FSparkplugMetric Metric;
	Metric.Name = MetricName;
	// Both name and alias go on the wire: the ClickHouse bridge fans out on
	// name, so an alias-only message would stop ingest.
	Metric.Alias = Instance->GetAlias(MetricName);
	Metric.DataType = DataType;
	Metric.Timestamp = SparkplugUtils::UtcNowMilliseconds();
	return Metric;
}

void UFactoryMachineComponent::PublishSample(
	const FString& EventType, const bool bIncludeCycleCompleteMetrics)
{
	UFactoryLineSubsystem* Line = GetLineSubsystem();
	if (Line == nullptr || !Line->IsOnline() || Instance == nullptr)
	{
		return;
	}

	TArray<FSparkplugMetric> Metrics;
	const UFactoryMachineArchetype* Archetype = Instance->Archetype;
	Metrics.Reserve(EffectiveMetrics.Num() + 7);

	for (const FFactoryMetricDefinition& Definition : EffectiveMetrics)
	{
		// Cycle-scoped metrics such as cycle_time_sec only make sense at the end
		// of a cycle; sending them every tick would publish meaningless numbers.
		if (Definition.PublishOn == EFactoryPublishTrigger::CycleComplete
			&& !bIncludeCycleCompleteMetrics)
		{
			continue;
		}

		FFactoryMetricRuntime* Runtime = MetricRuntime.Find(Definition.Name);
		if (Runtime == nullptr)
		{
			continue;
		}

		double Value;
		if (Definition.PublishOn == EFactoryPublishTrigger::CycleComplete)
		{
			// Report the measured duration rather than a synthesised sample.
			Value = Definition.Absolute.Clamp(
				LastCycleSeconds > 0.0f ? LastCycleSeconds : Definition.Nominal.Midpoint());
		}
		else
		{
			Value = SampleMetric(Definition, *Runtime);
		}

		Runtime->CurrentValue = Value;

		FSparkplugMetric Metric = MakeMetric(Definition.Name, Definition.DataType);
		if (Metric.UsesDoubleStorage())
		{
			Metric.DoubleValue = Value;
		}
		else
		{
			Metric.IntValue = static_cast<int64>(FMath::RoundHalfToEven(Value));
		}
		Metrics.Add(MoveTemp(Metric));
	}

	// Synthetic metrics, in the order aliases were historically allocated.
	FSparkplugMetric StateMetric = MakeMetric(
		FactorySyntheticMetrics::StateCode, ESparkplugDataType::Int32);
	StateMetric.IntValue = static_cast<int64>(State);
	Metrics.Add(MoveTemp(StateMetric));

	if (Archetype->bIsInspectionStation)
	{
		if (!LastInspectionResult.IsEmpty())
		{
			FSparkplugMetric ResultMetric = MakeMetric(
				FactorySyntheticMetrics::InspectionResult, ESparkplugDataType::String);
			ResultMetric.StringValue = LastInspectionResult;
			Metrics.Add(MoveTemp(ResultMetric));
		}

		FSparkplugMetric FailMetric = MakeMetric(
			FactorySyntheticMetrics::FailCounter, ESparkplugDataType::Int32);
		FailMetric.IntValue = FailCounter;
		Metrics.Add(MoveTemp(FailMetric));
	}

	if (!CurrentCarrierId.IsEmpty())
	{
		FSparkplugMetric CarrierMetric = MakeMetric(
			FactorySyntheticMetrics::ConveyorId, ESparkplugDataType::String);
		CarrierMetric.StringValue = CurrentCarrierId;
		Metrics.Add(MoveTemp(CarrierMetric));
	}

	if (!CurrentPartId.IsEmpty())
	{
		FSparkplugMetric CurrentPart = MakeMetric(
			FactorySyntheticMetrics::CurrentPartId, ESparkplugDataType::String);
		CurrentPart.StringValue = CurrentPartId;
		Metrics.Add(MoveTemp(CurrentPart));

		FSparkplugMetric Part = MakeMetric(
			FactorySyntheticMetrics::PartId, ESparkplugDataType::String);
		Part.StringValue = CurrentPartId;
		Metrics.Add(MoveTemp(Part));
	}

	FSparkplugMetric EventMetric = MakeMetric(
		FactorySyntheticMetrics::EventType, ESparkplugDataType::String);
	EventMetric.StringValue = EventType;
	Metrics.Add(MoveTemp(EventMetric));

	// This machine's own node, not simply the first one: with an edge node per
	// production line, publishing through the wrong one would put the device on
	// a topic no consumer of that line subscribes to.
	if (USparkplugEdgeNode* Node = Line->FindEdgeNodeForMachine(this))
	{
		Node->PublishDeviceData(Instance->GetDeviceId(), Metrics);
	}
}

TArray<FSparkplugMetric> UFactoryMachineComponent::BuildBirthMetrics() const
{
	TArray<FSparkplugMetric> Metrics;
	if (Instance == nullptr || Instance->Archetype == nullptr)
	{
		return Metrics;
	}

	const UFactoryMachineArchetype* Archetype = Instance->Archetype;

	// DBIRTH must advertise every metric the device will ever publish, because
	// it is what establishes the name/alias map consumers cache.
	for (const FFactoryMetricDefinition& Definition : EffectiveMetrics)
	{
		FSparkplugMetric Metric = MakeMetric(Definition.Name, Definition.DataType);
		if (Metric.UsesDoubleStorage())
		{
			Metric.DoubleValue = Definition.IdleValue;
		}
		else
		{
			Metric.IntValue = static_cast<int64>(Definition.IdleValue);
		}
		Metrics.Add(MoveTemp(Metric));
	}

	FSparkplugMetric StateMetric = MakeMetric(
		FactorySyntheticMetrics::StateCode, ESparkplugDataType::Int32);
	StateMetric.IntValue = static_cast<int64>(EFactoryMachineState::Idle);
	Metrics.Add(MoveTemp(StateMetric));

	if (Archetype->bIsInspectionStation)
	{
		FSparkplugMetric ResultMetric = MakeMetric(
			FactorySyntheticMetrics::InspectionResult, ESparkplugDataType::String);
		ResultMetric.StringValue = TEXT("PASS");
		Metrics.Add(MoveTemp(ResultMetric));

		FSparkplugMetric FailMetric = MakeMetric(
			FactorySyntheticMetrics::FailCounter, ESparkplugDataType::Int32);
		FailMetric.IntValue = 0;
		Metrics.Add(MoveTemp(FailMetric));
	}

	if (!Instance->CarrierIds.IsEmpty())
	{
		FSparkplugMetric CarrierMetric = MakeMetric(
			FactorySyntheticMetrics::ConveyorId, ESparkplugDataType::String);
		CarrierMetric.StringValue = Instance->CarrierIds[0];
		Metrics.Add(MoveTemp(CarrierMetric));
	}

	if (!Instance->PartIds.IsEmpty())
	{
		FSparkplugMetric CurrentPart = MakeMetric(
			FactorySyntheticMetrics::CurrentPartId, ESparkplugDataType::String);
		CurrentPart.StringValue = Instance->PartIds[0];
		Metrics.Add(MoveTemp(CurrentPart));

		FSparkplugMetric Part = MakeMetric(
			FactorySyntheticMetrics::PartId, ESparkplugDataType::String);
		Part.StringValue = Instance->PartIds[0];
		Metrics.Add(MoveTemp(Part));
	}

	FSparkplugMetric EventMetric = MakeMetric(
		FactorySyntheticMetrics::EventType, ESparkplugDataType::String);
	EventMetric.StringValue = FactoryEventTypes::Idle;
	Metrics.Add(MoveTemp(EventMetric));

	return Metrics;
}

// ---------------------------------------------------------------------------
// Blueprint control surface
// ---------------------------------------------------------------------------

void UFactoryMachineComponent::StartCycle()
{
	if (Instance == nullptr || Instance->Archetype == nullptr)
	{
		return;
	}

	CycleElapsed = 0.0f;
	bCycleInProgress = true;
	bInspectionReportedThisCycle = false;

	// Advance the part id as material enters the station.
	if (!Instance->PartIds.IsEmpty())
	{
		PartIndex = (PartIndex + 1) % Instance->PartIds.Num();
		CurrentPartId = Instance->PartIds[PartIndex];
	}

	const bool bHasWarmup = Instance->Archetype->WarmupSeconds > 0.0f
		&& Instance->Archetype->StateModel != EFactoryStateModel::Manual;

	SetMachineState(bHasWarmup ? EFactoryMachineState::Warmup : EFactoryMachineState::Running);
	PublishSample(FactoryEventTypes::CycleStarted, false);
}

void UFactoryMachineComponent::CompleteCycle()
{
	if (Instance == nullptr || Instance->Archetype == nullptr)
	{
		return;
	}

	if (!bCycleInProgress)
	{
		// No StartCycle preceded this, so CycleElapsed is left over from an
		// earlier board. Publishing it would double-count that duration
		// downstream, so report the transition without cycle-scoped metrics.
		UE_LOG(LogFactorySim, Warning,
			TEXT("%s: CompleteCycle without a matching StartCycle; skipping cycle metrics"),
			*GetNameSafe(GetOwner()));
		PublishSample(FactoryEventTypes::CycleComplete, false);
		SetMachineState(EFactoryMachineState::Idle);
		return;
	}

	LastCycleSeconds = CycleElapsed;
	bCycleInProgress = false;

	// An inspection station must report a verdict for every board. If the
	// Blueprint did not call ReportInspection explicitly, roll one from the
	// configured fail rate -- otherwise inspection_result and fail_counter would
	// sit at their birth values forever and fail_rate would have no effect.
	if (Instance->Archetype->bIsInspectionStation && !bInspectionReportedThisCycle)
	{
		RollInspection();
	}

	// The cycle-complete sample is the only one carrying cycle-scoped metrics.
	PublishSample(FactoryEventTypes::CycleComplete, true);

	const bool bHasCooldown = Instance->Archetype->CooldownSeconds > 0.0f
		&& Instance->Archetype->StateModel != EFactoryStateModel::Manual;

	SetMachineState(bHasCooldown ? EFactoryMachineState::Cooldown : EFactoryMachineState::Idle);
}

void UFactoryMachineComponent::PublishEvent(const FString& EventType)
{
	PublishSample(EventType, false);
}

void UFactoryMachineComponent::SetMachineState(const EFactoryMachineState NewState)
{
	if (State == NewState)
	{
		return;
	}

	const EFactoryMachineState OldState = State;
	State = NewState;
	StateElapsed = 0.0f;

	// Freeze where every metric is right now; Warmup and Cooldown ramp from here
	// so a transition never produces a step change in the published value.
	for (TPair<FString, FFactoryMetricRuntime>& Pair : MetricRuntime)
	{
		Pair.Value.RampStartValue = Pair.Value.CurrentValue;
	}

	OnStateChanged.Broadcast(OldState, NewState);

	// A state change is worth reporting immediately rather than waiting for the
	// next tick, so consumers see transitions promptly.
	PublishSample(FactoryEventTypes::PhaseStarted, false);
}

void UFactoryMachineComponent::ReportInspection(const bool bPassed)
{
	bInspectionReportedThisCycle = true;
	LastInspectionResult = bPassed ? TEXT("PASS") : TEXT("FAIL");
	if (!bPassed)
	{
		++FailCounter;
	}
}

bool UFactoryMachineComponent::RollInspection()
{
	if (Instance == nullptr)
	{
		return true;
	}

	const bool bPassed = FMath::FRand() >= Instance->GetFailRate();
	ReportInspection(bPassed);
	return bPassed;
}

void UFactoryMachineComponent::SetPartId(const FString& InPartId)
{
	CurrentPartId = InPartId;
}

void UFactoryMachineComponent::SetCarrierId(const FString& InCarrierId)
{
	CurrentCarrierId = InCarrierId;
	CarrierElapsed = 0.0f;
}

double UFactoryMachineComponent::GetMetricValue(const FString& MetricName) const
{
	const FFactoryMetricRuntime* Runtime = MetricRuntime.Find(MetricName);
	return Runtime != nullptr ? Runtime->CurrentValue : 0.0;
}

float UFactoryMachineComponent::GetCycleElapsedSeconds() const
{
	return CycleElapsed;
}
