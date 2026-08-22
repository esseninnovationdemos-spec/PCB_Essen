#include "FactoryOperatorStation.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "FactoryLineSubsystem.h"
#include "FactoryMachineComponent.h"
#include "FactorySimTypes.h"

namespace
{
	const TCHAR* DefaultMesh = TEXT("/Game/Human/Human_Lvl2.Human_Lvl2");
	const TCHAR* DefaultWorking = TEXT("/Game/Human/Human_Work_Sped_Anim1.Human_Work_Sped_Anim1");
	const TCHAR* DefaultResting = TEXT("/Game/Human/Human_Idle_Anim.Human_Idle_Anim");
}

AFactoryOperatorStation::AFactoryOperatorStation()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Operator = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Operator"));
	Operator->SetupAttachment(Root);
	// A person standing at a bench should not shove the bench.
	Operator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// One animation at a time, chosen here, so no animation Blueprint is needed.
	Operator->SetAnimationMode(EAnimationMode::AnimationSingleNode);
}

void AFactoryOperatorStation::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// Resolved here rather than in the constructor: loading during class default
	// object construction attaches what it loads to the CDO.
	if (OperatorMesh == nullptr)
	{
		OperatorMesh = LoadObject<USkeletalMesh>(nullptr, DefaultMesh);
	}
	if (WorkingAnimation == nullptr)
	{
		WorkingAnimation = LoadObject<UAnimSequence>(nullptr, DefaultWorking);
	}
	if (RestingAnimation == nullptr)
	{
		RestingAnimation = LoadObject<UAnimSequence>(nullptr, DefaultResting);
	}

	if (Operator != nullptr && OperatorMesh != nullptr)
	{
		Operator->SetSkeletalMesh(OperatorMesh);
		// Assigning a mesh rebuilds the component's animation state, which drops
		// the single-node mode set in the constructor. Restated here, and again
		// before every play, so the operator cannot end up stood in a T-pose.
		Operator->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	}
}

void AFactoryOperatorStation::BeginPlay()
{
	Super::BeginPlay();
	ApplyState();
}

void AFactoryOperatorStation::ApplyState()
{
	if (Operator == nullptr)
	{
		return;
	}

	// Look the machine up lazily: operators and stations are spawned in one pass
	// and the machines register themselves on their own BeginPlay, so it may not
	// exist yet the first time this runs.
	if (Machine == nullptr && !ServedDeviceId.IsEmpty())
	{
		UWorld* World = GetWorld();
		if (UFactoryLineSubsystem* Line = (World != nullptr)
			? World->GetSubsystem<UFactoryLineSubsystem>() : nullptr)
		{
			Machine = Line->FindMachine(ServedDeviceId);
		}
	}

	const bool bWorking = (Machine != nullptr)
		&& Machine->GetMachineState() == EFactoryMachineState::Running;

	UAnimSequence* Wanted = bWorking ? WorkingAnimation : RestingAnimation;
	if (Wanted == nullptr)
	{
		UE_LOG(LogFactorySim, Warning,
			TEXT("Operator at '%s' has no %s animation; it will stand in its bind pose"),
			*ServedDeviceId, bWorking ? TEXT("working") : TEXT("resting"));
		return;
	}
	if (Wanted == Playing)
	{
		return;
	}

	Operator->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	// Force the animation instance to exist. SetAnimationMode does nothing when
	// the mode already matches, and the mode is set before the mesh is assigned,
	// so without this the component can end up with no instance to evaluate --
	// which shows as a character standing in its bind pose while the log happily
	// reports the animation it is "playing".
	Operator->InitAnim(/*bForceReinit*/ true);
	Operator->PlayAnimation(Wanted, /*bLooping*/ true);
	// Bones have to be refreshed even when the operator is off screen, or the
	// pose only resolves in frames where something happens to be looking.
	Operator->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Playing = Wanted;

	UE_LOG(LogFactorySim, Verbose, TEXT("Operator at '%s' now playing '%s' (mesh %s)"),
		*ServedDeviceId, *Wanted->GetName(),
		Operator->GetSkeletalMeshAsset() != nullptr
			? *Operator->GetSkeletalMeshAsset()->GetName() : TEXT("none"));
}

void AFactoryOperatorStation::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Polled rather than checked every frame: a person picking up or putting
	// down their work a quarter of a second late is imperceptible, and this runs
	// once per operator per line.
	PollCountdown -= DeltaSeconds;
	if (PollCountdown > 0.0f)
	{
		return;
	}
	PollCountdown = PollSeconds;
	ApplyState();
}
