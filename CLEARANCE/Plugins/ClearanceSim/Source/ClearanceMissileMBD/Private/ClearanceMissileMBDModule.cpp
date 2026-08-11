// Module bootstrap for the CLEARANCE Simulink missile guidance bridge.
// Registers a smoke-test console command on load so the wrapper can be
// verified from the editor without wiring it into an actor. - TripleA

#include "Modules/ModuleManager.h"
#include "HAL/IConsoleManager.h"
#include "MissileWrapper.h"

class FClearanceMissileMBDModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogTemp, Log,
			TEXT("[ClearanceMissileMBD] Module loaded. HasGeneratedCode=%s. "
			     "Type 'clearance.missile.test' in the console to run a smoke test."),
			FMissileWrapper::HasGeneratedCode() ? TEXT("YES") : TEXT("NO"));

		SmokeTestCmd = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("clearance.missile.test"),
			TEXT("Run a smoke test of the ClearanceMissileMBD wrapper against a lateral-crossing target scene."),
			FConsoleCommandDelegate::CreateStatic(&FClearanceMissileMBDModule::RunSmokeTest),
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
		UE_LOG(LogTemp, Warning, TEXT("=== ClearanceMissileMBD smoke test ==="));
		UE_LOG(LogTemp, Warning, TEXT("HasGeneratedCode: %s"),
			FMissileWrapper::HasGeneratedCode() ? TEXT("YES (real model)") : TEXT("NO (stub pursuit)"));

		FMissileWrapper Wrapper;
		Wrapper.Initialize();

		// Lateral-crossing scene, same shape as target_scene.m in missile-mbd:
		// target at (5000, 500, 1000) m flying leftward at 100 m/s.
		FClearanceMissileInputs In;
		In.TargetVelMps    = FVector(-100.0, 0.0, 0.0);
		In.DeltaSeconds    = 0.02f;

		int32 LastTerm = 0;
		for (int32 Step = 0; Step < 3000; ++Step)
		{
			In.ElapsedSeconds  = Step * 0.02;
			In.TargetPosMeters = FVector(5000.0 + In.TargetVelMps.X * In.ElapsedSeconds, 500.0, 1000.0);

			const FClearanceMissileOutputs Out = Wrapper.Step(In);
			LastTerm = Out.TerminationFlag;

			if (Out.TerminationFlag != 0)
			{
				const FVector Miss = In.TargetPosMeters - Out.MissilePosMeters;
				UE_LOG(LogTemp, Warning,
					TEXT("Terminated at t=%.2fs, flag=%d (1=intercept 2=timeout 3=LOS reversal). "
					     "Missile at (%.1f, %.1f, %.1f), target at (%.1f, %.1f, %.1f), miss %.2f m"),
					In.ElapsedSeconds, LastTerm,
					Out.MissilePosMeters.X, Out.MissilePosMeters.Y, Out.MissilePosMeters.Z,
					In.TargetPosMeters.X, In.TargetPosMeters.Y, In.TargetPosMeters.Z,
					Miss.Size());
				break;
			}
		}
		if (LastTerm == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("Never terminated within 60s (3000 steps at 20ms)."));
		}

		Wrapper.Shutdown();
		UE_LOG(LogTemp, Warning, TEXT("=== smoke test complete ==="));
	}

private:
	IConsoleCommand* SmokeTestCmd = nullptr;
};

IMPLEMENT_MODULE(FClearanceMissileMBDModule, ClearanceMissileMBD)
