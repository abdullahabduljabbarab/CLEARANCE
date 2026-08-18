// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class CLEARANCETarget : TargetRules
{
	public CLEARANCETarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("CLEARANCE");

		// Override the shared build environment so the linker flag additions
		// below are accepted against the installed engine's UnrealGame binary
		// (Unique build environment requires a source-built engine). - TripleA
		bOverrideBuildEnvironment = true;

		// Monolithic packaged builds link every module into one exe, which
		// surfaces duplicate-symbol conflicts that modular editor DLLs hide:
		// Fast DDS's static asio duplicates UE's Trace-Analysis asio, and
		// Matlab RTW's rt_nonfinite symbols appear in every MBD wrapper. The
		// duplicates are identical implementations; /FORCE:MULTIPLE tells
		// the MSVC linker to accept them and pick one arbitrarily. Warning-
		// noisy but shipped-safe for this codebase. - TripleA
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			AdditionalLinkerArguments += " /FORCE:MULTIPLE /ignore:4006 /ignore:4088";
		}
	}
}
