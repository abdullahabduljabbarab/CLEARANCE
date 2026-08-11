#include "Simulation/ClearanceMissile.h"
#include "Simulation/ClearanceMissileLauncher.h"
#include "Simulation/ClearanceSimulationController.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Net/UnrealNetwork.h"
#include "ClearanceDIS/ClearanceDISPDU.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"

// -----------------------------------------------------------------------
// Unit conversions between CLEARANCE (nm / ft / kts) and the missile
// model's native inertial-frame metres / m/s. Sim uses nm for horizontal
// distances and ft for altitude; missile guidance code works in metres
// throughout. Convert once at the boundary each tick. - TripleA
// -----------------------------------------------------------------------
namespace
{
	constexpr double kMetersPerNauticalMile = 1852.0;
	constexpr double kMetersPerFoot         = 0.3048;
	constexpr double kMpsPerKnot            = 0.514444;

	FVector NmFtToMeters(const FVector& PositionNm, float AltitudeFt)
	{
		return FVector(PositionNm.X * kMetersPerNauticalMile,
		               PositionNm.Y * kMetersPerNauticalMile,
		               AltitudeFt   * kMetersPerFoot);
	}

	// FAircraftState::Velocity is in nm/second (see AircraftBehaviour: it's
	// State.Speed * KnotsToNmPerSec = kts * 1/3600). Convert to m/s by
	// multiplying by metres-per-nm. The previous KtsToMps() form treated
	// the values as knots and returned ~0.07 m/s for a 500 kt aircraft,
	// which broke the launcher-offset calculation. - TripleA
	FVector NmPerSecToMps(const FVector& VelocityNmPerSec)
	{
		return VelocityNmPerSec * kMetersPerNauticalMile;
	}

	// Legacy alias retained in case any call site still uses the wrong
	// name. Wraps the correct conversion.
	FVector KtsToMps(const FVector& VelocityNmPerSec)
	{
		return NmPerSecToMps(VelocityNmPerSec);
	}

	// Reverse for pushing missile state back into a Fire / Detonation event.
	FVector2D MetersToNm2D(const FVector& PositionMeters)
	{
		return FVector2D(PositionMeters.X / kMetersPerNauticalMile,
		                 PositionMeters.Y / kMetersPerNauticalMile);
	}

	float MetersToFt(double MetersZ)
	{
		return static_cast<float>(MetersZ / kMetersPerFoot);
	}

	float MpsToKts(double Mps)
	{
		return static_cast<float>(Mps / kMpsPerKnot);
	}
}

// Canonical SAM launcher callsign. Stamped into every Fire / Detonation
// PDU FiringEntity so federation observers can identify SAM traffic by
// this callsign alone. Change once here to rebrand the battery. - TripleA
const FName AClearanceMissile::GroundLauncherCallsign = FName(TEXT("SAM_01"));

// -----------------------------------------------------------------------
// AClearanceMissile
// -----------------------------------------------------------------------

