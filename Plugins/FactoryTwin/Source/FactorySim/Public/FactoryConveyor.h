#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "FactoryConveyor.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * A run of conveyor of any length, tiled from the project's conveyor section.
 *
 * Parametric rather than a placed mesh because a line is laid out by where its
 * machines are: the gaps between them are whatever the layout makes them, and
 * they change whenever a station moves. A fixed-length prop would have to be
 * re-authored every time; this fills the gap it is given.
 */
UCLASS()
class FACTORYSIM_API AFactoryConveyor : public AActor
{
	GENERATED_BODY()

public:
	AFactoryConveyor();

	/** Length of the run, along the actor's +X axis. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Conveyor",
		meta = (ClampMin = "0.1", Units = "m"))
	float LengthMetres = 2.0f;

	/** Section to tile. Defaults to the project's simple conveyor. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Conveyor")
	TObjectPtr<UStaticMesh> SectionMesh;

	/**
	 * Height of the belt surface above the floor.
	 *
	 * Every station in this project carries its own integral conveyor topping
	 * out at 90.3 cm, and the standalone sections match it. Anything riding the
	 * line has to sit at that height or it will not line up with the machines.
	 */
	static constexpr float BeltHeightCm = 90.3f;

	/** Rebuilds the tiled sections. Called on construction. */
	UFUNCTION(BlueprintCallable, Category = "Conveyor")
	void RebuildConveyor();

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	//~ End AActor

private:
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Sections;
};
