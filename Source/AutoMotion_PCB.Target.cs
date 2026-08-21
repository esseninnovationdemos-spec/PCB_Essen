using UnrealBuildTool;
using System.Collections.Generic;

public class AutoMotion_PCBTarget : TargetRules
{
	public AutoMotion_PCBTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V2;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_3;
		ExtraModuleNames.Add("AutoMotion_PCB");
	}
}
