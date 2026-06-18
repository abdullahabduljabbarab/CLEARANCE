#include "Simulation/ClearanceOperatorPC.h"
#include "Simulation/ClearanceSimulationController.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Scenario/ClearanceScenarioRunner.h"
#include "UI/ClearanceInstructorPanel.h"
#include "Blueprint/UserWidget.h"
#include "EngineUtils.h"

// Compact labels for instructor transcript lines - chosen for terseness so the
// AAR scroll list stays readable. Each inject handler logs a System line so the
// trainee + reviewer can see exactly what the instructor injected and when. The
// transcript already captures the downstream effect (mayday TTS, classification
// re-skin etc) - these lines tie those effects to the instructor's hand. - TripleA
static const TCHAR* EmergencyLabel(EEmergencyType K)
{
	switch (K)
	{
	case EEmergencyType::GeneralMayday: return TEXT("Mayday (7700)");
	case EEmergencyType::CommsFailure:  return TEXT("Comms Failure (7600)");
	case EEmergencyType::Hijack:        return TEXT("Hijack (7500)");
	case EEmergencyType::FuelLow:       return TEXT("Fuel Emergency");
	default:                            return TEXT("Emergency");
	}
}

static const TCHAR* ThreatClassLabel(EThreatClass C)
{
	switch (C)
	{
	case EThreatClass::Friendly: return TEXT("Friendly");
	case EThreatClass::Hostile:  return TEXT("Hostile");
	case EThreatClass::Neutral:  return TEXT("Neutral");
	case EThreatClass::Unknown:  return TEXT("Unknown");
	default:                     return TEXT("?");
	}
}

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

void AClearanceOperatorPC::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Only the operator (the PC that actually has a possessed pawn and is
	// running locally) pushes its viewpoint. The instructor PC has its pawn
	// destroyed in BeginPlay so it skips this naturally. - TripleA
	if (!IsLocalController()) { return; }
	APawn* P = GetPawn();
	if (!P) { return; }

	// Push every frame the host renders. The replicated UPROPERTY rate is
	// capped by NetUpdateFrequency anyway, so this is effectively "as fast as
	// the network allows" - which is what's needed for the PIP to stream
	// smoothly instead of catching different rotation values per capture. - TripleA
	ViewPushAccumSec += DeltaTime;
	if (ViewPushAccumSec < (1.f / 120.f)) { return; }
	ViewPushAccumSec = 0.f;

	const FRotator Rot = GetControlRotation();
	const FVector Loc = P->GetPawnViewLocation();

	if (HasAuthority())
	{
		// Listen-server / standalone host case: operator IS the server, just
		// write directly into the replicated UPROPERTY. - TripleA
		if (AClearanceSimulationController* C = FindSimController(GetWorld()))
		{
			C->SetOperatorViewRotation(Rot);
			C->SetOperatorViewLocation(Loc);
		}
	}
	else
	{
		// Operator is a remote client - RPC the value over to the server
		// where it'll get replicated back out to all clients including the
		// instructor. - TripleA
		Server_PushOperatorView(Rot, Loc);
	}
}

bool AClearanceOperatorPC::Server_PushOperatorView_Validate(FRotator NewRot, FVector NewLoc)
{
	return true;
}
void AClearanceOperatorPC::Server_PushOperatorView_Implementation(FRotator NewRot, FVector NewLoc)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->SetOperatorViewRotation(NewRot);
		C->SetOperatorViewLocation(NewLoc);
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
		C->LogTranscriptLine(EClearanceCommsRole::System, Callsign,
			FString::Printf(TEXT("[INSTRUCTOR] Injected %s on %s"), EmergencyLabel(Kind), *Callsign.ToString()));
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
		C->LogTranscriptLine(EClearanceCommsRole::System, Callsign,
			FString::Printf(TEXT("[INSTRUCTOR] Cleared emergency on %s"), *Callsign.ToString()));
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
		C->LogTranscriptLine(EClearanceCommsRole::System, Callsign,
			FString::Printf(TEXT("[INSTRUCTOR] Reclassified %s as %s"), *Callsign.ToString(), ThreatClassLabel(NewClass)));
		// "Inject" path = instructor panel reclassification; bypass mis-ID
		// scoring so changing a contact's threat class from god view doesn't
		// log a doctrine failure against the trainee. Operator voice
		// classifications still go through ClassifyAircraft with the default
		// bAsInstructor=false and DO score. - TripleA
		C->ClassifyAircraft(Callsign, NewClass, /*bAsInstructor=*/true);
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
		C->LogTranscriptLine(EClearanceCommsRole::System, BanditCallsign,
			FString::Printf(TEXT("[INSTRUCTOR] Scrambled interceptors on %s"), *BanditCallsign.ToString()));
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
		C->LogTranscriptLine(EClearanceCommsRole::System, NAME_None,
			FString::Printf(TEXT("[INSTRUCTOR] Wind set to %03.0f / %.0fkt"), DirectionDeg, SpeedKts));
		C->SetWind(DirectionDeg, SpeedKts);
	}
}