AClearanceMissile::AClearanceMissile()
{
	PrimaryActorTick.bCanEverTick = true;
	// Tick as late in the frame as possible so SetActorLocation and the
	// paired RefreshFollowCamera call land after every other actor + the
	// physics step, minimising the "camera lags mesh by one frame" that
	// reads as rubber-banding at 60 fps. - TripleA
	PrimaryActorTick.TickGroup = TG_LastDemotable;

	// Actor replicates so it exists on clients (needed for listen-server
	// / LAN sessions to render the mesh at all), but transform is NOT
	// replicated automatically - that smoother is what caused the visible
	// rubber-band at 60 fps. Instead, both server and client drive the
	// mesh transform themselves from the replicated AirspaceManager
	// state (same pattern aircraft visuals already use), which yields
	// jitter-free updates because SetActorLocation is direct. - TripleA
	bReplicates     = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);

	// Root component so the BP child can attach a mesh + Niagara trail.
	// AActor's default root is null; explicitly add a scene component so
	// the transform actually replicates. - TripleA
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("RootComp"));
	SetRootComponent(Root);

	// Attach the AIM-120b AMRAAM mesh so the missile is visible in every
	// camera view (main viewport, tower PIP, chase PIP, operator scope
	// camera feed) without needing debug-shape show flags. Mesh lives in
	// /Game/Missile/aim_120b_amraam. Constructor-only lookup - runtime
	// asset load would incur an async cost per spawn. - TripleA
	MissileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MissileMesh"));
	MissileMesh->SetupAttachment(Root);
	MissileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MissileMesh->SetCastShadow(true);
	MissileMesh->SetVisibleInSceneCaptureOnly(false);
	MissileMesh->bRenderInMainPass = true;
	MissileMesh->bRenderCustomDepth = true;   // helps PIPs that use custom-depth culling

	static ConstructorHelpers::FObjectFinder<UStaticMesh> MeshFinder(
		TEXT("/Game/Missile/aim_120b_amraam.aim_120b_amraam"));
	if (MeshFinder.Succeeded())
	{
		MissileMesh->SetStaticMesh(MeshFinder.Object);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ClearanceMissile] Could not load /Game/Missile/aim_120b_amraam - missile will be invisible."));
	}

	// Placeholder scale. 8x is the sweet spot from earlier: visible in
	// chase without swallowing the frame. - TripleA
	MissileMesh->SetRelativeScale3D(FVector(8.f, 8.f, 8.f));

	// AIM-120b .fbx is authored with the nose along -Y (Blender / Maya
	// export convention flipped from UE's +X-forward). +90 yaw aligns the
	// mesh nose with the actor's forward axis. If a re-authored asset
	// already has +X forward, drop this back to zero. - TripleA
	MissileMesh->SetRelativeRotation(FRotator(0.f, 90.f, 0.f));

	// Never distance-cull the missile mesh. At scale 3 in a compressed
	// world it's small, and UE's LOD/cull system will fade it out mid
	// flight if the camera view is a chase from behind on a fast mover.
	// The "disappears halfway to intercept" bug was exactly this. - TripleA
	MissileMesh->SetCullDistance(0);
	MissileMesh->SetCastHiddenShadow(false);
	MissileMesh->bNeverDistanceCull = true;

	// Try the project's authored material first, then fall back to a
	// known-good engine default so the mesh is never invisible. If the
	// authored one loads but still renders invisible, we'll see the
	// engine default instead which at least confirms this codepath ran. - TripleA
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MissileMatFinder(
		TEXT("/Game/Missile/Material.Material"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> FallbackMatFinder(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	UMaterialInterface* MatToUse = nullptr;
	if (MissileMatFinder.Succeeded())
	{
		MatToUse = MissileMatFinder.Object;
		UE_LOG(LogTemp, Log, TEXT("[ClearanceMissile] Loaded /Game/Missile/Material."));
	}
	else if (FallbackMatFinder.Succeeded())
	{
		MatToUse = FallbackMatFinder.Object;
		UE_LOG(LogTemp, Warning,
			TEXT("[ClearanceMissile] /Game/Missile/Material missing; falling back to BasicShapeMaterial."));
	}

	if (MatToUse)
	{
		// Loop over every material slot on the mesh in case the AMRAAM has
		// multiple sections (body + fins + nozzle). Otherwise the extra
		// slots keep the broken imported material and appear as invisible
		// gaps in the mesh. - TripleA
		const int32 NumSlots = MissileMesh->GetNumMaterials();
		for (int32 i = 0; i < FMath::Max(1, NumSlots); ++i)
		{
			MissileMesh->SetMaterial(i, MatToUse);
		}
	}
}

void AClearanceMissile::BeginPlay()
{
	Super::BeginPlay();

	// Resolve sim singletons on both sides - the client needs them so the
	// per-tick visual mirror can pull the replicated state. - TripleA
	SimController = Cast<AClearanceSimulationController>(
		UGameplayStatics::GetActorOfClass(this, AClearanceSimulationController::StaticClass()));
	if (SimController.IsValid())
	{
		AirspaceManager = SimController->GetAirspaceManager();
	}

	if (!HasAuthority())
	{
		// Guidance runs server-side only; the client Tick handles visual
		// mirroring from replicated state. Nothing else to set up here. - TripleA
		return;
	}

	if (!AirspaceManager.IsValid())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ClearanceMissile] BeginPlay: AirspaceManager not resolved, cannot guide. Destroying."));
		Destroy();
		return;
	}

	// Seed the missile-model position from the stored launcher pos.
	// SpawnActorDeferred at very large world coordinates does NOT
	// reliably place the actor there (UE clamps / world bounds), so we
	// rely on InitialLaunchPosMeters (stamped by Fire before FinishSpawn)
	// instead of GetActorLocation. Also SetActorLocation explicitly so
	// the mesh renders at the intended launch point. - TripleA
	MissilePosMeters = InitialLaunchPosMeters;
	// Place via the sim's aircraft-projection so the follow camera (which
	// uses WorldPositionFor internally) stares at the same world position
	// as the mesh. A launcher placed off the sim origin will visually
	// "teleport" the missile onto the sim grid at spawn - that's an
	// acceptable one-frame pop given the alternative was camera + mesh
	// living in different coordinate systems and the mesh appearing to
	// be invisible when chase-followed. - TripleA
	if (SimController.IsValid())
	{
		FAircraftState VisualState;
		const FVector2D PosNm = MetersToNm2D(MissilePosMeters);
		VisualState.Position = FVector(PosNm.X, PosNm.Y, 0.f);
		VisualState.Altitude = MetersToFt(MissilePosMeters.Z);
		SetActorLocation(SimController->WorldPositionFor(VisualState));
	}
	else
	{
		SetActorLocation(LauncherAnchorWorldLoc);
	}

	Wrapper = MakeUnique<FMissileWrapper>();
	Wrapper->Initialize();

	// Allocate the Fire event number + build the missile's DIS callsign
	// BEFORE queuing the Fire PDU. QueueFireEvent stamps MunitionEntityId
	// as HashCallsignToEntityNumber(MissileCallsign); if we called it
	// with MissileCallsign still None, the emitter would fall through to
	// the derived-hash path (DeriveMunitionEntityNumber(firer, event))
	// and the later Detonation PDU would use a different Munition ID -
	// federation observers pair by MunitionEntity, so a mismatch breaks
	// the correlation. - TripleA
	if (SimController.IsValid())
	{
		FireEventNumber = SimController->AllocateFireEventNumber();
	}
	MissileCallsign = FName(*FString::Printf(TEXT("MSL_%d"), FMath::Max(1, FireEventNumber)));

	QueueFireEvent();
	bInFlight = true;

	ElapsedSeconds = 0.0;

	// Publish the missile as a first-class contact in the airspace so the
	// instructor list, camera modes, and label overlay pick it up with no
	// per-caller special-casing. The SimulationController's registration
	// handler sees bIsMissile and skips Behaviour / CommsRouter / mesh-
	// variant spawn, so this doesn't fight the missile's own guidance
	// tick or duplicate the AIM-120b mesh. - TripleA

	FAircraftState S;
	S.Callsign            = MissileCallsign;
	S.Position            = FVector(MetersToNm2D(MissilePosMeters).X,
	                                MetersToNm2D(MissilePosMeters).Y, 0.f);
	S.Altitude            = MetersToFt(MissilePosMeters.Z);
	S.Speed               = MpsToKts(MissileVelMps.Size());
	S.Heading             = MissileVelMps.IsNearlyZero(1.f) ? 0.f : MissileVelMps.Rotation().Yaw;
	S.Velocity            = FVector(MissileVelMps.X / kMetersPerNauticalMile,
	                                MissileVelMps.Y / kMetersPerNauticalMile, 0.f);
	S.FlightPhase         = EFlightPhase::Enroute;
	S.bIsValid            = true;
	S.bIsMissile          = true;
	S.bUnderGCIControl    = true;                  // ATC can't clear a missile
	S.bIFFOperational     = false;                 // no transponder
	S.SquawkCode          = 0;
	S.ThreatClass         = EThreatClass::Friendly;
	S.TrueAffiliation     = EThreatClass::Friendly;
	S.WakeCategory        = EWakeCategory::Light;
	S.ServiceCeiling      = 100000.f;
	S.MaxOperatingSpeed   = 5000.f;
	AirspaceManager->RegisterAircraft(S);
}

