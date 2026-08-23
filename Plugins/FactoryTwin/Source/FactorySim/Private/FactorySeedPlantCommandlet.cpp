#include "FactorySeedPlantCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "FactoryLayoutGrid.h"
#include "FactoryMachineArchetype.h"
#include "FactoryMachineInstance.h"
#include "FactorySimTypes.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace PlantSeed
{
	const FString ArchetypeFolder = TEXT("/Game/FactoryTwin/Archetypes");
	const FString InstanceFolder = TEXT("/Game/FactoryTwin/Instances/Plant");

	/**
	 * Where the plant's aliases start, and how far apart the lines are spaced.
	 *
	 * Well clear of the SMT line (1-99) and the single assembly line (100-234),
	 * and a whole block per line so adding a station to one line cannot renumber
	 * the next one.
	 */
	constexpr int64 PlantAliasBase = 1000;
	constexpr int64 AliasesPerLine = 200;

	/**
	 * ISA-95 levels above the line.
	 *
	 * InnoLab is the enterprise, Essen the site it operates. Every line in this
	 * hall is one work centre inside a single SMT area: the stations that do
	 * final assembly are still part of the same physical cell, and splitting
	 * them into their own area would put half of each line on a separate edge
	 * node for no gain.
	 */
	const FString Enterprise = TEXT("InnoLab");
	const FString Site       = TEXT("Essen");
	const FString PlantArea  = TEXT("SMT");

	/**
	 * One station in the template every line is built from.
	 *
	 * Widths are the measured footprint of the station's own geometry where that
	 * has been established, and an estimate elsewhere; the level build prints
	 * measured against declared for every station so the estimates can be
	 * corrected from what the meshes actually are.
	 */
	struct FStationTemplate
	{
		/**
		 * ISA-95 work unit, the Sparkplug device, and the asset name suffix.
		 *
		 * Spelled the way the original SMT line spells its devices, because
		 * level2's line and this plant's line 1 are the same work centre --
		 * InnoLab/Essen/SMT/Line1 -- and a consumer subscribed to it should not
		 * see REFLOW_OVEN from one level and ReflowOven from the other.
		 */
		const TCHAR* Device;
		const TCHAR* Archetype;
		double WidthMetres;
		double DepthMetres;
		/** Set back from the belt, serving it from the side. */
		double SideOffsetMetres;
	};

	// A board enters at the loader, leaves the far end packed. SMT first, then
	// final assembly: one line is a whole product rather than half of one.
	const FStationTemplate Template[] = {
		// --- SMT
		{ TEXT("LOADER"),            TEXT("A_Buffer"),          1.61, 1.15, 0.0 },
		{ TEXT("LASER_MARKING"),     TEXT("A_LaserProcess"),    1.50, 1.20, 0.0 },
		{ TEXT("SOLDER_PASTE"),      TEXT("A_ManualStation"),   1.80, 1.20, 0.0 },
		{ TEXT("SOLDER_INSP"),       TEXT("A_VisionInspection"),1.60, 1.20, 0.0 },
		{ TEXT("COMPONENT_PLACER"),  TEXT("A_PickAndPlace"),    2.20, 1.40, 0.0 },
		{ TEXT("REFLOW_OVEN"),       TEXT("A_ThermalProcess"),  3.00, 1.40, 0.0 },
		{ TEXT("AUTO_OPTICALINSP"),  TEXT("A_VisionInspection"),1.60, 1.20, 0.0 },
		{ TEXT("PCB_CLEANER"),       TEXT("A_ManualStation"),   1.60, 1.20, 0.0 },
		// --- final assembly
		{ TEXT("HOUSING_ASSEMBLY"),  TEXT("A_OperatorBench"),   1.48, 0.99, 0.0 },
		{ TEXT("PIN_INSERTION"),     TEXT("A_PressInsertion"),  1.48, 0.99, 0.0 },
		{ TEXT("PIN_INSPECTION"),    TEXT("A_OperatorBench"),   2.10, 0.99, 0.0 },
		{ TEXT("ASSEMBLY_ROBOT"),    TEXT("A_RoboticArm"),      1.38, 0.61, 1.5 },
		{ TEXT("ICT"),               TEXT("A_ElectricalTest"),  2.10, 0.99, 0.0 },
		{ TEXT("FLASH_PROGRAMMING"), TEXT("A_Programming"),     1.19, 0.99, 0.0 },
		{ TEXT("PIN_CHECK"),         TEXT("A_VisionInspection"),1.19, 0.99, 0.0 },
		{ TEXT("EOL_TEST"),          TEXT("A_FunctionalTest"),  1.43, 1.78, 0.0 },
		{ TEXT("PACKAGING"),         TEXT("A_Packaging"),       1.61, 1.15, 0.0 },
	};

	/** The transport, which is one device for the whole line. */
	const TCHAR* ConveyorDevice = TEXT("CONVEYOR");

	/** Every metric name an archetype can emit, so none goes out unmapped. */
	TArray<FString> MetricNamesOf(UFactoryMachineArchetype* Archetype)
	{
		TArray<FString> Names;
		if (Archetype != nullptr)
		{
			for (const FFactoryMetricDefinition& Definition : Archetype->Metrics)
			{
				Names.Add(Definition.Name);
			}
		}
		return Names;
	}

	template <typename T>
	T* CreateAsset(const FString& Folder, const FString& AssetName, const bool bForce)
	{
		const FString PackageName = Folder / AssetName;
		if (!bForce && FPackageName::DoesPackageExist(PackageName))
		{
			return LoadObject<T>(nullptr, *(PackageName + TEXT(".") + AssetName));
		}

		UPackage* Package = CreatePackage(*PackageName);
		if (Package == nullptr)
		{
			return nullptr;
		}
		Package->FullyLoad();

		// NewObject cannot claim a name something else still holds, and silently
		// returns null if it tries -- so move any incumbent out of the way.
		if (UObject* Existing = StaticFindObject(nullptr, Package, *AssetName))
		{
			Existing->Rename(nullptr, GetTransientPackage(),
				REN_DontCreateRedirectors | REN_DoNotDirty);
		}

		T* Asset = NewObject<T>(Package, T::StaticClass(), FName(*AssetName),
			RF_Public | RF_Standalone);
		if (Asset != nullptr)
		{
			FAssetRegistryModule::AssetCreated(Asset);
			Package->MarkPackageDirty();
		}
		return Asset;
	}

	bool SaveAsset(UObject* Asset)
	{
		if (Asset == nullptr)
		{
			return false;
		}
		UPackage* Package = Asset->GetOutermost();
		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *FileName, Args);
	}
}

