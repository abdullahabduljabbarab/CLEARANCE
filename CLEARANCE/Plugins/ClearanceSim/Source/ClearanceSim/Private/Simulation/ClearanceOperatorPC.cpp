#include "Simulation/ClearanceOperatorPC.h"
#include "Simulation/ClearanceSimulationController.h"
#include "Comms/ClearanceVoiceInput.h"
#include "Net/UnrealNetwork.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Simulation/ClearanceDISEmitter.h"
#include "Simulation/ClearanceDISReceiver.h"
#include "Simulation/ClearanceDDSEmitter.h"
#include "Simulation/ClearanceDDSReceiver.h"
#include "Simulation/ClearanceRTIEmitter.h"
#include "Simulation/ClearanceHLAEmitter.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Aircraft/ClearanceAircraftSpawner.h"
#include "Scoring/ClearanceScoring.h"
#include "Scenario/ClearanceScenarioRunner.h"
#include "UI/ClearanceInstructorPanel.h"
#include "Blueprint/UserWidget.h"
#include "EngineUtils.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameModeBase.h"

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

	if (!IsLocalController()) { return; }

	// Role selection.
	//
	// Server side (listen-server host or standalone) reads the URL query
	// string that InitGame received - lives on the base
	// AGameModeBase::OptionsString UPROPERTY, so we can pull it without
	// casting to CLEARANCE's derived game mode (that class lives in the
	// main game module and this plugin can't include from it - circular
	// dep).
	//
	// Client side has no game mode instance (game mode is server-only),
	// so falls back to the authority convention: authority = operator,
	// non-authority = instructor. Matches how LAN clients connect -
	// JoinLANSession appends ?role=instructor which the server sees on
	// its side; the client-side non-authority check confirms it. Combined
	// session uses two separate processes on localhost (see
	// OpenCombinedSessionSolo), so the role dispatch here is identical to
	// HOST LAN / JOIN LAN and doesn't need a special combined branch. - TripleA
	bool bIsInstructor = false;
	if (GetLocalRole() == ROLE_Authority)
	{
		if (UWorld* W = GetWorld())
		{
			if (const AGameModeBase* GM = W->GetAuthGameMode())
			{
				const FString RoleOption = UGameplayStatics::ParseOption(GM->OptionsString, TEXT("role"));
				bIsInstructor = RoleOption.Equals(TEXT("instructor"), ESearchCase::IgnoreCase);
			}
		}
	}
	else
	{
		// Non-authority client peer: always instructor per network convention.
		bIsInstructor = true;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[ClearanceOperatorPC] BeginPlay LocalRole=%s bIsInstructor=%s"),
		(GetLocalRole() == ROLE_Authority ? TEXT("Authority") : TEXT("Non-Authority")),
		bIsInstructor ? TEXT("true") : TEXT("false"));

	if (!bIsInstructor) { return; }

	if (InstructorPanelClass.IsNull())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ClearanceOperatorPC] Instructor role selected but InstructorPanelClass is null "
			     "- panel will not spawn. Set it on the BP subclass of AClearanceOperatorPC."));
		return;
	}

	UClass* PanelClass = InstructorPanelClass.LoadSynchronous();
	if (!PanelClass) { return; }

	// Instructor-side clients (pure instructor client, LAN instructor peer,
	// or combined-mode Player 1) don't need a pawn - destroy it so its
	// input bindings can't recapture the mouse from the UI. Reaching this
	// point already means bIsInstructor is true, which for combined mode
	// implies LocalPlayerIndex >= 1 (Player 1, the flat-monitor instructor).
	// Player 0 in combined mode returned earlier and kept its VR pawn. - TripleA
	if (APawn* P = GetPawn())
	{
		UnPossess();
		P->Destroy();
	}

	InstructorPanel = CreateWidget<UClearanceInstructorPanel>(this, PanelClass);
	if (InstructorPanel)
	{
		InstructorPanel->AddToViewport();

		// Input mode selection:
		//  - VR + instructor client peer: GameAndUI. Motion controllers
		//    reach the pawn while WidgetInteractionComponent clicks reach
		//    the scope UI overlays.
		//  - Instructor-only desktop client / combined-mode Player 1:
		//    UIOnly. No pawn to talk to, lock clicks to the panel.
		// Combined mode's Player 0 is the operator side and returned above
		// before this input-mode block runs, so we don't need a special
		// GameAndUI case for combined here. - TripleA
		const bool bIsInVR = GEngine && GEngine->XRSystem.IsValid()
			&& GEngine->XRSystem->GetHMDDevice() != nullptr
			&& GEngine->XRSystem->GetHMDDevice()->IsHMDConnected();
		if (bIsInVR)
		{
			FInputModeGameAndUI GameUIMode;
			GameUIMode.SetWidgetToFocus(InstructorPanel->TakeWidget());
			GameUIMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
			SetInputMode(GameUIMode);
		}
		else
		{
			FInputModeUIOnly UIMode;
			UIMode.SetWidgetToFocus(InstructorPanel->TakeWidget());
			SetInputMode(UIMode);
		}
		// Show the cursor whenever we're on desktop (not in VR). Combined
		// mode is always desktop so cursor stays visible. - TripleA
		SetShowMouseCursor(!bIsInVR);
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

bool AClearanceOperatorPC::Server_InjectEmergency_Validate(FName Callsign, EEmergencyType Kind, float TimerMinutes)
{
	return true;  // Never kick the client — graceful no-op in Implementation instead
}
void AClearanceOperatorPC::Server_InjectEmergency_Implementation(FName Callsign, EEmergencyType Kind, float TimerMinutes)
{
	if (Callsign == NAME_None) { return; }
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, Callsign,
			FString::Printf(TEXT("Injected %s on %s"), EmergencyLabel(Kind), *Callsign.ToString()));
		C->DeclareEmergencyOn(Callsign, Kind, TimerMinutes);
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
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, Callsign,
			FString::Printf(TEXT("Cleared emergency on %s"), *Callsign.ToString()));
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
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, Callsign,
			FString::Printf(TEXT("Reclassified %s as %s"), *Callsign.ToString(), ThreatClassLabel(NewClass)));
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
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, BanditCallsign,
			FString::Printf(TEXT("Scrambled interceptors on %s"), *BanditCallsign.ToString()));
		C->ScrambleInterceptors(BanditCallsign);
	}
}