// --- Spawn / Clear ---------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectSpawn_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectSpawn_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->LogTranscriptLine(EClearanceCommsRole::System, NAME_None, TEXT("[INSTRUCTOR] Spawned aircraft"));
		C->SpawnOne();
	}
}

bool AClearanceOperatorPC::Server_InjectClearTraffic_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectClearTraffic_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->LogTranscriptLine(EClearanceCommsRole::System, NAME_None, TEXT("[INSTRUCTOR] Cleared all traffic"));
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
		C->LogTranscriptLine(EClearanceCommsRole::System, NAME_None,
			FString::Printf(TEXT("[INSTRUCTOR] Loaded scenario \"%s\""), *ScenarioName));
		C->Server_InjectLoadScenario(ScenarioName); // reuse the controller-side path
	}
}

bool AClearanceOperatorPC::Server_InjectStopScenario_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectStopScenario_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->LogTranscriptLine(EClearanceCommsRole::System, NAME_None, TEXT("[INSTRUCTOR] Stopped scenario"));
		C->Server_InjectStopScenario();
	}
}

// --- Pause -----------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectSetPaused_Validate(bool bNewPaused) { return true; }
void AClearanceOperatorPC::Server_InjectSetPaused_Implementation(bool bNewPaused)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->LogTranscriptLine(EClearanceCommsRole::System, NAME_None,
			bNewPaused ? TEXT("[INSTRUCTOR] Sim paused") : TEXT("[INSTRUCTOR] Sim resumed"));
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
	C->LogTranscriptLine(EClearanceCommsRole::System, Callsign,
		FString::Printf(TEXT("[INSTRUCTOR] Jammer %s on %s"), bOn ? TEXT("on") : TEXT("off"), *Callsign.ToString()));
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
	C->LogTranscriptLine(EClearanceCommsRole::System, Callsign,
		FString::Printf(TEXT("[INSTRUCTOR] Chaff dropped from %s"), *Callsign.ToString()));
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
		const float Clamped = FMath::Max(0.f, Scale);
		C->LogTranscriptLine(EClearanceCommsRole::System, NAME_None,
			FString::Printf(TEXT("[INSTRUCTOR] Time scale set to %.2fx"), Clamped));
		C->SimulationTimeScale = Clamped;
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
	C->LogTranscriptLine(EClearanceCommsRole::System, NAME_None,
		FString::Printf(TEXT("[INSTRUCTOR] Reset scenario \"%s\""), *Loaded));
	C->Server_InjectStopScenario();
	C->Server_InjectLoadScenario(Loaded);
}

// --- AAR replay control ----------------------------------------------------

bool AClearanceOperatorPC::Server_InjectEnterReplay_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectEnterReplay_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld())) { C->EnterReplay(); }
}

bool AClearanceOperatorPC::Server_InjectResumeLive_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectResumeLive_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld())) { C->ResumeLive(); }
}

bool AClearanceOperatorPC::Server_InjectSeekReplay_Validate(float TimeSeconds) { return true; }
void AClearanceOperatorPC::Server_InjectSeekReplay_Implementation(float TimeSeconds)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld())) { C->SeekReplay(TimeSeconds); }
}

bool AClearanceOperatorPC::Server_InjectSetReplayPaused_Validate(bool bInPaused) { return true; }
void AClearanceOperatorPC::Server_InjectSetReplayPaused_Implementation(bool bInPaused)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld())) { C->SetReplayPaused(bInPaused); }
}

bool AClearanceOperatorPC::Server_InjectSetReplaySpeed_Validate(float Multiplier) { return true; }
void AClearanceOperatorPC::Server_InjectSetReplaySpeed_Implementation(float Multiplier)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld())) { C->SetReplaySpeed(Multiplier); }
}

bool AClearanceOperatorPC::Server_InjectExportAAR_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectExportAAR_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		FString Path;
		C->ExportAARReport(Path);
	}
}
