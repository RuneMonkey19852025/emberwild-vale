using UnrealBuildTool;
using System.Collections.Generic;

public class EmberwildValeEditorTarget : TargetRules
{
	public EmberwildValeEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("EmberwildVale");
	}
}