// --- Missile fire ----------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectFireMissile_Validate(FName TargetCallsign)
{
	return true;
}
void AClearanceOperatorPC::Server_InjectFireMissile_Implementation(FName TargetCallsign)
{
	if (TargetCallsign == NAME_None) { return; }
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, TargetCallsign,
			FString::Printf(TEXT("SAM engagement on %s"), *TargetCallsign.ToString()));
		C->Server_InjectFireMissile(TargetCallsign);
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
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None,
			FString::Printf(TEXT("Wind set to %03.0f / %.0fkt"), DirectionDeg, SpeedKts));
		C->SetWind(DirectionDeg, SpeedKts);
	}
}

bool AClearanceOperatorPC::Server_InjectSetMaxAircraft_Validate(int32 NewMax)
{
	return true;
}
void AClearanceOperatorPC::Server_InjectSetMaxAircraft_Implementation(int32 NewMax)
{
	const int32 Clamped = FMath::Clamp(NewMax, 1, 40);
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		// Three separate caps have to move together:
		//   - AirspaceManager::MaxAircraftCount  gates state registration
		//   - Spawner::MaxConcurrentAircraft     gates spawn tick
		//   - Controller::MaxConcurrentAircraft  is the mirror re-applied on
		//     future spawner (re)inits
		// Setting only one leaves the smallest-cap gate winning - which is
		// why sliding the value up to 24 was still capping at 10. - TripleA
		if (AClearanceAirspaceManager* Mgr = C->GetAirspaceManager())
		{
			Mgr->MaxAircraftCount = Clamped;
		}
		if (C->Spawner)
		{
			C->Spawner->MaxConcurrentAircraft = Clamped;
		}
		C->MaxConcurrentAircraft = Clamped;
		C->PushNotification(FString::Printf(TEXT("TRAFFIC: max aircraft set to %d"), Clamped), FColor::Cyan, 4.f);
	}
}

// --- Spawn / Clear ---------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectSpawn_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectSpawn_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None, TEXT("Spawned aircraft"));
		C->SpawnOne();
	}
}

