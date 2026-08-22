#include "FactoryBuildLevelCommandlet.h"

#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "FactoryCycleDriverComponent.h"
#include "FactoryMachineComponent.h"
#include "FactoryMachineInstance.h"
#include "FactorySimTypes.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
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
		{ TEXT("/Game/FactoryTwin/Instances/I_Packaging.I_Packaging"),
		  TEXT("/Game/FinalAssembly-Workcenter/Packaging/Packaging_BP.Packaging_BP_C") },
	};

	/** Layout poses are authored in metres; Unreal works in centimetres. */
	constexpr double MetresToCm = 100.0;

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

	UWorld* World = UEditorLoadingAndSavingUtils::NewBlankMap(/*bSaveExistingMap*/ false);
	if (World == nullptr)
	{
		UE_LOG(LogFactorySim, Error, TEXT("Could not create a blank map"));
		return 1;
	}

	int32 Placed = 0;
	int32 Skipped = 0;

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
		// the same line.
		FTransform Transform;
		Transform.SetLocation(FVector(
			Instance->LayoutPosition.X * MetresToCm,
			Instance->LayoutPosition.Y * MetresToCm,
			0.0));
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

		// These stations have no authored animation driving their cycle, so give
		// each one a driver. Remove it per-station once real motion exists.
		UFactoryCycleDriverComponent* Driver = NewObject<UFactoryCycleDriverComponent>(
			Station, UFactoryCycleDriverComponent::StaticClass(), TEXT("CycleDriver"));
		Driver->RegisterComponent();
		Station->AddInstanceComponent(Driver);

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

		++Placed;
		UE_LOG(LogFactorySim, Display, TEXT("  placed %-20s at (%.1f, %.1f) m"),
			*Instance->DeviceId, Instance->LayoutPosition.X, Instance->LayoutPosition.Y);
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
		Sun->GetComponent()->SetIntensity(4.0f);
	}
	if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(0.0, 0.0, 500.0), FRotator::ZeroRotator))
	{
		Sky->SetActorLabel(TEXT("SkyLight"));
	}

	// A floor, so the stations are not floating in a void.
	if (UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		if (AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(
			FVector(900.0, 0.0, -5.0), FRotator::ZeroRotator))
		{
			Floor->SetMobility(EComponentMobility::Static);
			Floor->GetStaticMeshComponent()->SetStaticMesh(PlaneMesh);
			// Plane is 100cm; scale to roughly cover the 20m line.
			Floor->SetActorScale3D(FVector(40.0, 20.0, 1.0));
			Floor->SetActorLabel(TEXT("Floor"));
		}
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
