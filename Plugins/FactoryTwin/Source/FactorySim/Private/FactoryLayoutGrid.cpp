#include "FactoryLayoutGrid.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FactoryShapeMaterials.h"
#include "FactorySimTypes.h"
#include "FactoryTwinSettings.h"
#include "Materials/MaterialInterface.h"

namespace FactoryGrid
{
	float GetPitchMetres()
	{
		const UFactoryTwinSettings* Settings = UFactoryTwinSettings::Get();
		const float Pitch = (Settings != nullptr) ? Settings->GridPitchMetres : 0.5f;
		// A zero or negative pitch would make snapping divide by zero.
		return (Pitch > KINDA_SMALL_NUMBER) ? Pitch : 0.5f;
	}

	static float ResolvePitch(const float PitchMetres)
	{
		return (PitchMetres > KINDA_SMALL_NUMBER) ? PitchMetres : GetPitchMetres();
	}

	FVector2D SnapMetres(const FVector2D& Metres, const float PitchMetres)
	{
		const float Pitch = ResolvePitch(PitchMetres);
		return FVector2D(
			FMath::RoundToDouble(Metres.X / Pitch) * Pitch,
			FMath::RoundToDouble(Metres.Y / Pitch) * Pitch);
	}

	FFactoryGridCoord MetresToCell(const FVector2D& Metres, const float PitchMetres)
	{
		const float Pitch = ResolvePitch(PitchMetres);
		return FFactoryGridCoord(
			FMath::RoundToInt32(Metres.X / Pitch),
			FMath::RoundToInt32(Metres.Y / Pitch));
	}

	FVector2D CellToMetres(const FFactoryGridCoord& Cell, const float PitchMetres)
	{
		const float Pitch = ResolvePitch(PitchMetres);
		return FVector2D(Cell.X * Pitch, Cell.Y * Pitch);
	}

	FVector MetresToWorld(const FVector2D& Metres, const double ZCm)
	{
		return FVector(Metres.X * MetresToCm, Metres.Y * MetresToCm, ZCm);
	}
}

namespace
{
	/**
	 * Builds a line component.
	 *
	 * Deliberately loads nothing: this runs from the constructor, and anything
	 * loaded there is attached to the class default object and shared by every
	 * instance. Meshes and materials are resolved in RebuildGrid instead.
	 */
	UInstancedStaticMeshComponent* MakeLineComponent(
		AActor* Owner, USceneComponent* Parent, const TCHAR* Name)
	{
		UInstancedStaticMeshComponent* Component =
			Owner->CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Component->SetupAttachment(Parent);
		// Purely a drafting aid: it must never block a trace or cast shade over
		// the machines it is there to help place.
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		return Component;
	}
}

AFactoryFloorGrid::AFactoryFloorGrid()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	MinorLines = MakeLineComponent(this, Root, TEXT("MinorLines"));
	MajorLines = MakeLineComponent(this, Root, TEXT("MajorLines"));
}

float AFactoryFloorGrid::GetPitchMetres() const
{
	return (PitchMetresOverride > KINDA_SMALL_NUMBER)
		? PitchMetresOverride
		: FactoryGrid::GetPitchMetres();
}

void AFactoryFloorGrid::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildGrid();
}

void AFactoryFloorGrid::AddLine(
	UInstancedStaticMeshComponent* Target,
	const FVector2D& FromMetres,
	const FVector2D& ToMetres,
	const float WidthCm)
{
	if (Target == nullptr)
	{
		return;
	}

	const FVector From = FactoryGrid::MetresToWorld(FromMetres, HeightCm);
	const FVector To = FactoryGrid::MetresToWorld(ToMetres, HeightCm);
	const FVector Delta = To - From;
	const double Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// The basic cube is 100 cm on a side, so a scale of 1 is one metre.
	const FVector Scale(Length / 100.0, WidthCm / 100.0, 0.02);
	const FRotator Rotation = Delta.Rotation();
	Target->AddInstance(FTransform(Rotation, From + Delta * 0.5, Scale));
}

void AFactoryFloorGrid::RebuildGrid()
{
	if (MinorLines == nullptr || MajorLines == nullptr)
	{
		return;
	}

	// Resolved here rather than in the constructor, for the reason given on
	// MakeLineComponent.
	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		MinorLines->SetStaticMesh(Cube);
		MajorLines->SetStaticMesh(Cube);
	}
	if (UMaterialInterface* Tint = FactoryShapeMaterials::Load(FactoryShapeMaterials::GridMinor))
	{
		MinorLines->SetMaterial(0, Tint);
	}
	if (UMaterialInterface* Tint = FactoryShapeMaterials::Load(FactoryShapeMaterials::GridMajor))
	{
		MajorLines->SetMaterial(0, Tint);
	}

	MinorLines->ClearInstances();
	MajorLines->ClearInstances();

	const float Pitch = GetPitchMetres();
	if (Pitch <= KINDA_SMALL_NUMBER || MaxMetres.X <= MinMetres.X || MaxMetres.Y <= MinMetres.Y)
	{
		return;
	}

	// Step in cell indices rather than accumulating a float, so the last line
	// lands exactly on the extent instead of drifting off it.
	const int32 FirstX = FMath::CeilToInt32(MinMetres.X / Pitch);
	const int32 LastX = FMath::FloorToInt32(MaxMetres.X / Pitch);
	const int32 FirstY = FMath::CeilToInt32(MinMetres.Y / Pitch);
	const int32 LastY = FMath::FloorToInt32(MaxMetres.Y / Pitch);

	const int32 Major = FMath::Max(1, MajorEvery);

	for (int32 Index = FirstX; Index <= LastX; ++Index)
	{
		const bool bMajor = (Index % Major) == 0;
		const double X = Index * Pitch;
		AddLine(bMajor ? MajorLines : MinorLines,
			FVector2D(X, MinMetres.Y), FVector2D(X, MaxMetres.Y),
			bMajor ? MajorWidthCm : MinorWidthCm);
	}

	for (int32 Index = FirstY; Index <= LastY; ++Index)
	{
		const bool bMajor = (Index % Major) == 0;
		const double Y = Index * Pitch;
		AddLine(bMajor ? MajorLines : MinorLines,
			FVector2D(MinMetres.X, Y), FVector2D(MaxMetres.X, Y),
			bMajor ? MajorWidthCm : MinorWidthCm);
	}

	UE_LOG(LogFactorySim, Verbose, TEXT("Floor grid: %d minor, %d major lines at %.2f m pitch"),
		MinorLines->GetInstanceCount(), MajorLines->GetInstanceCount(), Pitch);
}