bool AClearanceOperatorPC::Server_InjectClearTraffic_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectClearTraffic_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None, TEXT("Cleared all traffic"));
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
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None,
			FString::Printf(TEXT("Loaded scenario \"%s\""), *ScenarioName));
		C->Server_InjectLoadScenario(ScenarioName); // reuse the controller-side path
	}
}

bool AClearanceOperatorPC::Server_InjectStopScenario_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectStopScenario_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None, TEXT("Stopped scenario"));
		C->Server_InjectStopScenario();
	}
}

// --- Pause -----------------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectSetPaused_Validate(bool bNewPaused) { return true; }
void AClearanceOperatorPC::Server_InjectSetPaused_Implementation(bool bNewPaused)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None,
			bNewPaused ? TEXT("Sim paused") : TEXT("Sim resumed"));
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
	C->LogTranscriptLine(EClearanceCommsRole::Instructor, Callsign,
		FString::Printf(TEXT("Jammer %s on %s"), bOn ? TEXT("on") : TEXT("off"), *Callsign.ToString()));
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
	C->LogTranscriptLine(EClearanceCommsRole::Instructor, Callsign,
		FString::Printf(TEXT("Chaff dropped from %s"), *Callsign.ToString()));
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
		C->LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None,
			FString::Printf(TEXT("Time scale set to %.2fx"), Clamped));
		C->SimulationTimeScale = Clamped;
	}
}

// --- Reset (current) Scenario ----------------------------------------------

bool AClearanceOperatorPC::Server_InjectResetScenario_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectResetScenario_Implementation()
{
	AClearanceSimulationController* C = FindSimController(GetWorld());
	if (!C || !C->GetScenarioRunner()) { return; }

	// Round-trip via the file name, not the display name. Metadata.Name is
	// the human title ("Baltic Intercept" with spaces); Server_InjectLoad-
	// Scenario expects the file stem ("baltic_intercept") so it can resolve
	// Plugins/ClearanceSim/Scenarios/<stem>.json. - TripleA
	const FString FileStem = C->GetScenarioRunner()->GetLoadedFileName();
	const FString Display  = C->GetScenarioRunner()->GetLoadedName();
	if (FileStem.IsEmpty())
	{
		C->PushNotification(TEXT("RESET: no scenario loaded"), FColor::Yellow, 4.f);
		return;
	}

	C->LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None,
		FString::Printf(TEXT("Reset scenario \"%s\""), *Display));

	if (AClearanceAirspaceManager* Mgr = C->GetAirspaceManager())
	{
		Mgr->ClearAllAircraft();
	}
	if (UClearanceScoring* Sc = C->GetScoring())
	{
		Sc->ResetSession();
	}
	C->Server_InjectStopScenario();
	C->Server_InjectLoadScenario(FileStem);
	C->PushNotification(FString::Printf(TEXT("SESSION: reset (scenario \"%s\" reloaded)"), *Display), FColor::Red, 6.f);
}

// --- DIS federation controls -----------------------------------------------

bool AClearanceOperatorPC::Server_InjectStartDISEmit_Validate(const FString&, int32) { return true; }
void AClearanceOperatorPC::Server_InjectStartDISEmit_Implementation(const FString& Host, int32 Port)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		const bool bOk = C->StartDIS(Host, Port);
		C->PushNotification(
			bOk ? FString::Printf(TEXT("DIS: emit -> %s:%d"), *Host, Port)
			    : FString::Printf(TEXT("DIS: emit failed (%s:%d)"), *Host, Port),
			bOk ? FColor::Cyan : FColor::Red, 5.f);
	}
}

bool AClearanceOperatorPC::Server_InjectStopDISEmit_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectStopDISEmit_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->StopDIS();
		C->PushNotification(TEXT("DIS: emit stopped"), FColor::Cyan, 4.f);
	}
}

bool AClearanceOperatorPC::Server_InjectStartDISRecv_Validate(int32) { return true; }
void AClearanceOperatorPC::Server_InjectStartDISRecv_Implementation(int32 Port)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		const bool bOk = C->StartDISReceiver(Port);
		C->PushNotification(
			bOk ? FString::Printf(TEXT("DIS: recv listening :%d"), Port)
			    : FString::Printf(TEXT("DIS: recv failed (:%d)"), Port),
			bOk ? FColor::Cyan : FColor::Red, 5.f);
	}
}

