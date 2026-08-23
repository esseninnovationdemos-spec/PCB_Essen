#pragma once

#include "CoreMinimal.h"

#include "FactoryIsa95.generated.h"

/**
 * A machine's place in the ISA-95 equipment hierarchy.
 *
 * This is the single definition of who a device is. Both representations the
 * system publishes are derived from it:
 *
 *   UNS topic       InnoLab/Essen/SMT/Line1/ReflowOven
 *   Sparkplug topic spBv1.0/InnoLab:Essen:SMT/DDATA/Line1/ReflowOven
 *
 * They used to be two hand-maintained strings on the instance asset, and they
 * drifted apart exactly as you would expect: the UNS path said Essen/InnoLab
 * while the Sparkplug topic still said SMT_Line/Cluj, because nothing forced
 * them to agree. Deriving both from one struct makes that class of bug
 * unrepresentable.
 *
 * ISA-95 gives five levels and Sparkplug gives three topic slots, so the top
 * three collapse into the group id. That is the usual compression, and it
 * keeps the mapping reversible -- swap ':' for '/' in the group id and the UNS
 * path falls out.
 */
USTRUCT(BlueprintType)
struct FACTORYSIM_API FFactoryIsa95Path
{
	GENERATED_BODY()

	/** ISA-95 level 1. The company. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ISA-95")
	FString Enterprise;

	/** ISA-95 level 2. The physical plant. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ISA-95")
	FString Site;

	/** ISA-95 level 3. A production area within the site, e.g. SMT. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ISA-95")
	FString Area;

	/**
	 * ISA-95 level 4. A production line.
	 *
	 * This is also the Sparkplug edge node, which is what makes NDEATH
	 * meaningful: one line dropping off the wire is a thing an operator cares
	 * about, whereas "the simulator exited" is not.
	 */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ISA-95")
	FString WorkCenter;

	/** ISA-95 level 5. One machine, and the Sparkplug device. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "ISA-95")
	FString WorkUnit;

	/** True once every level is filled in. A partial path publishes nothing. */
	bool IsValid() const;

	/** Enterprise/Site/Area/WorkCenter/WorkUnit. */
	FString ToUnsPath() const;

	/** Enterprise:Site:Area -- the top three levels, in one Sparkplug slot. */
	FString ToGroupId() const;

	/** The work centre. One Sparkplug edge node per production line. */
	FString ToEdgeNodeId() const { return WorkCenter; }

	/** The work unit. */
	FString ToDeviceId() const { return WorkUnit; }

	/**
	 * Parses Enterprise/Site/Area/WorkCenter/WorkUnit back into a path.
	 * Anything with the wrong number of segments returns an empty path, which
	 * IsValid() then rejects.
	 */
	static FFactoryIsa95Path FromUnsPath(const FString& UnsPath);

	bool operator==(const FFactoryIsa95Path& Other) const;
};

namespace FactoryIsa95
{
	/** Separator between the ISA-95 levels packed into a Sparkplug group id. */
	inline const TCHAR* GroupSeparator = TEXT(":");

	/**
	 * Characters Sparkplug reserves in the topic-element slots.
	 *
	 * The spec forbids '+', '#' and '/' in group, edge node and device ids --
	 * the first two are wildcards, the third is the topic separator, and any of
	 * them would let one device's name silently subscribe to another's traffic.
	 */
	FACTORYSIM_API bool IsLegalTopicElement(const FString& Element);

	/** Strips reserved characters so a name derived from level geometry is safe. */
	FACTORYSIM_API FString SanitizeTopicElement(const FString& Element);
}
