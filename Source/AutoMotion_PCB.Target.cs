using UnrealBuildTool;
using System.Collections.Generic;

public class AutoMotion_PCBTarget : TargetRules
{
	public AutoMotion_PCBTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		ExtraModuleNames.Add("AutoMotion_PCB");
	}
}
