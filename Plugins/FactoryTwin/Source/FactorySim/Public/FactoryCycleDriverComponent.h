#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "FactorySimTypes.h"

#include "FactoryCycleDriverComponent.generated.h"

class UFactoryMachineComponent;

/**
 * Runs a machine through cycles on its own.
 *
 * A FactoryMachineComponent deliberately never self-starts: on the SMT line the
 * Blueprint animation is the clock and calls StartCycle and CompleteCycle at the
 * right moments. That is correct for a station whose motion is authored, but it
 * means a newly placed machine registers and then sits Idle forever.
 *
 * This drives the same two calls from a timer instead, so any archetype can be
 * dropped into a level and produce a plausible stream without Blueprint work.
 * Use it for new stations, demo levels and soak runs; remove it once real
 * animation drives the station.
 */
UCLASS(ClassGroup = (FactoryTwin), meta = (BlueprintSpawnableComponent),
	HideCategories = (Variable, Tags, ComponentReplication, Activation, Cooking, AssetUserData))
class FACTORYSIM_API UFactoryCycleDriverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFactoryCycleDriverComponent();

	/** Begin cycling as soon as the line comes online. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Factory Twin|Cycle Driver")
	bool bAutoStart = true;

	/**
	 * How long a cycle runs, sampled per cycle.
	 *
	 * Leave at zero to take the range from the machine's own cycle-time metric,
	 * so a station's dwell matches the duration it reports rather than being
	 * configured twice.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Factory Twin|Cycle Driver",
		meta = (ClampMin = "0.0", Units = "s"))
	float CycleSecondsMin = 0.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Factory Twin|Cycle Driver",
		meta = (ClampMin = "0.0", Units = "s"))
	float CycleSecondsMax = 0.0f;

	/** Gap between finishing one board and starting the next. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Factory Twin|Cycle Driver",
		meta = (ClampMin = "0.0", Units = "s"))
	float IdleSecondsMin = 1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Factory Twin|Cycle Driver",
		meta = (ClampMin = "0.0", Units = "s"))
	float IdleSecondsMax = 3.0f;

	UFUNCTION(BlueprintCallable, Category = "Factory Twin|Cycle Driver")
	void StartDriving();

	UFUNCTION(BlueprintCallable, Category = "Factory Twin|Cycle Driver")
	void StopDriving();

	UFUNCTION(BlueprintPure, Category = "Factory Twin|Cycle Driver")
	bool IsDriving() const { return bDriving; }

	/** Cycles completed since driving began. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin|Cycle Driver")
	int32 GetCyclesCompleted() const { return CyclesCompleted; }

	//~ UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

private:
	UFUNCTION()
	void HandleLineOnline(bool bOnline);

	void BeginCycle();
	void EndCycle();

	/** Resolves the cycle duration, falling back to the machine's own metric. */
	float PickCycleSeconds() const;
	float PickIdleSeconds() const;

	UPROPERTY(Transient)
	TObjectPtr<UFactoryMachineComponent> Machine;

	FTimerHandle CycleTimer;
	bool bDriving = false;
	bool bBoundToLine = false;
	int32 CyclesCompleted = 0;
};
