#include "Simulation/ClearanceOperatorPC.h"
#include "Simulation/ClearanceSimulationController.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Scenario/ClearanceScenarioRunner.h"
#include "UI/ClearanceInstructorPanel.h"
#include "Blueprint/UserWidget.h"
#include "EngineUtils.h"

void AClearanceOperatorPC::BeginPlay()
{
	Super::BeginPlay();

	// Instructor panel only spawns on the client peer (the instructor's window).
	// The listen-server / authority window is the operator and gets the normal
	// readout HUD instead. - TripleA
	if (!IsLocalController() || GetLocalRole() == ROLE_Authority) { return; }
	if (InstructorPanelClass.IsNull()) { return; }

	UClass* PanelClass = InstructorPanelClass.LoadSynchronous();
	if (!PanelClass) { return; }

	// The instructor client doesn't need a pawn — destroy it so its input
	// bindings can't recapture the mouse from the UI. - TripleA
	if (APawn* P = GetPawn())
	{
		UnPossess();
		P->Destroy();
	}

	InstructorPanel = CreateWidget<UClearanceInstructorPanel>(this, PanelClass);
	if (InstructorPanel)
	{
		InstructorPanel->AddToViewport();

		// Lock input to UI-only so clicks hit buttons/combos, not the
		// game world. Show the mouse cursor. - TripleA
		FInputModeUIOnly UIMode;
		UIMode.SetWidgetToFocus(InstructorPanel->TakeWidget());
		SetInputMode(UIMode);
		SetShowMouseCursor(true);
	}
}

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
	return true;  // Never kick the client — graceful no-op in Implementation instead
}
void AClearanceOperatorPC::Server_InjectEmergency_Implementation(FName Callsign, EEmergencyType Kind)
{
	if (Callsign == NAME_None) { return; }
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->DeclareEmergencyOn(Callsign, Kind);
	}
}

bool AClearanceOperatorPC::Server_InjectClearEmergency_Validate(FName Callsign)
{
	return true;
}
void AClearanceOperatorPC::Server_InjectClearEmergency_Implementation(FName Callsign)
{
	if (Callsign == NAME_None) { return; }
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->ClearEmergencyOn(Callsign);
	}
}

// --- Classify --------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectClassify_Validate(FName Callsign, EThreatClass NewClass)
{
	return true;
}
void AClearanceOperatorPC::Server_InjectClassify_Implementation(FName Callsign, EThreatClass NewClass)
{
	if (Callsign == NAME_None) { return; }
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->ClassifyAircraft(Callsign, NewClass);
	}
}

// --- Scramble --------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectScramble_Validate(FName BanditCallsign)
{
	return true;
}
void AClearanceOperatorPC::Server_InjectScramble_Implementation(FName BanditCallsign)
{
	if (BanditCallsign == NAME_None) { return; }
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->ScrambleInterceptors(BanditCallsign);
	}
}

// --- Wind ------------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectSetWind_Validate(float DirectionDeg, float SpeedKts)
{
	return true;
}
void AClearanceOperatorPC::Server_InjectSetWind_Implementation(float DirectionDeg, float SpeedKts)
{
	SpeedKts = FMath::Clamp(SpeedKts, 0.f, 200.f);
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
	return true;
}
void AClearanceOperatorPC::Server_InjectLoadScenario_Implementation(const FString& ScenarioName)
{
	if (ScenarioName.IsEmpty()) { return; }
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

// --- EW Jamming / Chaff ----------------------------------------------------

bool AClearanceOperatorPC::Server_InjectJamming_Validate(FName Callsign, bool bOn)
{
	return true;
}
void AClearanceOperatorPC::Server_InjectJamming_Implementation(FName Callsign, bool bOn)
{
	if (Callsign == NAME_None) { return; }
	AClearanceSimulationController* C = FindSimController(GetWorld());
	if (!C || !C->GetAirspaceManager()) { return; }
	FAircraftState S = C->GetAirspaceManager()->GetAircraftState(Callsign);
	if (!S.bIsValid) { return; }
	S.bJammingOn = bOn;
	C->GetAirspaceManager()->RequestStateUpdate(S);
}

bool AClearanceOperatorPC::Server_InjectChaff_Validate(FName Callsign)
{
	return true;
}
void AClearanceOperatorPC::Server_InjectChaff_Implementation(FName Callsign)
{
	if (Callsign == NAME_None) { return; }
	AClearanceSimulationController* C = FindSimController(GetWorld());
	if (!C || !C->GetAirspaceManager()) { return; }
	const FAircraftState S = C->GetAirspaceManager()->GetAircraftState(Callsign);
	if (!S.bIsValid) { return; }
	C->GetAirspaceManager()->DropChaff(S.Position, S.Altitude);
}

// --- Time Scale ------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectSetTimeScale_Validate(float Scale)
{
	return true;
}
void AClearanceOperatorPC::Server_InjectSetTimeScale_Implementation(float Scale)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->SimulationTimeScale = FMath::Max(0.f, Scale);
	}
}

// --- Reset (current) Scenario ----------------------------------------------

bool AClearanceOperatorPC::Server_InjectResetScenario_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectResetScenario_Implementation()
{
	AClearanceSimulationController* C = FindSimController(GetWorld());
	if (!C || !C->GetScenarioRunner()) { return; }
	const FString Loaded = C->GetScenarioRunner()->GetLoadedName();
	if (Loaded.IsEmpty()) { return; }
	C->Server_InjectStopScenario();
	C->Server_InjectLoadScenario(Loaded);
}
