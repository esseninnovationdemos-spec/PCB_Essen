#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "FactorySeedPlantCommandlet.generated.h"

/**
 * Seeds a plant of several identical end-to-end lines.
 *
 * Run with -run=FactorySeedPlant [-Lines=N] [-Force].
 *
 * Each line runs a bare board from the loader through SMT and on through final
 * assembly, so a line is a whole product rather than half of one. Lines are
 * generated from a single template: they are the same equipment, and writing
 * them out one at a time would mean thirty-odd near-identical blocks drifting
 * apart the first time one of them was edited.
 */
UCLASS()
class FACTORYSIM_API UFactorySeedPlantCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UFactorySeedPlantCommandlet();

	virtual int32 Main(const FString& Params) override;
};
