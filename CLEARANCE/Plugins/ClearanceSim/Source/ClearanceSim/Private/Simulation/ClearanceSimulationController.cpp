#include "Simulation/ClearanceSimulationController.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Airspace/ClearanceRunway.h"
#include "Aircraft/ClearanceAircraftSpawner.h"
#include "Aircraft/ClearanceAircraftBehaviour.h"
#include "Aircraft/ClearanceAircraftVisualInterface.h"
#include "Comms/ClearanceInstructionValidator.h"
#include "Comms/ClearanceCommsRouter.h"
#include "Safety/ClearanceConflictDetector.h"
#include "Scoring/ClearanceScoring.h"
#include "Simulation/ClearanceSessionRecorder.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Core/ClearanceConstants.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	FColor ColourFor(EAlertLevel Level)
	{
		switch (Level)
		{
		case EAlertLevel::Critical: return FColor::Red;
		case EAlertLevel::Warning:  return FColor::Orange;
		case EAlertLevel::Advisory: return FColor::Yellow;
		default:                    return FColor::Green;
		}
	}

	// Order-independent key for a callsign pair, so {A,B} and {B,A} hash the same.
	FString MakePairKey(FName A, FName B)
	{
		const FString SA = A.ToString();
		const FString SB = B.ToString();
		return (SA <= SB) ? (SA + TEXT("|") + SB) : (SB + TEXT("|") + SA);
	}
}

AClearanceSimulationController::AClearanceSimulationController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AClearanceSimulationController::BeginPlay()
{
	Super::BeginPlay();
	InitialiseSystems();
	if (bAutoStart)
	{
		StartSession();
	}
}

void AClearanceSimulationController::InitialiseSystems()
{
	if (bInitialised)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!AirspaceManager)
	{
		AirspaceManager = World->SpawnActor<AClearanceAirspaceManager>();
	}
	if (!Spawner)
	{
		Spawner = World->SpawnActor<AClearanceAircraftSpawner>();
	}

	Validator = NewObject<UClearanceInstructionValidator>(this);
	Scoring = NewObject<UClearanceScoring>(this);
	ConflictDetector = NewObject<UClearanceConflictDetector>(this);
	CommsRouter = NewObject<UClearanceCommsRouter>(this);
	Recorder = NewObject<UClearanceSessionRecorder>(this);

	if (AirspaceManager)
	{
		AirspaceManager->DefaultWindDirection = WindDirectionDeg;
		AirspaceManager->DefaultWindSpeed = WindSpeedKts;
		AirspaceManager->InitialiseEnvironment(WindDirectionDeg, WindSpeedKts);

		// Build the runway list from any placed Runway actors, converting their
		// world location into sim nautical miles relative to this controller.
		TArray<FRunwayInfo> RunwayInfos;
		const FVector Origin = GetActorLocation();
		GroundWorldZ = Origin.Z; // no runway placed -> ground sits at the controller
		bool bGroundSet = false;
		for (TActorIterator<AClearanceRunway> It(GetWorld()); It; ++It)
		{
			const float H = It->LandingHeadingDeg;
			const float HRad = FMath::DegreesToRadians(H);
			const FVector2D Inbound(FMath::Sin(HRad), FMath::Cos(HRad)); // direction flown to land on H

			// Centre, length and ground height all come from the runway MESH bounds, so
			// placing and scaling the mesh moves and sizes the runway - one object drives
			// the touchdown points, the markers and everything else. - TripleA
			FVector CentreW = It->GetActorLocation();
			float LengthW = 1600.f; // fallback (~1.6nm) until a mesh is assigned
			float TopZ = CentreW.Z;
			FVector MeshCentre, MeshExtent;
			if (It->GetRunwayBounds(MeshCentre, MeshExtent))
			{
				CentreW = MeshCentre;
				LengthW = 2.f * (MeshExtent.X * FMath::Abs(Inbound.X) + MeshExtent.Y * FMath::Abs(Inbound.Y));
				TopZ = MeshCentre.Z + MeshExtent.Z;
			}

			const FVector2D CentreNm((CentreW.X - Origin.X) / WorldUnitsPerNm, (CentreW.Y - Origin.Y) / WorldUnitsPerNm);
			const float HalfNm = (LengthW / WorldUnitsPerNm) * 0.5f;

			// Landing on H, you cross the near threshold (behind the centre) and roll
			// through; the reciprocal lands the other way from the far end. - TripleA
			FRunwayInfo A; A.ThresholdNm = CentreNm - Inbound * HalfNm; A.HeadingDeg = H;
			RunwayInfos.Add(A);
			if (It->bAllowReciprocal)
			{
				FRunwayInfo B; B.ThresholdNm = CentreNm + Inbound * HalfNm;
				B.HeadingDeg = FMath::Fmod(H + 180.f, 360.f);
				RunwayInfos.Add(B);
			}
			if (!bGroundSet) { GroundWorldZ = TopZ; bGroundSet = true; } // 0ft = top of the runway mesh
		}
		// No placed runways? fall back to any plain headings set on the Controller.
		if (RunwayInfos.Num() == 0)
		{
			for (float H : RunwayHeadings)
			{
				FRunwayInfo Info; Info.ThresholdNm = FVector2D::ZeroVector; Info.HeadingDeg = H;
				RunwayInfos.Add(Info);
			}
		}
		AirspaceManager->SetRunways(RunwayInfos);
	}

	if (Spawner) { Spawner->SetReferences(AirspaceManager); }
	if (ConflictDetector) { ConflictDetector->SetReferences(AirspaceManager); }
	if (CommsRouter) { CommsRouter->SetReferences(AirspaceManager, Validator); }

	BindDelegates();
	SpawnPresetCameras();
	bInitialised = true;
}

void AClearanceSimulationController::BindDelegates()
{
	if (AirspaceManager)
	{
		AirspaceManager->OnAircraftRegistered.AddDynamic(this, &AClearanceSimulationController::HandleAircraftRegistered);
		AirspaceManager->OnAircraftDeregistered.AddDynamic(this, &AClearanceSimulationController::HandleAircraftDeregistered);
	}
	if (ConflictDetector)
	{
		ConflictDetector->OnConflictDetected.AddDynamic(this, &AClearanceSimulationController::HandleConflictDetected);
		ConflictDetector->OnConflictResolved.AddDynamic(this, &AClearanceSimulationController::HandleConflictResolved);
		ConflictDetector->OnGoAroundRequired.AddDynamic(this, &AClearanceSimulationController::HandleGoAroundRequired);
		ConflictDetector->OnWakeTurbulenceAdvisory.AddDynamic(this, &AClearanceSimulationController::HandleWakeAdvisory);
		ConflictDetector->OnTCASResolutionAdvisory.AddDynamic(this, &AClearanceSimulationController::HandleTCASResolutionAdvisory);
	}
	if (Scoring)
	{
		Scoring->OnDifficultyAdjusted.AddDynamic(this, &AClearanceSimulationController::HandleDifficultyAdjusted);
	}
}

void AClearanceSimulationController::StartSession()
{
	if (bSessionActive)
	{
		return;
	}
	InitialiseSystems();

	if (Scoring) { Scoring->ResetSession(); }
	if (Spawner)
	{
		// Apply the Controller's spawn settings AFTER ResetSession (which broadcasts
		// the base difficulty interval) so these win for the start of the session.
		Spawner->MaxConcurrentAircraft = MaxConcurrentAircraft;
		Spawner->SetAutoSpawn(bAutoSpawn);
		Spawner->SetSpawnInterval(SpawnIntervalSeconds);
	}

	SessionTime = 0.f;
	bPaused = false;
	bSessionActive = true;
}

void AClearanceSimulationController::PauseSession()
{
	bPaused = true;
}

void AClearanceSimulationController::ResumeSession()
{
	bPaused = false;
}

void AClearanceSimulationController::EndSession()
{
	bSessionActive = false;

	if (AirspaceManager)
	{
		AirspaceManager->ClearAllAircraft(); // fires deregistration -> cleans the maps
	}
	BehaviourMap.Empty();

	for (TPair<FName, FSpawnedAircraftVisual>& Pair : VisualActors)
	{
		if (Pair.Value.Actor) { Pair.Value.Actor->Destroy(); }
	}
	VisualActors.Empty();
}

