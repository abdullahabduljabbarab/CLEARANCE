#pragma once

#include "CoreMinimal.h"

// -----------------------------------------------------------------------
// C linkage for the Embedded-Coder-generated missile model entry points.
// Unlike the autopilot's ExtU / ExtY struct-based interface, the missile
// codegen uses a direct arg-list interface: r_T / v_T / t come in as
// arrays / scalars, r_M_out / v_M_out / term_flag_out come out the same
// way. The factory `missile()` allocates the model, wires internal state,
// and returns a pointer the caller owns. - TripleA
//
// While the generated code is absent, the wrapper falls back to a pure-
// C++ stub implementation so this module still links and the sim keeps
// running. When ClearanceMissileMBD.Build.cs detects the generated headers
// under ThirdParty/MissileGenerated/include it flips
// CLEARANCE_MISSILE_MBD_HAVE_CODEGEN to 1, the wrapper includes the
// generated header, and the extern "C" symbols resolve to the real
// model. - TripleA
// -----------------------------------------------------------------------

#ifndef CLEARANCE_MISSILE_MBD_HAVE_CODEGEN
#define CLEARANCE_MISSILE_MBD_HAVE_CODEGEN 0
#endif

#if CLEARANCE_MISSILE_MBD_HAVE_CODEGEN
extern "C"
{
	#include "missile.h"
}
#else
// Forward-declared tag type so the wrapper compiles without the header.
struct RT_MODEL_missile_T;
using cw_real_T = double;
extern "C"
{
	RT_MODEL_missile_T* missile(cw_real_T r_T[3], cw_real_T v_T[3], cw_real_T* t,
	                            cw_real_T r_M_out[3], cw_real_T v_M_out[3],
	                            cw_real_T* term_flag_out);
	void missile_initialize(RT_MODEL_missile_T* M);
	void missile_step(RT_MODEL_missile_T* M, cw_real_T r_T[3], cw_real_T v_T[3],
	                  cw_real_T t, cw_real_T r_M_out[3], cw_real_T v_M_out[3],
	                  cw_real_T* term_flag_out);
	void missile_terminate(RT_MODEL_missile_T* M);
}
#endif

// -----------------------------------------------------------------------
// I/O contract between CLEARANCE and the generated missile model.
// Everything in the model is inertial-frame metres, m/s, and seconds -
// the Simulink model doesn't know about UE cm or km. The wrapper does
// the unit / frame conversions at the boundary. - TripleA
// -----------------------------------------------------------------------

struct FClearanceMissileInputs
{
	// Target state in the model's inertial frame (metres, m/s).
	FVector TargetPosMeters   = FVector::ZeroVector;
	FVector TargetVelMps      = FVector::ZeroVector;

	// Elapsed engagement time in seconds since launch. Feeds the model's
	// timeout termination check.
	double  ElapsedSeconds    = 0.0;

	// Sim tick length in seconds. The model uses a fixed 50 Hz solver
	// internally so this is informational; the wrapper accumulates real
	// wall-time to decide when to call missile_step.
	float   DeltaSeconds      = 0.02f;
};

struct FClearanceMissileOutputs
{
	// Missile state after the step, inertial frame metres / m/s.
	FVector MissilePosMeters  = FVector::ZeroVector;
	FVector MissileVelMps     = FVector::ZeroVector;

	// Termination state (matches TerminationSubsystem/Priority_Latch):
	//   0 = in flight
	//   1 = intercept   (|r_T - r_M| < R_LETHAL)
	//   2 = timeout     (t >= T_MAX)
	//   3 = LOS reversal (target passed the missile)
	int32   TerminationFlag   = 0;
};

// -----------------------------------------------------------------------
// C++ wrapper. Owns the model's lifecycle and hides the extern "C"
// factory-plus-step pattern from every caller upstream. Kept out of the
// UObject hierarchy on purpose - one wrapper per in-flight missile,
// allocated by AClearanceMissile at spawn time. - TripleA
// -----------------------------------------------------------------------

class CLEARANCEMISSILEMBD_API FMissileWrapper
{
public:
	FMissileWrapper();
	~FMissileWrapper();

	FMissileWrapper(const FMissileWrapper&)            = delete;
	FMissileWrapper& operator=(const FMissileWrapper&) = delete;

	void Initialize();
	void Shutdown();

	// Push inputs, run one step, read outputs. Lazy-init on first call.
	FClearanceMissileOutputs Step(const FClearanceMissileInputs& In);

	static constexpr bool HasGeneratedCode()
	{
		return CLEARANCE_MISSILE_MBD_HAVE_CODEGEN != 0;
	}

private:
	bool bInitialised = false;

	// Per-instance model state. Allocated by the missile() factory in the
	// codegen-on build, or by the stub in the codegen-off build.
	void* ModelState = nullptr;

	// I/O buffers wired into the model at construction. Held as members
	// so their addresses stay stable for the model's lifetime - the
	// factory captures pointers to these into the internal RT_MODEL. - TripleA
	double InputTargetPos[3]  = {0.0, 0.0, 0.0};
	double InputTargetVel[3]  = {0.0, 0.0, 0.0};
	double InputElapsedTime   = 0.0;
	double OutputMissilePos[3]= {0.0, 0.0, 0.0};
	double OutputMissileVel[3]= {0.0, 0.0, 0.0};
	double OutputTermFlag     = 0.0;

	// Fixed-step accumulator so we call missile_step at the model's
	// authored 50 Hz regardless of the sim tick rate.
	double StepAccumulatorSeconds = 0.0;
	FClearanceMissileOutputs LastOutputs = {};
};
