#pragma once

#include "Commandlets/Commandlet.h"
#include "CoreMinimal.h"

#include "FactorySeedAssemblyCommandlet.generated.h"

/**
 * Seeds the Final Assembly half of the archetype library.
 *
 *   UnrealEditor-Cmd.exe AutoMotion_PCB.uproject -run=FactorySeedAssembly [-Force]
 *
 * Final Assembly is a genuinely different process from SMT -- press-fit
 * insertion, electrical test, firmware programming, functional test, packaging,
 * robot handling -- so it exercises the archetype split on machines that share
 * almost nothing with the surface-mount line.
 *
 * Aliases start at 100 to stay clear of the 1-87 block the SMT line occupies.
 * A cleaner end-state would give each line its own Sparkplug edge node, at which
 * point alias numbering restarts naturally per node; StartLineWithConfig already
 * accepts a full config, so that is a configuration change rather than a code
 * one.
 */
UCLASS()
class FACTORYSIM_API UFactorySeedAssemblyCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UFactorySeedAssemblyCommandlet();

	virtual int32 Main(const FString& Params) override;
};
