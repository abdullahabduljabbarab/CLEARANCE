#pragma once

// Monostatic radar range equation and receiver noise model, in the shape
// used by CLEARANCE's UClearanceRadar detection loop. Pure C++ - no UE
// headers - so the same math is testable standalone under the CI
// pipeline and exercisable inside the editor via automation tests.
//
// Reference: Skolnik, "Introduction to Radar Systems", 3rd ed., ch. 2-4.
// The equation form here is the classic monostatic pulse-radar equation
// with a single-pulse SNR and a Swerling-0 (non-fluctuating target)
// detection curve approximated as a logistic around the required SNR.
// Full Marcum-Q integration would sharpen the curve slightly but the
// difference is imperceptible against sim-time noise + operator jitter.
// - TripleA

#include <cmath>
#include <cstdint>

namespace ClearanceRadarEquation
{
	// -----------------------------------------------------------------------
	// Physical constants
	// -----------------------------------------------------------------------

	// Boltzmann's constant (J/K). Sets thermal noise floor kTBF.
	inline constexpr double kBoltzmann = 1.380649e-23;

	// Speed of light in vacuum (m/s). Sets wavelength lambda = c / f.
	inline constexpr double kSpeedOfLight = 2.99792458e8;

	// Nautical mile in metres, so callers using the sim's nm-native
	// distance can convert without pulling in another header.
	inline constexpr double kMetresPerNauticalMile = 1852.0;

	// -----------------------------------------------------------------------
	// dB <-> linear conversion. Used pervasively; kept inline. - TripleA
	// -----------------------------------------------------------------------

	inline double LinearFromDb(double Db) noexcept
	{
		return std::pow(10.0, Db * 0.1);
	}

	inline double DbFromLinear(double Linear) noexcept
	{
		// Guard log10(0). Returning a large negative dB is the right sentinel
		// for "no signal" - it never wraps to nonsense positive numbers.
		if (Linear <= 0.0) { return -1000.0; }
		return 10.0 * std::log10(Linear);
	}

	// -----------------------------------------------------------------------
	// Wavelength lambda (m) from frequency f (Hz). lambda = c / f.
	// -----------------------------------------------------------------------
	inline double WavelengthM(double FrequencyHz) noexcept
	{
		if (FrequencyHz <= 0.0) { return 0.0; }
		return kSpeedOfLight / FrequencyHz;
	}

	// -----------------------------------------------------------------------
	// Monostatic radar range equation (Skolnik eq. 2.1). Returns the power
	// (watts) that comes back at the receive antenna from a point target
	// at slant range R.
	//
	//   Pr = (Pt * Gt * Gr * lambda^2 * sigma)  /  ((4*pi)^3 * R^4 * L)
	//
	// All gains linear (not dB). All units SI. Loss L is a linear >= 1
	// factor rolling up antenna feedline, atmospheric absorption, and any
	// signal-processing degradation you want to model as one lump.
	// Returns 0 for R <= 0 to avoid the singularity. - TripleA
	// -----------------------------------------------------------------------
	inline double ReceivedPowerWatts(
		double PeakPowerWatts,
		double TransmitGainLinear,
		double ReceiveGainLinear,
		double WavelengthMetres,
		double RcsSquareMetres,
		double RangeMetres,
		double SystemLossLinear) noexcept
	{
		if (RangeMetres <= 0.0)      { return 0.0; }
		if (SystemLossLinear <= 0.0) { return 0.0; }

		constexpr double kFourPi = 4.0 * 3.14159265358979323846;
		const double Denominator =
			kFourPi * kFourPi * kFourPi
			* RangeMetres * RangeMetres * RangeMetres * RangeMetres
			* SystemLossLinear;

		return (PeakPowerWatts
			* TransmitGainLinear * ReceiveGainLinear
			* WavelengthMetres * WavelengthMetres
			* RcsSquareMetres) / Denominator;
	}

	// -----------------------------------------------------------------------
	// Thermal noise floor at the receiver: N = k * T * B * F.
	//   k = Boltzmann
	//   T = system noise temperature (K)
	//   B = receiver noise bandwidth (Hz)
	//   F = noise figure (linear, not dB)
	// -----------------------------------------------------------------------
	inline double NoiseFloorWatts(
		double SystemNoiseTemperatureKelvin,
		double ReceiverBandwidthHz,
		double NoiseFigureLinear) noexcept
	{
		if (SystemNoiseTemperatureKelvin <= 0.0) { return 0.0; }
		if (ReceiverBandwidthHz <= 0.0)          { return 0.0; }
		if (NoiseFigureLinear <= 0.0)            { return 0.0; }
		return kBoltzmann * SystemNoiseTemperatureKelvin
			* ReceiverBandwidthHz * NoiseFigureLinear;
	}

	// -----------------------------------------------------------------------
	// Single-pulse SNR (linear ratio). Divide received power by noise floor.
	// -----------------------------------------------------------------------
	inline double SnrLinear(double ReceivedPowerWatts_, double NoiseFloorWatts_) noexcept
	{
		if (NoiseFloorWatts_ <= 0.0) { return 0.0; }
		return ReceivedPowerWatts_ / NoiseFloorWatts_;
	}