using namespace PlantSeed;

UFactorySeedPlantCommandlet::UFactorySeedPlantCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 UFactorySeedPlantCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamMap);

	const bool bForce = Switches.Contains(TEXT("Force"));
	int32 Lines = 3;
	if (ParamMap.Contains(TEXT("Lines")))
	{
		Lines = FMath::Clamp(FCString::Atoi(*ParamMap[TEXT("Lines")]), 1, 8);
	}

	UE_LOG(LogFactorySim, Display, TEXT("Seeding a plant of %d line(s) under %s/%s/%s"),
		Lines, *Enterprise, *Site, *PlantArea);

	TArray<UObject*> Created;

	for (int32 Line = 1; Line <= Lines; ++Line)
	{
		int64 NextAlias = PlantAliasBase + (Line - 1) * AliasesPerLine;

		// Machines are packed nose to tail; only the ones set back from the belt
		// are skipped, because they stand beside the line rather than in it.
		double Cursor = 0.0;

		for (const FStationTemplate& Station : Template)
		{
			const FString AssetName = FString::Printf(TEXT("I_L%d_%s"), Line, Station.Device);

			UFactoryMachineInstance* Instance =
				CreateAsset<UFactoryMachineInstance>(InstanceFolder, AssetName, bForce);
			if (Instance == nullptr)
			{
				continue;
			}

			UFactoryMachineArchetype* Archetype = LoadObject<UFactoryMachineArchetype>(
				nullptr, *FString::Printf(TEXT("%s/%s.%s"), *ArchetypeFolder,
					Station.Archetype, Station.Archetype));
			if (Archetype == nullptr)
			{
				UE_LOG(LogFactorySim, Warning,
					TEXT("  archetype '%s' missing; run the SMT and assembly seeds first"),
					Station.Archetype);
			}

			Instance->Archetype = Archetype;

			// Identity is the ISA-95 path and nothing else; the device id and
			// the UNS path both fall out of it, so they cannot disagree. The
			// work unit is the bare station name -- the line is already carried
			// by the work centre, and repeating it as "L1_LOADER" would put the
			// same fact in the topic twice.
			Instance->Isa95 = FFactoryIsa95Path{
				Enterprise, Site, PlantArea,
				FString::Printf(TEXT("Line%d"), Line),
				Station.Device };

			Instance->LayoutFootprint = FVector2D(Station.WidthMetres, Station.DepthMetres);

			double X = Cursor + Station.WidthMetres * 0.5;
			if (Station.SideOffsetMetres > 0.0)
			{
				// Stands beside the belt, so it takes no space along it.
				X = Cursor;
			}
			else
			{
				Cursor += Station.WidthMetres;
			}
			Instance->LayoutPosition = FVector2D(X, Station.SideOffsetMetres);

			// A bench worked by a person runs at a human pace; the shared manual
			// archetype is timed for a rework bench and would throttle the line.
			if (FCString::Strcmp(Station.Archetype, TEXT("A_OperatorBench")) == 0)
			{
				Instance->NominalOverrides.Add(TEXT("cycle_time_sec"), FFactoryRange(8.0, 14.0));
			}

			// Every name the machine can emit, archetype metrics and the
			// synthetic extras alike, gets a number.
			Instance->MetricAliases.Empty();
			for (const FString& Name : Instance->GetAllMetricNames())
			{
				Instance->MetricAliases.Add(Name, NextAlias++);
			}

			Created.Add(Instance);
		}

		// The line's transport.
		const FString ConveyorAsset = FString::Printf(TEXT("I_L%d_%s"), Line, ConveyorDevice);
		if (UFactoryMachineInstance* Conveyor =
			CreateAsset<UFactoryMachineInstance>(InstanceFolder, ConveyorAsset, bForce))
		{
			Conveyor->Archetype = LoadObject<UFactoryMachineArchetype>(
				nullptr, TEXT("/Game/FactoryTwin/Archetypes/A_Conveyor.A_Conveyor"));
			// The belt belongs to the line it serves, not to a separate
			// "Transport" area: putting it elsewhere in the hierarchy would move
			// it onto its own edge node, so a line going down would leave its
			// own conveyor reporting healthy.
			Conveyor->Isa95 = FFactoryIsa95Path{
				Enterprise, Site, PlantArea,
				FString::Printf(TEXT("Line%d"), Line),
				TEXT("Conveyor") };
			Conveyor->LayoutPosition = FVector2D(Cursor * 0.5, 0.0);
			Conveyor->LayoutFootprint = FVector2D(Cursor, 0.8);

			Conveyor->MetricAliases.Empty();
			for (const FString& Name : Conveyor->GetAllMetricNames())
			{
				Conveyor->MetricAliases.Add(Name, NextAlias++);
			}
			Created.Add(Conveyor);
		}

		UE_LOG(LogFactorySim, Display,
			TEXT("  line %d: %d station(s) over %.1f m, aliases %lld-%lld"),
			Line, UE_ARRAY_COUNT(Template) + 1, Cursor,
			PlantAliasBase + (Line - 1) * AliasesPerLine, NextAlias - 1);
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
		TEXT("Plant seed complete: %d/%d assets across %d line(s)"),
		Saved, Created.Num(), Lines);
	return Saved == Created.Num() ? 0 : 1;
}
