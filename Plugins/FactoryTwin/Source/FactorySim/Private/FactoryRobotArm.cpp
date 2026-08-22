#include "FactoryRobotArm.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "FactoryMachineComponent.h"
#include "FactorySimTypes.h"

AFactoryRobotArm::AFactoryRobotArm()
{
	PrimaryActorTick.bCanEverTick = true;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootScene);

	BaseComponent = CreateDefaultSubobject<USceneComponent>(TEXT("BaseLink"));
	BaseComponent->SetupAttachment(RootScene);
}

void AFactoryRobotArm::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildHierarchy();
}

void AFactoryRobotArm::RebuildHierarchy()
{
	// Tear down any previous chain: OnConstruction can run repeatedly in editor.
	for (USceneComponent* Pivot : JointPivots)
	{
		if (Pivot != nullptr)
		{
			Pivot->DestroyComponent();
		}
	}
	for (UStaticMeshComponent* Link : LinkComponents)
	{
		if (Link != nullptr)
		{
			Link->DestroyComponent();
		}
	}
	JointPivots.Reset();
	LinkComponents.Reset();

	JointAngles.SetNumZeroed(Links.Num());
	AttachMeshes(BaseComponent, BaseMeshes, FVector::ZeroVector, TEXT("Base"));

	// The link meshes are exported already assembled: every one of them is
	// authored in a single shared frame, in the pose the CAD model was saved
	// in. So the chain is built by putting each pivot at its joint's absolute
	// centre in that frame and pulling the mesh back by the same amount. At
	// rest every mesh lands exactly where it was modelled, and rotating a pivot
	// swings it -- and everything outboard -- about the real joint centre.
	USceneComponent* Parent = BaseComponent;
	FVector PreviousCentre = FVector::ZeroVector;

	for (int32 Index = 0; Index < Links.Num(); ++Index)
	{
		const FFactoryRobotLink& Link = Links[Index];

		USceneComponent* Pivot = NewObject<USceneComponent>(
			this, USceneComponent::StaticClass(),
			*FString::Printf(TEXT("J%d_Pivot"), Index + 1));
		Pivot->SetupAttachment(Parent);
		// Differenced, because a component's placement is relative to its parent
		// while the table stores absolute centres.
		Pivot->SetRelativeLocation(Link.JointCentre - PreviousCentre);
		Pivot->RegisterComponent();
		JointPivots.Add(Pivot);

		AttachMeshes(Pivot, Link.Meshes, -Link.JointCentre,
			FString::Printf(TEXT("Link_%d"), Index + 1));

		Parent = Pivot;
		PreviousCentre = Link.JointCentre;
	}

	ApplyJointAngles();
}

void AFactoryRobotArm::AttachMeshes(
	USceneComponent* Parent,
	const TArray<TObjectPtr<UStaticMesh>>& Meshes,
	const FVector& RelativeLocation,
	const FString& NamePrefix)
{
	if (Parent == nullptr)
	{
		return;
	}

	for (int32 Index = 0; Index < Meshes.Num(); ++Index)
	{
		if (Meshes[Index] == nullptr)
		{
			continue;
		}

		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(
			this, UStaticMeshComponent::StaticClass(),
			*FString::Printf(TEXT("%s_Mesh_%d"), *NamePrefix, Index));
		Component->SetupAttachment(Parent);
		Component->SetStaticMesh(Meshes[Index]);
		Component->SetRelativeLocation(RelativeLocation);
		// Visual only: the arm's motion comes from joint values, not physics.
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->RegisterComponent();
		LinkComponents.Add(Component);
	}
}

void AFactoryRobotArm::BeginPlay()
{
	Super::BeginPlay();
	Machine = FindComponentByClass<UFactoryMachineComponent>();
}

void AFactoryRobotArm::SetJointAngles(const TArray<float>& AnglesDegrees)
{
	// A caller can reach this before OnConstruction has sized the array.
	if (JointAngles.Num() != Links.Num())
	{
		JointAngles.SetNumZeroed(Links.Num());
	}

	const int32 Count = FMath::Min(AnglesDegrees.Num(), Links.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		// Clamp rather than wrap: driving a joint past its stop should read as
		// "at the limit", not as a full revolution the real machine cannot do.
		JointAngles[Index] = FMath::Clamp(
			AnglesDegrees[Index], Links[Index].MinAngle, Links[Index].MaxAngle);
	}
	ApplyJointAngles();
}

void AFactoryRobotArm::ApplyJointAngles()
{
	for (int32 Index = 0; Index < JointPivots.Num() && Index < Links.Num(); ++Index)
	{
		USceneComponent* Pivot = JointPivots[Index];
		if (Pivot == nullptr)
		{
			continue;
		}

		// Rotate about the joint's own axis. Every pivot rests axis-aligned with
		// the mesh frame, so the tabled axis is directly usable as a relative
		// rotation; once a parent joint moves, the child's axis co-rotates with
		// it, which is what a real chain does.
		//
		// Negated because the URDF is right-handed and Unreal is not: a positive
		// angle on the real machine turns the other way here.
		Pivot->SetRelativeRotation(FQuat(
			Links[Index].JointAxis.GetSafeNormal(),
			FMath::DegreesToRadians(-JointAngles[Index])));
	}
}

void AFactoryRobotArm::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bDemoMotion || Links.Num() == 0)
	{
		return;
	}

	// Only move while the machine is actually working, so a stopped line shows a
	// stopped arm rather than one waving at nothing.
	if (Machine != nullptr && Machine->GetMachineState() == EFactoryMachineState::Idle)
	{
		return;
	}

	DemoTime += DeltaSeconds;
	const float Phase = (DemoCycleSeconds > 0.0f)
		? (DemoTime / DemoCycleSeconds) * 2.0f * PI
		: 0.0f;

	// Sweep each joint across a fraction of its travel, offset per joint so the
	// arm describes a reach-and-return rather than every axis moving in unison.
	TArray<float> Angles;
	Angles.Reserve(Links.Num());
	for (int32 Index = 0; Index < Links.Num(); ++Index)
	{
		const FFactoryRobotLink& Link = Links[Index];
		const float Centre = (Link.MinAngle + Link.MaxAngle) * 0.5f;
		const float HalfTravel = (Link.MaxAngle - Link.MinAngle) * 0.5f;
		// A third of travel keeps the pose plausible instead of slamming stops.
		const float Amplitude = HalfTravel * 0.33f;
		Angles.Add(Centre + Amplitude * FMath::Sin(Phase + Index * 0.7f));
	}

	SetJointAngles(Angles);
}
