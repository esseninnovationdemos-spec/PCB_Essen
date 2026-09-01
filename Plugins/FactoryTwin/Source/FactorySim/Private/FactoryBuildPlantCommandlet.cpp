#include "FactoryBuildPlantCommandlet.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FactoryConveyor.h"
#include "FactoryLayoutGrid.h"
#include "FactoryMachineComponent.h"
#include "FactoryMachineInstance.h"
#include "FactoryOperatorStation.h"
#include "FactoryProductionLine.h"
#include "FactoryShapeMaterials.h"
#include "FactorySimTypes.h"
#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Misc/Paths.h"
#include "GameFramework/PlayerStart.h"

namespace PlantBuild
{
	/** Station device suffix -> the Blueprint that represents it. */
	struct FStationVisual
	{
		const TCHAR* Device;
		const TCHAR* Blueprint;
		EFactoryProductStage StageOnComplete;
	};

	// Process order, matching the seed template. Stages advance only where a
	// part is actually fitted: an inspection decides whether what is already
	// there passes, so it leaves the stage where it found it.
	const FStationVisual Stations[] = {
		{ TEXT("LOADER"),            TEXT("/Game/SMT-Workcenter/Loader/Loader_BP.Loader_BP_C"),
		  EFactoryProductStage::Empty },
		{ TEXT("LASER_MARKING"),     TEXT("/Game/SMT-Workcenter/LaserMarking/Machine_LaserMarking_BP.Machine_LaserMarking_BP_C"),
		  EFactoryProductStage::Empty },
		{ TEXT("SOLDER_PASTE"),      TEXT("/Game/SMT-Workcenter/SolderPasteStation/Solder_Paste_Station_BP.Solder_Paste_Station_BP_C"),
		  EFactoryProductStage::Empty },
		{ TEXT("SOLDER_INSP"),       TEXT("/Game/SMT-Workcenter/SolderPasteInspection/Solder_Inspection_BP.Solder_Inspection_BP_C"),
		  EFactoryProductStage::Empty },
		{ TEXT("COMPONENT_PLACER"),  TEXT("/Game/SMT-Workcenter/ComponentPlacer/Components_Placer_BP.Components_Placer_BP_C"),
		  EFactoryProductStage::BoardFitted },
		{ TEXT("REFLOW_OVEN"),       TEXT("/Game/SMT-Workcenter/ReflowOven/ReflowOven_BP.ReflowOven_BP_C"),
		  EFactoryProductStage::BoardFitted },
		{ TEXT("AUTO_OPTICALINSP"),  TEXT("/Game/SMT-Workcenter/AutomaticOpticalInspection/AutomaticOpticalInspection_BP.AutomaticOpticalInspection_BP_C"),
		  EFactoryProductStage::BoardFitted },
		{ TEXT("PCB_CLEANER"),       TEXT("/Game/SMT-Workcenter/PCB_Cleaner/PCB_Cleaner_BP.PCB_Cleaner_BP_C"),
		  EFactoryProductStage::BoardFitted },

		{ TEXT("HOUSING_ASSEMBLY"),  TEXT("/Game/FinalAssembly-Workcenter/HousingAssembly/HousingAssembly_BP.HousingAssembly_BP_C"),
		  EFactoryProductStage::HousingFitted },
		{ TEXT("PIN_INSERTION"),     TEXT("/Game/FinalAssembly-Workcenter/SinglePinInsertion/SinglePinInsertion_BP.SinglePinInsertion_BP_C"),
		  EFactoryProductStage::PinsInserted },
		{ TEXT("PIN_INSPECTION"),    TEXT("/Game/FinalAssembly-Workcenter/PinVerification/PinInsertionCheck_BP.PinInsertionCheck_BP_C"),
		  EFactoryProductStage::PinsInserted },
		{ TEXT("ASSEMBLY_ROBOT"),    TEXT("/Game/UR5_DT/UR5/UR5_BP.UR5_BP_C"),
		  EFactoryProductStage::BoardFitted },
		{ TEXT("ICT"),               TEXT("/Game/FinalAssembly-Workcenter/ICT(ElectricalTest)/ICT_BP.ICT_BP_C"),
		  EFactoryProductStage::Tested },
		{ TEXT("FLASH_PROGRAMMING"), TEXT("/Game/FinalAssembly-Workcenter/FlashProgramming/Flash_Programming_BP.Flash_Programming_BP_C"),
		  EFactoryProductStage::Programmed },
		{ TEXT("PIN_CHECK"),         TEXT("/Game/FinalAssembly-Workcenter/PinCheck(AfterAssembly)/PinCheckAfterAssembly_BP.PinCheckAfterAssembly_BP_C"),
		  EFactoryProductStage::LidFitted },
		{ TEXT("EOL_TEST"),          TEXT("/Game/FinalAssembly-Workcenter/EndOfLine/EndOfLine_Inspection_BP.EndOfLine_Inspection_BP_C"),
		  EFactoryProductStage::FunctionTested },
		{ TEXT("PACKAGING"),         TEXT("/Game/SMT-Workcenter/Loader/Loader_BP.Loader_BP_C"),
		  EFactoryProductStage::Packed },
	};

	/**
	 * One box or cylinder of the control cabinet.
	 *
	 * Sizes and centres are in metres and in the cabinet's own frame: origin on
	 * the floor at the middle of its footprint, +X to its right, +Y towards its
	 * back, +Z up. The engine's basic Cube and Cylinder are both 100 units, so
	 * with 1 uu to the centimetre a scale of 1 is a metre and the numbers below
	 * read as the real dimensions they are.
	 */
	struct FCabinetPart
	{
		const TCHAR* Label;
		FVector CentreMetres;
		FVector SizeMetres;
		const TCHAR* Material;
		bool bCylinder = false;
	};

