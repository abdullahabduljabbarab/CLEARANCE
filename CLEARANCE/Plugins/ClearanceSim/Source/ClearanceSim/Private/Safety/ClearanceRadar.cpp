#include "Safety/ClearanceRadar.h"
#include "Safety/ClearanceRadarEquation.h"
#include "Airspace/ClearanceAirspaceManager.h"

namespace
{
	// Map an aircraft's wake category to a nominal radar cross-section
	// (m^2). Numbers per ClearanceRadarEquation.h constants; kept in
	// this TU so the radar tick doesn't have to reason about aircraft
	// taxonomy. - TripleA
	double RcsForWakeCategory(EWakeCategory Cat)
	{
		switch (Cat)
		{
			case EWakeCategory::Super:  return ClearanceRadarEquation::kRcsSuper;
			case EWakeCategory::Heavy:  return ClearanceRadarEquation::kRcsHeavy;
			case EWakeCategory::Medium: return ClearanceRadarEquation::kRcsMedium;
			case EWakeCategory::Light:  return ClearanceRadarEquation::kRcsLight;
		}
		return ClearanceRadarEquation::kRcsMedium;
	}
}

void UClearanceRadar::SetReferences(AClearanceAirspaceManager* InManager)
{
	Manager = InManager;
}

void UClearanceRadar::SetEnabled(bool bInEnabled)
{
	bEnabled = bInEnabled;
	if (!bEnabled) { Tracks.Empty(); SweepAngleDeg = 0.f; SweepPrevDeg = 0.f; RadarClockSeconds = 0.0; }
}

void UClearanceRadar::Reset()
{
	Tracks.Empty();
	SweepAngleDeg = 0.f;
	SweepPrevDeg = 0.f;
	RadarClockSeconds = 0.0;
}

float UClearanceRadar::BearingDeg(const FVector& PosNm)
{
	float Deg = FMath::RadiansToDegrees(FMath::Atan2(PosNm.X, PosNm.Y)); // X=East, Y=North
	if (Deg < 0.f) { Deg += 360.f; }
	return Deg;
}

bool UClearanceRadar::SweepCrossed(float Prev, float Now, float BearingDeg)
{
	if (Now >= Prev)
	{
		return BearingDeg > Prev && BearingDeg <= Now;
	}
	// Wrapped past 360.
	return BearingDeg > Prev || BearingDeg <= Now;
}