void AClearanceSimulationController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bSessionActive || bPaused)
	{
		return;
	}

	if (bReplayMode)
	{
		// Replay: don't run the sim - pose the world to the recorded snapshot at the
		// current replay time, then let UpdateVisuals draw it. - TripleA
		if (!bReplayPaused && Recorder && AirspaceManager)
		{
			ReplayTime = FMath::Clamp(ReplayTime + DeltaTime * ReplaySpeed, 0.f, Recorder->GetDurationSeconds());
			if (const FRecordedSnapshot* Snap = Recorder->FindSnapshotAt(ReplayTime))
			{
				Recorder->ApplySnapshotTo(AirspaceManager, *Snap);
			}
		}
		UpdateVisuals();
		UpdateFollowCamera();
		DrawDebugView();
		return;
	}

	const float SimDelta = DeltaTime * FMath::Max(0.f, SimulationTimeScale);
	SessionTime += SimDelta;
	StepSimulation(SimDelta);

	// Capture the post-tick state into the recording timeline.
	if (Recorder && Recorder->IsRecording() && AirspaceManager)
	{
		Recorder->CaptureSnapshot(SessionTime, AirspaceManager->GetAllAircraftStates());
	}

	UpdateFollowCamera();
}

void AClearanceSimulationController::StepSimulation(float DeltaTime)
{
	// The authoritative tick order from the architecture doc.
	if (Spawner) { Spawner->TickSpawning(DeltaTime); }            // 1. entry

	for (const TPair<FName, TObjectPtr<UClearanceAircraftBehaviour>>& Pair : BehaviourMap)
	{
		if (Pair.Value) { Pair.Value->UpdateMovement(DeltaTime); } // 2-4. move + commit
	}

	if (ConflictDetector) { ConflictDetector->DetectConflicts(); } // 5. monitor (6-8 fire via delegates)

	CheckExits();                                                  // landings / departures / strays

	UpdateVisuals();
	DrawDebugView();
}

FVector AClearanceSimulationController::WorldPositionFor(const FAircraftState& State) const
{
	const FVector Origin = GetActorLocation();
	return FVector(Origin.X + State.Position.X * WorldUnitsPerNm,
		Origin.Y + State.Position.Y * WorldUnitsPerNm,
		GroundWorldZ + AltitudeToWorldZOffset(State.Altitude));
}

float AClearanceSimulationController::AltitudeToWorldZOffset(float AltitudeFt) const
{
	const float Alt = FMath::Max(0.f, AltitudeFt);
	// Linear if the curve is disabled; otherwise a power curve that equals the linear
	// scale exactly at the reference altitude, sits BELOW it lower down (gentle approach)
	// and ABOVE it up high (towering cruise). - TripleA
	if (AltitudeCurveExponent <= 1.f || AltitudeCurveRefFt <= 0.f)
	{
		return Alt * AltitudeWorldScale;
	}
	return AltitudeWorldScale * AltitudeCurveRefFt * FMath::Pow(Alt / AltitudeCurveRefFt, AltitudeCurveExponent);
}

const TArray<FAircraftVisualVariant>& AClearanceSimulationController::VariantsFor(EWakeCategory Category) const
{
	switch (Category)
	{
	case EWakeCategory::Light: return LightVariants;
	case EWakeCategory::Heavy: return HeavyVariants;
	case EWakeCategory::Super: return SuperVariants;
	default:                   return MediumVariants;
	}
}

void AClearanceSimulationController::UpdateVisuals()
{
	if (!AirspaceManager)
	{
		return;
	}

	const FSectorEnvironment Env = AirspaceManager->GetCurrentEnvironment();
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	// More wind = more buffet, but never dead-calm-still: a floor keeps a bit of life
	// in the airframe even in light air. - TripleA
	const float Buffet = FMath::Clamp(Env.WindSpeed / 25.f, 0.35f, 1.6f);

	for (const FAircraftState& State : AirspaceManager->GetAllAircraftStates())
	{
		const FSpawnedAircraftVisual* Found = VisualActors.Find(State.Callsign);
		if (!Found || !Found->Actor)
		{
			continue;
		}

		Found->Actor->SetActorLocation(WorldPositionFor(State));

		// Point the nose along the full 3D track (heading + climb), so the aircraft
		// visibly pitches when climbing/descending, then bank about the nose. The
		// mesh-fix yaw keeps the roll a real bank, not a pitch, for offset meshes. - TripleA
		const float HeadingRad = FMath::DegreesToRadians(State.Heading);

		// Pitch attitude for the look. Following the raw flight path makes it dive at
		// the runway on a steep descent, so instead: clamp to a sane body-attitude range,
		// and near the ground flare the nose UP as it settles, then level on the deck -
		// reads like a real landing rather than a nose-first plant. - TripleA
		const bool bApproaching = (State.FlightPhase == EFlightPhase::Approach || State.FlightPhase == EFlightPhase::Landing);

		// VISUAL flight-path angle - how steep the aircraft is actually moving ON SCREEN
		// (through the altitude curve), so the nose points down its real descent path and
		// reads as flying DOWN, not falling flat. - TripleA
		const float HorizSpeedUU = State.Speed * (1.f / 3600.f) * WorldUnitsPerNm;            // uu/sec along the ground
		const float dZdAlt = (AltitudeToWorldZOffset(State.Altitude + 1.f) - AltitudeToWorldZOffset(FMath::Max(0.f, State.Altitude - 1.f))) * 0.5f;
		const float VertSpeedUU = dZdAlt * (State.ClimbRate / 60.f);                          // uu/sec vertical (ClimbRate is ft/min)
		const float VisualFpaDeg = (HorizSpeedUU > 1.f) ? FMath::RadiansToDegrees(FMath::Atan2(VertSpeedUU, HorizSpeedUU)) : 0.f;

		float PitchDeg;
		if (State.Altitude <= 12.f)
		{
			PitchDeg = 0.f; // on the deck - nose-wheel down, level for the rollout
		}
		else if (bApproaching && State.Altitude < 60.f && State.ClimbRate < 0.f)
		{
			PitchDeg = 4.f; // FLARE - nose eases up just before touchdown
		}
		else if (bApproaching && State.ClimbRate < -50.f)
		{
			// On the glidepath the real angle is shallow (~3deg) so it'd read as level;
			// a moderate nose-DOWN descent attitude for the landing, then the flare above
			// eases it up before touchdown - a smaller swing reads smoother. - TripleA
			PitchDeg = -6.f;
		}
		else
		{
			PitchDeg = FMath::Clamp(VisualFpaDeg, -22.f, 12.f); // follow the on-screen path; cap the dive
		}

		const float VertOverHoriz = FMath::Tan(FMath::DegreesToRadians(PitchDeg));
		const FVector Forward = FVector(FMath::Sin(HeadingRad), FMath::Cos(HeadingRad), VertOverHoriz).GetSafeNormal();

		const FQuat LookAlong = FRotationMatrix::MakeFromXZ(Forward, FVector::UpVector).ToQuat();
		const FQuat Bank = FQuat(FVector::ForwardVector, FMath::DegreesToRadians(State.BankAngle));
		const FQuat MeshFix = FRotator(0.f, Found->YawOffsetDeg, 0.f).Quaternion();

		// Gentle airborne buffet so a straight-and-level aircraft still breathes -
		// two roll frequencies for a non-repeating wing rock, plus a slower pitch bob
		// and yaw wander. A per-aircraft phase from the callsign stops the whole sector
		// wobbling in lockstep. Tiny degrees - it's life, not turbulence. - TripleA
		const float Ph = (GetTypeHash(State.Callsign) % 628) * 0.01f; // 0..2pi-ish
		// Fade the buffet out near the ground so it doesn't jitter on the flare/roll-out -
		// the airframe steadies as it lands. Full effect above 400ft, none on the deck. - TripleA
		const float GroundFade = FMath::Clamp(State.Altitude / 400.f, 0.f, 1.f) * Buffet;
		float RollWob  = (FMath::Sin(Now * 0.9f + Ph) * 1.6f + FMath::Sin(Now * 2.3f + Ph * 1.7f) * 0.5f) * GroundFade;
		float PitchWob = FMath::Sin(Now * 1.3f + Ph * 0.6f) * 0.8f * GroundFade;
		const float YawWob = FMath::Sin(Now * 0.7f + Ph * 1.3f) * 0.6f * GroundFade;

		// Wake turbulence: a sharp wing-rock + bump on top of the gentle buffet, scaled
		// by intensity - a Light behind a Super gets thrown around, a Heavy behind a
		// Heavy barely twitches. - TripleA
		const float WakeI = ConflictDetector ? ConflictDetector->GetWakeIntensity(State.Callsign) : 0.f;
		if (WakeI > 0.f)
		{
			RollWob  += (FMath::Sin(Now * 3.2f + Ph) * 18.f + FMath::Sin(Now * 5.7f + Ph * 2.f) * 8.f) * WakeI;
			PitchWob += FMath::Sin(Now * 4.1f + Ph * 0.5f) * 4.5f * WakeI;
		}

		const FQuat BuffetRot = FRotator(PitchWob, YawWob, RollWob).Quaternion();

		const FQuat TargetRot = LookAlong * Bank * BuffetRot * MeshFix;

		// The sim flips bank/pitch instantly when a turn or climb starts; easing the
		// visual toward it on REAL frame time (not the sped-up sim clock) rolls and
		// pitches the airframe in and out instead of snapping - kills the stiffness
		// without touching the flight model. Higher = quicker, stiffer. - TripleA
		const float RealDt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
		const FQuat Smoothed = FMath::QInterpTo(Found->Actor->GetActorQuat(), TargetRot, RealDt, 4.f);
		Found->Actor->SetActorRotation(Smoothed);

		// Hand the airframe's gear/engine state to the Blueprint (if it implements the
		// visual interface) so it can retract the gear in the cruise, drop it for the
		// approach, and spin the prop/turbine with throttle. - TripleA
		if (Found->Actor->GetClass()->ImplementsInterface(UClearanceAircraftVisualInterface::StaticClass()))
		{
			const bool bOnGround = State.Altitude <= 50.f;
			const bool bGearDeployed = bOnGround
				|| State.FlightPhase == EFlightPhase::Landing
				|| (State.FlightPhase == EFlightPhase::Approach && State.Altitude <= GearDownAltitudeFt)
				|| (State.FlightPhase == EFlightPhase::Departing && State.Altitude <= 1000.f);

			// Engine sits around cruise power, pushes up in a climb and eases back on
			// descent, idles on the deck - never off while airborne.
			const float Throttle = bOnGround ? 0.15f : FMath::Clamp(0.6f + State.ClimbRate / 4000.f, 0.25f, 1.f);

			IClearanceAircraftVisualInterface::Execute_UpdateAircraftVisual(Found->Actor, bGearDeployed, Throttle, bOnGround);
		}
	}
}

