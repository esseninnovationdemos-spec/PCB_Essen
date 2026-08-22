#include "FactoryProductionLine.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FactoryLayoutGrid.h"
#include "FactoryLineSubsystem.h"
#include "FactoryMachineComponent.h"
#include "FactoryMachineInstance.h"
#include "FactoryShapeMaterials.h"
#include "FactorySimTypes.h"
#include "Materials/MaterialInterface.h"

AFactoryProductionLine::AFactoryProductionLine()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Belt = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Belt"));
	Belt->SetupAttachment(Root);
	Belt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AFactoryProductionLine::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Stops are authored by hand and over MCP, so snap them rather than trusting
	// whoever set them to have landed on the grid.
	for (FFactoryLineStop& Stop : Stops)
	{
		Stop.PositionMetres = FactoryGrid::SnapMetres(Stop.PositionMetres);
	}
	EntryMetres = FactoryGrid::SnapMetres(EntryMetres);
	ExitMetres = FactoryGrid::SnapMetres(ExitMetres);

	RebuildBelt();
}

void AFactoryProductionLine::RebuildBelt()
{
	if (Belt == nullptr)
	{
		return;
	}

	Belt->SetVisibility(bShowBelt);
	if (!bShowBelt)
	{
		return;
	}

	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		Belt->SetStaticMesh(Cube);
	}
	if (UMaterialInterface* Tint = FactoryShapeMaterials::Load(FactoryShapeMaterials::Belt))
	{
		Belt->SetMaterial(0, Tint);
	}

	const FVector From = FactoryGrid::MetresToWorld(EntryMetres, BeltHeightCm);
	const FVector To = FactoryGrid::MetresToWorld(ExitMetres, BeltHeightCm);
	const FVector Delta = To - From;
	const double Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		Belt->SetVisibility(false);
		return;
	}

	// Drawn just under the ride height so units sit on it. The basic cube is one
	// metre, so scale is metres.
	Belt->SetRelativeLocation(From + Delta * 0.5 - FVector(0.0, 0.0, 3.0));
	Belt->SetRelativeRotation(Delta.Rotation());
	Belt->SetRelativeScale3D(FVector(Length / FactoryGrid::MetresToCm, BeltWidthMetres, 0.06));
}

void AFactoryProductionLine::BeginPlay()
{
	Super::BeginPlay();

	if (!bAutoStart)
	{
		return;
	}

	UWorld* World = GetWorld();
	UFactoryLineSubsystem* Line = (World != nullptr)
		? World->GetSubsystem<UFactoryLineSubsystem>() : nullptr;

	// Wait for the edge node: releasing units before the line is announced would
	// publish cycles against devices the broker has not been told about.
	if (Line != nullptr)
	{
		Line->OnLineOnlineChanged.AddDynamic(this, &AFactoryProductionLine::HandleLineOnline);
		bBoundToLine = true;
		if (Line->IsOnline())
		{
			StartProduction();
		}
	}
	else
	{
		StartProduction();
	}
}

void AFactoryProductionLine::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bBoundToLine)
	{
		UWorld* World = GetWorld();
		if (UFactoryLineSubsystem* Line = (World != nullptr)
			? World->GetSubsystem<UFactoryLineSubsystem>() : nullptr)
		{
			Line->OnLineOnlineChanged.RemoveDynamic(this, &AFactoryProductionLine::HandleLineOnline);
		}
		bBoundToLine = false;
	}

	for (FFactoryUnitInFlight& Unit : Units)
	{
		if (Unit.Product != nullptr)
		{
			Unit.Product->Destroy();
		}
	}
	Units.Reset();

	Super::EndPlay(EndPlayReason);
}

void AFactoryProductionLine::HandleLineOnline(const bool bOnline)
{
	if (bOnline)
	{
		StartProduction();
	}
	else
	{
		StopProduction();
	}
}

void AFactoryProductionLine::StartProduction()
{
	if (bProducing)
	{
		return;
	}
	bProducing = true;
	// Release the first unit immediately rather than after a full takt, so a
	// short run shows something without a wait.
	TaktAccumulator = TaktSeconds;
	UE_LOG(LogFactorySim, Display,
		TEXT("Production line started: %d stop(s), %.1fs takt"), Stops.Num(), TaktSeconds);
}

