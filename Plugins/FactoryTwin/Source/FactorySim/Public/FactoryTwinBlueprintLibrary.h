#pragma once

#include "CoreMinimal.h"
#include "FactoryLayoutGrid.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "FactoryTwinBlueprintLibrary.generated.h"

class UFactoryLineSubsystem;

/**
 * One-node Blueprint entry points for the factory twin.
 *
 * These exist so a Blueprint does not have to fetch and cast the world
 * subsystem before every call. They deliberately mirror the shape of the legacy
 * `factory_*` Python nodes they replace, so migrating a graph is a like-for-like
 * node swap rather than a rewrite.
 */
UCLASS()
class FACTORYSIM_API UFactoryTwinBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The line subsystem for this world, or null outside a game world. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Factory Line"))
	static UFactoryLineSubsystem* GetFactoryLine(const UObject* WorldContextObject);

	/**
	 * Connects to the broker and announces the line.
	 * Replaces `factory_init`.
	 *
	 * @param BrokerHost     Broker hostname or IP. Empty keeps the configured value.
	 * @param BrokerPort     Broker port. Zero or less keeps the configured value.
	 * @param BrokerUsername Optional; leave empty for an anonymous broker.
	 * @param BrokerPassword Optional; only used alongside a username.
	 * @return True if a connection attempt was started.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Start Factory Line",
			AdvancedDisplay = "BrokerUsername,BrokerPassword"))
	static bool StartFactoryLine(
		const UObject* WorldContextObject,
		const FString& BrokerHost,
		int32 BrokerPort,
		const FString& BrokerUsername,
		const FString& BrokerPassword);

	/**
	 * Publishes the death certificates and disconnects.
	 * Replaces `factory_shutdown`.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Stop Factory Line"))
	static void StopFactoryLine(const UObject* WorldContextObject);

	/**
	 * Publishes an event on a registered device by its Sparkplug device id.
	 * Replaces `factory_publish_event` for callers that are not a machine.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Publish Factory Device Event"))
	static bool PublishFactoryDeviceEvent(
		const UObject* WorldContextObject, const FString& DeviceId, const FString& EventType);

	/**
	 * Publishes a plain string to an arbitrary topic on the line's existing
	 * broker connection.
	 *
	 * This backs the project's UNS stream, which publishes JSON on its own topic
	 * tree alongside Sparkplug. Replaces the MqttUtilities publish path, so the
	 * two streams now share one connection rather than opening two.
	 *
	 * @return True if the line was connected and the publish was queued.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Publish UNS String"))
	static bool PublishUnsString(
		const UObject* WorldContextObject, const FString& Topic, const FString& Payload);

	/**
	 * Releases one board: opens a lot and fires the same path an inbound
	 * `new_material` command takes. This is what an operator "start new
	 * material" button should call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Request New Material"))
	static void RequestNewMaterial(const UObject* WorldContextObject);

	/**
	 * Starts releasing boards continuously.
	 *
	 * @param IntervalSeconds Gap between releases; zero uses the project default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin|Auto Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Start Auto Production"))
	static void StartAutoProduction(const UObject* WorldContextObject, float IntervalSeconds = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Factory Twin|Auto Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Stop Auto Production"))
	static void StopAutoProduction(const UObject* WorldContextObject);

	/** Flips auto production on or off; handy for a single UI toggle. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin|Auto Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Toggle Auto Production"))
	static bool ToggleAutoProduction(const UObject* WorldContextObject, float IntervalSeconds = 0.0f);

	UFUNCTION(BlueprintPure, Category = "Factory Twin|Auto Production",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Auto Production Running"))
	static bool IsAutoProductionRunning(const UObject* WorldContextObject);

	/** True once NBIRTH is out and devices are publishing. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Is Factory Line Online"))
	static bool IsFactoryLineOnline(const UObject* WorldContextObject);

	/** Current lot identifier. Replaces `factory_get_lot_id`. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Get Factory Lot Id"))
	static FString GetFactoryLotId(const UObject* WorldContextObject);

	/** Starts a new lot and returns its id. Replaces `factory_start_new_lot`. */
	UFUNCTION(BlueprintCallable, Category = "Factory Twin",
		meta = (WorldContext = "WorldContextObject", DisplayName = "Start New Factory Lot"))
	static FString StartNewFactoryLot(const UObject* WorldContextObject);

	/**
	 * Snaps a floor-plan position to the configured grid.
	 *
	 * The single entry point for placement maths, so a machine positioned from a
	 * Blueprint, a commandlet or an MCP call all land on the same intersections.
	 */
	UFUNCTION(BlueprintPure, Category = "Factory Twin|Layout",
		meta = (DisplayName = "Snap To Factory Grid"))
	static FVector2D SnapToFactoryGrid(FVector2D PositionMetres);

	/** Grid cell containing a floor-plan position. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin|Layout",
		meta = (DisplayName = "Factory Grid Cell At"))
	static FFactoryGridCoord FactoryGridCellAt(FVector2D PositionMetres);

	/** Centre of a grid cell, in floor-plan metres. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin|Layout",
		meta = (DisplayName = "Factory Grid Cell To Metres"))
	static FVector2D FactoryGridCellToMetres(FFactoryGridCoord Cell);

	/** A floor-plan position as an Unreal world location. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin|Layout",
		meta = (DisplayName = "Factory Layout To World"))
	static FVector FactoryLayoutToWorld(FVector2D PositionMetres, float HeightCm = 0.0f);

	/** Configured grid pitch, in metres. */
	UFUNCTION(BlueprintPure, Category = "Factory Twin|Layout",
		meta = (DisplayName = "Get Factory Grid Pitch"))
	static float GetFactoryGridPitchMetres();
};