void AClearanceSimulationController::DrawDebugView()
{
	UWorld* World = GetWorld();
	if (!bDrawDebug || !AirspaceManager || !World)
	{
		return;
	}

	const FVector Origin = GetActorLocation();
	const float S = WorldUnitsPerNm;

	// sector boundary ring (flat on the XY plane)
	DrawDebugCircle(World, Origin, ExitRadiusNm * S, 64, FColor(40, 80, 120), false, -1.f, 0, 120.f, FVector(1, 0, 0), FVector(0, 1, 0), false);

	// Compass rose: a tick + heading number every 30deg around the boundary, cardinals
	// called out, so headings are readable in the world. "Vector 090" = send it toward
	// the 090 mark. Heading 0=North, 90=East (X=East, Y=North). - TripleA
	for (int32 Deg = 0; Deg < 360; Deg += 30)
	{
		const float R = FMath::DegreesToRadians((float)Deg);
		const FVector Dir(FMath::Sin(R), FMath::Cos(R), 0.f);
		const FVector Edge = Origin + Dir * (ExitRadiusNm * S);
		DrawDebugLine(World, Origin + Dir * (ExitRadiusNm * S - 1.5f * S), Edge, FColor(60, 110, 160), false, -1.f, 0, 90.f);

		FString Label;
		switch (Deg)
		{
		case 0:   Label = TEXT("N 360"); break;
		case 90:  Label = TEXT("E 090"); break;
		case 180: Label = TEXT("S 180"); break;
		case 270: Label = TEXT("W 270"); break;
		default:  Label = FString::Printf(TEXT("%03d"), Deg); break;
		}
		DrawDebugString(World, Edge + FVector(0, 0, 2.f * S), Label, nullptr, FColor::Cyan, 0.f, true, 1.3f);
	}

	const TArray<FAircraftState> States = AirspaceManager->GetAllAircraftStates();
	const FSectorEnvironment Env = AirspaceManager->GetCurrentEnvironment();

	// Active runway threshold + the extended approach centreline (the localiser you
	// vector traffic onto) + the +/-3nm capture corridor, so the approach is flyable.
	{
		const float Fac = (Env.ActiveRunwayHeading >= 0.f) ? Env.ActiveRunwayHeading : 270.f;
		const float FacRad = FMath::DegreesToRadians(Fac);
		const FVector InboundDir(FMath::Sin(FacRad), FMath::Cos(FacRad), 0.f); // landing direction
		const FVector RightDir(FMath::Cos(FacRad), -FMath::Sin(FacRad), 0.f);  // perpendicular
		const float CorridorLen = ClearanceConstants::ApproachCorridorLengthNm;
		const FColor Grey(90, 90, 90);
		const FVector Thr(Origin.X + Env.ActiveRunwayThreshold.X * S, Origin.Y + Env.ActiveRunwayThreshold.Y * S, GroundWorldZ);

		// Capture funnel/cone: a narrow slot at the orb that opens out to a wide, tall
		// mouth 40nm away, with the white glidepath down the middle rising on the 3deg
		// slope. Fly the aircraft into the mouth, then ride the white line down onto the
		// orb. Matches the cone-shaped capture test in the Behaviour. - TripleA
		const float WMax = ClearanceConstants::ApproachCorridorHalfWidthNm * S; // far half-width
		const float WMin = 0.3f * S;                                            // half-width at the orb
		const float MouthZ = AltitudeToWorldZOffset(CorridorLen * 318.f);       // curved glide height at the mouth
		const FVector Up(0.f, 0.f, MouthZ);
		const FVector FarC = Thr - InboundDir * (CorridorLen * S);              // far point, on the ground

		const FVector FBL = FarC + RightDir * WMax;          // far mouth, ground, left/right
		const FVector FBR = FarC - RightDir * WMax;
		const FVector FTL = FBL + Up;                        // far mouth, top
		const FVector FTR = FBR + Up;

		DrawDebugSphere(World, Thr, 700.f, 12, FColor::White, false, -1.f, 0, 60.f);

		// Glidepath centreline as a CURVE - gentle near the orb, steepening out to the
		// mouth - following the altitude curve so it sits on the path planes actually fly.
		FVector Prev = Thr;
		const int32 Segs = 20;
		for (int32 i = 1; i <= Segs; ++i)
		{
			const float DistNm = (CorridorLen * i) / Segs;
			const FVector Pt = Thr - InboundDir * (DistNm * S) + FVector(0.f, 0.f, AltitudeToWorldZOffset(DistNm * 318.f));
			DrawDebugLine(World, Prev, Pt, FColor::White, false, -1.f, 0, 120.f);
			Prev = Pt;
		}

		DrawDebugLine(World, Thr + RightDir * WMin, FBL, Grey, false, -1.f, 0, 40.f);          // ground edges (widen out)
		DrawDebugLine(World, Thr - RightDir * WMin, FBR, Grey, false, -1.f, 0, 40.f);
		DrawDebugLine(World, Thr, FTL, Grey, false, -1.f, 0, 40.f);                            // top edges (rise + widen)
		DrawDebugLine(World, Thr, FTR, Grey, false, -1.f, 0, 40.f);
		DrawDebugLine(World, FBL, FBR, Grey, false, -1.f, 0, 40.f);                            // far mouth rectangle
		DrawDebugLine(World, FTL, FTR, Grey, false, -1.f, 0, 40.f);
		DrawDebugLine(World, FBL, FTL, Grey, false, -1.f, 0, 40.f);
		DrawDebugLine(World, FBR, FTR, Grey, false, -1.f, 0, 40.f);
	}

	// The physical runway strip(s), outlined straight from each runway MESH's bounds -
	// so the yellow box hugs the mesh and both thresholds sit on its ends. Move or
	// scale the mesh and this follows it exactly. - TripleA
	for (TActorIterator<AClearanceRunway> It(World); It; ++It)
	{
		const float HRad = FMath::DegreesToRadians(It->LandingHeadingDeg);
		const FVector Dir(FMath::Sin(HRad), FMath::Cos(HRad), 0.f);
		const FVector Side(FMath::Cos(HRad), -FMath::Sin(HRad), 0.f);

		FVector Cw = It->GetActorLocation();
		float HalfLen = 800.f, HalfWidth = 0.025f * S, Zc = Cw.Z;
		FVector MeshCentre, MeshExtent;
		if (It->GetRunwayBounds(MeshCentre, MeshExtent))
		{
			Cw = MeshCentre;
			HalfLen = MeshExtent.X * FMath::Abs(Dir.X) + MeshExtent.Y * FMath::Abs(Dir.Y);
			HalfWidth = MeshExtent.X * FMath::Abs(Side.X) + MeshExtent.Y * FMath::Abs(Side.Y);
			Zc = MeshCentre.Z + MeshExtent.Z;
		}
		const FVector C(Cw.X, Cw.Y, Zc);
		const FVector E1 = C - Dir * HalfLen;
		const FVector E2 = C + Dir * HalfLen;

		DrawDebugLine(World, E1, E2, FColor::Yellow, false, -1.f, 0, 250.f);                 // centreline of the strip
		DrawDebugLine(World, E1 + Side * HalfWidth, E2 + Side * HalfWidth, FColor::Yellow, false, -1.f, 0, 120.f); // edges
		DrawDebugLine(World, E1 - Side * HalfWidth, E2 - Side * HalfWidth, FColor::Yellow, false, -1.f, 0, 120.f);
		DrawDebugLine(World, E1 + Side * HalfWidth, E1 - Side * HalfWidth, FColor::Yellow, false, -1.f, 0, 120.f); // end caps
		DrawDebugLine(World, E2 + Side * HalfWidth, E2 - Side * HalfWidth, FColor::Yellow, false, -1.f, 0, 120.f);
		DrawDebugSphere(World, E1, 400.f, 8, FColor::Yellow, false, -1.f, 0, 40.f);
		DrawDebugSphere(World, E2, 400.f, 8, FColor::Yellow, false, -1.f, 0, 40.f);

		// Label each end with the heading you'd land on it, so you know which way to
		// vector and whether LandingHeadingDeg matches how the mesh actually points.
		// Real runway designator: heading rounded to the nearest 10, divided by 10, two
		// digits (180deg -> "18", 90 -> "09", 360/0 -> "36"). - TripleA
		auto Designator = [](float Hdg)
		{
			int32 N = FMath::RoundToInt(Hdg / 10.f) % 36;
			return (N == 0) ? 36 : N;
		};
		const int32 D1 = Designator(It->LandingHeadingDeg);
		const int32 D2 = Designator(FMath::Fmod(It->LandingHeadingDeg + 180.f, 360.f));
		DrawDebugString(World, E1 + FVector(0, 0, 1.5f * S), FString::Printf(TEXT("RWY %02d"), D1), nullptr, FColor::Yellow, 0.f, true, 1.2f);
		if (It->bAllowReciprocal)
		{
			DrawDebugString(World, E2 + FVector(0, 0, 1.5f * S), FString::Printf(TEXT("RWY %02d"), D2), nullptr, FColor::Yellow, 0.f, true, 1.2f);
		}
	}
	FString AARTag;
	if (bReplayMode && Recorder)
	{
		AARTag = FString::Printf(TEXT("  |  [REPLAY %s %.1f/%.1fs x%.1f]"),
			bReplayPaused ? TEXT("PAUSED") : TEXT("PLAY"), ReplayTime, Recorder->GetDurationSeconds(), ReplaySpeed);
	}
	else if (Recorder && Recorder->IsRecording())
	{
		AARTag = FString::Printf(TEXT("  |  [REC %d snapshots]"), Recorder->GetSnapshotCount());
	}

	FString Readout = FString::Printf(TEXT("CLEARANCE  |  t=%.0fs  |  score=%d  |  eff=%.0f%%  |  traffic=%d  |  wind %03.0f/%.0fkt  |  active rwy %03.0f%s\n"),
		SessionTime,
		Scoring ? Scoring->GetCurrentScore() : 0,
		Scoring ? Scoring->GetEfficiency() * 100.f : 100.f,
		States.Num(),
		Env.WindDirection, Env.WindSpeed, Env.ActiveRunwayHeading,
		*AARTag);

	// Scoring breakdown, tallied from the session log so we can see what's adding up.
	if (Scoring)
	{
		int32 nLand = 0, nDep = 0, nRes = 0, nGA = 0, nSep = 0, nExit = 0, nWake = 0, nTCAS = 0;
		for (const FIncidentRecord& R : Scoring->GetSessionLog())
		{
			switch (R.Type)
			{
			case EIncidentType::SuccessfulLanding:    ++nLand; break;
			case EIncidentType::SuccessfulDeparture:  ++nDep;  break;
			case EIncidentType::SuccessfulResolution: ++nRes;  break;
			case EIncidentType::GoAroundTriggered:    ++nGA;   break;
			case EIncidentType::SeparationLoss:       ++nSep;  break;
			case EIncidentType::UnresolvedExit:       ++nExit; break;
			case EIncidentType::WakeEncounter:        ++nWake; break;
			case EIncidentType::TCASResolutionAdvisory: ++nTCAS; break;
			default: break;
			}
		}
		Readout += FString::Printf(TEXT("SCORING  +land %d  +dep %d  +resolved %d   |   -go-around %d  -sep-loss %d  -wake %d  -tcas %d  -strayed %d   |   next spawn %.0fs\n"),
			nLand, nDep, nRes, nGA, nSep, nWake, nTCAS, nExit, Scoring->GetCurrentSpawnInterval());
	}

	for (const FAircraftState& A : States)
	{
		const EAlertLevel Alert = ConflictDetector ? ConflictDetector->GetAlertLevelFor(A.Callsign) : EAlertLevel::None;
		const FColor C = ColourFor(Alert);

		// Spheres are the fallback for any aircraft with no spawned mesh; modelled
		// aircraft skip them so the model isn't buried in a debug blob.
		if (!VisualActors.Contains(A.Callsign))
		{
			const FVector P(Origin.X + A.Position.X * S, Origin.Y + A.Position.Y * S, GroundWorldZ + AltitudeToWorldZOffset(A.Altitude));
			DrawDebugSphere(World, P, 500.f, 10, C, false, -1.f, 0, 40.f);

			const float HeadingRad = FMath::DegreesToRadians(A.Heading);
			const FVector Dir(FMath::Sin(HeadingRad), FMath::Cos(HeadingRad), 0.f);
			DrawDebugLine(World, P, P + Dir * 2200.f, C, false, -1.f, 0, 60.f);
		}

		// Float the callsign + current>target heading over each aircraft, so you can
		// pick one out and see where it's pointing vs where you've sent it. - TripleA
		DrawDebugString(World, WorldPositionFor(A) + FVector(0, 0, 1.2f * S),
			FString::Printf(TEXT("%s  hdg %03.0f>%03.0f"), *A.Callsign.ToString(), A.Heading, A.TargetHeading),
			nullptr, C, 0.f, true, 1.1f);

		// Shows current>target for each axis + vertical speed, so we can see if it's
		// actually descending (and whether the nose-down branch should fire).
		Readout += FString::Printf(TEXT("%s  hdg %3.0f>%3.0f  alt %5.0f>%5.0f  spd %3.0f>%3.0f  vs%+5.0f%s\n"),
			*A.Callsign.ToString(),
			A.Heading, A.TargetHeading,
			A.Altitude, A.TargetAltitude,
			A.Speed, A.TargetSpeed,
			A.ClimbRate,
			Alert != EAlertLevel::None ? TEXT(" <CONF>") : TEXT(""));
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(7, 0.f, FColor::White, Readout);
	}
}

