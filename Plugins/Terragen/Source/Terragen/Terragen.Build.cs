using UnrealBuildTool;

// ATerragenSkybox and UTerragenWorld. The shaders they draw with come from the TerragenShaders module.
public class Terragen : ModuleRules
{
	public Terragen(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
