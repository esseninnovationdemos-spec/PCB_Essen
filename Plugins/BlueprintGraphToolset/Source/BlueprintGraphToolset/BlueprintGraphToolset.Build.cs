using UnrealBuildTool;

public class BlueprintGraphToolset : ModuleRules
{
	public BlueprintGraphToolset(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"UnrealEd",
				"Kismet",
				"KismetCompiler",
				"BlueprintGraph",
				"GraphEditor",
				"AssetRegistry",
				"AssetTools",
				"Json",
				"Slate",
				"SlateCore",
				"ToolsetRegistry",
			}
			);
	}
}
