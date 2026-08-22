#include "FactoryConveyor.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FactoryLayoutGrid.h"
#include "FactorySimTypes.h"

namespace
{
	const TCHAR* DefaultSection = TEXT("/Game/Conveyors/Conveyor_Simple.Conveyor_Simple");
}

AFactoryConveyor::AFactoryConveyor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Instanced rather than one component per section: a 30 m line is a lot of
	// sections, and they are identical.
	Sections = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Sections"));
	Sections->SetupAttachment(Root);
	Sections->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AFactoryConveyor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildConveyor();
}

void AFactoryConveyor::RebuildConveyor()
{
	if (Sections == nullptr)
	{
		return;
	}

	// Resolved here rather than in the constructor: loading during class default
	// object construction attaches what it loads to the CDO.
	UStaticMesh* Mesh = SectionMesh;
	if (Mesh == nullptr)
	{
		Mesh = LoadObject<UStaticMesh>(nullptr, DefaultSection);
	}

	Sections->ClearInstances();
	if (Mesh == nullptr)
	{
		UE_LOG(LogFactorySim, Warning, TEXT("Conveyor has no section mesh"));
		return;
	}
	Sections->SetStaticMesh(Mesh);

	// Tiling pitch comes from the mesh, so the run stays seamless if the section
	// is ever swapped for a different one.
	const FBox Bounds = Mesh->GetBoundingBox();
	const double Pitch = Bounds.Max.X - Bounds.Min.X;
	const double RunLength = LengthMetres * FactoryGrid::MetresToCm;
	if (Pitch <= KINDA_SMALL_NUMBER || RunLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	const int32 Count = FMath::Max(1, FMath::RoundToInt32(RunLength / Pitch));

	// Stretch the sections by the small amount needed to fill the run exactly,
	// rather than leaving a ragged end or overshooting into the next machine.
	const double Scale = RunLength / (Count * Pitch);

	for (int32 Index = 0; Index < Count; ++Index)
	{
		// Offset by -Min.X so the run starts at the actor origin rather than
		// wherever the section's pivot happens to sit inside its own bounds.
		const double X = (-Bounds.Min.X + Index * Pitch) * Scale;
		Sections->AddInstance(FTransform(
			FRotator::ZeroRotator, FVector(X, 0.0, 0.0), FVector(Scale, 1.0, 1.0)));
	}

	UE_LOG(LogFactorySim, Verbose, TEXT("Conveyor: %.2f m as %d section(s)"),
		LengthMetres, Count);
}
