// Module bootstrap for the CLEARANCE Simulink radar bridge.
// Registers a smoke-test console command on load so the wrapper can
// be exercised from the editor. - TripleA

#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "RadarWrapper.h"

class FClearanceRadarMBDModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogTemp, Log,
			TEXT("[ClearanceRadarMBD] Module loaded. HasGeneratedCode=%s. "
			     "Type 'clearance.radar.mbd.test' in the console to run a smoke test."),
			FRadarWrapper::HasGeneratedCode() ? TEXT("YES") : TEXT("NO"));

		SmokeTestCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("clearance.radar.mbd.test"),
			TEXT("Instantiate the Simulink radar wrapper, run one CPI, log detections."),
			FConsoleCommandDelegate::CreateStatic(&FClearanceRadarMBDModule::RunSmokeTest),
			ECVF_Default);
	}

	virtual void ShutdownModule() override
	{
		if (SmokeTestCmd)
		{
			IConsoleManager::Get().UnregisterConsoleObject(SmokeTestCmd);
			SmokeTestCmd = nullptr;
		}
	}

	static void RunSmokeTest()
	{
		UE_LOG(LogTemp, Warning, TEXT("=== ClearanceRadarMBD smoke test ==="));
		UE_LOG(LogTemp, Warning, TEXT("HasGeneratedCode: %s"),
			FRadarWrapper::HasGeneratedCode() ? TEXT("YES (real DSP)") : TEXT("NO (stub)"));

		FRadarWrapper Wrapper;
		Wrapper.Initialize();

		// Build a trivial input - all zeros. Real DSP will return 0
		// detections on zero noise + zero signal; that's still enough
		// to prove the CPI runs end-to-end without crashing. Realistic
		// I/Q synthesis (target scene from aircraft states) lives in
		// the follow-up integration into UClearanceRadar. - TripleA
		FClearanceRadarInputs In;
		In.LookAngleRad = 0.0;

		const double StartTime = FPlatformTime::Seconds();
		const FClearanceRadarOutputs Out = Wrapper.Step(In);
		const double StepMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

		UE_LOG(LogTemp, Warning, TEXT("radar_step returned %d detections in %.2f ms"),
			Out.NumDetections, StepMs);
		for (int32 i = 0; i < Out.NumDetections; ++i)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("  det %d: R=%.2f km, v=%.1f m/s, SNR=%.1f dB"),
				i,
				Out.Detections[i].RangeMetres / 1000.f,
				Out.Detections[i].VelocityMps,
				Out.Detections[i].SnrDb);
		}
		Wrapper.Shutdown();
		UE_LOG(LogTemp, Warning, TEXT("=== smoke test complete ==="));
	}

private:
	IConsoleCommand* SmokeTestCmd = nullptr;
};

IMPLEMENT_MODULE(FClearanceRadarMBDModule, ClearanceRadarMBD)
