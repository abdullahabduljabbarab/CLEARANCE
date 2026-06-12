#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "Core/CLEARANCETypes.h"
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
class UClearanceDISReceiver;
class UClearanceRadar;
class ACameraActor;

// Fixed instructor views. Free-cam is intentionally out so the player can't roam. - TripleA
UENUM(BlueprintType)
enum class EClearanceCameraView : uint8
{
	Default     UMETA(DisplayName = "Default (player pawn)"),
	Overview    UMETA(DisplayName = "Sector overview"),
	Tower       UMETA(DisplayName = "Tower (active runway threshold)"),
	Approach    UMETA(DisplayName = "Far end of approach"),
	Follow      UMETA(DisplayName = "Chase a chosen aircraft")
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

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	AClearanceAirspaceManager* GetAirspaceManager() const { return AirspaceManager; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceScoring* GetScoring() const { return Scoring; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceCommsRouter* GetCommsRouter() const { return CommsRouter; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceSessionRecorder* GetRecorder() const { return Recorder; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceDISEmitter* GetDISEmitter() const { return DISEmitter; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceDISReceiver* GetDISReceiver() const { return DISReceiver; }

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
	UFUNCTION(BlueprintCallable, Category = "Simulation|GCI")
	void ClassifyAircraft(FName Callsign, EThreatClass NewClass);

	// Inject an emergency on a named aircraft - used by the scenario runner to script
	// scheduled emergencies. Mirrors the random-injection path: sets emergency type,
	// squawk code, timestamp, and (for fuel) starting fuel. Comms-failure aircraft
	// auto-fly the published lost-comms procedure on next tick. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Emergency")
	bool DeclareEmergencyOn(FName Callsign, EEmergencyType Kind);

	// Reset an active emergency back to normal flight: clears ActiveEmergency,
	// resets squawk to 1200 (civilian VFR default), drops EmergencyDetail. The
	// aircraft's behaviour loop sees the cleared state next tick and stops the
	// emergency procedure. Instructor god-mode use only. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation|Emergency")
	bool ClearEmergencyOn(FName Callsign);

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
	void Server_InjectEmergency(FName Callsign, EEmergencyType Kind);

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

	// --- Cameras ------------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera")
	void SetCameraView(EClearanceCameraView View, FName FollowCallsign = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera")
	void CycleCameraView();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera")
	void SetFollowAngle(EClearanceFollowAngle Angle);

	UFUNCTION(BlueprintCallable, Category = "Simulation|Camera")
	void CycleFollowAngle();

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
	int32 RepScoreDepartures = 0;

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

	UPROPERTY()
	TObjectPtr<UClearanceRadar> Radar;

	UPROPERTY()
	TObjectPtr<class UClearanceScenarioRunner> ScenarioRunner;

	bool bReplayMode = false;
	bool bReplayPaused = false;
	float ReplayTime = 0.f;          // seconds-into-the-recording we're posing the world to
	float ReplaySpeed = 1.f;

	// Preset cameras spawned on session start. Free-cam is intentionally not exposed.
	UPROPERTY()
	TObjectPtr<ACameraActor> CameraOverview;
	UPROPERTY()
	TObjectPtr<ACameraActor> CameraTower;
	UPROPERTY()
	TObjectPtr<ACameraActor> CameraApproach;
	UPROPERTY()
	TObjectPtr<ACameraActor> CameraFollow;
	EClearanceCameraView CurrentCameraView = EClearanceCameraView::Default;
	FName FollowTargetCallsign;
	EClearanceFollowAngle FollowAngle = EClearanceFollowAngle::Chase;

	void SpawnPresetCameras();
	void UpdateFollowCamera();
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
