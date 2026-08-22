#pragma once

#include "CoreMinimal.h"
#include "FactoryConveyor.h"
#include "FactoryProduct.h"
#include "GameFramework/Actor.h"

#include "FactoryProductionLine.generated.h"

class AFactoryProduct;
class UFactoryMachineComponent;
class UStaticMeshComponent;
class UUserWidget;

/** One position on the belt, and the machine that works on whatever stops there. */
USTRUCT(BlueprintType)
struct FACTORYSIM_API FFactoryLineStop
{
	GENERATED_BODY()

	/**
	 * Sparkplug device id of the machine serving this stop. May be empty for a
	 * pure transport position.
	 *
	 * Named rather than referenced because the machine is a separate actor
	 * placed from its own instance asset, and a stop should not care whether
	 * that actor exists yet.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Stop")
	FString DeviceId;

	/** Where the unit waits, in floor-plan metres. Snapped to the grid on build. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Stop")
	FVector2D PositionMetres = FVector2D::ZeroVector;

	/**
	 * How long the unit is held here.
	 *
	 * Zero takes the machine's own cycle-time metric, so the dwell and the
	 * duration the machine reports cannot drift apart.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Stop",
		meta = (ClampMin = "0.0", Units = "s"))
	float DwellSecondsOverride = 0.0f;

	/** Stage the unit reaches when this stop finishes with it. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Stop")
	EFactoryProductStage StageOnComplete = EFactoryProductStage::Empty;
};

/**
 * A unit and where it is in the line.
 *
 * Declared here rather than nested in the line actor because UHT does not
 * support struct types declared inside a UCLASS.
 */
USTRUCT()
struct FFactoryUnitInFlight
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AFactoryProduct> Product;

	/**
	 * Stop the unit holds or is travelling to; INDEX_NONE once it has been
	 * released to the exit.
	 *
	 * A unit reserves its target stop the moment it starts moving toward it, not
	 * on arrival, so two units can never converge on the same stop.
	 */
	UPROPERTY()
	int32 TargetStop = 0;

	/** True once it has reached TargetStop and the station has it. */
	UPROPERTY()
	bool bArrived = false;

	/** Dwell remaining, seconds. */
	UPROPERTY()
	float DwellRemaining = 0.0f;

	/** Set while waiting on an occupied stop downstream. */
	UPROPERTY()
	bool bBlocked = false;
};

/**
 * Moves units down a line of stops, driving each station as it goes.
 *
 * The stations already publish plausible telemetry on their own timers, but
 * each on its own clock, so nothing on the line is related to anything else:
 * two neighbouring machines can both be mid-cycle on a line carrying no work.
 * Here the material is what exists, and a machine runs because a unit is in
 * front of it. That makes the wire traffic tell a consistent story, and it makes
 * blocking fall out for free -- a stop that cannot hand its unit onward holds
 * it, and its machine goes Blocked, which is what a real line does.
 */
UCLASS()
class FACTORYSIM_API AFactoryProductionLine : public AActor
{
	GENERATED_BODY()

public:
	AFactoryProductionLine();