void AClearanceSimulationController::CheckExits()
{
	if (!AirspaceManager)
	{
		return;
	}

	// GetAllAircraftStates returns a copy, so deregistering inside the loop is safe.
	for (const FAircraftState& State : AirspaceManager->GetAllAircraftStates())
	{
		const float Dist = FVector2D(State.Position.X, State.Position.Y).Size();

		// Pull it off only once it's on the deck AND has braked to a FULL STOP, so the
		// whole roll-out plays out - touch down, brake, slow, stop - before it's
		// removed, instead of vanishing while still rolling. - TripleA
		if (State.FlightPhase == EFlightPhase::Landing && State.Altitude <= 100.f && State.Speed <= 1.f)
		{
			if (Scoring) { Scoring->LogIncident(EIncidentType::SuccessfulLanding, State.Callsign, NAME_None, TEXT("Landed")); }
			AirspaceManager->DeregisterAircraft(State.Callsign);
		}
		else if (Dist > ExitRadiusNm)
		{
			// Cleared to leave = a clean departure; drifting out otherwise is a miss.
			const EIncidentType Outcome = (State.FlightPhase == EFlightPhase::Exiting)
				? EIncidentType::SuccessfulDeparture
				: EIncidentType::UnresolvedExit;
			if (Scoring) { Scoring->LogIncident(Outcome, State.Callsign, NAME_None, TEXT("Left sector")); }
			AirspaceManager->DeregisterAircraft(State.Callsign);
		}
	}
}

EInstructionResult AClearanceSimulationController::PlayerIssueInstruction(const FAircraftInstruction& Instruction)
{
	if (Scoring) { Scoring->RecordInstruction(); }
	if (Recorder)
	{
		Recorder->LogEvent(SessionTime, FString::Printf(TEXT("INSTR %s %s %.0f"),
			*UEnum::GetValueAsString(Instruction.Type), *Instruction.TargetCallsign.ToString(), Instruction.TargetValue));
	}
	if (CommsRouter)
	{
		return CommsRouter->IssueInstruction(Instruction);
	}
	return EInstructionResult::Rejected_InvalidCallsign;
}

void AClearanceSimulationController::StartRecording()
{
	if (!Recorder) { return; }
	Recorder->ClearRecording();
	Recorder->StartRecording();
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, TEXT("AAR: recording started")); }
}

