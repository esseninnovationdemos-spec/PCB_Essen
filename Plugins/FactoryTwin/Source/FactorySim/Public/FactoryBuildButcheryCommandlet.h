#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "FactoryBuildButcheryCommandlet.generated.h"

/**
 * Builds the butchery plant level from Tools/butchery/plant_layout.json.
 *
 *   UnrealEditor-Cmd.exe AutoMotion_PCB.uproject -run=FactoryBuildButchery
 *       [-Level=/Game/level5]
 *
 * The layout is not restated here. Chambers, the lines inside them, the rail
 * route and the transfers all come from the JSON that also draws the top-view
 * map, so the plan and the level cannot disagree. Asset sizes come from the
 * manifest the Blender build writes, which is how stations are spaced without
 * anyone maintaining a second table of dimensions.
 *
 * Idempotent: it builds a fresh map every time and overwrites. Editing the
 * level by hand and re-running loses the edits, which is deliberate -- the
 * layout file is the source, not the level.
 */
UCLASS()
class FACTORYSIM_API UFactoryBuildButcheryCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UFactoryBuildButcheryCommandlet();

	virtual int32 Main(const FString& Params) override;
};
