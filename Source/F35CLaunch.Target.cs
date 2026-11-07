// Copyright (c) 2026 nefariousjosiah. MIT License.

using UnrealBuildTool;

public class F35CLaunchTarget : TargetRules
{
	public F35CLaunchTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		ExtraModuleNames.AddRange(new string[] { "LaunchSim", "F35CLaunch" });

		// UE 5.5's UnrealBuildTool prefers this toolset and flags the newer 14.4x
		// compilers as non-preferred; pinning it keeps a Visual Studio update from
		// silently breaking the build.
		WindowsPlatform.CompilerVersion = "14.38.33130";
	}
}
