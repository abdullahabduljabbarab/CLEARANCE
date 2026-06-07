#include "Safety/ClearanceRadar.h"
#include "Airspace/ClearanceAirspaceManager.h"

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

	// Paint anything inside range that the beam just crossed. Range + bearing are
	// computed RELATIVE to the radar's site position so multiple sites at different
	// locations cover different airspace patches. - TripleA
	const TArray<FAircraftState> Truth = Manager->GetAllAircraftStates();
	for (const FAircraftState& T : Truth)
	{
		const FVector2D Rel = FVector2D(T.Position.X, T.Position.Y) - SitePositionNm;
		const float DistNm = Rel.Size();
		if (DistNm > RangeNm) { continue; }

		const float Bearing = BearingDeg(FVector(Rel.X, Rel.Y, 0.f));
		if (!SweepCrossed(SweepPrevDeg, SweepAngleDeg, Bearing)) { continue; }

		// Beam hit it - update (or initialise) the track.
		FRadarTrack& Trk = Tracks.FindOrAdd(T.Callsign);
		const bool bSecondary = FMath::FRand() <= SecondaryReturnChance;
		const FVector JitterOffset(
			FMath::FRandRange(-PositionJitterNm, PositionJitterNm),
			FMath::FRandRange(-PositionJitterNm, PositionJitterNm),
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
		Trk.Confidence        = 1.f;
	}

	// Fade tracks that haven't been painted recently; drop the dead ones.
	TArray<FName> Dead;
	for (TPair<FName, FRadarTrack>& Pair : Tracks)
	{
		FRadarTrack& Trk = Pair.Value;
		const float Since = Now - Trk.LastPaintTime;
		Trk.Confidence = FMath::Clamp(1.f - (Since / FMath::Max(0.1f, TrackFadeSeconds)), 0.f, 1.f);
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
