#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "Core/CLEARANCETypes.h"
#include "UI/ClearanceInstructorTypes.h"
#include "ClearanceSimulationController.generated.h"

class AClearanceAirspaceManager;
class AClearanceAircraftSpawner;
class UClearanceAircraftBehaviour;
class UClearanceInstructionValidator;
class UClearanceConflictDetector;
class UClearanceCommsRouter;
class UClearanceScoring;
class UClearanceSessionRecorder;
class UClearanceDISEmitter;
class UClearanceDDSEmitter;
class UClearanceDDSReceiver;
class UClearanceDISReceiver;
class UClearanceRTIEmitter;
class UClearanceHLAEmitter;
class UClearanceRadar;
class ACameraActor;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

// Fixed instructor views. Free-cam is intentionally out so the player can't roam. - TripleA
UENUM(BlueprintType)
enum class EClearanceCameraView : uint8
{
	Default     UMETA(DisplayName = "Default (player pawn)"),
	Overview    UMETA(DisplayName = "Sector overview"),
	Tower       UMETA(DisplayName = "Tower (active runway threshold)"),
	Approach    UMETA(DisplayName = "Far end of approach"),
	Follow      UMETA(DisplayName = "Chase a chosen aircraft"),
	Operator    UMETA(DisplayName = "Operator POV (what the trainee sees)")
};

// Sub-angles for the Follow camera. - TripleA
UENUM(BlueprintType)
enum class EClearanceFollowAngle : uint8
{
	Chase    UMETA(DisplayName = "Chase (behind + above)"),
	Cockpit  UMETA(DisplayName = "Cockpit (forward view)"),
	Side     UMETA(DisplayName = "Wing/side"),
	Top      UMETA(DisplayName = "Top-down on aircraft")
};

// One assignable aircraft model, with its OWN facing correction and size - so
// different meshes (even within the same category) tune independently. - TripleA
USTRUCT(BlueprintType)
struct FAircraftVisualVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals")
	TSubclassOf<AActor> AircraftClass;

	// Add to the computed yaw if this mesh's forward axis isn't +X.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals")
	float YawOffsetDeg = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Visuals")
	float Scale = 1.f;
};

// A spawned visual we're tracking, plus the yaw offset of the variant it used
// (kept per-aircraft because each variant can face a different way).
USTRUCT()
struct FSpawnedAircraftVisual
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AActor> Actor = nullptr;

	float YawOffsetDeg = 0.f;
};

// One entry in the player-facing notification ring. Server pushes, replicates
// down, client HUD draws the recent ones with a fade based on age. - TripleA
USTRUCT(BlueprintType)
struct FClearanceNotification
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) float ServerTimeAdded = 0.f;
	UPROPERTY(BlueprintReadOnly) FColor Colour = FColor::White;
	UPROPERTY(BlueprintReadOnly) float LifetimeSec = 6.f;
};