void UClearanceRadar::Tick(float RealDeltaSeconds)
{
	if (!bEnabled || !Manager || RealDeltaSeconds <= 0.f) { return; }

	RadarClockSeconds += RealDeltaSeconds;
	const float Now = static_cast<float>(RadarClockSeconds);

	// Advance the sweep antenna. Kept updating even under Simulink DSP
	// so the visual sweep bar on the scope still rotates - the sweep
	// itself doesn't drive detection when Simulink is on. - TripleA
	SweepPrevDeg = SweepAngleDeg;
	const float DegPerSec = SweepRpm * 6.f; // 1 rpm = 6 deg/sec
	SweepAngleDeg = FMath::Fmod(SweepAngleDeg + DegPerSec * RealDeltaSeconds, 360.f);

	// Simulink DSP branch - runs the auto-generated radar model at
	// SimulinkStepIntervalSeconds cadence and replaces the Tracks map
	// with its detections. Skips the analytic sweep entirely; the
	// visual sweep bar still rotates via the code above. - TripleA
	if (bUseSimulinkDSP)
	{
		if (RadarClockSeconds - LastSimulinkStepSeconds >= SimulinkStepIntervalSeconds)
		{
			UpdateTracksFromSimulinkDSP(Now);
			LastSimulinkStepSeconds = RadarClockSeconds;
		}

		// Chaff clouds paint as a cluster of low-confidence ghost blips
		// (a "bloom") near each cloud centre. The Simulink DSP path skips
		// modelling chaff physics inside the I/Q cube because a chaff
		// cloud radiates back thousands of individual dipole reflectors
		// that would need per-particle Doppler + range spread - way
		// heavier than aircraft synthesis for a purely visual feature.
		// Instead we emit N ghosts directly, matching the analytic
		// path's behaviour but with more particles per cloud so it
		// visibly blooms on scope. - TripleA
		constexpr int32 GhostsPerCloud = 5;
		const TArray<FChaffCloud> Clouds = Manager->GetActiveChaffClouds();
		for (const FChaffCloud& C : Clouds)
		{
			const FVector2D Rel = FVector2D(C.PositionNm.X, C.PositionNm.Y) - SitePositionNm;
			if (Rel.Size() > RangeNm) { continue; }
			for (int32 k = 0; k < GhostsPerCloud; ++k)
			{
				const FName GhostKey(*FString::Printf(TEXT("GHOST_%d_%d"), GetTypeHash(C.DropSessionTime), k));
				FRadarTrack& Trk = Tracks.FindOrAdd(GhostKey);
				Trk.TruthCallsign   = GhostKey;
				Trk.DisplayCallsign = NAME_None;
				Trk.Position        = C.PositionNm + FVector(FMath::FRandRange(-1.2f, 1.2f), FMath::FRandRange(-1.2f, 1.2f), 0.f);
				Trk.Altitude        = FMath::RoundToFloat(C.AltitudeFt / 200.f) * 200.f;
				Trk.Heading         = 0.f;
				Trk.Speed           = 0.f;
				Trk.bHasSecondary   = false;
				Trk.LastPaintTime   = Now;
				Trk.PaintConfidence = 0.4f;
				Trk.Confidence      = Trk.PaintConfidence;
			}
		}

		// Fade only kicks in AFTER the CPI grace period expires. Inside
		// the grace, blips hold their paint confidence steady - between
		// CPI updates the track is not stale, it's just quiescent.
		// Otherwise every tick would nibble the confidence and the
		// scope would visibly pulse at the CPI cadence. - TripleA
		const float GraceSeconds = SimulinkStepIntervalSeconds * 1.5f;
		TArray<FName> Dead;
		for (TPair<FName, FRadarTrack>& Pair : Tracks)
		{
			FRadarTrack& Trk = Pair.Value;
			const float Since = Now - Trk.LastPaintTime;
			if (Since <= GraceSeconds)
			{
				// Fresh - keep confidence at the paint value
				Trk.Confidence = Trk.PaintConfidence;
			}
			else
			{
				const float FadeSince = Since - GraceSeconds;
				const float Freshness = FMath::Clamp(1.f - (FadeSince / FMath::Max(0.1f, TrackFadeSeconds)), 0.f, 1.f);
				Trk.Confidence = Trk.PaintConfidence * Freshness;
				if (Trk.Confidence <= 0.f) { Dead.Add(Pair.Key); }
			}
		}
		for (const FName& K : Dead) { Tracks.Remove(K); }
		return;
	}

	const TArray<FAircraftState> Truth = Manager->GetAllAircraftStates();

	// EW pass 1: figure out which bearing wedges from THIS radar are blanketed
	// by an active jammer. Anything in that wedge gets degraded reads even if
	// it isn't itself jamming - that's how a single jammer denies an arc to
	// one radar without taking the whole sensor net offline. - TripleA
	struct FJamWedge { float CentreBearing; float HalfWidth; };
	TArray<FJamWedge> JamWedges;
	for (const FAircraftState& T : Truth)
	{
		if (!T.bJammingOn) { continue; }
		const FVector2D Rel = FVector2D(T.Position.X, T.Position.Y) - SitePositionNm;
		if (Rel.Size() > RangeNm * 1.4f) { continue; } // out of range to interfere
		FJamWedge W;
		W.CentreBearing = BearingDeg(FVector(Rel.X, Rel.Y, 0.f));
		W.HalfWidth = 12.f; // +/- 12 deg arc washed out by the jammer
		JamWedges.Add(W);
	}
	auto InAnyJamWedge = [&](float Bearing) -> bool
	{
		for (const FJamWedge& W : JamWedges)
		{
			float Delta = FMath::Abs(FMath::FindDeltaAngleDegrees(W.CentreBearing, Bearing));
			if (Delta <= W.HalfWidth) { return true; }
		}
		return false;
	};

	// Precompute the parts of the radar equation that don't vary per-target,
	// so the inner loop touches only range + RCS. This is Skolnik's classic
	// monostatic pulse-radar equation - see ClearanceRadarEquation.h.
	// - TripleA
	ClearanceRadarEquation::FDetectionInputs Physics;
	Physics.PeakPowerWatts          = static_cast<double>(PeakPowerKilowatts) * 1000.0;
	Physics.TransmitGainDb          = TransmitAntennaGainDb;
	Physics.ReceiveGainDb           = ReceiveAntennaGainDb;
	Physics.SystemLossDb            = SystemLossDb;
	Physics.NoiseFigureDb           = NoiseFigureDb;
	Physics.SystemNoiseTemperatureK = SystemNoiseTemperatureK;
	Physics.ReceiverBandwidthHz     = static_cast<double>(ReceiverBandwidthMhz) * 1.0e6;
	Physics.RequiredSnrDb           = MinimumDetectableSnrDb;
	Physics.DetectionSlopeDb        = DetectionSlopeDb;
	// Prefer the radar's own emission-signature frequency so ELINT PDUs and
	// detection math agree; fall back to S-band if unset. - TripleA
	const double LowHz  = EmissionSignature.FrequencyLowHz;
	const double HighHz = EmissionSignature.FrequencyHighHz;
	if (LowHz > 0.0 && HighHz >= LowHz) { Physics.FrequencyHz = 0.5 * (LowHz + HighHz); }

	// Paint anything the beam just crossed at which the radar-equation Pd
	// clears a random-uniform threshold. Range + bearing are computed
	// RELATIVE to the radar's site position so multiple sites at different
	// locations cover different airspace patches. - TripleA
	for (const FAircraftState& T : Truth)
	{
		const FVector2D Rel = FVector2D(T.Position.X, T.Position.Y) - SitePositionNm;
		const float DistNm = Rel.Size();

		const float Bearing = BearingDeg(FVector(Rel.X, Rel.Y, 0.f));
		if (!SweepCrossed(SweepPrevDeg, SweepAngleDeg, Bearing)) { continue; }

		const bool bJammedHere = T.bJammingOn || InAnyJamWedge(Bearing);

		// Detection decision. Physics path rolls Pd from the radar equation
		// (target RCS, wavelength, kTBF noise, R^4 fall-off). Legacy path is
		// the old binary distance gate + flat confidence, kept behind
		// bUsePhysicsDetection for A/B testing. - TripleA
		float PaintBaseConfidence = 1.f;
		if (bUsePhysicsDetection)
		{
			Physics.RangeMetres = static_cast<double>(DistNm) * ClearanceRadarEquation::kMetresPerNauticalMile;
			Physics.RcsSquareMetres = RcsForWakeCategory(T.WakeCategory);
			const auto Det = ClearanceRadarEquation::ComputeDetection(Physics);
			double Pd = Det.ProbabilityOfDetection;
			// Jamming eats detection headroom - model as a straight Pd
			// multiplier so operators see a clean fusion gradient across
			// the sensor net rather than a discrete jammed / not-jammed step.
			if (bJammedHere) { Pd *= 0.25; }
			if (FMath::FRand() > static_cast<float>(Pd)) { continue; }
			PaintBaseConfidence = static_cast<float>(FMath::Clamp(Pd, 0.0, 1.0));
		}
		else
		{
			if (DistNm > RangeNm) { continue; }
			PaintBaseConfidence = bJammedHere ? 0.25f : 1.f;
		}

		// Beam hit it - update (or initialise) the track.
		FRadarTrack& Trk = Tracks.FindOrAdd(T.Callsign);
		// Jammed paints lose the transponder more often AND get much fatter jitter.
		const float JamSecondaryPenalty = bJammedHere ? 0.4f : 0.f;
		const float JamJitterMul        = bJammedHere ? 8.f : 1.f;
		const bool bSecondary = FMath::FRand() <= (SecondaryReturnChance - JamSecondaryPenalty);
		const FVector JitterOffset(
			FMath::FRandRange(-PositionJitterNm, PositionJitterNm) * JamJitterMul,
			FMath::FRandRange(-PositionJitterNm, PositionJitterNm) * JamJitterMul,
			0.f);

		Trk.TruthCallsign     = T.Callsign;
		Trk.DisplayCallsign   = bSecondary ? T.Callsign : NAME_None;
		Trk.Position          = T.Position + JitterOffset;
		// Primary-only altitudes are rougher (no Mode C); quantise to 200ft.
		Trk.Altitude          = bSecondary ? T.Altitude : (FMath::RoundToFloat(T.Altitude / 200.f) * 200.f);
		Trk.Heading           = T.Heading;
		Trk.Speed             = T.Speed;
		Trk.bHasSecondary     = bSecondary;
		Trk.LastPaintTime     = Now;
		// Fusion across multiple sensors is the whole point - one sensor
		// paints at 0.4 (long-range low-RCS), another at 0.9 (short-range
		// high-RCS), max-fusion still gives the operator a solid track. - TripleA
		Trk.PaintConfidence   = PaintBaseConfidence;
		Trk.Confidence        = Trk.PaintConfidence;
	}

	// EW pass 2: chaff clouds in range get painted as low-confidence ghost
	// tracks with no transponder. They stay until they age out of the
	// manager's list (which trims them by lifetime). - TripleA
	const TArray<FChaffCloud> Clouds = Manager->GetActiveChaffClouds();
	for (const FChaffCloud& C : Clouds)
	{
		const FVector2D Rel = FVector2D(C.PositionNm.X, C.PositionNm.Y) - SitePositionNm;
		if (Rel.Size() > RangeNm) { continue; }
		const float Bearing = BearingDeg(FVector(Rel.X, Rel.Y, 0.f));
		if (!SweepCrossed(SweepPrevDeg, SweepAngleDeg, Bearing)) { continue; }

		// Stable synthetic callsign per cloud so the same chaff cloud paints to
		// the same ghost entry each sweep instead of multiplying. - TripleA
		const FName GhostKey(*FString::Printf(TEXT("GHOST_%d"), GetTypeHash(C.DropSessionTime)));
		FRadarTrack& Trk = Tracks.FindOrAdd(GhostKey);
		Trk.TruthCallsign   = GhostKey;
		Trk.DisplayCallsign = NAME_None;
		Trk.Position        = C.PositionNm + FVector(FMath::FRandRange(-0.3f, 0.3f), FMath::FRandRange(-0.3f, 0.3f), 0.f);
		Trk.Altitude        = FMath::RoundToFloat(C.AltitudeFt / 200.f) * 200.f;
		Trk.Heading         = 0.f;
		Trk.Speed           = 0.f;
		Trk.bHasSecondary   = false;
		Trk.LastPaintTime   = Now;
		Trk.PaintConfidence = 0.35f;
		Trk.Confidence      = Trk.PaintConfidence;
	}

	// Fade tracks that haven't been painted recently; drop the dead ones. The
	// freshness factor multiplies the paint-time confidence so a jammed read
	// of 0.25 doesn't snap back to 1.0 on the next tick. - TripleA
	TArray<FName> Dead;
	for (TPair<FName, FRadarTrack>& Pair : Tracks)
	{
		FRadarTrack& Trk = Pair.Value;
		const float Since = Now - Trk.LastPaintTime;
		const float Freshness = FMath::Clamp(1.f - (Since / FMath::Max(0.1f, TrackFadeSeconds)), 0.f, 1.f);
		Trk.Confidence = Trk.PaintConfidence * Freshness;
		if (Trk.Confidence <= 0.f) { Dead.Add(Pair.Key); }
	}
	for (const FName& K : Dead) { Tracks.Remove(K); }
}

