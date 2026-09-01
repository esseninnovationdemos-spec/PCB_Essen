#include "FactoryIsa95.h"

bool FFactoryIsa95Path::IsValid() const
{
	// All or nothing. A path missing a level would produce a topic with an
	// empty segment, which is legal MQTT but reads as a different device to
	// every consumer -- worse than refusing to publish.
	return !Enterprise.IsEmpty()
		&& !Site.IsEmpty()
		&& !Area.IsEmpty()
		&& !WorkCenter.IsEmpty()
		&& !WorkUnit.IsEmpty();
}

FString FFactoryIsa95Path::ToUnsPath() const
{
	return FString::Printf(TEXT("%s/%s/%s/%s/%s"),
		*Enterprise, *Site, *Area, *WorkCenter, *WorkUnit);
}

FString FFactoryIsa95Path::ToGroupId() const
{
	return FString::Join(
		TArray<FString>{ Enterprise, Site, Area }, FactoryIsa95::GroupSeparator);
}

FFactoryIsa95Path FFactoryIsa95Path::FromUnsPath(const FString& UnsPath)
{
	TArray<FString> Segments;
	UnsPath.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty=*/false);

	FFactoryIsa95Path Path;
	if (Segments.Num() != 5)
	{
		// Deliberately returns an empty path rather than guessing which levels
		// a short path meant: a wrong guess would publish under a plausible but
		// incorrect identity, which is harder to notice than nothing at all.
		return Path;
	}

	Path.Enterprise = Segments[0];
	Path.Site       = Segments[1];
	Path.Area       = Segments[2];
	Path.WorkCenter = Segments[3];
	Path.WorkUnit   = Segments[4];
	return Path;
}

bool FFactoryIsa95Path::operator==(const FFactoryIsa95Path& Other) const
{
	return Enterprise == Other.Enterprise
		&& Site == Other.Site
		&& Area == Other.Area
		&& WorkCenter == Other.WorkCenter
		&& WorkUnit == Other.WorkUnit;
}

namespace FactoryIsa95
{
	bool IsLegalTopicElement(const FString& Element)
	{
		return !Element.IsEmpty()
			&& !Element.Contains(TEXT("/"))
			&& !Element.Contains(TEXT("+"))
			&& !Element.Contains(TEXT("#"));
	}

	FString SanitizeTopicElement(const FString& Element)
	{
		FString Clean = Element;
		Clean.ReplaceInline(TEXT("/"), TEXT("_"));
		Clean.ReplaceInline(TEXT("+"), TEXT("_"));
		Clean.ReplaceInline(TEXT("#"), TEXT("_"));
		return Clean;
	}
}
