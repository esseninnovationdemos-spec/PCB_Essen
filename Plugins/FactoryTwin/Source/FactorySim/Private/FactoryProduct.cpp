#include "FactoryProduct.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FactoryShapeMaterials.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* CubeMesh = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* SphereMesh = TEXT("/Engine/BasicShapes/Sphere.Sphere");

	// Everything is laid out relative to the carrier's centre, in centimetres,
	// with the carrier 2 cm thick so its top face sits at z = 1.
	const FVector CarrierSize(0.34, 0.26, 0.03);
	const FVector HousingSize(0.18, 0.12, 0.045);
	const FVector ConnectorSize(0.05, 0.09, 0.03);
	const FVector BoardSize(0.15, 0.095, 0.004);
	const FVector LidSize(0.185, 0.125, 0.008);
	const FVector CartonSize(0.34, 0.24, 0.16);
	const FVector LampSize(0.03, 0.03, 0.03);
}

AFactoryProduct::AFactoryProduct()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Carrier   = CreatePart(TEXT("Carrier"),   CarrierSize,   FVector(0.0, 0.0, 0.0));
	Housing   = CreatePart(TEXT("Housing"),   HousingSize,   FVector(0.0, 0.0, 3.25));
	Connector = CreatePart(TEXT("Connector"), ConnectorSize, FVector(11.5, 0.0, 3.25));
	// The board sits on the housing rather than inside it. A unit that has been
	// populated should look populated; a board hidden in a closed box would be
	// realistic and useless.
	Board     = CreatePart(TEXT("Board"),     BoardSize,     FVector(0.0, 0.0, 5.7));
	Lid       = CreatePart(TEXT("Lid"),       LidSize,       FVector(0.0, 0.0, 6.3));
	Carton    = CreatePart(TEXT("Carton"),    CartonSize,    FVector(0.0, 0.0, 9.0));
	StatusLamp = CreatePart(TEXT("StatusLamp"), LampSize,    FVector(-12.0, 0.0, 3.0));
}

UStaticMeshComponent* AFactoryProduct::CreatePart(
	const TCHAR* Name, const FVector& SizeMetres, const FVector& CentreCm)
{
	UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(GetRootComponent());
	Part->SetRelativeLocation(CentreCm);
	Part->SetRelativeScale3D(SizeMetres);
	// Units are carried by the line, not by physics, and must never shove a
	// machine or block a trace.
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	return Part;
}

void AFactoryProduct::TintPart(UStaticMeshComponent* Part, const TCHAR* MaterialName)
{
	if (Part == nullptr)
	{
		return;
	}
	// Leave the mesh's own material in place if the assets have not been
	// generated: an untinted unit still reads, a black one does not.
	if (UMaterialInterface* Tint = FactoryShapeMaterials::Load(MaterialName))
	{
		Part->SetMaterial(0, Tint);
	}
}

void AFactoryProduct::BuildVisuals()
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, CubeMesh);
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, SphereMesh);

	const TPair<UStaticMeshComponent*, const TCHAR*> Boxes[] = {
		{ Carrier,   FactoryShapeMaterials::Carrier },
		{ Housing,   FactoryShapeMaterials::Housing },
		{ Connector, FactoryShapeMaterials::Connector },
		{ Board,     FactoryShapeMaterials::Board },
		{ Lid,       FactoryShapeMaterials::Lid },
		{ Carton,    FactoryShapeMaterials::Carton },
	};

	for (const TPair<UStaticMeshComponent*, const TCHAR*>& Entry : Boxes)
	{
		if (Entry.Key != nullptr)
		{
			Entry.Key->SetStaticMesh(Cube);
			TintPart(Entry.Key, Entry.Value);
		}
	}

	if (StatusLamp != nullptr)
	{
		StatusLamp->SetStaticMesh(Sphere);
	}

	ApplyStage();
}

void AFactoryProduct::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildVisuals();
}

void AFactoryProduct::SetStage(const EFactoryProductStage NewStage)
{
	Stage = NewStage;
	ApplyStage();
}

void AFactoryProduct::SetPassed(const bool bInPassed)
{
	bPassed = bInPassed;
	ApplyStage();
}

void AFactoryProduct::ApplyStage()
{
	// Compared with >=, so each part stays on once its step has happened.
	const auto ShowFrom = [this](UStaticMeshComponent* Part, const EFactoryProductStage From)
	{
		if (Part != nullptr)
		{
			Part->SetVisibility(Stage >= From, /*bPropagateToChildren*/ true);
		}
	};

	ShowFrom(Carrier,   EFactoryProductStage::Empty);
	ShowFrom(Housing,   EFactoryProductStage::HousingFitted);
	ShowFrom(Connector, EFactoryProductStage::PinsInserted);
	ShowFrom(Board,     EFactoryProductStage::BoardFitted);
	ShowFrom(Lid,       EFactoryProductStage::LidFitted);
	ShowFrom(Carton,    EFactoryProductStage::Packed);
	ShowFrom(StatusLamp, EFactoryProductStage::Tested);

	// The lamp only means anything once something has tested the unit.
	if (StatusLamp != nullptr && Stage >= EFactoryProductStage::Tested)
	{
		TintPart(StatusLamp, bPassed
			? FactoryShapeMaterials::LampPass
			: FactoryShapeMaterials::LampFail);
	}
}
