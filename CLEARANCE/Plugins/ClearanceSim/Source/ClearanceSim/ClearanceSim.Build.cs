using System.IO;
using UnrealBuildTool;

public class ClearanceSim : ModuleRules
{
	public ClearanceSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Meta XR Interaction SDK (Oculus Interaction) is only present on
		// machines that have installed Meta's separately-distributed plugin.
		// Auto-detect and gate the anim-driving code with a preprocessor
		// macro so the module compiles + runs cleanly with or without it.
		// Same shape as the MBD generated-code fallback pattern. - TripleA
		string OculusInteractionUPlugin = Path.Combine(PluginDirectory, "..", "MetaXRInteraction", "OculusInteraction.uplugin");
		bool bHasOculusInteraction = File.Exists(OculusInteractionUPlugin);
		PublicDefinitions.Add(bHasOculusInteraction ?
			"CLEARANCE_HAS_OCULUS_INTERACTION=1" :
			"CLEARANCE_HAS_OCULUS_INTERACTION=0");

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"UMG",                // UUserWidget base class for the instructor panel
			"Slate",              // FSlateApplication for the renderer resource handle (coverage gradients)
			"SlateCore",          // FCanvasItem / FGeometry types the widget uses
			"RenderCore",         // BeginCleanup + FWidgetRenderer deferred-cleanup path
			"HeadMountedDisplay", // MotionControllerComponent + IXRTrackingSystem for the VR operator pawn
			"XRBase",             // UHeadMountedDisplayFunctionLibrary::EnableHMD (moved out of HeadMountedDisplay in UE 5.7)
			"ClearanceDIS",       // Pure-C++ wire format for DIS PDU serialisation / parsing
			"ClearanceDDS",       // Fast DDS pub/sub middleware for real-time telemetry
			"ClearanceRTI",       // RTI Connext DDS pub/sub middleware - third wire alongside DDS
			"ClearanceHLA",       // OpenRTI IEEE 1516-2010 HLA-Evolved federate - fourth wire
			"ClearanceAutopilotMBD", // Simulink-generated cascade autopilot (Model-Based Design bridge)
			"ClearanceRadarMBD",     // Simulink-generated radar DSP chain (Model-Based Design bridge)
			"ClearanceMissileMBD"    // Simulink-generated proportional-navigation missile guidance (Model-Based Design bridge)
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",         // FKey / EKeys for push-to-talk
			"EnhancedInput",     // Input mapping context for the VR operator pawn triggers / grip
			"AudioCapture",      // mic input
			"AudioCaptureCore",  // low-level capture stream
			"HTTP",              // POST audio to the local whisper server
			"Json",              // parse the transcription response
			"Sockets",           // detect an already-running server
			"Networking",
			"OnlineSubsystem",           // Identity + Session interface for EOS sessions
			"OnlineSubsystemUtils"       // helpers (BeaconHost, world-net plumbing)
		});

		if (bHasOculusInteraction)
		{
			PrivateDependencyModuleNames.Add("OculusInteractionPrebuilts"); // QuestControllerAnimInstance setters
		}
	}
}
