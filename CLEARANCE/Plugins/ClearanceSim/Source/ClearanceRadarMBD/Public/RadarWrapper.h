#pragma once

#include "CoreMinimal.h"

// -----------------------------------------------------------------------
// C linkage for the Embedded-Coder-generated radar signal processor.
// Exact names come from radar.slx's default step-function interface -
// the three below are the classic initialize / step / terminate.
//
// While the generated code is absent (fresh checkout, or the codegen
// artefacts haven't been copied into ThirdParty/RadarGenerated), the
// wrapper falls back to a pure-C++ stub so this module still links
// and CLEARANCE keeps running. When Build.cs detects the generated
// headers, CLEARANCE_RADAR_MBD_HAVE_CODEGEN flips to 1 and the
// wrapper switches to the real model. - TripleA
// -----------------------------------------------------------------------

#ifndef CLEARANCE_RADAR_MBD_HAVE_CODEGEN
#define CLEARANCE_RADAR_MBD_HAVE_CODEGEN 0
#endif

#if CLEARANCE_RADAR_MBD_HAVE_CODEGEN
extern "C"
{
	#include "radar.h"
}
#else
// Forward declaration only; concrete stub state lives in the .cpp.
struct RT_MODEL_radar_T;
extern "C"
{
	void radar_initialize(RT_MODEL_radar_T*);
	void radar_step      (RT_MODEL_radar_T*);
	void radar_terminate (RT_MODEL_radar_T*);
}
#endif

// -----------------------------------------------------------------------
// I/O contract between CLEARANCE and the generated radar model.
// Runtime dimensions match what the Simulink model was compiled for.
// Long-range profile (v2 - 810 nm unambig):
//   rx_cube : Nspp_receive x N_elements x N_pulses = 2500 x 8 x 16
//   tx      : Nspp                                = 25 (100 us at 250 kHz)
//
// MAX_DETECTIONS matches the codegen fixed-size buffer. Anything the
// model finds beyond that gets dropped (radar_step keeps the top-K
// by power). - TripleA
// -----------------------------------------------------------------------

struct FClearanceRadarConstants
{
	static constexpr int32 NRangeSamples = 2500;   // Nspp_receive = PRI * fs
	static constexpr int32 NElements     = 8;
	static constexpr int32 NPulses       = 16;
	static constexpr int32 NTxSamples    = 25;     // tau * fs = 100us * 250kHz
	static constexpr int32 MaxDetections = 16;
};

struct FClearanceRadarInputs
{
	// Steering direction (radians from broadside). Set by caller.
	double LookAngleRad = 0.0;

	// Cube of complex I/Q samples. Row-major layout matches the
	// Simulink model's [Nspp x N_el x N_pulses] port dimension.
	// Total size = 2500 * 8 * 16 * 2 doubles = ~5.12 MB per call.
	// The caller (CLEARANCE aircraft-scene synthesiser) owns
	// generating this from aircraft positions + noise. - TripleA
	double RxCubeReal[FClearanceRadarConstants::NRangeSamples
	                * FClearanceRadarConstants::NElements
	                * FClearanceRadarConstants::NPulses] = {0};
	double RxCubeImag[FClearanceRadarConstants::NRangeSamples
	                * FClearanceRadarConstants::NElements
	                * FClearanceRadarConstants::NPulses] = {0};

	// Transmit chirp reference (complex baseband), used by the
	// matched filter inside the model.
	double TxReal[FClearanceRadarConstants::NTxSamples] = {0};
	double TxImag[FClearanceRadarConstants::NTxSamples] = {0};
};

struct FClearanceRadarDetection
{
	float RangeMetres    = 0.f;
	float VelocityMps    = 0.f;   // positive = closing
	float SnrDb          = 0.f;
};

struct FClearanceRadarOutputs
{
	FClearanceRadarDetection Detections[FClearanceRadarConstants::MaxDetections];
	int32 NumDetections = 0;
};

// -----------------------------------------------------------------------
// C++ wrapper. Owns exactly one FRadarInstance so each radar site
// carries its own model state (per-CPI integrator, filter state,
// spatial-covariance history). Non-copyable to prevent accidental
// state sharing. - TripleA
// -----------------------------------------------------------------------

class CLEARANCERADARMBD_API FRadarWrapper
{
public:
	FRadarWrapper();
	~FRadarWrapper();

	FRadarWrapper(const FRadarWrapper&)            = delete;
	FRadarWrapper& operator=(const FRadarWrapper&) = delete;

	void Initialize();
	void Shutdown();

	// One CPI: push inputs, step, read detections. Safe to call
	// before Initialize() - lazy-inits so scenario startup ordering
	// doesn't have to be exact. - TripleA
	FClearanceRadarOutputs Step(const FClearanceRadarInputs& In);

	// True when compiled against real generated code (as opposed to
	// the stub). Callers can branch on this to fall back to the
	// hand-rolled radar model when Simulink codegen isn't available.
	static constexpr bool HasGeneratedCode()
	{
		return CLEARANCE_RADAR_MBD_HAVE_CODEGEN != 0;
	}

private:
	bool  bInitialised = false;
	void* ModelState   = nullptr;   // Opaque - concrete type in .cpp
};