void AClearanceMissile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AClearanceMissile, InitialLaunchPosMeters);
	DOREPLIFETIME(AClearanceMissile, LauncherAnchorWorldLoc);
	DOREPLIFETIME(AClearanceMissile, LauncherCallsign);
	DOREPLIFETIME(AClearanceMissile, TargetCallsign);
	DOREPLIFETIME(AClearanceMissile, FireEventNumber);
	DOREPLIFETIME(AClearanceMissile, MissileCallsign);
}

void AClearanceMissile::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Remove ourselves from the airspace list so the instructor UI doesn't
	// hold onto a ghost contact after detonation / timeout. - TripleA
	if (MissileCallsign != NAME_None && AirspaceManager.IsValid())
	{
		AirspaceManager->DeregisterAircraft(MissileCallsign);
		MissileCallsign = NAME_None;
	}

	if (Wrapper.IsValid())
	{
		Wrapper->Shutdown();
		Wrapper.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void AClearanceMissile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Client-side visual: mirror the missile's actual position from the
	// replicated AirspaceManager state onto the local mesh, same pattern
	// as aircraft visuals. Runs when this instance isn't authoritative
	// (multiplayer client). With SetReplicateMovement(false) the actor's
	// transform doesn't auto-sync, so this is how the client keeps up. - TripleA
	if (!HasAuthority())
	{
		if (SimController.IsValid()
			&& AirspaceManager.IsValid()
			&& MissileCallsign != NAME_None)
		{
			const FAircraftState S = AirspaceManager->GetAircraftState(MissileCallsign);
			if (S.bIsValid)
			{
				SetActorLocation(SimController->WorldPositionFor(S));
				if (!S.Velocity.IsNearlyZero(1e-4f))
				{
					SetActorRotation(S.Velocity.GetSafeNormal().Rotation());
				}
			}
		}
		return;
	}

	if (!bInFlight || !Wrapper.IsValid() || !AirspaceManager.IsValid())
	{
		return;
	}

	// Freeze the missile mid-air when the session is paused so it
	// suspends alongside the aircraft (which respect the same flag via
	// their behaviour tick). Guidance, visuals, timers all halt until
	// the operator resumes. - TripleA
	if (SimController.IsValid() && SimController->IsSessionPaused())
	{
		return;
	}

	ElapsedSeconds += static_cast<double>(DeltaSeconds);

	// Pull the target's current state each tick. If the target has been
	// removed (destroyed by another intercept, left the sector) we treat
	// it as a miss and terminate. - TripleA
	const FAircraftState Target = AirspaceManager->GetAircraftState(TargetCallsign);
	if (!Target.bIsValid)
	{
		OnTerminationDetected(3); // LOS reversal semantically (no target)
		return;
	}

	// World-frame target state.
	const FVector TargetPosWorld = NmFtToMeters(Target.Position, Target.Altitude);
	const FVector TargetVelWorld = NmPerSecToMps(Target.Velocity);

	// Transform target into the model's launcher-local frame. The
	// generated Simulink model has hardcoded initial missile position
	// R_M0 = [0,0,0] in missile_params.m, so the model always thinks the
	// missile starts at origin. We feed it target coords relative to the
	// launcher, run guidance in that local frame, then translate the
	// model's missile output back to world coords. Velocities are frame-
	// invariant (both frames are inertial). - TripleA
	// Scale the missile's time step by the sim's global time-acceleration
	// so guidance and target physics tick in the same time frame. The
	// aircraft integrator applies SimulationTimeScale internally, so its
	// world-frame velocity as observed here is already scaled up. If we
	// stepped the missile on unscaled wall-clock DeltaSeconds it would
	// close at 1/scale of the target's effective speed and never catch
	// anything at scale = 10. - TripleA
	float ScaledDelta = DeltaSeconds;
	if (SimController.IsValid())
	{
		ScaledDelta = DeltaSeconds * SimController->SimulationTimeScale;
	}

	FClearanceMissileInputs In;
	In.TargetPosMeters = TargetPosWorld - InitialLaunchPosMeters;
	In.TargetVelMps    = TargetVelWorld;
	In.ElapsedSeconds  = ElapsedSeconds;
	In.DeltaSeconds    = ScaledDelta;

	const FClearanceMissileOutputs Out = Wrapper->Step(In);

	// Translate model output back to world frame.
	MissilePosMeters = Out.MissilePosMeters + InitialLaunchPosMeters;
	MissileVelMps    = Out.MissileVelMps;
	TerminationFlag  = Out.TerminationFlag;

	// Sim-projected visual position so the follow camera and mesh live
	// in the same coordinate system. See BeginPlay comment for why we
	// prefer WorldPositionFor over anchoring to LauncherAnchorWorldLoc. - TripleA
	if (SimController.IsValid())
	{
		FAircraftState VisualState;
		const FVector2D PosNm = MetersToNm2D(MissilePosMeters);
		VisualState.Position = FVector(PosNm.X, PosNm.Y, 0.f);
		VisualState.Altitude = MetersToFt(MissilePosMeters.Z);
		SetActorLocation(SimController->WorldPositionFor(VisualState));
	}
	else
	{
		SetActorLocation(LauncherAnchorWorldLoc);
	}

	// Republish the missile's contact state so the instructor list, scope
	// symbol, and camera-follow track it. Cheap per-tick copy - the
	// AirspaceManager is designed for this update cadence for aircraft. - TripleA
	if (MissileCallsign != NAME_None && AirspaceManager.IsValid())
	{
		FAircraftState S = AirspaceManager->GetAircraftState(MissileCallsign);
		if (S.bIsValid)
		{
			const FVector2D PosNm = MetersToNm2D(MissilePosMeters);
			S.Position = FVector(PosNm.X, PosNm.Y, 0.f);
			S.Altitude = MetersToFt(MissilePosMeters.Z);
			S.Speed    = MpsToKts(MissileVelMps.Size());

			// Heading uses the sim's atan2(X, Y) convention where 0 = +Y (north).
			// Kept from the horizontal-XY components only so the scope direction
			// line follows the missile's ground track, matching how aircraft
			// heading works everywhere else. For a straight-up missile the XY
			// motion is zero and we hold the last known heading to avoid a
			// spinning-line effect. - TripleA
			const double HorizVelSq = MissileVelMps.X * MissileVelMps.X
			                        + MissileVelMps.Y * MissileVelMps.Y;
			if (HorizVelSq > 1e-4)
			{
				S.Heading = FMath::RadiansToDegrees(
					FMath::Atan2(MissileVelMps.X, MissileVelMps.Y));
			}

			// Publish full 3D velocity so the follow camera can see the
			// vertical component and orient itself properly during boost /
			// terminal-dive phases. Aircraft publish Z = 0 so their camera
			// keeps the horizontal-only heading fallback. - TripleA
			S.Velocity = FVector(MissileVelMps.X / kMetersPerNauticalMile,
			                     MissileVelMps.Y / kMetersPerNauticalMile,
			                     MissileVelMps.Z / kMetersPerNauticalMile);
			AirspaceManager->RequestStateUpdate(S);
		}
	}

	// Orient the actor along its world-space displacement, not the raw
	// metres-frame velocity - AltitudeWorldScale distorts vertical vs
	// horizontal, so the two directions don't match. Skip the first
	// frame (no previous location) and any tick where the actor barely
	// moved to avoid jitter. - TripleA
	{
		const FVector WorldNow = GetActorLocation();
		if (bHasLastWorldLocation)
		{
			const FVector WorldDelta = WorldNow - LastWorldLocation;
			if (WorldDelta.SizeSquared() > 100.f)   // >10 UE units of travel
			{
				SetActorRotation(WorldDelta.Rotation());
			}
		}
		LastWorldLocation = WorldNow;
		bHasLastWorldLocation = true;
	}

	// Diagnostic visualisation. Draws a red sphere at the missile's world
	// position and a yellow line to the target every tick. Persistent for
	// one tick so the shapes update in place. Visible in editor + PIE by
	// default; no `Show > Debug` toggle needed. - TripleA
	if (UWorld* W = GetWorld())
	{
		const FVector MissileWorld = GetActorLocation();
		const FVector TargetWorldCm = TargetPosWorld * 100.0;
		DrawDebugSphere(W, MissileWorld, 200.f, 8, FColor::Red,   false, -1.f, 0, 2.f);
		DrawDebugSphere(W, TargetWorldCm, 300.f, 8, FColor::Green, false, -1.f, 0, 2.f);
		DrawDebugLine(W, MissileWorld, TargetWorldCm, FColor::Yellow, false, -1.f, 0, 1.f);
	}

	// Log once per second so the console shows convergence progress.
	// Per-instance state - a `static` here would leak across launches and
	// silence logs on any missile spawned after the first one finished. - TripleA
	if (ElapsedSeconds - LastLogSeconds >= 1.0)
	{
		LastLogSeconds = ElapsedSeconds;
		const double RangeMeters = FVector::Dist(MissilePosMeters, TargetPosWorld);
		UE_LOG(LogTemp, Log,
			TEXT("[ClearanceMissile] t=%5.2fs range=%6.0fm  missile=(%.0f, %.0f, %.0f) target=(%.0f, %.0f, %.0f)"),
			ElapsedSeconds, RangeMeters,
			MissilePosMeters.X, MissilePosMeters.Y, MissilePosMeters.Z,
			TargetPosWorld.X, TargetPosWorld.Y, TargetPosWorld.Z);
	}

	// Force the mesh transform to propagate immediately so a same-frame
	// camera update (below) sees the mesh in its final position. Without
	// this the mesh's world matrix can trail the actor by a frame, which
	// reads as rubber-banding when viewed from a same-frame camera. - TripleA
	if (MissileMesh)
	{
		MissileMesh->UpdateComponentToWorld();
	}

	// Force the follow camera to re-target this frame if it's following us.
	// SimController's own Tick runs UpdateFollowCamera at whatever moment
	// UE schedules it - typically BEFORE our Tick within the same frame,
	// so the camera stares at last-frame's missile pos. Calling it here
	// synchronously guarantees the camera sees our fresh SetActorLocation
	// output in the same frame, killing the visible rubber-band. - TripleA
	if (SimController.IsValid() && MissileCallsign != NAME_None)
	{
		SimController->RefreshFollowCamera();
	}

	// Ignore termination flags during a short grace period after launch.
	// The Simulink model bakes V_M_INIT for the default lateral-crossing
	// scenario, so on arbitrary launches the initial velocity points the
	// wrong way and LOS_Reversal_Check can trip on tick 1 before the
	// guidance has settled. Give it 2 seconds to establish. Proper fix
	// is to regenerate the model with launch-velocity-as-input. - TripleA
	constexpr double kGuidanceSettleSeconds = 2.0;
	if (TerminationFlag != 0 && ElapsedSeconds >= kGuidanceSettleSeconds)
	{
		OnTerminationDetected(TerminationFlag);
	}
}