void AClearanceSimulationController::StopRecording()
{
	if (!Recorder) { return; }
	Recorder->StopRecording();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan,
			FString::Printf(TEXT("AAR: stopped - %d snapshots, %.1fs"), Recorder->GetSnapshotCount(), Recorder->GetDurationSeconds()));
	}
}

void AClearanceSimulationController::EnterReplay()
{
	if (!Recorder || Recorder->GetSnapshotCount() == 0)
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, TEXT("AAR: no recording to replay")); }
		return;
	}
	// Freeze the live world so ResumeLive can put it back as it was. - TripleA
	if (AirspaceManager)
	{
		PreReplayState.TimeStamp = SessionTime;
		PreReplayState.States = AirspaceManager->GetAllAircraftStates();
		PreReplaySessionTime = SessionTime;
		bHasPreReplayState = true;
	}

	Recorder->StopRecording();
	bReplayMode = true;
	bReplayPaused = false;
	ReplayTime = 0.f;
	// Default replay speed = sim time scale, so the playback runs at the same pace
	// the user actually saw live (otherwise a 10x sim plays back 10x slower). - TripleA
	ReplaySpeed = FMath::Max(0.1f, SimulationTimeScale);
	// Pose to t=0 immediately so the user sees the start of the recording, not a stale frame.
	if (AirspaceManager)
	{
		if (const FRecordedSnapshot* Snap = Recorder->FindSnapshotAt(0.f))
		{
			Recorder->ApplySnapshotTo(AirspaceManager, *Snap);
		}
	}
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, TEXT("AAR: REPLAY  (clearance.replay.pause/seek/speed/live to control)")); }
}

void AClearanceSimulationController::ResumeLive()
{
	// Put the world back to where the live sim was when we entered replay.
	if (bHasPreReplayState && AirspaceManager && Recorder)
	{
		Recorder->ApplySnapshotTo(AirspaceManager, PreReplayState);
		SessionTime = PreReplaySessionTime;
		bHasPreReplayState = false;
	}
	bReplayMode = false;
	bReplayPaused = false;
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, TEXT("AAR: live")); }
}

void AClearanceSimulationController::SeekReplay(float TimeSeconds)
{
	if (!bReplayMode || !Recorder) { return; }
	ReplayTime = FMath::Clamp(TimeSeconds, 0.f, Recorder->GetDurationSeconds());
	if (AirspaceManager)
	{
		if (const FRecordedSnapshot* Snap = Recorder->FindSnapshotAt(ReplayTime))
		{
			Recorder->ApplySnapshotTo(AirspaceManager, *Snap);
		}
	}
}

void AClearanceSimulationController::SetReplayPaused(bool bInPaused)
{
	bReplayPaused = bInPaused;
}

void AClearanceSimulationController::SetReplaySpeed(float Multiplier)
{
	ReplaySpeed = FMath::Max(0.05f, Multiplier);
}

void AClearanceSimulationController::SpawnPresetCameras()
{
	UWorld* World = GetWorld();
	if (!World || !AirspaceManager) { return; }

	const FVector Origin = GetActorLocation();
	const float S = WorldUnitsPerNm;
	const FSectorEnvironment Env = AirspaceManager->GetCurrentEnvironment();

	// Active runway threshold in world space (matches the white orb in the overlay).
	const FVector ThrW(Origin.X + Env.ActiveRunwayThreshold.X * S,
		Origin.Y + Env.ActiveRunwayThreshold.Y * S,
		GroundWorldZ);
	const float FAC = (Env.ActiveRunwayHeading >= 0.f) ? Env.ActiveRunwayHeading : 270.f;
	const float FacRad = FMath::DegreesToRadians(FAC);
	const FVector InboundDir(FMath::Sin(FacRad), FMath::Cos(FacRad), 0.f); // landing direction

	// OVERVIEW: well above the sector, looking down and slightly toward the runway. - TripleA
	{
		const FVector Pos = Origin + FVector(0.f, -ExitRadiusNm * S * 0.55f, ExitRadiusNm * S * 0.95f);
		const FRotator Rot = (ThrW - Pos).Rotation();
		CameraOverview = World->SpawnActor<ACameraActor>(Pos, Rot);
	}

	// TOWER: at the runway threshold a bit elevated, looking down the approach (out
	// the way aircraft come in from).
	{
		const FVector Pos = ThrW + FVector(0.f, 0.f, 600.f);
		const FVector LookAt = ThrW - InboundDir * (20.f * S);
		const FRotator Rot = (LookAt - Pos).Rotation();
		CameraTower = World->SpawnActor<ACameraActor>(Pos, Rot);
	}

	// APPROACH: out at the far mouth of the corridor at glide altitude, looking back
	// at the runway - this is where you "see" arriving aircraft from.
	{
		const float CorridorLen = ClearanceConstants::ApproachCorridorLengthNm;
		const FVector FarPos = ThrW - InboundDir * (CorridorLen * 0.6f * S)
			+ FVector(0.f, 0.f, AltitudeToWorldZOffset(CorridorLen * 0.6f * 318.f));
		const FRotator Rot = (ThrW - FarPos).Rotation();
		CameraApproach = World->SpawnActor<ACameraActor>(FarPos, Rot);
	}

	// FOLLOW: position updated per-tick in UpdateFollowCamera.
	CameraFollow = World->SpawnActor<ACameraActor>(Origin, FRotator::ZeroRotator);
}

void AClearanceSimulationController::SetCameraView(EClearanceCameraView View, FName FollowCallsign)
{
	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!PC) { return; }

	AActor* Target = nullptr;
	switch (View)
	{
	case EClearanceCameraView::Overview: Target = CameraOverview; break;
	case EClearanceCameraView::Tower:    Target = CameraTower;    break;
	case EClearanceCameraView::Approach: Target = CameraApproach; break;
	case EClearanceCameraView::Follow:
		Target = CameraFollow;
		FollowTargetCallsign = FollowCallsign;
		UpdateFollowCamera(); // snap into position before the blend starts
		break;
	case EClearanceCameraView::Default:
	default:
		Target = PC->GetPawn();
		break;
	}

	if (Target)
	{
		PC->SetViewTargetWithBlend(Target, 0.6f);
		CurrentCameraView = View;
	}
}

void AClearanceSimulationController::CycleCameraView()
{
	// Default -> Overview -> Tower -> Approach -> Follow -> Default ...
	EClearanceCameraView Next = EClearanceCameraView::Overview;
	switch (CurrentCameraView)
	{
	case EClearanceCameraView::Default:  Next = EClearanceCameraView::Overview; break;
	case EClearanceCameraView::Overview: Next = EClearanceCameraView::Tower;    break;
	case EClearanceCameraView::Tower:    Next = EClearanceCameraView::Approach; break;
	case EClearanceCameraView::Approach: Next = EClearanceCameraView::Follow;   break;
	case EClearanceCameraView::Follow:   Next = EClearanceCameraView::Default;  break;
	}

	// For Follow, pick the first aircraft in the sector if none chosen.
	FName TargetCallsign = FollowTargetCallsign;
	if (Next == EClearanceCameraView::Follow && TargetCallsign.IsNone() && AirspaceManager)
	{
		const TArray<FAircraftState> All = AirspaceManager->GetAllAircraftStates();
		if (All.Num() > 0) { TargetCallsign = All[0].Callsign; }
	}
	SetCameraView(Next, TargetCallsign);
}

void AClearanceSimulationController::UpdateFollowCamera()
{
	if (!CameraFollow || FollowTargetCallsign.IsNone() || !AirspaceManager) { return; }
	const FAircraftState S = AirspaceManager->GetAircraftState(FollowTargetCallsign);
	if (!S.bIsValid) { return; }

	const FVector Aircraft = WorldPositionFor(S);
	const float HeadingRad = FMath::DegreesToRadians(S.Heading);
	const FVector Forward(FMath::Sin(HeadingRad), FMath::Cos(HeadingRad), 0.f);
	const FVector Right(FMath::Cos(HeadingRad), -FMath::Sin(HeadingRad), 0.f); // 90 right of forward
	const FVector Up(0.f, 0.f, 1.f);

	FVector CamPos = Aircraft;
	FRotator CamRot = FRotator::ZeroRotator;
	switch (FollowAngle)
	{
	case EClearanceFollowAngle::Cockpit:
		// Just ahead/above the aircraft origin, looking forward along heading.
		CamPos = Aircraft + Forward * 60.f + Up * 80.f;
		CamRot = Forward.Rotation();
		break;
	case EClearanceFollowAngle::Side:
		// Off the right wing, slightly above, looking back at the aircraft.
		CamPos = Aircraft + Right * 1800.f + Up * 500.f;
		CamRot = (Aircraft - CamPos).Rotation();
		break;
	case EClearanceFollowAngle::Top:
		// Directly above, looking straight down.
		CamPos = Aircraft + Up * 3500.f;
		CamRot = (Aircraft - CamPos).Rotation();
		break;
	case EClearanceFollowAngle::Chase:
	default:
		// Behind and above, angled down onto the aircraft.
		CamPos = Aircraft - Forward * 2500.f + Up * 1200.f;
		CamRot = (Aircraft - CamPos).Rotation();
		break;
	}
	CameraFollow->SetActorLocationAndRotation(CamPos, CamRot);
}

