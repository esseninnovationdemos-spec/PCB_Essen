#include "FactoryBuildPlantCommandlet.h"

#include "Components/DirectionalLightComponent.h"
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

	/** Benches worked by a person rather than a machine. */
	bool IsOperatorBench(const FString& DeviceSuffix)
	{
		return DeviceSuffix == TEXT("HOUSING_ASSEMBLY")
			|| DeviceSuffix == TEXT("PIN_INSPECTION");
	}

	const FString InstanceFolder = TEXT("/Game/FactoryTwin/Instances/Plant");

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
	int32 HallParts = 0;
	int32 NaniteEnabled = 0;
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

			if (AStaticMeshActor* Piece = World->SpawnActor<AStaticMeshActor>(
				FVector::ZeroVector, FRotator::ZeroRotator))
			{
				Piece->SetMobility(EComponentMobility::Static);
				Piece->GetStaticMeshComponent()->SetStaticMesh(Mesh);
				Piece->SetActorLabel(FString::Printf(TEXT("Hall_%s"), *Asset.AssetName.ToString()));
				++HallParts;
			}
		}

		if (ToSave.Num() > 0)
		{
			UEditorLoadingAndSavingUtils::SavePackages(ToSave, /*bOnlyDirty*/ false);
		}
		UE_LOG(LogFactorySim, Display,
			TEXT("  hall: %d part(s), %lld triangles, Nanite switched on for %d"),
			HallParts, TotalTriangles, NaniteEnabled);
	}

	// Lanes run along the hall's length. A line packs to roughly 27 m and the
	// hall is 17 m across, so laid the other way each one would run out through
	// a wall.
	constexpr double LaneSpacing = 5.0;
	constexpr double LineStartY = -13.0;

	int32 TotalPlaced = 0;
	int32 TotalOperators = 0;

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
					World->PersistentLevel, StationClass, FName(*Instance->DeviceId));

				// Turned a quarter so the machines face across a line that runs
				// along Y rather than the X they were authored for.
				const FTransform Where(FRotator(0.0, 90.0, 0.0),
					ToWorld(Instance->LayoutPosition));

				if (AActor* Actor = World->SpawnActor<AActor>(StationClass, Where, StationParams))
				{
					Actor->SetActorLabel(Instance->DeviceId);
					++TotalPlaced;

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
			Stop.DeviceId = Instance->DeviceId;
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
					Worker->ServedDeviceId = Instance->DeviceId;
					Worker->SetActorLabel(FString::Printf(TEXT("Operator_%s"), *Instance->DeviceId));
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
			Belt->ConveyorDeviceId = FString::Printf(TEXT("L%d_CONVEYOR"), Line);
			// Staggered so the lines do not all release on the same beat, which
			// would make three lines behave like one wide one.
			Belt->TaktSeconds = 18.0f + Line * 1.5f;

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

	// Daylight outside, and bay lighting inside because a roof keeps the sun out.
	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
		FVector(0.0, 0.0, 800.0), FRotator(-50.0, -35.0, 0.0)))
	{
		Sun->SetActorLabel(TEXT("Sun"));
		Sun->GetComponent()->SetIntensity(10.0f);
		Sun->GetComponent()->DynamicShadowDistanceMovableLight = 6000.0f;
	}
	World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.0, 0.0, 500.0), FRotator::ZeroRotator))
	{
		Sky->SetActorLabel(TEXT("SkyLight"));
		Sky->GetLightComponent()->SetIntensity(1.0f);
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
						Light->SetIntensity(28000.0f);
						Light->SetAttenuationRadius(1200.0f);
						// Fill only; the sun casts the shadows.
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
	}

	if (APlayerStart* Start = World->SpawnActor<APlayerStart>(
		FVector(-900.0, -1900.0, 250.0), FRotator(-8.0, 55.0, 0.0)))
	{
		Start->SetActorLabel(TEXT("ViewingPosition"));
	}

	if (!UEditorLoadingAndSavingUtils::SaveMap(World, LevelPath))
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not save %s"), *LevelPath);
		return 1;
	}

	UE_LOG(LogFactorySim, Display,
		TEXT("Built %s: %d line(s), %d device(s), %d operator(s), %d hall part(s)"),
		*LevelPath, Lines, TotalPlaced, TotalOperators, HallParts);
	return 0;
}
