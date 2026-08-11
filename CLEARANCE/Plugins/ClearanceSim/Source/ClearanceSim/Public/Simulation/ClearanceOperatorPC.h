// Networked instructor/operator PlayerController. Each connected player owns
// their own instance, so it's the right object to host client-to-server RPCs
// for the instructor station injects. The PC just forwards to the authoritative
// AClearanceSimulationController on the server. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceOperatorPC.generated.h"

class UUserWidget;
class UClearanceInstructorPanel;

UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceOperatorPC : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Accessor so the VR operator pawn can dismiss the desktop instructor
	// widget once it takes over. Desktop operators keep the panel; in VR
	// the diegetic scope replaces it. - TripleA
	UClearanceInstructorPanel* GetInstructorPanel() const { return InstructorPanel; }

	// Soft class so we don't force-load UMG content from C++. Default points
	// at /Game/UI/WBP_InstructorPanel. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor",
		meta = (MetaClass = "/Script/ClearanceSim.ClearanceInstructorPanel"))
	TSoftClassPtr<UClearanceInstructorPanel> InstructorPanelClass =
		TSoftClassPtr<UClearanceInstructorPanel>(FSoftObjectPath(TEXT("/Game/UI/WBP_InstructorPanel.WBP_InstructorPanel_C")));

private:
	UPROPERTY(Transient)
	TObjectPtr<UClearanceInstructorPanel> InstructorPanel;