bool AClearanceOperatorPC::Server_InjectStopDISRecv_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectStopDISRecv_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->StopDISReceiver();
		C->PushNotification(TEXT("DIS: recv stopped"), FColor::Cyan, 4.f);
	}
}

// --- DDS pub/sub -----------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectStartDDSEmit_Validate(int32) { return true; }
void AClearanceOperatorPC::Server_InjectStartDDSEmit_Implementation(int32 DomainId)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		const bool bOk = C->StartDDSEmitter(DomainId);
		C->PushNotification(
			bOk ? FString::Printf(TEXT("DDS: publishing on domain %d"), DomainId)
			    : FString::Printf(TEXT("DDS: failed to start (domain %d)"), DomainId),
			bOk ? FColor::Cyan : FColor::Red, 5.f);
	}
}

bool AClearanceOperatorPC::Server_InjectStopDDSEmit_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectStopDDSEmit_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->StopDDSEmitter();
		C->PushNotification(TEXT("DDS: publishing stopped"), FColor::Cyan, 4.f);
	}
}

bool AClearanceOperatorPC::Server_InjectStartDDSRecv_Validate(int32) { return true; }
void AClearanceOperatorPC::Server_InjectStartDDSRecv_Implementation(int32 DomainId)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		const bool bOk = C->StartDDSReceiver(DomainId);
		C->PushNotification(
			bOk ? FString::Printf(TEXT("DDS: listening on domain %d"), DomainId)
			    : FString::Printf(TEXT("DDS: recv failed (domain %d)"), DomainId),
			bOk ? FColor::Cyan : FColor::Red, 5.f);
	}
}

bool AClearanceOperatorPC::Server_InjectStopDDSRecv_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectStopDDSRecv_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->StopDDSReceiver();
		C->PushNotification(TEXT("DDS: recv stopped"), FColor::Cyan, 4.f);
	}
}

bool AClearanceOperatorPC::Server_InjectSetFederateSiteId_Validate(int32) { return true; }
void AClearanceOperatorPC::Server_InjectSetFederateSiteId_Implementation(int32 NewSiteId)
{
	AClearanceSimulationController* C = FindSimController(GetWorld());
	if (!C) { return; }
	if (UClearanceDISEmitter*  E = C->GetDISEmitter())  { E->SiteId      = NewSiteId; }
	if (UClearanceDISReceiver* R = C->GetDISReceiver()) { R->LocalSiteId = NewSiteId; }
	if (UClearanceDDSEmitter*  E = C->GetDDSEmitter())  { E->SiteId      = NewSiteId; }
	if (UClearanceDDSReceiver* R = C->GetDDSReceiver()) { R->LocalSiteId = NewSiteId; }
	if (UClearanceRTIEmitter*  E = C->GetRTIEmitter())  { E->SiteId      = NewSiteId; }
	C->PushNotification(
		FString::Printf(TEXT("Federate Site ID = %d (DIS + DDS + RTI - peers must differ)"), NewSiteId),
		FColor::Cyan, 5.f);
}

// --- RTI Connext pub -------------------------------------------------------

bool AClearanceOperatorPC::Server_InjectStartRTIEmit_Validate(int32) { return true; }
void AClearanceOperatorPC::Server_InjectStartRTIEmit_Implementation(int32 DomainId)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		const bool bOk = C->StartRTIEmitter(DomainId);
		C->PushNotification(
			bOk ? FString::Printf(TEXT("RTI: publishing on domain %d"), DomainId)
			    : FString::Printf(TEXT("RTI: failed to start (domain %d)"), DomainId),
			bOk ? FColor::Green : FColor::Red, 5.f);
	}
}

bool AClearanceOperatorPC::Server_InjectStopRTIEmit_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectStopRTIEmit_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->StopRTIEmitter();
		C->PushNotification(TEXT("RTI: publishing stopped"), FColor::Green, 4.f);
	}
}

// --- HLA federate join/resign -----------------------------------------------