TArray<FRadarTrack> UClearanceRadar::GetTracks() const
{
	TArray<FRadarTrack> Out;
	Out.Reserve(Tracks.Num());
	for (const TPair<FName, FRadarTrack>& Pair : Tracks) { Out.Add(Pair.Value); }
	return Out;
}

// -----------------------------------------------------------------------
// Simulink DSP integration - synthesise an I/Q cube from the live
// airspace state, run one radar_step, match the detections back to
// aircraft callsigns by (aliased range, Doppler) nearest-neighbour,
// and populate the Tracks map with the matched entries. Every field
// that would normally be filled by the analytic sweep is populated
// from the matched aircraft state so downstream UI code (data blocks,
// leader lines, fusion tags) works unchanged.
// -----------------------------------------------------------------------

namespace ClearanceRadarMBD_Internal
{
	// Long-range surveillance profile - see radar_repo/model/waveform_params.m
	// NSpp_receive = PRI * fs = 10 ms * 250 kHz = 2500 (unchanged).
	// NTx         = tau * fs = 100 us * 250 kHz = 25.
	// R_unamb     = c * PRI / 2 = 1500 km ~ 810 nm.
	// v_unamb     = lambda / (4 * PRI) = ~2.7 m/s - Doppler aliases, we
	//              match on range only in the scoring below. - TripleA
	constexpr int32  NSpp        = FClearanceRadarConstants::NRangeSamples;   // 2500
	constexpr int32  NEl         = FClearanceRadarConstants::NElements;       // 8
	constexpr int32  NPulses     = FClearanceRadarConstants::NPulses;         // 16
	constexpr int32  NTx         = FClearanceRadarConstants::NTxSamples;      // 25
	constexpr double FsHz        = 250.0e3;
	constexpr double PriSec      = 10.0e-3;
	constexpr double TauSec      = 100.0e-6;
	constexpr double BwHz        = 100.0e3;
	constexpr double Fc          = 2.8e9;
	constexpr double C_MPS       = 2.99792458e8;
	constexpr double LambdaM     = C_MPS / Fc;
	constexpr double ElemSpacing = 0.5 * LambdaM;
	constexpr double PI2         = 6.283185307179586;
	constexpr double NoiseStd    = 1.0;
	constexpr double RUnambM     = C_MPS * PriSec * 0.5;                       // ~1500 km
	constexpr double RangeBinM   = C_MPS / (2.0 * FsHz);                       // 600 m per bin

