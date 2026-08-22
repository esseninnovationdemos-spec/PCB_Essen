#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "FactoryProduct.generated.h"

class UStaticMeshComponent;

/**
 * How far through assembly a unit is.
 *
 * Ordered, and compared with >=, so a stage both names a step and reveals every
 * part fitted up to it. The values are wire-visible: they go out as the unit's
 * stage metric, so inserting a step in the middle renumbers history. Append.
 */
UENUM(BlueprintType)
enum class EFactoryProductStage : uint8
{
	/** Bare carrier, nothing fitted yet. */
	Empty = 0,
	HousingFitted = 1,
	PinsInserted = 2,
	BoardFitted = 3,
	Tested = 4,
	Programmed = 5,
	LidFitted = 6,
	FunctionTested = 7,
	Packed = 8,
};

/**
 * One unit travelling the assembly line.
 *
 * Built from primitives rather than an authored mesh so the geometry can be
 * revealed a step at a time: the point of showing a unit at all is that you can
 * see what has been done to it, and a single finished mesh cannot show that.
 */
UCLASS()
class FACTORYSIM_API AFactoryProduct : public AActor
{
	GENERATED_BODY()

public:
	AFactoryProduct();

	/** Serial carried through the line and stamped onto each station's part id. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Product")
	FString Serial;

	UFUNCTION(BlueprintCallable, Category = "Product")
	void SetStage(EFactoryProductStage NewStage);

	UFUNCTION(BlueprintPure, Category = "Product")
	EFactoryProductStage GetStage() const { return Stage; }

	/** Drives the status lamp. A failed unit stays visibly failed. */
	UFUNCTION(BlueprintCallable, Category = "Product")
	void SetPassed(bool bInPassed);

	UFUNCTION(BlueprintPure, Category = "Product")
	bool HasPassed() const { return bPassed; }

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	//~ End AActor

private:
	/** Shows or hides each part according to the current stage. */
	void ApplyStage();

	/**
	 * Creates one part, sized and placed but with no mesh yet.
	 *
	 * @param SizeMetres  Full size, not half-extent. The basic shapes are one
	 *                    metre across, so this doubles as the scale.
	 * @param CentreCm    Centre relative to the carrier's centre.
	 */
	UStaticMeshComponent* CreatePart(const TCHAR* Name,
		const FVector& SizeMetres, const FVector& CentreCm);

	/**
	 * Assigns meshes and colours.
	 *
	 * Separate from the constructor because loading assets while the class
	 * default object is being built is not safe; OnConstruction runs late enough
	 * that the content is there to load.
	 */
	void BuildVisuals();

	/** Applies one of the shared shape materials, by name. */
	void TintPart(UStaticMeshComponent* Part, const TCHAR* MaterialName);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Carrier;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Housing;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Connector;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Board;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Lid;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Carton;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> StatusLamp;

	UPROPERTY()
	EFactoryProductStage Stage = EFactoryProductStage::Empty;

	UPROPERTY()
	bool bPassed = true;
};
