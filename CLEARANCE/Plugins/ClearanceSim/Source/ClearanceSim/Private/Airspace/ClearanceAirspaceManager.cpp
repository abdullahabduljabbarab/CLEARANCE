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

void AClearanceAirspaceManager::InitialiseEnvironment(float WindDir, float WindSpeed)
{
	SectorEnvironment.WindDirection = FMath::Fmod(WindDir, 360.f);
	if (SectorEnvironment.WindDirection < 0.f)
	{
		SectorEnvironment.WindDirection += 360.f;
	}
	SectorEnvironment.WindSpeed = FMath::Max(0.f, WindSpeed);
	SectorEnvironment.ActiveRunwayHeading = -1.f; // force a fresh pick
	RecalculateActiveRunway();
}

void AClearanceAirspaceManager::SetRunways(const TArray<FRunwayInfo>& InRunways)
{
	Runways = InRunways;

	// Mirror the headings for display/Blueprint.
	SectorEnvironment.AvailableRunways.Reset();
	for (const FRunwayInfo& R : Runways)
	{
		SectorEnvironment.AvailableRunways.Add(R.HeadingDeg);
	}

	SectorEnvironment.ActiveRunwayHeading = -1.f; // re-pick with the new runways
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
	// Use the configured runways, or fall back to a default 09/27 strip at the
	// sector centre so selection still runs with none placed. - TripleA
	TArray<FRunwayInfo> Effective = Runways;
	if (Effective.Num() == 0)
	{
		FRunwayInfo A; A.ThresholdNm = FVector2D::ZeroVector; A.HeadingDeg = 90.f;  Effective.Add(A);
		FRunwayInfo B; B.ThresholdNm = FVector2D::ZeroVector; B.HeadingDeg = 270.f; Effective.Add(B);
	}

	// We land INTO wind. Score each runway by crosswind (want it low) minus headwind
	// (want it high): crosswind alone can't separate the two ends of one strip - they
	// share it exactly - so the headwind term is what makes the into-wind end win and
	// the downwind end lose. Lower score = better. - TripleA
	const float Wind = SectorEnvironment.WindDirection;
	auto RunwayCost = [&](float RunwayHeading)
	{
		const float Delta = FMath::DegreesToRadians(Wind - RunwayHeading);
		const float Crosswind = SectorEnvironment.WindSpeed * FMath::Abs(FMath::Sin(Delta));
		const float Headwind  = SectorEnvironment.WindSpeed * FMath::Cos(Delta);
		return Crosswind - Headwind;
	};

	int32 BestIdx = 0;
	float BestCost = RunwayCost(Effective[0].HeadingDeg);
	for (int32 i = 1; i < Effective.Num(); ++i)
	{
		const float C = RunwayCost(Effective[i].HeadingDeg);
		if (C < BestCost) { BestCost = C; BestIdx = i; }
	}
	const FRunwayInfo& Best = Effective[BestIdx];

	// Dead-band: don't switch away from the current runway for a marginal gain.
	const bool bHasCurrent = SectorEnvironment.ActiveRunwayHeading >= 0.f;
	if (bHasCurrent && !FMath::IsNearlyEqual(Best.HeadingDeg, SectorEnvironment.ActiveRunwayHeading))
	{
		const float CurrentCost = RunwayCost(SectorEnvironment.ActiveRunwayHeading);
		if (CurrentCost - BestCost < ClearanceConstants::RunwaySwitchDeadbandKts)
		{
			return;
		}
	}

	const bool bChanged = !FMath::IsNearlyEqual(Best.HeadingDeg, SectorEnvironment.ActiveRunwayHeading);
	SectorEnvironment.ActiveRunwayHeading = Best.HeadingDeg;
	SectorEnvironment.ActiveRunwayThreshold = FVector(Best.ThresholdNm.X, Best.ThresholdNm.Y, 0.f);
	if (bChanged)
	{
		OnRunwayChanged.Broadcast(Best.HeadingDeg);
	}
}
