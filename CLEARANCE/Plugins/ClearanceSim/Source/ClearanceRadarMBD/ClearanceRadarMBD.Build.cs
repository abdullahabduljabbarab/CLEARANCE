using System.IO;
using UnrealBuildTool;

// CLEARANCE bridge to a Simulink-generated pulsed radar signal
// processor. The Simulink model lives in a separate radar-mbd
// repository; this module is the Unreal-side landing pad. When the
// generated C sources drop into ThirdParty/RadarGenerated, this
// module picks them up and flips CLEARANCE_RADAR_MBD_HAVE_CODEGEN=1.
// Otherwise the wrapper compiles as a no-op stub so downstream
// callers still link.
//
// Same pattern as ClearanceAutopilotMBD. - TripleA
public class ClearanceRadarMBD : ModuleRules
{
	public ClearanceRadarMBD(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// -------- Generated code drop-in ----------------------------------
		// The standalone radar-mbd repo's codegen/ output gets copied (or
		// symlinked) into ClearanceSim/ThirdParty/RadarGenerated so this
		// module can find it. Directory deliberately outside Source/ so a
		// full clean of Intermediate/ never touches the generated
		// artefacts. Everything below is a no-op until that directory
		// actually contains headers and sources. - TripleA
		string GeneratedRoot    = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "RadarGenerated");
		string GeneratedInclude = Path.Combine(GeneratedRoot, "include");
		string GeneratedSource  = Path.Combine(GeneratedRoot, "src");

		if (Directory.Exists(GeneratedInclude))
		{
			// Public so downstream modules can call into the wrapper
			// without needing the generated header on a private path.
			PublicIncludePaths.Add(GeneratedInclude);

			// Also expose the generated src/ directory to Private so the
			// compilation shim under Private/RadarGeneratedUnit.cpp can
			// #include the raw .c file without hopping through the file
			// system.
			if (Directory.Exists(GeneratedSource))
			{
				PrivateIncludePaths.Add(GeneratedSource);
			}

			PublicDefinitions.Add("CLEARANCE_RADAR_MBD_HAVE_CODEGEN=1");
		}
		else
		{
			PublicDefinitions.Add("CLEARANCE_RADAR_MBD_HAVE_CODEGEN=0");
		}
	}
}
