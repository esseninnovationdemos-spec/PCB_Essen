#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "FactoryRobotArm.generated.h"

class UFactoryMachineComponent;
class UStaticMesh;
class UStaticMeshComponent;

/** One revolute joint and the link mesh it carries. */
USTRUCT(BlueprintType)
struct FACTORYSIM_API FFactoryRobotLink
{
	GENERATED_BODY()

	/**
	 * Meshes making up this link.
	 *
	 * Usually one, but the source COLLADA splits some links into separate
	 * geometry nodes and the glTF conversion preserves that, so a link may
	 * arrive as several meshes that share a frame.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Link")
	TArray<TObjectPtr<UStaticMesh>> Meshes;

	/**
	 * Joint centre in the frame the link meshes are authored in, centimetres.
	 *
	 * Absolute within that frame, not an offset from the previous joint: the
	 * meshes import already assembled, so the rig is built by pivoting posed
	 * geometry in place rather than by stacking a chain of relative transforms.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Link")
	FVector JointCentre = FVector::ZeroVector;

	/** Rotation axis, in that same frame. Normalised on use. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Link")
	FVector JointAxis = FVector::ZAxisVector;

	/** Travel limits in degrees; the arm never drives outside these. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Link")
	float MinAngle = -180.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Link")
	float MaxAngle = 180.0f;
};

/**
 * An articulated arm assembled from per-link static meshes.
 *
 * Built as a component hierarchy rather than a skeletal mesh because a twin's
 * robot pose comes from joint values on the wire. Six angles in, six component
 * rotations out, and no animation asset that would play a fixed motion
 * regardless of what the data says.
 *
 * Link geometry is transcribed from a URDF, so the arm is dimensionally correct
 * and respects the real machine's travel limits.
 */
UCLASS()
class FACTORYSIM_API AFactoryRobotArm : public AActor
{
	GENERATED_BODY()

public:
	AFactoryRobotArm();

	/** Fixed pedestal the chain hangs from; may also be several meshes. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Robot")
	TArray<TObjectPtr<UStaticMesh>> BaseMeshes;

	/** The driven chain, base outward. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Robot")
	TArray<FFactoryRobotLink> Links;

	/**
	 * Sweep the arm through a idle demonstration pose cycle.
	 *
	 * Useful for a demo level. Turn it off and call SetJointAngles from
	 * whatever actually owns the robot's motion.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Robot")
	bool bDemoMotion = true;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Robot",
		meta = (EditCondition = "bDemoMotion", ClampMin = "0.01"))
	float DemoCycleSeconds = 8.0f;

	/** Drives the arm. Values outside a joint's limits are clamped, not wrapped. */
	UFUNCTION(BlueprintCallable, Category = "Robot")
	void SetJointAngles(const TArray<float>& AnglesDegrees);

	UFUNCTION(BlueprintPure, Category = "Robot")
	TArray<float> GetJointAngles() const { return JointAngles; }

	/** Rebuilds the component hierarchy from Links. Called on construction. */
	void RebuildHierarchy();

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

private:
	void ApplyJointAngles();

	UPROPERTY()
	TObjectPtr<USceneComponent> RootScene;

	UPROPERTY()
	TObjectPtr<USceneComponent> BaseComponent;

	/** One pivot per joint; rotating it moves everything outboard of it. */
	UPROPERTY()
	TArray<TObjectPtr<USceneComponent>> JointPivots;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> LinkComponents;

	/** Spawns one mesh component per entry, all sharing Parent's frame. */
	void AttachMeshes(USceneComponent* Parent, const TArray<TObjectPtr<UStaticMesh>>& Meshes,
		const FVector& RelativeLocation, const FString& NamePrefix);

	UPROPERTY()
	TArray<float> JointAngles;

	UPROPERTY()
	TObjectPtr<UFactoryMachineComponent> Machine;

	float DemoTime = 0.0f;
};
