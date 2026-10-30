using UnrealBuildTool;

// Registers the plugin's Shaders folder as the /TerragenShaders virtual shader folder, so materials can
// #include Terragen.ush. It loads at PostConfigInit, before any material compiles.
public class TerragenShaders : ModuleRules
{
	public TerragenShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[] { "Core", "RenderCore", "Projects" });
	}
}
