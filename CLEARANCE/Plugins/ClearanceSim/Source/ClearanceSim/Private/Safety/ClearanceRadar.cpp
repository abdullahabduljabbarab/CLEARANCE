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

	// Advance the sweep antenna.
	SweepPrevDeg = SweepAngleDeg;
	const float DegPerSec = SweepRpm * 6.f; // 1 rpm = 6 deg/sec
	SweepAngleDeg = FMath::Fmod(SweepAngleDeg + DegPerSec * RealDeltaSeconds, 360.f);

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
