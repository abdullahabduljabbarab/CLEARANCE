#include "Simulation/ClearanceOperatorPC.h"
#include "Simulation/ClearanceSimulationController.h"
#include "EngineUtils.h"

namespace
{
	AClearanceSimulationController* FindSimController(UWorld* World)
	{
		if (!World) { return nullptr; }
		for (TActorIterator<AClearanceSimulationController> It(World); It; ++It) { return *It; }
		return nullptr;
	}
}

// --- Emergency -------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectEmergency_Validate(FName Callsign, EEmergencyType Kind)
{
	return Callsign != NAME_None;
}
void AClearanceOperatorPC::Server_InjectEmergency_Implementation(FName Callsign, EEmergencyType Kind)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->DeclareEmergencyOn(Callsign, Kind);
	}
}

// --- Classify --------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectClassify_Validate(FName Callsign, EThreatClass NewClass)
{
	return Callsign != NAME_None;
}
void AClearanceOperatorPC::Server_InjectClassify_Implementation(FName Callsign, EThreatClass NewClass)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->ClassifyAircraft(Callsign, NewClass);
	}
}

// --- Scramble --------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectScramble_Validate(FName BanditCallsign)
{
	return BanditCallsign != NAME_None;
}
void AClearanceOperatorPC::Server_InjectScramble_Implementation(FName BanditCallsign)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->ScrambleInterceptors(BanditCallsign);
	}
}

// --- Wind ------------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectSetWind_Validate(float DirectionDeg, float SpeedKts)
{
	return SpeedKts >= 0.f && SpeedKts <= 200.f;
}
void AClearanceOperatorPC::Server_InjectSetWind_Implementation(float DirectionDeg, float SpeedKts)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->SetWind(DirectionDeg, SpeedKts);
	}
}

// --- Spawn / Clear ---------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectSpawn_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectSpawn_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->SpawnOne();
	}
}

bool AClearanceOperatorPC::Server_InjectClearTraffic_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectClearTraffic_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->ClearTraffic();
	}
}

// --- Scenario --------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectLoadScenario_Validate(const FString& ScenarioName)
{
	return !ScenarioName.IsEmpty();
}
void AClearanceOperatorPC::Server_InjectLoadScenario_Implementation(const FString& ScenarioName)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->Server_InjectLoadScenario(ScenarioName); // reuse the controller-side path
	}
}

bool AClearanceOperatorPC::Server_InjectStopScenario_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectStopScenario_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->Server_InjectStopScenario();
	}
}

// --- Pause -----------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectSetPaused_Validate(bool bNewPaused) { return true; }
void AClearanceOperatorPC::Server_InjectSetPaused_Implementation(bool bNewPaused)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->Server_InjectSetPaused(bNewPaused);
	}
}
