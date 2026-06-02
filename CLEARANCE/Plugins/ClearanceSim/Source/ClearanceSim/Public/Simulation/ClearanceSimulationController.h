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
	UClearanceRadar* GetRadar() const { return Radar; }

	UFUNCTION(BlueprintCallable, Category = "Simulation|Radar")
	void SetRadarEnabled(bool bInEnabled);

	// --- DIS interop --------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "Simulation|DIS")
	bool StartDIS(const FString& Host, int32 Port);

	UFUNCTION(BlueprintCallable, Category = "Simulation|DIS")
	void StopDIS();

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

	// Start automatically on BeginPlay (handy for testing - just press Play).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bAutoStart = true;

	// Time acceleration. Real ATC is slow to watch; 1 = real time, 10 = watchable.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	float SimulationTimeScale = 10.f;

	// Spawning controls. For testing one aircraft, set MaxConcurrentAircraft = 1
	// (no new plane spawns until the current one leaves) or untick bAutoSpawn and
	// use the clearance.spawn console command.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Spawning")
	bool bAutoSpawn = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Spawning")
	float SpawnIntervalSeconds = 30.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Spawning")
	int32 MaxConcurrentAircraft = 10;

	// Optional: assign placed actors in the level; otherwise they're spawned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Refs")
	TObjectPtr<AClearanceAirspaceManager> AirspaceManager;

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
	bool SpawnOne();

	UFUNCTION(BlueprintCallable, Category = "Simulation|Spawning")
	void ClearTraffic();

	// Throwaway debug radar so the sim can be watched before a real UI exists.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Debug")
	bool bDrawDebug = true;

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

protected:
	virtual void BeginPlay() override;

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
	TObjectPtr<UClearanceRadar> Radar;

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
