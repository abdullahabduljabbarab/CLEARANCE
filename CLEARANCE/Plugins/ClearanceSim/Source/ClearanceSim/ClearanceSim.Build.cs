using UnrealBuildTool;

public class ClearanceSim : ModuleRules
{
	public ClearanceSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",        // UUserWidget base class for the instructor panel
			"SlateCore"   // FCanvasItem / FGeometry types the widget uses
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",         // FKey / EKeys for push-to-talk
			"AudioCapture",      // mic input
			"AudioCaptureCore",  // low-level capture stream
			"HTTP",              // POST audio to the local whisper server
			"Json",              // parse the transcription response
			"Sockets",           // detect an already-running server
			"Networking",
			"OnlineSubsystem",        // Identity + Session interface for EOS sessions
			"OnlineSubsystemUtils"    // helpers (BeaconHost, world-net plumbing)
		});
	}
}