bool AClearanceOperatorPC::Server_InjectStartHLAJoin_Validate(const FString&, const FString&, const FString&) { return true; }
void AClearanceOperatorPC::Server_InjectStartHLAJoin_Implementation(const FString& FederationName, const FString& FederateName, const FString& FomModulePath)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		// Defensive path resolution - BP-side string concatenation for the
		// FOM path is fragile (slash direction, separator between the
		// plugin dir + FOM subpath). If the caller-supplied path is empty
		// OR doesn't point at a real file, fall back to the canonical
		// location under the ClearanceSim plugin. Keeps the panel JOIN
		// button working even when the BP wiring mangles the path. - TripleA
		FString ResolvedFom = FomModulePath;
		if (ResolvedFom.IsEmpty() || !FPaths::FileExists(ResolvedFom))
		{
			ResolvedFom = FPaths::Combine(FPaths::ProjectPluginsDir(),
				TEXT("ClearanceSim/FOM/ClearanceRPR-FOM.xml"));
			UE_LOG(LogTemp, Display, TEXT("[HLA] BP-supplied FOM path '%s' unresolvable - using default '%s'"),
				*FomModulePath, *ResolvedFom);
		}

		const bool bOk = C->StartHLAFederate(FederationName, FederateName, ResolvedFom);
		C->PushNotification(
			bOk ? FString::Printf(TEXT("HLA: joined '%s' as '%s'"), *FederationName, *FederateName)
			    : FString::Printf(TEXT("HLA: join failed on '%s'"), *FederationName),
			bOk ? FColor::Purple : FColor::Red, 5.f);
	}
}

bool AClearanceOperatorPC::Server_InjectStopHLAJoin_Validate() { return true; }
void AClearanceOperatorPC::Server_InjectStopHLAJoin_Implementation()
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld()))
	{
		C->StopHLAFederate();
		C->PushNotification(TEXT("HLA: resigned"), FColor::Purple, 4.f);
	}
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

bool AClearanceOperatorPC::Server_EndSessionAndReport_Validate() { return true; }
void AClearanceOperatorPC::Server_EndSessionAndReport_Implementation()
{
	// Server-side end-session bundle: snapshot score + session time BEFORE
	// stopping the sim (so the numbers reflect the moment the button was
	// hit, not any subsequent teardown drift), export the AAR, call
	// EndSession to actually halt the sim (freezes aircraft, halts scoring
	// tick, closes the session out), then RPC the report back to the
	// calling client so its WBP_EndSessionReport modal can display it.
	// The modal's Restart / Exit buttons wire into the existing
	// Server_InjectResetScenario RPC and ExitToMainMenu function library
	// helper respectively. - TripleA
	AClearanceSimulationController* C = FindSimController(GetWorld());
	if (!C)
	{
		Client_ReceiveEndSessionReport(TEXT("(no SimController)"), 0, 0.f);
		return;
	}

	int32 Score = 0;
	if (UClearanceScoring* Sc = C->GetScoring())
	{
		Score = Sc->GetCurrentScore();
	}
	const float SessionSeconds = C->GetSessionTime();

	FString Path;
	const bool bOk = C->ExportAARReport(Path);
	if (!bOk)
	{
		Path = TEXT("(export failed - see Output Log for details)");
	}

	// Actually stop the sim after the snapshot + export. Aircraft freeze,
	// scoring tick halts, session flag flips to inactive. Restart button
	// on the modal reverses this via Server_InjectResetScenario. - TripleA
	C->EndSession();

	Client_ReceiveEndSessionReport(Path, Score, SessionSeconds);
}

void AClearanceOperatorPC::Client_ReceiveEndSessionReport_Implementation(
	const FString& FilePath, int32 Score, float SessionTimeSeconds)
{
	OnEndSessionReport.Broadcast(FilePath, Score, SessionTimeSeconds);
}

bool AClearanceOperatorPC::Server_InjectSaveCheckpoint_Validate(FName Name) { return true; }
void AClearanceOperatorPC::Server_InjectSaveCheckpoint_Implementation(FName Name)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld())) { C->SaveCheckpoint(Name); }
}

bool AClearanceOperatorPC::Server_InjectLoadCheckpoint_Validate(FName Name) { return true; }
void AClearanceOperatorPC::Server_InjectLoadCheckpoint_Implementation(FName Name)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld())) { C->LoadCheckpoint(Name); }
}

bool AClearanceOperatorPC::Server_InjectDeleteCheckpoint_Validate(FName Name) { return true; }
void AClearanceOperatorPC::Server_InjectDeleteCheckpoint_Implementation(FName Name)
{
	if (AClearanceSimulationController* C = FindSimController(GetWorld())) { C->DeleteCheckpoint(Name); }
}

