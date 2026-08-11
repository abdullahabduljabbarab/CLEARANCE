#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/CLEARANCETypes.h"
#include "MissileWrapper.h"
#include "ClearanceMissile.generated.h"

class AClearanceAirspaceManager;
class AClearanceSimulationController;

// -----------------------------------------------------------------------
// AClearanceMissile - single in-flight missile actor. One instance per
// live missile. Owns a FMissileWrapper (Simulink-generated proportional-
// navigation guidance) and ticks it against the current target aircraft
// state pulled from the Airspace Manager each frame.
//
// Federation wiring:
//   BeginPlay        -> queues a FWeaponsFireEvent on the SimController
//   Termination      -> queues a FWeaponsDetonationEvent on the SimController
// Both flow through the existing DIS / DDS / RTI / HLA emitters, so this
// actor doesn't touch any wire code directly. - TripleA
//
// Spawned via AClearanceMissile::Fire() (static helper) or via the
// SimulationController's Server_InjectFireMissile UFUNCTION so Blueprint
// callers (Instructor Panel button) have a canonical entry point. - TripleA
// -----------------------------------------------------------------------

UCLASS(Blueprintable, BlueprintType)
class CLEARANCESIM_API AClearanceMissile : public AActor
{
	GENERATED_BODY()

public:
	AClearanceMissile();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// -------- Static helper: fire a ground SAM at the given target ------
	//
	// Server-only. Ground-launched: spawns at the fixed SAM battery point
	// (airport reference, 0/0/0 nm-ft) with an initial velocity computed
	// as a lead-collision-course toward the target's current state. The
	// generated prop-nav guidance refines the trajectory in flight.
	//
	// Returns nullptr if:
	//   - not on the authority
	//   - target callsign doesn't resolve to a valid aircraft
	//   - the SimulationController or AirspaceManager can't be found
	static AClearanceMissile* Fire(UObject* WorldContext, FName TargetCallsign);

	// -------- Read-only accessors for BP / UI --------------------------

	UFUNCTION(BlueprintPure, Category = "Missile")
	FName GetLauncherCallsign() const { return LauncherCallsign; }

	UFUNCTION(BlueprintPure, Category = "Missile")
	FName GetTargetCallsign() const { return TargetCallsign; }

	// Canonical launcher callsign for the ground SAM battery. Baked into
	// the Fire / Detonation PDU FiringEntity field so a federation
	// observer can identify all SAM traffic by this callsign. - TripleA
	static const FName GroundLauncherCallsign;

	UFUNCTION(BlueprintPure, Category = "Missile")
	int32 GetFireEventNumber() const { return FireEventNumber; }

	// Latest termination flag from the wrapper. 0 = in flight,
	// 1 = intercept, 2 = timeout, 3 = LOS reversal.
	UFUNCTION(BlueprintPure, Category = "Missile")
	int32 GetTerminationFlag() const { return TerminationFlag; }

	// True once BeginPlay has queued the Fire event, until the actor
	// is destroyed by termination.
	UFUNCTION(BlueprintPure, Category = "Missile")
	bool IsInFlight() const { return bInFlight; }

	// -------- BP hook for the visual layer -----------------------------
	//
	// Neo attaches a Niagara particle trail + mesh in the BP child.
	// This event fires when the wrapper reports termination so the BP
	// can trigger detonation VFX before the actor is destroyed. - TripleA
	UFUNCTION(BlueprintImplementableEvent, Category = "Missile")
	void OnTerminated(int32 InTerminationFlag);

protected:
	// Initial launch position in the missile model's inertial-frame
	// metres. Stamped by Fire() before FinishSpawning. Replicated so the
	// client-side visual mirror has a reference frame too. - TripleA
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Missile")
	FVector InitialLaunchPosMeters = FVector::ZeroVector;

	// UE world location of the muzzle at launch (cm). - TripleA
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Missile")
	FVector LauncherAnchorWorldLoc = FVector::ZeroVector;

	// Callsigns wire into DIS Fire / Detonation event payloads.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Missile")
	FName LauncherCallsign = NAME_None;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Missile")
	FName TargetCallsign   = NAME_None;

	// Event number stamped into the Fire PDU on launch; the matching
	// Detonation PDU carries the same number so a federation receiver
	// can pair the two. Assigned by the SimulationController at fire time.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Missile")
	int32 FireEventNumber = 0;

	// The missile's own callsign in the AirspaceManager (e.g. "MSL_1"),
	// derived from FireEventNumber at launch so it's unique per launch.
	// Replicated so the client-side visual mirror can look up the state
	// from the AirspaceManager; without this the client tick had no
	// callsign to query and the mesh stayed frozen at spawn. - TripleA
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Missile")
	FName MissileCallsign = NAME_None;

	// Latest termination flag from the wrapper.
	UPROPERTY(BlueprintReadOnly, Category = "Missile")
	int32 TerminationFlag = 0;

	// True between launch queue and actor destruction.
	UPROPERTY(BlueprintReadOnly, Category = "Missile")
	bool bInFlight = false;

	// -------- Internal ----------------------------------------------------

	// Elapsed engagement time in seconds since launch.
	double ElapsedSeconds = 0.0;

	// Tracks the last ElapsedSeconds we emitted a range/position log at,
	// so the once-per-second cadence stays per-missile instead of leaking
	// across launches via a function-local static. - TripleA
	double LastLogSeconds = -1.0;

	// The guidance wrapper. Owns the RT_MODEL_missile_T for this missile.
	// Held as a unique_ptr because FMissileWrapper is non-copyable and
	// UPROPERTY doesn't like raw non-UObject wrappers. - TripleA
	TUniquePtr<FMissileWrapper> Wrapper;

	// Missile visual - AIM-120b AMRAAM StaticMeshComponent attached to
	// the actor root. Loaded in the constructor so the missile shows in
	// every camera view (main viewport, tower PIP, chase PIP, etc.)
	// without needing debug-shape show flags on each SceneCapture2D.
	// Neo's BP_ClearanceMissile child can override the mesh / add a
	// Niagara trail on top. - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Missile")
	TObjectPtr<class UStaticMeshComponent> MissileMesh;

	// Weak refs to sim singletons resolved at BeginPlay so we don't hit
	// GetGameState every tick. Server-only.
	TWeakObjectPtr<AClearanceAirspaceManager>       AirspaceManager;
	TWeakObjectPtr<AClearanceSimulationController>  SimController;

	// Missile position in the missile model's inertial-frame metres. UE
	// world position (cm) is derived from this each tick.
	FVector MissilePosMeters = FVector::ZeroVector;
	FVector MissileVelMps    = FVector::ZeroVector;

	// Previous frame's UE world-space location, used to compute a nose
	// rotation from the actual world-space displacement. MissileVelMps'
	// direction is in real-space metres and doesn't match the visible
	// flight direction because AltitudeWorldScale expands vertical ~12x
	// vs horizontal in this project. - TripleA
	FVector LastWorldLocation = FVector::ZeroVector;
	bool    bHasLastWorldLocation = false;

	// Queues the Fire PDU on the SimController.
	void QueueFireEvent();

	// Queues the Detonation PDU on the SimController and destroys the actor.
	void OnTerminationDetected(int32 InTerminationFlag);
};
