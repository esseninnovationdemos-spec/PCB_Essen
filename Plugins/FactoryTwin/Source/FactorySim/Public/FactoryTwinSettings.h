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

	UFactoryTwinSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UFactoryTwinSettings* Get();
};
