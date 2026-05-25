#include "Airspace/ClearanceAirspaceManager.h"
#include "Core/ClearanceConstants.h"

AClearanceAirspaceManager::AClearanceAirspaceManager()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AClearanceAirspaceManager::BeginPlay()
{
	Super::BeginPlay();

	SectorEnvironment.WindDirection = DefaultWindDirection;
	SectorEnvironment.WindSpeed = DefaultWindSpeed;
	SectorEnvironment.AvailableRunways = AvailableRunwayHeadings;
	SectorEnvironment.ActiveRunwayHeading = -1.f; // forces the first pick below

	RecalculateActiveRunway();
}

bool AClearanceAirspaceManager::RegisterAircraft(const FAircraftState& NewAircraft)
{
	if (!ValidateState(NewAircraft))
	{
		return false;
	}

	if (AircraftStates.Contains(NewAircraft.Callsign))
	{
		return false; // already in the sector - don't clobber the live state
	}

	if (AircraftStates.Num() >= MaxAircraftCount)
	{
		return false;
	}

	FAircraftState State = NewAircraft;
	ClampStateValues(State);
	State.bIsValid = true;
	State.TimeEnteredSector = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	AircraftStates.Add(State.Callsign, State);
	OnAircraftRegistered.Broadcast(State.Callsign);
	return true;
}

bool AClearanceAirspaceManager::DeregisterAircraft(FName Callsign)
{
	if (AircraftStates.Remove(Callsign) > 0)
	{
		OnAircraftDeregistered.Broadcast(Callsign);
		return true;
	}
	return false;
}

bool AClearanceAirspaceManager::RequestStateUpdate(const FAircraftState& UpdatedState)
{
	FAircraftState* Existing = AircraftStates.Find(UpdatedState.Callsign);
	if (!Existing)
	{
		return false; // nothing to update - reject rather than silently create one - TripleA
	}

	if (!ValidateState(UpdatedState))
	{
		return false;
	}

	FAircraftState State = UpdatedState;
	ClampStateValues(State);
	State.bIsValid = true;
	State.TimeEnteredSector = Existing->TimeEnteredSector; // registration-time fact, not the Behaviour's to change

	*Existing = State;
	OnAircraftStateUpdated.Broadcast(State.Callsign);
	return true;
}

FAircraftState AClearanceAirspaceManager::GetAircraftState(FName Callsign) const
{
	if (const FAircraftState* Found = AircraftStates.Find(Callsign))
	{
		return *Found;
	}
	return FAircraftState(); // bIsValid stays false
}

TArray<FAircraftState> AClearanceAirspaceManager::GetAllAircraftStates() const
{
	TArray<FAircraftState> Out;
	AircraftStates.GenerateValueArray(Out);
	return Out;
}

int32 AClearanceAirspaceManager::GetAircraftCount() const
{
	return AircraftStates.Num();
}

bool AClearanceAirspaceManager::IsCallsignRegistered(FName Callsign) const
{
	return AircraftStates.Contains(Callsign);
}

void AClearanceAirspaceManager::ClearAllAircraft()
{
	TArray<FName> Callsigns;
	AircraftStates.GenerateKeyArray(Callsigns);

	AircraftStates.Empty();

	for (const FName& Callsign : Callsigns)
	{
		OnAircraftDeregistered.Broadcast(Callsign);
	}
}

FSectorEnvironment AClearanceAirspaceManager::GetCurrentEnvironment() const
{
	return SectorEnvironment;
}