void AClearanceSimulationController::SetFollowAngle(EClearanceFollowAngle Angle)
{
	FollowAngle = Angle;
	UpdateFollowCamera();
}

void AClearanceSimulationController::CycleFollowAngle()
{
	switch (FollowAngle)
	{
	case EClearanceFollowAngle::Chase:   FollowAngle = EClearanceFollowAngle::Cockpit; break;
	case EClearanceFollowAngle::Cockpit: FollowAngle = EClearanceFollowAngle::Side;    break;
	case EClearanceFollowAngle::Side:    FollowAngle = EClearanceFollowAngle::Top;     break;
	case EClearanceFollowAngle::Top:     FollowAngle = EClearanceFollowAngle::Chase;   break;
	}
	UpdateFollowCamera();
}

void AClearanceSimulationController::SetWind(float DirectionDeg, float SpeedKts)
{
	WindDirectionDeg = DirectionDeg;
	WindSpeedKts = SpeedKts;
	if (AirspaceManager)
	{
		AirspaceManager->UpdateWindConditions(DirectionDeg, SpeedKts);
	}
}

void AClearanceSimulationController::SetAutoSpawn(bool bEnabled)
{
	bAutoSpawn = bEnabled;
	if (Spawner) { Spawner->SetAutoSpawn(bEnabled); }
}

bool AClearanceSimulationController::SpawnOne()
{
	return Spawner ? Spawner->SpawnAircraft() : false;
}

void AClearanceSimulationController::ClearTraffic()
{
	if (AirspaceManager) { AirspaceManager->ClearAllAircraft(); }
}

void AClearanceSimulationController::HandleAircraftRegistered(FName Callsign)
{
	UClearanceAircraftBehaviour* Behaviour = NewObject<UClearanceAircraftBehaviour>(this);
	Behaviour->Initialise(AirspaceManager, Callsign);
	Behaviour->TouchdownZoneOffsetNm = FMath::Max(0.f, TouchdownZoneMeters) / 1852.f; // metres -> nm
	BehaviourMap.Add(Callsign, Behaviour);
	if (CommsRouter) { CommsRouter->RegisterBehaviour(Callsign, Behaviour); }

	// Spawn a visual for this aircraft's category, picking a random variant.
	if (GetWorld() && AirspaceManager)
	{
		const FAircraftState State = AirspaceManager->GetAircraftState(Callsign);
		const TArray<FAircraftVisualVariant>& Variants = VariantsFor(State.WakeCategory);
		if (Variants.Num() > 0)
		{
			const FAircraftVisualVariant& Variant = Variants[FMath::RandRange(0, Variants.Num() - 1)];
			if (Variant.AircraftClass)
			{
				if (AActor* Visual = GetWorld()->SpawnActor<AActor>(Variant.AircraftClass))
				{
					// The sim drives the transform directly each tick, so kill any
					// physics/collision on the mesh - otherwise gravity or collisions
					// fight our SetActorLocation and the plane judders. - TripleA
					Visual->SetActorEnableCollision(false);
					TArray<UPrimitiveComponent*> Prims;
					Visual->GetComponents<UPrimitiveComponent>(Prims);
					for (UPrimitiveComponent* Prim : Prims)
					{
						Prim->SetMobility(EComponentMobility::Movable);
						Prim->SetSimulatePhysics(false);
					}
					if (USceneComponent* Root = Visual->GetRootComponent())
					{
						Root->SetMobility(EComponentMobility::Movable);
					}

					Visual->SetActorScale3D(FVector(Variant.Scale));
					Visual->SetActorLocation(WorldPositionFor(State));

					FSpawnedAircraftVisual Entry;
					Entry.Actor = Visual;
					Entry.YawOffsetDeg = Variant.YawOffsetDeg;
					VisualActors.Add(Callsign, Entry);
				}
			}
		}
	}
}

void AClearanceSimulationController::HandleAircraftDeregistered(FName Callsign)
{
	BehaviourMap.Remove(Callsign);
	if (CommsRouter) { CommsRouter->UnregisterBehaviour(Callsign); }
	if (ConflictDetector) { ConflictDetector->RemoveAircraft(Callsign); }

	// Drop any TCAS pair entries involving this aircraft so the set doesn't leak.
	const FString CallStr = Callsign.ToString();
	for (auto It = TCASPairsAwaitingResolution.CreateIterator(); It; ++It)
	{
		if (It->Contains(CallStr)) { It.RemoveCurrent(); }
	}

	if (FSpawnedAircraftVisual* Visual = VisualActors.Find(Callsign))
	{
		if (Visual->Actor) { Visual->Actor->Destroy(); }
		VisualActors.Remove(Callsign);
	}
}

void AClearanceSimulationController::HandleConflictDetected(FConflictEvent Conflict)
{
	// Only an actual critical loss is logged as an incident; advisories/warnings
	// are heads-ups, not penalties.
	if (Conflict.AlertLevel == EAlertLevel::Critical && Scoring)
	{
		Scoring->LogIncident(EIncidentType::SeparationLoss, Conflict.AircraftA, Conflict.AircraftB, TEXT("Critical separation loss"));
	}

	const TCHAR* Lvl = Conflict.AlertLevel == EAlertLevel::Critical ? TEXT("CRITICAL")
		: Conflict.AlertLevel == EAlertLevel::Warning ? TEXT("WARNING") : TEXT("ADVISORY");

	// Surface it on screen until there's a real UI, so conflicts are visible. - TripleA
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, ColourFor(Conflict.AlertLevel),
			FString::Printf(TEXT("%s  %s / %s  -  %.1f nm, %.0f ft%s"), Lvl,
				*Conflict.AircraftA.ToString(), *Conflict.AircraftB.ToString(),
				Conflict.HorizontalSeparationNm, Conflict.VerticalSeparationFt,
				Conflict.bRequiresGoAround ? TEXT("  [GO-AROUND]") : TEXT("")));
	}

	if (Recorder)
	{
		Recorder->LogEvent(SessionTime, FString::Printf(TEXT("%s %s/%s (%.1fnm %.0fft)"),
			Lvl, *Conflict.AircraftA.ToString(), *Conflict.AircraftB.ToString(),
			Conflict.HorizontalSeparationNm, Conflict.VerticalSeparationFt));
	}
}

void AClearanceSimulationController::HandleConflictResolved(FConflictEvent Conflict)
{
	const FString Key = MakePairKey(Conflict.AircraftA, Conflict.AircraftB);
	const bool bTCASHandledThisOne = TCASPairsAwaitingResolution.Remove(Key) > 0;

	// Reward pulling apart a genuine conflict (Warning or worse) ONLY when the player
	// actually did it. If TCAS had to fire, the auto-split handled it - no reward, just
	// take the sep-loss + TCAS penalties. Trivial advisories clear on their own and
	// aren't worth points either. - TripleA
	if (Scoring && !bTCASHandledThisOne &&
		(Conflict.AlertLevel == EAlertLevel::Warning || Conflict.AlertLevel == EAlertLevel::Critical))
	{
		Scoring->LogIncident(EIncidentType::SuccessfulResolution, Conflict.AircraftA, Conflict.AircraftB, TEXT("Conflict resolved"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
				FString::Printf(TEXT("RESOLVED  %s / %s  (+%d)"), *Conflict.AircraftA.ToString(), *Conflict.AircraftB.ToString(), Scoring->PointsResolution));
		}
	}
	if (Recorder)
	{
		Recorder->LogEvent(SessionTime, FString::Printf(TEXT("RESOLVED %s/%s%s"),
			*Conflict.AircraftA.ToString(), *Conflict.AircraftB.ToString(),
			bTCASHandledThisOne ? TEXT(" (TCAS)") : TEXT("")));
	}
}

void AClearanceSimulationController::HandleGoAroundRequired(FName Callsign)
{
	if (CommsRouter) { CommsRouter->RouteGoAround(Callsign); }
	if (Scoring) { Scoring->LogIncident(EIncidentType::GoAroundTriggered, Callsign, NAME_None, TEXT("Go-around")); }
	if (Recorder) { Recorder->LogEvent(SessionTime, FString::Printf(TEXT("GO-AROUND %s"), *Callsign.ToString())); }
}

