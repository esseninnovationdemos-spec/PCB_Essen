#include "FactoryCycleDriverComponent.h"

#include "FactoryLineSubsystem.h"
#include "FactoryMachineComponent.h"
#include "FactoryMachineInstance.h"

UFactoryCycleDriverComponent::UFactoryCycleDriverComponent()
{
	// Everything is timer driven, so there is nothing to do per frame.
	PrimaryComponentTick.bCanEverTick = false;
}

void UFactoryCycleDriverComponent::BeginPlay()
{
	Super::BeginPlay();

	Machine = GetOwner() != nullptr
		? GetOwner()->FindComponentByClass<UFactoryMachineComponent>()
		: nullptr;

	if (Machine == nullptr)
	{
		UE_LOG(LogFactorySim, Warning,
			TEXT("%s has a cycle driver but no FactoryMachineComponent to drive"),
			*GetNameSafe(GetOwner()));
		return;
	}

	if (!bAutoStart)
	{
		return;
	}

	// Wait for the line rather than starting immediately: cycling while offline
	// would burn through boards whose telemetry goes nowhere.
	if (const UWorld* World = GetWorld())
	{
		if (UFactoryLineSubsystem* Line = World->GetSubsystem<UFactoryLineSubsystem>())
		{
			Line->OnLineOnlineChanged.AddDynamic(
				this, &UFactoryCycleDriverComponent::HandleLineOnline);
			bBoundToLine = true;

			if (Line->IsOnline())
			{
				StartDriving();
			}
		}
	}
}

void UFactoryCycleDriverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopDriving();

	if (bBoundToLine)
	{
		if (const UWorld* World = GetWorld())
		{
			if (UFactoryLineSubsystem* Line = World->GetSubsystem<UFactoryLineSubsystem>())
			{
				Line->OnLineOnlineChanged.RemoveDynamic(
					this, &UFactoryCycleDriverComponent::HandleLineOnline);
			}
		}
		bBoundToLine = false;
	}

	Super::EndPlay(EndPlayReason);
}

void UFactoryCycleDriverComponent::HandleLineOnline(const bool bOnline)
{
	if (bOnline)
	{
		StartDriving();
	}
	else
	{
		StopDriving();
	}
}

void UFactoryCycleDriverComponent::StartDriving()
{
	if (bDriving || Machine == nullptr)
	{
		return;
	}

	bDriving = true;
	CyclesCompleted = 0;

	// Stagger the first cycle so a row of identical stations does not step in
	// perfect lockstep, which looks synthetic and bunches broker traffic.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CycleTimer, this, &UFactoryCycleDriverComponent::BeginCycle,
			FMath::FRandRange(0.1f, FMath::Max(0.2f, IdleSecondsMax)), /*bLoop*/ false);
	}
}

void UFactoryCycleDriverComponent::StopDriving()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CycleTimer);
	}
	CycleTimer.Invalidate();
	bDriving = false;
}

void UFactoryCycleDriverComponent::BeginCycle()
{
	if (!bDriving || Machine == nullptr)
	{
		return;
	}

	Machine->StartCycle();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CycleTimer, this, &UFactoryCycleDriverComponent::EndCycle,
			PickCycleSeconds(), /*bLoop*/ false);
	}
}

void UFactoryCycleDriverComponent::EndCycle()
{
	if (!bDriving || Machine == nullptr)
	{
		return;
	}

	Machine->CompleteCycle();
	++CyclesCompleted;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CycleTimer, this, &UFactoryCycleDriverComponent::BeginCycle,
			PickIdleSeconds(), /*bLoop*/ false);
	}
}

float UFactoryCycleDriverComponent::PickCycleSeconds() const
{
	if (CycleSecondsMax > 0.0f)
	{
		return FMath::FRandRange(FMath::Min(CycleSecondsMin, CycleSecondsMax), CycleSecondsMax);
	}

	// Fall back to whatever the machine says its cycle time is, so the dwell and
	// the reported duration agree instead of being configured in two places.
	if (Machine != nullptr && Machine->Instance != nullptr)
	{
		for (const FFactoryMetricDefinition& Definition : Machine->Instance->GetEffectiveMetrics())
		{
			if (Definition.PublishOn == EFactoryPublishTrigger::CycleComplete
				&& Definition.Name.Contains(TEXT("cycle_time")))
			{
				const FFactoryRange Range = Machine->Instance->GetNominalRange(Definition);
				return FMath::Max(0.1f, static_cast<float>(
					FMath::FRandRange(Range.Min, Range.Max)));
			}
		}
	}

	return 5.0f;
}

float UFactoryCycleDriverComponent::PickIdleSeconds() const
{
	const float Min = FMath::Min(IdleSecondsMin, IdleSecondsMax);
	return FMath::Max(0.05f, FMath::FRandRange(Min, IdleSecondsMax));
}
