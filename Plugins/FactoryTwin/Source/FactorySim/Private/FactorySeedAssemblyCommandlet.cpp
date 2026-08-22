#include "FactorySeedAssemblyCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "FactoryMachineArchetype.h"
#include "FactoryMachineInstance.h"
#include "FactorySimTypes.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace AssemblySeed
{
	const FString ArchetypeFolder = TEXT("/Game/FactoryTwin/Archetypes");
	const FString InstanceFolder = TEXT("/Game/FactoryTwin/Instances");

	/** Final Assembly aliases begin here, clear of the SMT line's 1-87. */
	constexpr int64 AssemblyAliasBase = 100;

	FFactoryMetricDefinition MakeFloat(
		const FString& Name,
		const FString& Unit,
		const FFactoryRange& Nominal,
		const FFactoryRange& Absolute,
		const double IdleValue,
		const double FaultValue)
	{
		FFactoryMetricDefinition D;
		D.Name = Name;
		D.DataType = ESparkplugDataType::Float;
		D.Unit = Unit;
		D.Nominal = Nominal;
		D.Absolute = Absolute;
		D.IdleValue = IdleValue;
		D.FaultValue = FaultValue;
		return D;
	}

	FFactoryMetricDefinition MakeInt(
		const FString& Name,
		const FString& Unit,
		const FFactoryRange& Nominal,
		const FFactoryRange& Absolute,
		const double IdleValue)
	{
		FFactoryMetricDefinition D = MakeFloat(Name, Unit, Nominal, Absolute, IdleValue, IdleValue);
		D.DataType = ESparkplugDataType::Int32;
		return D;
	}

	FFactoryMetricDefinition MakeCycleTime(
		const FString& Name, const FFactoryRange& Nominal, const FFactoryRange& Absolute)
	{
		FFactoryMetricDefinition D = MakeFloat(Name, TEXT("s"), Nominal, Absolute, 0.0, 0.0);
		D.PublishOn = EFactoryPublishTrigger::CycleComplete;
		return D;
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
			return nullptr;
		}

		if (bExists)
		{
			// NewObject cannot claim a name the loaded asset still holds.
			Package->FullyLoad();
			if (UObject* Existing = StaticFindObject(nullptr, Package, *AssetName))
			{
				Existing->Rename(
					nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}

		return NewObject<AssetType>(
			Package, AssetType::StaticClass(), *AssetName, RF_Public | RF_Standalone);
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

	int64 PinAliases(
		UFactoryMachineInstance* Instance, const int64 Start, const TArray<FString>& OrderedNames)
	{
		int64 Next = Start;
		for (const FString& Name : OrderedNames)
		{
			Instance->MetricAliases.Add(Name, Next++);
		}
		for (const FString& Name : FactorySyntheticMetrics::GetAll())
		{
			Instance->MetricAliases.Add(Name, Next++);
		}

		for (const FString& Name : Instance->GetAllMetricNames())
		{
			if (!Instance->MetricAliases.Contains(Name))
			{
				UE_LOG(LogFactorySim, Error,
					TEXT("%s emits '%s' with no pinned alias"), *Instance->DeviceId, *Name);
			}
		}

		UE_LOG(LogFactorySim, Display, TEXT("  %-24s aliases %lld-%lld"),
			*Instance->DeviceId, Start, Next - 1);
		return Next;
	}
}

using namespace AssemblySeed;

UFactorySeedAssemblyCommandlet::UFactorySeedAssemblyCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UFactorySeedAssemblyCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);
	const bool bForce = Switches.Contains(TEXT("Force"));

	UE_LOG(LogFactorySim, Display, TEXT("Seeding Final Assembly archetypes..."));

	TArray<UObject*> Created;

	// =====================================================================
	// Archetypes
	// =====================================================================

	// --- PressInsertion --------------------------------------------------
	UFactoryMachineArchetype* PressType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_PressInsertion"), bForce);
	if (PressType != nullptr)
	{
		PressType->ArchetypeName = TEXT("PressInsertion");
		PressType->Description = NSLOCTEXT("FactoryTwin", "PressDesc",
			"Servo press for press-fit pins and connectors. Force and depth are the "
			"process signature: a pin that reads low force at full depth was not gripped, "
			"and one that reads high force short of depth hit an obstruction.");
		PressType->StateModel = EFactoryStateModel::Automated;
		PressType->DefaultTickIntervalSeconds = 0.2f;

		FFactoryMetricDefinition Force = MakeFloat(TEXT("insertion_force_n"), TEXT("N"),
			{ 180.0, 260.0 }, { 0.0, 400.0 }, 0.0, 385.0);
		// Force builds through the stroke rather than sitting at a level.
		Force.MotionProfile = EFactoryMotionProfile::Sawtooth;
		Force.MotionPeriodSeconds = 1.5;

		FFactoryMetricDefinition Depth = MakeFloat(TEXT("insertion_depth_mm"), TEXT("mm"),
			{ 3.8, 4.2 }, { 0.0, 6.0 }, 0.0, 1.5);
		Depth.MotionProfile = EFactoryMotionProfile::Sawtooth;
		Depth.MotionPeriodSeconds = 1.5;

		PressType->Metrics = { Force, Depth,
			MakeCycleTime(TEXT("cycle_time_sec"), { 1.2, 2.4 }, { 0.5, 6.0 }) };
		Created.Add(PressType);
	}

	// --- ElectricalTest --------------------------------------------------
	UFactoryMachineArchetype* IctType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_ElectricalTest"), bForce);
	if (IctType != nullptr)
	{
		IctType->ArchetypeName = TEXT("ElectricalTest");
		IctType->Description = NSLOCTEXT("FactoryTwin", "IctDesc",
			"In-circuit test on a bed of nails. Reports rail voltage, draw and insulation "
			"resistance, and a pass/fail verdict per board.");
		IctType->StateModel = EFactoryStateModel::Automated;
		IctType->DefaultTickIntervalSeconds = 0.5f;
		IctType->bIsInspectionStation = true;
		IctType->DefaultFailRate = 0.03f;
		IctType->Metrics = {
			MakeFloat(TEXT("test_voltage_v"), TEXT("V"),
				{ 11.8, 12.2 }, { 0.0, 15.0 }, 0.0, 8.5),
			MakeFloat(TEXT("test_current_ma"), TEXT("mA"),
				{ 120.0, 180.0 }, { 0.0, 500.0 }, 0.0, 470.0),
			MakeFloat(TEXT("insulation_mohm"), TEXT("MOhm"),
				{ 45.0, 90.0 }, { 0.0, 120.0 }, 0.0, 2.0),
			MakeCycleTime(TEXT("cycle_time_sec"), { 8.0, 14.0 }, { 4.0, 30.0 }) };
		Created.Add(IctType);
	}

	// --- Programming -----------------------------------------------------
	UFactoryMachineArchetype* FlashType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_Programming"), bForce);
	if (FlashType != nullptr)
	{
		FlashType->ArchetypeName = TEXT("Programming");
		FlashType->Description = NSLOCTEXT("FactoryTwin", "FlashDesc",
			"Firmware flashing and verification. Throughput is the useful signal: a slow "
			"write usually means a marginal connection rather than a slow part.");
		FlashType->StateModel = EFactoryStateModel::Automated;
		FlashType->DefaultTickIntervalSeconds = 0.5f;
		FlashType->bIsInspectionStation = true;
		FlashType->DefaultFailRate = 0.01f;
		FlashType->Metrics = {
			MakeFloat(TEXT("flash_throughput_kbs"), TEXT("kB/s"),
				{ 480.0, 620.0 }, { 0.0, 800.0 }, 0.0, 40.0),
			MakeInt(TEXT("flash_bytes"), TEXT("B"),
				{ 262144.0, 262144.0 }, { 0.0, 1048576.0 }, 0.0),
			MakeCycleTime(TEXT("cycle_time_sec"), { 4.0, 7.0 }, { 2.0, 20.0 }) };
		Created.Add(FlashType);
	}

	// --- FunctionalTest --------------------------------------------------
	UFactoryMachineArchetype* EolType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_FunctionalTest"), bForce);
	if (EolType != nullptr)
	{
		EolType->ArchetypeName = TEXT("FunctionalTest");
		EolType->Description = NSLOCTEXT("FactoryTwin", "EolDesc",
			"End-of-line functional test: powers the finished unit and exercises it as the "
			"customer would. The last gate before packing, so its fail rate is the one that "
			"reaches the customer if it drifts.");
		EolType->StateModel = EFactoryStateModel::Automated;
		EolType->DefaultTickIntervalSeconds = 0.5f;
		EolType->bIsInspectionStation = true;
		EolType->DefaultFailRate = 0.015f;
		EolType->Metrics = {
			MakeFloat(TEXT("supply_current_ma"), TEXT("mA"),
				{ 90.0, 140.0 }, { 0.0, 400.0 }, 0.0, 380.0),
			MakeFloat(TEXT("boot_time_ms"), TEXT("ms"),
				{ 380.0, 520.0 }, { 0.0, 3000.0 }, 0.0, 2900.0),
			MakeCycleTime(TEXT("cycle_time_sec"), { 10.0, 18.0 }, { 5.0, 40.0 }) };
		Created.Add(EolType);
	}

	// --- RoboticArm ------------------------------------------------------
	UFactoryMachineArchetype* RobotType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_RoboticArm"), bForce);
	if (RobotType != nullptr)
	{
		RobotType->ArchetypeName = TEXT("RoboticArm");
		RobotType->Description = NSLOCTEXT("FactoryTwin", "RobotDesc",
			"Articulated handling robot. Pairs with the rigged UR5e and Fanuc assets already "
			"in the project, so the published joint load and TCP speed describe the same "
			"motion the animation is playing.");
		RobotType->StateModel = EFactoryStateModel::Automated;
		RobotType->DefaultTickIntervalSeconds = 0.1f;

		FFactoryMetricDefinition TcpSpeed = MakeFloat(TEXT("tcp_speed_mms"), TEXT("mm/s"),
			{ 120.0, 900.0 }, { 0.0, 1500.0 }, 0.0, 0.0);
		TcpSpeed.MotionProfile = EFactoryMotionProfile::Sine;
		TcpSpeed.MotionPeriodSeconds = 4.0;

		FFactoryMetricDefinition JointLoad = MakeFloat(TEXT("joint_load_pct"), TEXT("%"),
			{ 18.0, 62.0 }, { 0.0, 100.0 }, 0.0, 98.0);
		JointLoad.MotionProfile = EFactoryMotionProfile::Sine;
		JointLoad.MotionPeriodSeconds = 2.5;

		RobotType->Metrics = { TcpSpeed, JointLoad,
			MakeFloat(TEXT("payload_kg"), TEXT("kg"),
				{ 0.4, 1.8 }, { 0.0, 5.0 }, 0.0, 0.0),
			MakeCycleTime(TEXT("cycle_time_sec"), { 3.0, 8.0 }, { 1.0, 20.0 }) };
		Created.Add(RobotType);
	}

	// --- Packaging -------------------------------------------------------
	UFactoryMachineArchetype* PackType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_Packaging"), bForce);
	if (PackType != nullptr)
	{
		PackType->ArchetypeName = TEXT("Packaging");
		PackType->Description = NSLOCTEXT("FactoryTwin", "PackDesc",
			"Cartoning and sealing. Units-per-carton is a running count rather than a "
			"measurement, so it is an integer tag.");
		PackType->StateModel = EFactoryStateModel::Automated;
		PackType->DefaultTickIntervalSeconds = 1.0f;
		PackType->Metrics = {
			MakeInt(TEXT("units_in_carton"), TEXT("units"),
				{ 0.0, 24.0 }, { 0.0, 24.0 }, 0.0),
			MakeFloat(TEXT("seal_temp_c"), TEXT("C"),
				{ 140.0, 165.0 }, { 20.0, 200.0 }, 22.0, 195.0),
			MakeCycleTime(TEXT("cycle_time_sec"), { 6.0, 11.0 }, { 3.0, 25.0 }) };
		// Sealing bar is thermal, so it should ease rather than step.
		PackType->Metrics[1].bThermal = true;
		PackType->WarmupSeconds = 15.0f;
		PackType->CooldownSeconds = 12.0f;
		Created.Add(PackType);
	}

	// --- Buffer ----------------------------------------------------------
	UFactoryMachineArchetype* BufferType =
		CreateAsset<UFactoryMachineArchetype>(ArchetypeFolder, TEXT("A_Buffer"), bForce);
	if (BufferType != nullptr)
	{
		BufferType->ArchetypeName = TEXT("Buffer");
		BufferType->Description = NSLOCTEXT("FactoryTwin", "BufferDesc",
			"Accumulation buffer between stations. Occupancy is the interesting tag: a "
			"buffer sitting full marks the station downstream of it as the constraint.");
		BufferType->StateModel = EFactoryStateModel::Buffer;
		BufferType->DefaultTickIntervalSeconds = 1.0f;
		BufferType->Metrics = {
			MakeInt(TEXT("occupancy"), TEXT("units"), { 0.0, 10.0 }, { 0.0, 20.0 }, 0.0),
			MakeFloat(TEXT("fill_pct"), TEXT("%"), { 0.0, 60.0 }, { 0.0, 100.0 }, 0.0, 100.0) };
		Created.Add(BufferType);
	}

	// =====================================================================
	// Instances: the Final Assembly line, in process order.
	// =====================================================================

	// Re-seeding incrementally skips archetypes that already exist, so pick up
	// the ones on disk before wiring instances to them.
	PressType = Resolve(PressType, TEXT("A_PressInsertion"));
	IctType = Resolve(IctType, TEXT("A_ElectricalTest"));
	FlashType = Resolve(FlashType, TEXT("A_Programming"));
	EolType = Resolve(EolType, TEXT("A_FunctionalTest"));
	RobotType = Resolve(RobotType, TEXT("A_RoboticArm"));
	PackType = Resolve(PackType, TEXT("A_Packaging"));
	BufferType = Resolve(BufferType, TEXT("A_Buffer"));

	int64 NextAlias = AssemblyAliasBase;

	auto Make = [&](const FString& AssetName,
					UFactoryMachineArchetype* Archetype,
					const FString& DeviceId,
					const FString& UnsLeaf,
					const FVector2D& Position,
					const FVector2D& Footprint) -> UFactoryMachineInstance*
	{
		UFactoryMachineInstance* Instance =
			CreateAsset<UFactoryMachineInstance>(InstanceFolder, AssetName, bForce);
		if (Instance != nullptr)
		{
			Instance->Archetype = Archetype;
			Instance->DeviceId = DeviceId;
			Instance->UnsPath = FString::Printf(TEXT("Essen/Cluj/Assembly/Line1/%s"), *UnsLeaf);
			Instance->LayoutPosition = Position;
			Instance->LayoutFootprint = Footprint;
		}
		return Instance;
	};

	if (UFactoryMachineInstance* I = Make(TEXT("I_HousingAssembly"), nullptr,
		TEXT("HOUSING_ASSEMBLY"), TEXT("HousingAssembly"), { 0.0, 0.0 }, { 2.0, 1.5 }))
	{
		// Housing fit is an operator task on this line.
		I->Archetype = LoadObject<UFactoryMachineArchetype>(
			nullptr, TEXT("/Game/FactoryTwin/Archetypes/A_ManualStation.A_ManualStation"));

		// The shared manual archetype is timed for the SMT line, where a manual
		// step is a rework bench at 25-90 s. Here the same archetype is the first
		// station on a flowing line, so that range throttles everything behind it
		// -- the line ran at one unit a minute and stood empty. An operator
		// fitting a housing into a carrier is a 10-second job, and overriding the
		// band rather than the dwell keeps the reported cycle time honest.
		I->NominalOverrides.Add(TEXT("cycle_time_sec"), FFactoryRange(8.0, 14.0));
		NextAlias = PinAliases(I, NextAlias, { TEXT("cycle_time_sec") });
		Created.Add(I);
	}

	if (UFactoryMachineInstance* I = Make(TEXT("I_PinInsertion"), PressType,
		TEXT("PIN_INSERTION"), TEXT("PinInsertion"), { 2.5, 0.0 }, { 1.6, 1.4 }))
	{
		NextAlias = PinAliases(I, NextAlias,
			{ TEXT("insertion_force_n"), TEXT("insertion_depth_mm"), TEXT("cycle_time_sec") });
		Created.Add(I);
	}

	if (UFactoryMachineInstance* I = Make(TEXT("I_AssemblyRobot"), RobotType,
		TEXT("ASSEMBLY_ROBOT"), TEXT("HandlingRobot"), { 5.0, 1.5 }, { 1.2, 1.2 }))
	{
		NextAlias = PinAliases(I, NextAlias,
			{ TEXT("tcp_speed_mms"), TEXT("joint_load_pct"), TEXT("payload_kg"),
			  TEXT("cycle_time_sec") });
		Created.Add(I);
	}

	if (UFactoryMachineInstance* I = Make(TEXT("I_InCircuitTest"), IctType,
		TEXT("ICT"), TEXT("ICT"), { 7.5, 0.0 }, { 1.8, 1.4 }))
	{
		NextAlias = PinAliases(I, NextAlias,
			{ TEXT("test_voltage_v"), TEXT("test_current_ma"), TEXT("insulation_mohm"),
			  TEXT("cycle_time_sec") });
		Created.Add(I);
	}

	if (UFactoryMachineInstance* I = Make(TEXT("I_FlashProgramming"), FlashType,
		TEXT("FLASH_PROGRAMMING"), TEXT("FlashProgramming"), { 10.0, 0.0 }, { 1.4, 1.2 }))
	{
		NextAlias = PinAliases(I, NextAlias,
			{ TEXT("flash_throughput_kbs"), TEXT("flash_bytes"), TEXT("cycle_time_sec") });
		Created.Add(I);
	}

	if (UFactoryMachineInstance* I = Make(TEXT("I_AssemblyBuffer"), BufferType,
		TEXT("ASSEMBLY_BUFFER"), TEXT("Buffer"), { 12.5, 0.0 }, { 2.4, 0.8 }))
	{
		NextAlias = PinAliases(I, NextAlias, { TEXT("occupancy"), TEXT("fill_pct") });
		Created.Add(I);
	}

	if (UFactoryMachineInstance* I = Make(TEXT("I_EndOfLineTest"), EolType,
		TEXT("EOL_TEST"), TEXT("EndOfLineTest"), { 17.5, 0.0 }, { 1.8, 1.4 }))
	{
		NextAlias = PinAliases(I, NextAlias,
			{ TEXT("supply_current_ma"), TEXT("boot_time_ms"), TEXT("cycle_time_sec") });
		Created.Add(I);
	}

	if (UFactoryMachineInstance* I = Make(TEXT("I_Packaging"), PackType,
		TEXT("PACKAGING"), TEXT("Packaging"), { 20.0, 0.0 }, { 2.2, 1.6 }))
	{
		NextAlias = PinAliases(I, NextAlias,
			{ TEXT("units_in_carton"), TEXT("seal_temp_c"), TEXT("cycle_time_sec") });
		Created.Add(I);
	}

	// A second arm, this one built from the imported KUKA KR10 link meshes.
	// Same RoboticArm archetype as the UR5 handler, which is the point: one
	// archetype, two physically different robots.
	if (UFactoryMachineInstance* I = Make(TEXT("I_KukaHandler"), RobotType,
		TEXT("KUKA_HANDLER"), TEXT("KukaHandler"), { 15.0, 2.0 }, { 1.1, 1.1 }))
	{
		NextAlias = PinAliases(I, NextAlias,
			{ TEXT("tcp_speed_mms"), TEXT("joint_load_pct"), TEXT("payload_kg"),
			  TEXT("cycle_time_sec") });
		Created.Add(I);
	}

	int32 Saved = 0;
	for (UObject* Asset : Created)
	{
		if (SaveAsset(Asset))
		{
			++Saved;
		}
	}

	UE_LOG(LogFactorySim, Display,
		TEXT("Assembly seed complete: %d/%d assets, aliases %lld-%lld"),
		Saved, Created.Num(), AssemblyAliasBase, NextAlias - 1);

	return Saved == Created.Num() ? 0 : 1;
}
