// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Prototype1 : ModuleRules
{
	public Prototype1(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // TODO: UnrealEd is for debug builds. Find a way to remove it in shipping/dev builds.
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UnrealEd" }); 
	}
}
