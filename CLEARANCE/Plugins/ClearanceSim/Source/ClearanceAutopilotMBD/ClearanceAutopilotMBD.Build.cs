using System.IO;
using UnrealBuildTool;

// CLEARANCE bridge to a Simulink / Embedded-Coder-generated autopilot.
// The Simulink model lives in a separate autopilot-mbd repository -
// this module is the Unreal-side landing pad. It ships as a compilable
// stub today so the plugin, module manifest, and Build.cs wiring are
// all in place; when the generated C sources drop into
// ThirdParty/AutopilotGenerated, this module picks them up without
// any other file needing to change. - TripleA
public class ClearanceAutopilotMBD : ModuleRules
{
	public ClearanceAutopilotMBD(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// Core covers FString / IPluginManager plumbing needed by the
		// module bootstrap. ClearanceSim is a runtime dependency so the
		// bridge can convert UE-side aircraft state to the model's
		// double[] I/O shape without pulling in the whole sim from
		// downstream consumers. - TripleA
		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// -------- Generated code drop-in ----------------------------------
		// Path convention: the standalone autopilot-mbd repo's
		// `codegen/` output gets copied (or symlinked) into
		// ClearanceSim/ThirdParty/AutopilotGenerated so this module can
		// find it. The directory is deliberately outside Source/ so a
		// full clean of Intermediate/ never touches the generated
		// artefacts. Everything below is a no-op until that directory
		// actually contains headers and sources. - TripleA
		string GeneratedRoot = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "AutopilotGenerated");
		string GeneratedInclude = Path.Combine(GeneratedRoot, "include");
		string GeneratedSource  = Path.Combine(GeneratedRoot, "src");
		string GeneratedLibDir  = Path.Combine(GeneratedRoot, "lib");

		if (Directory.Exists(GeneratedInclude))
		{
			// Public so downstream modules can call into the wrapper
			// without needing the generated header on a private path.
			PublicIncludePaths.Add(GeneratedInclude);

			// Also expose the generated src/ directory to Private so the
			// shim unit under Private/AutopilotGeneratedUnit.cpp can
			// #include the raw .c file without hopping through the file
			// system. - TripleA
			if (Directory.Exists(GeneratedSource))
			{
				PrivateIncludePaths.Add(GeneratedSource);
			}

			// Preprocessor gate the wrapper uses to switch from stub
			// entry points to real generated calls. - TripleA
			PublicDefinitions.Add("CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN=1");
		}
		else
		{
			PublicDefinitions.Add("CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN=0");
		}

		// Two link paths depending on how the standalone repo packages
		// its output. If Embedded Coder produces a static library
		// (autopilot.lib on Windows / libautopilot.a on Linux), link
		// against it directly. Otherwise compile every .c under
		// generated/src into this module. Both are silent no-ops when
		// nothing has been dropped in. - TripleA
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibPath = Path.Combine(GeneratedLibDir, "autopilot.lib");
			if (File.Exists(LibPath))
			{
				PublicAdditionalLibraries.Add(LibPath);
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			string LibPath = Path.Combine(GeneratedLibDir, "libautopilot.a");
			if (File.Exists(LibPath))
			{
				PublicAdditionalLibraries.Add(LibPath);
			}
		}
	}
}