	/**
	 * A groov EPIC in an open-front enclosure, built from primitives.
	 *
	 * Open-front deliberately: a closed cabinet is a grey box, and the point of
	 * putting the controller in the level is that you can see it. What is inside
	 * is what an EPIC actually looks like -- power supply, processor with a
	 * display, then I/O modules on a DIN rail, colour-coded by function the way
	 * Opto 22 codes them.
	 *
	 * Placeholder geometry, and good enough to read at demo distance. It is
	 * sized from the real chassis so a modelled replacement can drop into the
	 * same footprint without moving anything around it.
	 */
	const FCabinetPart CabinetParts[] = {
		// --- enclosure
		{ TEXT("Plinth"),      { 0.000,  0.000, 0.060 }, { 0.860, 0.420, 0.120 },
		  FactoryShapeMaterials::MachineFrame },
		{ TEXT("BackPanel"),   { 0.000,  0.170, 0.790 }, { 0.800, 0.040, 1.300 },
		  FactoryShapeMaterials::CabinetShell },
		{ TEXT("SideLeft"),    {-0.380,  0.000, 0.790 }, { 0.040, 0.380, 1.300 },
		  FactoryShapeMaterials::CabinetShell },
		{ TEXT("SideRight"),   { 0.380,  0.000, 0.790 }, { 0.040, 0.380, 1.300 },
		  FactoryShapeMaterials::CabinetShell },
		{ TEXT("Roof"),        { 0.000,  0.000, 1.465 }, { 0.800, 0.400, 0.050 },
		  FactoryShapeMaterials::CabinetShell },
		{ TEXT("Shelf"),       { 0.000,  0.000, 0.160 }, { 0.760, 0.360, 0.040 },
		  FactoryShapeMaterials::CabinetShell },

		// --- the chassis on its rail
		{ TEXT("DinRail"),     { 0.000,  0.120, 1.015 }, { 0.720, 0.035, 0.035 },
		  FactoryShapeMaterials::SteelPolished },
		{ TEXT("PowerSupply"), {-0.300,  0.100, 1.095 }, { 0.090, 0.110, 0.130 },
		  FactoryShapeMaterials::MachineFrame },
		{ TEXT("Processor"),   {-0.175,  0.100, 1.095 }, { 0.130, 0.110, 0.130 },
		  FactoryShapeMaterials::ModuleFace },
		{ TEXT("Display"),     {-0.175,  0.043, 1.105 }, { 0.085, 0.008, 0.050 },
		  FactoryShapeMaterials::GridMajor },

		// I/O modules, colour-coded by function as Opto 22 codes them:
		// black digital in, red digital out, white analog in, blue analog out.
		{ TEXT("ModuleDigIn"), {-0.062,  0.100, 1.095 }, { 0.038, 0.110, 0.130 },
		  FactoryShapeMaterials::Connector },
		{ TEXT("ModuleDigOut"),{-0.018,  0.100, 1.095 }, { 0.038, 0.110, 0.130 },
		  FactoryShapeMaterials::LampFail },
		{ TEXT("ModuleAnaIn"), { 0.026,  0.100, 1.095 }, { 0.038, 0.110, 0.130 },
		  FactoryShapeMaterials::Lid },
		{ TEXT("ModuleAnaOut"),{ 0.070,  0.100, 1.095 }, { 0.038, 0.110, 0.130 },
		  FactoryShapeMaterials::GridMajor },

		// --- the wiring nobody models and every panel has
		{ TEXT("TerminalStrip"),{ 0.000, 0.110, 0.880 }, { 0.720, 0.060, 0.050 },
		  FactoryShapeMaterials::Connector },
		{ TEXT("WiringDuct"),  { 0.000,  0.130, 0.720 }, { 0.720, 0.050, 0.090 },
		  FactoryShapeMaterials::CabinetShell },

		// --- stack light, the three lamps the PAC Control strategy drives
		{ TEXT("StackPole"),   { 0.300,  0.000, 1.540 }, { 0.028, 0.028, 0.100 },
		  FactoryShapeMaterials::MachineFrame, true },
		{ TEXT("StackGreen"),  { 0.300,  0.000, 1.620 }, { 0.075, 0.075, 0.060 },
		  FactoryShapeMaterials::LampPass, true },
		{ TEXT("StackAmber"),  { 0.300,  0.000, 1.680 }, { 0.075, 0.075, 0.060 },
		  FactoryShapeMaterials::LampWarn, true },
		{ TEXT("StackRed"),    { 0.300,  0.000, 1.740 }, { 0.075, 0.075, 0.060 },
		  FactoryShapeMaterials::LampFail, true },
	};