public:
	// All RPCs reliable + validated. Each is a thin forwarder - find the local
	// AClearanceSimulationController on the server and call its method. - TripleA
	// TimerMinutes lets the instructor override the default countdown -
	// 5 min fuel / 7 min mayday. Pass <= 0 (default) to keep the built-in
	// defaults. Hijack and CommsFailure ignore the param. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectEmergency(FName Callsign, EEmergencyType Kind, float TimerMinutes = -1.f);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectClearEmergency(FName Callsign);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectClassify(FName Callsign, EThreatClass NewClass);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectScramble(FName BanditCallsign);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectSetWind(float DirectionDeg, float SpeedKts);

	// Live traffic-density cap. Clamped 1..40 server-side. Existing aircraft
	// aren't yanked if the cap is lowered - only new spawns are gated. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectSetMaxAircraft(int32 NewMax);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectSpawn();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectClearTraffic();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectLoadScenario(const FString& ScenarioName);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectStopScenario();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectSetPaused(bool bNewPaused);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectJamming(FName Callsign, bool bOn);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectChaff(FName Callsign);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectSetTimeScale(float Scale);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectResetScenario();

	// DIS federation controls - join / leave a live IEEE 1278 federation.
	// Emit broadcasts local aircraft; Recv pulls entities from external
	// simulators into the airspace. Instructor UI drives these instead
	// of the console. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|DIS")
	void Server_InjectStartDISEmit(const FString& Host, int32 Port);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|DIS")
	void Server_InjectStopDISEmit();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|DIS")
	void Server_InjectStartDISRecv(int32 Port);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|DIS")
	void Server_InjectStopDISRecv();

	// Fast DDS pub/sub emitter controls - same shape as the DIS RPCs above.
	// Start/Stop the DDS participant + all six topic writers on the given
	// domain. Runs in parallel to DIS on the sim tick. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|DDS")
	void Server_InjectStartDDSEmit(int32 DomainId);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|DDS")
	void Server_InjectStopDDSEmit();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|DDS")
	void Server_InjectStartDDSRecv(int32 DomainId);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|DDS")
	void Server_InjectStopDDSRecv();

	// Sets the federate Site ID (both DIS and DDS wires) on the SERVER-side
	// controller. Console command routes through this so client-side console
	// input reaches the authoritative emitter/receiver (the client-side ghost
	// has null DIS/DDS pointers). - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|Federation")
	void Server_InjectSetFederateSiteId(int32 NewSiteId);

	// RTI Connext DDS pub - third wire alongside DIS + Fast DDS. Same
	// server-authoritative gate: the client-side controller has a null
	// RTIEmitter pointer so the console's RPC has to reach the server
	// side to start/stop the real publisher. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|RTI")
	void Server_InjectStartRTIEmit(int32 DomainId);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|RTI")
	void Server_InjectStopRTIEmit();

	// HLA federate controls - fourth interoperability wire, IEEE 1516-2010.
	// Server_InjectStartHLAJoin creates+joins the federation; resign tears
	// down. The federate lives on the server-authoritative controller. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|HLA")
	void Server_InjectStartHLAJoin(const FString& FederationName, const FString& FederateName, const FString& FomModulePath);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|HLA")
	void Server_InjectStopHLAJoin();

	// Replay control: client UI fires these so the SERVER's controller flips
	// into replay mode and poses the world to the recording. The server then
	// replicates the airspace back so the client sees the scrubbed traffic. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectEnterReplay();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectResumeLive();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectSeekReplay(float TimeSeconds);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectSetReplayPaused(bool bInPaused);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectSetReplaySpeed(float Multiplier);

	// Instructor clicks "Export AAR" on the panel - server snapshots the
	// session into a Markdown report under <ProjectSavedDir>/Reports/. The
	// report path is announced back to all peers via PushNotification +
	// transcript so the instructor sees where the file landed. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectExportAAR();

	// Session checkpoint controls - save the live world state under a name,
	// reload it later to reset for a trainee retry. Defence training rigs
	// use this for multi-attempt scenario rehearsals. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|Checkpoints")
	void Server_InjectSaveCheckpoint(FName Name);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|Checkpoints")
	void Server_InjectLoadCheckpoint(FName Name);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|Checkpoints")
	void Server_InjectDeleteCheckpoint(FName Name);

	// Push the operator's full control rotation to the SimController so the
	// instructor PIP can mirror it. Unreliable - we send it every ~30Hz so a
	// dropped packet just leaves the instructor one frame behind. - TripleA
	UFUNCTION(Server, Unreliable, WithValidation)
	void Server_PushOperatorView(FRotator NewRot, FVector NewLoc);

	// --- Operator console button dispatch ---------------------------------
	//
	// Fired from AClearanceOperatorButton actors placed over the physical
	// buttons on the tower mesh. All are client-local: selection state
	// lives on the local instructor-panel widget, and PTT gates the local
	// microphone. Server round-trips happen naturally when a resulting
	// voice-parsed instruction reaches the sim. - TripleA

	// Cycle the panel's SelectedCallsign forward through the current
	// aircraft list (wraps at end). Used by NEXT-callsign button.
	UFUNCTION(BlueprintCallable, Category = "Operator|Console")
	void SelectNextAircraft();

	// Reverse of SelectNextAircraft (wraps at start).
	UFUNCTION(BlueprintCallable, Category = "Operator|Console")
	void SelectPreviousAircraft();

	// Clear the current selection (SelectedCallsign = NAME_None). Used by
	// UNSELECT button and after successful handoff.
	UFUNCTION(BlueprintCallable, Category = "Operator|Console")
	void ClearAircraftSelection();

	// Push-to-talk press: start mic capture on the local AClearanceVoiceInput.
	// Whisper transcribes on release; transcript flows through the existing
	// phraseology parser + PlayerIssueInstruction path. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Operator|Console")
	void StartPTT();

	// PTT release: stop mic capture, triggers whisper transcription.
	UFUNCTION(BlueprintCallable, Category = "Operator|Console")
	void StopPTT();

	// --- Frequency bank -------------------------------------------------------
	// TWR / APP / EMRG / GRD buttons switch which channel PTT transmits on.
	// Aircraft with a matching AssignedFrequency receive the voice; others
	// don't. Console indicator + transcript tag reflect the active channel. - TripleA
	UPROPERTY(ReplicatedUsing = OnRep_CurrentTxFrequency, BlueprintReadOnly, Category = "Operator|Console")
	ECommsFrequency CurrentTxFrequency = ECommsFrequency::Tower;

	UFUNCTION()
	void OnRep_CurrentTxFrequency();

	// Server RPC fired by the freq buttons. Server validates + assigns +
	// replicates back to the owning client. Also broadcasts a System line
	// into the transcript so the change is audit-visible. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Operator|Console")
	void Server_SetTxFrequency(ECommsFrequency NewFreq);

	// Convenience helpers wired into the AClearanceOperatorButton dispatch
	// switch. Each just calls Server_SetTxFrequency with its constant. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Operator|Console") void SetTxFreqTower();
	UFUNCTION(BlueprintCallable, Category = "Operator|Console") void SetTxFreqApproach();
	UFUNCTION(BlueprintCallable, Category = "Operator|Console") void SetTxFreqEmergency();
	UFUNCTION(BlueprintCallable, Category = "Operator|Console") void SetTxFreqGuard();

	// Fire the airfield crash alarm - critical-priority notification broadcast
	// to all peers, red transcript System line so the AAR captures the event,
	// and if an aircraft is currently selected the incident is tagged to that
	// callsign (per-aircraft crash callout). Alarm siren SFX comes from the
	// alarm button's PressSound, not this helper, so the sound + haptic + visual
	// flash fire in the physical location the operator pressed. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Operator|Console")
	void FireCrashAlarm();

private:
	float ViewPushAccumSec = 0.f;

	// Cached this-client's voice-input actor. Resolved on first StartPTT
	// via TActorIterator; nulls out if the actor is destroyed. - TripleA
	UPROPERTY(Transient)
	TObjectPtr<class AClearanceVoiceInput> CachedVoiceInput;
};
