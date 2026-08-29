#include "FactoryImportButcheryCommandlet.h"

#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Factories/FbxAnimSequenceImportData.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "Factories/FbxStaticMeshImportData.h"
#include "FactorySimTypes.h"
#include "IAssetTools.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace ButcheryImport
{
	const FString MeshFolder = TEXT("/Game/Butchery/Meshes");
	const FString SourceSubDir = TEXT("Tools/butchery/fbx");
	const FString ManifestPath = TEXT("Tools/butchery/asset_manifest.json");

	/**
	 * Import settings for one asset.
	 *
	 * Shared where they can be: both paths want the mesh combined into one
	 * object and the materials that came with it, and neither wants Unreal
	 * inventing a normal it can read off the file.
	 */
	UFbxImportUI* MakeOptions(const bool bSkeletal)
	{
		UFbxImportUI* Options = NewObject<UFbxImportUI>();
		Options->bImportMaterials = true;
		Options->bImportTextures = false;
		Options->bImportAsSkeletal = bSkeletal;
		Options->bAutomatedImportShouldDetectType = false;

		if (bSkeletal)
		{
			Options->MeshTypeToImport = FBXIT_SkeletalMesh;
			Options->bImportMesh = true;
			Options->bImportAnimations = true;
			Options->bCreatePhysicsAsset = true;

			// Bones are rigid-skinned one part each, so nothing benefits from
			// smoothing across them.
			Options->SkeletalMeshImportData->bImportMeshesInBoneHierarchy = true;
			Options->SkeletalMeshImportData->bImportMorphTargets = false;
			Options->SkeletalMeshImportData->NormalImportMethod = FBXNIM_ImportNormals;
			Options->SkeletalMeshImportData->bConvertScene = true;
			Options->SkeletalMeshImportData->bForceFrontXAxis = false;

			// The exporter baked every frame, so resampling would only lose
			// precision; the range comes from the file rather than being guessed.
			Options->AnimSequenceImportData->bImportBoneTracks = true;
			Options->AnimSequenceImportData->AnimationLength = FBXALIT_ExportedTime;
			Options->AnimSequenceImportData->bUseDefaultSampleRate = false;
			Options->AnimSequenceImportData->bRemoveRedundantKeys = false;
		}
		else
		{
			Options->MeshTypeToImport = FBXIT_StaticMesh;
			Options->bImportMesh = true;
			Options->bImportAnimations = false;

			Options->StaticMeshImportData->bCombineMeshes = true;
			// Lightmap UVs on a second channel: channel 0 is the smart project
			// from Blender, which overlaps deliberately and would bake badly.
			Options->StaticMeshImportData->bGenerateLightmapUVs = true;
			Options->StaticMeshImportData->bAutoGenerateCollision = true;
			Options->StaticMeshImportData->NormalImportMethod = FBXNIM_ImportNormals;
			Options->StaticMeshImportData->bConvertScene = true;
			Options->StaticMeshImportData->bForceFrontXAxis = false;
		}
		return Options;
	}

	struct FImported
	{
		int32 StaticMeshes = 0;
		int32 SkeletalMeshes = 0;
		int32 Animations = 0;
		int32 Skipped = 0;
		int32 Failed = 0;
	};

	void ImportOne(const FString& SourceFile, const FString& AssetName,
		const bool bSkeletal, const bool bForce, FImported& Tally)
	{
		const FString FullName = MeshFolder / AssetName;
		if (!bForce && FPackageName::DoesPackageExist(FullName))
		{
			++Tally.Skipped;
			return;
		}

		if (!FPaths::FileExists(SourceFile))
		{
			UE_LOG(LogFactorySim, Error, TEXT("  missing source: %s"), *SourceFile);
			++Tally.Failed;
			return;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = SourceFile;
		Task->DestinationPath = MeshFolder;
		Task->DestinationName = AssetName;
		Task->bAutomated = true;
		Task->bSave = true;
		Task->bReplaceExisting = true;
		Task->Options = MakeOptions(bSkeletal);

		TArray<UAssetImportTask*> Tasks = { Task };
		FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);

		bool bGotMesh = false;
		for (UObject* Object : Task->GetObjects())
		{
			if (Cast<UStaticMesh>(Object) != nullptr)
			{
				++Tally.StaticMeshes;
				bGotMesh = true;
			}
			else if (Cast<USkeletalMesh>(Object) != nullptr)
			{
				++Tally.SkeletalMeshes;
				bGotMesh = true;
			}
			else if (Cast<UAnimSequence>(Object) != nullptr)
			{
				++Tally.Animations;
			}
		}

		if (!bGotMesh)
		{
			UE_LOG(LogFactorySim, Warning, TEXT("  %s produced no mesh"), *AssetName);
			++Tally.Failed;
		}
	}
}

using namespace ButcheryImport;

UFactoryImportButcheryCommandlet::UFactoryImportButcheryCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UFactoryImportButcheryCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);
	const bool bForce = Switches.Contains(TEXT("Force"));

	const FString ManifestFile = FPaths::ProjectDir() / ManifestPath;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *ManifestFile))
	{
		UE_LOG(LogFactorySim, Error,
			TEXT("Could not read %s. Run the Blender build first."), *ManifestFile);
		return 1;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogFactorySim, Error, TEXT("Manifest is not valid JSON: %s"), *ManifestFile);
		return 1;
	}

	const TArray<TSharedPtr<FJsonValue>>* Assets = nullptr;
	if (!Root->TryGetArrayField(TEXT("assets"), Assets))
	{
		UE_LOG(LogFactorySim, Error, TEXT("Manifest has no 'assets' array"));
		return 1;
	}

	const FString SourceDir = FPaths::ProjectDir() / SourceSubDir;
	UE_LOG(LogFactorySim, Display,
		TEXT("Importing %d butchery asset(s) from %s"), Assets->Num(), *SourceDir);

	FImported Tally;
	for (const TSharedPtr<FJsonValue>& Value : *Assets)
	{
		const TSharedPtr<FJsonObject> Entry = Value->AsObject();
		if (!Entry.IsValid())
		{
			continue;
		}

		const FString File = Entry->GetStringField(TEXT("file"));
		const FString AssetName = FPaths::GetBaseFilename(File);
		const bool bSkeletal = Entry->GetBoolField(TEXT("skeletal"));

		ImportOne(SourceDir / File, AssetName, bSkeletal, bForce, Tally);
	}

	UE_LOG(LogFactorySim, Display,
		TEXT("Butchery import: %d static, %d skeletal, %d animation(s), "
			 "%d already present, %d failed"),
		Tally.StaticMeshes, Tally.SkeletalMeshes, Tally.Animations,
		Tally.Skipped, Tally.Failed);

	return Tally.Failed == 0 ? 0 : 1;
}