void AFactoryProductionLine::StopProduction()
{
	bProducing = false;
}

UFactoryMachineComponent* AFactoryProductionLine::MachineForStop(const int32 StopIndex) const
{
	if (!Stops.IsValidIndex(StopIndex) || Stops[StopIndex].DeviceId.IsEmpty())
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	UFactoryLineSubsystem* Line = (World != nullptr)
		? World->GetSubsystem<UFactoryLineSubsystem>() : nullptr;
	return (Line != nullptr) ? Line->FindMachine(Stops[StopIndex].DeviceId) : nullptr;
}

float AFactoryProductionLine::ResolveDwellSeconds(const int32 StopIndex) const
{
	if (!Stops.IsValidIndex(StopIndex))
	{
		return 1.0f;
	}
	if (Stops[StopIndex].DwellSecondsOverride > 0.0f)
	{
		return Stops[StopIndex].DwellSecondsOverride;
	}

	// Same source the cycle driver uses, so a station's dwell matches the cycle
	// time it reports on the wire instead of being configured twice.
	if (const UFactoryMachineComponent* Machine = MachineForStop(StopIndex))
	{
		if (Machine->Instance != nullptr)
		{
			for (const FFactoryMetricDefinition& Definition : Machine->Instance->GetEffectiveMetrics())
			{
				if (Definition.PublishOn == EFactoryPublishTrigger::CycleComplete
					&& Definition.Name.Contains(TEXT("cycle_time")))
				{
					const FFactoryRange Range = Machine->Instance->GetNominalRange(Definition);
					return FMath::Max(0.1f,
						static_cast<float>(FMath::FRandRange(Range.Min, Range.Max)));
				}
			}
		}
	}
	return 4.0f;
}

bool AFactoryProductionLine::IsStopReserved(const int32 StopIndex) const
{
	for (const FFactoryUnitInFlight& Unit : Units)
	{
		if (Unit.TargetStop == StopIndex)
		{
			return true;
		}
	}
	return false;
}

void AFactoryProductionLine::SpawnUnit()
{
	UWorld* World = GetWorld();
	if (World == nullptr || Stops.Num() == 0)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	const FVector Location = GetActorLocation()
		+ FactoryGrid::MetresToWorld(EntryMetres, BeltHeightCm);

	AFactoryProduct* Product = World->SpawnActor<AFactoryProduct>(
		AFactoryProduct::StaticClass(), FTransform(FRotator::ZeroRotator, Location), Params);
	if (Product == nullptr)
	{
		UE_LOG(LogFactorySim, Warning, TEXT("Could not spawn a unit at the line entry"));
		return;
	}

	++UnitsReleased;
	Product->Serial = FString::Printf(TEXT("ECU-%05d"), UnitsReleased);
	UE_LOG(LogFactorySim, Verbose, TEXT("Released unit %s at the line entry"), *Product->Serial);
	Product->SetStage(EFactoryProductStage::Empty);

	FFactoryUnitInFlight Unit;
	Unit.Product = Product;
	// Reserves stop 0 for the whole trip toward it, not just on arrival.
	Unit.TargetStop = 0;
	Units.Add(Unit);
}

bool AFactoryProductionLine::MoveToward(
	AFactoryProduct* Product, const FVector2D& TargetMetres, const float DeltaSeconds) const
{
	if (Product == nullptr)
	{
		return true;
	}

	const FVector Target = GetActorLocation()
		+ FactoryGrid::MetresToWorld(TargetMetres, BeltHeightCm);
	const FVector Current = Product->GetActorLocation();
	const FVector Delta = Target - Current;
	const double Distance = Delta.Size();

	const double Step = TransportSpeedMetresPerSecond * FactoryGrid::MetresToCm * DeltaSeconds;
	if (Distance <= Step || Distance <= KINDA_SMALL_NUMBER)
	{
		Product->SetActorLocation(Target);
		return true;
	}

	Product->SetActorLocation(Current + Delta / Distance * Step);
	return false;
}