	// -----------------------------------------------------------------------
	// Probability of detection given single-pulse SNR (dB). We use a
	// logistic centred on the minimum-detectable SNR with a configurable
	// slope. This is a smooth approximation to the classic Swerling-0
	// non-fluctuating-target curve - not a physics-accurate Marcum-Q
	// integral. Chosen because:
	//   - it's monotonic (a stronger signal never lowers Pd),
	//   - it's C^1 smooth (the coverage overlay renders without banding),
	//   - it has exactly two intuitive knobs the designer can tune
	//     (centre = required SNR, slope = "how sharp is the cliff?").
	//
	// SlopeDb is the "1/e" width in dB - larger = softer transition.
	// Typical value 3-6 dB for a search radar.
	// - TripleA
	// -----------------------------------------------------------------------
	inline double ProbabilityOfDetection(
		double SnrDb,
		double RequiredSnrDb,
		double SlopeDb) noexcept
	{
		if (SlopeDb <= 0.0) { SlopeDb = 1.0; }
		const double x = (SnrDb - RequiredSnrDb) / SlopeDb;
		// Numerically stable logistic: split by sign so exp() never overflows.
		if (x >= 0.0)
		{
			const double e = std::exp(-x);
			return 1.0 / (1.0 + e);
		}
		const double e = std::exp(x);
		return e / (1.0 + e);
	}

	// -----------------------------------------------------------------------
	// Convenience: full one-shot detection probability from the raw inputs.
	// Wraps the equation above so callers don't repeat the plumbing at
	// every call site. - TripleA
	// -----------------------------------------------------------------------
	struct FDetectionInputs
	{
		double PeakPowerWatts             = 1.4e6;      // 1.4 MW peak - ASR-9 class civil surveillance
		double TransmitGainDb             = 34.0;
		double ReceiveGainDb              = 34.0;
		double SystemLossDb               = 6.0;        // feedline + processing + atmospheric roll-up
		double FrequencyHz                = 2.8e9;      // S-band, 2.7-2.9 GHz airport surveillance band
		double NoiseFigureDb              = 3.0;
		double SystemNoiseTemperatureK    = 290.0;      // IEEE reference temperature
		double ReceiverBandwidthHz        = 1.0e6;      // matched to ~1 us pulse (B ~ 1/tau)
		double RequiredSnrDb              = 13.0;       // Swerling-0 for Pd = 0.9, Pfa = 1e-6
		double DetectionSlopeDb           = 4.0;
		double RcsSquareMetres            = 10.0;       // typical airliner side-on
		double RangeMetres                = 1852.0;     // 1 nm sanity default
	};

	struct FDetectionResult
	{
		double ReceivedPowerWatts = 0.0;
		double NoiseFloorWatts    = 0.0;
		double SnrLinear          = 0.0;
		double SnrDb              = -1000.0;
		double ProbabilityOfDetection = 0.0;
	};

	inline FDetectionResult ComputeDetection(const FDetectionInputs& In) noexcept
	{
		FDetectionResult Out;

		const double Lambda = WavelengthM(In.FrequencyHz);
		const double GtLin  = LinearFromDb(In.TransmitGainDb);
		const double GrLin  = LinearFromDb(In.ReceiveGainDb);
		const double LLin   = LinearFromDb(In.SystemLossDb);
		const double FLin   = LinearFromDb(In.NoiseFigureDb);

		Out.ReceivedPowerWatts = ReceivedPowerWatts(
			In.PeakPowerWatts,
			GtLin, GrLin,
			Lambda,
			In.RcsSquareMetres,
			In.RangeMetres,
			LLin);

		Out.NoiseFloorWatts = NoiseFloorWatts(
			In.SystemNoiseTemperatureK,
			In.ReceiverBandwidthHz,
			FLin);

		Out.SnrLinear = SnrLinear(Out.ReceivedPowerWatts, Out.NoiseFloorWatts);
		Out.SnrDb     = DbFromLinear(Out.SnrLinear);

		Out.ProbabilityOfDetection = ProbabilityOfDetection(
			Out.SnrDb, In.RequiredSnrDb, In.DetectionSlopeDb);

		return Out;
	}

	// -----------------------------------------------------------------------
	// Typical target RCS by rough aircraft class (square metres). These are
	// order-of-magnitude side-on values from open literature, not radar-
	// scattering-lab data - the point is that a 747 paints differently
	// from a Cessna, which lets the coverage overlay tell a truthful story
	// and gives EW / stealth handling something meaningful to modulate.
	// - TripleA
	// -----------------------------------------------------------------------
	inline constexpr double kRcsSuper     = 200.0;   // A380, An-225 - massive airframe
	inline constexpr double kRcsHeavy     = 100.0;   // 747, 777, A350 - widebody
	inline constexpr double kRcsMedium    =  10.0;   // 737, A320 - narrowbody
	inline constexpr double kRcsLight     =   1.0;   // Cessna, GA
	inline constexpr double kRcsMilitary  =   5.0;   // fourth-gen fighter, side-on
	inline constexpr double kRcsStealth   =   0.1;   // F-35 / F-22 open-source estimate
}