// --- Operator console button helpers ---------------------------------------
//
// Called from AClearanceOperatorButton::HandlePress / HandleRelease when a
// motion-controller fingertip fires the trigger over one of the physical
// buttons on the tower mesh. All client-local: selection state lives on
// the local instructor-panel widget, PTT gates the local mic. - TripleA

// InstructorPanel widget class is instantiated per host: once for the desktop
// viewport (the instructor client's window) AND once per world-space
// WidgetComponent (the diegetic VR monitor mesh on the operator's tower cab).
// The operator sits SERVER-SIDE where PC->InstructorPanel is intentionally
// null (created only on the instructor client, see BeginPlay's role gate).
// So both the "read aircraft list" side and the "write selection" side have
// to route through whichever widget instances actually exist in the world -
// which for the operator is the VR monitor's WidgetComponent-owned instance.
// GetAllWidgetsOfClass finds those regardless of host role. - TripleA

// Grab any live InstructorPanel instance - all instances see the same
// replicated aircraft list from the sim controller so any is fine as the
// data source. - TripleA
static UClearanceInstructorPanel* FindAnyInstructorPanel(UObject* WorldContext)
{
	if (!WorldContext) { return nullptr; }
	TArray<UUserWidget*> Found;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(WorldContext, Found,
		UClearanceInstructorPanel::StaticClass(), /*TopLevelOnly*/ false);
	for (UUserWidget* W : Found)
	{
		if (auto* Panel = Cast<UClearanceInstructorPanel>(W))
		{
			return Panel;
		}
	}
	return nullptr;
}

static void PushSelectedCallsignToAllPanels(UObject* WorldContext, FName Callsign)
{
	if (!WorldContext) { return; }
	TArray<UUserWidget*> Found;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(WorldContext, Found,
		UClearanceInstructorPanel::StaticClass(), /*TopLevelOnly*/ false);
	for (UUserWidget* W : Found)
	{
		if (auto* Panel = Cast<UClearanceInstructorPanel>(W))
		{
			Panel->SetSelectedCallsign(Callsign);
		}
	}
}

void AClearanceOperatorPC::SelectNextAircraft()
{
	UClearanceInstructorPanel* Panel = FindAnyInstructorPanel(this);
	if (!Panel) { return; }
	const TArray<FInstructorAircraftRow> Rows = Panel->GetAircraftRows();
	if (Rows.Num() == 0)
	{
		PushSelectedCallsignToAllPanels(this, NAME_None);
		return;
	}

	const FName Current = Panel->GetSelectedCallsign();
	int32 Next = 0;
	if (Current != NAME_None)
	{
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			if (Rows[i].Callsign == Current) { Next = (i + 1) % Rows.Num(); break; }
		}
	}
	PushSelectedCallsignToAllPanels(this, Rows[Next].Callsign);
}

void AClearanceOperatorPC::SelectPreviousAircraft()
{
	UClearanceInstructorPanel* Panel = FindAnyInstructorPanel(this);
	if (!Panel) { return; }
	const TArray<FInstructorAircraftRow> Rows = Panel->GetAircraftRows();
	if (Rows.Num() == 0)
	{
		PushSelectedCallsignToAllPanels(this, NAME_None);
		return;
	}

	const FName Current = Panel->GetSelectedCallsign();
	int32 Prev = Rows.Num() - 1;
	if (Current != NAME_None)
	{
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			if (Rows[i].Callsign == Current) { Prev = (i - 1 + Rows.Num()) % Rows.Num(); break; }
		}
	}
	PushSelectedCallsignToAllPanels(this, Rows[Prev].Callsign);
}

void AClearanceOperatorPC::ClearAircraftSelection()
{
	PushSelectedCallsignToAllPanels(this, NAME_None);
}

void AClearanceOperatorPC::StartPTT()
{
	if (!CachedVoiceInput)
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AClearanceVoiceInput> It(World); It; ++It)
			{
				CachedVoiceInput = *It;
				break;
			}
		}
	}
	if (CachedVoiceInput) { CachedVoiceInput->StartListening(); }
}

void AClearanceOperatorPC::StopPTT()
{
	if (CachedVoiceInput) { CachedVoiceInput->StopListening(); }
}

