#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SparkplugEdgeNode.h"

#include "FactoryTwinSettings.generated.h"

/**
 * Project settings for the factory twin, under Project Settings > Plugins.
 *
 * Broker credentials are deliberately not stored here. The existing line supplies
 * them at runtime from the UI, and the shipped config has them blank so no
 * secrets sit on disk; that stays true.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Factory Twin"))
class FACTORYSIM_API UFactoryTwinSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Master switch between the native simulation and the legacy Python layer.
	 *
	 * Kept so both paths can run against the same broker during migration and be
	 * diffed on the wire. Once parity is confirmed, the Python layer and this
	 * switch both go away.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Migration")
	bool bUseNativeSimulation = true;

	/** Connect and announce automatically when play begins. */
	UPROPERTY(Config, EditAnywhere, Category = "Connection")
	bool bAutoStartOnBeginPlay = false;

	/**
	 * Keep releasing boards for as long as the line is up, instead of waiting
	 * for an operator button press or an inbound `new_material` command.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Auto Production")
	bool bAutoProduceOnStart = false;

	/**
	 * Gap between board releases.
	 *
	 * This is line takt, not machine cycle time. Releasing faster than the
	 * slowest station can clear will pile boards up at that station -- which is
	 * a legitimate thing to simulate, but do it deliberately.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Auto Production",
		meta = (ClampMin = "0.1", Units = "s"))
	float AutoProductionIntervalSeconds = 30.0f;

	/**
	 * Floor-plan grid pitch, in metres.
	 *
	 * Layout is authored in metres because that is how a factory is drawn, but
	 * placements should land on a regular pitch rather than wherever a number
	 * happened to fall -- machines that share a cell edge line up, and a layout
	 * can be described as cells instead of decimals. Everything that places a
	 * machine snaps through this.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Layout",
		meta = (ClampMin = "0.05", Units = "m"))
	float GridPitchMetres = 0.5f;

	/**
	 * Identity and broker settings.
	 *
	 * Group and node id are wire-visible; the downstream ClickHouse bridge and
	 * Ignition both key off them. Default host points at the local development
	 * broker in Tools/broker until the production one is reachable.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Connection")
	FSparkplugEdgeNodeConfig EdgeNode;

	// --- External control -------------------------------------------------

	/**
	 * Hand station sequencing to an outside controller.
	 *
	 * Off, the line runs itself on takt and a controller can still gate
	 * individual stations. On, stations cycle only when triggered, so a
	 * controller that goes quiet stops the line -- which is the point, and why
	 * the watchdog below exists.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "External Control")
	bool bExternalControlEnabled = false;

	/**
	 * Sparkplug edge node id the PLC publishes under.
	 *
	 * The twin follows this peer's DATA stream because a groov EPIC is an edge
	 * node, not a primary application: it publishes its own tags but has no way
	 * to send us a DCMD. Leave blank to accept commands only from a real primary
	 * application over DCMD, such as Ignition.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "External Control")
	FString PlcEdgeNodeId = TEXT("PLC01");

	/** How long the PLC may go quiet before the watchdog acts. */
	UPROPERTY(Config, EditAnywhere, Category = "External Control",
		meta = (ClampMin = "1.0", Units = "s"))
	float PlcTimeoutSeconds = 10.0f;

	/**
	 * On timeout, hand sequencing back to the line instead of stopping.
	 *
	 * A demo that halts because a cable was nudged is worse than one that
	 * quietly keeps running, so this defaults on. Turn it off when the point of
	 * the exercise is to show the interlock actually holding.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "External Control")
	bool bFallBackToLocalOnPlcTimeout = true;

	/**
	 * Address of the groov EPIC, for diagnostics and commissioning only.
	 *
	 * BLANK until the device is on the network -- fill this in when it is. The
	 * control path does not use it: the PLC reaches us through the broker, so
	 * the address the PLC needs is the broker's, configured on the device in
	 * groov Manage. Nothing here opens a connection to it.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "External Control")
	FString PlcHostAddress;

	UFactoryTwinSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UFactoryTwinSettings* Get();
};