// -----------------------------------------------------------------------
// Fire event queue - stamps the event number, samples launcher state,
// pushes onto SimController->PendingFireEvents. The controller's DIS /
// DDS / RTI / HLA emitters drain and publish on their next tick. - TripleA
// -----------------------------------------------------------------------
void AClearanceMissile::QueueFireEvent()
{
	if (!SimController.IsValid()) { return; }

	AClearanceSimulationController* Ctrl = SimController.Get();
	// FireEventNumber may already have been allocated upfront in BeginPlay
	// so the MissileCallsign / MunitionEntity hash is stable before we
	// build the Fire event. Only allocate here if a caller wired us in
	// without doing that. - TripleA
	if (FireEventNumber <= 0)
	{
		FireEventNumber = Ctrl->AllocateFireEventNumber();
	}

	FWeaponsFireEvent Event;
	Event.FiringCallsign = LauncherCallsign;
	Event.TargetCallsign = TargetCallsign;
	Event.LocationNm     = MetersToNm2D(MissilePosMeters);
	Event.AltitudeFt     = MetersToFt(MissilePosMeters.Z);
	Event.VelocityXKts   = MpsToKts(MissileVelMps.X);
	Event.VelocityYKts   = MpsToKts(MissileVelMps.Y);
	Event.VelocityZKts   = MpsToKts(MissileVelMps.Z);
	Event.MunitionKind        = 2;    // Category = Guided (SISO-REF-010)
	Event.MunitionDomain      = 3;    // Anti-Air
	Event.MunitionSubcategory = 8;    // AIM-120 family
	Event.MunitionSpecific    = 3;    // AIM-120B
	Event.MunitionCategory    = 2;    // Guided (redundant with MunitionKind mapping but explicit)
	Event.WarheadKind    = 1000; // Annex A: 1000 = HE
	Event.FuseKind       = 1000; // Annex A: 1000 = contact
	Event.Quantity       = 1;
	Event.Rate           = 0;    // rounds/min, 0 for missile
	// Range-at-launch: straight-line distance from muzzle to target at the
	// moment we queue the Fire PDU. Fire PDU §7.4.3 wants effective range
	// as a diagnostic, not a live update. - TripleA
	{
		FVector TargetPosWorld = FVector::ZeroVector;
		if (AirspaceManager.IsValid())
		{
			const FAircraftState T = AirspaceManager->GetAircraftState(TargetCallsign);
			if (T.bIsValid)
			{
				TargetPosWorld = NmFtToMeters(T.Position, T.Altitude);
			}
		}
		Event.RangeMeters = TargetPosWorld.IsNearlyZero()
			? 0.f
			: static_cast<float>(FVector::Dist(MissilePosMeters, TargetPosWorld));
	}
	Event.EventNumber    = FireEventNumber;

	// Stamp the munition entity ID so the Fire PDU points at the same DIS
	// entity the missile broadcasts as via EmitStates - lets external
	// federates correlate the "declared munition" with the flying entity's
	// live position PDUs. Uses the exact hash EntityFromCallsign runs. - TripleA
	if (!MissileCallsign.IsNone())
	{
		const FTCHARToUTF8 Utf8(*MissileCallsign.ToString());
		Event.MunitionEntityId = static_cast<int32>(
			ClearanceDIS::HashCallsignToEntityNumber(std::string_view(Utf8.Get(), Utf8.Length())));
	}

	Ctrl->QueueFireEvent(Event);

	UE_LOG(LogTemp, Log,
		TEXT("[ClearanceMissile] Fire event #%d queued: %s -> %s at (%.2f, %.2f) nm, %.0f ft"),
		FireEventNumber,
		*LauncherCallsign.ToString(), *TargetCallsign.ToString(),
		Event.LocationNm.X, Event.LocationNm.Y, Event.AltitudeFt);
}

