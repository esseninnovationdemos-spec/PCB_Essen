#include "FactoryMachineArchetype.h"
#include "FactoryMachineInstance.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "FactoryTwin"

namespace FactorySyntheticMetrics
{
	const FString StateCode        = TEXT("state_code");
	const FString EventType        = TEXT("event_type");
	const FString InspectionResult = TEXT("inspection_result");
	const FString FailCounter      = TEXT("fail_counter");
	const FString ConveyorId       = TEXT("conveyor_id");
	const FString CurrentPartId    = TEXT("current_part_id");
	const FString PartId           = TEXT("part_id");

	TArray<FString> GetAll()
	{
		// Order matters: the Python layer allocated aliases in exactly this
		// sequence after each device's configured metrics, and those alias
		// numbers are baked into the downstream mapping.
		return { StateCode, InspectionResult, FailCounter,
				 ConveyorId, CurrentPartId, PartId, EventType };
	}
}

namespace FactoryEventTypes
{
	const FString Idle          = TEXT("IDLE");
	const FString PhaseStarted  = TEXT("PHASE_STARTED");
	const FString CycleStarted  = TEXT("CYCLE_STARTED");
	const FString PhaseRunning  = TEXT("PHASE_RUNNING");
	const FString CycleComplete = TEXT("CYCLE_COMPLETE");
	const FString NewMaterial   = TEXT("NEW_MATERIAL");
}

// ---------------------------------------------------------------------------
// UFactoryMachineArchetype
// ---------------------------------------------------------------------------

const FFactoryMetricDefinition* UFactoryMachineArchetype::FindMetric(const FString& MetricName) const
{
	return Metrics.FindByPredicate(
		[&MetricName](const FFactoryMetricDefinition& Definition)
		{
			return Definition.Name == MetricName;
		});
}

TArray<FString> UFactoryMachineArchetype::GetAllMetricNames() const
{
	TArray<FString> Names;
	Names.Reserve(Metrics.Num() + 7);

	for (const FFactoryMetricDefinition& Definition : Metrics)
	{
		Names.Add(Definition.Name);
	}
	Names.Append(FactorySyntheticMetrics::GetAll());
	return Names;
}

FPrimaryAssetId UFactoryMachineArchetype::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("FactoryMachineArchetype"), GetFName());
}

#if WITH_EDITOR
EDataValidationResult UFactoryMachineArchetype::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	TSet<FString> SeenNames;
	for (const FFactoryMetricDefinition& Definition : Metrics)
	{
		if (Definition.Name.IsEmpty())
		{
			Context.AddError(LOCTEXT("EmptyMetricName", "A metric has no name."));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		// A duplicate name would produce two metrics sharing one alias slot.
		if (SeenNames.Contains(Definition.Name))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateMetricName", "Metric '{0}' is defined more than once."),
				FText::FromString(Definition.Name)));
			Result = EDataValidationResult::Invalid;
		}
		SeenNames.Add(Definition.Name);

		// Colliding with a synthetic name would silently shadow it.
		if (FactorySyntheticMetrics::GetAll().Contains(Definition.Name))
		{
			Context.AddError(FText::Format(
				LOCTEXT("ReservedMetricName",
					"Metric '{0}' collides with a reserved synthetic metric."),
				FText::FromString(Definition.Name)));
			Result = EDataValidationResult::Invalid;
		}

		if (Definition.Absolute.Min > Definition.Absolute.Max)
		{
			Context.AddError(FText::Format(
				LOCTEXT("InvertedAbsolute", "Metric '{0}' has an inverted absolute range."),
				FText::FromString(Definition.Name)));
			Result = EDataValidationResult::Invalid;
		}

		// Nominal outside absolute means every generated sample gets clamped.
		if (Definition.Nominal.Min < Definition.Absolute.Min
			|| Definition.Nominal.Max > Definition.Absolute.Max)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("NominalOutsideAbsolute",
					"Metric '{0}' has a nominal range outside its absolute range; "
					"values will be clamped."),
				FText::FromString(Definition.Name)));
		}
	}

	return Result;
}
#endif

// ---------------------------------------------------------------------------
// UFactoryMachineInstance
// ---------------------------------------------------------------------------

float UFactoryMachineInstance::GetTickIntervalSeconds() const
{
	if (TickIntervalSecondsOverride > 0.0f)
	{
		return TickIntervalSecondsOverride;
	}
	return Archetype != nullptr ? Archetype->DefaultTickIntervalSeconds : 1.0f;
}

float UFactoryMachineInstance::GetFailRate() const
{
	if (FailRateOverride >= 0.0f)
	{
		return FailRateOverride;
	}
	return Archetype != nullptr ? Archetype->DefaultFailRate : 0.0f;
}

TArray<FFactoryMetricDefinition> UFactoryMachineInstance::GetEffectiveMetrics() const
{
	TArray<FFactoryMetricDefinition> Result;
	if (Archetype != nullptr)
	{
		Result = Archetype->Metrics;
	}
	Result.Append(AdditionalMetrics);
	return Result;
}

