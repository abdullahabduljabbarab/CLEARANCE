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
			"UMG",          // UUserWidget base class for the instructor panel
			"Slate",        // FSlateApplication for the renderer resource handle (coverage gradients)
			"SlateCore",    // FCanvasItem / FGeometry types the widget uses
			"ClearanceDIS", // Pure-C++ wire format for DIS PDU serialisation / parsing
			"ClearanceDDS", // Fast DDS pub/sub middleware for real-time telemetry
			"ClearanceRTI", // RTI Connext DDS pub/sub middleware - third wire alongside DDS
			"ClearanceHLA",  // OpenRTI IEEE 1516-2010 HLA-Evolved federate - fourth wire
			"ClearanceAutopilotMBD", // Simulink-generated cascade autopilot (Model-Based Design bridge)
			"ClearanceRadarMBD"      // Simulink-generated radar DSP chain (Model-Based Design bridge)
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
