using UnrealBuildTool;

public class FactorySim : ModuleRules
{
	public FactorySim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// The commandlets in this module each keep their helpers in their own
		// named namespace and pull them in with a file-scope using-directive.
		// That is correct per translation unit, but unity builds merge several
		// files into one, and then two namespaces called into scope together
		// make every shared helper name -- CreateAsset, ArchetypeFolder --
		// ambiguous. Which files get merged shifts as they are edited, so the
		// break appears and disappears with unrelated changes. The module is
		// small enough that building its files separately costs little and
		// removes the whole class of failure.
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"MqttTransport",
				"SparkplugB",
				"DeveloperSettings",
				// The line puts its operator panel on screen itself.
				"UMG",
			}
			);

		// Seeding the archetype library is an editor-only authoring step.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(
				new string[]
				{
					"UnrealEd",
					"AssetTools",
					"AssetRegistry",
					// Building the PBR parent material's node graph. Connecting
					// expressions by hand means reaching into the material's
					// editor-only data; UMaterialEditingLibrary is the supported
					// way to do it and keeps the material compiling itself.
					"MaterialEditor",
				}
				);
		}
	}
}
