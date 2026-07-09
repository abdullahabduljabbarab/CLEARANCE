#include "AutopilotWrapper.h"

// -----------------------------------------------------------------------
// Per-instance model state. In reusable-function packaging, every
// autopilot_step / initialize / terminate call takes a pointer to an
// RT_MODEL_autopilot_T that owns the run-time model state (block IO,
// continuous states, external inputs / outputs). Allocating one
// FInstance per wrapper is what makes a fleet of aircraft safe to
// run concurrently - no shared globals across instances. - TripleA
// -----------------------------------------------------------------------

#if CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN

struct FAutopilotInstance
{
	RT_MODEL_autopilot_T Model;
	B_autopilot_T        BlockIO;
	X_autopilot_T        ContStates;
	XDis_autopilot_T     ContStateDisabled;
	ExtU_autopilot_T     Inputs;
	ExtY_autopilot_T     Outputs;

	void Wire()
	{
		Model.blockIO           = &BlockIO;
		Model.contStates        = &ContStates;
		Model.contStateDisabled = &ContStateDisabled;
		Model.inputs            = &Inputs;
		Model.outputs           = &Outputs;
	}
};

#else

// -----------------------------------------------------------------------
// While no generated code is present, the wrapper falls back to a
// pure-C++ stub. Each FAutopilotInstance holds its own stub state
// so multi-aircraft engagement still works in the codegen-off build.
// -----------------------------------------------------------------------
struct FAutopilotInstance
{
	bool                        bStubReady = false;
	FClearanceAutopilotInputs   LastInput;
	FClearanceAutopilotOutputs  LastOutput;
};

extern "C" void autopilot_initialize(RT_MODEL_autopilot_T* rtM)
{
	FAutopilotInstance* Inst = reinterpret_cast<FAutopilotInstance*>(rtM);
	if (Inst)
	{
		Inst->bStubReady  = true;
		Inst->LastInput   = FClearanceAutopilotInputs();
		Inst->LastOutput  = FClearanceAutopilotOutputs();
	}
}

extern "C" void autopilot_step(RT_MODEL_autopilot_T* rtM)
{
	FAutopilotInstance* Inst = reinterpret_cast<FAutopilotInstance*>(rtM);
	if (!Inst || !Inst->bStubReady) { return; }

	// Proportional-only stubs so the outputs at least track the sign of
	// each error. Numbers here are meaningless placeholders - the real
	// gains, integrator windup handling, and rate limits live inside
	// the Simulink model. - TripleA
	const float HdgErr = Inst->LastInput.TargetHeadingDeg  - Inst->LastInput.HeadingDeg;
	const float AltErr = Inst->LastInput.TargetAltitudeFt  - Inst->LastInput.AltitudeFt;
	const float SpdErr = Inst->LastInput.TargetAirspeedKts - Inst->LastInput.AirspeedKts;

	Inst->LastOutput.AileronCmd  = FMath::Clamp(0.03f * HdgErr, -1.f, 1.f);
	Inst->LastOutput.ElevatorCmd = FMath::Clamp(0.0005f * AltErr, -1.f, 1.f);
	Inst->LastOutput.RudderCmd   = 0.f;
	Inst->LastOutput.ThrottleCmd = FMath::Clamp(0.5f + 0.01f * SpdErr, 0.f, 1.f);
}

extern "C" void autopilot_terminate(RT_MODEL_autopilot_T* rtM)
{
	FAutopilotInstance* Inst = reinterpret_cast<FAutopilotInstance*>(rtM);
	if (Inst) { Inst->bStubReady = false; }
}

#endif // CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN

// -----------------------------------------------------------------------
// FAutopilotWrapper - thin adaptor above the extern "C" surface. Owns
// exactly one FAutopilotInstance so each aircraft carries its own PID
// integrators, filter states, and controller history. - TripleA
// -----------------------------------------------------------------------

FAutopilotWrapper::FAutopilotWrapper() = default;

FAutopilotWrapper::~FAutopilotWrapper()
{
	Shutdown();
}