	/**
	 * Spawns the cabinet and returns it, or null if the primitives are missing.
	 *
	 * Components are added to the actor instance rather than to a Blueprint, so
	 * the whole thing lives in the generated level and re-running the build is
	 * how you change it.
	 */
	AActor* BuildControlCabinet(
		UWorld* World, const FTransform& Where, const int32 Line, int32& OutParts)
	{
		UStaticMesh* Cube = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		UStaticMesh* Cylinder = LoadObject<UStaticMesh>(
			nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		if (Cube == nullptr || Cylinder == nullptr)
		{
			UE_LOG(LogFactorySim, Warning,
				TEXT("Engine basic shapes are missing; skipping the control cabinet"));
			return nullptr;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* Cabinet = World->SpawnActor<AActor>(AActor::StaticClass(), Where, Params);
		if (Cabinet == nullptr)
		{
			return nullptr;
		}

		Cabinet->SetActorLabel(FString::Printf(TEXT("L%d_ControlCabinet"), Line));

		USceneComponent* Root = NewObject<USceneComponent>(Cabinet, TEXT("Root"));
		Cabinet->SetRootComponent(Root);
		Root->RegisterComponent();

		// After the root exists, not before. A bare AActor has no root at spawn
		// time, so the transform handed to SpawnActor has nothing to apply
		// itself to and is silently dropped -- which put all three cabinets on
		// top of each other at the world origin.
		Cabinet->SetActorTransform(Where);

		for (const FCabinetPart& Part : CabinetParts)
		{
			UStaticMeshComponent* Piece =
				NewObject<UStaticMeshComponent>(Cabinet, Part.Label);
			Piece->SetStaticMesh(Part.bCylinder ? Cylinder : Cube);
			Piece->SetupAttachment(Root);
			Piece->SetRelativeLocation(Part.CentreMetres * FactoryGrid::MetresToCm);
			Piece->SetRelativeScale3D(Part.SizeMetres);

			// Nothing walks into a panel, and collision on twenty boxes is cost
			// for no behaviour.
			Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);

			if (UMaterialInterface* Material = FactoryShapeMaterials::Load(Part.Material))
			{
				Piece->SetMaterial(0, Material);
			}

			Piece->RegisterComponent();
			Cabinet->AddInstanceComponent(Piece);
			++OutParts;
		}

		return Cabinet;
	}

	/** Benches worked by a person rather than a machine. */
	bool IsOperatorBench(const FString& DeviceSuffix)
	{
		return DeviceSuffix == TEXT("HOUSING_ASSEMBLY")
			|| DeviceSuffix == TEXT("PIN_INSPECTION");
	}

	const FString InstanceFolder = TEXT("/Game/FactoryTwin/Instances/Plant");

	/** Assigns an object-typed Blueprint variable on a spawned actor. */
	bool SetActorObjectProperty(AActor* Actor, const FName PropertyName, UObject* Value)
	{
		if (Actor == nullptr)
		{
			return false;
		}
		FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(
			Actor->GetClass()->FindPropertyByName(PropertyName));
		if (Property == nullptr)
		{
			return false;
		}
		Property->SetObjectPropertyValue(
			Property->ContainerPtrToValuePtr<void>(Actor), Value);
		return true;
	}

	UFactoryMachineInstance* LoadInstance(const int32 Line, const TCHAR* DeviceSuffix)
	{
		const FString Name = FString::Printf(TEXT("I_L%d_%s"), Line, DeviceSuffix);
		return LoadObject<UFactoryMachineInstance>(
			nullptr, *FString::Printf(TEXT("%s/%s.%s"), *InstanceFolder, *Name, *Name));
	}
}

using namespace PlantBuild;

UFactoryBuildPlantCommandlet::UFactoryBuildPlantCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UFactoryBuildPlantCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	const FString LevelPath = ParamMap.Contains(TEXT("Level"))
		? ParamMap[TEXT("Level")] : TEXT("/Game/level4");
	int32 Lines = 3;
	if (ParamMap.Contains(TEXT("Lines")))
	{
		Lines = FMath::Clamp(FCString::Atoi(*ParamMap[TEXT("Lines")]), 1, 8);
	}

	UE_LOG(LogFactorySim, Display, TEXT("Building %s: %d line(s)"), *LevelPath, Lines);

