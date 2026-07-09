// Module bootstrap for the CLEARANCE Simulink autopilot bridge.
// Registers the smoke-test console command on load so the wrapper can
// be verified from the editor without wiring it into an actor. - TripleA

#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "AutopilotWrapper.h"

class FClearanceAutopilotMBDModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogTemp, Log,
			TEXT("[ClearanceAutopilotMBD] Module loaded. HasGeneratedCode=%s. "
			     "Type 'clearance.autopilot.test' in the console to run a smoke test."),
			FAutopilotWrapper::HasGeneratedCode() ? TEXT("YES") : TEXT("NO"));

		SmokeTestCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("clearance.autopilot.test"),
			TEXT("Run a 10-step smoke test of the ClearanceAutopilotMBD wrapper and log outputs."),
			FConsoleCommandDelegate::CreateStatic(&FClearanceAutopilotMBDModule::RunSmokeTest),
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
		UE_LOG(LogTemp, Warning, TEXT("=== ClearanceAutopilotMBD smoke test ==="));
		UE_LOG(LogTemp, Warning, TEXT("HasGeneratedCode: %s"),
			FAutopilotWrapper::HasGeneratedCode() ? TEXT("YES (real model)") : TEXT("NO (stub PID)"));

		FAutopilotWrapper Wrapper;
		Wrapper.Initialize();

		FClearanceAutopilotInputs In;
		In.TargetAirspeedKts  = 250.f;
		In.AirspeedKts        = 240.f;
		In.PitchAngleDeg      = 2.5f;
		In.BankAngleDeg       = 0.f;
		In.TargetAltitudeFt   = 30000.f;
		In.AltitudeFt         = 29500.f;
		In.DeltaSeconds       = 0.02f;

		for (int32 Step = 0; Step < 10; ++Step)
		{
			const FClearanceAutopilotOutputs Out = Wrapper.Step(In);
			UE_LOG(LogTemp, Warning,
				TEXT("Step %2d: throttle=%.4f  aileron=%.4f  elevator=%.4f  rudder=%.4f"),
				Step, Out.ThrottleCmd, Out.AileronCmd, Out.ElevatorCmd, Out.RudderCmd);
		}

		Wrapper.Shutdown();
		UE_LOG(LogTemp, Warning, TEXT("=== smoke test complete ==="));
	}

private:
	IConsoleCommand* SmokeTestCmd = nullptr;
};

IMPLEMENT_MODULE(FClearanceAutopilotMBDModule, ClearanceAutopilotMBD)
