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
			const FVector W = It->GetActorLocation();
			const FVector2D CentreNm((W.X - Origin.X) / WorldUnitsPerNm, (W.Y - Origin.Y) / WorldUnitsPerNm);
			const float H = It->LandingHeadingDeg;
			const float HalfNm = FMath::Max(0.f, It->RunwayLengthMeters) / 1852.f * 0.5f;
			const float HRad = FMath::DegreesToRadians(H);
			const FVector2D Inbound(FMath::Sin(HRad), FMath::Cos(HRad)); // direction flown to land on H

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
			if (!bGroundSet) { GroundWorldZ = W.Z; bGroundSet = true; } // 0ft = runway surface
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
		GroundWorldZ + State.Altitude * AltitudeWorldScale);
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
		const float VertOverHoriz = (State.Speed > 1.f) ? ((State.ClimbRate / 101.269f) / State.Speed) : 0.f; // 1 kt = 101.27 ft/min
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
		const float CorridorHalf = ClearanceConstants::ApproachCorridorHalfWidthNm;
		const FVector Thr(Origin.X + Env.ActiveRunwayThreshold.X * S, Origin.Y + Env.ActiveRunwayThreshold.Y * S, GroundWorldZ);
		const FVector ApproachEnd = Thr - InboundDir * (CorridorLen * S);      // out along the approach side

		DrawDebugSphere(World, Thr, 700.f, 12, FColor::White, false, -1.f, 0, 60.f);
		DrawDebugLine(World, Thr, ApproachEnd, FColor::White, false, -1.f, 0, 120.f);
		DrawDebugLine(World, Thr + RightDir * CorridorHalf * S, ApproachEnd + RightDir * CorridorHalf * S, FColor(90, 90, 90), false, -1.f, 0, 40.f);
		DrawDebugLine(World, Thr - RightDir * CorridorHalf * S, ApproachEnd - RightDir * CorridorHalf * S, FColor(90, 90, 90), false, -1.f, 0, 40.f);
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
			const FVector P(Origin.X + A.Position.X * S, Origin.Y + A.Position.Y * S, GroundWorldZ + A.Altitude * AltitudeWorldScale);
			DrawDebugSphere(World, P, 500.f, 10, C, false, -1.f, 0, 40.f);

			const float HeadingRad = FMath::DegreesToRadians(A.Heading);
			const FVector Dir(FMath::Sin(HeadingRad), FMath::Cos(HeadingRad), 0.f);
			DrawDebugLine(World, P, P + Dir * 2200.f, C, false, -1.f, 0, 60.f);
		}

		// Shows current>target for each axis, so we can see if instructions take.
		Readout += FString::Printf(TEXT("%s  hdg %3.0f>%3.0f  alt %5.0f>%5.0f  spd %3.0f>%3.0f%s\n"),
			*A.Callsign.ToString(),
			A.Heading, A.TargetHeading,
			A.Altitude, A.TargetAltitude,
			A.Speed, A.TargetSpeed,
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

		if (State.FlightPhase == EFlightPhase::Landing && State.Altitude <= 100.f)
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
