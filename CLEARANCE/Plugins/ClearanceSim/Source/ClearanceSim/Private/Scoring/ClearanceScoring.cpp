#include "Scoring/ClearanceScoring.h"

UClearanceScoring::UClearanceScoring()
{
	CurrentSpawnIntervalSeconds = BaseSpawnIntervalSeconds;
}

void UClearanceScoring::LogIncident(EIncidentType Type, FName AircraftA, FName AircraftB, const FString& Details)
{
	FIncidentRecord Record;
	Record.Type = Type;
	Record.AircraftA = AircraftA;
	Record.AircraftB = AircraftB;
	Record.TimeStamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	Record.Details = Details;
	IncidentLog.Add(Record);

	switch (Type)
	{
	case EIncidentType::SuccessfulLanding:    ++TotalLandings;   ++TotalHandled; break;
	case EIncidentType::SuccessfulDeparture:  ++TotalDepartures; ++TotalHandled; break;
	case EIncidentType::SuccessfulEmergencyHandling: ++TotalHandled; break;
	case EIncidentType::GoAroundTriggered:    ++TotalGoArounds;  break;
	case EIncidentType::SeparationLoss:       ++TotalSeparationLosses; ++TotalFailures; break;
	case EIncidentType::UnresolvedExit:
	case EIncidentType::MissedHandoff:
	case EIncidentType::WakeEncounter:
	case EIncidentType::TCASResolutionAdvisory:
	case EIncidentType::MisidentifiedCivilian:
	case EIncidentType::ViolationZoneBreached:
	case EIncidentType::AircraftCrashed:
	case EIncidentType::RestrictedAirspaceBust:++TotalFailures;   break;
	default: break;
	}

	CurrentScore += PointsForIncident(Type);
	OnScoreUpdated.Broadcast(CurrentScore);

	AdjustDifficulty();
}

void UClearanceScoring::RecordInstruction()
{
	++TotalInstructions;
}

float UClearanceScoring::GetEfficiency() const
{
	// Share of resolved situations that went well. No traffic yet = perfect.
	const int32 Outcomes = TotalHandled + TotalFailures + TotalGoArounds;
	if (Outcomes == 0)
	{
		return 1.f;
	}
	return FMath::Clamp(static_cast<float>(TotalHandled) / static_cast<float>(Outcomes), 0.f, 1.f);
}

void UClearanceScoring::ResetSession()
{
	IncidentLog.Reset();
	CurrentScore = 0;
	TotalLandings = TotalDepartures = TotalGoArounds = 0;
	TotalSeparationLosses = TotalInstructions = TotalHandled = TotalFailures = 0;
	CurrentSpawnIntervalSeconds = BaseSpawnIntervalSeconds;

	OnScoreUpdated.Broadcast(CurrentScore);
	OnDifficultyAdjusted.Broadcast(CurrentSpawnIntervalSeconds);
}

int32 UClearanceScoring::PointsForIncident(EIncidentType Type) const
{
	switch (Type)
	{
	case EIncidentType::SuccessfulLanding:     return PointsLanding;
	case EIncidentType::SuccessfulDeparture:   return PointsDeparture;
	case EIncidentType::SuccessfulResolution:  return PointsResolution;
	case EIncidentType::SuccessfulIntercept:   return PointsIntercept;
	case EIncidentType::SeparationLoss:        return -PenaltySeparationLoss;
	case EIncidentType::GoAroundTriggered:     return -PenaltyGoAround;
	case EIncidentType::UnresolvedExit:        return -PenaltyUnresolvedExit;
	case EIncidentType::MissedHandoff:         return -PenaltyMissedHandoff;
	case EIncidentType::LateInstruction:       return -PenaltyLateInstruction;
	case EIncidentType::WakeEncounter:         return -PenaltyWakeEncounter;
	case EIncidentType::TCASResolutionAdvisory:return -PenaltyTCASResolutionAdvisory;
	case EIncidentType::MisidentifiedCivilian: return -PenaltyMisidentifiedCivilian;
	case EIncidentType::ViolationZoneBreached: return -PenaltyViolationZoneBreached;
	case EIncidentType::SuccessfulEmergencyHandling: return PointsEmergencyHandled;
	case EIncidentType::AircraftCrashed:       return -PenaltyAircraftCrashed;
	case EIncidentType::RestrictedAirspaceBust:return -PenaltyRestrictedAirspaceBust;
	default:                                   return 0;
	}
}

void UClearanceScoring::AdjustDifficulty()
{
	const float NewInterval = FMath::Clamp(
		BaseSpawnIntervalSeconds - TotalHandled * DifficultySecondsPerHandled,
		MinSpawnIntervalSeconds, MaxSpawnIntervalSeconds);

	if (!FMath::IsNearlyEqual(NewInterval, CurrentSpawnIntervalSeconds))
	{
		CurrentSpawnIntervalSeconds = NewInterval;
		OnDifficultyAdjusted.Broadcast(CurrentSpawnIntervalSeconds);
	}
}
