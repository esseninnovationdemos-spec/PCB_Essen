#include "FactorySeedCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "FactoryMachineArchetype.h"
#include "FactoryMachineInstance.h"
#include "FactorySimTypes.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace SmtSeed
{
	const FString ArchetypeFolder = TEXT("/Game/FactoryTwin/Archetypes");
	const FString InstanceFolder = TEXT("/Game/FactoryTwin/Instances");

	/** Where this line sits in the plant. The original SMT line, on level2. */
	const FString Enterprise = TEXT("InnoLab");
	const FString Site       = TEXT("Essen");
	const FString Area       = TEXT("SMT");
	const FString WorkCentre = TEXT("Line1");

	/** Builds a Float process metric. Most tags are this shape. */
	FFactoryMetricDefinition MakeFloatMetric(
		const FString& Name,
		const FString& Unit,
		const FFactoryRange& Nominal,
		const FFactoryRange& Absolute,
		const double IdleValue,
		const double FaultValue)
	{
		FFactoryMetricDefinition Definition;
		Definition.Name = Name;
		Definition.DataType = ESparkplugDataType::Float;
		Definition.Unit = Unit;
		Definition.Nominal = Nominal;
		Definition.Absolute = Absolute;
		Definition.IdleValue = IdleValue;
		Definition.FaultValue = FaultValue;
		return Definition;
	}

	/** A count rather than a measurement. */
	FFactoryMetricDefinition MakeIntMetric(
		const FString& Name,
		const FString& Unit,
		const FFactoryRange& Nominal,
		const FFactoryRange& Absolute,
		const double IdleValue)
	{
		FFactoryMetricDefinition Definition =
			MakeFloatMetric(Name, Unit, Nominal, Absolute, IdleValue, IdleValue);
		Definition.DataType = ESparkplugDataType::Int32;
		return Definition;
	}

	/** Cycle duration tags only publish when a cycle ends. */
	FFactoryMetricDefinition MakeCycleTimeMetric(
		const FString& Name, const FFactoryRange& Nominal, const FFactoryRange& Absolute)
	{
		FFactoryMetricDefinition Definition =
			MakeFloatMetric(Name, TEXT("s"), Nominal, Absolute, 0.0, 0.0);
		Definition.PublishOn = EFactoryPublishTrigger::CycleComplete;
		return Definition;
	}

	template <typename AssetType>
	AssetType* CreateAsset(const FString& Folder, const FString& AssetName, const bool bForce)
	{
		const FString PackageName = Folder / AssetName;
		const bool bExists = FPackageName::DoesPackageExist(PackageName);

		if (bExists && !bForce)
		{
			UE_LOG(LogFactorySim, Warning,
				TEXT("%s already exists; skipping (pass -Force to overwrite)"), *PackageName);
			return nullptr;
		}

		UPackage* Package = CreatePackage(*PackageName);
		if (Package == nullptr)
		{
			UE_LOG(LogFactorySim, Error, TEXT("Could not create package %s"), *PackageName);
			return nullptr;
		}

		if (bExists)
		{
			// Overwriting needs the old object out of the way first: NewObject
			// cannot claim a name that is still taken, and silently returning a
			// differently-named object would make -Force look like it worked
			// while leaving the old asset on disk untouched.
			Package->FullyLoad();
			if (UObject* Existing = StaticFindObject(nullptr, Package, *AssetName))
			{
				Existing->Rename(
					nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}

		AssetType* Asset = NewObject<AssetType>(
			Package, AssetType::StaticClass(), *AssetName, RF_Public | RF_Standalone);
		return Asset;
	}

	bool SaveAsset(UObject* Asset)
	{
		if (Asset == nullptr)
		{
			return false;
		}

		UPackage* Package = Asset->GetOutermost();
		FAssetRegistryModule::AssetCreated(Asset);
		Package->MarkPackageDirty();

		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;

		if (!UPackage::SavePackage(Package, Asset, *FileName, SaveArgs))
		{
			UE_LOG(LogFactorySim, Error, TEXT("Failed to save %s"), *FileName);
			return false;
		}

		UE_LOG(LogFactorySim, Display, TEXT("Wrote %s"), *Package->GetName());
		return true;
	}


	/**
	 * Returns the archetype, whether it was just created or already existed.
	 *
	 * CreateAsset returns null when an asset is present and -Force was not
	 * passed. Handing that null straight to an instance produced an instance
	 * with no archetype, which fails silently: the machine component logs at
	 * BeginPlay and never registers, so the device simply never appears on the
	 * wire. An incremental re-seed must reuse what is on disk.
	 */
	UFactoryMachineArchetype* Resolve(
		UFactoryMachineArchetype* JustCreated, const TCHAR* AssetName)
	{
		if (JustCreated != nullptr)
		{
			return JustCreated;
		}

		const FString Path = FString::Printf(
			TEXT("%s/%s.%s"), *ArchetypeFolder, AssetName, AssetName);
		UFactoryMachineArchetype* Existing =
			LoadObject<UFactoryMachineArchetype>(nullptr, *Path);
		if (Existing == nullptr)
		{
			UE_LOG(LogFactorySim, Error,
				TEXT("Archetype '%s' neither created nor found on disk"), AssetName);
		}
		return Existing;
	}

	/**
	 * Pins aliases from an explicit, ordered metric list, then the seven
	 * synthetic extras.
	 *
	 * The order is given per device rather than derived from the archetype
	 * because the two do not always agree: AOI's Python allocation runs
	 * comp_position_offset_mm, solder_quality_score, cycle_time_sec, whereas
	 * cycle_time_sec lives on the shared archetype and its instance-specific
	 * measurements are additions. Deriving the order would silently shift
	 * cycle_time_sec from 46 to 44 and break the downstream mapping, so the
	 * numbering is stated outright and verified below.
	 *
	 * Returns the next free alias.
	 */
	int64 PinAliases(
		UFactoryMachineInstance* Instance,
		const int64 Start,
		const TArray<FString>& OrderedMetricNames)
	{
		int64 Next = Start;
		for (const FString& MetricName : OrderedMetricNames)
		{
			Instance->MetricAliases.Add(MetricName, Next++);
		}
		for (const FString& MetricName : FactorySyntheticMetrics::GetAll())
		{
			Instance->MetricAliases.Add(MetricName, Next++);
		}

		// Catch a definition that gained or lost a metric without the alias map
		// being updated to match.
		const TArray<FString> Emittable = Instance->GetAllMetricNames();
		for (const FString& MetricName : Emittable)
		{
			if (!Instance->MetricAliases.Contains(MetricName))
			{
				UE_LOG(LogFactorySim, Error,
					TEXT("%s can emit '%s' but no alias was pinned for it"),
					*Instance->GetDeviceId(), *MetricName);
			}
		}

		UE_LOG(LogFactorySim, Display, TEXT("  %-22s aliases %lld-%lld"),
			*Instance->GetDeviceId(), Start, Next - 1);
		return Next;
	}
}

using namespace SmtSeed;

UFactorySeedCommandlet::UFactorySeedCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UFactorySeedCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);
	const bool bForce = Switches.Contains(TEXT("Force"));

	UE_LOG(LogFactorySim, Display, TEXT("Seeding FactoryTwin archetype library..."));

	TArray<UObject*> Created;

	// =====================================================================
	// Archetypes
	// =====================================================================

	// --- Conveyor --------------------------------------------------------
	UFactoryMachineArchetype* ConveyorType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_Conveyor"), bForce);
	if (ConveyorType != nullptr)
	{
		ConveyorType->ArchetypeName = TEXT("Conveyor");
		ConveyorType->Description = NSLOCTEXT("FactoryTwin", "ConveyorDesc",
			"Continuous transport. Runs whenever the line runs; carries material between stations.");
		ConveyorType->StateModel = EFactoryStateModel::Conveyor;
		ConveyorType->DefaultTickIntervalSeconds = 0.2f;
		ConveyorType->Metrics = {
			MakeFloatMetric(TEXT("belt_speed"), TEXT("m/min"),
				{ 8.0, 18.0 }, { 0.0, 25.0 }, 0.0, 0.0),
			MakeFloatMetric(TEXT("motor_temp"), TEXT("C"),
				{ 35.0, 65.0 }, { 15.0, 105.0 }, 22.0, 95.0),
			MakeFloatMetric(TEXT("rpm"), TEXT("RPM"),
				{ 1800.0, 3500.0 }, { 0.0, 4500.0 }, 0.0, 0.0),
			MakeFloatMetric(TEXT("torque"), TEXT(""),
				{ 240.0, 320.0 }, { 0.0, 450.0 }, 5.0, 420.0),
		};
		// Motor temperature is thermal; it should ease rather than jump.
		ConveyorType->Metrics[1].bThermal = true;
		Created.Add(ConveyorType);
	}

	// --- PickAndPlace ----------------------------------------------------
	UFactoryMachineArchetype* PlacerType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_PickAndPlace"), bForce);
	if (PlacerType != nullptr)
	{
		PlacerType->ArchetypeName = TEXT("PickAndPlace");
		PlacerType->Description = NSLOCTEXT("FactoryTwin", "PlacerDesc",
			"Gantry placement head. Axis positions sweep continuously while running.");
		PlacerType->StateModel = EFactoryStateModel::Automated;
		PlacerType->DefaultTickIntervalSeconds = 0.25f;

		FFactoryMetricDefinition ArmX = MakeFloatMetric(TEXT("arm_pos_x"), TEXT("mm"),
			{ 40.0, 380.0 }, { 0.0, 460.0 }, 0.0, 0.0);
		ArmX.MotionProfile = EFactoryMotionProfile::Sine;
		ArmX.MotionPeriodSeconds = 2.0;

		FFactoryMetricDefinition ArmY = MakeFloatMetric(TEXT("arm_pos_y"), TEXT("mm"),
			{ 30.0, 290.0 }, { 0.0, 340.0 }, 0.0, 0.0);
		ArmY.MotionProfile = EFactoryMotionProfile::Sine;
		ArmY.MotionPeriodSeconds = 3.0;

		// Z is a pick-place-return stroke, so it ramps and snaps back.
		FFactoryMetricDefinition ArmZ = MakeFloatMetric(TEXT("arm_pos_z"), TEXT("mm"),
			{ 88.0, 96.0 }, { 82.0, 102.0 }, 95.0, 95.0);
		ArmZ.MotionProfile = EFactoryMotionProfile::Sawtooth;
		ArmZ.MotionPeriodSeconds = 1.0;

		PlacerType->Metrics = { ArmX, ArmY, ArmZ,
			MakeCycleTimeMetric(TEXT("cycle_time"), { 0.4, 2.5 }, { 0.05, 6.0 }) };
		Created.Add(PlacerType);
	}

	// --- ThermalProcess --------------------------------------------------
	UFactoryMachineArchetype* ThermalType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_ThermalProcess"), bForce);
	if (ThermalType != nullptr)
	{
		ThermalType->ArchetypeName = TEXT("ThermalProcess");
		ThermalType->Description = NSLOCTEXT("FactoryTwin", "ThermalDesc",
			"Oven or furnace with a heated zone and a cooling zone. Temperatures follow an "
			"exponential approach rather than a linear ramp.");
		ThermalType->StateModel = EFactoryStateModel::Automated;
		ThermalType->DefaultTickIntervalSeconds = 1.0f;
		ThermalType->ThermalRampConstant = 3.0f;
		ThermalType->WarmupSeconds = 20.0f;
		ThermalType->CooldownSeconds = 20.0f;

		// SAC305 reflow peak band.
		FFactoryMetricDefinition OvenTemp = MakeFloatMetric(TEXT("oven_temp_c"), TEXT("C"),
			{ 235.0, 248.0 }, { 20.0, 285.0 }, 30.0, 283.0);
		OvenTemp.bThermal = true;
		OvenTemp.bUseWarmupValue = true;
		OvenTemp.WarmupValue = 200.0;

		FFactoryMetricDefinition CoolTemp = MakeFloatMetric(TEXT("cooling_temp_c"), TEXT("C"),
			{ 40.0, 50.0 }, { 25.0, 70.0 }, 25.0, 65.0);
		CoolTemp.bThermal = true;

		ThermalType->Metrics = { OvenTemp, CoolTemp,
			MakeCycleTimeMetric(TEXT("cycle_time_sec"), { 175.0, 185.0 }, { 160.0, 220.0 }) };
		Created.Add(ThermalType);
	}

	// --- LaserProcess ----------------------------------------------------
	UFactoryMachineArchetype* LaserType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_LaserProcess"), bForce);
	if (LaserType != nullptr)
	{
		LaserType->ArchetypeName = TEXT("LaserProcess");
		LaserType->Description = NSLOCTEXT("FactoryTwin", "LaserDesc",
			"Laser marking or cutting station with fume extraction. Has a distinct warmup "
			"where the source is powered but not yet at working output.");
		LaserType->StateModel = EFactoryStateModel::Automated;
		LaserType->DefaultTickIntervalSeconds = 0.5f;
		LaserType->WarmupSeconds = 8.0f;
		LaserType->CooldownSeconds = 5.0f;

		FFactoryMetricDefinition LaserTemp = MakeFloatMetric(TEXT("laser_temp_c"), TEXT("C"),
			{ 38.0, 52.0 }, { 20.0, 110.0 }, 28.0, 101.0);
		LaserTemp.bThermal = true;
		LaserTemp.bUseWarmupValue = true;
		LaserTemp.WarmupValue = 40.0;

		FFactoryMetricDefinition LaserPower = MakeFloatMetric(TEXT("laser_power_pct"), TEXT("%"),
			{ 55.0, 90.0 }, { 0.0, 100.0 }, 0.0, 0.0);
		LaserPower.bUseWarmupValue = true;
		LaserPower.WarmupValue = 20.0;

		FFactoryMetricDefinition Fume = MakeFloatMetric(TEXT("fume_extractor_rpm"), TEXT("RPM"),
			{ 2800.0, 3300.0 }, { 0.0, 3500.0 }, 0.0, 3500.0);
		Fume.bUseWarmupValue = true;
		Fume.WarmupValue = 1500.0;

		LaserType->Metrics = { LaserTemp, LaserPower, Fume,
			MakeCycleTimeMetric(TEXT("cycle_time_sec"), { 12.0, 17.0 }, { 10.0, 25.0 }) };
		Created.Add(LaserType);
	}

	// --- VisionInspection ------------------------------------------------
	// Shared by AOI and SPI. It owns the behaviour they have in common --
	// pass/fail rolling, fail counting, cycle timing -- while each placement
	// adds the measurements specific to what it inspects.
	UFactoryMachineArchetype* VisionType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_VisionInspection"), bForce);
	if (VisionType != nullptr)
	{
		VisionType->ArchetypeName = TEXT("VisionInspection");
		VisionType->Description = NSLOCTEXT("FactoryTwin", "VisionDesc",
			"Optical inspection station. Rolls a pass/fail result per cycle and tracks a "
			"failure count. Instances add the measurements they actually take: AOI reports "
			"component offset and solder quality, SPI reports paste area and volume.");
		VisionType->StateModel = EFactoryStateModel::Automated;
		VisionType->DefaultTickIntervalSeconds = 0.5f;
		VisionType->bIsInspectionStation = true;
		VisionType->DefaultFailRate = 0.02f;
		VisionType->Metrics = {
			MakeCycleTimeMetric(TEXT("cycle_time_sec"), { 2.0, 3.0 }, { 1.5, 5.0 }) };
		Created.Add(VisionType);
	}

	// --- ManualStation ---------------------------------------------------
	UFactoryMachineArchetype* ManualType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_ManualStation"), bForce);
	if (ManualType != nullptr)
	{
		ManualType->ArchetypeName = TEXT("ManualStation");
		ManualType->Description = NSLOCTEXT("FactoryTwin", "ManualDesc",
			"Operator-paced station such as the PCB cleaner or solder paste station. No warmup "
			"or cooldown ramp, longer and more variable cycles, and a reduced tag set: what a "
			"manual station can report is what the operator signals, not instrumentation.");
		ManualType->StateModel = EFactoryStateModel::Manual;
		ManualType->DefaultTickIntervalSeconds = 1.0f;
		ManualType->Metrics = {
			MakeCycleTimeMetric(TEXT("cycle_time_sec"), { 25.0, 90.0 }, { 10.0, 300.0 }) };
		Created.Add(ManualType);
	}

	// --- LineAggregate ---------------------------------------------------
	UFactoryMachineArchetype* LineType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_LineAggregate"), bForce);
	if (LineType != nullptr)
	{
		LineType->ArchetypeName = TEXT("LineAggregate");
		LineType->Description = NSLOCTEXT("FactoryTwin", "LineDesc",
			"Whole-line rollup. Event-driven only: it has no tick cadence and publishes takt "
			"time when a board completes the line.");
		LineType->StateModel = EFactoryStateModel::Automated;
		// Zero means event-only; the component skips scheduled publishing.
		LineType->DefaultTickIntervalSeconds = 0.0f;
		LineType->Metrics = {
			MakeCycleTimeMetric(TEXT("cycle_time"), { 125.0, 195.0 }, { 100.0, 240.0 }) };
		Created.Add(LineType);
	}

	// --- Controller ------------------------------------------------------
	UFactoryMachineArchetype* ControllerType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_Controller"), bForce);
	if (ControllerType != nullptr)
	{
		ControllerType->ArchetypeName = TEXT("Controller");
		ControllerType->Description = NSLOCTEXT("FactoryTwin", "ControllerDesc",
			"The line's control cabinet: a DIN-rail PLC and its I/O. Processes no material, so "
			"it reports its own health instead -- scan time, load, temperature, and whether its "
			"link to the broker is keeping up. Modelled on a groov EPIC.");

		// Conveyor, not Automated: a controller runs for as long as the line
		// runs and never cycles, which is exactly what that state model means.
		// Automated would leave it Idle and silent, because an idle machine
		// deliberately publishes nothing.
		ControllerType->StateModel = EFactoryStateModel::Conveyor;
		ControllerType->DefaultTickIntervalSeconds = 1.0f;

		// Scan time is the number a controls engineer looks at first: it is the
		// budget every rung has to fit inside, and it climbing is the earliest
		// sign the strategy is outgrowing the processor.
		FFactoryMetricDefinition ScanTime = MakeFloatMetric(TEXT("scan_time_ms"), TEXT("ms"),
			{ 1.5, 4.5 }, { 0.0, 30.0 }, 0.9, 28.0);

		FFactoryMetricDefinition ChassisTemp = MakeFloatMetric(TEXT("chassis_temp_c"), TEXT("C"),
			{ 34.0, 52.0 }, { 15.0, 90.0 }, 24.0, 86.0);
		ChassisTemp.bThermal = true;

		ControllerType->Metrics = {
			ScanTime,
			MakeFloatMetric(TEXT("cpu_load_pct"), TEXT("%"),
				{ 12.0, 38.0 }, { 0.0, 100.0 }, 4.0, 97.0),
			MakeFloatMetric(TEXT("memory_free_mb"), TEXT("MB"),
				{ 360.0, 470.0 }, { 0.0, 512.0 }, 496.0, 12.0),
			ChassisTemp,
			MakeIntMetric(TEXT("io_points_active"), TEXT(""),
				{ 18.0, 52.0 }, { 0.0, 64.0 }, 0.0),
			// The two that say whether the control link itself is healthy --
			// the ones worth a panel on the andon when a PLC is driving.
			MakeFloatMetric(TEXT("mqtt_publish_rate"), TEXT("msg/s"),
				{ 6.0, 24.0 }, { 0.0, 60.0 }, 0.0, 0.0),
			MakeFloatMetric(TEXT("network_latency_ms"), TEXT("ms"),
				{ 0.4, 3.5 }, { 0.0, 250.0 }, 0.3, 240.0),
		};
		Created.Add(ControllerType);
	}

	// =====================================================================
	// Instances
	//
	// Alias numbering must reproduce what the Python layer allocated: a single
	// global counter walking the machines in config order, each contributing its
	// own metrics and then the seven synthetic extras. The ClickHouse bridge is
	// mapped against these numbers, so the run 1-71 is fixed.
	// =====================================================================

	// Re-seeding incrementally skips archetypes that already exist, so pick up
	// the ones on disk before wiring instances to them.
	ConveyorType = Resolve(ConveyorType, TEXT("A_Conveyor"));
	PlacerType = Resolve(PlacerType, TEXT("A_PickAndPlace"));
	ThermalType = Resolve(ThermalType, TEXT("A_ThermalProcess"));
	LaserType = Resolve(LaserType, TEXT("A_LaserProcess"));
	VisionType = Resolve(VisionType, TEXT("A_VisionInspection"));
	ManualType = Resolve(ManualType, TEXT("A_ManualStation"));
	LineType = Resolve(LineType, TEXT("A_LineAggregate"));

	int64 NextAlias = 1;

	auto MakeInstance = [&](
		const FString& AssetName,
		UFactoryMachineArchetype* Archetype,
		const FString& DeviceId) -> UFactoryMachineInstance*
	{
		UFactoryMachineInstance* Instance =
			CreateAsset<UFactoryMachineInstance>(InstanceFolder, AssetName, bForce);
		if (Instance != nullptr)
		{
			Instance->Archetype = Archetype;

			// The existing device ids are kept verbatim as the work unit, odd
			// casing and all. They are what the downstream ClickHouse bridge is
			// mapped against, and this line's whole reason for pinning aliases
			// is not to disturb that mapping. Only the levels above the device
			// change, which is unavoidable: the group and node used to say
			// SMT_Line/Cluj while the UNS path said Essen, and one of the two
			// had to move.
			Instance->Isa95 = FFactoryIsa95Path{
				SmtSeed::Enterprise, SmtSeed::Site, SmtSeed::Area,
				SmtSeed::WorkCentre, DeviceId };
		}
		return Instance;
	};

	// Conveyor: aliases 1-4 then extras 5-11.
	if (UFactoryMachineInstance* Instance = MakeInstance(
		TEXT("I_Conveyor"), ConveyorType, TEXT("Conveyor")))
	{
		for (int32 Index = 1; Index <= 8; ++Index)
		{
			Instance->CarrierIds.Add(FString::Printf(TEXT("CC_%02d"), Index));
		}
		Instance->CarrierDwellSeconds = 12.0f;
		Instance->LayoutPosition = FVector2D(0.0, 0.0);
		Instance->LayoutFootprint = FVector2D(8.0, 0.6);
		NextAlias = PinAliases(Instance, NextAlias, { TEXT("belt_speed"), TEXT("motor_temp"), TEXT("rpm"), TEXT("torque") });
		Created.Add(Instance);
	}

	// COMPONENT_PLACER: 12-15 then extras 16-22.
	if (UFactoryMachineInstance* Instance = MakeInstance(
		TEXT("I_ComponentPlacer"), PlacerType, TEXT("COMPONENT_PLACER")))
	{
		Instance->LayoutPosition = FVector2D(4.0, 0.0);
		Instance->LayoutFootprint = FVector2D(2.0, 1.6);
		NextAlias = PinAliases(Instance, NextAlias, { TEXT("arm_pos_x"), TEXT("arm_pos_y"), TEXT("arm_pos_z"), TEXT("cycle_time") });
		Created.Add(Instance);
	}

	// REFLOW_OVEN: 23-25 then extras 26-32.
	if (UFactoryMachineInstance* Instance = MakeInstance(
		TEXT("I_ReflowOven"), ThermalType, TEXT("REFLOW_OVEN")))
	{
		Instance->LayoutPosition = FVector2D(8.0, 0.0);
		Instance->LayoutFootprint = FVector2D(4.0, 1.4);
		NextAlias = PinAliases(Instance, NextAlias, { TEXT("oven_temp_c"), TEXT("cooling_temp_c"), TEXT("cycle_time_sec") });
		Created.Add(Instance);
	}

	// LASER_MARKING: 33-36 then extras 37-43.
	if (UFactoryMachineInstance* Instance = MakeInstance(
		TEXT("I_LaserMarking"), LaserType, TEXT("LASER_MARKING")))
	{
		Instance->PartIds = {
			TEXT("ACC-Inno-1"), TEXT("ACC-Inno-2"), TEXT("ACC-Inno-3"),
			TEXT("ACC-Inno-4"), TEXT("ACC-Inno-5"), TEXT("ABC-FG") };
		Instance->LayoutPosition = FVector2D(2.0, 0.0);
		Instance->LayoutFootprint = FVector2D(1.5, 1.2);
		NextAlias = PinAliases(Instance, NextAlias, { TEXT("laser_temp_c"), TEXT("laser_power_pct"), TEXT("fume_extractor_rpm"), TEXT("cycle_time_sec") });
		Created.Add(Instance);
	}

	// AUTO_OPTICALINSP: 44-46 then extras 47-53.
	// Adds its own measurements on top of the shared VisionInspection archetype.
	if (UFactoryMachineInstance* Instance = MakeInstance(
		TEXT("I_AutoOpticalInspection"), VisionType, TEXT("AUTO_OPTICALINSP")))
	{
		Instance->AdditionalMetrics = {
			MakeFloatMetric(TEXT("comp_position_offset_mm"), TEXT("mm"),
				{ 0.0, 0.3 }, { 0.0, 1.0 }, 0.0, 0.85),
			MakeFloatMetric(TEXT("solder_quality_score"), TEXT("0-100"),
				{ 91.0, 96.0 }, { 0.0, 100.0 }, 0.0, 75.0),
		};
		Instance->LayoutPosition = FVector2D(12.0, 0.0);
		Instance->LayoutFootprint = FVector2D(1.5, 1.2);
		NextAlias = PinAliases(Instance, NextAlias, { TEXT("comp_position_offset_mm"), TEXT("solder_quality_score"), TEXT("cycle_time_sec") });
		Created.Add(Instance);
	}

	// SOLDER_INSP: 54-56 then extras 57-63.
	if (UFactoryMachineInstance* Instance = MakeInstance(
		TEXT("I_SolderInspection"), VisionType, TEXT("SOLDER_INSP")))
	{
		Instance->AdditionalMetrics = {
			MakeFloatMetric(TEXT("area"), TEXT("mm2"),
				{ 84.0, 86.0 }, { 0.0, 95.0 }, 0.0, 78.0),
			MakeFloatMetric(TEXT("volume"), TEXT(""),
				{ 8.0, 13.0 }, { 0.0, 20.0 }, 0.0, 5.0),
		};
		Instance->LayoutPosition = FVector2D(6.0, 0.0);
		Instance->LayoutFootprint = FVector2D(1.5, 1.2);
		NextAlias = PinAliases(Instance, NextAlias, { TEXT("area"), TEXT("volume"), TEXT("cycle_time_sec") });
		Created.Add(Instance);
	}

	// SMT_LINE: 64 then extras 65-71.
	if (UFactoryMachineInstance* Instance = MakeInstance(
		TEXT("I_SmtLine"), LineType, TEXT("SMT_LINE")))
	{
		Instance->LayoutPosition = FVector2D(0.0, 0.0);
		Instance->LayoutFootprint = FVector2D(16.0, 3.0);
		NextAlias = PinAliases(Instance, NextAlias, { TEXT("cycle_time") });
		Created.Add(Instance);
	}

	// =====================================================================
	// Manual stations. These have no entry in factory_config.toml because the
	// Python layer never instrumented them, so they take fresh aliases after
	// the existing run rather than disturbing 1-71.
	// =====================================================================

	if (UFactoryMachineInstance* Instance = MakeInstance(
		TEXT("I_PcbCleaner"), ManualType, TEXT("PCB_CLEANER")))
	{
		Instance->LayoutPosition = FVector2D(14.0, 0.0);
		Instance->LayoutFootprint = FVector2D(1.2, 1.0);
		NextAlias = PinAliases(Instance, NextAlias, { TEXT("cycle_time_sec") });
		Created.Add(Instance);
	}

	if (UFactoryMachineInstance* Instance = MakeInstance(
		TEXT("I_SolderPasteStation"), ManualType, TEXT("SOLDER_PASTE_STATION")))
	{
		Instance->LayoutPosition = FVector2D(5.0, 0.0);
		Instance->LayoutFootprint = FVector2D(1.8, 1.2);
		NextAlias = PinAliases(Instance, NextAlias, { TEXT("cycle_time_sec") });
		Created.Add(Instance);
	}

	// =====================================================================

	int32 SavedCount = 0;
	for (UObject* Asset : Created)
	{
		if (SaveAsset(Asset))
		{
			++SavedCount;
		}
	}

	UE_LOG(LogFactorySim, Display,
		TEXT("Seed complete: %d/%d assets written, aliases 1-%lld allocated"),
		SavedCount, Created.Num(), NextAlias - 1);

	return SavedCount == Created.Num() ? 0 : 1;
}
