#include "Safety/ClearanceConflictDetector.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Core/ClearanceConstants.h"

namespace
{
	bool MoreSevere(EAlertLevel A, EAlertLevel B)
	{
		return static_cast<uint8>(A) > static_cast<uint8>(B);
	}

	bool IsLandingPhase(EFlightPhase Phase)
	{
		return Phase == EFlightPhase::Approach || Phase == EFlightPhase::Landing;
	}
}

void UClearanceConflictDetector::SetReferences(AClearanceAirspaceManager* InManager)
{
	Manager = InManager;
}

void UClearanceConflictDetector::DetectConflicts()
{
	if (!Manager)
	{
		return;
	}

	const TArray<FAircraftState> States = Manager->GetAllAircraftStates();
	const float Now = Manager->GetWorld() ? Manager->GetWorld()->GetTimeSeconds() : 0.f;

	TSet<FString> SeenConflicts;
	TSet<FString> SeenWake;
	TMap<FName, float> WakeFollowersThisPass;

	for (int32 i = 0; i < States.Num(); ++i)
	{
		for (int32 j = i + 1; j < States.Num(); ++j)
		{
			const FAircraftState& A = States[i];
			const FAircraftState& B = States[j];

			// Missiles are inert wrt the civilian safety net - a SAM racing
			// toward its target must not trigger TCAS / wake / separation
			// alerts on either the missile or the target. Skip any pair that
			// has a missile on either side. - TripleA
			if (A.bIsMissile || B.bIsMissile)
			{
				continue;
			}

			// Engagement pairs - suppress the civilian safety net (sep alerts, TCAS,
			// wake) so the operator isn't penalised for the intercept itself:
			//   * Hostile/Unknown-involved - any contact GCI is actively tracking
			//   * Both GCI-controlled       - vipers flying tight formation on each other
			//   * GCI vs hijack             - shadow fighters tailing a 7500 aircraft
			// Mixed pairs (GCI-controlled fighter vs ordinary civilian) STAY alerted:
			// keeping civilians clear of the engagement is the GCI controller's job.
			// Unknown contacts are suppressed too - the operator can't talk to them,
			// they're a GCI problem, civilian TCAS/wake/sep penalties don't apply.
			// We look at BOTH ThreatClass (what the operator sees) and TrueAffiliation
			// (the god-view truth). A Scandinavian probe that the operator hasn't
			// declared yet still shows as Neutral on the scope, but its truth is
			// Hostile - and vipers scrambled against it must not trigger TCAS
			// penalties for closing on their assigned target. - TripleA
			const bool bThreatInvolved = (A.ThreatClass == EThreatClass::Hostile || B.ThreatClass == EThreatClass::Hostile
			                            || A.ThreatClass == EThreatClass::Unknown || B.ThreatClass == EThreatClass::Unknown
			                            || A.TrueAffiliation == EThreatClass::Hostile || B.TrueAffiliation == EThreatClass::Hostile
			                            || A.TrueAffiliation == EThreatClass::Unknown || B.TrueAffiliation == EThreatClass::Unknown);
			const bool bBothUnderGCI    = (A.bUnderGCIControl && B.bUnderGCIControl);
			const bool bShadowPair      = (A.bUnderGCIControl && B.ActiveEmergency == EEmergencyType::Hijack)
			                           || (B.bUnderGCIControl && A.ActiveEmergency == EEmergencyType::Hijack);
			if (bThreatInvolved || bBothUnderGCI || bShadowPair)
			{
				continue;
			}

			const float HorizNow = HorizontalSeparationNm(A, B);
			const float VertNow = VerticalSeparationFt(A, B);
			EAlertLevel Level = AlertFromSeparation(HorizNow, VertNow);

			// Projected look-ahead: if they're clear now but their tracks bring
			// them into conflict soon, flag it as an early advisory. - TripleA
			if (Level == EAlertLevel::None && ProjectionLookaheadSeconds > 0.f)
			{
				FAircraftState Ap = A;
				FAircraftState Bp = B;
				Ap.Position += A.Velocity * ProjectionLookaheadSeconds;
				Bp.Position += B.Velocity * ProjectionLookaheadSeconds;
				Ap.Altitude += A.ClimbRate * (ProjectionLookaheadSeconds / 60.f);
				Bp.Altitude += B.ClimbRate * (ProjectionLookaheadSeconds / 60.f);

				if (AlertFromSeparation(HorizontalSeparationNm(Ap, Bp), VerticalSeparationFt(Ap, Bp)) != EAlertLevel::None)
				{
					Level = EAlertLevel::Advisory;
				}
			}

			const FString Key = PairKey(A.Callsign, B.Callsign);

			if (Level != EAlertLevel::None)
			{
				SeenConflicts.Add(Key);

				FConflictEvent Event;
				Event.AircraftA = A.Callsign;
				Event.AircraftB = B.Callsign;
				Event.HorizontalSeparationNm = HorizNow;
				Event.VerticalSeparationFt = VertNow;
				Event.AlertLevel = Level;
				Event.TimeOfDetection = Now;
				Event.bRequiresGoAround = (Level == EAlertLevel::Critical) && (IsLandingPhase(A.FlightPhase) || IsLandingPhase(B.FlightPhase));

				FConflictEvent* Prev = ActiveConflicts.Find(Key);
				const bool bNew = (Prev == nullptr);
				const bool bEscalated = Prev && MoreSevere(Level, Prev->AlertLevel);

				ActiveConflicts.Add(Key, Event);

				if (bNew || bEscalated)
				{
					OnConflictDetected.Broadcast(Event);

					if (Event.bRequiresGoAround)
					{
						// the aircraft on approach is the one told to go around
						const FName GoArounder = IsLandingPhase(A.FlightPhase) ? A.Callsign : B.Callsign;
						OnGoAroundRequired.Broadcast(GoArounder);
					}
				}

				// Imminent collision -> TCAS Resolution Advisory: the higher aircraft climbs,
				// the lower descends. Fires ONCE per encounter; cleared when the pair stops
				// being in any conflict, so it can fire again on a fresh encounter. - TripleA
				if (Level == EAlertLevel::Critical && !ActiveTCAS.Contains(Key))
				{
					const FAircraftState* Climber = nullptr;
					const FAircraftState* Descender = nullptr;
					if (A.Altitude > B.Altitude) { Climber = &A; Descender = &B; }
					else if (B.Altitude > A.Altitude) { Climber = &B; Descender = &A; }
					else // same altitude - deterministic split by callsign so both sides agree
					{
						if (A.Callsign.ToString() <= B.Callsign.ToString()) { Climber = &A; Descender = &B; }
						else { Climber = &B; Descender = &A; }
					}
					const float ClimbTo = Climber->Altitude + 1500.f;
					const float DescendTo = FMath::Max(0.f, Descender->Altitude - 1500.f);
					ActiveTCAS.Add(Key);
					OnTCASResolutionAdvisory.Broadcast(Climber->Callsign, Descender->Callsign, ClimbTo, DescendTo);
				}
			}

			// Wake turbulence is a separate concern: a lighter aircraft trailing a
			// heavier one too closely, regardless of the separation alert above.
			const uint8 RankA = static_cast<uint8>(A.WakeCategory);
			const uint8 RankB = static_cast<uint8>(B.WakeCategory);

			const FAircraftState* Leader;
			const FAircraftState* Follower;
			if (RankA > RankB) { Leader = &A; Follower = &B; }
			else if (RankB > RankA) { Leader = &B; Follower = &A; }
			else { if (FollowerIsBehindLeader(A, B)) { Leader = &A; Follower = &B; } else { Leader = &B; Follower = &A; } }

			const float Required = RequiredWakeSeparationNm(Leader->WakeCategory, Follower->WakeCategory);
			const bool bInTrail = FollowerIsBehindLeader(*Leader, *Follower);
			const bool bViolation = bInTrail
				&& HorizontalSeparationNm(*Leader, *Follower) < Required
				&& VerticalSeparationFt(*Leader, *Follower) < ClearanceConstants::VerticalMinimumFt;

			const FString WakeKey = PairKey(A.Callsign, B.Callsign);
			if (bViolation)
			{
				SeenWake.Add(WakeKey);
				// Intensity scales with how much heavier the leader is than the follower:
				// (leaderRank - followerRank + 1)/4, so Light-behind-Super ~1.0 and
				// Heavy-behind-Heavy ~0.25. - TripleA
				const int32 LeadRank = static_cast<int32>(Leader->WakeCategory);
				const int32 FollowRank = static_cast<int32>(Follower->WakeCategory);
				const float Intensity = FMath::Clamp((LeadRank - FollowRank + 1) / 4.f, 0.25f, 1.f);
				WakeFollowersThisPass.Add(Follower->Callsign, Intensity);
				if (!ActiveWakeAdvisories.Contains(WakeKey))
				{
					ActiveWakeAdvisories.Add(WakeKey);
					OnWakeTurbulenceAdvisory.Broadcast(Follower->Callsign, Leader->Callsign, Required);
				}
			}
		}
	}

	// Conflicts no longer present this pass have resolved.
	TArray<FString> Resolved;
	for (const TPair<FString, FConflictEvent>& Pair : ActiveConflicts)
	{
		if (!SeenConflicts.Contains(Pair.Key))
		{
			OnConflictResolved.Broadcast(Pair.Value);
			Resolved.Add(Pair.Key);
		}
	}
	for (const FString& Key : Resolved)
	{
		ActiveConflicts.Remove(Key);
	}

	// Wake advisories clear silently once separation is restored.
	ActiveWakeAdvisories = ActiveWakeAdvisories.Intersect(SeenWake);
	WakeAffected = MoveTemp(WakeFollowersThisPass);
	// TCAS state clears once the pair is fully out of conflict, so a fresh encounter
	// can re-fire the RA. - TripleA
	ActiveTCAS = ActiveTCAS.Intersect(SeenConflicts);
}

