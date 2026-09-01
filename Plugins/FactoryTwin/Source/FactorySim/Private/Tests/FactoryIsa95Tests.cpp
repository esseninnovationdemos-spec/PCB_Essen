#include "FactoryIsa95.h"
#include "FactoryMachineInstance.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags Isa95TestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	FFactoryIsa95Path MakePath()
	{
		return FFactoryIsa95Path{
			TEXT("InnoLab"), TEXT("Essen"), TEXT("SMT"),
			TEXT("Line2"), TEXT("ReflowOven") };
	}
}

/**
 * The two published representations must stay derivable from one another.
 *
 * This is the invariant the ISA-95 struct exists to hold: the Sparkplug topic
 * and the UNS path were previously separate hand-typed strings, and they drifted
 * -- the topic said SMT_Line/Cluj while the path said Essen. Swapping the group
 * separator for a slash has to reproduce the UNS path exactly, or they have
 * come apart again.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFactoryIsa95MappingTest,
	"FactoryTwin.FactorySim.Isa95TopicMapping",
	Isa95TestFlags)

bool FFactoryIsa95MappingTest::RunTest(const FString& Parameters)
{
	const FFactoryIsa95Path Path = MakePath();

	TestTrue(TEXT("a fully specified path is valid"), Path.IsValid());
	TestEqual(TEXT("UNS path"), Path.ToUnsPath(),
		FString(TEXT("InnoLab/Essen/SMT/Line2/ReflowOven")));
	TestEqual(TEXT("group id packs the top three levels"), Path.ToGroupId(),
		FString(TEXT("InnoLab:Essen:SMT")));
	TestEqual(TEXT("edge node is the work centre"), Path.ToEdgeNodeId(),
		FString(TEXT("Line2")));
	TestEqual(TEXT("device is the work unit"), Path.ToDeviceId(),
		FString(TEXT("ReflowOven")));

	// The mapping is reversible: this is what stops the two representations
	// describing different machines.
	const FString Rebuilt = FString::Printf(TEXT("%s/%s/%s"),
		*Path.ToGroupId().Replace(FactoryIsa95::GroupSeparator, TEXT("/")),
		*Path.ToEdgeNodeId(), *Path.ToDeviceId());
	TestEqual(TEXT("topic elements rebuild the UNS path"), Rebuilt, Path.ToUnsPath());

	TestTrue(TEXT("round trips through FromUnsPath"),
		FFactoryIsa95Path::FromUnsPath(Path.ToUnsPath()) == Path);

	return true;
}

/** A path missing any level publishes nothing rather than a malformed topic. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFactoryIsa95ValidityTest,
	"FactoryTwin.FactorySim.Isa95RejectsPartialPaths",
	Isa95TestFlags)

bool FFactoryIsa95ValidityTest::RunTest(const FString& Parameters)
{
	FFactoryIsa95Path Missing = MakePath();
	Missing.Area = FString();
	TestFalse(TEXT("a path with no area is invalid"), Missing.IsValid());

	TestFalse(TEXT("a four-segment path does not parse"),
		FFactoryIsa95Path::FromUnsPath(TEXT("InnoLab/Essen/SMT/Line2")).IsValid());
	TestFalse(TEXT("a six-segment path does not parse"),
		FFactoryIsa95Path::FromUnsPath(TEXT("A/B/C/D/E/F")).IsValid());

	// Sparkplug reserves these in every topic-element slot.
	TestFalse(TEXT("slash is rejected"), FactoryIsa95::IsLegalTopicElement(TEXT("A/B")));
	TestFalse(TEXT("plus is rejected"), FactoryIsa95::IsLegalTopicElement(TEXT("A+B")));
	TestFalse(TEXT("hash is rejected"), FactoryIsa95::IsLegalTopicElement(TEXT("A#B")));
	TestFalse(TEXT("empty is rejected"), FactoryIsa95::IsLegalTopicElement(TEXT("")));
	TestTrue(TEXT("the group separator is legal"),
		FactoryIsa95::IsLegalTopicElement(TEXT("InnoLab:Essen:SMT")));

	return true;
}

/**
 * An asset seeded before the hierarchy existed keeps publishing.
 *
 * Machines are matched to an edge node by their derived identity, so an
 * instance that returned nothing here would silently drop off the wire on load
 * rather than failing visibly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FFactoryIsa95LegacyFallbackTest,
	"FactoryTwin.FactorySim.Isa95LegacyFallback",
	Isa95TestFlags)

bool FFactoryIsa95LegacyFallbackTest::RunTest(const FString& Parameters)
{
	UFactoryMachineInstance* Instance = NewObject<UFactoryMachineInstance>();

	Instance->DeviceId = TEXT("REFLOW_OVEN");
	Instance->UnsPath = TEXT("Legacy/Path/To/Reflow/Oven");

	TestEqual(TEXT("device id falls back"), Instance->GetDeviceId(),
		FString(TEXT("REFLOW_OVEN")));
	TestEqual(TEXT("UNS path falls back"), Instance->GetUnsPath(),
		FString(TEXT("Legacy/Path/To/Reflow/Oven")));
	TestTrue(TEXT("no group id without a hierarchy"), Instance->GetGroupId().IsEmpty());

	// Once migrated, the hierarchy wins even though the legacy fields remain.
	Instance->Isa95 = MakePath();
	TestEqual(TEXT("device id comes from the hierarchy"), Instance->GetDeviceId(),
		FString(TEXT("ReflowOven")));
	TestEqual(TEXT("UNS path comes from the hierarchy"), Instance->GetUnsPath(),
		FString(TEXT("InnoLab/Essen/SMT/Line2/ReflowOven")));
	TestEqual(TEXT("level label is qualified by work centre"), Instance->GetLevelLabel(),
		FString(TEXT("Line2_ReflowOven")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