// --- Replication ------------------------------------------------------------

void AClearanceOperatorPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Owning client only - only the operator whose PC this is needs to see
	// their own tx frequency (drives the console freq lamp + local PTT
	// gating). Other peers don't need it. - TripleA
	DOREPLIFETIME_CONDITION(AClearanceOperatorPC, CurrentTxFrequency, COND_OwnerOnly);
}

// --- Frequency bank ---------------------------------------------------------

static const TCHAR* FreqLabel(ECommsFrequency F)
{
	switch (F)
	{
	case ECommsFrequency::Tower:     return TEXT("TWR");
	case ECommsFrequency::Approach:  return TEXT("APP");
	case ECommsFrequency::Emergency: return TEXT("EMRG");
	case ECommsFrequency::Guard:     return TEXT("GRD");
	default:                         return TEXT("---");
	}
}

bool AClearanceOperatorPC::Server_SetTxFrequency_Validate(ECommsFrequency)
{
	return true;
}

void AClearanceOperatorPC::Server_SetTxFrequency_Implementation(ECommsFrequency NewFreq)
{
	if (CurrentTxFrequency == NewFreq) { return; }
	CurrentTxFrequency = NewFreq;

	// Announce the switch in the transcript so the reviewer can trace which
	// channel every transmission went on. Uses the sim controller's system
	// logger if available; falls back silently in bare-actor tests. - TripleA
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AClearanceSimulationController> It(World); It; ++It)
		{
			if (AClearanceSimulationController* SC = *It)
			{
				SC->LogTranscriptSystem(NAME_None, FString::Printf(TEXT("Freq -> %s"), FreqLabel(NewFreq)));
				break;
			}
		}
	}

	// Hand a client-side hook via OnRep so the UI can react to server-side
	// forced changes (e.g. auto-tune to Emergency when an aircraft flagged
	// as focused declares mayday). - TripleA
	if (IsLocalController())
	{
		OnRep_CurrentTxFrequency();
	}
}

void AClearanceOperatorPC::OnRep_CurrentTxFrequency()
{
	// Placeholder hook - UMG panel binds here to drive the console freq lamp
	// and the mic-gate widget colouring. No behaviour yet; comms-router
	// filter will read CurrentTxFrequency directly on the server. - TripleA
}

void AClearanceOperatorPC::SetTxFreqTower()     { Server_SetTxFrequency(ECommsFrequency::Tower);     }
void AClearanceOperatorPC::SetTxFreqApproach()  { Server_SetTxFrequency(ECommsFrequency::Approach);  }
void AClearanceOperatorPC::SetTxFreqEmergency() { Server_SetTxFrequency(ECommsFrequency::Emergency); }
void AClearanceOperatorPC::SetTxFreqGuard()     { Server_SetTxFrequency(ECommsFrequency::Guard);     }

// --- Crash alarm ------------------------------------------------------------

void AClearanceOperatorPC::FireCrashAlarm()
{
	// Read the currently selected aircraft (may be None) so the per-aircraft
	// tag is optional - operator can slam the alarm without a specific
	// callsign focused. Uses the same panel-fanout lookup the select buttons
	// use so it works on the server side where PC->InstructorPanel is null. - TripleA
	FName CrashCallsign = NAME_None;
	TArray<UUserWidget*> Panels;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Panels,
		UClearanceInstructorPanel::StaticClass(), /*TopLevelOnly*/ false);
	for (UUserWidget* W : Panels)
	{
		if (auto* Panel = Cast<UClearanceInstructorPanel>(W))
		{
			const FName Cs = Panel->GetSelectedCallsign();
			if (Cs != NAME_None) { CrashCallsign = Cs; break; }
		}
	}

	AClearanceSimulationController* SC = nullptr;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AClearanceSimulationController> It(World); It; ++It)
		{
			SC = *It;
			break;
		}
	}
	if (!SC) { return; }

	const FString Body = CrashCallsign != NAME_None
		? FString::Printf(TEXT("CRASH ALARM: %s"), *CrashCallsign.ToString())
		: TEXT("CRASH ALARM ACTIVATED");
	SC->PushNotification(Body, FColor::Red, /*LifetimeSec*/ 12.f);
	SC->LogTranscriptSystem(CrashCallsign, Body);
}