// The conductor. Creates and owns every system, binds them together with
// delegates, owns the per-aircraft Behaviour objects, and runs the authoritative
// tick order each frame. This is the one Actor you drop in a level to run the sim.
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceSimulationController : public AActor
{
	GENERATED_BODY()

public:
	AClearanceSimulationController();

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void StartSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void PauseSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void ResumeSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void EndSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	bool IsSessionActive() const { return bSessionActive && !bPaused; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	float GetSessionTime() const { return SessionTime; }

	// The UI's single way in: build an instruction, send it here. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	EInstructionResult PlayerIssueInstruction(const FAircraftInstruction& Instruction);

	// Toggle the Simulink cascade autopilot for one aircraft. When engaged,
	// the aircraft's speed / bank / climb rate come from the Embedded-
	// Coder-generated PID loops instead of the built-in slew logic. Returns
	// false if the callsign isn't in the sim. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Autopilot")
	bool SetAircraftAutopilotEngaged(FName Callsign, bool bEngaged);

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	AClearanceAirspaceManager* GetAirspaceManager() const { return AirspaceManager; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceScoring* GetScoring() const { return Scoring; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceCommsRouter* GetCommsRouter() const { return CommsRouter; }

	// Lazy-init so the panel - which runs on the client and may poll before
	// BeginPlay has finished initialising the controller - always gets a
	// valid recorder back. NewObject is idempotent under the if-guard. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceSessionRecorder* GetRecorder();

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceDISEmitter* GetDISEmitter() const { return DISEmitter; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceDISReceiver* GetDISReceiver() const { return DISReceiver; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceDDSEmitter* GetDDSEmitter() const { return DDSEmitter; }

	// Access the RTI Connext emitter. Same shape as GetDDSEmitter but
	// bound against RTI's commercial runtime. Server-only. - TripleA
	UClearanceRTIEmitter* GetRTIEmitter() const { return RTIEmitter; }

	// Server RPC-ish entry: create + start the RTI emitter on a given
	// domain. Called by AClearanceOperatorPC::Server_InjectStartRTIEmit
	// which the console command clearance.rti.start goes through. - TripleA
	bool StartRTIEmitter(int32 DomainId);
	void StopRTIEmitter();
	bool IsRTIEmitting() const;
	int32 GetRTIPacketsSent() const;

	// Access the HLA federate. Fourth interoperability wire alongside
	// DIS + DDS + RTI. Server-only. - TripleA
	UClearanceHLAEmitter* GetHLAEmitter() const { return HLAEmitter; }

	// Server-side federation join + resign. FederationName / FederateName
	// / FomModulePath drive the RTIambassador; typically routed from
	// AClearanceOperatorPC's Server_InjectStartHLAJoin RPC. - TripleA
	bool StartHLAFederate(const FString& FederationName, const FString& FederateName, const FString& FomModulePath);
	void StopHLAFederate();
	bool IsHLAJoined() const;
	int32 GetHLAUpdatesSent() const;

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceDDSReceiver* GetDDSReceiver() const { return DDSReceiver; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceRadar* GetRadar() const { return Radar; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Radar")
	void SetRadarEnabled(bool bInEnabled);

	// --- GCI / Air Defence --------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Simulation|GCI")
	void SetGCIModeEnabled(bool bInEnabled);

	UFUNCTION(BlueprintCallable, Category = "Simulation|GCI")
	bool IsGCIModeEnabled() const { return bGCIMode; }

	// Operator manually classifies a contact (e.g. after IFF interrogation or VID).
	// bAsInstructor = true bypasses the mis-ID scoring penalty - the instructor
	// reclassifying a contact via the inject panel isn't the operator making a
	// call, it's god-mode scenario tuning. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|GCI")
	void ClassifyAircraft(FName Callsign, EThreatClass NewClass, bool bAsInstructor = false);

	// Inject an emergency on a named aircraft - used by the scenario runner to script
	// scheduled emergencies. Mirrors the random-injection path: sets emergency type,
	// squawk code, timestamp, and (for fuel) starting fuel. Comms-failure aircraft
	// auto-fly the published lost-comms procedure on next tick. - TripleA
	// TimerMinutes overrides the default countdown - 5 min fuel / 7 min
	// mayday. Pass <= 0 to use the class-default values. Ignored for Hijack
	// and CommsFailure (no timer). - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Emergency")
	bool DeclareEmergencyOn(FName Callsign, EEmergencyType Kind, float TimerMinutes = -1.f);

	// Multicast the appropriate audible cue for a declared emergency:
	// Mayday / FuelLow -> voiced pilot mayday call; CommsFailure -> 2-sec
	// static (broken radio); Hijack -> brief carrier blip. Shared by the
	// random-tick emergency path AND the instructor inject path so both
	// sound identical on the client. - TripleA
	void AnnounceEmergency(FName Callsign, EEmergencyType Kind, const FString& EmergencyDetail);

	// Reset an active emergency back to normal flight: clears ActiveEmergency,
	// resets squawk to 1200 (civilian VFR default), drops EmergencyDetail. The
	// aircraft's behaviour loop sees the cleared state next tick and stops the
	// emergency procedure. Instructor god-mode use only. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Emergency")
	bool ClearEmergencyOn(FName Callsign);

	// Snapshot of every aircraft in an abnormal state (Mayday / FuelLow /
	// CommsFailure / Hijack). Sorted by urgency - shortest timer first;
	// timerless emergencies (Hijack, NORDO) rank ahead of any timed one to
	// stay pinned at the top of the operator's board. Backing store is the
	// airspace manager's replicated aircraft states, so this is safe to call
	// from either server or client and refreshes for free every tick. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Emergency")
	TArray<FOperatorEmergencyEntry> GetActiveEmergencies() const;

	UFUNCTION(BlueprintCallable, Category = "Simulation|Scenario")
	class UClearanceScenarioRunner* GetScenarioRunner() const { return ScenarioRunner; }

	// ----- Replicated voice / cockpit audio (Phase 6) -------------------------
	// Server hits these to push pilot readbacks, mayday calls, GPWS alarms, and
	// radio-static cues out to every peer. Each client has its own placed
	// AClearanceVoiceOutput actor (with its own local Piper TTS server), so the
	// payload is just the line + voice tag - each peer synthesises its own audio
	// locally. Far cheaper than shipping PCM blobs and keeps every machine in
	// sync with what the server "hears". - TripleA
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayTTS(FName Callsign, const FString& Text, const FString& VoiceTag, bool bPanic);

	// Cockpit cues that aren't synthesised lines - 0 = radio static (Duration =
	// seconds), 1 = GPWS terrain alarm for the named callsign. - TripleA
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayCockpitCue(FName Callsign, uint8 Kind, float Duration);

	// ----- Instructor station server RPCs ------------------------------------
	// Clients (instructor station) call these to mutate the authoritative sim.
	// All inject paths route through here so the server stays the only writer. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Simulation|Instructor")
	void Server_InjectEmergency(FName Callsign, EEmergencyType Kind, float TimerMinutes = -1.f);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Simulation|Instructor")
	void Server_InjectClassify(FName Callsign, EThreatClass NewClass);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Simulation|Instructor")
	void Server_InjectScramble(FName BanditCallsign);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Simulation|Instructor")
	void Server_InjectSetWind(float DirectionDeg, float SpeedKts);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Simulation|Instructor")
	void Server_InjectSpawn();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Simulation|Instructor")
	void Server_InjectClearTraffic();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Simulation|Instructor")
	void Server_InjectLoadScenario(const FString& ScenarioName);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Simulation|Instructor")
	void Server_InjectStopScenario();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Simulation|Instructor")
	void Server_InjectSetPaused(bool bNewPaused);

	// Scenario sets this true on Start() to disable all placed RestrictedArea +
	// ViolationZone checks for the duration of the scripted run. Stops level-
	// placed zones from polluting a scenario that didn't author them. - TripleA
	UPROPERTY(BlueprintReadWrite, Category = "Simulation|Scenario")
	bool bZoneChecksSuspended = false;

	// IFF interrogation - returns the contact's true class + squawk if its IFF is on.
	// Returns Unknown / 0 / false otherwise.
	UFUNCTION(BlueprintCallable, Category = "Simulation|GCI")
	bool InterrogateIFF(FName Callsign, EThreatClass& OutClass, int32& OutSquawk);

	// Vector a friendly fighter onto an intercept course with a target. Computes the
	// lead-pursuit heading and issues a HeadingChange instruction. Returns false if
	// no solution (fighter too slow to ever catch target).
	UFUNCTION(BlueprintCallable, Category = "Simulation|GCI")
	bool VectorIntercept(FName FighterCallsign, FName TargetCallsign);

	// Launch a 3-ship intercept flight from the sector boundary onto the target,
	// auto-classifying the target as Hostile if it wasn't already. Returns how many
	// fighters made it out (3 unless the boundary spawn rejected one). - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|GCI")
	int32 ScrambleInterceptors(FName BanditCallsign);

	// Launch a 3-ship to SHADOW a hijacked aircraft - same boundary spawn + intercept
	// vector as Scramble, but the target is NOT classified hostile and the formation
	// rejoin DOESN'T turn the target outward. Fighters tail the aircraft to the
	// extent possible; success is the aircraft landing safely, failure is it
	// reaching a violation zone. Only valid against an aircraft squawking 7500. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|GCI")
	int32 ShadowEscort(FName HijackCallsign);

	// --- DIS interop --------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Simulation|DIS")
	bool StartDIS(const FString& Host, int32 Port);

	UFUNCTION(BlueprintCallable, Category = "Simulation|DIS")
	void StopDIS();

	// Bind a UDP port and start ingesting Entity State PDUs from anyone else on
	// the network publishing DIS. Their aircraft appear on our scope as external
	// traffic (no local Behaviour, not commandable by the player, not re-emitted).
	UFUNCTION(BlueprintCallable, Category = "Simulation|DIS")
	bool StartDISReceiver(int32 Port);

	UFUNCTION(BlueprintCallable, Category = "Simulation|DIS")
	void StopDISReceiver();

	// --- DDS pub/sub interop ------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Simulation|DDS")
	bool StartDDSEmitter(int32 DomainId);

	UFUNCTION(BlueprintCallable, Category = "Simulation|DDS")
	void StopDDSEmitter();

	UFUNCTION(BlueprintPure, Category = "Simulation|DDS")
	bool IsDDSEmitting() const;

	UFUNCTION(BlueprintPure, Category = "Simulation|DDS")
	int32 GetDDSPacketsSent() const;

	// Ingest peer federate aircraft over DDS. Subscribes to the same
	// clearance/aircraft/state topic we publish and lands external
	// aircraft into the AirspaceManager. Sibling to StartDISReceiver. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|DDS")
	bool StartDDSReceiver(int32 DomainId);

	UFUNCTION(BlueprintCallable, Category = "Simulation|DDS")
	void StopDDSReceiver();

	UFUNCTION(BlueprintPure, Category = "Simulation|DDS")
	bool IsDDSReceiving() const;

	UFUNCTION(BlueprintPure, Category = "Simulation|DDS")
	int32 GetDDSAircraftIngested() const;

	// --- After-Action Review ------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	void StartRecording();

	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	void StopRecording();

	// Suspend the live sim and pose the world to the recording's timeline. Call
	// SeekReplay/SetReplaySpeed to scrub; ResumeLive() to go back to playing live.
	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	void EnterReplay();

	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	void ResumeLive();

	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	void SeekReplay(float TimeSeconds);

	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	void SetReplayPaused(bool bInPaused);

	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	void SetReplaySpeed(float Multiplier);

	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	bool IsInReplay() const { return bReplayMode; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	float GetReplayTime() const { return ReplayTime; }

	// Frozen duration of the recording at the moment EnterReplay was called -
	// stable maximum for the scrub bar (the client-side recorder's own
	// GetDurationSeconds would keep ticking up forever even during replay). - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	float GetReplayDuration() const { return ReplayDuration; }

	// Seconds-into-the-recording for each "user went live" boundary - lets
	// the scrub-bar widget paint a tick per seam. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	const TArray<float>& GetReplaySegmentSeams() const { return ReplaySegmentSeams; }

	// ATC comms transcript - operator commands, pilot readbacks / refusals,
	// system advisories. Populated automatically as the CommsRouter
	// broadcasts instruction results. The panel's Performance > Transcript
	// sub-tab binds to this. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	const TArray<FCommsTranscriptEntry>& GetTranscript() const { return Transcript; }

	// Append a system line ("Mayday declared", "Conflict alert AAL101/DLH103",
	// "Reclassifying UNK002 as Hostile" etc). Anything that isn't a direct
	// operator/pilot exchange goes through here. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	void LogTranscriptSystem(FName Callsign, const FString& Text);

	// Append an operator (ATC) or pilot (aircraft) line directly - for paths
	// that bypass the CommsRouter delegate (parser "say again", emergency
	// declarations the pilot makes on the radio, crash mayday, voice readbacks
	// etc). Speaker label is derived from Role inside Append. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	void LogTranscriptLine(EClearanceCommsRole InRole, FName Callsign, const FString& Text);

	// Snapshot the current session into a Markdown AAR report on disk under
	// <ProjectSavedDir>/Reports/Session_YYYYMMDD_HHMMSS.md. Pulls everything
	// from already-replicated state: score totals, RepScoringLog (per-category
	// timeline), Transcript (full comms log), scenario name + elapsed, wind +
	// runway. No "end of session" state required - the report is always
	// "everything since StartSession", instructor can export mid-debrief or
	// after specific incidents. Returns true + populates OutPath on success.
	// Server-only (HasAuthority) so client calls are no-ops. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	bool ExportAARReport(FString& OutPath);

	// The operator's sensor-fused radar tracks - what the trainee actually sees
	// on their scope, not the truth held by the AirspaceManager. Server fuses
	// every enabled radar site each tick (latest-paint wins; secondary returns
	// keep their DisplayCallsign, primary-only paints fall back to "PRI"),
	// replicated to the instructor panel so the instructor scope mode can
	// render exactly what the trainee is seeing - including degraded paints
	// from EW jamming, ghost contacts from chaff, dropped tracks where the
	// fusion went stale. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Radar")
	const TArray<FRadarTrack>& GetOperatorTracks() const { return RepOperatorTracks; }

	// --- Session checkpoints (training reset) -------------------------------
	// Defence-training rigs let an instructor set up a scenario, save a
	// checkpoint right before the critical moment, let the trainee try, then
	// roll back to the same state for another attempt. Same pattern in
	// CAE/L3/Lockheed courseware. Stored entirely in memory - no disk
	// persistence yet. Server-only (HasAuthority). - TripleA

	UFUNCTION(BlueprintCallable, Category = "Simulation|Checkpoints")
	void SaveCheckpoint(FName Name);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Checkpoints")
	bool LoadCheckpoint(FName Name);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Checkpoints")
	void DeleteCheckpoint(FName Name);

	// Lightweight info list of every saved checkpoint - replicated to clients
	// so the instructor panel dropdown can populate without an RPC. The full
	// payload (aircraft states, scoring log) stays server-side. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Checkpoints")
	const TArray<FClearanceCheckpointInfo>& GetCheckpoints() const { return RepCheckpoints; }

	// --- Cameras ------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera")
	void SetCameraView(EClearanceCameraView View, FName FollowCallsign = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera")
	void CycleCameraView();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera")
	void SetFollowAngle(EClearanceFollowAngle Angle);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera")
	void CycleFollowAngle();

	// --- Instructor PIP -----------------------------------------------------
	// A SceneCapture that mirrors whichever preset camera is selected for the
	// instructor's picture-in-picture feed. Independent of the operator's main
	// view target - cycling the PIP doesn't disturb whatever the operator's
	// looking at. - TripleA

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Camera|PIP")
	UTextureRenderTarget2D* GetInstructorPipRT() const { return InstructorPipRT; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void SetInstructorPipView(EClearanceCameraView View);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void CycleInstructorPipView();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Camera|PIP")
	EClearanceCameraView GetInstructorPipView() const { return InstructorPipView; }

	// Gate the SceneCapture render pass. SceneCapture is a full extra scene
	// render so we keep it off until the instructor flips the panel to
	// camera mode. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void SetInstructorPipEnabled(bool bEnabled);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Camera|PIP")
	bool IsInstructorPipEnabled() const { return bInstructorPipEnabled; }

	// Projected screen-space labels for the camera-feed HUD overlay. Walks
	// every aircraft, transforms its world position through the PIP capture's
	// view-projection matrix, and emits a label for each one inside the
	// frustum. UMG positions a widget at ScreenUV * ImageSize. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Camera|PIP")
	TArray<FInstructorCameraLabel> GetCameraLabels() const;

	// Projected world-space lines for the camera-feed HUD overlay - runway
	// centerlines, approach corridors. Lines fully behind the camera are
	// dropped; lines that straddle the near plane are clipped to it so the
	// projection doesn't go off to infinity. UMG paints each entry between
	// StartUV * ImageSize and EndUV * ImageSize. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Camera|PIP")
	TArray<FInstructorCameraLine> GetCameraOverlayLines() const;

	// Screen-space text labels for the camera-feed HUD overlay. Currently
	// emits one runway designator (e.g. "36R") per threshold so both ends
	// of every runway are marked. UMG paints each entry at ScreenUV *
	// ImageSize. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Camera|PIP")
	TArray<FInstructorCameraText> GetCameraOverlayText() const;

	// Aircraft the PIP follows in Follow mode. Per-instance state - each client
	// (and the host) tracks its own target independently from the operator's
	// main view, so the instructor can chase a contact without yanking the
	// operator's camera off whatever they were watching. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void SetInstructorPipFollowCallsign(FName Callsign);

	// Pan the Tower view left/right. A real ATC tower has 360-degree visibility
	// so a fixed angle defeats the purpose - the instructor holds arrow
	// buttons and the camera sweeps around the airfield. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void ApplyTowerYawDelta(float DeltaDeg);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Camera|PIP")
	float GetTowerYawDeg() const { return InstructorTowerYawDeg; }

	// Pan the Overview camera by a normalized image-space delta (-1..1 of
	// the camera-feed image). UMG hooks up OnMouseMove on Img_CameraFeed,
	// computes (cursor - lastCursor) / imageSize, and pushes the value in
	// here. C++ converts to world units using the current visible area so
	// the drag feels 1:1 with the cursor regardless of zoom. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void AddOverviewPan(FVector2D PanDeltaUv);

	// Zoom the Overview camera in (positive) or out (negative). Typical
	// scroll-wheel delta is +/- 0.1 per notch. Internally clamped to a
	// 0.3x-4.0x range of the default altitude. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void AddOverviewZoom(float ZoomDelta);

	// Reset pan + zoom to default. Bound to a double-click on the
	// camera-feed image (or an explicit reset button) so the instructor
	// can snap back to the sector-centred default. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void ResetOverviewView();

	// Approach view runway picker. The instructor clicks APPROACH to pop a
	// selector, picks one of these labels, the camera frames that runway. - TripleA

	// Display strings for every runway in the airspace, e.g. "RWY 27R" /
	// "RWY 27L" / "RWY 09L" / "RWY 09R". L / R / C suffixes are derived from
	// each threshold's lateral offset from its same-heading siblings. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Camera|PIP")
	TArray<FString> GetApproachRunwayLabels() const;

	// Switch to Approach view AND select the runway at the given index. The
	// index matches the order of GetApproachRunwayLabels. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void SetInstructorPipApproachRunway(int32 Index);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Camera|PIP")
	int32 GetInstructorPipApproachRunwayIndex() const { return InstructorApproachRunwayIndex; }

	// Crash-proof alternative to SetInstructorPipApproachRunway(int32). UMG
	// designer puts 4 buttons, each calls this with a hardcoded label string
	// like "RWY 27R" - no GetArrayItem, no dynamic generation, no index math.
	// Silent no-op if the label doesn't match any runway. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void PickApproachRunwayByLabel(const FString& Label);

	// Chase view sub-angle. Independent of the operator's main FollowAngle so
	// cycling here doesn't yank the operator's main camera. Cycles Chase ->
	// Cockpit -> Side -> Top -> Chase ... - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void CycleInstructorPipFollowAngleNext();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void CycleInstructorPipFollowAnglePrev();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void SetInstructorPipFollowAngle(EClearanceFollowAngle Angle);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Simulation|Camera|PIP")
	EClearanceFollowAngle GetInstructorPipFollowAngle() const { return InstructorPipFollowAngle; }

	// Explicit operator view replication. The OperatorPC pushes its
	// ControlRotation + view location at ~30Hz so the instructor PIP can
	// mirror exactly what the operator sees, regardless of pawn class or
	// rotation replication settings. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void SetOperatorViewRotation(FRotator NewRot);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera|PIP")
	void SetOperatorViewLocation(FVector NewLoc);

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceConflictDetector* GetConflictDetector() const { return ConflictDetector; }

	// HUD pulls the latest readout/diagnostic text from these each frame. We stop
	// writing to GEngine's on-screen-debug queue because PIE multi-window renders
	// it inconsistently per viewport. - TripleA
	UPROPERTY(BlueprintReadOnly, Category = "Simulation|HUD")
	FString CurrentReadout;

	UPROPERTY(BlueprintReadOnly, Category = "Simulation|HUD")
	FString CurrentDiagLine;

	// Start automatically on BeginPlay (handy for testing - just press Play).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bAutoStart = true;

	// Always-on at session start: the session is recorded for AAR replay/debrief.
	// Real ops are always under recording - no extra console step needed. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AutoStart")
	bool bAutoStartRecording = true;

	// Off by default - real air defence picture is fused from distributed sites, no
	// magic sensor at sector origin. Drop AClearanceRadarSite actors instead. Flip on
	// (or `clearance.radar on`) for a quick single-sensor god view during dev. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AutoStart")
	bool bAutoStartRadar = false;

	// Auto-flip GCI mode (threat tags, IFF lockouts) the moment any aircraft with
	// IFF off OR a non-friendly threat class enters the sector. When the sector
	// returns to clean civilian traffic, flip it off. Off = leave it manual. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AutoStart")
	bool bAutoGCIMode = true;

	// Opt-in: publish the sector over DIS to a federation peer. Defaults off so
	// solo dev sessions don't broadcast - enable per-level or per-deploy. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AutoStart")
	bool bAutoStartDIS = false;

	// Per-second probability that any normal civilian aircraft declares an
	// emergency mid-flight. Spread across the four types in EmergencyTypeMix.
	// 0 disables; ~0.001 = one emergency per ~1000 aircraft-seconds at 1x sim
	// (much more frequent at 10x sim). - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Emergencies", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EmergencyChancePerSecond = 0.001f;

	// Starting fuel minutes when an aircraft declares a fuel emergency. Ticks
	// down on REAL time so the sim time scale doesn't change the clock. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Emergencies")
	float FuelEmergencyMinutes = 5.f;

	// How long a Mayday (squawk 7700) aircraft has before the underlying situation
	// runs out of time - hijack-the-cockpit scenario, smoke in the cabin, engine
	// failure progressing to total loss. Same real-time clock as fuel. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Emergencies")
	float MaydayTimeoutMinutes = 7.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AutoStart")
	FString DISDefaultHost = TEXT("broadcast");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|AutoStart")
	int32 DISDefaultPort = 3000;

	// Time acceleration. Real ATC is slow to watch; 1 = real time, 10 = watchable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	float SimulationTimeScale = 10.f;

	// On by default for free-play sessions. Scenarios independently lock the
	// spawner via bSpawnerScenarioLocked so this stays a clean user preference. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Spawning")
	bool bAutoSpawn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Spawning")
	float SpawnIntervalSeconds = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Spawning")
	int32 MaxConcurrentAircraft = 10;

	// Optional: assign placed actors in the level; otherwise they're spawned.
	// Replicated so the client's controller can find the (also replicated) Manager
	// without doing its own TActorIterator. Server sets it on spawn, clients OnRep. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_AirspaceManager, Category = "Simulation|Refs")
	TObjectPtr<AClearanceAirspaceManager> AirspaceManager;

	UFUNCTION()
	void OnRep_AirspaceManager();

	// ----- Replicated scenario state (Phase 3) ---------------------------------
	// Server mirrors ScenarioRunner state into these every tick so the client's
	// DrawDebugView can show the SCENARIO line without having a ScenarioRunner
	// instance (it's server-only). - TripleA

	UPROPERTY(Replicated)
	bool bRepScenarioRunning = false;

	UPROPERTY(Replicated)
	FString RepScenarioName;

	UPROPERTY(Replicated)
	float RepScenarioElapsedSec = 0.f;

	UPROPERTY(Replicated)
	int32 RepScenarioFiredEvents = 0;

	UPROPERTY(Replicated)
	int32 RepScenarioTotalEvents = 0;

	UPROPERTY(Replicated)
	int32 RepScenarioFiredTriggers = 0;

	UPROPERTY(Replicated)
	int32 RepScenarioTotalTriggers = 0;

	// ----- Replicated notification ring buffer (Phase 5) -----------------------
	// Player-facing alerts (conflicts, scenario events, score deltas). The HUD
	// reads this and draws the most recent N entries with fade-out. Server pushes
	// via PushNotification, clients receive via replication. - TripleA
	UPROPERTY(Replicated)
	TArray<FClearanceNotification> RepNotifications;

	// Push a player-facing alert into the ring buffer. Server-only; replicates
	// down. Trims oldest if buffer exceeds 12 entries. - TripleA
	void PushNotification(const FString& Text, FColor Colour = FColor::White, float LifetimeSec = 6.f);

	// ----- Replicated score state (Phase 4) ------------------------------------
	// Mirror of UClearanceScoring counters so the client's SCORING line +
	// main CLEARANCE line (score, eff) show server truth instead of zeroes. - TripleA

	UPROPERTY(Replicated)
	int32 RepScoreTotal = 0;

	UPROPERTY(Replicated)
	float RepScoreEfficiencyPct = 100.f;

	UPROPERTY(Replicated)
	int32 RepScoreLandings = 0;

	UPROPERTY(Replicated)
	int32 RepScoreHandoffs = 0;

	UPROPERTY(Replicated)
	int32 RepScoreResolved = 0;

	UPROPERTY(Replicated)
	int32 RepScoreIntercepts = 0;

	UPROPERTY(Replicated)
	int32 RepScoreEmergencies = 0;

	UPROPERTY(Replicated)
	int32 RepScoreGoArounds = 0;

	UPROPERTY(Replicated)
	int32 RepScoreSepLoss = 0;

	UPROPERTY(Replicated)
	int32 RepScoreWake = 0;

	UPROPERTY(Replicated)
	int32 RepScoreTCAS = 0;

	UPROPERTY(Replicated)
	int32 RepScoreStrayed = 0;

	UPROPERTY(Replicated)
	int32 RepScoreMisID = 0;

	UPROPERTY(Replicated)
	int32 RepScoreViolated = 0;

	UPROPERTY(Replicated)
	int32 RepScoreCrashed = 0;

	UPROPERTY(Replicated)
	int32 RepScoreBusted = 0;

	UPROPERTY(Replicated)
	float RepScoreNextSpawnSec = 0.f;

	// Federation packet counters, server-mirrored from the DIS/DDS emitter+
	// receiver subobjects. The subobject pointers themselves aren't replicated
	// (they're server-only UPROPERTY() members), so the panel widget on a
	// client couldn't read counts by dereferencing them - we mirror the ints
	// here so any client sees live counts and the rate sampler on the panel
	// has non-zero deltas to work with. Updated once per emit tick. - TripleA
	UPROPERTY(Replicated)
	int32 RepDISPacketsSent = 0;

	UPROPERTY(Replicated)
	int32 RepDISPacketsReceived = 0;

	UPROPERTY(Replicated)
	int32 RepDDSPacketsSent = 0;

	UPROPERTY(Replicated)
	int32 RepDDSPacketsReceived = 0;

	UPROPERTY(Replicated)
	bool bRepDISEmitting = false;

	UPROPERTY(Replicated)
	bool bRepDISReceiving = false;

	UPROPERTY(Replicated)
	bool bRepDDSEmitting = false;

	UPROPERTY(Replicated)
	bool bRepDDSReceiving = false;

	// RTI Connext DDS mirrors - same server->client visibility pattern as
	// DIS + DDS. Only EMIT side for now since the RTI receiver isn't shipped
	// yet (publish-only MVP). - TripleA
	UPROPERTY(Replicated)
	int32 RepRTIPacketsSent = 0;

	UPROPERTY(Replicated)
	bool bRepRTIEmitting = false;

	// HLA federate mirrors - fourth wire, publish-only for MVP. - TripleA
	UPROPERTY(Replicated)
	int32 RepHLAUpdatesSent = 0;

	UPROPERTY(Replicated)
	bool bRepHLAJoined = false;

	// Full ordered list of scored events, server-mirrored from Scoring->GetSessionLog
	// in the same pass that updates the rep counters. Each entry carries TimeStamp +
	// Type + AircraftA/B + Details so the Performance tab can render per-category
	// "[mm:ss] CALLSIGN" rows under each counter. Replicated as a whole array; the
	// session log is hundreds of entries at most so the bandwidth is trivial. - TripleA
	UPROPERTY(Replicated)
	TArray<FIncidentRecord> RepScoringLog;

	UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
	const TArray<FIncidentRecord>& GetScoringLog() const { return RepScoringLog; }

	// The operator's sensor-fused radar tracks - mirrored from the per-tick
	// fusion of every enabled UClearanceRadar site. Server is the sole writer;
	// the instructor panel's operator-scope sub-mode binds via GetOperatorTracks
	// and paints these instead of the truth states, so the instructor sees
	// exactly what the trainee sees on the radar (EW-degraded confidences,
	// chaff ghost contacts, dropped tracks where fusion went stale). - TripleA
	UPROPERTY(Replicated)
	TArray<FRadarTrack> RepOperatorTracks;

	// Lightweight replicated info for the checkpoints dropdown UI. The full
	// payload (aircraft states + scoring log) stays in CheckpointStore
	// server-side; clients only see name + summary. - TripleA
	UPROPERTY(Replicated)
	TArray<FClearanceCheckpointInfo> RepCheckpoints;

	// Server-only full checkpoint payload. Not a UPROPERTY because clients
	// don't need to know about the full snapshot - the lightweight info
	// above is what the dropdown reads. - TripleA
	struct FCheckpointPayload
	{
		TArray<FAircraftState> AircraftStates;
		float SessionTime         = 0.f;
		float WindDirectionDeg    = 0.f;
		float WindSpeedKts        = 0.f;
		int32 ScoreAtSave         = 0;
		TArray<FIncidentRecord> ScoringLog;
	};
	TMap<FName, FCheckpointPayload> CheckpointStore;

	// Rebuild RepCheckpoints from CheckpointStore - call after Save or Delete
	// so clients see the dropdown update. - TripleA
	void RebuildRepCheckpoints();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Refs")
	TObjectPtr<AClearanceAircraftSpawner> Spawner;

	// Distance from sector centre at which an aircraft is considered to have left.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	float ExitRadiusNm = 50.f;

	// Wind the sector starts with. Non-zero speed makes aircraft visibly crab/drift.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Environment")
	float WindDirectionDeg = 220.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Environment")
	float WindSpeedKts = 25.f;

	// Optional runway headings the sector can choose between for the active runway.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Environment")
	TArray<float> RunwayHeadings;

	// Change the wind at runtime (re-picks the active runway).
	UFUNCTION(BlueprintCallable, Category = "Simulation|Environment")
	void SetWind(float DirectionDeg, float SpeedKts);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Spawning")
	void SetAutoSpawn(bool bEnabled);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Spawning")
	void SetSpawnerScenarioLocked(bool bLocked);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Spawning")
	bool SpawnOne();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Spawning")
	void ClearTraffic();

	// Throwaway debug radar so the sim can be watched before a real UI exists.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Debug")
	bool bDrawDebug = true;

	// Rotation offset (degrees) applied to every runway/heading calculation to
	// match the world's local coordinate frame to true north. Needed when the
	// sim runs on Cesium tiles: Cesium's ENU local frame at the georeference
	// origin rotates the "world forward" relative to compass north, so a
	// runway with LandingHeadingDeg=70 renders visually off by whatever the
	// local frame is rotated. Set this once for the level (typical value for
	// EGNO Warton on Cesium is around 225) and every downstream direction
	// calculation - runway draw, aircraft approach, autopilot vectors - stays
	// consistent. LandingHeadingDeg on placed runways still means true magnetic
	// bearing (070 for Warton's 07/25), which keeps ATC readbacks correct. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|World")
	float WorldNorthOffsetDeg = 0.f;

	// Internal (sim math frame) -> magnetic (operator-facing frame). Everything
	// the trainee reads on the scope - aircraft heading, wind, runway designator,
	// ATC readback numbers - goes through this so the operator sees true magnetic
	// bearings regardless of how the underlying math frame is rotated to match
	// Cesium tiles. Wrap into [0, 360). - TripleA
	UFUNCTION(BlueprintPure, Category = "Simulation|World")
	float ApplyWorldNorthOffset(float HeadingDeg) const
	{
		return FMath::Fmod(FMath::Fmod(HeadingDeg + WorldNorthOffsetDeg, 360.f) + 360.f, 360.f);
	}

	// Magnetic (operator-facing) -> internal (sim math frame). Every operator
	// INPUT of a bearing - "cleared heading 070", wind SetWind(DirDeg), a
	// scripted vector in a scenario file - runs through here on ingest so the
	// stored value is in the frame the motion/draw code uses. Inverse of
	// ApplyWorldNorthOffset. - TripleA
	UFUNCTION(BlueprintPure, Category = "Simulation|World")
	float RemoveWorldNorthOffset(float HeadingDeg) const
	{
		return FMath::Fmod(FMath::Fmod(HeadingDeg - WorldNorthOffsetDeg, 360.f) + 360.f, 360.f);
	}

	// Central radar tuning - the one toggled by `clearance.radar on/off`. Sits at the
	// Controller's location, acts as the "centre" sensor in fusion. Placed RadarSite
	// actors carry their own tuning and stack on top. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Radar (Central)")
	float CentralRadarRangeNm = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Radar (Central)")
	float CentralRadarSweepRpm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Radar (Central)")
	float CentralRadarSecondaryReturnChance = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Radar (Central)")
	float CentralRadarPositionJitterNm = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Radar (Central)")
	float CentralRadarTrackFadeSeconds = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Radar (Central)")
	FName CentralRadarSiteName = TEXT("CENTRE");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Debug")
	float WorldUnitsPerNm = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Debug")
	float AltitudeWorldScale = 2.f;

	// Altitude is mapped to height with a curve so low altitudes stay gently scaled (a
	// shallow, natural approach + flare) while high altitudes tower for dramatic cruise.
	// 1 = linear (no curve); higher = more exaggeration up top, gentler down low.
	// AltitudeWorldScale is the scale exactly at AltitudeCurveRefFt. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Debug")
	float AltitudeCurveExponent = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Debug")
	float AltitudeCurveRefFt = 30000.f;

	// At or below this height (ft) an approaching/departing aircraft flies gear-down;
	// above it the gear is up. Fed to the aircraft Blueprint's visual interface.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Visuals")
	float GearDownAltitudeFt = 2500.f;

	// How far past the threshold (metres) aircraft aim to touch down - the touchdown
	// zone, so they plant in the runway instead of on the edge. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Visuals")
	float TouchdownZoneMeters = 1000.f;

	// Model variants per wake category - add several per class, each with its own
	// yaw/scale; one is chosen at random per spawn. A category left empty falls
	// back to debug spheres for those aircraft.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Visuals")
	TArray<FAircraftVisualVariant> LightVariants;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Visuals")
	TArray<FAircraftVisualVariant> MediumVariants;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Visuals")
	TArray<FAircraftVisualVariant> HeavyVariants;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Visuals")
	TArray<FAircraftVisualVariant> SuperVariants;

	// Military aircraft (bIsMilitary on the state) pick from here instead of the
	// civilian wake-category pools - drop your F-35 / fighter meshes here. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Visuals")
	TArray<FAircraftVisualVariant> FighterVariants;

	// Hostile aircraft (ThreatClass == Hostile) pick from this pool first - bandits
	// get a MiG, friendly military still gets the F-35 above. Falls back to the
	// fighter pool if empty.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Visuals")
	TArray<FAircraftVisualVariant> HostileVariants;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY()
	TObjectPtr<UClearanceInstructionValidator> Validator;

	UPROPERTY()
	TObjectPtr<UClearanceConflictDetector> ConflictDetector;

	UPROPERTY()
	TObjectPtr<UClearanceCommsRouter> CommsRouter;

	UPROPERTY()
	TObjectPtr<UClearanceScoring> Scoring;

	UPROPERTY()
	TObjectPtr<UClearanceSessionRecorder> Recorder;

	UPROPERTY()
	TObjectPtr<UClearanceDISEmitter> DISEmitter;

	UPROPERTY()
	TObjectPtr<UClearanceDISReceiver> DISReceiver;

	// Fast DDS pub/sub emitter - real-time telemetry over the OMG DDS
	// standard. Runs on the same tick as the DIS emitter so a federation
	// observer sees the same six data primitives simultaneously on both
	// wires. Instructor panel exposes START/STOP DDS controls next to the
	// DIS FEDERATION section. - TripleA
	UPROPERTY()
	TObjectPtr<UClearanceDDSEmitter> DDSEmitter;

	UPROPERTY()
	TObjectPtr<UClearanceDDSReceiver> DDSReceiver;

	// RTI Connext emitter - the third wire alongside DIS + Fast DDS.
	// Same six-topic schema, same identity fields, but bound against
	// RTI's commercial DDS runtime. Server-only UPROPERTY() so subobject
	// pointer lives only on the authoritative side; panel visibility
	// (rate/count for the UI) goes through the same replicated mirror
	// path as the other wires. - TripleA
	UPROPERTY()
	TObjectPtr<UClearanceRTIEmitter> RTIEmitter;

	// OpenRTI HLA-Evolved federate - the fourth wire, IEEE 1516-2010.
	// Server-only UPROPERTY() with the subobject pointer unreplicated;
	// panel visibility flows through replicated ints/bool mirrors
	// below. - TripleA
	UPROPERTY()
	TObjectPtr<UClearanceHLAEmitter> HLAEmitter;

	// Pending Fire / Detonation events queued between DIS emit ticks. The sim
	// event points (SCRAMBLE launch, intercept success) push here; the emitter
	// tick drains and publishes them, then clears the queue. Keeps the event
	// sites free of DIS-emitter dependency and ensures no lost PDUs even if
	// several events fire between ticks. - TripleA
	UPROPERTY()
	TArray<FWeaponsFireEvent> PendingFireEvents;

	UPROPERTY()
	TArray<FWeaponsDetonationEvent> PendingDetonationEvents;

	// Queue of transcribed radio transmissions waiting to be published as
	// Signal PDUs on the next DIS tick. LogTranscriptLine feeds this. - TripleA
	UPROPERTY()
	TArray<FVoiceCommsEvent> PendingVoiceEvents;

	// Monotonic per-launch counter for the Event ID field. Wraps at 16 bits
	// which is DIS's natural width for the field. - TripleA
	uint32 NextFireEventNumber = 1;

	UPROPERTY()
	TObjectPtr<UClearanceRadar> Radar;

	UPROPERTY()
	TObjectPtr<class UClearanceScenarioRunner> ScenarioRunner;

	// Replicated so the client-side instructor panel can poll them every paint
	// for the scrub bar / play-pause icon / speed indicator without an RPC
	// round-trip. Writes happen only on the server (via the Server_Inject*
	// RPCs on the OperatorPC). - TripleA
	UPROPERTY(Replicated)
	bool bReplayMode = false;
	UPROPERTY(Replicated)
	bool bReplayPaused = false;
	UPROPERTY(Replicated)
	float ReplayTime = 0.f;          // seconds-into-the-recording we're posing the world to
	UPROPERTY(Replicated)
	float ReplaySpeed = 1.f;

	// Frozen-at-EnterReplay duration of the recording, replicated so the
	// scrub bar has a stable maximum. Client-side Recorder->GetDurationSeconds()
	// keeps growing (or returns stale data during the RPC round-trip), so
	// the UI binds to this instead. - TripleA
	UPROPERTY(Replicated)
	float ReplayDuration = 0.f;

	// Timestamps (seconds-into-the-recording) where the user went Live then
	// re-entered Replay. The scrub bar paints a small tick at each so the
	// instructor can see the boundaries between session segments without
	// scrubbing through to find them. - TripleA
	UPROPERTY(Replicated)
	TArray<float> ReplaySegmentSeams;

	// ATC comms transcript. Capped at the most recent N entries to keep the
	// replication payload bounded; the panel UI shows the tail of this list
	// in scrolling order. - TripleA
	UPROPERTY(Replicated)
	TArray<FCommsTranscriptEntry> Transcript;

private:
	UFUNCTION()
	void HandleInstructionResult(FName Callsign, FAircraftInstruction Instruction, EInstructionResult Result);

	void AppendTranscriptEntry(EClearanceCommsRole InRole, FName Callsign, const FString& Text);

public:

	// Preset cameras spawned on session start. Free-cam is intentionally not exposed.
	// Replicated so the instructor PIP on clients can resolve them - the
	// CameraActors themselves replicate (built-in), but the controller's
	// pointers do not unless we ask. - TripleA
	UPROPERTY(Replicated)
	TObjectPtr<ACameraActor> CameraOverview;
	UPROPERTY(Replicated)
	TObjectPtr<ACameraActor> CameraTower;
	UPROPERTY(Replicated)
	TObjectPtr<ACameraActor> CameraApproach;
	UPROPERTY(Replicated)
	TObjectPtr<ACameraActor> CameraFollow;
	EClearanceCameraView CurrentCameraView = EClearanceCameraView::Default;
	FName FollowTargetCallsign;
	EClearanceFollowAngle FollowAngle = EClearanceFollowAngle::Chase;

	// Instructor PIP feed. The capture component lives on the controller and
	// teleports per-tick to whichever preset camera the instructor selected.
	// One capture pass, four viewpoints. - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Simulation|Camera|PIP", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneCaptureComponent2D> InstructorPipCapture;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Simulation|Camera|PIP", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UTextureRenderTarget2D> InstructorPipRT;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Camera|PIP", meta = (AllowPrivateAccess = "true"))
	FIntPoint InstructorPipResolution = FIntPoint(1024, 768);

	// Throttle the SceneCapture - it's a second full scene render so it adds
	// real GPU cost. 60Hz gives a smooth stream that matches the host's render
	// rate; drop it to 30 if you need more headroom. 20 reads as choppy when
	// the operator is actively looking around because each capture lands on a
	// different rotation. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Camera|PIP", meta = (AllowPrivateAccess = "true", ClampMin = "1.0", ClampMax = "120.0"))
	float InstructorPipCaptureRateHz = 60.f;

	// Pawn class to look up for the Operator POV view. The PIP scans the world
	// for any pawn of this class that isn't the local player's pawn and uses
	// its first CameraComponent's world transform - that way the instructor
	// sees exactly what the trainee sees (including their VR head tracking,
	// once the HMD is wired). Leave null to fall back to any non-local pawn. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Camera|PIP", meta = (AllowPrivateAccess = "true"))
	TSubclassOf<APawn> OperatorPawnClass;

	EClearanceCameraView InstructorPipView = EClearanceCameraView::Tower;
	bool bInstructorPipEnabled = false;
	float InstructorPipCaptureAccum = 0.f;
	FName InstructorPipFollowCallsign;
	float InstructorTowerYawDeg = 0.f;
	// Persisted pan + zoom state for the Overview camera, mutated by
	// AddOverviewPan / AddOverviewZoom from UMG drag + scroll events.
	// Persists across view switches so the instructor can leave Overview,
	// come back, and find the picture they left. - TripleA
	FVector2D InstructorOverviewPanOffsetUnits = FVector2D::ZeroVector;
	float InstructorOverviewZoomLevel = 1.f;
	// Which runway threshold the Approach view is framing. Re-clicking APPROACH
	// while already in Approach mode advances to the next runway in the list so
	// the instructor can cycle through parallel pairs (27R -> 27L -> 09L ...). - TripleA
	int32 InstructorApproachRunwayIndex = 0;
	EClearanceFollowAngle InstructorPipFollowAngle = EClearanceFollowAngle::Chase;

	// Replicated operator viewpoint. Pushed from AClearanceOperatorPC::PlayerTick
	// (either directly on the host or via Server RPC from a remote client) and
	// auto-replicated out to every machine - including the instructor's, which
	// reads it in UpdateInstructorPip's Operator case. - TripleA
	UPROPERTY(Replicated)
	FRotator OperatorViewRotation = FRotator::ZeroRotator;

	UPROPERTY(Replicated)
	FVector OperatorViewLocation = FVector::ZeroVector;

	void SpawnPresetCameras();
	void UpdateFollowCamera();
	void UpdateInstructorPip(float DeltaSeconds);
	// Live world frozen on EnterReplay so ResumeLive can restore it. - TripleA
	UPROPERTY()
	FRecordedSnapshot PreReplayState;
	float PreReplaySessionTime = 0.f;
	bool bHasPreReplayState = false;

	UPROPERTY()
	TMap<FName, TObjectPtr<UClearanceAircraftBehaviour>> BehaviourMap;

	UPROPERTY()
	TMap<FName, FSpawnedAircraftVisual> VisualActors;

	bool bSessionActive = false;
	bool bPaused = false;

	// Replicated so the instructor panel (which runs on the client in a host /
	// client split) can read it via GetSessionTime() each paint without an RPC.
	// Server-only writer (the Tick accumulator) - clients just observe. - TripleA
	UPROPERTY(Replicated)
	float SessionTime = 0.f;
	bool bInitialised = false;

	bool bGCIMode = false;

	// Active intercepts (fighter -> bandit). Both aircraft go bUnderGCIControl while
	// in here; ATC commands targeting them are rejected.
	TMap<FName, FName> ActiveIntercepts;
	// Fighters that have closed within join-up range - bandit + fighter are now in the
	// escort-out phase.
	TSet<FName> JoinedIntercepts;
	// Joined fighters that have actually flown into their formation slot - from here
	// they're glued to it; before, they fly themselves in via normal behaviour.
	TSet<FName> SettledInFormation;

	// Aircraft that have actually been inside the sector at least once. CheckExits
	// only deregisters "stray" aircraft that LEFT - aircraft that spawned outside
	// (scenario incoming traffic) get a free pass until they've actually entered. - TripleA
	TSet<FName> EverEnteredSector;

	// Monotonic counter so each SCRAMBLE allocates fresh VIPER### callsigns - two
	// scrambles in the same session need different IDs or the second one's
	// RegisterAircraft would collide with the first. - TripleA
	int32 NextViperNumber = 1;

	// Targets that are being SHADOWED (hijacks) rather than escorted-out. The
	// TickGCIIntercepts join-up step skips the "turn outward" instruction for
	// these so the aircraft continues under hijacker control. - TripleA
	TSet<FName> ShadowTargets;

	// (Zone name, aircraft callsign) pairs that have already triggered the
	// catastrophic ViolationZoneBreached incident this session. One-shot per
	// pair - prevents a hostile that's stuck inside a zone from spamming the
	// penalty every tick. Cleared on session reset. - TripleA
	TSet<FName> ViolatedPairs;

	// Same one-shot semantics for civilian RestrictedAirspaceBust events. - TripleA
	TSet<FName> BustedPairs;

	// Bitmask of urgency thresholds already vocalised per emergency aircraft -
	// bit 0 = 66% remaining, bit 1 = 33% remaining, bit 2 = 10% remaining. So
	// each escalation call only fires once. Cleared on deregister. - TripleA
	TMap<FName, int32> UrgencyThresholdsHit;

	// Persistent crash markers. Drawn as smoking ground sites in the debug view
	// until session reset. - TripleA
	struct FCrashSite { FVector PositionNm; float SessionSeconds; FName Callsign; };
	TArray<FCrashSite> CrashSites;

	// Reasons for in-progress crashes so we can log them at impact. - TripleA
	TMap<FName, FString> PendingCrashReasons;

	// Pending second panic line timer per crashing aircraft - cancelled at
	// impact so the dead pilot doesn't say his final words after the wreck. - TripleA
	TMap<FName, FTimerHandle> PendingPanicTimers;

	// Begin a visible uncontrolled descent. Aircraft state goes bCrashing=true;
	// CrashAircraft is called from the per-tick descent when it hits the ground.
	void BeginCrash(const FAircraftState& S, const FString& Reason);
	void TickCrashingAircraft(float DeltaTime);
	void CrashAircraft(const FAircraftState& S, const FString& Reason);

	void TickGCIIntercepts(float DeltaTime);

	// Bandit reactive EW: hostiles (and military Unknowns) auto-jam when an
	// interceptor closes inside 25nm, auto-drop chaff at 10nm. Makes every
	// intercept run a sensor problem, not just a vectoring one. - TripleA
	void TickBanditEW(float DeltaTime);

	// Per-bandit cooldown timestamps so we don't toggle jamming every frame
	// or spam chaff. Keyed by callsign. - TripleA
	struct FBanditEWState
	{
		float LastJamToggleTime = -1000.f;
		float LastChaffDropTime = -1000.f;
		bool  bWasJammingAtIntercept = false;  // scoring bookkeeping
	};
	TMap<FName, FBanditEWState> BanditEWStates;

	// Pair keys (sorted "A|B") that have had TCAS fire on them this encounter; while
	// the pair is in here we suppress the resolution reward, because TCAS did the
	// resolving, not the player. Cleared when the pair resolves or either side leaves.
	TSet<FString> TCASPairsAwaitingResolution;

	// World Z that counts as ground/0ft - taken from the placed runway mesh so the
	// threshold marker and touchdown sit on the runway, not the controller. - TripleA
	float GroundWorldZ = 0.f;

	void InitialiseSystems();
	void BindDelegates();
	void StepSimulation(float DeltaTime);
	void CheckExits();
	void UpdateVisuals();
	void DrawDebugView();

	// Iterate every enabled radar site, latest-paint-wins merge per truth
	// callsign, write the result into RepOperatorTracks. Called from the
	// server tick alongside the other rep mirrors. - TripleA
	void RefreshOperatorTracks();
	FVector WorldPositionFor(const FAircraftState& State) const;
	// Altitude (ft) -> vertical world offset above ground, via the altitude curve.
	float AltitudeToWorldZOffset(float AltitudeFt) const;
	const TArray<FAircraftVisualVariant>& VariantsFor(EWakeCategory Category) const;

	UFUNCTION()
	void HandleAircraftRegistered(FName Callsign);

	UFUNCTION()
	void HandleAircraftDeregistered(FName Callsign);

	UFUNCTION()
	void HandleConflictDetected(FConflictEvent Conflict);

	UFUNCTION()
	void HandleConflictResolved(FConflictEvent Conflict);

	UFUNCTION()
	void HandleGoAroundRequired(FName Callsign);

	UFUNCTION()
	void HandleWakeAdvisory(FName FollowingCallsign, FName LeadingCallsign, float RequiredSeparationNm);

	UFUNCTION()
	void HandleTCASResolutionAdvisory(FName ClimberCallsign, FName DescenderCallsign, float ClimberTargetAltitudeFt, float DescenderTargetAltitudeFt);

	UFUNCTION()
	void HandleDifficultyAdjusted(float NewSpawnRate);
};
