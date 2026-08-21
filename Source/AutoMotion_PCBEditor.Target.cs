using UnrealBuildTool;
using System.Collections.Generic;

public class AutoMotion_PCBEditorTarget : TargetRules
{
	public AutoMotion_PCBEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		ExtraModuleNames.Add("AutoMotion_PCB");
	}
}
