#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "FactoryLayoutGrid.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;

/**
 * A cell on the factory floor plan.
 *
 * The layout is authored in metres because that is how a factory is drawn and
 * discussed, but "3.7 metres" is not a placement anyone means. Cells make the
 * intent explicit: a machine sits in a cell, and two machines in adjacent cells
 * line up exactly.
 */
USTRUCT(BlueprintType)
struct FACTORYSIM_API FFactoryGridCoord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Grid")
	int32 X = 0;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Grid")
	int32 Y = 0;

	FFactoryGridCoord() = default;
	FFactoryGridCoord(const int32 InX, const int32 InY) : X(InX), Y(InY) {}

	bool operator==(const FFactoryGridCoord& Other) const
	{
		return X == Other.X && Y == Other.Y;
	}
};

FORCEINLINE uint32 GetTypeHash(const FFactoryGridCoord& Coord)
{
	return HashCombine(::GetTypeHash(Coord.X), ::GetTypeHash(Coord.Y));
}

/**
 * Floor-plan grid maths.
 *
 * One place that knows the pitch and the metres-to-centimetres conversion, so a
 * layout position means the same thing to the seeder, the level builder, the
 * production line and anything placing machines over MCP.
 */
namespace FactoryGrid
{
	constexpr double MetresToCm = 100.0;

	/** Configured pitch, from project settings. */
	FACTORYSIM_API float GetPitchMetres();

	/** Nearest grid intersection. A pitch of zero or less uses the configured one. */
	FACTORYSIM_API FVector2D SnapMetres(const FVector2D& Metres, float PitchMetres = 0.0f);

	FACTORYSIM_API FFactoryGridCoord MetresToCell(const FVector2D& Metres, float PitchMetres = 0.0f);
	FACTORYSIM_API FVector2D CellToMetres(const FFactoryGridCoord& Cell, float PitchMetres = 0.0f);

	/** Floor-plan metres to an Unreal world location. */
	FACTORYSIM_API FVector MetresToWorld(const FVector2D& Metres, double ZCm = 0.0);
}

/**
 * Draws the floor-plan grid so placements can be read off the level.
 *
 * Lines are instances of one mesh rather than separate actors: a 30 by 16 metre
 * floor at half-metre pitch is well over a hundred lines, and that many actors
 * makes the outliner unusable for the machines that actually matter.
 */
UCLASS()
class FACTORYSIM_API AFactoryFloorGrid : public AActor
{
	GENERATED_BODY()

public:
	AFactoryFloorGrid();

	/** Extent covered, in floor-plan metres. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Grid")
	FVector2D MinMetres = FVector2D(-4.0, -8.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Grid")
	FVector2D MaxMetres = FVector2D(24.0, 8.0);

	/** Zero or less takes the pitch from project settings. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Grid", meta = (Units = "m"))
	float PitchMetresOverride = 0.0f;

	/** Every Nth line is drawn heavier, so the eye can count cells. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Grid", meta = (ClampMin = "1"))
	int32 MajorEvery = 4;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Grid", meta = (ClampMin = "0.1"))
	float MinorWidthCm = 1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Grid", meta = (ClampMin = "0.1"))
	float MajorWidthCm = 3.0f;

	/** Height above the floor. Enough to beat z-fighting, little enough to read flat. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Grid")
	float HeightCm = 0.6f;

	/** Rebuilds the line instances. Called on construction. */
	UFUNCTION(BlueprintCallable, Category = "Grid")
	void RebuildGrid();

	/** Resolved pitch, honouring the override. */
	UFUNCTION(BlueprintPure, Category = "Grid")
	float GetPitchMetres() const;

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	//~ End AActor

private:
	/** Adds one line instance spanning From to To, in floor-plan metres. */
	void AddLine(UInstancedStaticMeshComponent* Target,
		const FVector2D& FromMetres, const FVector2D& ToMetres, float WidthCm);

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> MinorLines;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> MajorLines;
};
