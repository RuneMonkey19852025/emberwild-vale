using UnrealBuildTool;
using System.Collections.Generic;

public class EmberwildValeTarget : TargetRules
{
	public EmberwildValeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("EmberwildVale");
	}
}