bool UClearanceConflictDetector::IsInWakeTurbulence(FName Callsign) const
{
	return WakeAffected.Contains(Callsign);
}

float UClearanceConflictDetector::GetWakeIntensity(FName Callsign) const
{
	const float* Found = WakeAffected.Find(Callsign);
	return Found ? *Found : 0.f;
}

EAlertLevel UClearanceConflictDetector::GetAlertLevelFor(FName Callsign) const
{
	EAlertLevel Highest = EAlertLevel::None;
	for (const TPair<FString, FConflictEvent>& Pair : ActiveConflicts)
	{
		const FConflictEvent& Event = Pair.Value;
		if ((Event.AircraftA == Callsign || Event.AircraftB == Callsign) && MoreSevere(Event.AlertLevel, Highest))
		{
			Highest = Event.AlertLevel;
		}
	}
	return Highest;
}

void UClearanceConflictDetector::RemoveAircraft(FName Callsign)
{
	TArray<FString> ToRemove;
	for (const TPair<FString, FConflictEvent>& Pair : ActiveConflicts)
	{
		if (Pair.Value.AircraftA == Callsign || Pair.Value.AircraftB == Callsign)
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FString& Key : ToRemove)
	{
		ActiveConflicts.Remove(Key);
		ActiveWakeAdvisories.Remove(Key);
		ActiveTCAS.Remove(Key);
	}
}