void AClearanceAirspaceManager::InitialiseEnvironment(float WindDir, float WindSpeed, const TArray<float>& Runways)
{
	SectorEnvironment.WindDirection = FMath::Fmod(WindDir, 360.f);
	if (SectorEnvironment.WindDirection < 0.f)
	{
		SectorEnvironment.WindDirection += 360.f;
	}
	SectorEnvironment.WindSpeed = FMath::Max(0.f, WindSpeed);
	// Always have something to choose from so the selection actually runs - default
	// to a single 09/27 strip (usable both directions). - TripleA
	SectorEnvironment.AvailableRunways = (Runways.Num() > 0) ? Runways : TArray<float>({ 90.f, 270.f });
	SectorEnvironment.ActiveRunwayHeading = -1.f; // force a fresh pick
	RecalculateActiveRunway();
}

void AClearanceAirspaceManager::UpdateWindConditions(float NewWindDirection, float NewWindSpeed)
{
	SectorEnvironment.WindDirection = FMath::Fmod(NewWindDirection, 360.f);
	if (SectorEnvironment.WindDirection < 0.f)
	{
		SectorEnvironment.WindDirection += 360.f;
	}
	SectorEnvironment.WindSpeed = FMath::Max(0.f, NewWindSpeed);

	RecalculateActiveRunway();
}

float AClearanceAirspaceManager::GetActiveRunway() const
{
	return SectorEnvironment.ActiveRunwayHeading;
}

bool AClearanceAirspaceManager::ValidateState(const FAircraftState& State) const
{
	if (State.Callsign.IsNone())
	{
		return false;
	}
	if (!FMath::IsFinite(State.Altitude) || !FMath::IsFinite(State.Speed) || !FMath::IsFinite(State.Heading))
	{
		return false;
	}
	return true;
}

void AClearanceAirspaceManager::ClampStateValues(FAircraftState& State) const
{
	State.Altitude = FMath::Clamp(State.Altitude, MinSafeAltitude, MaxSafeAltitude);
	State.Speed = FMath::Clamp(State.Speed, MinSafeSpeed, MaxSafeSpeed);

	State.Heading = FMath::Fmod(State.Heading, 360.f);
	if (State.Heading < 0.f)
	{
		State.Heading += 360.f;
	}
}

void AClearanceAirspaceManager::RecalculateActiveRunway()
{
	if (SectorEnvironment.AvailableRunways.Num() == 0)
	{
		return;
	}

	// Crosswind for a runway is wind speed times the sine of the angle between
	// the wind and the runway heading; the best runway is the one that minimises
	// it. The dead-band below stops the active runway flip-flopping when the wind
	// sits right on the boundary between two options - TripleA
	const float Wind = SectorEnvironment.WindDirection;

	auto Crosswind = [&](float RunwayHeading)
	{
		const float Delta = FMath::DegreesToRadians(Wind - RunwayHeading);
		return SectorEnvironment.WindSpeed * FMath::Abs(FMath::Sin(Delta));
	};

	float BestRunway = SectorEnvironment.AvailableRunways[0];
	float BestCrosswind = Crosswind(BestRunway);
	for (int32 i = 1; i < SectorEnvironment.AvailableRunways.Num(); ++i)
	{
		const float ThisCrosswind = Crosswind(SectorEnvironment.AvailableRunways[i]);
		if (ThisCrosswind < BestCrosswind)
		{
			BestCrosswind = ThisCrosswind;
			BestRunway = SectorEnvironment.AvailableRunways[i];
		}
	}

	const bool bHasCurrent = SectorEnvironment.ActiveRunwayHeading >= 0.f;
	if (bHasCurrent && BestRunway != SectorEnvironment.ActiveRunwayHeading)
	{
		// only switch if the new runway is meaningfully better, not marginally
		const float CurrentCrosswind = Crosswind(SectorEnvironment.ActiveRunwayHeading);
		if (CurrentCrosswind - BestCrosswind < ClearanceConstants::RunwaySwitchDeadbandKts)
		{
			return;
		}
	}

	if (BestRunway != SectorEnvironment.ActiveRunwayHeading)
	{
		SectorEnvironment.ActiveRunwayHeading = BestRunway;
		OnRunwayChanged.Broadcast(BestRunway);
	}
}
