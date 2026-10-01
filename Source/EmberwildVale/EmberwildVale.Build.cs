using UnrealBuildTool;

public class EmberwildVale : ModuleRules
{
	public EmberwildVale(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"UMG",
			"Json",
			"ProceduralMeshComponent"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RenderCore",
			"RHI",
			"AudioMixer",
			"AudioExtensions", // IAudioProxyDataFactory vtable slots inherited via USoundWave
			"DeveloperSettings"
		});

		// Headless playtest autopilot (EmberwildAutopilot.cpp) stays compiled out.
		// To run it, re-enable and do a `-game -nullrhi -nosound` boot:
		//   PublicDefinitions.Add("EMBERWILD_AUTOPILOT=1");
	}
}
