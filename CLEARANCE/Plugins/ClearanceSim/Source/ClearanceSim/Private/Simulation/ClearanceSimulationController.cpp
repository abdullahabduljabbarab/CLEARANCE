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
		ConflictDetector->OnGoAroundRequired.AddDynamic(this, &AClearanceSimulationController::HandleGoAroundRequired);
		ConflictDetector->OnWakeTurbulenceAdvisory.AddDynamic(this, &AClearanceSimulationController::HandleWakeAdvisory);
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

	const float SimDelta = DeltaTime * FMath::Max(0.f, SimulationTimeScale);
	SessionTime += SimDelta;
	StepSimulation(SimDelta);
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
		else if (bApproaching && State.Altitude < 150.f && State.ClimbRate < 0.f)
		{
			PitchDeg = 8.f; // FLARE - nose lifts up just before touchdown
		}
		else if (bApproaching && State.ClimbRate < -50.f)
		{
			// On the glidepath the real angle is shallow (~3deg) so it'd read as level;
			// dramatise a clear nose-DOWN descent attitude for the landing, then the
			// flare above lifts it right before touchdown. - TripleA
			PitchDeg = -9.f; // clear nose-down descent attitude
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
		const float RollWob  = (FMath::Sin(Now * 0.9f + Ph) * 1.6f + FMath::Sin(Now * 2.3f + Ph * 1.7f) * 0.5f) * Buffet;
		const float PitchWob = FMath::Sin(Now * 1.3f + Ph * 0.6f) * 0.8f * Buffet;
		const float YawWob   = FMath::Sin(Now * 0.7f + Ph * 1.3f) * 0.6f * Buffet;
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
		const int32 H1 = FMath::RoundToInt(It->LandingHeadingDeg);
		const int32 H2 = (H1 + 180) % 360;
		DrawDebugString(World, E1 + FVector(0, 0, 1.5f * S), FString::Printf(TEXT("RWY %03d"), H1), nullptr, FColor::Yellow, 0.f, true, 1.2f);
		if (It->bAllowReciprocal)
		{
			DrawDebugString(World, E2 + FVector(0, 0, 1.5f * S), FString::Printf(TEXT("RWY %03d"), H2), nullptr, FColor::Yellow, 0.f, true, 1.2f);
		}
	}
	FString Readout = FString::Printf(TEXT("CLEARANCE  |  t=%.0fs  |  score=%d  |  eff=%.0f%%  |  traffic=%d  |  wind %03.0f/%.0fkt  |  active rwy %03.0f\n"),
		SessionTime,
		Scoring ? Scoring->GetCurrentScore() : 0,
		Scoring ? Scoring->GetEfficiency() * 100.f : 100.f,
		States.Num(),
		Env.WindDirection, Env.WindSpeed, Env.ActiveRunwayHeading);

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
	if (CommsRouter)
	{
		return CommsRouter->IssueInstruction(Instruction);
	}
	return EInstructionResult::Rejected_InvalidCallsign;
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
}

void AClearanceSimulationController::HandleGoAroundRequired(FName Callsign)
{
	if (CommsRouter) { CommsRouter->RouteGoAround(Callsign); }
	if (Scoring) { Scoring->LogIncident(EIncidentType::GoAroundTriggered, Callsign, NAME_None, TEXT("Go-around")); }
}

void AClearanceSimulationController::HandleWakeAdvisory(FName FollowingCallsign, FName LeadingCallsign, float RequiredSeparationNm)
{
	if (CommsRouter)
	{
		CommsRouter->ReceiveAdvisory(
			FString::Printf(TEXT("Wake caution: %s behind %s (need %.0f nm)"), *FollowingCallsign.ToString(), *LeadingCallsign.ToString(), RequiredSeparationNm),
			EAlertLevel::Advisory);
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
