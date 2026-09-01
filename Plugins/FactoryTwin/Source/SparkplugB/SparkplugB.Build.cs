using UnrealBuildTool;

public class SparkplugB : ModuleRules
{
	public SparkplugB(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"MqttTransport",
			}
			);
	}
}