TArray<FString> UFactoryMachineInstance::GetAllMetricNames() const
{
	TArray<FString> Names;
	for (const FFactoryMetricDefinition& Definition : GetEffectiveMetrics())
	{
		Names.Add(Definition.Name);
	}
	Names.Append(FactorySyntheticMetrics::GetAll());
	return Names;
}

int64 UFactoryMachineInstance::GetAlias(const FString& MetricName) const
{
	const int64* Found = MetricAliases.Find(MetricName);
	return Found != nullptr ? *Found : 0;
}

FFactoryRange UFactoryMachineInstance::GetNominalRange(
	const FFactoryMetricDefinition& Definition) const
{
	if (const FFactoryRange* Override = NominalOverrides.Find(Definition.Name))
	{
		return *Override;
	}
	return Definition.Nominal;
}

FString UFactoryMachineInstance::GetDeviceId() const
{
	// The hierarchy wins where it is filled in, so an asset that has been
	// migrated publishes under its ISA-95 work unit even if the legacy field
	// was left behind. Only assets with no hierarchy at all fall back.
	return Isa95.IsValid() ? Isa95.ToDeviceId() : DeviceId;
}

FString UFactoryMachineInstance::GetGroupId() const
{
	return Isa95.IsValid() ? Isa95.ToGroupId() : FString();
}

FString UFactoryMachineInstance::GetEdgeNodeId() const
{
	return Isa95.IsValid() ? Isa95.ToEdgeNodeId() : FString();
}

FString UFactoryMachineInstance::GetUnsPath() const
{
	return Isa95.IsValid() ? Isa95.ToUnsPath() : UnsPath;
}

FString UFactoryMachineInstance::GetLevelLabel() const
{
	if (!Isa95.IsValid())
	{
		return DeviceId;
	}
	return FString::Printf(TEXT("%s_%s"), *Isa95.WorkCenter, *Isa95.WorkUnit);
}

FPrimaryAssetId UFactoryMachineInstance::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(TEXT("FactoryMachineInstance"), GetFName());
}

#if WITH_EDITOR
EDataValidationResult UFactoryMachineInstance::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Archetype == nullptr)
	{
		Context.AddError(LOCTEXT("NoArchetype", "Instance has no archetype."));
		return EDataValidationResult::Invalid;
	}

	if (GetDeviceId().IsEmpty())
	{
		Context.AddError(LOCTEXT("NoDeviceId",
			"Instance has no device id; it would publish to a malformed topic."));
		Result = EDataValidationResult::Invalid;
	}

	if (!Isa95.IsValid())
	{
		// A warning, not an error: the legacy fields still publish. But an
		// unmigrated asset cannot be addressed by hierarchy, so it will not
		// appear under the site's UNS tree alongside everything else.
		Context.AddWarning(LOCTEXT("NoIsa95",
			"Instance has no ISA-95 path; it falls back to the legacy device id "
			"and UNS path, and its topic will not match the site hierarchy."));
	}
	else
	{
		// Sparkplug reserves these in every topic-element slot. A name carrying
		// one would publish to a topic that overlaps another device's
		// subscription, which is far worse than refusing to build it.
		for (const TPair<FString, FString>& Level : {
				TPair<FString, FString>(TEXT("Enterprise"), Isa95.Enterprise),
				TPair<FString, FString>(TEXT("Site"),       Isa95.Site),
				TPair<FString, FString>(TEXT("Area"),       Isa95.Area),
				TPair<FString, FString>(TEXT("WorkCenter"), Isa95.WorkCenter),
				TPair<FString, FString>(TEXT("WorkUnit"),   Isa95.WorkUnit) })
		{
			if (!FactoryIsa95::IsLegalTopicElement(Level.Value))
			{
				Context.AddError(FText::Format(
					LOCTEXT("IllegalIsa95Level",
						"ISA-95 level '{0}' is '{1}', which contains a character "
						"Sparkplug reserves ('/', '+' or '#')."),
					FText::FromString(Level.Key),
					FText::FromString(Level.Value)));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	// Every emittable metric needs an alias, and no two may share one.
	TMap<int64, FString> AliasOwners;
	for (const FString& MetricName : GetAllMetricNames())
	{
		const int64 Alias = GetAlias(MetricName);
		if (Alias == 0)
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("MissingAlias",
					"Metric '{0}' has no pinned alias; it will publish by name only."),
				FText::FromString(MetricName)));
			continue;
		}

		if (const FString* Existing = AliasOwners.Find(Alias))
		{
			Context.AddError(FText::Format(
				LOCTEXT("DuplicateAlias",
					"Alias {0} is used by both '{1}' and '{2}'."),
				FText::AsNumber(Alias),
				FText::FromString(*Existing),
				FText::FromString(MetricName)));
			Result = EDataValidationResult::Invalid;
		}
		AliasOwners.Add(Alias, MetricName);
	}

	// Aliases for names this placement cannot emit are stale config.
	const TArray<FString> Emittable = GetAllMetricNames();
	for (const TPair<FString, int64>& Pair : MetricAliases)
	{
		if (!Emittable.Contains(Pair.Key))
		{
			Context.AddWarning(FText::Format(
				LOCTEXT("StaleAlias",
					"Alias mapped for '{0}', which this archetype does not publish."),
				FText::FromString(Pair.Key)));
		}
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
