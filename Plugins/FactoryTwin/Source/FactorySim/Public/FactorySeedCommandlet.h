#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "FactorySeedCommandlet.generated.h"

/**
 * Seeds the archetype library and machine instances from the existing line
 * configuration.
 *
 * Run once to port Content/Python/factory_config.toml into data assets:
 *
 *   UnrealEditor-Cmd.exe AutoMotion_PCB.uproject -run=FactorySeed
 *
 * Values are transcribed rather than parsed from the TOML, so the assets stay
 * readable and reviewable and there is no runtime TOML dependency. The alias
 * numbers in particular are reproduced exactly: the downstream ClickHouse bridge
 * maps on them, so 1-71 must not shift.
 *
 * Pass -Force to overwrite assets that already exist.
 */
UCLASS()
class FACTORYSIM_API UFactorySeedCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UFactorySeedCommandlet();

	virtual int32 Main(const FString& Params) override;
};
