using UnrealBuildTool;

public class FactorySim : ModuleRules
{
	public FactorySim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"MqttTransport",
				"SparkplugB",
				"DeveloperSettings",
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
				}
				);
		}
	}
}