void AClearanceSimulationController::HandleWakeAdvisory(FName FollowingCallsign, FName LeadingCallsign, float RequiredSeparationNm)
{
	if (CommsRouter)
	{
		CommsRouter->ReceiveAdvisory(
			FString::Printf(TEXT("Wake caution: %s behind %s (need %.0f nm)"), *FollowingCallsign.ToString(), *LeadingCallsign.ToString(), RequiredSeparationNm),
			EAlertLevel::Advisory);
	}

	// Letting an aircraft into another's wake is a controller failure - scored as an
	// incident. Fires once per new encounter (the detector only broadcasts on entry),
	// so it doesn't spam the log. - TripleA
	if (Scoring)
	{
		Scoring->LogIncident(EIncidentType::WakeEncounter, FollowingCallsign, LeadingCallsign,
			FString::Printf(TEXT("In trail behind heavier traffic (need %.0fnm)"), RequiredSeparationNm));
	}

	// On screen too, until the UI exists. - TripleA
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor(255, 180, 0),
			FString::Printf(TEXT("WAKE CAUTION  %s behind %s  (need %.0f nm)"),
				*FollowingCallsign.ToString(), *LeadingCallsign.ToString(), RequiredSeparationNm));
	}
	if (Recorder)
	{
		Recorder->LogEvent(SessionTime, FString::Printf(TEXT("WAKE %s behind %s (need %.0fnm)"),
			*FollowingCallsign.ToString(), *LeadingCallsign.ToString(), RequiredSeparationNm));
	}
}

void AClearanceSimulationController::HandleTCASResolutionAdvisory(FName ClimberCallsign, FName DescenderCallsign, float ClimberTargetAltitudeFt, float DescenderTargetAltitudeFt)
{
	// Execute the coordinated split. An aircraft on approach converts to a go-around
	// (the climb has to win against the glideslope); enroute aircraft just get an
	// expedited altitude change through the normal instruction pipeline. - TripleA
	auto Issue = [&](FName Callsign, float TargetAlt, bool bClimb)
	{
		if (!AirspaceManager) { return; }
		const FAircraftState S = AirspaceManager->GetAircraftState(Callsign);
		if (!S.bIsValid) { return; }

		const bool bOnApproach = (S.FlightPhase == EFlightPhase::Approach || S.FlightPhase == EFlightPhase::Landing);
		if (bClimb && bOnApproach)
		{
			if (TObjectPtr<UClearanceAircraftBehaviour>* Bp = BehaviourMap.Find(Callsign))
			{
				if (*Bp) { (*Bp)->ExecuteGoAround(); }
			}
		}
		else
		{
			FAircraftInstruction I;
			I.TargetCallsign = Callsign;
			I.Type = EInstructionType::AltitudeChange;
			I.TargetValue = TargetAlt;
			I.bExpedite = true; // TCAS RAs are aggressive - shove the rate up
			PlayerIssueInstruction(I);
		}
	};

	Issue(ClimberCallsign, ClimberTargetAltitudeFt, true);
	Issue(DescenderCallsign, DescenderTargetAltitudeFt, false);

	// Suppress the resolution reward when this pair eventually clears - TCAS did the
	// resolving, not the player; awarding +50 here would be a point farm. - TripleA
	TCASPairsAwaitingResolution.Add(MakePairKey(ClimberCallsign, DescenderCallsign));

	if (Scoring)
	{
		Scoring->LogIncident(EIncidentType::TCASResolutionAdvisory, ClimberCallsign, DescenderCallsign,
			FString::Printf(TEXT("TCAS RA: %s CLIMB to %.0fft, %s DESCEND to %.0fft"),
				*ClimberCallsign.ToString(), ClimberTargetAltitudeFt,
				*DescenderCallsign.ToString(), DescenderTargetAltitudeFt));
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Red,
			FString::Printf(TEXT("TCAS RA  %s CLIMB  /  %s DESCEND"),
				*ClimberCallsign.ToString(), *DescenderCallsign.ToString()));
	}
	if (Recorder)
	{
		Recorder->LogEvent(SessionTime, FString::Printf(TEXT("TCAS RA  %s CLIMB / %s DESCEND"),
			*ClimberCallsign.ToString(), *DescenderCallsign.ToString()));
	}
}

void AClearanceSimulationController::HandleDifficultyAdjusted(float NewSpawnRate)
{
	if (Spawner) { Spawner->SetSpawnInterval(NewSpawnRate); }
}

// ---------------------------------------------------------------------------
// Console commands for steering aircraft by hand before a real input UI exists:
//   clearance.vector  <callsign> <heading>
//   clearance.climb   <callsign> <altitudeFt>
//   clearance.speed   <callsign> <knots>
// ---------------------------------------------------------------------------
static void ClearanceIssueFromConsole(const TArray<FString>& Args, UWorld* World, EInstructionType Type, const TCHAR* Label)
{
	if (!World || Args.Num() < 2)
	{
		UE_LOG(LogTemp, Warning, TEXT("usage: clearance.%s <callsign> <value>"), Label);
		return;
	}

	const FName Callsign(*Args[0]);
	const float Value = FCString::Atof(*Args[1]);

	for (TActorIterator<AClearanceSimulationController> It(World); It; ++It)
	{
		FAircraftInstruction Instruction;
		Instruction.TargetCallsign = Callsign;
		Instruction.Type = Type;
		Instruction.TargetValue = Value;

		const EInstructionResult Result = It->PlayerIssueInstruction(Instruction);
		const FString Msg = FString::Printf(TEXT("%s %s %.0f -> %s"), Label, *Callsign.ToString(), Value, *UEnum::GetValueAsString(Result));
		UE_LOG(LogTemp, Display, TEXT("%s"), *Msg);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, Msg); }
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("clearance.%s: no ClearanceSimulationController in the world"), Label);
}