// -----------------------------------------------------------------------
// Detonation event queue + actor teardown. Fires the BP OnTerminated
// event so the visual layer can play detonation VFX before the actor
// is destroyed. - TripleA
// -----------------------------------------------------------------------
void AClearanceMissile::OnTerminationDetected(int32 InTerminationFlag)
{
	if (!bInFlight) { return; }
	bInFlight = false;
	TerminationFlag = InTerminationFlag;

	if (SimController.IsValid())
	{
		FWeaponsDetonationEvent Event;
		Event.FiringCallsign   = LauncherCallsign;
		Event.TargetCallsign   = (InTerminationFlag == 1) ? TargetCallsign : NAME_None;
		Event.LocationNm       = MetersToNm2D(MissilePosMeters);
		Event.AltitudeFt       = MetersToFt(MissilePosMeters.Z);
		Event.VelocityXKts     = MpsToKts(MissileVelMps.X);
		Event.VelocityYKts     = MpsToKts(MissileVelMps.Y);
		Event.VelocityZKts     = MpsToKts(MissileVelMps.Z);
		Event.MunitionKind        = 2;
		Event.MunitionDomain      = 3;
		Event.MunitionSubcategory = 8;
		Event.MunitionSpecific    = 3;
		Event.MunitionCategory    = 2;
		Event.WarheadKind      = 1000;
		Event.FuseKind         = 1000;
		Event.Quantity         = 1;
		Event.Rate             = 0;
		// IEEE 1278.1 §7.4.4 table 7-4:
		//   1 = Entity Impact  (our intercept)
		//   5 = Detonation     (proximity / timeout burn-out)
		//   6 = None           (LOS reversal - hard miss, no detonation)
		Event.DetonationResult = (InTerminationFlag == 1) ? 1 : (InTerminationFlag == 2 ? 5 : 6);
		Event.EventNumber      = FireEventNumber;

		// Match the Fire PDU's MunitionEntity so the pair reads correctly
		// on a federation observer that keys by entity ID. - TripleA
		if (!MissileCallsign.IsNone())
		{
			const FTCHARToUTF8 Utf8(*MissileCallsign.ToString());
			Event.MunitionEntityId = static_cast<int32>(
				ClearanceDIS::HashCallsignToEntityNumber(std::string_view(Utf8.Get(), Utf8.Length())));
		}

		SimController->QueueDetonationEvent(Event);

		UE_LOG(LogTemp, Log,
			TEXT("[ClearanceMissile] Detonation event #%d queued: term=%d result=%d at (%.2f, %.2f) nm"),
			FireEventNumber, InTerminationFlag, Event.DetonationResult,
			Event.LocationNm.X, Event.LocationNm.Y);

		// Intercept - route the hit through the existing crash pipeline.
		// The target aircraft goes into visible mayday-style descent and
		// eventually hits the ground, logged as an incident and marked
		// on the scope with a crash site. Reads identically to a natural
		// mayday from the operator's view. - TripleA
		if (InTerminationFlag == 1)
		{
			SimController->MissileHit(TargetCallsign,
				FString::Printf(TEXT("Missile intercept (SAM event #%d)"), FireEventNumber));
		}
	}

	// BP hook so a Blueprint child can spawn detonation VFX / sound.
	OnTerminated(InTerminationFlag);

	// Give any BP effects a tick to spawn, then remove the missile.
	// 0.5s is short enough not to accumulate stale missiles even under
	// a saturated salvo. - TripleA
	SetLifeSpan(0.5f);
}

