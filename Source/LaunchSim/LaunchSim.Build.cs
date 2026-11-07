// Copyright (c) 2026 nefariousjosiah. MIT License.

using UnrealBuildTool;

// Pure C++ launch physics. Nothing in Public/ or Private/ (other than
// LaunchSimModule.cpp) may include Unreal headers: the same sources are built by
// CMake for the standalone unit tests that run in CI, where Unreal is not available.
public class LaunchSim : ModuleRules
{
	public LaunchSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		CppStandard = CppStandardVersion.Cpp20;
		PublicDependencyModuleNames.Add("Core");
	}
}
