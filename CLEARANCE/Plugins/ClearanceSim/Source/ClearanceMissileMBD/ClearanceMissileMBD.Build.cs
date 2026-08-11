using System.IO;
using UnrealBuildTool;

// CLEARANCE bridge to a Simulink / Embedded-Coder-generated proportional-
// navigation missile guidance model. The Simulink model lives in a separate
// missile-mbd repository - this module is the Unreal-side landing pad.
// Ships as a compilable stub today so the plugin, module manifest, and
// Build.cs wiring are all in place; when the generated C sources drop into
// ThirdParty/MissileGenerated, this module picks them up without any other
// file needing to change. - TripleA
public class ClearanceMissileMBD : ModuleRules
{
	public ClearanceMissileMBD(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// -------- Generated code drop-in ----------------------------------
		// Path convention: the standalone missile-mbd repo's rtwbuild output
		// (missile_ert_rtw/) gets split into headers -> include/ and sources
		// -> src/ under ClearanceSim/ThirdParty/MissileGenerated. Outside
		// Source/ so a full clean of Intermediate/ never touches the
		// generated artefacts. Everything below is a no-op until that
		// directory actually contains headers and sources. - TripleA
		string GeneratedRoot = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "MissileGenerated");
		string GeneratedInclude = Path.Combine(GeneratedRoot, "include");
		string GeneratedSource  = Path.Combine(GeneratedRoot, "src");
		string GeneratedLibDir  = Path.Combine(GeneratedRoot, "lib");

		if (Directory.Exists(GeneratedInclude))
		{
			PublicIncludePaths.Add(GeneratedInclude);

			if (Directory.Exists(GeneratedSource))
			{
				PrivateIncludePaths.Add(GeneratedSource);
			}

			PublicDefinitions.Add("CLEARANCE_MISSILE_MBD_HAVE_CODEGEN=1");
		}
		else
		{
			PublicDefinitions.Add("CLEARANCE_MISSILE_MBD_HAVE_CODEGEN=0");
		}

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string LibPath = Path.Combine(GeneratedLibDir, "missile.lib");
			if (File.Exists(LibPath))
			{
				PublicAdditionalLibraries.Add(LibPath);
			}
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			string LibPath = Path.Combine(GeneratedLibDir, "libmissile.a");
			if (File.Exists(LibPath))
			{
				PublicAdditionalLibraries.Add(LibPath);
			}
		}
	}
}