bool AFactoryProductionLine::TickUnit(FFactoryUnitInFlight& Unit, const float DeltaSeconds)
{
	if (Unit.Product == nullptr)
	{
		return false;
	}

	// Released past the last stop: run to the exit and retire.
	if (Unit.TargetStop == INDEX_NONE)
	{
		if (MoveToward(Unit.Product, ExitMetres, DeltaSeconds))
		{
			UE_LOG(LogFactorySim, Verbose, TEXT("Unit %s completed (%d total)"),
				*Unit.Product->Serial, UnitsCompleted + 1);
			Unit.Product->Destroy();
			Unit.Product = nullptr;
			++UnitsCompleted;
			return false;
		}
		return true;
	}

	if (!Stops.IsValidIndex(Unit.TargetStop))
	{
		return true;
	}
	const FFactoryLineStop& Stop = Stops[Unit.TargetStop];

	if (!Unit.bArrived)
	{
		if (MoveToward(Unit.Product, Stop.PositionMetres, DeltaSeconds))
		{
			Unit.bArrived = true;
			Unit.DwellRemaining = ResolveDwellSeconds(Unit.TargetStop);

			UE_LOG(LogFactorySim, Verbose, TEXT("%s arrived at stop %d (%s)"),
				*Unit.Product->Serial, Unit.TargetStop,
				Stop.DeviceId.IsEmpty() ? TEXT("transport") : *Stop.DeviceId);

			if (UFactoryMachineComponent* Machine = MachineForStop(Unit.TargetStop))
			{
				Machine->SetPartId(Unit.Product->Serial);
				Machine->StartCycle();
			}
		}
		return true;
	}

	// Held at the stop while the station works on it.
	if (Unit.DwellRemaining > 0.0f)
	{
		Unit.DwellRemaining -= DeltaSeconds;
		if (Unit.DwellRemaining > 0.0f)
		{
			return true;
		}

		if (UFactoryMachineComponent* Machine = MachineForStop(Unit.TargetStop))
		{
			// Let the station decide pass or fail and carry that on the unit, so
			// a reject is visible on the floor and not only on the wire.
			const bool bPassed = Machine->RollInspection();
			Unit.Product->SetPassed(bPassed);
			Machine->CompleteCycle();
		}
		Unit.Product->SetStage(Stop.StageOnComplete);
	}

	// Work is finished; try to hand the unit downstream.
	const int32 NextStop = Unit.TargetStop + 1;
	const bool bPastEnd = !Stops.IsValidIndex(NextStop);

	if (!bPastEnd && IsStopReserved(NextStop))
	{
		// Nowhere to put the finished work: that is what Blocked means.
		if (!Unit.bBlocked)
		{
			Unit.bBlocked = true;
			if (UFactoryMachineComponent* Machine = MachineForStop(Unit.TargetStop))
			{
				Machine->SetMachineState(EFactoryMachineState::Blocked);
			}
		}
		return true;
	}

	if (Unit.bBlocked)
	{
		Unit.bBlocked = false;
		if (UFactoryMachineComponent* Machine = MachineForStop(Unit.TargetStop))
		{
			Machine->SetMachineState(EFactoryMachineState::Idle);
		}
	}

	Unit.TargetStop = bPastEnd ? INDEX_NONE : NextStop;
	Unit.bArrived = false;
	return true;
}

void AFactoryProductionLine::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bProducing)
	{
		return;
	}

	for (int32 Index = 0; Index < Units.Num(); )
	{
		if (TickUnit(Units[Index], DeltaSeconds))
		{
			++Index;
		}
		else
		{
			Units.RemoveAt(Index);
		}
	}

	if (Stops.Num() == 0)
	{
		return;
	}

	TaktAccumulator += DeltaSeconds;
	if (TaktAccumulator >= TaktSeconds && !IsStopReserved(0))
	{
		// Only release when the head of the line is clear. Holding the release
		// rather than queueing units off the end of the belt keeps work in
		// progress bounded, the way a real release point does.
		TaktAccumulator = 0.0f;
		SpawnUnit();
	}
}
