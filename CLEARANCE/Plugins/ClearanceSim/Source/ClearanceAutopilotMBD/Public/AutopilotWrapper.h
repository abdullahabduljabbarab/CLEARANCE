#pragma once

#include "CoreMinimal.h"

// -----------------------------------------------------------------------
// C linkage for the Embedded-Coder-generated model entry points. Their
// exact names are chosen at codegen time in Simulink - see the model's
// "Code Generation > Interface" pane. The three below are the classic
// step-function interface (initialize / step / terminate).
//
// While the generated code is absent, the wrapper falls back to a
// pure-C++ stub implementation so this module still links and the sim
// keeps running. When ClearanceAutopilotMBD.Build.cs detects the
// generated headers under ThirdParty/AutopilotGenerated/include it
// flips CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN to 1, the wrapper
// includes the generated header, and the extern "C" symbols resolve to
// the real model. - TripleA
// -----------------------------------------------------------------------

#ifndef CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN
#define CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN 0
#endif

#if CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN
extern "C"
{
	#include "autopilot.h"
}
#else
// Forward declarations for the stub build. RT_MODEL_autopilot_T is a
// tag-only type here so the wrapper compiles even without the generated
// header. Concrete stub state lives inside the .cpp. - TripleA
struct RT_MODEL_autopilot_T;
extern "C"
{
	void autopilot_initialize(RT_MODEL_autopilot_T*);
	void autopilot_step      (RT_MODEL_autopilot_T*);
	void autopilot_terminate (RT_MODEL_autopilot_T*);
}
#endif

// -----------------------------------------------------------------------
// I/O contract between CLEARANCE and the generated autopilot model.
// The Simulink model is authored around these two structs so the
// generated code has a stable shape regardless of block-diagram
// refactors on the model side. Keep field order + units synchronised
// with model/plant_params.m in the standalone autopilot-mbd repo.
// - TripleA
// -----------------------------------------------------------------------

struct FClearanceAutopilotInputs
{
	// Target commands issued by the ATC / instructor loop.
	float TargetHeadingDeg   = 0.f;
	float TargetAltitudeFt   = 0.f;
	float TargetAirspeedKts  = 0.f;

	// Current aircraft state, sampled from FAircraftState at call time.
	float HeadingDeg         = 0.f;
	float AltitudeFt         = 0.f;
	float AirspeedKts        = 0.f;
	float BankAngleDeg       = 0.f;
	float PitchAngleDeg      = 0.f;
	float VerticalSpeedFpm   = 0.f;

	// Timestep matches the sim's control cadence (see MODEL_TS in the
	// standalone repo's tools/run_model_tests_and_build.m).
	float DeltaSeconds       = 0.02f;   // 50 Hz default
};

struct FClearanceAutopilotOutputs
{
	// Control-surface commands, normalised to +/- 1 unless the model
	// documents otherwise. The behaviour loop translates these back
	// into aircraft-state deltas.
	float ElevatorCmd = 0.f;   // pitch axis
	float AileronCmd  = 0.f;   // roll axis
	float RudderCmd   = 0.f;   // yaw axis
	float ThrottleCmd = 0.f;   // 0..1 throttle position
};

// -----------------------------------------------------------------------
// C++ wrapper. Owns the model's lifecycle and hides the extern "C"
// step-function pattern from every caller upstream. Kept out of the
// UObject hierarchy on purpose - the wrapper is a plain value type so
// scenario runners can hold one per aircraft without dragging the
// UE reflection system into the codegen boundary. - TripleA
// -----------------------------------------------------------------------

class CLEARANCEAUTOPILOTMBD_API FAutopilotWrapper
{
public:
	FAutopilotWrapper();
	~FAutopilotWrapper();

	// Non-copyable so nothing accidentally clones the model's internal
	// state and desynchronises the two copies.
	FAutopilotWrapper(const FAutopilotWrapper&)            = delete;
	FAutopilotWrapper& operator=(const FAutopilotWrapper&) = delete;

	void Initialize();
	void Shutdown();

	// Push inputs, run one step, read outputs. Safe to call before
	// Initialize() - it will initialise lazily so scenario startup
	// ordering doesn't have to be exact. - TripleA
	FClearanceAutopilotOutputs Step(const FClearanceAutopilotInputs& In);

	// True when the wrapper is compiled against real generated code.
	// Callers can branch on this to fall back to the classic
	// hand-rolled behaviour loop when the model isn't available.
	static constexpr bool HasGeneratedCode()
	{
		return CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN != 0;
	}

private:
	bool bInitialised = false;

	// Per-instance model state. Each wrapper owns its own RT_MODEL and
	// substructures so a fleet of aircraft can run the autopilot
	// concurrently without stepping on each other's integrators or
	// controller state. Held as raw pointers because the underlying
	// structs are C types from the generated header, allocated in the
	// .cpp under the CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN gate. - TripleA
	void* ModelState = nullptr;
};