float UClearanceConflictDetector::HorizontalSeparationNm(const FAircraftState& A, const FAircraftState& B)
{
	return FVector2D::Distance(FVector2D(A.Position.X, A.Position.Y), FVector2D(B.Position.X, B.Position.Y));
}

float UClearanceConflictDetector::VerticalSeparationFt(const FAircraftState& A, const FAircraftState& B)
{
	return FMath::Abs(A.Altitude - B.Altitude);
}

EAlertLevel UClearanceConflictDetector::AlertFromSeparation(float HorizNm, float VertFt)
{
	// Stacked vertically by 1000 ft or more is safe no matter how close laterally.
	if (VertFt >= ClearanceConstants::VerticalMinimumFt)
	{
		return EAlertLevel::None;
	}

	if (HorizNm < ClearanceConstants::CriticalHorizontalNm) return EAlertLevel::Critical;
	if (HorizNm < ClearanceConstants::WarningHorizontalNm)  return EAlertLevel::Warning;
	if (HorizNm < ClearanceConstants::AdvisoryHorizontalNm) return EAlertLevel::Advisory;
	return EAlertLevel::None;
}

FString UClearanceConflictDetector::PairKey(FName A, FName B)
{
	const FString SA = A.ToString();
	const FString SB = B.ToString();
	return (SA <= SB) ? (SA + TEXT("|") + SB) : (SB + TEXT("|") + SA);
}

bool UClearanceConflictDetector::FollowerIsBehindLeader(const FAircraftState& Leader, const FAircraftState& Follower)
{
	const float HeadingRad = FMath::DegreesToRadians(Leader.Heading);
	const FVector2D LeaderDir(FMath::Sin(HeadingRad), FMath::Cos(HeadingRad)); // ENU: X=E, Y=N
	const FVector2D ToFollower(Follower.Position.X - Leader.Position.X, Follower.Position.Y - Leader.Position.Y);
	return FVector2D::DotProduct(LeaderDir, ToFollower) < 0.f; // behind = opposite the nose
}

float UClearanceConflictDetector::RequiredWakeSeparationNm(EWakeCategory Leader, EWakeCategory Follower)
{
	using namespace ClearanceConstants;

	// Docs specify the matrix up to Heavy; treat Super as Heavy for now. - TripleA
	auto Normalise = [](EWakeCategory C) { return C == EWakeCategory::Super ? EWakeCategory::Heavy : C; };
	const EWakeCategory L = Normalise(Leader);
	const EWakeCategory F = Normalise(Follower);

	if (L == EWakeCategory::Heavy && F == EWakeCategory::Light)  return WakeLightBehindHeavyNm;
	if (L == EWakeCategory::Heavy && F == EWakeCategory::Medium) return WakeMediumBehindHeavyNm;
	if (L == EWakeCategory::Heavy && F == EWakeCategory::Heavy)  return WakeHeavyBehindHeavyNm;
	if (L == EWakeCategory::Medium && F == EWakeCategory::Light) return WakeLightBehindMediumNm;
	return WakeStandardMinimumNm;
}