	FORCEINLINE int32 CubeIdx(int32 r, int32 e, int32 p)
	{
		return (p * NEl + e) * NSpp + r;
	}
	FORCEINLINE double ElementX(int32 ElemIdx)
	{
		return (static_cast<double>(ElemIdx) - 0.5 * (NEl - 1)) * ElemSpacing;
	}
	FORCEINLINE void GaussianComplex(double& OutRe, double& OutIm)
	{
		const double u1 = FMath::Max(1e-12, FMath::FRand());
		const double u2 = FMath::FRand();
		const double r  = FMath::Sqrt(-2.0 * FMath::Loge(u1));
		OutRe = r * FMath::Cos(PI2 * u2) / FMath::Sqrt(2.0);
		OutIm = r * FMath::Sin(PI2 * u2) / FMath::Sqrt(2.0);
	}
}

void UClearanceRadar::UpdateTracksFromSimulinkDSP(float NowSeconds)
{
	using namespace ClearanceRadarMBD_Internal;

	if (!Manager) { return; }

	const TArray<FAircraftState> Truth = Manager->GetAllAircraftStates();

	// ---- Build target geometries relative to this site ----
	struct FGeom
	{
		FAircraftState Ac;
		double AliasedRangeM = 0.0;
		double AngleRad      = 0.0;
		double VelocityMps   = 0.0;
	};
	TArray<FGeom> Geoms;
	Geoms.Reserve(Truth.Num());

	for (const FAircraftState& Ac : Truth)
	{
		const FVector2D RelNm = FVector2D(Ac.Position.X, Ac.Position.Y) - SitePositionNm;
		const double RangeM  = FMath::Max(0.0, static_cast<double>(RelNm.Size()) * 1852.0);
		if (RangeM <= 0.0) { continue; }

		FGeom G;
		G.Ac            = Ac;
		G.AliasedRangeM = FMath::Fmod(RangeM, RUnambM);
		G.AngleRad      = FMath::Atan2(static_cast<double>(RelNm.X), static_cast<double>(RelNm.Y));

		// Radial velocity (positive closing). Ac.Velocity in nm/s.
		const FVector2D VelMpsVec = FVector2D(Ac.Velocity.X, Ac.Velocity.Y) * 1852.0;
		const double UnitX = (static_cast<double>(RelNm.X) * 1852.0) / RangeM;
		const double UnitY = (static_cast<double>(RelNm.Y) * 1852.0) / RangeM;
		G.VelocityMps = -(static_cast<double>(VelMpsVec.X) * UnitX + static_cast<double>(VelMpsVec.Y) * UnitY);

		Geoms.Add(G);
	}

	// ---- Synthesise I/Q cube ----
	FClearanceRadarInputs In;
	In.LookAngleRad = 0.0;

	// LFM chirp reference into tx
	const double ChirpRate = BwHz / TauSec;
	TArray<double> ChirpRe; ChirpRe.SetNumUninitialized(NTx);
	TArray<double> ChirpIm; ChirpIm.SetNumUninitialized(NTx);
	for (int32 k = 0; k < NTx; ++k)
	{
		const double t = static_cast<double>(k) / FsHz;
		const double phi = PI2 * (-0.5 * BwHz * t + 0.5 * ChirpRate * t * t);
		ChirpRe[k] = FMath::Cos(phi);
		ChirpIm[k] = FMath::Sin(phi);
		In.TxReal[k] = ChirpRe[k];
		In.TxImag[k] = ChirpIm[k];
	}

	// Zero cube then add every target's contribution
	constexpr int32 CubeSize = NSpp * NEl * NPulses;
	FMemory::Memzero(In.RxCubeReal, sizeof(double) * CubeSize);
	FMemory::Memzero(In.RxCubeImag, sizeof(double) * CubeSize);

	for (const FGeom& G : Geoms)
	{
		const int32 DelaySamples = static_cast<int32>(FMath::RoundToInt(2.0 * G.AliasedRangeM / C_MPS * FsHz));
		if (DelaySamples < 0 || DelaySamples + NTx > NSpp) { continue; }

		const double FDoppler = 2.0 * G.VelocityMps / LambdaM;

		for (int32 p = 0; p < NPulses; ++p)
		{
			const double DopPhase = PI2 * FDoppler * static_cast<double>(p) * PriSec;
			const double DopRe = FMath::Cos(DopPhase);
			const double DopIm = FMath::Sin(DopPhase);

			for (int32 e = 0; e < NEl; ++e)
			{
				const double SteerPhase = PI2 * ElementX(e) * FMath::Sin(G.AngleRad) / LambdaM;
				const double SteerRe = FMath::Cos(SteerPhase);
				const double SteerIm = FMath::Sin(SteerPhase);
				const double WRe = DopRe * SteerRe - DopIm * SteerIm;
				const double WIm = DopRe * SteerIm + DopIm * SteerRe;

				for (int32 k = 0; k < NTx; ++k)
				{
					const double TxRe = ChirpRe[k];
					const double TxIm = ChirpIm[k];
					const double AmpRe = WRe * TxRe - WIm * TxIm;
					const double AmpIm = WRe * TxIm + WIm * TxRe;
					const int32 Idx = CubeIdx(DelaySamples + k, e, p);
					In.RxCubeReal[Idx] += AmpRe;
					In.RxCubeImag[Idx] += AmpIm;
				}
			}
		}
	}

	// Per-element AWGN
	for (int32 p = 0; p < NPulses; ++p)
	{
		for (int32 e = 0; e < NEl; ++e)
		{
			for (int32 r = 0; r < NSpp; ++r)
			{
				double NRe, NIm;
				GaussianComplex(NRe, NIm);
				const int32 Idx = CubeIdx(r, e, p);
				In.RxCubeReal[Idx] += NoiseStd * NRe;
				In.RxCubeImag[Idx] += NoiseStd * NIm;
			}
		}
	}

	// ---- Run the model ----
	const FClearanceRadarOutputs Out = SimulinkWrapper.Step(In);

	// ---- Match each detection to the nearest aircraft ----
	// Track which callsigns have already been claimed so we don't
	// map two CFAR sidelobe hits from the same target to different
	// aircraft. - TripleA
	TSet<FName> Claimed;
	for (int32 i = 0; i < Out.NumDetections; ++i)
	{
		const float DetR = Out.Detections[i].RangeMetres;

		// Long-range profile: velocity aliases into a tiny +/-2.7 m/s
		// window so Doppler is unusable for matching. Score on range
		// only, normalised to one range bin. - TripleA
		double BestScore = TNumericLimits<double>::Max();
		int32  BestIdx   = INDEX_NONE;
		for (int32 g = 0; g < Geoms.Num(); ++g)
		{
			if (Claimed.Contains(Geoms[g].Ac.Callsign)) { continue; }
			const double DR = FMath::Abs(Geoms[g].AliasedRangeM - static_cast<double>(DetR)) / RangeBinM;
			const double S  = DR * DR;
			if (S < BestScore) { BestScore = S; BestIdx = g; }
		}
		if (BestIdx == INDEX_NONE) { continue; }

		// Reject far matches - if score > 4 range cells, it's noise, not target.
		if (BestScore > 16.0) { continue; }

		const FGeom& Match = Geoms[BestIdx];
		Claimed.Add(Match.Ac.Callsign);

		FRadarTrack& Trk = Tracks.FindOrAdd(Match.Ac.Callsign);
		Trk.TruthCallsign   = Match.Ac.Callsign;
		Trk.DisplayCallsign = Match.Ac.Callsign;    // Simulink DSP produces "secondary-quality" tags
		Trk.Position        = Match.Ac.Position;    // ground-truth 2D position for scope display
		Trk.Altitude        = Match.Ac.Altitude;
		Trk.Heading         = Match.Ac.Heading;
		Trk.Speed           = Match.Ac.Speed;
		Trk.bHasSecondary   = !Match.Ac.bJammingOn;   // jammer loses its transponder tag
		Trk.LastPaintTime   = NowSeconds;
		// Any CFAR-passing detection paints the blip at (near-)full brightness.
		// Scaling brightness linearly with per-CPI SNR made the blip visibly
		// pulse at 2 Hz as the noise realisation shifted the raw SNR reading
		// - PPI scopes don't do that. Fade after the grace window still
		// handles the "lost contact" case separately. - TripleA
		float Base = FMath::Clamp(Out.Detections[i].SnrDb / 40.f, 0.9f, 1.f);
		// Self-jamming: the aircraft radiating a noise jammer masks its own
		// return in the receiver, so its blip dims. Every other aircraft on
		// the scope is unaffected. - TripleA
		if (Match.Ac.bJammingOn) { Base *= 0.3f; }
		Trk.PaintConfidence = Base;
		Trk.Confidence      = Trk.PaintConfidence;
	}
}