	/** Stops in process order. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line")
	TArray<FFactoryLineStop> Stops;

	/** Where units appear, and where they are taken away. Floor-plan metres. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line")
	FVector2D EntryMetres = FVector2D(-2.5, 0.0);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line")
	FVector2D ExitMetres = FVector2D(22.5, 0.0);

	/** Gap between unit releases. This is takt, not machine cycle time. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line",
		meta = (ClampMin = "0.5", Units = "s"))
	float TaktSeconds = 14.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line", meta = (ClampMin = "0.01"))
	float TransportSpeedMetresPerSecond = 0.9f;

	/**
	 * Height of the belt surface, where units ride.
	 *
	 * Defaults to the height every conveyor and every machine's integral
	 * conveyor in this project shares. A unit riding at any other height does
	 * not line up with the machines it is supposedly passing through.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line")
	float BeltHeightCm = AFactoryConveyor::BeltHeightCm;

	/**
	 * Draw a plain box in place of the belt.
	 *
	 * Off by default now that real conveyor is built between the machines; it
	 * remains for laying out a line before the conveyor exists.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line")
	bool bShowBelt = false;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line", meta = (ClampMin = "0.05"))
	float BeltWidthMetres = 0.4f;

	/** Start releasing units as soon as the line comes online. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line")
	bool bAutoStart = true;

	/**
	 * Conveyor device driven alongside production.
	 *
	 * The belt is a machine like any other: it should report running while the
	 * line runs and idle when it stops, rather than publishing a steady speed
	 * for a line that is standing still.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line")
	FString ConveyorDeviceId = TEXT("ASSEMBLY_CONVEYOR");

	UFUNCTION(BlueprintCallable, Category = "Line")
	void StartProduction();

	UFUNCTION(BlueprintCallable, Category = "Line")
	void StopProduction();

	UFUNCTION(BlueprintPure, Category = "Line")
	bool IsProducing() const { return bProducing; }

	/** Units that have reached the exit. */
	UFUNCTION(BlueprintPure, Category = "Line")
	int32 GetUnitsCompleted() const { return UnitsCompleted; }

	UFUNCTION(BlueprintPure, Category = "Line")
	int32 GetUnitsInProgress() const { return Units.Num(); }

	UFUNCTION(BlueprintPure, Category = "Line")
	float GetTaktSeconds() const { return TaktSeconds; }

	/**
	 * Ready-to-display status lines.
	 *
	 * Formatted here rather than assembled from Blueprint nodes: the wording is
	 * part of the line's behaviour, it is the same in every panel that shows it,
	 * and a string built in C++ can be changed without opening a widget graph.
	 */
	UFUNCTION(BlueprintPure, Category = "Line|UI")
	FText GetLineStateText() const;

	UFUNCTION(BlueprintPure, Category = "Line|UI")
	FText GetInProgressText() const;

	UFUNCTION(BlueprintPure, Category = "Line|UI")
	FText GetCompletedText() const;

	UFUNCTION(BlueprintPure, Category = "Line|UI")
	FText GetTaktText() const;

	/**
	 * Operator panel put on screen when play begins.
	 *
	 * The line owns it rather than a separate manager Blueprint because the
	 * panel exists to show and drive this line, and pairing them here means a
	 * level gets its UI simply by having a line in it.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Line|UI")
	TSubclassOf<UUserWidget> HudWidgetClass;

	/** Rebuilds the belt mesh. Called on construction. */
	void RebuildBelt();

	//~ AActor
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	//~ End AActor

private:
	UPROPERTY()
	TArray<FFactoryUnitInFlight> Units;

	/** Advances one unit. Returns false if the unit was retired. */
	bool TickUnit(FFactoryUnitInFlight& Unit, float DeltaSeconds);

	/** Moves toward a floor-plan target. True once it is there. */
	bool MoveToward(AFactoryProduct* Product, const FVector2D& TargetMetres, float DeltaSeconds) const;

	/** True if any unit holds or has reserved this stop. */
	bool IsStopReserved(int32 StopIndex) const;

	void ReleaseUnit(int32 StopIndex);

	UFactoryMachineComponent* MachineForStop(int32 StopIndex) const;

	/** Dwell for a stop, falling back to the machine's cycle-time metric. */
	float ResolveDwellSeconds(int32 StopIndex) const;

	void SpawnUnit();

	/** Puts the conveyor device into Running or Idle. */
	void SetConveyorRunning(bool bRunning);

	UFUNCTION()
	void HandleLineOnline(bool bOnline);

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Belt;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> HudWidget;

	bool bProducing = false;
	bool bBoundToLine = false;
	float TaktAccumulator = 0.0f;
	int32 UnitsCompleted = 0;
	int32 UnitsReleased = 0;
};
