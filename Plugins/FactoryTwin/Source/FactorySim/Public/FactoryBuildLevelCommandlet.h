#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "FactoryBuildLevelCommandlet.generated.h"

/**
 * Builds a playable level from machine instance assets.
 *
 *   UnrealEditor-Cmd.exe AutoMotion_PCB.uproject -run=FactoryBuildLevel
 *       [-Level=/Game/level3] [-Force]
 *
 * Each instance already carries its 2D floor pose, so the level is generated
 * from the same data that describes the line rather than from a second, hand-
 * placed copy of that layout. This is the first working piece of the
 * MCP-driven modelling goal: describe a line as data, get a level.
 */
UCLASS()
class FACTORYSIM_API UFactoryBuildLevelCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UFactoryBuildLevelCommandlet();

	virtual int32 Main(const FString& Params) override;
};
