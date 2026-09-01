#include "FactoryImportRobotCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "Engine/StaticMesh.h"
#include "FactorySimTypes.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace RobotImport
{
	const FString MeshFolder = TEXT("/Game/FactoryTwin/Robots/KUKA_KR10");

	/**
	 * The link meshes to import, in chain order.
	 *
	 * Only the names are needed: each file carries its own geometry, and the
	 * joint centres and axes that assemble them are recovered separately from
	 * the URDF <visual> origins.
	 */
	const TCHAR* LinkMeshes[] = {
		TEXT("base_link"),
		TEXT("link_1"), TEXT("link_2"), TEXT("link_3"),
		TEXT("link_4"), TEXT("link_5"), TEXT("link_6"),
	};


	UStaticMesh* ImportGlb(const FString& SourceFile, const FString& AssetName, const bool bForce)
	{
		const FString PackagePath = MeshFolder;
		const FString FullName = PackagePath / AssetName;

		if (!bForce && FPackageName::DoesPackageExist(FullName))
		{
			UE_LOG(LogFactorySim, Display, TEXT("  %s already imported"), *AssetName);
			return LoadObject<UStaticMesh>(nullptr, *(FullName + TEXT(".") + AssetName));
		}

		if (!FPaths::FileExists(SourceFile))
		{
			UE_LOG(LogFactorySim, Error, TEXT("Source mesh missing: %s"), *SourceFile);
			return nullptr;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = SourceFile;
		Task->DestinationPath = PackagePath;
		Task->DestinationName = AssetName;
		Task->bAutomated = true;   // never prompt; this runs headless
		Task->bSave = true;
		Task->bReplaceExisting = true;

		TArray<UAssetImportTask*> Tasks = { Task };
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);

		// glTF import can yield several objects (mesh, materials, textures); take
		// the static mesh and ignore the rest.
		for (UObject* Object : Task->GetObjects())
		{
			if (UStaticMesh* Mesh = Cast<UStaticMesh>(Object))
			{
				UE_LOG(LogFactorySim, Display, TEXT("  imported %s"), *AssetName);
				return Mesh;
			}
		}

		UE_LOG(LogFactorySim, Warning, TEXT("  import produced no static mesh for %s"), *AssetName);
		return nullptr;
	}
}

using namespace RobotImport;

UFactoryImportRobotCommandlet::UFactoryImportRobotCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UFactoryImportRobotCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);
	const bool bForce = Switches.Contains(TEXT("Force"));

	const FString SourceDir =
		FPaths::ProjectDir() / TEXT("Tools/robot_import/kuka_kr10");

	UE_LOG(LogFactorySim, Display, TEXT("Importing KUKA KR10 meshes from %s"), *SourceDir);

	TArray<FString> Names;
	for (const TCHAR* Mesh : LinkMeshes)
	{
		Names.Add(Mesh);
	}

	int32 Imported = 0;
	for (const FString& Name : Names)
	{
		const FString Source = SourceDir / (Name + TEXT(".glb"));
		if (ImportGlb(Source, FString::Printf(TEXT("SM_KR10_%s"), *Name), bForce) != nullptr)
		{
			++Imported;
		}
	}

	UE_LOG(LogFactorySim, Display,
		TEXT("Imported %d/%d KR10 link mesh(es)."), Imported, Names.Num());

	// The kinematic chain itself is not derived here. These meshes export
	// already assembled in a shared frame, so the rig is driven by joint centres
	// and axes recovered from the URDF <visual> origins -- see the KUKA table in
	// FactoryBuildLevelCommandlet. Deriving a chain from the joint origins alone
	// would place the links as if the CAD pose were the URDF zero pose, and it
	// is not.

	return Imported == Names.Num() ? 0 : 1;
}
