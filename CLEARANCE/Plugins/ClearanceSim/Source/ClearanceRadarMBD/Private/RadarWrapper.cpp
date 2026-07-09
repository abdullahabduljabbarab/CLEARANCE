#include "RadarWrapper.h"

// -----------------------------------------------------------------------
// Per-instance model state. Every wrapper owns its own RT_MODEL,
// blockIO, ExtU, and ExtY so a fleet of radar sites runs concurrently
// without shared globals. Matches the autopilot MBD pattern.
// - TripleA
// -----------------------------------------------------------------------

#if CLEARANCE_RADAR_MBD_HAVE_CODEGEN

struct FRadarInstance
{
	RT_MODEL_radar_T Model;
	B_radar_T        BlockIO;
	ExtU_radar_T     Inputs;
	ExtY_radar_T     Outputs;

	void Wire()
	{
		Model.blockIO = &BlockIO;
		Model.inputs  = &Inputs;
		Model.outputs = &Outputs;
	}
};

#else

// -----------------------------------------------------------------------
// While no generated code is present, the wrapper falls back to a
// no-op stub. Reports zero detections regardless of input. The
// module still links, downstream C++ code compiles, CLEARANCE
// keeps running. Real detections only appear once the standalone
// radar-mbd repo's codegen output drops into
// ThirdParty/RadarGenerated. - TripleA
// -----------------------------------------------------------------------
struct FRadarInstance
{
	bool bStubReady = false;
};

extern "C" void radar_initialize(RT_MODEL_radar_T* rtM)
{
	FRadarInstance* Inst = reinterpret_cast<FRadarInstance*>(rtM);
	if (Inst) { Inst->bStubReady = true; }
}

extern "C" void radar_step(RT_MODEL_radar_T* rtM)
{
	// No-op. Real DSP appears once codegen artefacts land.
	(void)rtM;
}

extern "C" void radar_terminate(RT_MODEL_radar_T* rtM)
{
	FRadarInstance* Inst = reinterpret_cast<FRadarInstance*>(rtM);
	if (Inst) { Inst->bStubReady = false; }
}

#endif // CLEARANCE_RADAR_MBD_HAVE_CODEGEN

// -----------------------------------------------------------------------
// FRadarWrapper - thin adaptor above the extern "C" surface. Owns
// exactly one FRadarInstance. Lazily initialises on first Step().
// - TripleA
// -----------------------------------------------------------------------

FRadarWrapper::FRadarWrapper() = default;

FRadarWrapper::~FRadarWrapper()
{
	Shutdown();
}

void FRadarWrapper::Initialize()
{
	if (bInitialised) { return; }

	if (!ModelState)
	{
		FRadarInstance* Inst = new FRadarInstance();
	#if CLEARANCE_RADAR_MBD_HAVE_CODEGEN
		Inst->Wire();
	#endif
		ModelState = Inst;
	}

#if CLEARANCE_RADAR_MBD_HAVE_CODEGEN
	FRadarInstance* Inst = static_cast<FRadarInstance*>(ModelState);
	radar_initialize(&Inst->Model);
#else
	radar_initialize(reinterpret_cast<RT_MODEL_radar_T*>(ModelState));
#endif
	bInitialised = true;
}

void FRadarWrapper::Shutdown()
{
	if (!bInitialised && !ModelState) { return; }

	if (ModelState)
	{
	#if CLEARANCE_RADAR_MBD_HAVE_CODEGEN
		FRadarInstance* Inst = static_cast<FRadarInstance*>(ModelState);
		if (bInitialised) { radar_terminate(&Inst->Model); }
	#else
		if (bInitialised) { radar_terminate(reinterpret_cast<RT_MODEL_radar_T*>(ModelState)); }
	#endif
		delete static_cast<FRadarInstance*>(ModelState);
		ModelState = nullptr;
	}
	bInitialised = false;
}

FClearanceRadarOutputs FRadarWrapper::Step(const FClearanceRadarInputs& In)
{
	if (!bInitialised) { Initialize(); }

	FClearanceRadarOutputs Out;
	FRadarInstance* Inst = static_cast<FRadarInstance*>(ModelState);

#if CLEARANCE_RADAR_MBD_HAVE_CODEGEN
	// Copy the I/Q data into the model's ExtU struct. Real and
	// imaginary channels come in as separate float arrays for
	// clean UE-side authoring; the model uses creal_T (a struct
	// with .re / .im) which lays out in the same order.
	constexpr int32 NCubeElems = FClearanceRadarConstants::NRangeSamples
	                           * FClearanceRadarConstants::NElements
	                           * FClearanceRadarConstants::NPulses;
	for (int32 i = 0; i < NCubeElems; ++i)
	{
		Inst->Inputs.rx_cube[i].re = In.RxCubeReal[i];
		Inst->Inputs.rx_cube[i].im = In.RxCubeImag[i];
	}
	for (int32 i = 0; i < FClearanceRadarConstants::NTxSamples; ++i)
	{
		Inst->Inputs.tx[i].re = In.TxReal[i];
		Inst->Inputs.tx[i].im = In.TxImag[i];
	}
	Inst->Inputs.look_angle_rad = In.LookAngleRad;

	radar_step(&Inst->Model);

	Out.NumDetections = static_cast<int32>(Inst->Outputs.n_detections);
	const int32 N = FMath::Min(Out.NumDetections, FClearanceRadarConstants::MaxDetections);
	for (int32 i = 0; i < N; ++i)
	{
		Out.Detections[i].RangeMetres = static_cast<float>(Inst->Outputs.det_range_m[i]);
		Out.Detections[i].VelocityMps = static_cast<float>(Inst->Outputs.det_velocity_mps[i]);
		Out.Detections[i].SnrDb       = static_cast<float>(Inst->Outputs.det_snr_dB[i]);
	}
	return Out;
#else
	// Stub build: zero detections. Real DSP appears once codegen lands.
	(void)In;
	radar_step(reinterpret_cast<RT_MODEL_radar_T*>(Inst));
	Out.NumDetections = 0;
	return Out;
#endif
}
