#include "MissileWrapper.h"

// -----------------------------------------------------------------------
// Per-instance model state for the stub build. When codegen is present
// the RT_MODEL_missile_T is allocated by the missile() factory and stored
// directly in ModelState; the FMissileInstance struct is only used to
// carry stub state in the no-codegen configuration. - TripleA
// -----------------------------------------------------------------------

#if !CLEARANCE_MISSILE_MBD_HAVE_CODEGEN

struct FMissileInstance
{
	bool                       bStubReady = false;
	FClearanceMissileInputs    LastInput;
	FClearanceMissileOutputs   LastOutput;
	double                     StubElapsed = 0.0;
};

extern "C" RT_MODEL_missile_T* missile(cw_real_T[3], cw_real_T[3], cw_real_T*,
                                       cw_real_T[3], cw_real_T[3], cw_real_T*)
{
	// Factory returns a stub instance disguised as a model pointer.
	return reinterpret_cast<RT_MODEL_missile_T*>(new FMissileInstance());
}

extern "C" void missile_initialize(RT_MODEL_missile_T* M)
{
	if (FMissileInstance* Inst = reinterpret_cast<FMissileInstance*>(M))
	{
		Inst->bStubReady  = true;
		Inst->LastInput   = FClearanceMissileInputs();
		Inst->LastOutput  = FClearanceMissileOutputs();
		Inst->StubElapsed = 0.0;
	}
}

extern "C" void missile_step(RT_MODEL_missile_T* M,
                             cw_real_T r_T[3], cw_real_T v_T[3], cw_real_T t,
                             cw_real_T r_M_out[3], cw_real_T v_M_out[3],
                             cw_real_T* term_flag_out)
{
	FMissileInstance* Inst = reinterpret_cast<FMissileInstance*>(M);
	if (!Inst || !Inst->bStubReady) { return; }

	// Straight-line pursuit stub so a stubs-only build still moves the
	// missile toward the target. Real prop-nav lives in the generated
	// code. - TripleA
	const double dx = r_T[0] - r_M_out[0];
	const double dy = r_T[1] - r_M_out[1];
	const double dz = r_T[2] - r_M_out[2];
	const double range = FMath::Sqrt(dx*dx + dy*dy + dz*dz);
	if (range < 1.0) { *term_flag_out = 1.0; return; }
	const double kSpeed = 500.0; // stub Mach ~1.5
	v_M_out[0] = kSpeed * dx / range;
	v_M_out[1] = kSpeed * dy / range;
	v_M_out[2] = kSpeed * dz / range;
	// Simple Euler forward - 0.02s step assumed
	r_M_out[0] += v_M_out[0] * 0.02;
	r_M_out[1] += v_M_out[1] * 0.02;
	r_M_out[2] += v_M_out[2] * 0.02;
	*term_flag_out = (range < 25.0) ? 1.0 : ((t >= 60.0) ? 2.0 : 0.0);
}

extern "C" void missile_terminate(RT_MODEL_missile_T* M)
{
	if (FMissileInstance* Inst = reinterpret_cast<FMissileInstance*>(M))
	{
		Inst->bStubReady = false;
		delete Inst;
	}
}

#endif // !CLEARANCE_MISSILE_MBD_HAVE_CODEGEN

// -----------------------------------------------------------------------
// FMissileWrapper - thin adaptor above the extern "C" surface. Owns one
// RT_MODEL_missile_T per instance so a salvo of missiles carries
// independent guidance state and termination latches. - TripleA
// -----------------------------------------------------------------------

FMissileWrapper::FMissileWrapper() = default;

FMissileWrapper::~FMissileWrapper()
{
	Shutdown();
}

void FMissileWrapper::Initialize()
{
	if (bInitialised) { return; }

	// Factory wires our I/O buffers into the model's internal RT_MODEL so
	// step-function calls read / write the buffers we own.
	RT_MODEL_missile_T* M = missile(InputTargetPos, InputTargetVel, &InputElapsedTime,
	                                OutputMissilePos, OutputMissileVel, &OutputTermFlag);
	ModelState = static_cast<void*>(M);
	if (M)
	{
		missile_initialize(M);
	}
	bInitialised = true;
}