void FAutopilotWrapper::Initialize()
{
	if (bInitialised) { return; }

	if (!ModelState)
	{
		FAutopilotInstance* Inst = new FAutopilotInstance();
	#if CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN
		Inst->Wire();
	#endif
		ModelState = Inst;
	}

#if CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN
	FAutopilotInstance* Inst = static_cast<FAutopilotInstance*>(ModelState);
	autopilot_initialize(&Inst->Model);
#else
	autopilot_initialize(reinterpret_cast<RT_MODEL_autopilot_T*>(ModelState));
#endif
	bInitialised = true;
}

void FAutopilotWrapper::Shutdown()
{
	if (!bInitialised && !ModelState) { return; }

	if (ModelState)
	{
	#if CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN
		FAutopilotInstance* Inst = static_cast<FAutopilotInstance*>(ModelState);
		if (bInitialised) { autopilot_terminate(&Inst->Model); }
	#else
		if (bInitialised) { autopilot_terminate(reinterpret_cast<RT_MODEL_autopilot_T*>(ModelState)); }
	#endif
		delete static_cast<FAutopilotInstance*>(ModelState);
		ModelState = nullptr;
	}
	bInitialised = false;
}

FClearanceAutopilotOutputs FAutopilotWrapper::Step(const FClearanceAutopilotInputs& In)
{
	if (!bInitialised) { Initialize(); }

	FAutopilotInstance* Inst = static_cast<FAutopilotInstance*>(ModelState);

#if CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN
	// Classic cascade autopilot: this wrapper runs the outer loops
	// (heading -> target bank, altitude -> target pitch), the Simulink
	// model runs the inner loops (bank -> aileron, pitch -> elevator,
	// airspeed -> throttle). Model outputs are control-surface commands
	// that the aircraft-side kinematics turn into rate changes. - TripleA

	constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

	// Outer loop 1: heading error -> target bank. Wrap the error into
	// [-180, +180] so a 359 -> 001 command doesn't go the long way round.
	double HeadingErrorDeg = In.TargetHeadingDeg - In.HeadingDeg;
	while (HeadingErrorDeg >  180.0) { HeadingErrorDeg -= 360.0; }
	while (HeadingErrorDeg < -180.0) { HeadingErrorDeg += 360.0; }
	constexpr double kBankPerHeadingDeg = 0.3;
	constexpr double kMaxBankDeg        = 15.0;
	constexpr double kHeadingCaptureDeg = 2.0;
	double TargetBankDeg = FMath::Clamp(
		HeadingErrorDeg * kBankPerHeadingDeg, -kMaxBankDeg, kMaxBankDeg);
	if (FMath::Abs(HeadingErrorDeg) < kHeadingCaptureDeg)
	{
		TargetBankDeg = 0.0;
	}

	// Outer loop 2: altitude error -> target pitch. Similar shape.
	const double AltErrorFt = In.TargetAltitudeFt - In.AltitudeFt;
	constexpr double kPitchPerAltFt = 0.0005;
	constexpr double kMaxPitchDeg   = 8.0;
	const double TargetPitchDeg = FMath::Clamp(
		AltErrorFt * kPitchPerAltFt, -kMaxPitchDeg, kMaxPitchDeg);

	// Push into this instance's own ExtU (not the singleton global).
	Inst->Inputs.V_cmd     = In.TargetAirspeedKts;
	Inst->Inputs.V         = In.AirspeedKts;
	Inst->Inputs.phi       = In.BankAngleDeg  * kDegToRad;
	Inst->Inputs.theta     = In.PitchAngleDeg * kDegToRad;
	Inst->Inputs.phi_cmd   = TargetBankDeg  * kDegToRad;
	Inst->Inputs.theta_cmd = TargetPitchDeg * kDegToRad;

	autopilot_step(&Inst->Model);

	FClearanceAutopilotOutputs Out;
	Out.ThrottleCmd = static_cast<float>(Inst->Outputs.delta_t_out);
	Out.ElevatorCmd = static_cast<float>(Inst->Outputs.delta_e_out);
	Out.AileronCmd  = static_cast<float>(Inst->Outputs.delta_a_out);
	Out.RudderCmd   = 0.f;
	return Out;
#else
	Inst->LastInput = In;
	autopilot_step(reinterpret_cast<RT_MODEL_autopilot_T*>(Inst));
	return Inst->LastOutput;
#endif
}
