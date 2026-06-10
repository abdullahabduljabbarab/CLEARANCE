#include "Aircraft/ClearanceAircraftSpawner.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Core/ClearanceConstants.h"

AClearanceAircraftSpawner::AClearanceAircraftSpawner()
{
	PrimaryActorTick.bCanEverTick = false; // the Controller drives spawning timing
}

void AClearanceAircraftSpawner::SetReferences(AClearanceAirspaceManager* InManager)
{
	Manager = InManager;
}

void AClearanceAircraftSpawner::TickSpawning(float DeltaTime)
{
	if (!bAutoSpawn || !Manager || CurrentSpawnIntervalSeconds <= 0.f)
	{
		return;
	}

	SpawnTimer += DeltaTime;
	if (SpawnTimer >= CurrentSpawnIntervalSeconds)
	{
		SpawnTimer = 0.f;
		if (Manager->GetAircraftCount() < MaxConcurrentAircraft)
		{
			SpawnAircraft();
		}
	}
}

bool AClearanceAircraftSpawner::SpawnAircraft()
{
	if (!Manager || Manager->GetAircraftCount() >= MaxConcurrentAircraft)
	{
		return false;
	}

	const bool bBandit = (BanditChance > 0.f) && (FMath::FRand() < BanditChance);

	FAircraftSpawnData Data = GenerateSpawnData();

	FAircraftState State;
	State.Position = Data.EntryPosition;
	State.Altitude = Data.EntryAltitude;
	State.Speed = Data.EntrySpeed;
	State.Heading = Data.EntryHeading;
	State.FlightPhase = Data.InitialPhase;
	State.WakeCategory = Data.WakeCategory;

	if (bBandit)
	{
		// Drop a hostile contact in alongside the civilian traffic. ThreatClass stays
		// UNKNOWN until the operator interrogates and classifies - the player has to
		// notice no IFF response and make the call. Squawk 7777 is the NATO hostile
		// code; civilians never use it, so a sharp operator catches it on the scope
		// too. - TripleA
		State.Callsign         = GenerateBanditCallsign();
		State.ThreatClass      = EThreatClass::Unknown;
		State.SquawkCode       = 7777;
		State.bIFFOperational  = false;
		State.bIsMilitary      = true;
	}
	else
	{
		State.Callsign = Data.Callsign;
	}

	// targets / performance limits are filled in when the Behaviour initialises
	return Manager->RegisterAircraft(State);
}

void AClearanceAircraftSpawner::SetSpawnInterval(float Seconds)
{
	CurrentSpawnIntervalSeconds = FMath::Max(1.f, Seconds);
}

void AClearanceAircraftSpawner::SetAutoSpawn(bool bEnabled)
{
	bAutoSpawn = bEnabled;
}

FAircraftSpawnData AClearanceAircraftSpawner::GenerateSpawnData()
{
	FAircraftSpawnData Data;
	Data.Callsign = GenerateCallsign();
	Data.WakeCategory = RandomCategory();
	Data.InitialPhase = EFlightPhase::Enroute;

	// Enter somewhere on the sector boundary circle, pointing roughly inbound.
	const float AngleDeg = FMath::FRandRange(0.f, 360.f);
	const float AngleRad = FMath::DegreesToRadians(AngleDeg);
	Data.EntryPosition = FVector(EntryRadiusNm * FMath::Sin(AngleRad), EntryRadiusNm * FMath::Cos(AngleRad), 0.f);

	// Inbound bearing = toward origin, with a little spread so they don't all aim
	// dead-centre and collide. Compass bearing from (east, north) deltas.
	const float Inbound = FMath::RadiansToDegrees(FMath::Atan2(-Data.EntryPosition.X, -Data.EntryPosition.Y));
	Data.EntryHeading = FMath::Fmod(Inbound + FMath::FRandRange(-25.f, 25.f) + 360.f, 360.f);

	Data.EntryAltitude = FMath::FRandRange(MinEntryAltitudeFt, MaxEntryAltitudeFt);

	const ClearanceConstants::FCategoryPerformance Perf = ClearanceConstants::GetCategoryPerformance(Data.WakeCategory);
	Data.EntrySpeed = FMath::FRandRange(Perf.MinOperatingSpeedKts + 20.f, Perf.MaxOperatingSpeedKts - 20.f);

	return Data;
}

FName AClearanceAircraftSpawner::GenerateCallsign()
{
	static const TCHAR* Airlines[] = { TEXT("BAW"), TEXT("DLH"), TEXT("UAL"), TEXT("AAL"), TEXT("AFR"), TEXT("UAE") };
	const TCHAR* Prefix = Airlines[FMath::RandRange(0, UE_ARRAY_COUNT(Airlines) - 1)];
	return FName(*FString::Printf(TEXT("%s%d"), Prefix, 100 + (++CallsignCounter)));
}

FName AClearanceAircraftSpawner::GenerateBanditCallsign()
{
	return FName(*FString::Printf(TEXT("UNK%03d"), ++BanditCounter));
}

EWakeCategory AClearanceAircraftSpawner::RandomCategory() const
{
	// Roughly realistic mix: mostly Medium, some Heavy, fewer Light, rare Super.
	const float Roll = FMath::FRand();
	if (Roll < 0.15f) return EWakeCategory::Light;
	if (Roll < 0.78f) return EWakeCategory::Medium;
	if (Roll < 0.97f) return EWakeCategory::Heavy;
	return EWakeCategory::Super;
}
