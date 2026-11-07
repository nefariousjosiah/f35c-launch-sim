// Copyright (c) 2026 nefariousjosiah. MIT License.

using UnrealBuildTool;

public class F35CLaunch : ModuleRules
{
	public F35CLaunch(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"CinematicCamera",
			"Niagara",
			"Json",
			"LaunchSim",
		});
	}
}
