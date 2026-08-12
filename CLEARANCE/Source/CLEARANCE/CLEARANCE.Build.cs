// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class CLEARANCE : ModuleRules
{
	public CLEARANCE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"HeadMountedDisplay",
			"XRBase",
			"Voice",
			"AudioCapture",
			"AudioCaptureCore",
			"Sockets",
			"Networking",
			"CesiumRuntime",
			"ClearanceSim"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"CLEARANCE",
			"CLEARANCE/UI",
			"CLEARANCE/Variant_Horror",
			"CLEARANCE/Variant_Horror/UI",
			"CLEARANCE/Variant_Shooter",
			"CLEARANCE/Variant_Shooter/AI",
			"CLEARANCE/Variant_Shooter/UI",
			"CLEARANCE/Variant_Shooter/Weapons"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