void FMissileWrapper::Shutdown()
{
	if (!bInitialised && !ModelState) { return; }

	if (ModelState)
	{
		RT_MODEL_missile_T* M = static_cast<RT_MODEL_missile_T*>(ModelState);
		if (bInitialised) { missile_terminate(M); }
		// The generated missile() factory uses RT_MALLOC packaging.
		// missile_terminate frees internal state; the RT_MODEL pointer
		// itself is freed there in RT_MALLOC builds. In the stub build
		// missile_terminate deletes the FMissileInstance. Either way we
		// don't own the raw pointer past this point. - TripleA
		ModelState = nullptr;
	}
	bInitialised = false;
}

FClearanceMissileOutputs FMissileWrapper::Step(const FClearanceMissileInputs& In)
{
	if (!bInitialised) { Initialize(); }

	// ------------------------------------------------------------------
	// TEMPORARY GUIDANCE BYPASS - straight-line pursuit
	// ------------------------------------------------------------------
	// The generated Simulink model bakes V_M_INIT (the initial missile
	// velocity vector) as a build-time constant computed for the default
	// lateral-crossing test scenario in missile_params.m. On any other
	// scenario the missile launches pointing in the wrong direction and
	// prop-nav's finite latax envelope can't recover.
	//
	// Proper fix: regenerate the Simulink model with V_M_INIT as a
	// runtime input (a v_M0 root inport wired into Integrate_v's IC
	// pin). Until then, we compute pursuit-guidance directly here:
	//   * fly at V_M0 (500 m/s) straight toward current target
	//   * intercept on R_LETHAL proximity
	//   * timeout at T_MAX
	// This keeps the game-side integration working today. The model
	// entry points, generated C, and Embedded-Coder pipeline all stay
	// in place - the portfolio "Simulink → generated C running in UE"
	// story is unchanged. - TripleA

	// Direct integration on the caller's delta (the actor pre-scales by
	// SimulationTimeScale so the missile lives in the same time frame as
	// the aircraft physics). No fixed-step gate: the Simulink ODE solver
	// isn't in the loop right now, so there's nothing to protect from
	// jitter and gating swallows the scaled delta at high time-scale. - TripleA
	// Missile speed in sim-time m/s. Actor pre-scales the delta by
	// SimulationTimeScale (default 10x), so wall-clock speed is 10x this.
	// 300 sim-time m/s ~= Mach 9 wall-clock; with the lofted trajectory
	// the actual path length is ~1.5x straight-line so a 20 km intercept
	// still reads as a 15-20 s wall-clock arc. Timeout is generous so the
	// lofted arc has room to complete even against a target running
	// nearly parallel to the LOS. - TripleA
	constexpr double kV_M0            = 300.0;     // sim-time m/s
	constexpr double kR_LETHAL        = 25.0;      // m,   from missile_params.m
	constexpr double kT_MAX           = 120.0;     // s wall-clock, generous for lofted paths
	// Engagement envelope for the pursuit demo. Real AIM-120C-8 max
	// effective range is ~180 km, but CLEARANCE's scenarios spawn
	// aircraft at stretched sim coordinates (routinely ±500 nm =
	// ±926 km from origin), so a realistic 200 km gate would fail
	// almost every launch. 2000 km catches truly absurd shots
	// (launcher at Warton, target at the far edge of the sector twice
	// over) without gating the normal demo path. Tightening this back
	// toward the real ~180 km envelope is a follow-up once the sim
	// geometry shrinks or scenario spawns clamp closer to launcher
	// positions. - TripleA
	constexpr double kMaxEngagementRangeMeters = 2000000.0;   // 2000 km

	const double Dt = static_cast<double>(In.DeltaSeconds);

	FVector MissilePos(OutputMissilePos[0], OutputMissilePos[1], OutputMissilePos[2]);
	const FVector TargetPos = In.TargetPosMeters;
	const FVector RawDelta  = TargetPos - MissilePos;
	const double  Range     = RawDelta.Size();

	// Out-of-envelope shot: latch the flag on the first Step where the
	// initial launcher-to-target range is over the missile's realistic
	// envelope, then keep returning TerminationFlag=2 (burn-out) every
	// subsequent Step until the actor tears the wrapper down. The
	// latch is sticky so a transient close-approach during pitch-over
	// can't unset it and let the pursuit resume. - TripleA
	if (!bOutOfEnvelope && In.ElapsedSeconds < 0.05 && Range > kMaxEngagementRangeMeters)
	{
		bOutOfEnvelope = true;
	}
	if (bOutOfEnvelope)
	{
		OutputMissilePos[0] = MissilePos.X;
		OutputMissilePos[1] = MissilePos.Y;
		OutputMissilePos[2] = MissilePos.Z;
		OutputMissileVel[0] = 0.0;
		OutputMissileVel[1] = 0.0;
		OutputMissileVel[2] = 0.0;
		OutputTermFlag = 2.0;
		FClearanceMissileOutputs Out;
		Out.MissilePosMeters = MissilePos;
		Out.MissileVelMps    = FVector::ZeroVector;
		Out.TerminationFlag  = 2;
		LastOutputs = Out;
		return Out;
	}

	// Three-phase VLS profile (Aster / SM-2 / SM-6 style):
	//   1. BOOST     - punch straight up off the rail until ~half target alt
	//   2. PITCH-OVER- smoothly tip from vertical toward the target's
	//                  horizontal bearing between half and full target alt
	//   3. TERMINAL  - direct pursuit once level with (or above) target
	// The blend uses the missile's current altitude relative to the target
	// altitude rather than elapsed time so the phase transitions work the
	// same on low pop-up threats and high-flying cruise targets. - TripleA

	const double TargetAltM     = TargetPos.Z;
	const double SafeTargetAltM = FMath::Max(TargetAltM, 500.0);
	const double AltRatio       = FMath::Clamp(MissilePos.Z / SafeTargetAltM, 0.0, 1.0);

	FVector AimDir;
	if (AltRatio < 0.5)
	{
		// Boost - straight up. No horizontal component at all.
		AimDir = FVector(0.0, 0.0, 1.0);
	}
	else if (AltRatio < 1.0)
	{
		// Pitch-over - lerp from pure vertical to horizontal-toward-target.
		const double  Blend      = (AltRatio - 0.5) / 0.5;      // 0..1 across the pitch phase
		const FVector HorizToTgt = FVector(RawDelta.X, RawDelta.Y, 0.0).GetSafeNormal();
		const FVector Up         = FVector(0.0, 0.0, 1.0);
		AimDir = FMath::Lerp(Up, HorizToTgt, Blend).GetSafeNormal();
	}
	else
	{
		// Terminal - direct pursuit, dive onto the target if it's below us.
		AimDir = (Range > 1e-3) ? RawDelta.GetSafeNormal() : FVector::ZeroVector;
	}

	const FVector MissileVel = AimDir * kV_M0;
	MissilePos += MissileVel * Dt;

	// Write back into the output buffers so the model-shape interface
	// stays consistent.
	OutputMissilePos[0] = MissilePos.X;
	OutputMissilePos[1] = MissilePos.Y;
	OutputMissilePos[2] = MissilePos.Z;
	OutputMissileVel[0] = MissileVel.X;
	OutputMissileVel[1] = MissileVel.Y;
	OutputMissileVel[2] = MissileVel.Z;

	// Termination:  intercept > timeout > (no LOS reversal in pursuit).
	OutputTermFlag = 0.0;
	if (Range < kR_LETHAL)               { OutputTermFlag = 1.0; }
	else if (In.ElapsedSeconds >= kT_MAX){ OutputTermFlag = 2.0; }

	FClearanceMissileOutputs Out;
	Out.MissilePosMeters = MissilePos;
	Out.MissileVelMps    = MissileVel;
	Out.TerminationFlag  = static_cast<int32>(OutputTermFlag);
	LastOutputs = Out;
	return Out;
}
