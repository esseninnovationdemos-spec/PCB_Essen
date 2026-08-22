#include "FactoryBuildLevelCommandlet.h"

#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Components/SkyLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "FactoryCycleDriverComponent.h"
#include "FactoryMachineComponent.h"
#include "FactoryLayoutGrid.h"
#include "FactoryMachineInstance.h"
#include "Animation/AnimSequence.h"
#include "FactoryConveyor.h"
#include "FactoryOperatorStation.h"
#include "FactoryProductionLine.h"
#include "FactoryShapeMaterials.h"
#include "FactoryRobotArm.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "FactorySimTypes.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace LevelBuild
{
	/** Instance asset -> the Blueprint that visually represents it. */
	struct FStationSpec
	{
		const TCHAR* InstanceAsset;
		const TCHAR* BlueprintAsset;
	};

	const FStationSpec AssemblyLine[] = {
		{ TEXT("/Game/FactoryTwin/Instances/I_HousingAssembly.I_HousingAssembly"),
		  TEXT("/Game/FinalAssembly-Workcenter/HousingAssembly/HousingAssembly_BP.HousingAssembly_BP_C") },
		{ TEXT("/Game/FactoryTwin/Instances/I_PinInsertion.I_PinInsertion"),
		  TEXT("/Game/FinalAssembly-Workcenter/SinglePinInsertion/SinglePinInsertion_BP.SinglePinInsertion_BP_C") },
		{ TEXT("/Game/FactoryTwin/Instances/I_AssemblyRobot.I_AssemblyRobot"),
		  TEXT("/Game/UR5_DT/UR5/UR5_BP.UR5_BP_C") },
		{ TEXT("/Game/FactoryTwin/Instances/I_InCircuitTest.I_InCircuitTest"),
		  TEXT("/Game/FinalAssembly-Workcenter/ICT(ElectricalTest)/ICT_BP.ICT_BP_C") },
		{ TEXT("/Game/FactoryTwin/Instances/I_FlashProgramming.I_FlashProgramming"),
		  TEXT("/Game/FinalAssembly-Workcenter/FlashProgramming/Flash_Programming_BP.Flash_Programming_BP_C") },
		{ TEXT("/Game/FactoryTwin/Instances/I_EndOfLineTest.I_EndOfLineTest"),
		  TEXT("/Game/FinalAssembly-Workcenter/EndOfLine/EndOfLine_Inspection_BP.EndOfLine_Inspection_BP_C") },
		// Packaging_BP has no machine body either -- only the carton meshes its
		// graph spawns. The magazine unit that finished goods go into measures
		// 11 by 7 metres, which is a staging area rather than a station, so the
		// loader stands in here as well: a loader at the head of the line and an
		// unloader at the end is how the line is actually built.
		{ TEXT("/Game/FactoryTwin/Instances/I_Packaging.I_Packaging"),
		  TEXT("/Game/SMT-Workcenter/Loader/Loader_BP.Loader_BP_C") },
		// Stations the project ships that the line was not using.
		// ReceiveSemi_BP is a waypoint with no geometry; the SMT loader is the
		// same job -- material arriving at the head of a line -- and has a body.
		{ TEXT("/Game/FactoryTwin/Instances/I_ReceiveSemi.I_ReceiveSemi"),
		  TEXT("/Game/SMT-Workcenter/Loader/Loader_BP.Loader_BP_C") },
		{ TEXT("/Game/FactoryTwin/Instances/I_PinInspection.I_PinInspection"),
		  TEXT("/Game/FinalAssembly-Workcenter/PinVerification/PinInsertionCheck_BP.PinInsertionCheck_BP_C") },
		{ TEXT("/Game/FactoryTwin/Instances/I_PinCheck.I_PinCheck"),
		  TEXT("/Game/FinalAssembly-Workcenter/PinCheck(AfterAssembly)/PinCheckAfterAssembly_BP.PinCheckAfterAssembly_BP_C") },
		{ TEXT("/Game/FactoryTwin/Instances/I_SemiStack.I_SemiStack"),
		  TEXT("/Game/FinalAssembly-Workcenter/CreatedSemi/StackSemiFinished_BP.StackSemiFinished_BP_C") },
	};

	/**
	 * The belt: where a unit stops, and which machine works on it there.
	 *
	 * Every stop sits on the belt centreline. The two robots are placed off to
	 * the side and serve the stop beside them rather than having the unit detour
	 * to reach them, which is both how the cell is really laid out and what
	 * keeps the belt a straight line.
	 *
	 * Positions are 2.5 m apart, an exact multiple of the half-metre grid.
	 */
	struct FLineStopSpec
	{
		const TCHAR* DeviceId;
		EFactoryProductStage StageOnComplete;
	};

	/**
	 * Process order. Where each stop sits comes from its instance's layout
	 * position, not from here -- the packing is computed when the instances are
	 * seeded, and a second copy of the numbers would only drift from it.
	 *
	 * SEMI_STACK is not a stop: it is an 11 by 7 metre staging area standing
	 * beside the tail of the line, not something a unit passes through.
	 */
	const FLineStopSpec LineStops[] = {
		{ TEXT("RECEIVE_SEMI"),       EFactoryProductStage::Empty },
		{ TEXT("HOUSING_ASSEMBLY"),   EFactoryProductStage::HousingFitted },
		{ TEXT("PIN_INSERTION"),      EFactoryProductStage::PinsInserted },
		// Verification fits no new parts; it decides whether what is already
		// there passes.
		{ TEXT("PIN_INSPECTION"),     EFactoryProductStage::PinsInserted },
		{ TEXT("ASSEMBLY_ROBOT"),     EFactoryProductStage::BoardFitted },
		{ TEXT("ICT"),                EFactoryProductStage::Tested },
		{ TEXT("FLASH_PROGRAMMING"),  EFactoryProductStage::Programmed },
		{ TEXT("KUKA_HANDLER"),       EFactoryProductStage::LidFitted },
		{ TEXT("PIN_CHECK"),          EFactoryProductStage::LidFitted },
		{ TEXT("EOL_TEST"),           EFactoryProductStage::FunctionTested },
		{ TEXT("PACKAGING"),          EFactoryProductStage::Packed },
	};

	/**
	 * Instances that are stops on the line but are not placed from a Blueprint.
	 *
	 * The KUKA is assembled in C++ from imported link meshes, so it has no entry
	 * in the station list above and would otherwise be invisible to a lookup
	 * that only walked that list -- which silently dropped its stop, and with it
	 * the step that fits the lid.
	 */
	const TCHAR* NonBlueprintInstances[] = {
		TEXT("/Game/FactoryTwin/Instances/I_KukaHandler.I_KukaHandler"),
	};

	/** Instance asset holding a device's layout, by device id. */
	const TCHAR* InstanceForDevice(const FString& DeviceId)
	{
		for (const FStationSpec& Spec : AssemblyLine)
		{
			if (UFactoryMachineInstance* Instance =
				LoadObject<UFactoryMachineInstance>(nullptr, Spec.InstanceAsset))
			{
				if (Instance->DeviceId == DeviceId)
				{
					return Spec.InstanceAsset;
				}
			}
		}
		for (const TCHAR* Asset : NonBlueprintInstances)
		{
			if (UFactoryMachineInstance* Instance =
				LoadObject<UFactoryMachineInstance>(nullptr, Asset))
			{
				if (Instance->DeviceId == DeviceId)
				{
					return Asset;
				}
			}
		}
		return nullptr;
	}

	/** Where a machine sits across the belt, so conveyor is not run through it. */
	struct FBeltBlocker
	{
		double MinX;
		double MaxX;
	};

	/** True when the production line, rather than a timer, drives this machine. */
	bool IsDrivenByLine(const FString& DeviceId)
	{
		for (const FLineStopSpec& Stop : LineStops)
		{
			if (DeviceId == Stop.DeviceId)
			{
				return true;
			}
		}
		return false;
	}

	/** Layout poses are authored in metres; Unreal works in centimetres. */
	constexpr double MetresToCm = 100.0;

	/**
	 * Collects every static mesh the importer produced for one link.
	 *
	 * The glTF importer puts each source file in its own folder and names meshes
	 * from the glTF's internal node names, which the COLLADA conversion mostly
	 * left as "Default". Some links also arrive split across several meshes. So
	 * the meshes are gathered by folder rather than looked up by a name we
	 * cannot predict.
	 */
	TArray<TObjectPtr<UStaticMesh>> LoadLinkMeshes(const FString& LinkName)
	{
		TArray<TObjectPtr<UStaticMesh>> Meshes;

		const FAssetRegistryModule& Registry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

		FARFilter Filter;
		Filter.bRecursivePaths = true;
		Filter.PackagePaths.Add(FName(*FString::Printf(
			TEXT("/Game/FactoryTwin/Robots/KUKA_KR10/%s"), *LinkName)));
		Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());

		TArray<FAssetData> Found;
		Registry.Get().GetAssets(Filter, Found);

		// Sort by name so the hierarchy is stable between rebuilds.
		Found.Sort([](const FAssetData& A, const FAssetData& B)
		{
			return A.AssetName.LexicalLess(B.AssetName);
		});

		for (const FAssetData& Asset : Found)
		{
			if (UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.GetAsset()))
			{
				Meshes.Add(Mesh);
			}
		}

		if (Meshes.Num() == 0)
		{
			UE_LOG(LogFactorySim, Warning,
				TEXT("No meshes for link '%s'; run -run=FactoryImportRobot first"), *LinkName);
		}
		return Meshes;
	}

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
			UE_LOG(LogFactorySim, Warning, TEXT("'%s' has no object property '%s'"),
				*Actor->GetName(), *PropertyName.ToString());
			return false;
		}

		Property->SetObjectPropertyValue(
			Property->ContainerPtrToValuePtr<void>(Actor), Value);
		return true;
	}
}