	FactoryShapeMaterials::EnsureAll();

	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/ false);
	if (World == nullptr)
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not create a blank map"));
		return 1;
	}

	// Import the hall if it is not already in the project. Done here rather than
	// by hand in the editor so a fresh clone can build this level from the FBX
	// alone -- and because an import made in an editor session that is never
	// saved is discarded when the editor closes, which is exactly how the first
	// attempt at this produced a level with no building in it.
	{
		const FAssetRegistryModule& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		FARFilter Present;
		Present.bRecursivePaths = true;
		Present.PackagePaths.Add(FName(TEXT("/Game/Environment/CGTrader_Warehouse")));
		Present.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
		TArray<FAssetData> Already;
		Registry.Get().GetAssets(Present, Already);

		if (Already.Num() == 0)
		{
			const FString Source = FPaths::ProjectDir() / TEXT("warehouse6.fbx");
			if (!FPaths::FileExists(Source))
			{
				UE_LOG(LogFactorySim, Warning,
					TEXT("  hall source '%s' not found; the level will have no building"),
					*Source);
			}
			else
			{
				UAssetImportTask* Task = NewObject<UAssetImportTask>();
				Task->Filename = Source;
				Task->DestinationPath = TEXT("/Game/Environment/CGTrader_Warehouse");
				Task->DestinationName = TEXT("SM_Warehouse_Hall");
				Task->bAutomated = true;
				Task->bSave = true;
				Task->bReplaceExisting = true;

				TArray<UAssetImportTask*> Tasks = { Task };
				FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
				UE_LOG(LogFactorySim, Display,
					TEXT("  hall imported from %s (%d object(s))"),
					*Source, Task->GetObjects().Num());
			}
		}
	}

	// The hall, in the parts it was imported as. Nanite is switched on because
	// this building is 1.9 million triangles -- 89 times the project's other
	// warehouse, for a smaller shell -- which is squarely the workload Nanite
	// exists for, unlike the ten-thousand-triangle machines standing in it.
	// Lanes run along the hall's length. A line packs to 27.5 m and the hall is
	// 17.3 m across, so laid the other way each one would run out through a wall.
	constexpr double LaneSpacing = 5.0;
	constexpr double LineStartY = -13.0;

	// The floor a line needs, plus room to stand at it. Anything the building
	// keeps in here has to go: the hall ships as a storage warehouse, with
	// racking and pallets spread across exactly the floor the lines want.
	constexpr double CorridorHalfWidth = 2.2;
	TArray<FBox> Corridors;
	for (int32 Lane = 1; Lane <= Lines; ++Lane)
	{
		const double LaneX = (Lane - (Lines + 1) * 0.5) * LaneSpacing;
		Corridors.Add(FBox(
			FVector((LaneX - CorridorHalfWidth) * FactoryGrid::MetresToCm,
			        (LineStartY - 2.0) * FactoryGrid::MetresToCm, -100.0),
			FVector((LaneX + CorridorHalfWidth) * FactoryGrid::MetresToCm,
			        (LineStartY + 30.0) * FactoryGrid::MetresToCm, 400.0)));
	}

	int32 HallParts = 0;
	int32 HallSkipped = 0;
	int32 NaniteEnabled = 0;
	FBox HallBounds(ForceInit);
	{
		// Gathered from the folder rather than a list written here: the importer
		// names the parts from the FBX's own node names, there are 28 of them,
		// and a list typed out by hand was simply wrong -- which is how the
		// first build produced a level with no building in it and said so.
		const FAssetRegistryModule& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(FName(TEXT("/Game/Environment/CGTrader_Warehouse")));
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());

		TArray<FAssetData> Found;
		Registry.Get().GetAssets(Filter, Found);
		Found.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.AssetName.LexicalLess(B.AssetName);
		});

		TArray<UPackage*> ToSave;
		int64 TotalTriangles = 0;

		for (const FAssetData& Asset : Found)
		{
			UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset());
			if (Mesh == nullptr)
			{
				continue;
			}

			// This building is 1.9 million triangles, 89 times the project's
			// other warehouse for a smaller shell. That is squarely what Nanite
			// is for -- unlike the ten-thousand-triangle machines standing in
			// it, where it would have been overhead for nothing.
			if (!Mesh->IsNaniteEnabled())
			{
				Mesh->NaniteSettings.bEnabled = true;
				Mesh->Modify();
				Mesh->PostEditChange();
				ToSave.Add(Mesh->GetOutermost());
				++NaniteEnabled;
			}
			if (Mesh->GetRenderData() != nullptr && Mesh->GetNumLODs() > 0)
			{
				TotalTriangles += Mesh->GetNumTriangles(0);
			}

			const FString PartName = Asset.AssetName.ToString();

			// The shell and anything fixed to it stay whatever they overlap -- a
			// line running through the floor is the floor's business, not a
			// reason to delete the floor. Stairs, switchgear, the transformer and
			// the air handling are part of the building too.
			//
			// The cull below is written as a corridor test because that is the
			// right rule, but note it can only ever act on a whole prop type:
			// the FBX batches every instance of a material into one mesh, so
			// "the cardboard" is a single object spanning the hall rather than
			// two hundred boxes that could be thinned individually.
			const bool bBelongsToBuilding =
				PartName.Contains(TEXT("rangka")) || PartName.Contains(TEXT("dinding"))
				|| PartName.Contains(TEXT("lantai")) || PartName.Contains(TEXT("besi_lis"))
				|| PartName.Contains(TEXT("gerbang")) || PartName.Contains(TEXT("cat_garis"))
				|| PartName.Contains(TEXT("lampu")) || PartName.Contains(TEXT("kabel"))
				|| PartName.Contains(TEXT("tangga")) || PartName.Contains(TEXT("trafo"))
				|| PartName.Contains(TEXT("saklar")) || PartName.Contains(TEXT("AC"))
				|| PartName.Contains(TEXT("PEMADAM")) || PartName.Contains(TEXT("KLLNG"))
				|| PartName.Contains(TEXT("lemari")) || PartName.Contains(TEXT("meja"))
				|| PartName.Contains(TEXT("ondo"));

			const bool bStructural = bBelongsToBuilding;

			if (!bStructural)
			{
				const FBox PartBox = Mesh->GetBoundingBox();
				bool bInTheWay = false;
				for (const FBox& Corridor : Corridors)
				{
					if (Corridor.Intersect(PartBox))
					{
						bInTheWay = true;
						break;
					}
				}
				if (bInTheWay)
				{
					++HallSkipped;
					continue;
				}
			}

			if (AStaticMeshActor* Piece = World->SpawnActor<AStaticMeshActor>(
				FVector::ZeroVector, FRotator::ZeroRotator))
			{
				Piece->SetMobility(EComponentMobility::Static);
				Piece->GetStaticMeshComponent()->SetStaticMesh(Mesh);
				Piece->SetActorLabel(FString::Printf(TEXT("Hall_%s"), *Asset.AssetName.ToString()));
				++HallParts;

				// The shell is what bounds the level. Only the structure counts:
				// a stack of boxes against a wall would drag the extent inward
				// and put the viewing position inside the wall instead.
				if (PartName.Contains(TEXT("rangka")) || PartName.Contains(TEXT("dinding"))
					|| PartName.Contains(TEXT("lantai")))
				{
					HallBounds += Mesh->GetBoundingBox();
				}
			}
		}

		if (ToSave.Num() > 0)
		{
			UEditorLoadingAndSavingUtils::SavePackages(ToSave, /*bOnlyDirty*/ false);
		}
		UE_LOG(LogFactorySim, Display,
			TEXT("  hall: %d part(s) placed, %d skipped as standing in a line, "
			     "%lld triangles, Nanite switched on for %d"),
			HallParts, HallSkipped, TotalTriangles, NaniteEnabled);
	}

	int32 TotalPlaced = 0;
	int32 TotalOperators = 0;
	/// Material slots repainted in a machine finish, for the summary line.
	int32 TotalSlotsFinished = 0;
	/// SceneCapture components switched off free-running, for the summary line.
	int32 TotalCapturesTamed = 0;
	/// Station-internal lights switched to non-shadow-casting.
	int32 TotalLightsUnshadowed = 0;
	/// Control cabinets placed, and the primitives they are built from.
	int32 TotalCabinets = 0;
	int32 TotalCabinetParts = 0;

	for (int32 Line = 1; Line <= Lines; ++Line)
	{
		const double LaneX = (Line - (Lines + 1) * 0.5) * LaneSpacing;

		// Local X along the line becomes world Y; the machines turn to match.
		auto ToWorld = [&](const FVector2D& Local) -> FVector
		{
			return FactoryGrid::MetresToWorld(
				FVector2D(LaneX + Local.Y, LineStartY + Local.X));
		};

		AFactoryProductionLine* Belt = nullptr;
		double FirstX = TNumericLimits<double>::Max();
		double LastX = TNumericLimits<double>::Lowest();

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		TArray<FFactoryLineStop> Stops;

		for (const FStationVisual& Station : Stations)
		{
			UFactoryMachineInstance* Instance = LoadInstance(Line, Station.Device);
			if (Instance == nullptr)
			{
				UE_LOG(LogFactorySim, Warning,
					TEXT("  L%d %s: no instance; run -run=FactorySeedPlant first"),
					Line, Station.Device);
				continue;
			}

			UClass* StationClass = LoadObject<UClass>(nullptr, Station.Blueprint);
			if (StationClass != nullptr)
			{
				FActorSpawnParameters StationParams = Params;
				StationParams.Name = MakeUniqueObjectName(
					World->PersistentLevel, StationClass, FName(*Instance->GetLevelLabel()));

				// Turned a quarter so the machines face across a line that runs
				// along Y rather than the X they were authored for.
				const FTransform Where(FRotator(0.0, 90.0, 0.0),
					ToWorld(Instance->LayoutPosition));

				if (AActor* Actor = World->SpawnActor<AActor>(StationClass, Where, StationParams))
				{
					Actor->SetActorLabel(Instance->GetLevelLabel());
					++TotalPlaced;
					TotalSlotsFinished += FactoryShapeMaterials::ApplyMachineFinish(Actor);

					// Seven of the station Blueprints carry an inspection
					// monitor backed by a SceneCaptureComponent2D, and each one
					// re-renders the whole scene every frame. Profiled at 42.3 ms
					// across the twenty-one placed stations -- 76% of a 55.9 ms
					// frame, against 14.4 ms for the actual player view. Nothing
					// reads these targets at a glance; the monitors are a few
					// centimetres of screen space on a machine panel.
					//
					// Left enabled but no longer free-running: they capture when
					// something explicitly asks, so the cost is paid on demand
					// rather than 60 times a second.
					TArray<USceneCaptureComponent2D*> Captures;
					Actor->GetComponents<USceneCaptureComponent2D>(Captures);
					for (USceneCaptureComponent2D* Capture : Captures)
					{
						if (Capture == nullptr)
						{
							continue;
						}
						Capture->bCaptureEveryFrame = false;
						Capture->bCaptureOnMovement = false;
						// Without this the capture keeps a full set of rendering
						// state resident per component even while idle.
						Capture->bAlwaysPersistRenderingState = false;
						++TotalCapturesTamed;
					}

					// Task lights inside the bench Blueprints cast cubemap
					// shadows. Each one costs 1.8-3.2 ms -- for a 32x32 cubemap,
					// so the price is scene traversal per face, not resolution --
					// and six benches were spending 16 ms of a 20.5 ms
					// ShadowDepths pass between them. The sun, by comparison,
					// costs 3.05 ms for the whole hall.
					//
					// They stay lit; they just stop casting. Same trade already
					// made for the bay lamps: point lights here are fill, and
					// Lumen supplies the contact darkening that sells the shape.
					TArray<ULightComponent*> Lights;
					Actor->GetComponents<ULightComponent>(Lights);
					for (ULightComponent* Light : Lights)
					{
						if (Light != nullptr && Light->CastShadows)
						{
							Light->SetCastShadows(false);
							++TotalLightsUnshadowed;
						}
					}

					// The UR5 drives its arm from a separate Goal actor holding the
					// waypoint path, and the two hold references to each other.
					// Placed without one, its animation Blueprint reads a null
					// Goal every frame and says so, once per frame, forever.
					if (Actor->GetClass()->FindPropertyByName(TEXT("Goal Ref")) != nullptr)
					{
						if (UClass* GoalClass = LoadObject<UClass>(
							nullptr, TEXT("/Game/UR5_DT/UR5/Goal_BP.Goal_BP_C")))
						{
							FActorSpawnParameters GoalParams;
							GoalParams.SpawnCollisionHandlingOverride =
								ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
							GoalParams.Name = MakeUniqueObjectName(World->PersistentLevel,
								GoalClass, FName(*(Instance->GetLevelLabel() + TEXT("_Goal"))));

							FTransform GoalWhere = Where;
							GoalWhere.SetLocation(Where.GetLocation() + FVector(0.0, 0.0, 60.0));

							if (AActor* Goal = World->SpawnActor<AActor>(
								GoalClass, GoalWhere, GoalParams))
							{
								Goal->SetActorLabel(Instance->GetLevelLabel() + TEXT("_Goal"));
								SetActorObjectProperty(Actor, TEXT("Goal Ref"), Goal);
								SetActorObjectProperty(Goal, TEXT("UR5e Ref"), Actor);
							}
						}
					}

					// Machines placed from a Blueprint carry their own machine
					// component; give it this line's instance so it publishes as
					// its own device rather than sharing the template's identity.
					if (UFactoryMachineComponent* Machine =
						Actor->FindComponentByClass<UFactoryMachineComponent>())
					{
						Machine->Instance = Instance;
					}
					else
					{
						UFactoryMachineComponent* Added = NewObject<UFactoryMachineComponent>(
							Actor, UFactoryMachineComponent::StaticClass(), TEXT("FactoryMachine"));
						Added->Instance = Instance;
						Added->RegisterComponent();
						Actor->AddInstanceComponent(Added);
					}
				}
			}

			// Stops sit on the belt centreline even for machines set back from
			// it: the robot reaches across to the unit, not the other way round.
			FFactoryLineStop Stop;
			// The full UNS path, not the bare device id: every line runs the same
			// station names, so "ReflowOven" alone would resolve to whichever
			// line registered first and all three lines would drive one machine.
			Stop.DeviceId = Instance->GetUnsPath();
			Stop.PositionMetres = FVector2D(LaneX, LineStartY + Instance->LayoutPosition.X);
			Stop.StageOnComplete = Station.StageOnComplete;
			Stops.Add(Stop);

			FirstX = FMath::Min(FirstX, Instance->LayoutPosition.X);
			LastX = FMath::Max(LastX, Instance->LayoutPosition.X);

			if (IsOperatorBench(Station.Device))
			{
				if (AFactoryOperatorStation* Worker = World->SpawnActor<AFactoryOperatorStation>(
					AFactoryOperatorStation::StaticClass(),
					FTransform(FRotator(0.0, 180.0, 0.0),
						ToWorld(FVector2D(Instance->LayoutPosition.X, -1.2))), Params))
				{
					Worker->ServedDeviceId = Instance->GetUnsPath();
					Worker->SetActorLabel(FString::Printf(TEXT("Operator_%s"), *Instance->GetLevelLabel()));
					++TotalOperators;
				}
			}
		}

		// Conveyor along the lane, running the length of the machines.
		if (LastX > FirstX)
		{
			const double Lead = 1.5;
			if (AFactoryConveyor* Conveyor = World->SpawnActor<AFactoryConveyor>(
				AFactoryConveyor::StaticClass(),
				FTransform(FRotator(0.0, 90.0, 0.0),
					FactoryGrid::MetresToWorld(
						FVector2D(LaneX, LineStartY + FirstX - Lead))), Params))
			{
				Conveyor->LengthMetres = static_cast<float>(LastX - FirstX + Lead * 2.0);
				Conveyor->RebuildConveyor();
				Conveyor->SetActorLabel(FString::Printf(TEXT("L%d_Conveyor"), Line));

				if (UFactoryMachineInstance* ConveyorInstance = LoadInstance(Line, TEXT("CONVEYOR")))
				{
					UFactoryMachineComponent* Machine = NewObject<UFactoryMachineComponent>(
						Conveyor, UFactoryMachineComponent::StaticClass(), TEXT("FactoryMachine"));
					Machine->Instance = ConveyorInstance;
					Machine->RegisterComponent();
					Conveyor->AddInstanceComponent(Machine);
					++TotalPlaced;
				}
			}
		}

		// The control cabinet, at the head of the line and off to the side.
		if (LastX > FirstX)
		{
			int32 PartsBuilt = 0;
			const FTransform CabinetAt(
				FRotator(0.0, 90.0, 0.0),
				FactoryGrid::MetresToWorld(
					// In the aisle beside the line, on the opposite side from the
					// robot arm. 1.9 m out rather than nearer: the racking rows
					// sit between the belt and here, and anything closer ends up
					// behind a shelf where nobody can see it or reach it.
					FVector2D(LaneX - 1.9, LineStartY + FirstX - 0.5)));

			if (AActor* Cabinet = BuildControlCabinet(World, CabinetAt, Line, PartsBuilt))
			{
				TotalCabinetParts += PartsBuilt;

				const FVector At = Cabinet->GetActorLocation();
				UE_LOG(LogFactorySim, Display,
					TEXT("  line %d control cabinet at (%.2f, %.2f) m, %d part(s)"),
					Line, At.X / FactoryGrid::MetresToCm, At.Y / FactoryGrid::MetresToCm,
					PartsBuilt);

				if (UFactoryMachineInstance* ControllerInstance =
					LoadInstance(Line, TEXT("LINE_CONTROLLER")))
				{
					UFactoryMachineComponent* Machine = NewObject<UFactoryMachineComponent>(
						Cabinet, UFactoryMachineComponent::StaticClass(), TEXT("FactoryMachine"));
					Machine->Instance = ControllerInstance;
					Machine->RegisterComponent();
					Cabinet->AddInstanceComponent(Machine);
					++TotalPlaced;
					++TotalCabinets;
				}
				else
				{
					UE_LOG(LogFactorySim, Warning,
						TEXT("  line %d has a cabinet but no LINE_CONTROLLER instance; "
							 "re-run the plant seed"), Line);
				}
			}
		}

		// The line that carries units between the stops and drives them.
		Belt = World->SpawnActor<AFactoryProductionLine>(
			AFactoryProductionLine::StaticClass(),
			FTransform(FRotator::ZeroRotator, FVector::ZeroVector), Params);
		if (Belt != nullptr)
		{
			Belt->SetActorLabel(FString::Printf(TEXT("Line%d"), Line));
			Belt->Stops = Stops;
			Belt->EntryMetres = FVector2D(LaneX, LineStartY + FirstX - 2.0);
			Belt->ExitMetres = FVector2D(LaneX, LineStartY + LastX + 2.0);
			// Qualified for the same reason the stops are: every line has a
			// device called "Conveyor".
			if (const UFactoryMachineInstance* ConveyorInstance =
				LoadInstance(Line, TEXT("CONVEYOR")))
			{
				Belt->ConveyorDeviceId = ConveyorInstance->GetUnsPath();
			}
			// Staggered so the lines do not all release on the same beat, which
			// would make three lines behave like one wide one.
			//
			// Seconds between releases, so this sets how full the line runs. At
			// the original ~20s a unit entered every twenty seconds across
			// seventeen stations and the line was almost always empty: the andon
			// board showed IDLE nearly everywhere and looked broken rather than
			// quiet. Around 5s keeps roughly a dozen units in flight, which
			// reads as a working plant and still leaves the slowest station
			// visibly constraining the ones upstream of it.
			Belt->TaktSeconds = 5.0f + Line * 0.5f;

			// Only the first line puts a panel up: three overlapping panels
			// would be unreadable, and they all report the same plant.
			if (Line == 1)
			{
				if (UClass* HudClass = LoadObject<UClass>(
					nullptr, TEXT("/Game/UI/W_AssemblyPanel.W_AssemblyPanel_C")))
				{
					Belt->HudWidgetClass = HudClass;
				}
			}
		}

		UE_LOG(LogFactorySim, Display,
			TEXT("  line %d at x=%.1f m: %d stop(s), %.1f m long"),
			Line, LaneX, Stops.Num(), LastX - FirstX);
	}

	// The line controller. Without it nothing calls StartLine, so the edge node
	// never announces, no device registers, and every production line sits
	// waiting on an OnLineOnlineChanged that will not come -- which is exactly
	// what the first build of this level did: 54 machines and complete silence.
	if (UClass* ManagerClass = LoadObject<UClass>(
		nullptr, TEXT("/Game/BP_MQTT_Manager.BP_MQTT_Manager_C")))
	{
		FActorSpawnParameters ManagerParams;
		ManagerParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (AActor* Manager = World->SpawnActor<AActor>(ManagerClass,
			FTransform(FRotator::ZeroRotator,
				FactoryGrid::MetresToWorld(FVector2D(-7.0, 0.0))), ManagerParams))
		{
			Manager->SetActorLabel(TEXT("LineController"));
			UE_LOG(LogFactorySim, Display, TEXT("  line controller placed"));
		}
	}
	else
	{
		UE_LOG(LogFactorySim, Warning,
			TEXT("  BP_MQTT_Manager not found; the lines will not start on their own"));
	}

	// Daylight outside, and bay lighting inside because a roof keeps the sun out.
	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
		FVector(0.0, 0.0, 800.0), FRotator(-50.0, -35.0, 0.0)))
	{
		Sun->SetActorLabel(TEXT("Sun"));
		Sun->GetComponent()->SetIntensity(10.0f);
		// 20 m of cascades over two splits, not 60 m over four. ShadowDepths
		// profiled at 9.16 ms -- 64% of the 14.4 ms player view -- for a light
		// contributing 10 lux through a closed roof. The hall is 17 m across, so
		// cascades reaching 60 m were shadowing empty ground outside the
		// building, and the bay lamps are fill-only and cast nothing.
		Sun->GetComponent()->DynamicShadowDistanceMovableLight = 2000.0f;
		Sun->GetComponent()->DynamicShadowCascades = 2;
		Sun->GetComponent()->CascadeDistributionExponent = 2.0f;
		// Late-afternoon sun rather than noon. It contributes little inside --
		// that is what the roof is for -- but what does reach the floor through
		// the roof lights should agree with the lamps rather than fight them.
		Sun->GetComponent()->bUseTemperature = true;
		Sun->GetComponent()->Temperature = 4800.0f;
	}
	World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.0, 0.0, 500.0), FRotator::ZeroRotator))
	{
		Sky->SetActorLabel(TEXT("SkyLight"));
		// Dimmed, but not out. At full strength the captured sky was the dominant
		// light indoors and put a cold cast over every machine in the hall, so no
		// amount of warmth at the lamps read as warm. Cutting it to a fifth went
		// too far the other way and the whole hall came out sepia: with nothing
		// neutral left to balance them, the lamps just tinted everything. This is
		// the level where white surfaces still read as white.
		Sky->GetLightComponent()->SetIntensity(0.55f);
		// Captured once. Nothing in this sky moves, so re-rendering it every
		// frame buys nothing.
		Sky->GetLightComponent()->bRealTimeCapture = false;
		Sky->GetLightComponent()->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	}

	{
		const int32 Rows = 4;
		const int32 Cols = FMath::Max(1, Lines);
		for (int32 Row = 0; Row < Rows; ++Row)
		{
			for (int32 Col = 0; Col < Cols; ++Col)
			{
				const double X = (Col - (Cols - 1) * 0.5) * 5.0;
				const double Y = -12.0 + Row * 8.0;
				if (APointLight* Bay = World->SpawnActor<APointLight>(
					FactoryGrid::MetresToWorld(FVector2D(X, Y), 430.0), FRotator::ZeroRotator))
				{
					Bay->SetActorLabel(FString::Printf(TEXT("BayLight_%d_%d"), Row + 1, Col + 1));
					Bay->SetMobility(EComponentMobility::Movable);
					if (UPointLightComponent* Light =
						Cast<UPointLightComponent>(Bay->GetLightComponent()))
					{
						// Warm white, and brighter to compensate: a 3000K lamp
						// reads dimmer than a colourless one of the same
						// intensity because so much of its output sits where the
						// eye is less sensitive. This is what actually makes the
						// hall look lit rather than merely visible.
						Light->bUseTemperature = true;
						Light->SetTemperature(3600.0f);
						Light->SetIntensity(40000.0f);
						Light->SetAttenuationRadius(1500.0f);
						// Fill only; the sun casts the shadows. Twelve
						// shadow-casting lamps would look better still and cost
						// interactive frame rate in a level that is already
						// heavy, so the trade stays where it was.
						Light->SetCastShadows(false);
					}
				}
			}
		}
	}

	if (APostProcessVolume* Exposure = World->SpawnActor<APostProcessVolume>(
		FVector::ZeroVector, FRotator::ZeroRotator))
	{
		Exposure->SetActorLabel(TEXT("ExposureLock"));
		Exposure->bUnbound = true;
		FPostProcessSettings& Settings = Exposure->Settings;
		Settings.bOverride_AutoExposureMinBrightness = true;
		Settings.bOverride_AutoExposureMaxBrightness = true;
		Settings.AutoExposureMinBrightness = 0.03f;
		Settings.AutoExposureMaxBrightness = 8.0f;
		Settings.bOverride_AutoExposureBias = true;
		Settings.AutoExposureBias = 1.0f;

		// Warm the grade on top of the warm lamps. WhiteTemp says what the
		// camera should treat as white, so setting it above the 6500K default
		// tells it the illuminant is bluer than it is and it compensates by
		// pushing the image warm. Doing it here as well as at the lamps means
		// the daylight coming through the roof gets carried along too, instead
		// of leaving cold patches under every skylight.
		Settings.bOverride_WhiteTemp = true;
		Settings.WhiteTemp = 6800.0f;

		// A little bloom off the lamps and the polished panels. Enough to feel
		// like a lit room, not enough to wash out the stack lights.
		Settings.bOverride_BloomIntensity = true;
		Settings.BloomIntensity = 0.75f;
	}

	// The camera the flythrough drives. Placed here rather than spawned by the
	// sequence so the sequence can simply possess it: a possessable binding to
	// an actor that is in the level is stable, whereas a spawnable has to be
	// authored through the binding system and would be one more thing to get
	// wrong in a render that takes minutes to find out.
	if (ACineCameraActor* Camera = World->SpawnActor<ACineCameraActor>(
		FVector(-700.0, -1440.0, 285.0), FRotator(-8.0, 48.0, 0.0)))
	{
		Camera->SetActorLabel(TEXT("RenderCam"));
		if (UCineCameraComponent* Lens = Camera->GetCineCameraComponent())
		{
			// Wide enough to hold three lines in frame from the aisle without
			// the barrel-distorted look a very wide lens gives a long hall.
			Lens->SetFieldOfView(75.0f);
			Lens->Filmback.SensorWidth = 36.0f;
			Lens->Filmback.SensorHeight = 20.25f;   // 16:9
			// Deep focus. A shallow depth of field would throw away the far
			// lines, which are the point of the shot.
			Lens->FocusSettings.FocusMethod = ECameraFocusMethod::Disable;
		}
	}

	// Inside the hall, in a corner, looking back down the lines. Derived from
	// the building rather than written down: the coordinates carried over from
	// level3, which had no walls, and put the starting view outside this one on
	// both axes -- the level opened looking at the outside of a shed.
	{
		FVector Where(-700.0, -1400.0, 200.0);
		FRotator Facing(-6.0, 55.0, 0.0);

		if (HallBounds.IsValid)
		{
			// A couple of metres in off the corner so the near wall is behind
			// the camera rather than through it.
			constexpr double Inset = 200.0;
			Where = FVector(
				HallBounds.Min.X + Inset,
				HallBounds.Min.Y + Inset,
				HallBounds.Min.Z + 200.0);

			// Aim at the middle of the floor, which is where the lines are.
			const FVector Centre(
				HallBounds.GetCenter().X, HallBounds.GetCenter().Y, Where.Z);
			Facing = (Centre - Where).Rotation();
			Facing.Pitch = -6.0;
		}

		if (APlayerStart* Start = World->SpawnActor<APlayerStart>(Where, Facing))
		{
			Start->SetActorLabel(TEXT("ViewingPosition"));
			UE_LOG(LogFactorySim, Display,
				TEXT("  viewing position at (%.1f, %.1f) m, facing %.0f deg"),
				Where.X / FactoryGrid::MetresToCm, Where.Y / FactoryGrid::MetresToCm,
				Facing.Yaw);
		}
	}

	if (!UEditorLoadingAndSavingUtils::SaveMap(World, LevelPath))
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not save %s"), *LevelPath);
		return 1;
	}

	UE_LOG(LogFactorySim, Display,
		TEXT("Built %s: %d line(s), %d device(s), %d operator(s), %d hall part(s), "
			 "%d control cabinet(s) of %d part(s), %d slot(s) in a machine finish, "
			 "%d scene capture(s) throttled, %d light(s) unshadowed"),
		*LevelPath, Lines, TotalPlaced, TotalOperators, HallParts,
		TotalCabinets, TotalCabinetParts, TotalSlotsFinished,
		TotalCapturesTamed, TotalLightsUnshadowed);
	return 0;
}
