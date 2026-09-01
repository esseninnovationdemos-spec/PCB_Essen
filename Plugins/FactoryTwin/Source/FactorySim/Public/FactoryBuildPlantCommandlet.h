#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "FactoryBuildPlantCommandlet.generated.h"

/**
 * Builds a plant level from the instances the plant seed produced.
 *
 * Run with -run=FactoryBuildPlant [-Lines=N] [-Level=/Game/level4].
 *
 * Lines run along the hall's length rather than its width: a line packs to
 * roughly 27 m and the hall is 17 m across, so laid the other way each line
 * would run out through a wall.
 */
UCLASS()
class FACTORYSIM_API UFactoryBuildPlantCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UFactoryBuildPlantCommandlet();

	virtual int32 Main(const FString& Params) override;
};