static FAutoConsoleCommandWithWorldAndArgs GClearanceVectorCmd(
	TEXT("clearance.vector"),
	TEXT("clearance.vector <callsign> <heading> - turn an aircraft onto a heading"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		ClearanceIssueFromConsole(Args, World, EInstructionType::HeadingChange, TEXT("vector"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceClimbCmd(
	TEXT("clearance.climb"),
	TEXT("clearance.climb <callsign> <altitudeFt> - climb/descend an aircraft"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		ClearanceIssueFromConsole(Args, World, EInstructionType::AltitudeChange, TEXT("climb"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceSpeedCmd(
	TEXT("clearance.speed"),
	TEXT("clearance.speed <callsign> <knots> - change an aircraft's speed"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		ClearanceIssueFromConsole(Args, World, EInstructionType::SpeedChange, TEXT("speed"));
	}));

static AClearanceSimulationController* FindClearanceController(UWorld* World)
{
	if (World)
	{
		for (TActorIterator<AClearanceSimulationController> It(World); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

static FAutoConsoleCommandWithWorldAndArgs GClearanceAutoSpawnCmd(
	TEXT("clearance.autospawn"),
	TEXT("clearance.autospawn <0|1> - stop/start automatic spawning"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			const bool bOn = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : true;
			C->SetAutoSpawn(bOn);
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, FString::Printf(TEXT("auto-spawn %s"), bOn ? TEXT("ON") : TEXT("OFF"))); }
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceSpawnCmd(
	TEXT("clearance.spawn"),
	TEXT("clearance.spawn - spawn one aircraft now"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			const bool bOk = C->SpawnOne();
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, bOk ? TEXT("spawned 1") : TEXT("spawn failed (at cap?)")); }
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceClearCmd(
	TEXT("clearance.clear"),
	TEXT("clearance.clear - remove all aircraft from the sector"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->ClearTraffic();
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, TEXT("cleared all traffic")); }
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceCameraCmd(
	TEXT("clearance.camera"),
	TEXT("clearance.camera <default|overview|tower|approach|follow [callsign]|next>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C) { return; }
		if (Args.Num() < 1)
		{
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, TEXT("camera: default|overview|tower|approach|follow [callsign]|next")); }
			return;
		}
		const FString Sub = Args[0].ToLower();
		if (Sub.StartsWith(TEXT("next")))         { C->CycleCameraView(); return; }
		if (Sub.StartsWith(TEXT("over")))         { C->SetCameraView(EClearanceCameraView::Overview); return; }
		if (Sub.StartsWith(TEXT("tow")))          { C->SetCameraView(EClearanceCameraView::Tower); return; }
		if (Sub.StartsWith(TEXT("app")))          { C->SetCameraView(EClearanceCameraView::Approach); return; }
		if (Sub.StartsWith(TEXT("fol")))
		{
			const FName Cs = (Args.Num() > 1) ? FName(*Args[1]) : NAME_None;
			C->SetCameraView(EClearanceCameraView::Follow, Cs);
			// Optional 3rd arg = angle (chase/cockpit/side/top).
			if (Args.Num() > 2)
			{
				const FString A = Args[2].ToLower();
				if (A.StartsWith(TEXT("cock"))) { C->SetFollowAngle(EClearanceFollowAngle::Cockpit); }
				else if (A.StartsWith(TEXT("sid"))) { C->SetFollowAngle(EClearanceFollowAngle::Side); }
				else if (A.StartsWith(TEXT("top"))) { C->SetFollowAngle(EClearanceFollowAngle::Top); }
				else { C->SetFollowAngle(EClearanceFollowAngle::Chase); }
			}
			return;
		}
		if (Sub.StartsWith(TEXT("angle")))
		{
			// Change just the follow sub-angle without changing the followed aircraft.
			if (Args.Num() > 1)
			{
				const FString A = Args[1].ToLower();
				if (A.StartsWith(TEXT("next"))) { C->CycleFollowAngle(); }
				else if (A.StartsWith(TEXT("cock"))) { C->SetFollowAngle(EClearanceFollowAngle::Cockpit); }
				else if (A.StartsWith(TEXT("sid"))) { C->SetFollowAngle(EClearanceFollowAngle::Side); }
				else if (A.StartsWith(TEXT("top"))) { C->SetFollowAngle(EClearanceFollowAngle::Top); }
				else { C->SetFollowAngle(EClearanceFollowAngle::Chase); }
			}
			else
			{
				C->CycleFollowAngle();
			}
			return;
		}
		C->SetCameraView(EClearanceCameraView::Default);
	}));

// --- After-Action Review console commands -------------------------------------
static FAutoConsoleCommandWithWorldAndArgs GClearanceRecStartCmd(
	TEXT("clearance.rec.start"),
	TEXT("clearance.rec.start - start recording the session for replay"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World)) { C->StartRecording(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceRecStopCmd(
	TEXT("clearance.rec.stop"),
	TEXT("clearance.rec.stop - stop recording (keeps the buffer)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World)) { C->StopRecording(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceReplayStartCmd(
	TEXT("clearance.replay.start"),
	TEXT("clearance.replay.start - freeze live sim and replay the recording from t=0"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World)) { C->EnterReplay(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceReplayLiveCmd(
	TEXT("clearance.replay.live"),
	TEXT("clearance.replay.live - leave replay and go back to the live sim"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World)) { C->ResumeLive(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceReplayPauseCmd(
	TEXT("clearance.replay.pause"),
	TEXT("clearance.replay.pause [0|1] - pause/unpause the replay (no arg = pause)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			const bool bPaused = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : true;
			C->SetReplayPaused(bPaused);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceReplaySeekCmd(
	TEXT("clearance.replay.seek"),
	TEXT("clearance.replay.seek <seconds> - jump the replay to this session time"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { return; }
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->SeekReplay(FCString::Atof(*Args[0]));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceReplaySpeedCmd(
	TEXT("clearance.replay.speed"),
	TEXT("clearance.replay.speed <x> - replay playback speed (e.g. 0.5 slow, 4 fast)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { return; }
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->SetReplaySpeed(FCString::Atof(*Args[0]));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceExitCmd(
	TEXT("clearance.exit"),
	TEXT("clearance.exit <callsign> - clear an aircraft to leave the sector (scores as a successful departure when it crosses the ring)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("usage: clearance.exit <callsign>")); return; }
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			FAircraftInstruction I;
			I.TargetCallsign = FName(*Args[0]);
			I.Type = EInstructionType::ExitSector;
			const EInstructionResult Result = C->PlayerIssueInstruction(I);
			const FString Msg = FString::Printf(TEXT("exit %s -> %s"), *I.TargetCallsign.ToString(), *UEnum::GetValueAsString(Result));
			UE_LOG(LogTemp, Display, TEXT("%s"), *Msg);
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, Msg); }
		}
	}));

// Drop a single aircraft straight into the sector at a chosen state - used by the test
// scenarios below to set up a guaranteed conflict / wake situation. - TripleA
static void SpawnTestAircraft(AClearanceAirspaceManager* Mgr, FName Callsign, FVector PosNm, float AltFt, float HeadingDeg, float SpeedKts, EWakeCategory Cat)
{
	if (!Mgr) { return; }
	FAircraftState S;
	S.Callsign = Callsign;
	S.Position = PosNm;
	S.Altitude = AltFt;
	S.Heading = HeadingDeg;
	S.Speed = SpeedKts;
	S.FlightPhase = EFlightPhase::Enroute;
	S.WakeCategory = Cat;
	Mgr->RegisterAircraft(S); // Behaviour holds the entry heading/alt/speed, so it flies straight
}

static FAutoConsoleCommandWithWorldAndArgs GClearanceTestConflictCmd(
	TEXT("clearance.test.conflict"),
	TEXT("clearance.test.conflict - two aircraft head-on at the same level; watch the alert escalate"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C || !C->GetAirspaceManager()) { return; }
		C->ClearTraffic();
		AClearanceAirspaceManager* M = C->GetAirspaceManager();
		// 12 nm apart on the E-W line, same altitude, flying at each other.
		SpawnTestAircraft(M, TEXT("CONFL1"), FVector(-6.f, 0.f, 0.f), 10000.f,  90.f, 250.f, EWakeCategory::Medium);
		SpawnTestAircraft(M, TEXT("CONFL2"), FVector( 6.f, 0.f, 0.f), 10000.f, 270.f, 250.f, EWakeCategory::Medium);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("TEST: CONFL1/CONFL2 head-on at FL100 - alert should escalate ADVISORY->WARNING->CRITICAL")); }
	}));

// light/small/L, medium/M, heavy/big/H, super/S - forgiving so you can type the word
// or just the first letter. - TripleA
static EWakeCategory ParseWakeCat(const FString& Tok, EWakeCategory Default)
{
	const FString T = Tok.ToUpper();
	if (T.StartsWith(TEXT("MED")) || T == TEXT("M")) return EWakeCategory::Medium;
	if (T.StartsWith(TEXT("HEA")) || T.StartsWith(TEXT("BIG")) || T == TEXT("H") || T == TEXT("B")) return EWakeCategory::Heavy;
	if (T.StartsWith(TEXT("SUP")) || T == TEXT("S")) return EWakeCategory::Super;
	if (T.StartsWith(TEXT("LIG")) || T.StartsWith(TEXT("SMA")) || T == TEXT("L")) return EWakeCategory::Light;
	return Default;
}

static const TCHAR* WakeCatName(EWakeCategory C)
{
	switch (C)
	{
	case EWakeCategory::Light:  return TEXT("Light");
	case EWakeCategory::Heavy:  return TEXT("Heavy");
	case EWakeCategory::Super:  return TEXT("Super");
	default:                    return TEXT("Medium");
	}
}

static FAutoConsoleCommandWithWorldAndArgs GClearanceTestWakeCmd(
	TEXT("clearance.test.wake"),
	TEXT("clearance.test.wake [leader] [follower] - follower trails the leader in wake. Categories: light/medium/heavy/super (default heavy light)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C || !C->GetAirspaceManager()) { return; }
		AClearanceAirspaceManager* M = C->GetAirspaceManager();

		const EWakeCategory LeadCat = Args.Num() > 0 ? ParseWakeCat(Args[0], EWakeCategory::Heavy) : EWakeCategory::Heavy;
		const EWakeCategory FollowCat = Args.Num() > 1 ? ParseWakeCat(Args[1], EWakeCategory::Light) : EWakeCategory::Light;

		const ClearanceConstants::FCategoryPerformance LP = ClearanceConstants::GetCategoryPerformance(LeadCat);
		const ClearanceConstants::FCategoryPerformance FP = ClearanceConstants::GetCategoryPerformance(FollowCat);
		const float LSpd = FMath::Clamp(175.f, LP.MinOperatingSpeedKts, LP.MaxOperatingSpeedKts);
		const float FSpd = FMath::Clamp(170.f, FP.MinOperatingSpeedKts, FP.MaxOperatingSpeedKts);

		C->ClearTraffic();
		// Leader ahead, follower 3.5 nm behind in trail at the same level.
		SpawnTestAircraft(M, TEXT("LEAD"),  FVector(0.f,  0.f, 0.f), 8000.f, 0.f, LSpd, LeadCat);
		SpawnTestAircraft(M, TEXT("TRAIL"), FVector(0.f, -3.5f, 0.f), 8000.f, 0.f, FSpd, FollowCat);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Cyan,
				FString::Printf(TEXT("TEST: %s (TRAIL) 3.5nm behind %s (LEAD) - wake rocks the trailer if the leader makes enough wake"),
					WakeCatName(FollowCat), WakeCatName(LeadCat)));
		}
	}));
