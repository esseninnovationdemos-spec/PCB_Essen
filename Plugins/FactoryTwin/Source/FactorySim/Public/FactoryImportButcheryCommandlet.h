#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "FactoryImportButcheryCommandlet.generated.h"

/**
 * Imports the butchery asset set built in Blender.
 *
 *   UnrealEditor-Cmd.exe AutoMotion_PCB.uproject -run=FactoryImportButchery [-Force]
 *
 * Reads Tools/butchery/asset_manifest.json rather than scanning the folder,
 * because the one thing that cannot be inferred from an FBX filename is whether
 * it holds a skeleton. The manifest is written by the Blender build from the
 * scene it exported, so it always agrees with the files beside it.
 *
 * Static and skeletal need genuinely different import settings, and getting
 * them the wrong way round fails quietly: a rigged FBX imported as a static
 * mesh arrives frozen in its rest pose with the animation silently discarded,
 * which looks like a modelling problem rather than an import one.
 */
UCLASS()
class FACTORYSIM_API UFactoryImportButcheryCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UFactoryImportButcheryCommandlet();

	virtual int32 Main(const FString& Params) override;
};
