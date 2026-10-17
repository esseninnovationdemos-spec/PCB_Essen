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

		// Only the JSON payload codec reads and writes JSON; nothing in the public
		// headers exposes it.
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Json",
			}
			);
	}
}
