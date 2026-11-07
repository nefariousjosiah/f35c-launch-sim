// Copyright (c) 2026 nefariousjosiah. MIT License.

using UnrealBuildTool;

public class F35CLaunchEditorTarget : TargetRules
{
	public F35CLaunchEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5;
		ExtraModuleNames.AddRange(new string[] { "LaunchSim", "F35CLaunch" });
		WindowsPlatform.CompilerVersion = "14.38.33130";  // see F35CLaunch.Target.cs
	}
}