// -----------------------------------------------------------------------
// Static fire helper - ground-launched SAM at the given target aircraft.
// Callable from a console command, a Blueprint, or another C++ system.
// Server-only.
// -----------------------------------------------------------------------
AClearanceMissile* AClearanceMissile::Fire(UObject* WorldContext, FName TargetCallsign)
{
	if (!WorldContext) { return nullptr; }
	UWorld* World = WorldContext->GetWorld();
	if (!World) { return nullptr; }

	AClearanceSimulationController* Ctrl = Cast<AClearanceSimulationController>(
		UGameplayStatics::GetActorOfClass(World, AClearanceSimulationController::StaticClass()));
	if (!Ctrl || !Ctrl->HasAuthority())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ClearanceMissile] Fire: no authority SimulationController in world; cannot spawn."));
		return nullptr;
	}

	AClearanceAirspaceManager* Airspace = Ctrl->GetAirspaceManager();
	if (!Airspace)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ClearanceMissile] Fire: SimulationController has no AirspaceManager. Missile aborted."));
		return nullptr;
	}

	const FAircraftState Target = Airspace->GetAircraftState(TargetCallsign);
	if (!Target.bIsValid)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ClearanceMissile] Fire: target %s not tracked."),
			*TargetCallsign.ToString());
		return nullptr;
	}

	const FVector TargetPosMeters = NmFtToMeters(Target.Position, Target.Altitude);

	// Prefer a placed launcher actor - drop one at Warton (or anywhere) in
	// the level and the missile launches from its world position, giving a
	// proper ballistic-looking arc rather than a spawn-behind-the-target
	// debug shortcut. Falls back to the 20 km-behind-target placement if
	// no launcher is present so the console command still works in an
	// empty test level. - TripleA
	FName    ChosenLauncherCallsign = GroundLauncherCallsign;
	FVector  LauncherPosMeters      = FVector::ZeroVector;
	FVector  LaunchLocation         = FVector::ZeroVector;
	FRotator LaunchRotation         = FRotator::ZeroRotator;

	AClearanceMissileLauncher* PlacedLauncher = Cast<AClearanceMissileLauncher>(
		UGameplayStatics::GetActorOfClass(World, AClearanceMissileLauncher::StaticClass()));

	if (PlacedLauncher)
	{
		LaunchLocation         = PlacedLauncher->GetMuzzleWorldLocation();
		LauncherPosMeters      = Ctrl->WorldToSimMeters(LaunchLocation);
		ChosenLauncherCallsign = PlacedLauncher->LauncherCallsign;

		// Aim the launch rotation at the target so the missile's first
		// tick already points broadly right; guidance refines from there.
		LaunchRotation = (TargetPosMeters - LauncherPosMeters).Rotation();
	}
	else
	{
		const FVector TargetVelMps  = KtsToMps(Target.Velocity);
		FVector       LauncherOff   = FVector::ZeroVector;
		if (!TargetVelMps.IsNearlyZero(0.1f))
		{
			const FVector VelDir = TargetVelMps.GetSafeNormal();
			LauncherOff    = -VelDir * 20000.0;
			LauncherOff.Z  = -TargetPosMeters.Z;
		}
		LauncherPosMeters = TargetPosMeters + LauncherOff;
		LaunchLocation    = LauncherPosMeters * 100.0;
		LaunchRotation    = (TargetPosMeters - LauncherPosMeters).Rotation();

		UE_LOG(LogTemp, Log,
			TEXT("[ClearanceMissile] Fire: no placed launcher actor - using fallback 20 km-behind-target placement."));
	}

	UE_LOG(LogTemp, Log,
		TEXT("[ClearanceMissile] Fire: launcher placed at (%.0f, %.0f, %.0f)m, target at (%.0f, %.0f, %.0f)m, initial range %.1f km"),
		LauncherPosMeters.X, LauncherPosMeters.Y, LauncherPosMeters.Z,
		TargetPosMeters.X,   TargetPosMeters.Y,   TargetPosMeters.Z,
		FVector::Dist(TargetPosMeters, LauncherPosMeters) / 1000.0);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = Ctrl;

	// Deferred spawn so we can stamp the launcher / target callsigns
	// before BeginPlay runs. Otherwise QueueFireEvent fires with
	// NAME_None on both sides because our post-Spawn assignment lands
	// AFTER BeginPlay has already queued the event. - TripleA
	AClearanceMissile* Missile = World->SpawnActorDeferred<AClearanceMissile>(
		AClearanceMissile::StaticClass(), FTransform(LaunchRotation, LaunchLocation),
		Ctrl, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Missile)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClearanceMissile] Fire: SpawnActorDeferred returned null."));
		return nullptr;
	}

	Missile->LauncherCallsign        = ChosenLauncherCallsign;
	Missile->TargetCallsign          = TargetCallsign;
	Missile->InitialLaunchPosMeters  = LauncherPosMeters;   // authoritative launch pos, survives SpawnActor clamping
	Missile->LauncherAnchorWorldLoc  = LaunchLocation;      // UE-world anchor for visual updates

	// Now that names + launch pos are stamped, complete the spawn.
	// BeginPlay fires inside FinishSpawning; QueueFireEvent sees the
	// right names and MissilePosMeters is seeded from InitialLaunchPosMeters.
	Missile->FinishSpawning(FTransform(LaunchRotation, LaunchLocation));

	UE_LOG(LogTemp, Log, TEXT("[ClearanceMissile] Fire: SAM launched at %s"),
		*TargetCallsign.ToString());

	return Missile;
}
