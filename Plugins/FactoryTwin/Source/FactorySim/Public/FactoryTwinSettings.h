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
