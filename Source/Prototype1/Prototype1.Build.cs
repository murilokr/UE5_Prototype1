// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Prototype1 : ModuleRules
{
	public Prototype1(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });

        // UnrealEd is only used by MDebugHelper::PausePIE (WITH_EDITOR).
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
        }
	}
}