using namespace LevelBuild;

UFactoryBuildLevelCommandlet::UFactoryBuildLevelCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UFactoryBuildLevelCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	const FString LevelPath = ParamMap.Contains(TEXT("Level"))
		? ParamMap[TEXT("Level")]
		: TEXT("/Game/level3");

	UE_LOG(LogFactorySim, Display, TEXT("Building %s from instance assets..."), *LevelPath);

	// Must happen before anything is spawned: the generated geometry references
	// these, and a level referencing a material that does not exist yet saves
	// with the reference dropped.
	FactoryShapeMaterials::EnsureAll();

	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/ false);
	if (World == nullptr)
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not create a blank map"));
		return 1;
	}

	int32 Placed = 0;
	int32 Skipped = 0;
	TArray<FBeltBlocker> Blockers;
	double LineStartX = 0.0;
	double LineExtentX = 0.0;

	for (const FStationSpec& Spec : AssemblyLine)
	{
		UFactoryMachineInstance* Instance =
			LoadObject<UFactoryMachineInstance>(nullptr, Spec.InstanceAsset);
		if (Instance == nullptr)
		{
			UE_LOG(LogFactorySim, Warning,
				TEXT("Instance '%s' not found; run -run=FactorySeedAssembly first"),
				Spec.InstanceAsset);
			++Skipped;
			continue;
		}

		UClass* StationClass = LoadObject<UClass>(nullptr, Spec.BlueprintAsset);
		if (StationClass == nullptr)
		{
			UE_LOG(LogFactorySim, Warning,
				TEXT("Blueprint '%s' not found; skipping %s"),
				Spec.BlueprintAsset, *Instance->DeviceId);
			++Skipped;
			continue;
		}

		// Drive the pose from the instance so the level and the tag map describe
		// the same line, and snap it, so a hand-edited layout position still
		// lands where the floor grid says it should.
		FTransform Transform;
		Transform.SetLocation(FactoryGrid::MetresToWorld(
			FactoryGrid::SnapMetres(Instance->LayoutPosition)));
		Transform.SetRotation(FRotator(0.0, Instance->LayoutRotationDegrees, 0.0).Quaternion());

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.Name = MakeUniqueObjectName(World->PersistentLevel, StationClass,
			FName(*Instance->DeviceId));

		AActor* Station = World->SpawnActor<AActor>(StationClass, Transform, SpawnParams);
		if (Station == nullptr)
		{
			UE_LOG(LogFactorySim, Warning, TEXT("Could not spawn %s"), *Instance->DeviceId);
			++Skipped;
			continue;
		}

		Station->SetActorLabel(Instance->DeviceId);

		// Stations the production line serves are driven by the units arriving at
		// them, so they must not also run on a timer -- two things calling
		// StartCycle on one machine interleave into nonsense. Anything the line
		// does not reach still needs a driver to do more than sit Idle.
		if (!IsDrivenByLine(Instance->DeviceId))
		{
			UFactoryCycleDriverComponent* Driver = NewObject<UFactoryCycleDriverComponent>(
				Station, UFactoryCycleDriverComponent::StaticClass(), TEXT("CycleDriver"));
			Driver->RegisterComponent();
			Station->AddInstanceComponent(Driver);
		}

		// The UR5 drives its arm from a separate Goal actor holding the waypoint
		// path, and the two hold references to each other. Placing the robot
		// without it leaves the AnimBP reading a null Goal Ref every frame.
		if (Station->GetClass()->FindPropertyByName(TEXT("Goal Ref")) != nullptr)
		{
			if (UClass* GoalClass = LoadObject<UClass>(
				nullptr, TEXT("/Game/UR5_DT/UR5/Goal_BP.Goal_BP_C")))
			{
				FTransform GoalTransform = Transform;
				GoalTransform.SetLocation(
					Transform.GetLocation() + FVector(0.0, 0.0, 60.0));

				// Fresh parameters: SpawnParams still carries the station's
				// explicit Name, and reusing it collides and asserts.
				FActorSpawnParameters GoalSpawnParams;
				GoalSpawnParams.SpawnCollisionHandlingOverride =
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				GoalSpawnParams.Name = MakeUniqueObjectName(
					World->PersistentLevel, GoalClass,
					FName(*FString::Printf(TEXT("%s_Goal"), *Instance->DeviceId)));

				if (AActor* Goal = World->SpawnActor<AActor>(
					GoalClass, GoalTransform, GoalSpawnParams))
				{
					Goal->SetActorLabel(FString::Printf(TEXT("%s_Goal"), *Instance->DeviceId));
					SetActorObjectProperty(Station, TEXT("Goal Ref"), Goal);
					SetActorObjectProperty(Goal, TEXT("UR5e Ref"), Station);
					UE_LOG(LogFactorySim, Display,
						TEXT("  paired %s with its motion goal"), *Instance->DeviceId);
				}
			}
		}

		// Report what the station actually measures against what its instance
		// claims. The declared footprints drive both the packing here and the 2D
		// floor plan, so a claim that does not match the geometry shows up as a
		// gap on the floor and a wrong box on the plan.
		{
			const FBox Box = Station->GetComponentsBoundingBox(true);
			const FVector Size = Box.GetSize() / FactoryGrid::MetresToCm;
			UE_LOG(LogFactorySim, Display,
				TEXT("    %-18s measured %5.2f x %5.2f m   declared %5.2f x %5.2f m"),
				*Instance->DeviceId, Size.X, Size.Y,
				Instance->LayoutFootprint.X, Instance->LayoutFootprint.Y);
		}

		// Machines standing on the belt centreline interrupt it; ones set back
		// from it (the two robots) serve the line from the side and do not.
		if (FMath::Abs(Instance->LayoutPosition.Y) < 0.5)
		{
			const double HalfWidth = Instance->LayoutFootprint.X * 0.5;
			Blockers.Add({ Instance->LayoutPosition.X - HalfWidth,
			               Instance->LayoutPosition.X + HalfWidth });
		}

		++Placed;
		UE_LOG(LogFactorySim, Display, TEXT("  placed %-20s at (%.1f, %.1f) m"),
			*Instance->DeviceId, Instance->LayoutPosition.X, Instance->LayoutPosition.Y);
	}

	// Extent of the packed line. Computed here rather than where the conveyor is
	// built, because the floor is laid before that and was being sized from a
	// still-zero extent -- a 10 m slab under a 21 m line, with the rest of the
	// grid hanging over nothing.
	{
		Blockers.Sort([](const FBeltBlocker& A, const FBeltBlocker& B)
		{
			return A.MinX < B.MinX;
		});

		double MinX = 0.0;
		double MaxX = 0.0;
		for (int32 Index = 0; Index < Blockers.Num(); ++Index)
		{
			MinX = (Index == 0) ? Blockers[Index].MinX : FMath::Min(MinX, Blockers[Index].MinX);
			MaxX = FMath::Max(MaxX, Blockers[Index].MaxX);
		}
		LineStartX = MinX - 1.5;
		LineExtentX = MaxX + 1.5;
	}

	// The KUKA arm, assembled from the imported ROS-Industrial link meshes. It
	// is a C++ actor rather than a Blueprint because the chain is built from
	// URDF geometry, so there is nothing to author by hand.
	if (UFactoryMachineInstance* KukaInstance = LoadObject<UFactoryMachineInstance>(
		nullptr, TEXT("/Game/FactoryTwin/Instances/I_KukaHandler.I_KukaHandler")))
	{
		FTransform KukaTransform;
		KukaTransform.SetLocation(FVector(
			KukaInstance->LayoutPosition.X * MetresToCm,
			KukaInstance->LayoutPosition.Y * MetresToCm, 0.0));

		FActorSpawnParameters KukaParams;
		KukaParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		if (AFactoryRobotArm* Kuka = World->SpawnActor<AFactoryRobotArm>(
			AFactoryRobotArm::StaticClass(), KukaTransform, KukaParams))
		{
			Kuka->SetActorLabel(TEXT("KUKA_HANDLER"));
			Kuka->BaseMeshes = LoadLinkMeshes(TEXT("base_link"));

			// Derived from the <visual> origins in kr10_r1100_2_macro.xacro.
			// That origin is the mesh-frame-to-link-frame map, so inverting it
			// yields each joint's centre and axis directly in the frame the
			// imported meshes already live in -- which is what this rig needs,
			// and which forward kinematics would not give, because the CAD pose
			// the meshes were exported in is not the URDF zero pose.
			//
			// Cross-checked against the imported bounds: every centre lands on
			// the seam between consecutive link meshes.
			const TCHAR* LinkNames[] = {
				TEXT("link_1"), TEXT("link_2"), TEXT("link_3"),
				TEXT("link_4"), TEXT("link_5"), TEXT("link_6") };
			const FVector JointCentres[] = {
				{  0.00, 0.00, 20.80 },   // J1 base yaw, at the pedestal seam
				{  2.50, 9.07, 40.00 },   // J2 shoulder
				{  2.50, 8.65, 96.00 },   // J3 elbow
				{ 22.10, 0.00, 98.50 },   // J4 forearm roll
				{ 54.00, 5.05, 98.50 },   // J5 wrist pitch
				{ 60.15, 0.00, 98.50 } }; // J6 flange roll
			const FVector JointAxes[] = {
				{  0.0, 0.0, -1.0 }, {  0.0, 1.0, 0.0 }, {  0.0, 1.0, 0.0 },
				{ -1.0, 0.0,  0.0 }, {  0.0, 1.0, 0.0 }, { -1.0, 0.0, 0.0 } };
			const float Lower[] = { -170.0f, -190.0f, -120.0f, -185.0f, -120.0f, -350.0f };
			const float Upper[] = {  170.0f,   45.0f,  156.0f,  185.0f,  120.0f,  350.0f };

			for (int32 Index = 0; Index < 6; ++Index)
			{
				FFactoryRobotLink Link;
				Link.Meshes = LoadLinkMeshes(LinkNames[Index]);
				Link.JointCentre = JointCentres[Index];
				Link.JointAxis = JointAxes[Index];
				Link.MinAngle = Lower[Index];
				Link.MaxAngle = Upper[Index];
				Kuka->Links.Add(Link);
			}
			Kuka->RebuildHierarchy();

			UFactoryMachineComponent* KukaMachine = NewObject<UFactoryMachineComponent>(
				Kuka, UFactoryMachineComponent::StaticClass(), TEXT("FactoryMachine"));
			KukaMachine->Instance = KukaInstance;
			KukaMachine->RegisterComponent();
			Kuka->AddInstanceComponent(KukaMachine);

			// No cycle driver: the production line drives this one, same as the
			// belt stations.

			++Placed;
			int32 MeshCount = Kuka->BaseMeshes.Num();
			for (const FFactoryRobotLink& L : Kuka->Links)
			{
				MeshCount += L.Meshes.Num();
			}
			UE_LOG(LogFactorySim, Display,
				TEXT("  placed %-20s at (%.1f, %.1f) m  [KR10, 6 joints, %d meshes]"),
				TEXT("KUKA_HANDLER"),
				KukaInstance->LayoutPosition.X, KukaInstance->LayoutPosition.Y, MeshCount);
		}
	}

	// The line controller: without it nothing calls StartFactoryLine and the
	// stations would register but never publish.
	if (UClass* ManagerClass = LoadObject<UClass>(
		nullptr, TEXT("/Game/BP_MQTT_Manager.BP_MQTT_Manager_C")))
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AActor* Manager = World->SpawnActor<AActor>(
			ManagerClass, FTransform(FVector(0.0, -400.0, 0.0)), SpawnParams))
		{
			Manager->SetActorLabel(TEXT("LineController"));
		}
	}

	// Minimal lighting so the level is actually viewable.
	if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
		FVector(0.0, 0.0, 800.0), FRotator(-50.0, -35.0, 0.0)))
	{
		Sun->SetActorLabel(TEXT("Sun"));
		Sun->GetComponent()->SetIntensity(10.0f);

		// Cascaded shadows default to covering 200 m. The line and its floor fit
		// in 60, and the cascades are spread across whatever distance they are
		// given, so the default was spending most of its shadow resolution on
		// empty ground beyond the factory.
		Sun->GetComponent()->DynamicShadowDistanceMovableLight = 6000.0f;
		Sun->GetComponent()->DynamicShadowCascades = 3;
	}

	// The sky light captures the atmosphere in real time, so there has to be an
	// atmosphere for it to capture. Without one it captures blackness and
	// contributes no ambient at all, which leaves everything the sun does not
	// directly hit unlit.
	World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);

	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.0, 0.0, 500.0), FRotator::ZeroRotator))
	{
		Sky->SetActorLabel(TEXT("SkyLight"));
		Sky->GetLightComponent()->SetIntensity(1.0f);

		// Captured once rather than every frame. Real-time capture re-renders the
		// sky into a cubemap continuously, which is worth paying for when the sky
		// changes -- a day/night cycle, moving cloud. Nothing here moves, so it
		// was re-rendering an identical sky every frame for the whole session.
		Sky->GetLightComponent()->bRealTimeCapture = false;
		Sky->GetLightComponent()->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	}

	// Bound the auto exposure rather than pin it. An earlier pass let it run
	// free and a near-empty level drove it to the end of its range, blowing the
	// scene out to white; the pass after that pinned min and max together, which
	// removed the adaptation entirely and made the scene depend on the sun's
	// absolute intensity being guessed correctly -- it was not, and everything
	// came out black. A wide but bounded range self-corrects either way.
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

	// A floor, so the stations are not floating in a void.
	if (UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		if (AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(
			FVector((LineStartX + LineExtentX) * 0.5 * FactoryGrid::MetresToCm, -300.0, -5.0),
			FRotator::ZeroRotator))
		{
			Floor->SetMobility(EComponentMobility::Static);
			Floor->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
			// Plane is 100 cm; cover the packed line plus the staging area beside it.
			Floor->SetActorScale3D(FVector(
				FMath::CeilToDouble(LineExtentX - LineStartX) + 8.0, 24.0, 1.0));
			Floor->SetActorLabel(TEXT("Floor"));
		}
	}

	// Conveyor between the machines. Each station carries its own integral
	// conveyor through its own body, so running belt through them as well would
	// leave two surfaces fighting at exactly the same height. What is missing is
	// the stretches in between, which is what gets built here.
	{
		const double EntryX = LineStartX;
		const double ExitX = LineExtentX;
		// Leave a small air gap so a run does not visually jam into a machine.
		const double Clearance = 0.15;

		TArray<TPair<double, double>> Runs;
		double Cursor = EntryX;
		for (const FBeltBlocker& Blocker : Blockers)
		{
			if (Blocker.MinX - Clearance > Cursor)
			{
				Runs.Add({ Cursor, Blocker.MinX - Clearance });
			}
			Cursor = FMath::Max(Cursor, Blocker.MaxX + Clearance);
		}
		if (ExitX > Cursor)
		{
			Runs.Add({ Cursor, ExitX });
		}

		int32 RunIndex = 0;
		AFactoryConveyor* FirstRun = nullptr;
		for (const TPair<double, double>& Run : Runs)
		{
			const double Length = Run.Value - Run.Key;
			if (Length < 0.4)
			{
				// Nothing between two machines that are touching. Conveyor now
				// only leads in and runs off; the machines carry the belt
				// through themselves.
				continue;
			}

			FActorSpawnParameters ConveyorParams;
			ConveyorParams.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			if (AFactoryConveyor* Conveyor = World->SpawnActor<AFactoryConveyor>(
				AFactoryConveyor::StaticClass(),
				FTransform(FRotator::ZeroRotator,
					FactoryGrid::MetresToWorld(FVector2D(Run.Key, 0.0))),
				ConveyorParams))
			{
				Conveyor->LengthMetres = static_cast<float>(Length);
				Conveyor->RebuildConveyor();
				Conveyor->SetActorLabel(FString::Printf(TEXT("Conveyor_%d"), ++RunIndex));
				if (FirstRun == nullptr)
				{
					FirstRun = Conveyor;
				}
			}
		}

		// The transport is one device on the wire, not one per stretch of belt.
		if (FirstRun != nullptr)
		{
			if (UFactoryMachineInstance* ConveyorInstance =
				LoadObject<UFactoryMachineInstance>(
					nullptr, TEXT("/Game/FactoryTwin/Instances/I_AssemblyConveyor.I_AssemblyConveyor")))
			{
				UFactoryMachineComponent* Machine = NewObject<UFactoryMachineComponent>(
					FirstRun, UFactoryMachineComponent::StaticClass(), TEXT("FactoryMachine"));
				Machine->Instance = ConveyorInstance;
				Machine->RegisterComponent();
				FirstRun->AddInstanceComponent(Machine);
				++Placed;
			}
		}

		UE_LOG(LogFactorySim, Display,
			TEXT("  conveyor: %d run(s) between %d machine(s) on the belt"),
			RunIndex, Blockers.Num());
	}

	// Operators at the benches. Only the manual stations get one: an automated
	// cell with a person stood inside it would be telling the opposite of the
	// truth about how the step is done.
	{
		struct FOperatorSpec
		{
			const TCHAR* Device;
			bool bSeated;
		};

		const FOperatorSpec Operators[] = {
			{ TEXT("HOUSING_ASSEMBLY"), false },
			// This bench ships with a seat, so its operator uses it.
			{ TEXT("PIN_INSPECTION"), true },
		};

		UAnimSequence* Sitting = LoadObject<UAnimSequence>(
			nullptr, TEXT("/Game/Human/Human_Sitting_Anim.Human_Sitting_Anim"));

		int32 Placed_Operators = 0;
		for (const FOperatorSpec& Spec : Operators)
		{
			const TCHAR* Asset = InstanceForDevice(Spec.Device);
			UFactoryMachineInstance* Instance = (Asset != nullptr)
				? LoadObject<UFactoryMachineInstance>(nullptr, Asset) : nullptr;
			if (Instance == nullptr)
			{
				continue;
			}

			FActorSpawnParameters OperatorParams;
			OperatorParams.SpawnCollisionHandlingOverride =
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			// A metre in front of the bench, turned to face it.
			const FVector Where = FactoryGrid::MetresToWorld(
				FVector2D(Instance->LayoutPosition.X, Instance->LayoutPosition.Y - 1.0));

			if (AFactoryOperatorStation* Worker = World->SpawnActor<AFactoryOperatorStation>(
				AFactoryOperatorStation::StaticClass(),
				FTransform(FRotator(0.0, 90.0, 0.0), Where), OperatorParams))
			{
				Worker->ServedDeviceId = Spec.Device;
				if (Spec.bSeated && Sitting != nullptr)
				{
					Worker->RestingAnimation = Sitting;
				}
				Worker->SetActorLabel(FString::Printf(TEXT("Operator_%s"), Spec.Device));
				++Placed_Operators;
			}
		}

		UE_LOG(LogFactorySim, Display,
			TEXT("  operators: %d at the manual benches"), Placed_Operators);
	}

	// Somewhere to stand. Without a player start the pawn spawns at the origin,
	// which on this line is inside the first machine, looking at the underside
	// of a conveyor.
	if (APlayerStart* Start = World->SpawnActor<APlayerStart>(
		FVector(-500.0, -900.0, 250.0), FRotator(-8.0, 35.0, 0.0)))
	{
		Start->SetActorLabel(TEXT("ViewingPosition"));
	}

	// The floor-plan grid, so a placement can be read straight off the level
	// instead of by opening the instance asset and reading a decimal.
	if (AFactoryFloorGrid* Grid = World->SpawnActor<AFactoryFloorGrid>(
		FVector::ZeroVector, FRotator::ZeroRotator))
	{
		Grid->SetActorLabel(TEXT("FloorGrid"));
		// Wide enough in -Y to take in the staging area standing beside the line.
		Grid->MinMetres = FVector2D(FMath::FloorToDouble(LineStartX) - 1.0, -11.0);
		Grid->MaxMetres = FVector2D(FMath::CeilToDouble(LineExtentX) + 1.0, 5.0);
		Grid->RebuildGrid();
	}

	// The belt, and the units that travel it. This is what makes the line
	// produce something rather than just report that it is busy.
	if (AFactoryProductionLine* Line = World->SpawnActor<AFactoryProductionLine>(
		FVector::ZeroVector, FRotator::ZeroRotator))
	{
		Line->SetActorLabel(TEXT("AssemblyLine"));
		Line->TaktSeconds = 18.0f;

		double FirstX = TNumericLimits<double>::Max();
		double LastX = TNumericLimits<double>::Lowest();

		for (const FLineStopSpec& Spec : LineStops)
		{
			const TCHAR* Asset = InstanceForDevice(Spec.DeviceId);
			UFactoryMachineInstance* Instance = (Asset != nullptr)
				? LoadObject<UFactoryMachineInstance>(nullptr, Asset) : nullptr;
			if (Instance == nullptr)
			{
				UE_LOG(LogFactorySim, Warning,
					TEXT("  no instance for stop '%s'; skipping it"), Spec.DeviceId);
				continue;
			}

			FFactoryLineStop Stop;
			Stop.DeviceId = Spec.DeviceId;
			// On the belt centreline even for the machines set back from it: the
			// robots reach across to the unit, the unit does not detour to them.
			Stop.PositionMetres = FVector2D(Instance->LayoutPosition.X, 0.0);
			Stop.StageOnComplete = Spec.StageOnComplete;
			Line->Stops.Add(Stop);

			FirstX = FMath::Min(FirstX, Stop.PositionMetres.X);
			LastX = FMath::Max(LastX, Stop.PositionMetres.X);
		}

		// Enough lead-in and run-off to see a unit arrive and leave.
		Line->EntryMetres = FVector2D(FirstX - 1.5, 0.0);
		Line->ExitMetres = FVector2D(LastX + 1.5, 0.0);
		// The panel that shows and drives this line.
		if (UClass* HudClass = LoadObject<UClass>(
			nullptr, TEXT("/Game/UI/W_AssemblyPanel.W_AssemblyPanel_C")))
		{
			Line->HudWidgetClass = HudClass;
		}
		else
		{
			UE_LOG(LogFactorySim, Warning,
				TEXT("W_AssemblyPanel not found; the line will run without a panel"));
		}

		Line->RebuildBelt();

		UE_LOG(LogFactorySim, Display,
			TEXT("  assembly line: %d stop(s), %.1fs takt, %.1f m of belt"),
			Line->Stops.Num(), Line->TaktSeconds,
			Line->ExitMetres.X - Line->EntryMetres.X);
	}

	if (!UEditorLoadingAndSavingUtils::SaveMap(World, LevelPath))
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not save %s"), *LevelPath);
		return 1;
	}

	UE_LOG(LogFactorySim, Display,
		TEXT("Built %s: %d station(s) placed, %d skipped"), *LevelPath, Placed, Skipped);
	return Skipped == 0 ? 0 : 1;
}
