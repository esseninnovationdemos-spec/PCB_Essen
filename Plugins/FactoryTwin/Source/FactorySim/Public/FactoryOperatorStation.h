#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "FactoryOperatorStation.generated.h"

class UAnimSequence;
class UFactoryMachineComponent;
class USkeletalMesh;
class USkeletalMeshComponent;

/**
 * An operator standing at a station, working when the station works.
 *
 * Manual stations were the one part of the line with nobody at them: a bench
 * that reports a human doing a job, with no human. This puts one there and ties
 * the animation to the same machine state everything else reads, so the floor
 * and the wire agree -- an idle bench has an idle operator, and a blocked line
 * has people standing still.
 *
 * Deliberately not a canned loop playing regardless of the data. That is the
 * same reason the robot arm is driven by joint values rather than an animation
 * asset: an operator working through a stoppage would be a lie told in 3D.
 *
 * KNOWN DEFECT: the state machine is correct and verified -- the log shows it
 * selecting the resting animation at rest and Human_Work_Sped_Anim1 the moment
 * the bench starts -- but the pose does not evaluate and the operator renders
 * in its bind pose. PlayAnimation is reached with a valid mesh and a valid
 * sequence (whose thumbnail shows the working pose), mesh and animation share
 * Human_Lvl2_Skeleton, single-node mode is set and InitAnim is forced. Driving
 * these sequences from a hand-rolled component is still missing something.
 *
 * The project's own Human_BP already plays them, so the likely fix is to drive
 * that Blueprint rather than a component built here.
 */
UCLASS()
class FACTORYSIM_API AFactoryOperatorStation : public AActor
{
	GENERATED_BODY()

public:
	AFactoryOperatorStation();

	/** Sparkplug device id of the station this operator is working at. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Operator")
	FString ServedDeviceId;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Operator")
	TObjectPtr<USkeletalMesh> OperatorMesh;

	/** Played while the station is running. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Operator")
	TObjectPtr<UAnimSequence> WorkingAnimation;

	/** Played the rest of the time: idle, blocked, faulted. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Operator")
	TObjectPtr<UAnimSequence> RestingAnimation;

	/** How often the station's state is checked, in seconds. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Operator",
		meta = (ClampMin = "0.05", Units = "s"))
	float PollSeconds = 0.25f;

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

private:
	/** Switches the played animation when the station's state changes. */
	void ApplyState();

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> Operator;

	UPROPERTY(Transient)
	TObjectPtr<UFactoryMachineComponent> Machine;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Playing;

	float PollCountdown = 0.0f;
};
