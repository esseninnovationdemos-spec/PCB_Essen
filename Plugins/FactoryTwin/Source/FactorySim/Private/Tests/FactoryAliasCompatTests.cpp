#include "FactoryMachineArchetype.h"
#include "FactoryMachineInstance.h"
#include "FactorySimTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags AliasTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	UFactoryMachineInstance* LoadInstance(const FString& AssetName)
	{
		const FString Path = FString::Printf(
			TEXT("/Game/FactoryTwin/Instances/%s.%s"), *AssetName, *AssetName);
		return LoadObject<UFactoryMachineInstance>(nullptr, *Path);
	}
}

/**
 * Locks the wire alias map to what the legacy Python layer allocated.
 *
 * The downstream SpB-to-ClickHouse bridge is mapped against these numbers, so a
 * shift here silently stops ingest rather than failing loudly. Aliases are
 * therefore pinned per instance rather than derived, and this test is what keeps
 * them pinned: adding a metric to a shared archetype would otherwise renumber
 * every device after it.
 *
 * Expected numbering, from Content/Python/factory_config.toml order:
 *   Conveyor           1-11   COMPONENT_PLACER  12-22
 *   REFLOW_OVEN        23-32  LASER_MARKING     33-43
 *   AUTO_OPTICALINSP   44-53  SOLDER_INSP       54-63
 *   SMT_LINE           64-71
 * Machines added since (the manual stations) take 72 upward and must never
 * displace the run above.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFactoryAliasCompatTest,
	"FactoryTwin.FactorySim.AliasMapMatchesLegacy",
	AliasTestFlags)

bool FFactoryAliasCompatTest::RunTest(const FString& Parameters)
{
	struct FExpectation
	{
		const TCHAR* AssetName;
		const TCHAR* DeviceId;
		TArray<TPair<FString, int64>> Aliases;
	};

	const TArray<FExpectation> Expectations = {
		{ TEXT("I_Conveyor"), TEXT("Conveyor"), {
			{ TEXT("belt_speed"), 1 }, { TEXT("motor_temp"), 2 },
			{ TEXT("rpm"), 3 }, { TEXT("torque"), 4 },
			{ TEXT("state_code"), 5 }, { TEXT("inspection_result"), 6 },
			{ TEXT("fail_counter"), 7 }, { TEXT("conveyor_id"), 8 },
			{ TEXT("current_part_id"), 9 }, { TEXT("part_id"), 10 },
			{ TEXT("event_type"), 11 } } },

		{ TEXT("I_ComponentPlacer"), TEXT("COMPONENT_PLACER"), {
			{ TEXT("arm_pos_x"), 12 }, { TEXT("arm_pos_y"), 13 },
			{ TEXT("arm_pos_z"), 14 }, { TEXT("cycle_time"), 15 },
			{ TEXT("state_code"), 16 }, { TEXT("event_type"), 22 } } },

		{ TEXT("I_ReflowOven"), TEXT("REFLOW_OVEN"), {
			{ TEXT("oven_temp_c"), 23 }, { TEXT("cooling_temp_c"), 24 },
			{ TEXT("cycle_time_sec"), 25 },
			{ TEXT("state_code"), 26 }, { TEXT("event_type"), 32 } } },

		{ TEXT("I_LaserMarking"), TEXT("LASER_MARKING"), {
			{ TEXT("laser_temp_c"), 33 }, { TEXT("laser_power_pct"), 34 },
			{ TEXT("fume_extractor_rpm"), 35 }, { TEXT("cycle_time_sec"), 36 },
			{ TEXT("state_code"), 37 }, { TEXT("event_type"), 43 } } },

		// The case that motivated explicit pinning: cycle_time_sec comes from
		// the shared VisionInspection archetype but must still be numbered
		// third, after this instance's own measurements.
		{ TEXT("I_AutoOpticalInspection"), TEXT("AUTO_OPTICALINSP"), {
			{ TEXT("comp_position_offset_mm"), 44 }, { TEXT("solder_quality_score"), 45 },
			{ TEXT("cycle_time_sec"), 46 },
			{ TEXT("state_code"), 47 }, { TEXT("event_type"), 53 } } },

		{ TEXT("I_SolderInspection"), TEXT("SOLDER_INSP"), {
			{ TEXT("area"), 54 }, { TEXT("volume"), 55 },
			{ TEXT("cycle_time_sec"), 56 },
			{ TEXT("state_code"), 57 }, { TEXT("event_type"), 63 } } },

		{ TEXT("I_SmtLine"), TEXT("SMT_LINE"), {
			{ TEXT("cycle_time"), 64 },
			{ TEXT("state_code"), 65 }, { TEXT("event_type"), 71 } } },
	};

	for (const FExpectation& Expectation : Expectations)
	{
		UFactoryMachineInstance* Instance = LoadInstance(Expectation.AssetName);
		if (Instance == nullptr)
		{
			AddError(FString::Printf(
				TEXT("Could not load %s. Run the seeder: "
					 "UnrealEditor-Cmd.exe AutoMotion_PCB.uproject -run=FactorySeed"),
				Expectation.AssetName));
			continue;
		}

		TestEqual(FString::Printf(TEXT("%s device id"), Expectation.AssetName),
			Instance->GetDeviceId(), FString(Expectation.DeviceId));

		for (const TPair<FString, int64>& Pair : Expectation.Aliases)
		{
			TestEqual(
				FString::Printf(TEXT("%s alias for '%s'"), Expectation.DeviceId, *Pair.Key),
				Instance->GetAlias(Pair.Key), Pair.Value);
		}

		// Every metric the placement can emit must be mapped, or DDATA would go
		// out alias-less and the bridge would not resolve it.
		for (const FString& MetricName : Instance->GetAllMetricNames())
		{
			TestTrue(
				FString::Printf(TEXT("%s has an alias for '%s'"),
					Expectation.DeviceId, *MetricName),
				Instance->GetAlias(MetricName) != 0);
		}
	}

	return true;
}

/** The manual stations must sit above the legacy run, never inside it. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFactoryManualStationAliasTest,
	"FactoryTwin.FactorySim.ManualStationsDoNotDisplaceLegacyAliases",
	AliasTestFlags)

bool FFactoryManualStationAliasTest::RunTest(const FString& Parameters)
{
	const TArray<FString> ManualAssets = { TEXT("I_PcbCleaner"), TEXT("I_SolderPasteStation") };

	for (const FString& AssetName : ManualAssets)
	{
		UFactoryMachineInstance* Instance = LoadInstance(AssetName);
		if (Instance == nullptr)
		{
			AddError(FString::Printf(TEXT("Could not load %s; run -run=FactorySeed"), *AssetName));
			continue;
		}

		TestNotNull(FString::Printf(TEXT("%s has an archetype"), *AssetName),
			Instance->Archetype.Get());

		if (Instance->Archetype != nullptr)
		{
			// A manual station must not be modelled as an automated one: no
			// warmup or cooldown ramp, because an operator has no such phase.
			TestEqual(FString::Printf(TEXT("%s uses the Manual state model"), *AssetName),
				Instance->Archetype->StateModel, EFactoryStateModel::Manual);
		}

		for (const FString& MetricName : Instance->GetAllMetricNames())
		{
			const int64 Alias = Instance->GetAlias(MetricName);
			TestTrue(
				FString::Printf(TEXT("%s alias for '%s' is above the legacy run"),
					*AssetName, *MetricName),
				Alias > 71);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
