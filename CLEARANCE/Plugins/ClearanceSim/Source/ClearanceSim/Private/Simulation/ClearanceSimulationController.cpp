#include "Simulation/ClearanceSimulationController.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Airspace/ClearanceRunway.h"
#include "Airspace/ClearanceViolationZone.h"
#include "Airspace/ClearanceRestrictedArea.h"
#include "Aircraft/ClearanceAircraftSpawner.h"
#include "Aircraft/ClearanceAircraftBehaviour.h"
#include "Aircraft/ClearanceAircraftVisualInterface.h"
#include "Comms/ClearanceInstructionValidator.h"
#include "Comms/ClearanceCommsRouter.h"
#include "Comms/ClearanceVoiceOutput.h"
#include "Safety/ClearanceConflictDetector.h"
#include "Safety/ClearanceRadarSite.h"
#include "Safety/ClearanceRadar.h"
#include "Scoring/ClearanceScoring.h"
#include "Simulation/ClearanceSessionRecorder.h"
#include "Simulation/ClearanceDISEmitter.h"
#include "Simulation/ClearanceDISReceiver.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Core/ClearanceConstants.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
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

	// Give the controller a real scene root so its transform gizmo shows up in the
	// editor - the actor's location is the sector centre, so the user has to be
	// able to drag it around. Without a root component the actor has no widget. - TripleA
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("SectorRoot"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);
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
	DISEmitter = NewObject<UClearanceDISEmitter>(this);
	DISReceiver = NewObject<UClearanceDISReceiver>(this);
	Radar = NewObject<UClearanceRadar>(this);
	if (Radar) { Radar->SetReferences(AirspaceManager); }

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
		// Keep the entry circle locked to the visible sector boundary so aircraft
		// appear ON the edge, not 10nm inside it. - TripleA
		Spawner->EntryRadiusNm = ExitRadiusNm;
	}

	SessionTime = 0.f;
	bPaused = false;
	bSessionActive = true;
	NextViperNumber = 1;
	ViolatedPairs.Reset();
	BustedPairs.Reset();
	CrashSites.Reset();

	// Background systems that should "just be on" for normal operator play. Each
	// console toggle stays for dev use, but the player layer doesn't see them. - TripleA
	if (bAutoStartRecording) { StartRecording(); }
	if (bAutoStartRadar)     { SetRadarEnabled(true); }
	if (bAutoStartDIS)       { StartDIS(DISDefaultHost, DISDefaultPort); }
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
		// Radar still ticks in replay, so the operator can debrief from the radar view.
		if (Radar && Radar->IsEnabled()) { Radar->Tick(DeltaTime); }
		// DIS receiver still polls in replay - a partner sim watching the same debrief
		// can still drop traffic on our scope. - TripleA
		if (DISReceiver && DISReceiver->IsRunning() && AirspaceManager && GetWorld())
		{
			DISReceiver->Poll(AirspaceManager, GetWorld()->GetRealTimeSeconds());
		}
		UpdateVisuals();
		UpdateFollowCamera();
		if (DISEmitter && DISEmitter->IsRunning() && AirspaceManager)
		{
			DISEmitter->EmitStates(AirspaceManager->GetAllAircraftStates(), ReplayTime);
		}
		DrawDebugView();
		return;
	}

	// Pull anything off the DIS wire BEFORE the local sim steps, so federated
	// aircraft are present for separation checks this same tick. - TripleA
	if (DISReceiver && DISReceiver->IsRunning() && AirspaceManager && GetWorld())
	{
		DISReceiver->Poll(AirspaceManager, GetWorld()->GetRealTimeSeconds());
	}

	const float SimDelta = DeltaTime * FMath::Max(0.f, SimulationTimeScale);
	SessionTime += SimDelta;

	// Violation zone check: any declared-hostile aircraft inside a protected zone
	// fires a catastrophic ViolationZoneBreached incident (mirror of mis-ID). One-
	// shot per (zone, aircraft) pair so a stuck hostile doesn't spam the score. - TripleA
	if (AirspaceManager && GetWorld())
	{
		TArray<AClearanceViolationZone*> Zones;
		for (TActorIterator<AClearanceViolationZone> ZIt(GetWorld()); ZIt; ++ZIt) { Zones.Add(*ZIt); }
		if (Zones.Num() > 0)
		{
			const FVector OriginW = GetActorLocation();
			const float W = FMath::Max(1.f, WorldUnitsPerNm);
			for (const FAircraftState& S : AirspaceManager->GetAllAircraftStates())
			{
				// Real ROE: protected airspace doesn't care about your formal classification.
				// Hostile, Unknown, NORDO, or a hijacked civilian (squawk 7500) are all
				// unauthorised - any of them inside is the failure. - TripleA
				const bool bUncooperative = (S.ThreatClass == EThreatClass::Hostile)
					|| (S.ThreatClass == EThreatClass::Unknown)
					|| !S.bIFFOperational
					|| (S.ActiveEmergency == EEmergencyType::Hijack);
				if (!bUncooperative) { continue; }
				for (AClearanceViolationZone* Z : Zones)
				{
					if (!Z) { continue; }
					const FVector ZW = Z->GetActorLocation();
					const FVector2D ZNm((ZW.X - OriginW.X) / W, (ZW.Y - OriginW.Y) / W);
					const float DistNm = FVector2D::Distance(FVector2D(S.Position.X, S.Position.Y), ZNm);
					if (DistNm > Z->RadiusNm) { continue; }
					const FName PairKey = FName(*(S.Callsign.ToString() + TEXT("|") + Z->ZoneName.ToString()));
					if (ViolatedPairs.Contains(PairKey)) { continue; }
					ViolatedPairs.Add(PairKey);
					if (Scoring)
					{
						Scoring->LogIncident(EIncidentType::ViolationZoneBreached, S.Callsign, Z->ZoneName,
							FString::Printf(TEXT("Hostile %s reached %s"), *S.Callsign.ToString(), *Z->ZoneName.ToString()));
					}
					if (Recorder)
					{
						Recorder->LogEvent(SessionTime, FString::Printf(
							TEXT("VIOLATION - hostile %s breached %s"), *S.Callsign.ToString(), *Z->ZoneName.ToString()));
					}
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(102, 30.f, FColor::Red,
							FString::Printf(TEXT("*** VIOLATION *** HOSTILE %s REACHED %s (-%d)"),
								*S.Callsign.ToString(), *Z->ZoneName.ToString(),
								Scoring ? Scoring->PenaltyViolationZoneBreached : 1000));
					}
				}
			}
		}
	}

	// Restricted airspace check: civilian aircraft entering a restricted area
	// (military training zone, P-area, nuclear site) logs a RestrictedAirspaceBust.
	// One-shot per (area, aircraft) pair. NOT a doctrine failure (no -1000) - just
	// a controller screw-up the operator should have prevented by vectoring around
	// it. - TripleA
	if (AirspaceManager && GetWorld())
	{
		TArray<AClearanceRestrictedArea*> Areas;
		for (TActorIterator<AClearanceRestrictedArea> AIt(GetWorld()); AIt; ++AIt) { Areas.Add(*AIt); }
		if (Areas.Num() > 0)
		{
			const FVector OriginW = GetActorLocation();
			const float W = FMath::Max(1.f, WorldUnitsPerNm);
			for (const FAircraftState& S : AirspaceManager->GetAllAircraftStates())
			{
				// Only civilian friendly traffic - military, hostiles, hijacks etc
				// are handled by other paths (or are LEGITIMATELY in the area).
				if (S.bIsMilitary || S.bIsExternal || S.ThreatClass != EThreatClass::Friendly) { continue; }
				if (S.ActiveEmergency != EEmergencyType::None) { continue; }
				for (AClearanceRestrictedArea* A : Areas)
				{
					if (!A) { continue; }
					const FVector AW = A->GetActorLocation();
					const FVector2D ANm((AW.X - OriginW.X) / W, (AW.Y - OriginW.Y) / W);
					const float DistNm = FVector2D::Distance(FVector2D(S.Position.X, S.Position.Y), ANm);
					if (DistNm > A->RadiusNm) { continue; }
					const FName PairKey = FName(*(S.Callsign.ToString() + TEXT("|") + A->AreaName.ToString()));
					if (BustedPairs.Contains(PairKey)) { continue; }
					BustedPairs.Add(PairKey);
					if (Scoring)
					{
						Scoring->LogIncident(EIncidentType::RestrictedAirspaceBust, S.Callsign, A->AreaName,
							FString::Printf(TEXT("Civilian %s busted %s"), *S.Callsign.ToString(), *A->AreaName.ToString()));
					}
					if (Recorder)
					{
						Recorder->LogEvent(SessionTime, FString::Printf(
							TEXT("AIRSPACE BUST - %s entered %s"), *S.Callsign.ToString(), *A->AreaName.ToString()));
					}
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor(255, 140, 0),
							FString::Printf(TEXT("AIRSPACE BUST: %s entered %s (-%d)"),
								*S.Callsign.ToString(), *A->AreaName.ToString(),
								Scoring ? Scoring->PenaltyRestrictedAirspaceBust : 150));
					}
				}
			}
		}
	}

	// Emergency declaration + fuel tick + crash check. Runs on REAL DeltaTime so
	// the fuel clock is consistent regardless of simulation time scale. - TripleA
	if (AirspaceManager)
	{
		const TArray<FAircraftState> Snapshot = AirspaceManager->GetAllAircraftStates();
		for (const FAircraftState& Ro : Snapshot)
		{
			// Civilian only - military, external, and GCI-controlled aircraft don't
			// roll for emergencies, and aircraft already in one don't re-roll.
			if (Ro.bIsExternal || Ro.bIsMilitary || Ro.bUnderGCIControl) { continue; }
			if (Ro.FlightPhase == EFlightPhase::Landing || Ro.FlightPhase == EFlightPhase::Approach) { continue; }

			// Conversion roll.
			if (Ro.ActiveEmergency == EEmergencyType::None && EmergencyChancePerSecond > 0.f)
			{
				const float PerTick = EmergencyChancePerSecond * DeltaTime;
				if (FMath::FRand() < PerTick)
				{
					FAircraftState S = Ro;
					const int32 R = FMath::RandRange(0, 3);
					int32 NewSquawk = S.SquawkCode;
					switch (R)
					{
					case 0:
					{
						S.ActiveEmergency = EEmergencyType::GeneralMayday;
						NewSquawk = 7700;
						S.FuelRemainingMinutes = MaydayTimeoutMinutes;
						// Pick a specific failure - real MAYDAYs always state the cause.
						static const TCHAR* Causes[] = {
							TEXT("engine failure"),
							TEXT("engine fire"),
							TEXT("smoke in the cockpit"),
							TEXT("hydraulic failure"),
							TEXT("cabin depressurization"),
							TEXT("bird strike, lost both engines"),
							TEXT("medical emergency, captain incapacitated"),
							TEXT("cargo fire"),
							TEXT("landing gear failure"),
							TEXT("electrical failure"),
							TEXT("flight controls jammed"),
						};
						S.EmergencyDetail = Causes[FMath::RandRange(0, UE_ARRAY_COUNT(Causes) - 1)];
						break;
					}
					case 1: S.ActiveEmergency = EEmergencyType::CommsFailure;  NewSquawk = 7600; break;
					case 2: S.ActiveEmergency = EEmergencyType::Hijack;        NewSquawk = 7500; break;
					default: S.ActiveEmergency = EEmergencyType::FuelLow;       S.FuelRemainingMinutes = FuelEmergencyMinutes; break;
					}
					S.SquawkCode = NewSquawk;
					S.EmergencyDeclaredAtSeconds = SessionTime;

					// Comms failure (7600): the pilot follows the published lost-comms
					// procedure - descend to pattern altitude, head for the active
					// runway, fly the approach as if cleared. ATC's job is keeping
					// other traffic clear; the NORDO flies itself in. - TripleA
					if (S.ActiveEmergency == EEmergencyType::CommsFailure && AirspaceManager)
					{
						const FSectorEnvironment Env = AirspaceManager->GetCurrentEnvironment();
						if (Env.ActiveRunwayHeading >= 0.f)
						{
							const FVector2D Threshold(Env.ActiveRunwayThreshold.X, Env.ActiveRunwayThreshold.Y);
							const FVector2D Here(S.Position.X, S.Position.Y);
							// Aim ~10nm out on the localiser so we capture from outside the corridor.
							const float HRad = FMath::DegreesToRadians(Env.ActiveRunwayHeading);
							const FVector2D Inbound(FMath::Sin(HRad), FMath::Cos(HRad));
							const FVector2D ApproachGate = Threshold - Inbound * 10.f;
							const FVector2D ToGate = ApproachGate - Here;
							float Hdg = FMath::RadiansToDegrees(FMath::Atan2(ToGate.X, ToGate.Y));
							if (Hdg < 0.f) { Hdg += 360.f; }
							S.TargetHeading = Hdg;
							S.TargetAltitude = 3000.f;
							S.FlightPhase = EFlightPhase::Approach;
						}
					}

					// Hijacks: the hijackers now point the aircraft at a high-value
					// target. If any violation zone is placed, the aircraft turns onto
					// it from this moment. - TripleA
					if (S.ActiveEmergency == EEmergencyType::Hijack && GetWorld())
					{
						TArray<AClearanceViolationZone*> Zones;
						for (TActorIterator<AClearanceViolationZone> ZIt(GetWorld()); ZIt; ++ZIt) { Zones.Add(*ZIt); }
						if (Zones.Num() > 0)
						{
							AClearanceViolationZone* Z = Zones[FMath::RandRange(0, Zones.Num() - 1)];
							if (Z)
							{
								const FVector ZW = Z->GetActorLocation();
								const FVector OriginW = GetActorLocation();
								const float W = FMath::Max(1.f, WorldUnitsPerNm);
								const FVector2D ZNm((ZW.X - OriginW.X) / W, (ZW.Y - OriginW.Y) / W);
								const FVector2D ToZone = ZNm - FVector2D(S.Position.X, S.Position.Y);
								float Hdg = FMath::RadiansToDegrees(FMath::Atan2(ToZone.X, ToZone.Y));
								if (Hdg < 0.f) { Hdg += 360.f; }
								S.TargetHeading = Hdg;
							}
						}
					}

					AirspaceManager->RequestStateUpdate(S);

					if (Recorder)
					{
						Recorder->LogEvent(SessionTime, FString::Printf(TEXT("EMERGENCY - %s declared %s (sq %d)"),
							*S.Callsign.ToString(), *UEnum::GetValueAsString(S.ActiveEmergency), NewSquawk));
					}
					if (GEngine)
					{
						const FColor Col = (S.ActiveEmergency == EEmergencyType::Hijack) ? FColor::Red : FColor::Yellow;
						GEngine->AddOnScreenDebugMessage(-1, 8.f, Col,
							FString::Printf(TEXT("EMERGENCY: %s SQUAWK %d (%s)"),
								*S.Callsign.ToString(), NewSquawk, *UEnum::GetValueAsString(S.ActiveEmergency)));
					}

					// Audio cue via any placed VoiceOutput:
					//   Mayday / FuelLow -> voice the declaration (different lines)
					//   CommsFailure     -> short static burst (broken radio)
					//   Hijack           -> very brief static (pilot keyed mic and was cut off)
					// The static cue for hijack is a small concession to gameplay over
					// strict doctrine; pure silence is more authentic but leaves the
					// player wondering if anything happened. - TripleA
					if (GetWorld())
					{
						for (TActorIterator<AClearanceVoiceOutput> VoIt(GetWorld()); VoIt; ++VoIt)
						{
							if (!*VoIt) { break; }
							switch (S.ActiveEmergency)
							{
							case EEmergencyType::GeneralMayday:
								VoIt->Speak(S.Callsign,
									S.EmergencyDetail.IsEmpty()
										? FString::Printf(TEXT("Mayday, mayday, mayday, %s, declaring emergency, request immediate landing"), *S.Callsign.ToString())
										: FString::Printf(TEXT("Mayday, mayday, mayday, %s, %s, request immediate landing"), *S.Callsign.ToString(), *S.EmergencyDetail),
									FString());
								break;
							case EEmergencyType::FuelLow:
								VoIt->Speak(S.Callsign,
									FString::Printf(TEXT("Mayday, mayday, mayday, %s, fuel emergency, request immediate landing"), *S.Callsign.ToString()),
									FString());
								break;
							case EEmergencyType::CommsFailure:
								VoIt->PlayStatic(2.0f);
								break;
							case EEmergencyType::Hijack:
								VoIt->PlayStatic(0.6f);
								break;
							default: break;
							}
							break;
						}
					}
					continue;
				}
			}

			// Countdown tick + crash for any emergency carrying a timer (fuel or
			// mayday). Hijack and comms failure don't time out by themselves. - TripleA
			const bool bHasTimer = (Ro.ActiveEmergency == EEmergencyType::FuelLow)
				|| (Ro.ActiveEmergency == EEmergencyType::GeneralMayday);
			if (bHasTimer && Ro.FuelRemainingMinutes > 0.f && !Ro.bCrashing)
			{
				FAircraftState S = Ro;
				S.FuelRemainingMinutes -= DeltaTime / 60.f;
				if (S.FuelRemainingMinutes <= 0.f)
				{
					BeginCrash(S, (Ro.ActiveEmergency == EEmergencyType::FuelLow)
						? TEXT("Fuel exhaustion") : TEXT("Mayday situation deteriorated"));
				}
				else
				{
					AirspaceManager->RequestStateUpdate(S);

					// Escalating urgency calls as the countdown burns through.
					// Threshold bits: 1=66%, 2=33%, 4=10% remaining. Each fires
					// once per aircraft. - TripleA
					const float Total = (S.ActiveEmergency == EEmergencyType::FuelLow)
						? FuelEmergencyMinutes : MaydayTimeoutMinutes;
					if (Total > 0.f)
					{
						const float Pct = S.FuelRemainingMinutes / Total;
						int32& Mask = UrgencyThresholdsHit.FindOrAdd(S.Callsign);
						const FName Cs = S.Callsign;
						const bool bFuel = (S.ActiveEmergency == EEmergencyType::FuelLow);

						FString Line;
						int32 NewBit = 0;
						if (Pct < 0.10f && !(Mask & 4))
						{
							NewBit = 4;
							Line = bFuel
								? FString::Printf(TEXT("%s, fuel exhausted any moment, we need runway now"), *Cs.ToString())
								: FString::Printf(TEXT("%s, we can't hold her, we're going down"), *Cs.ToString());
						}
						else if (Pct < 0.33f && !(Mask & 2))
						{
							NewBit = 2;
							Line = bFuel
								? FString::Printf(TEXT("%s, fuel state critical, request immediate vectors"), *Cs.ToString())
								: FString::Printf(TEXT("%s, situation worsening fast, we need a runway now"), *Cs.ToString());
						}
						else if (Pct < 0.66f && !(Mask & 1))
						{
							NewBit = 1;
							Line = bFuel
								? FString::Printf(TEXT("%s, fuel low, request priority handling"), *Cs.ToString())
								: FString::Printf(TEXT("%s, situation deteriorating, request priority"), *Cs.ToString());
						}

						if (NewBit != 0 && GetWorld())
						{
							Mask |= NewBit;
							for (TActorIterator<AClearanceVoiceOutput> VoIt(GetWorld()); VoIt; ++VoIt)
							{
								if (*VoIt) { VoIt->Speak(Cs, Line, FString()); }
								break;
							}
						}
					}
				}
			}
		}
	}

	// GCI mode flips on the moment any non-cooperative or non-friendly aircraft
	// is in the sector, off when only normal civilian traffic remains. Reflects
	// real ops - there's no 'air defence mode' switch, the situation determines
	// whether the operator is doing GCI or not. - TripleA
	if (bAutoGCIMode && AirspaceManager)
	{
		bool bThreatPresent = false;
		for (const FAircraftState& S : AirspaceManager->GetAllAircraftStates())
		{
			if (!S.bIFFOperational || S.ThreatClass == EThreatClass::Hostile || S.ThreatClass == EThreatClass::Unknown)
			{
				bThreatPresent = true;
				break;
			}
		}
		if (bThreatPresent != bGCIMode) { SetGCIModeEnabled(bThreatPresent); }
	}

	StepSimulation(SimDelta);

	// Capture the post-tick state into the recording timeline.
	if (Recorder && Recorder->IsRecording() && AirspaceManager)
	{
		Recorder->CaptureSnapshot(SessionTime, AirspaceManager->GetAllAircraftStates());
	}

	// Publish each aircraft as a DIS Entity State PDU. Runs in both live and replay.
	if (DISEmitter && DISEmitter->IsRunning() && AirspaceManager)
	{
		DISEmitter->EmitStates(AirspaceManager->GetAllAircraftStates(), SessionTime);
	}

	// Radar sees truth and produces tracks (what the operator gets to see).
	if (Radar && Radar->IsEnabled()) { Radar->Tick(DeltaTime); }

	UpdateFollowCamera();
}

void AClearanceSimulationController::TickGCIIntercepts(float DeltaTime)
{
	if (!AirspaceManager || ActiveIntercepts.Num() == 0) { return; }

	constexpr float JoinUpNm = 0.8f;
	constexpr float JoinUpAltFt = 1500.f;
	constexpr float FormationSideNm  = 2.0f;   // left/right wing slots
	constexpr float FormationTrailNm = 3.0f;   // trail slot directly behind
	// Settle thresholds: a wingman is "in slot" once it's close enough AND its heading
	// is close to the bandit's. Until then it FLIES toward the slot via the normal
	// behaviour (heading + StepPosition), so it banks and turns like a real aircraft. - TripleA
	constexpr float SettleDistNm = 0.4f;
	constexpr float SettleHeadingDeg = 25.f;

	// Group active intercepts by bandit so a flight (multiple fighters on one bandit)
	// joins up as a single coordinated escort. - TripleA
	TMap<FName, TArray<FName>> ByBandit;
	for (const TPair<FName, FName>& P : ActiveIntercepts)
	{
		ByBandit.FindOrAdd(P.Value).Add(P.Key);
	}

	TArray<FName> DeadFighters;
	for (TPair<FName, TArray<FName>>& Grp : ByBandit)
	{
		const FName BanditCs = Grp.Key;
		const FAircraftState BanditS0 = AirspaceManager->GetAircraftState(BanditCs);
		if (!BanditS0.bIsValid)
		{
			for (const FName& F : Grp.Value) { DeadFighters.Add(F); }
			continue;
		}

		// Was any fighter in the flight already joined BEFORE this tick?
		bool bWasJoined = false;
		for (const FName& Fc : Grp.Value)
		{
			if (JoinedIntercepts.Contains(Fc)) { bWasJoined = true; break; }
		}

		// Distance-based join check on the unjoined ones. Refresh the lead-pursuit
		// vector AND target altitude each tick before checking - a single VectorIntercept
		// call from SCRAMBLE goes stale the moment the bandit drifts off its original
		// course, and any vertical offset stops the join-up gate from firing. - TripleA
		bool bAnyNewJoin = false;
		for (const FName& Fc : Grp.Value)
		{
			if (JoinedIntercepts.Contains(Fc)) { continue; }
			const FAircraftState FS = AirspaceManager->GetAircraftState(Fc);
			if (!FS.bIsValid) { DeadFighters.Add(Fc); continue; }

			// Re-compute lead pursuit each tick so the heading keeps tracking.
			VectorIntercept(Fc, BanditCs);
			// Match the bandit's altitude so vertical separation closes during the run.
			FAircraftState FS2 = AirspaceManager->GetAircraftState(Fc);
			if (FS2.bIsValid)
			{
				FS2.TargetAltitude = BanditS0.Altitude;
				AirspaceManager->RequestStateUpdate(FS2);
			}

			const float Horiz = FVector2D::Distance(
				FVector2D(FS.Position.X, FS.Position.Y),
				FVector2D(BanditS0.Position.X, BanditS0.Position.Y));
			const float Vert = FMath::Abs(FS.Altitude - BanditS0.Altitude);
			if (Horiz <= JoinUpNm && Vert <= JoinUpAltFt)
			{
				JoinedIntercepts.Add(Fc);
				bAnyNewJoin = true;
			}
		}

		// FORMATION REJOIN: the first viper to arrive triggers the whole flight to join.
		// Without this, stragglers chase a bandit that just turned outward and miss.
		// Shadow ops on a hijack get the same outward escort - intercepting the
		// aircraft is the win condition either way. - TripleA
		if (bAnyNewJoin && !bWasJoined)
		{
			for (const FName& Fc : Grp.Value) { JoinedIntercepts.Add(Fc); }

			FAircraftState B = BanditS0;
			float OutHdg = FMath::RadiansToDegrees(FMath::Atan2(B.Position.X, B.Position.Y));
			if (OutHdg < 0.f) { OutHdg += 360.f; }
			B.TargetHeading = OutHdg;
			B.FlightPhase = EFlightPhase::Exiting;
			AirspaceManager->RequestStateUpdate(B);

			const bool bShadow = ShadowTargets.Contains(BanditCs);
			const TCHAR* Verb = bShadow ? TEXT("shadowing out") : TEXT("escorting out");
			if (Recorder) { Recorder->LogEvent(SessionTime, FString::Printf(TEXT("JOIN-UP %d-ship on %s, %s heading %.0f"),
				Grp.Value.Num(), *BanditCs.ToString(), Verb, OutHdg)); }
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan,
					FString::Printf(TEXT("JOIN-UP  %d-ship on %s  %s heading %.0f"),
						Grp.Value.Num(), *BanditCs.ToString(), Verb, OutHdg));
			}
		}

		// Anyone joined? If so, snap each joined fighter into its formation slot.
		bool bAnyJoined = false;
		for (const FName& Fc : Grp.Value)
		{
			if (JoinedIntercepts.Contains(Fc)) { bAnyJoined = true; break; }
		}
		if (!bAnyJoined) { continue; }

		// Slots, relative to bandit's heading frame:
		//   0 -> left wing  (side -1nm)        Bandit
		//   1 -> right wing (side +1nm)     0 [BANDIT] 1
		//   2 -> trail      (forward -1.5nm)        2
		const FAircraftState B = AirspaceManager->GetAircraftState(BanditCs);
		const float Hrad = FMath::DegreesToRadians(B.Heading);
		const FVector2D Fwd(FMath::Sin(Hrad), FMath::Cos(Hrad));
		const FVector2D Right(FMath::Cos(Hrad), -FMath::Sin(Hrad));
		const float SlotSide[3] = { -FormationSideNm, +FormationSideNm, 0.f };
		const float SlotFwd[3]  = {  0.f,              0.f,             -FormationTrailNm };
		// Vertical stagger during the rejoin so wingmen don't fly through each other or
		// the bandit. Hi-rejoin / lo-rejoin / trail-low. The offset smoothly merges to
		// zero as the fighter approaches its slot. - TripleA
		const float SlotRejoinAltFt[3] = { +1500.f, -1500.f, -500.f };

		int32 SlotIdx = 0;
		for (const FName& Fc : Grp.Value)
		{
			if (!JoinedIntercepts.Contains(Fc)) { continue; }
			if (SlotIdx >= 3) { break; }

			const FVector SlotPos(
				B.Position.X + Right.X * SlotSide[SlotIdx] + Fwd.X * SlotFwd[SlotIdx],
				B.Position.Y + Right.Y * SlotSide[SlotIdx] + Fwd.Y * SlotFwd[SlotIdx],
				0.f);

			FAircraftState FS = AirspaceManager->GetAircraftState(Fc);
			if (!FS.bIsValid) { DeadFighters.Add(Fc); ++SlotIdx; continue; }

			if (SettledInFormation.Contains(Fc))
			{
				// In slot - glue to the slot so the formation tracks the bandit.
				FS.Position = SlotPos;
				FS.Heading = B.Heading;
				FS.TargetHeading = B.Heading;
				FS.BankAngle = 0.f;
				FS.Altitude = B.Altitude;
				FS.Speed = B.Speed;
				FS.ClimbRate = 0.f;
				FS.FlightPhase = EFlightPhase::Exiting;
				AirspaceManager->RequestStateUpdate(FS);
			}
			else
			{
				// Not in slot yet - FLY there. Point the nose at the slot, let the normal
				// behaviour bank/turn/move forward. Match altitude over time too. - TripleA
				const FVector2D ToSlot(SlotPos.X - FS.Position.X, SlotPos.Y - FS.Position.Y);
				const float DistNm = ToSlot.Size();
				float HeadingToSlot = FMath::RadiansToDegrees(FMath::Atan2(ToSlot.X, ToSlot.Y));
				if (HeadingToSlot < 0.f) { HeadingToSlot += 360.f; }
				const float HdgErr = FMath::Abs(FMath::FindDeltaAngleDegrees(FS.Heading, B.Heading));

				if (DistNm <= SettleDistNm && HdgErr <= SettleHeadingDeg)
				{
					SettledInFormation.Add(Fc);
				}
				else
				{
					// Aim the nose at the slot; behaviour does the rest. Stagger altitude
					// on the way in so wingmen and the bandit never share airspace - it
					// merges back to zero as we close the last nautical mile. Hold combat
					// pursuit speed all the way in - only inside the last 0.6nm do we begin
					// to bleed off so we can settle alongside the bandit instead of shooting
					// past him. - TripleA
					const float MergeT   = FMath::Clamp((DistNm - 0.3f) / 0.9f, 0.f, 1.f);
					const float AltOff   = SlotRejoinAltFt[SlotIdx] * MergeT;
					const float SlowT    = FMath::Clamp((DistNm - 0.3f) / 0.3f, 0.f, 1.f);
					const float PursueKt = 640.f;
					const float SlotKt   = FMath::Max(B.Speed, 200.f);

					FS.TargetHeading  = HeadingToSlot;
					FS.TargetAltitude = B.Altitude + AltOff;
					FS.TargetSpeed    = FMath::Lerp(SlotKt, PursueKt, SlowT);
					FS.FlightPhase    = EFlightPhase::Exiting;
					AirspaceManager->RequestStateUpdate(FS);
				}
			}
			++SlotIdx;
		}
	}

	for (const FName& K : DeadFighters)
	{
		ActiveIntercepts.Remove(K);
		JoinedIntercepts.Remove(K);
		SettledInFormation.Remove(K);
	}
}

void AClearanceSimulationController::SetGCIModeEnabled(bool bInEnabled)
{
	bGCIMode = bInEnabled;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan,
			bInEnabled ? TEXT("GCI / Air Defence mode ON - classify contacts, interrogate IFF, vector intercepts")
			           : TEXT("GCI / Air Defence mode OFF"));
	}
}

void AClearanceSimulationController::ClassifyAircraft(FName Callsign, EThreatClass NewClass)
{
	if (!AirspaceManager) { return; }
	FAircraftState S = AirspaceManager->GetAircraftState(Callsign);
	if (!S.bIsValid) { return; }

	// CATASTROPHIC DOCTRINE FAILURE: declaring a confirmed civilian (IFF on AND
	// non-military) as hostile. Vincennes / KAL-007 territory. Mark the incident,
	// hit the score, lock further scrambles for the session. The classification
	// still goes through - the player committed to it - but the consequences are
	// permanent and unmistakable. - TripleA
	if (NewClass == EThreatClass::Hostile && S.bIFFOperational && !S.bIsMilitary &&
		S.ThreatClass != EThreatClass::Hostile)
	{
		if (Scoring)
		{
			Scoring->LogIncident(EIncidentType::MisidentifiedCivilian, Callsign, NAME_None,
				FString::Printf(TEXT("Civilian %s with active IFF declared HOSTILE"), *Callsign.ToString()));
		}
		if (Recorder)
		{
			Recorder->LogEvent(SessionTime, FString::Printf(
				TEXT("MISIDENTIFICATION - %s declared HOSTILE despite active IFF / civilian airframe"),
				*Callsign.ToString()));
		}
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(101, 30.f, FColor::Red,
				FString::Printf(TEXT("*** MISIDENTIFICATION *** %s WAS CIVILIAN (-%d)"),
					*Callsign.ToString(), Scoring ? Scoring->PenaltyMisidentifiedCivilian : 1000));
		}
	}

	S.ThreatClass = NewClass;
	// Hostile contacts lock out of civilian ATC immediately.
	S.bUnderGCIControl = (NewClass == EThreatClass::Hostile);
	AirspaceManager->RequestStateUpdate(S);
	if (GEngine)
	{
		const TCHAR* L = NewClass == EThreatClass::Friendly ? TEXT("FRIENDLY")
			: NewClass == EThreatClass::Hostile ? TEXT("HOSTILE")
			: NewClass == EThreatClass::Neutral ? TEXT("NEUTRAL") : TEXT("UNKNOWN");
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan,
			FString::Printf(TEXT("CLASSIFY  %s -> %s"), *Callsign.ToString(), L));
	}
}

bool AClearanceSimulationController::InterrogateIFF(FName Callsign, EThreatClass& OutClass, int32& OutSquawk)
{
	OutClass = EThreatClass::Unknown;
	OutSquawk = 0;
	if (!AirspaceManager) { return false; }
	const FAircraftState S = AirspaceManager->GetAircraftState(Callsign);
	if (!S.bIsValid) { return false; }

	// A hostile contact may have its IFF off; interrogation returns nothing. - TripleA
	if (!S.bIFFOperational)
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red,
			FString::Printf(TEXT("IFF %s: NO RESPONSE"), *Callsign.ToString())); }
		return false;
	}
	OutClass = S.ThreatClass;
	OutSquawk = S.SquawkCode;
	if (GEngine)
	{
		const TCHAR* L = OutClass == EThreatClass::Friendly ? TEXT("FRIENDLY")
			: OutClass == EThreatClass::Hostile ? TEXT("HOSTILE")
			: OutClass == EThreatClass::Neutral ? TEXT("NEUTRAL") : TEXT("UNKNOWN");
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green,
			FString::Printf(TEXT("IFF %s: squawk %04d  %s"), *Callsign.ToString(), OutSquawk, L));
	}
	return true;
}

bool AClearanceSimulationController::VectorIntercept(FName FighterCallsign, FName TargetCallsign)
{
	if (!AirspaceManager) { return false; }
	const FAircraftState F = AirspaceManager->GetAircraftState(FighterCallsign);
	const FAircraftState T = AirspaceManager->GetAircraftState(TargetCallsign);
	if (!F.bIsValid || !T.bIsValid) { return false; }

	// Lead-pursuit intercept: find time t with |Pt + Vt*t - Pf| = Sf*t. Quadratic in t.
	// at^2 + bt + c = 0 where a = |Vt|^2 - Sf^2, b = 2(Pt-Pf).Vt, c = |Pt-Pf|^2. - TripleA
	const FVector2D Pf(F.Position.X, F.Position.Y);
	const FVector2D Pt(T.Position.X, T.Position.Y);
	const FVector2D Vt(T.Velocity.X, T.Velocity.Y); // nm/s (already populated by StepPosition)
	const float Sf = F.Speed / 3600.f;              // fighter speed in nm/s

	const FVector2D Diff = Pt - Pf;
	const float a = Vt.SizeSquared() - Sf * Sf;
	const float b = 2.f * FVector2D::DotProduct(Diff, Vt);
	const float c = Diff.SizeSquared();

	float TimeToIntercept = -1.f;
	if (FMath::Abs(a) < KINDA_SMALL_NUMBER)
	{
		// Same speed - linear: bt + c = 0.
		if (FMath::Abs(b) > KINDA_SMALL_NUMBER) { TimeToIntercept = -c / b; }
	}
	else
	{
		const float Disc = b * b - 4.f * a * c;
		if (Disc >= 0.f)
		{
			const float Sqrt = FMath::Sqrt(Disc);
			const float T1 = (-b - Sqrt) / (2.f * a);
			const float T2 = (-b + Sqrt) / (2.f * a);
			// Smallest positive root.
			TimeToIntercept = (T1 > 0.f) ? T1 : T2;
		}
	}
	if (TimeToIntercept <= 0.f)
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red,
			FString::Printf(TEXT("INTERCEPT %s -> %s: no solution (too slow)"),
				*FighterCallsign.ToString(), *TargetCallsign.ToString())); }
		return false;
	}

	const FVector2D InterceptPt = Pt + Vt * TimeToIntercept;
	const FVector2D ToIntercept = InterceptPt - Pf;
	float HeadingDeg = FMath::RadiansToDegrees(FMath::Atan2(ToIntercept.X, ToIntercept.Y));
	if (HeadingDeg < 0.f) { HeadingDeg += 360.f; }

	// The fighter goes under GCI control too - civilian ATC can't redirect it now.
	// Issue the heading change directly via the manager (bypass the ATC router which
	// would reject GCI-controlled aircraft). Push to combat speed so we actually
	// close on the target - the formation tick slows them back down at the slot. - TripleA
	FAircraftState F2 = F;
	F2.TargetHeading = HeadingDeg;
	F2.TargetSpeed = FMath::Max(F2.TargetSpeed, 1050.f);
	F2.bUnderGCIControl = true;
	AirspaceManager->RequestStateUpdate(F2);

	// Only announce on the first call - this gets called every tick to refresh the
	// vector for unjoined fighters, and the on-screen message would flood. - TripleA
	const bool bFirstTime = !ActiveIntercepts.Contains(FighterCallsign);
	ActiveIntercepts.Add(FighterCallsign, TargetCallsign);

	if (bFirstTime && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan,
			FString::Printf(TEXT("INTERCEPT %s -> %s  vector %03.0f  ETA %.0fs"),
				*FighterCallsign.ToString(), *TargetCallsign.ToString(), HeadingDeg, TimeToIntercept));
	}
	return true;
}

void AClearanceSimulationController::BeginCrash(const FAircraftState& InState, const FString& Reason)
{
	if (!AirspaceManager || InState.bCrashing) { return; }
	FAircraftState S = InState;
	S.bCrashing = true;
	AirspaceManager->RequestStateUpdate(S);
	PendingCrashReasons.Add(S.Callsign, Reason);

	// Cockpit panic from the pilot's voice. Real CVRs go through three arcs:
	// initial alarm ("we're losing it"), denial/fighting ("pull up, climb"),
	// then acceptance / last words ("tell them I love them"). We fire an
	// initial line now and schedule a final/acceptance line ~15s real later -
	// the half-duplex queue handles spacing. Same voice for both so it sounds
	// like one pilot, not two. - TripleA
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AClearanceVoiceOutput> VoIt(W); VoIt; ++VoIt)
		{
			if (!*VoIt) { break; }

			static const TCHAR* PanicLines[] = {
				TEXT("Oh God, oh God, oh God. We're going down. We're losing her. We're losing her."),
				TEXT("Oh shit, oh shit, oh shit. We're out of control. I can't hold it. I can't hold it."),
				TEXT("Mayday, mayday, mayday. We're going down. Pull up. Pull up. Oh God."),
				TEXT("She's not responding. She's not responding. We're going in. We're going in."),
				TEXT("Climb! Why won't she climb! We're going to die. We're going to die."),
				TEXT("I have control. I have control. No, I can't. I can't hold her."),
				TEXT("Terrain. Terrain. Pull up. Pull up. Oh no no no."),
				TEXT("Captain. Captain. We're going to crash. We're going to crash."),
			};
			// Lock the voice for both lines so it's clearly the same pilot.
			const FString Voice = VoIt->PickVoiceForCallsign(S.Callsign);
			const int32 P = FMath::RandRange(0, UE_ARRAY_COUNT(PanicLines) - 1);
			VoIt->SpeakPanic(S.Callsign, PanicLines[P], Voice);

			// GPWS cockpit alarm runs UNDER the pilot's voice, in parallel - bypasses
			// the half-duplex queue. Mirrors a real CVR where you hear the airframe
			// shouting "TERRAIN, PULL UP" while the crew panics. Tracked by callsign
			// so we can stop it on impact. - TripleA
			VoIt->PlayGPWS(S.Callsign);

			TWeakObjectPtr<AClearanceVoiceOutput> WeakVO(*VoIt);
			const FName Cs = S.Callsign;
			FTimerHandle& Handle = PendingPanicTimers.FindOrAdd(Cs);
			W->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(
				[WeakVO, Cs, Voice]()
				{
					if (!WeakVO.IsValid()) { return; }
					static const TCHAR* FinalLines[] = {
						TEXT("Goodbye. Goodbye. I love you. I love you all."),
						TEXT("I love you. Tell my family I love them. Tell them I love them."),
						TEXT("I'm sorry. I'm sorry. I tried. I tried."),
						TEXT("Mama. Mama. I'm sorry. I love you, mama."),
						TEXT("Honey. Honey, I love you. Tell the kids I love them."),
						TEXT("God forgive me. God forgive us all."),
						TEXT("It's over. It's over. I love you."),
						TEXT("Brace. Brace. Brace for impact."),
					};
					const int32 Fi = FMath::RandRange(0, UE_ARRAY_COUNT(FinalLines) - 1);
					WeakVO->SpeakPanic(Cs, FinalLines[Fi], Voice);
				}), 14.f, false);

			break;
		}
	}
}

void AClearanceSimulationController::TickCrashingAircraft(float DeltaTime)
{
	if (!AirspaceManager || DeltaTime <= 0.f) { return; }
	const float SimDt = DeltaTime * FMath::Max(0.f, SimulationTimeScale);

	const TArray<FAircraftState> Snap = AirspaceManager->GetAllAircraftStates();
	for (const FAircraftState& Ro : Snap)
	{
		if (!Ro.bCrashing) { continue; }

		FAircraftState S = Ro;

		// Values are SIM ft/min - multiplied by SimulationTimeScale for the visual.
		// At default 10x: 1500 sim = 15,000 ft/min real = 250 ft/sec real visual.
		// From FL150 that's ~60 real seconds to ground - watchable, dramatic, not
		// instant. Pitch is forced -55deg in the visual layer separately. - TripleA
		constexpr float TerminalDescentFtMin = 1500.f;
		constexpr float DescentAccelFtMinPerSec = 80.f;     // ramp up over ~20 sim sec
		constexpr float ForwardDecayKtsPerSec = 0.2f;

		const float CurDescent = FMath::Max(0.f, -S.ClimbRate);
		const float NewDescent = FMath::Min(TerminalDescentFtMin, CurDescent + DescentAccelFtMinPerSec * SimDt);
		S.ClimbRate = -NewDescent;
		S.Altitude  = FMath::Max(0.f, S.Altitude - (NewDescent / 60.f) * SimDt);

		// No spiral, no wobble - the forced -55deg pitch in the visual layer makes
		// the dive obvious. Aircraft holds its current heading and flies forward
		// into the ground. - TripleA
		S.BankAngle = 0.f;
		S.Speed     = FMath::Max(60.f, S.Speed - ForwardDecayKtsPerSec * SimDt);

		// Move forward along current heading at current speed (nm/s = kt/3600).
		const float HRad = FMath::DegreesToRadians(S.Heading);
		const float SpeedNmPerSec = S.Speed / 3600.f;
		const FVector2D Step(FMath::Sin(HRad) * SpeedNmPerSec * SimDt,
		                     FMath::Cos(HRad) * SpeedNmPerSec * SimDt);
		S.Position.X += Step.X;
		S.Position.Y += Step.Y;
		S.Velocity   = FVector(FMath::Sin(HRad) * SpeedNmPerSec, FMath::Cos(HRad) * SpeedNmPerSec, 0.f);

		S.TargetAltitude = S.Altitude;
		S.TargetHeading  = S.Heading;
		S.TargetSpeed    = S.Speed;

		if (S.Altitude <= 0.f)
		{
			S.Altitude = 0.f;
			AirspaceManager->RequestStateUpdate(S);
			const FString Reason = PendingCrashReasons.Contains(S.Callsign)
				? PendingCrashReasons[S.Callsign] : TEXT("Crash");
			PendingCrashReasons.Remove(S.Callsign);
			CrashAircraft(S, Reason);
		}
		else
		{
			AirspaceManager->RequestStateUpdate(S);
		}
	}
}

void AClearanceSimulationController::CrashAircraft(const FAircraftState& S, const FString& Reason)
{
	// Cut the cockpit alarm and any pending pilot lines - the wreck is silent
	// from this moment forward. - TripleA
	if (UWorld* W = GetWorld())
	{
		if (FTimerHandle* Pending = PendingPanicTimers.Find(S.Callsign))
		{
			W->GetTimerManager().ClearTimer(*Pending);
			PendingPanicTimers.Remove(S.Callsign);
		}
		for (TActorIterator<AClearanceVoiceOutput> VoIt(W); VoIt; ++VoIt)
		{
			if (*VoIt) { VoIt->StopGPWS(S.Callsign); }
			break;
		}
	}

	if (Scoring)
	{
		Scoring->LogIncident(EIncidentType::AircraftCrashed, S.Callsign, NAME_None, Reason);
	}
	if (Recorder)
	{
		Recorder->LogEvent(SessionTime, FString::Printf(TEXT("CRASH - %s (%s)"), *S.Callsign.ToString(), *Reason));
	}
	FCrashSite Site;
	Site.PositionNm = S.Position;
	Site.SessionSeconds = SessionTime;
	Site.Callsign = S.Callsign;
	CrashSites.Add(Site);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Red,
			FString::Printf(TEXT("*** CRASH *** %s - %s (-%d)"),
				*S.Callsign.ToString(), *Reason,
				Scoring ? Scoring->PenaltyAircraftCrashed : 500));
	}

	// Controller's deadpan "Lost contact" on the en_US voice, ~1.5s after impact
	// so the listener hears the silence first. - TripleA
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AClearanceVoiceOutput> VoIt(W); VoIt; ++VoIt)
		{
			if (!*VoIt) { break; }
			const FName Cs = S.Callsign;
			TWeakObjectPtr<AClearanceVoiceOutput> WeakVO(*VoIt);
			FTimerHandle Handle;
			W->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(
				[WeakVO, Cs]()
				{
					if (WeakVO.IsValid())
					{
						WeakVO->Speak(NAME_None,
							FString::Printf(TEXT("Lost contact, %s"), *Cs.ToString()),
							TEXT("en-US-EricNeural"));  // deadpan controller voice
					}
				}), 1.5f, false);
			break;
		}
	}

	if (AirspaceManager) { AirspaceManager->DeregisterAircraft(S.Callsign); }
}

int32 AClearanceSimulationController::ShadowEscort(FName HijackCallsign)
{
	if (!AirspaceManager) { return 0; }
	const FAircraftState Hijack = AirspaceManager->GetAircraftState(HijackCallsign);
	if (!Hijack.bIsValid) { return 0; }

	// Doctrine: SHADOW is only authorised against a 7500 squawk. Without that
	// signal there's no justification for launching - that's what SCRAMBLE is
	// for. Refuse loudly so the operator doesn't shadow normal traffic. - TripleA
	if (Hijack.ActiveEmergency != EEmergencyType::Hijack)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
				FString::Printf(TEXT("SHADOW refused: %s not squawking 7500"), *HijackCallsign.ToString()));
		}
		return 0;
	}

	ShadowTargets.Add(HijackCallsign);

	const float R = FMath::Max(10.f, ExitRadiusNm);
	const float Bearing = FMath::Fmod(
		FMath::RadiansToDegrees(FMath::Atan2(Hijack.Position.X, Hijack.Position.Y)) + 360.f, 360.f);
	const float FanDeg = 8.f;
	int32 Launched = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		const float ADeg = FMath::Fmod(Bearing + (i - 1) * (FanDeg * 0.5f) + 360.f, 360.f);
		const float ARad = FMath::DegreesToRadians(ADeg);
		const FVector Pos(R * FMath::Sin(ARad), R * FMath::Cos(ARad), 0.f);

		const float Inbound = FMath::RadiansToDegrees(FMath::Atan2(Hijack.Position.X - Pos.X, Hijack.Position.Y - Pos.Y));
		const float Hdg = FMath::Fmod(Inbound + 360.f, 360.f);

		const int32 Num = NextViperNumber++;
		FAircraftState V;
		V.Callsign         = FName(*FString::Printf(TEXT("VIPER%02d"), Num));
		V.Position         = Pos;
		V.Altitude         = Hijack.Altitude;
		V.Heading          = Hdg;
		V.Speed            = 620.f;
		V.WakeCategory     = EWakeCategory::Medium;
		V.FlightPhase      = EFlightPhase::Enroute;
		V.ThreatClass      = EThreatClass::Friendly;
		V.SquawkCode       = 2200 + Num;
		V.bIFFOperational  = true;
		V.bIsMilitary      = true;
		if (!AirspaceManager->RegisterAircraft(V)) { continue; }
		if (VectorIntercept(V.Callsign, HijackCallsign)) { ++Launched; }
	}
	return Launched;
}

int32 AClearanceSimulationController::ScrambleInterceptors(FName BanditCallsign)
{
	if (!AirspaceManager) { return 0; }
	const FAircraftState Bandit = AirspaceManager->GetAircraftState(BanditCallsign);
	if (!Bandit.bIsValid) { return 0; }

	// SCRAMBLE requires a positively-identified hostile - the operator has to DECLARE
	// the target hostile first. This is the doctrine guardrail that stops fighters
	// being launched on civilian traffic; mis-classification has to be the operator's
	// own call, not laundered away by the scramble step. - TripleA
	if (Bandit.ThreatClass != EThreatClass::Hostile)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red,
				FString::Printf(TEXT("SCRAMBLE refused: %s not declared HOSTILE"), *BanditCallsign.ToString()));
		}
		return 0;
	}

	// Spawn all 3 from the stretch of boundary CLOSEST to the bandit, spread in a
	// tight angular fan. Random angles around the whole ring put fighters on
	// opposite sides of the sector - by the time #1 arrives, #2 and #3 are still
	// crossing the whole map and the engagement breaks down. - TripleA
	const float R = FMath::Max(10.f, ExitRadiusNm);
	const float BanditBearingDeg = FMath::Fmod(
		FMath::RadiansToDegrees(FMath::Atan2(Bandit.Position.X, Bandit.Position.Y)) + 360.f, 360.f);
	const float FanDeg = 8.f; // total angular spread across the 3-ship at the boundary
	int32 Launched = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		const float ADeg = FMath::Fmod(BanditBearingDeg + (i - 1) * (FanDeg * 0.5f) + 360.f, 360.f);
		const float ARad = FMath::DegreesToRadians(ADeg);
		const FVector Pos(R * FMath::Sin(ARad), R * FMath::Cos(ARad), 0.f);

		const float Inbound = FMath::RadiansToDegrees(FMath::Atan2(Bandit.Position.X - Pos.X, Bandit.Position.Y - Pos.Y));
		const float Hdg = FMath::Fmod(Inbound + 360.f, 360.f);

		const int32 Num = NextViperNumber++;
		FAircraftState V;
		V.Callsign         = FName(*FString::Printf(TEXT("VIPER%02d"), Num));
		V.Position         = Pos;
		V.Altitude         = Bandit.Altitude; // match co-altitude on arrival so vertical join-up actually fires
		V.Heading          = Hdg;
		V.Speed            = 620.f;
		V.WakeCategory     = EWakeCategory::Medium;
		V.FlightPhase      = EFlightPhase::Enroute;
		V.ThreatClass      = EThreatClass::Friendly;
		V.SquawkCode       = 2200 + Num;
		V.bIFFOperational  = true;
		V.bIsMilitary      = true;
		if (!AirspaceManager->RegisterAircraft(V)) { continue; }
		if (VectorIntercept(V.Callsign, BanditCallsign)) { ++Launched; }
	}
	return Launched;
}

void AClearanceSimulationController::SetRadarEnabled(bool bInEnabled)
{
	if (!Radar) { return; }
	Radar->SetEnabled(bInEnabled);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan,
			bInEnabled ? TEXT("Radar ON - sensor view (range, sweep, primary/secondary, fade)")
			           : TEXT("Radar OFF - god's-eye truth view"));
	}
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

	TickGCIIntercepts(DeltaTime);                                  // join-up + escort
	TickCrashingAircraft(DeltaTime);                               // drop falling aircraft to the ground
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

	// The 3D world always shows real aircraft moving smoothly - they're physical things.
	// The radar is a separate observer that produces tracks for an operator's scope (a
	// future 2D HUD overlay); it never deforms the actual airframe visuals. - TripleA
	for (const FAircraftState& State : AirspaceManager->GetAllAircraftStates())
	{
		const FSpawnedAircraftVisual* Found = VisualActors.Find(State.Callsign);
		if (!Found || !Found->Actor)
		{
			continue;
		}

		// External (federated) aircraft only refresh on incoming packets - any small
		// snap between dead-reckoned and fresh truth would read as a jitter. Smooth
		// the visual position toward truth on REAL frame time so the jolt becomes a
		// half-second slide instead. Local aircraft skip this - their state updates
		// every tick from physics, so a direct write looks correct. - TripleA
		const FVector TargetPos = WorldPositionFor(State);
		if (State.bIsExternal)
		{
			const float RealDt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
			const FVector Smoothed = FMath::VInterpTo(Found->Actor->GetActorLocation(), TargetPos, RealDt, 6.f);
			Found->Actor->SetActorLocation(Smoothed);
		}
		else
		{
			Found->Actor->SetActorLocation(TargetPos);
		}

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
		if (State.bCrashing)
		{
			// Forced steep nose-down attitude regardless of the FPA calculation -
			// derived FPA can read shallow at this sim's scaling and the operator
			// just sees the aircraft glide level. -55deg is unmistakeably "diving
			// into the ground". - TripleA
			PitchDeg = -55.f;
		}
		else if (State.Altitude <= 12.f)
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
		// without touching the flight model. Higher = quicker, stiffer. Military jets
		// roll about twice as fast as an airliner. - TripleA
		const float RealDt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
		const float RollRate = State.bIsMilitary ? 9.f : 4.f;
		const FQuat Smoothed = FMath::QInterpTo(Found->Actor->GetActorQuat(), TargetRot, RealDt, RollRate);
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

	// Crash sites - red ring + label on the ground, persistent for the session.
	// Reads as "something bad happened HERE" without being a particle effect. - TripleA
	for (const FCrashSite& Cr : CrashSites)
	{
		const FVector CrW(Origin.X + Cr.PositionNm.X * S, Origin.Y + Cr.PositionNm.Y * S, GroundWorldZ);
		DrawDebugCircle(World, CrW, 0.5f * S, 32, FColor(200, 30, 30), false, -1.f, 0, 60.f, FVector(1,0,0), FVector(0,1,0), false);
		DrawDebugCircle(World, CrW, 0.25f * S, 24, FColor(120, 20, 20), false, -1.f, 0, 60.f, FVector(1,0,0), FVector(0,1,0), false);
		DrawDebugString(World, CrW + FVector(0, 0, 0.8f * S),
			FString::Printf(TEXT("WRECK %s"), *Cr.Callsign.ToString()), nullptr, FColor(220, 60, 60), 0.f, true, 1.0f);
	}

	// Protected violation zones - pulsing red circles on the ground. Stand out
	// without being obnoxious, the same pulse cadence as the active runway. - TripleA
	{
		const float RWTv = World->GetRealTimeSeconds();
		const float Pv = 0.65f + 0.35f * (0.5f + 0.5f * FMath::Sin(RWTv * 3.f));
		const FColor ZoneCol(
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(255 * Pv), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt( 60 * Pv), 0, 255)),
			static_cast<uint8>(FMath::Clamp(FMath::RoundToInt( 60 * Pv), 0, 255)));
		for (TActorIterator<AClearanceViolationZone> ZIt(World); ZIt; ++ZIt)
		{
			if (!*ZIt) { continue; }
			const FVector C(ZIt->GetActorLocation().X, ZIt->GetActorLocation().Y, GroundWorldZ);
			DrawDebugCircle(World, C, ZIt->RadiusNm * S, 48, ZoneCol, false, -1.f, 0, 120.f, FVector(1,0,0), FVector(0,1,0), false);
			DrawDebugString(World, C + FVector(0, 0, 1.5f * S),
				FString::Printf(TEXT("[%s]"), *ZIt->ZoneName.ToString()), nullptr, ZoneCol, 0.f, true, 1.3f);
		}
	}

	// Restricted airspace - amber/blue rings (don't pulse - this is just
	// an obstacle the controller has to vector around, not a critical threat). - TripleA
	{
		const FColor AreaCol(70, 130, 200);
		const FColor AreaLabel(140, 180, 230);
		for (TActorIterator<AClearanceRestrictedArea> AIt(World); AIt; ++AIt)
		{
			if (!*AIt) { continue; }
			const FVector C(AIt->GetActorLocation().X, AIt->GetActorLocation().Y, GroundWorldZ);
			DrawDebugCircle(World, C, AIt->RadiusNm * S, 48, AreaCol, false, -1.f, 0, 80.f, FVector(1,0,0), FVector(0,1,0), false);
			DrawDebugCircle(World, C, AIt->RadiusNm * S * 0.95f, 48, AreaCol, false, -1.f, 0, 40.f, FVector(1,0,0), FVector(0,1,0), false);
			DrawDebugString(World, C + FVector(0, 0, 1.2f * S),
				FString::Printf(TEXT("[%s]"), *AIt->AreaName.ToString()), nullptr, AreaLabel, 0.f, true, 1.2f);
		}
	}

	// Radar coverage rings - one circle per placed RadarSite, sized by its
	// configured RangeNm. Per-site colour so the operator can tell which sensor
	// covers which patch. Centred on the actor's world location. - TripleA
	for (TActorIterator<AClearanceRadarSite> SIt(World); SIt; ++SIt)
	{
		if (!*SIt || !SIt->Radar) { continue; }
		const FVector C(SIt->GetActorLocation().X, SIt->GetActorLocation().Y, GroundWorldZ);
		const float Rad = SIt->Radar->RangeNm * S;
		DrawDebugCircle(World, C, Rad, 64, SIt->CoverageColour, false, -1.f, 0, 60.f, FVector(1,0,0), FVector(0,1,0), false);
		DrawDebugString(World, C + FVector(0, 0, 1.f * S),
			FString::Printf(TEXT("RDR %s"), *SIt->SiteName.ToString()), nullptr, SIt->CoverageColour, 0.f, true, 1.1f);
	}

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

	// Approach corridor + glidepath for EVERY runway threshold the manager knows
	// about, not just the wind-active one. The active runway draws white; the others
	// draw dimmer so the operator can still see they exist. - TripleA
	TArray<FRunwayInfo> AllRunways = AirspaceManager->GetAllRunways();
	if (AllRunways.Num() == 0)
	{
		// Fallback so we still draw something sensible if no runway actors registered.
		FRunwayInfo Fallback;
		Fallback.HeadingDeg = (Env.ActiveRunwayHeading >= 0.f) ? Env.ActiveRunwayHeading : 270.f;
		Fallback.ThresholdNm = FVector2D(Env.ActiveRunwayThreshold.X, Env.ActiveRunwayThreshold.Y);
		AllRunways.Add(Fallback);
	}

	// Pre-resolve each runway's actual half-width so the capture funnel's narrow end
	// matches the strip it leads onto, instead of a hard-coded 0.3nm slot. Key by
	// rounded heading (10 deg buckets, both directions of each strip) so the lookup
	// works against the FRunwayInfo loop below. - TripleA
	TMap<int32, float> WidthByHeading;
	for (TActorIterator<AClearanceRunway> WIt(World); WIt; ++WIt)
	{
		FVector MC, ME;
		if (!WIt->GetRunwayBounds(MC, ME)) { continue; }
		const float HRad = FMath::DegreesToRadians(WIt->LandingHeadingDeg);
		const FVector SideV(FMath::Cos(HRad), -FMath::Sin(HRad), 0.f);
		const float HW = ME.X * FMath::Abs(SideV.X) + ME.Y * FMath::Abs(SideV.Y);
		const int32 H1 = (FMath::RoundToInt(WIt->LandingHeadingDeg / 10.f) % 36) * 10;
		const int32 H2 = (FMath::RoundToInt(FMath::Fmod(WIt->LandingHeadingDeg + 180.f, 360.f) / 10.f) % 36) * 10;
		WidthByHeading.Add(H1, HW);
		WidthByHeading.Add(H2, HW);
	}

	// Slow real-time pulse on the active runway's markings so the operator can find
	// the in-use threshold at a glance. - TripleA
	const float RWT2 = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
	const float Pulse2 = 0.65f + 0.35f * (0.5f + 0.5f * FMath::Sin(RWT2 * 4.f));

	for (const FRunwayInfo& Rwy : AllRunways)
	{
		const bool bActive = (Env.ActiveRunwayHeading >= 0.f) && FMath::IsNearlyEqual(Rwy.HeadingDeg, Env.ActiveRunwayHeading, 0.5f);

		// Active end gets warm amber pulsing - same family as the runway box. Inactive
		// ends sit in a cool slate so they read as available but not in use. - TripleA
		auto Scale = [&](int32 R, int32 G, int32 B, float K) -> FColor
		{
			return FColor(
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(R * K), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(G * K), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(B * K), 0, 255)));
		};
		const FColor LineCol = bActive ? Scale(255, 200, 80, Pulse2) : FColor(110, 130, 150);
		const FColor ConeCol = bActive ? Scale(255, 175, 60, Pulse2) : FColor(60, 75, 95);

		const float Fac = Rwy.HeadingDeg;
		const float FacRad = FMath::DegreesToRadians(Fac);
		const FVector InboundDir(FMath::Sin(FacRad), FMath::Cos(FacRad), 0.f); // landing direction
		const FVector RightDir(FMath::Cos(FacRad), -FMath::Sin(FacRad), 0.f);  // perpendicular
		const float CorridorLen = ClearanceConstants::ApproachCorridorLengthNm;
		const FVector Thr(Origin.X + Rwy.ThresholdNm.X * S, Origin.Y + Rwy.ThresholdNm.Y * S, GroundWorldZ);

		// Capture funnel/cone: the narrow end now matches the runway's own width, so
		// the cone visibly leads onto the actual strip. - TripleA
		const float WMax = ClearanceConstants::ApproachCorridorHalfWidthNm * S; // far half-width
		const int32 HKey = (FMath::RoundToInt(Rwy.HeadingDeg / 10.f) % 36) * 10;
		const float* FoundHW = WidthByHeading.Find(HKey);
		const float WMin = FoundHW ? *FoundHW : 0.3f * S;                       // half-width at the orb (= runway half-width)
		const float MouthZ = AltitudeToWorldZOffset(CorridorLen * 318.f);
		const FVector Up(0.f, 0.f, MouthZ);
		const FVector FarC = Thr - InboundDir * (CorridorLen * S);

		const FVector FBL = FarC + RightDir * WMax;
		const FVector FBR = FarC - RightDir * WMax;
		const FVector FTL = FBL + Up;
		const FVector FTR = FBR + Up;

		// Glidepath centreline - solid, thin, curves up from threshold along the 3deg
		// slope. With short perpendicular ticks every 10nm acting as range marks so
		// the line has structure instead of floating empty. - TripleA
		{
			const int32 Segs = 60;
			const float LineThick = bActive ? 80.f : 40.f;
			FVector Prev = Thr;
			for (int32 i = 1; i <= Segs; ++i)
			{
				const float DistNm = (CorridorLen * i) / Segs;
				const FVector Pt = Thr - InboundDir * (DistNm * S) + FVector(0.f, 0.f, AltitudeToWorldZOffset(DistNm * 318.f));
				DrawDebugLine(World, Prev, Pt, LineCol, false, -1.f, 0, LineThick);
				Prev = Pt;
			}
			// 10-nm range marks along the glidepath.
			const float Step = 10.f;
			for (float D = Step; D < CorridorLen; D += Step)
			{
				const FVector Pt = Thr - InboundDir * (D * S) + FVector(0.f, 0.f, AltitudeToWorldZOffset(D * 318.f));
				const float TickHalf = FMath::Max(WMin * 0.25f, 200.f);
				DrawDebugLine(World, Pt + RightDir * TickHalf, Pt - RightDir * TickHalf, LineCol, false, -1.f, 0, LineThick);
				DrawDebugString(World, Pt + FVector(0, 0, 0.6f * S),
					FString::Printf(TEXT("%.0fnm"), D), nullptr, LineCol, 0.f, true, 0.9f);
			}
		}

		// Capture funnel as a FLAT GROUND FAN - keeps the runway-extension shape but
		// drops the 3D wireframe walls that read as a debug widget. Just the strip,
		// the two diverging ground edges out to the corridor mouth, and the mouth
		// itself. - TripleA
		const float ConeThick = bActive ? 50.f : 30.f;
		DrawDebugLine(World, Thr + RightDir * WMin, FBL, ConeCol, false, -1.f, 0, ConeThick);
		DrawDebugLine(World, Thr - RightDir * WMin, FBR, ConeCol, false, -1.f, 0, ConeThick);
		DrawDebugLine(World, FBL, FBR, ConeCol, false, -1.f, 0, ConeThick);

		// Thin cross-bars at 10/20/30/40nm so the fan has scale - reads like sector
		// chart range rings instead of an empty triangle. - TripleA
		for (float D = 10.f; D < CorridorLen; D += 10.f)
		{
			const float T = D / CorridorLen;
			const float WHere = FMath::Lerp(WMin, WMax, T);
			const FVector Mid = Thr - InboundDir * (D * S);
			DrawDebugLine(World, Mid + RightDir * WHere, Mid - RightDir * WHere, ConeCol, false, -1.f, 0, FMath::Max(ConeThick * 0.5f, 25.f));
		}
	}

	// Resolve "RWY 09L / 09R / 09C" labels: every runway endpoint that shares a
	// designator with another gets a left/right/centre suffix sorted from the
	// pilot's left to right looking down the landing direction. - TripleA
	auto Designator = [](float Hdg)
	{
		int32 N = FMath::RoundToInt(Hdg / 10.f) % 36;
		return (N == 0) ? 36 : N;
	};

	struct FRwyEnd
	{
		AClearanceRunway* Actor;
		float HeadingDeg;
		FVector Centre;
		int32 Number;
		bool bIsReciprocal;
	};
	TArray<FRwyEnd> Ends;
	for (TActorIterator<AClearanceRunway> EIt(World); EIt; ++EIt)
	{
		FVector C = EIt->GetActorLocation();
		FVector MC, ME;
		if (EIt->GetRunwayBounds(MC, ME)) { C = MC; }
		Ends.Add({ *EIt, EIt->LandingHeadingDeg, C, Designator(EIt->LandingHeadingDeg), false });
		if (EIt->bAllowReciprocal)
		{
			const float H2 = FMath::Fmod(EIt->LandingHeadingDeg + 180.f, 360.f);
			Ends.Add({ *EIt, H2, C, Designator(H2), true });
		}
	}

	TMap<int32, TArray<int32>> ByNumber;
	for (int32 i = 0; i < Ends.Num(); ++i) { ByNumber.FindOrAdd(Ends[i].Number).Add(i); }

	TArray<FString> Suffix; Suffix.SetNum(Ends.Num());
	for (auto& Pair : ByNumber)
	{
		TArray<int32>& Grp = Pair.Value;
		if (Grp.Num() < 2) { continue; }
		const float HRad = FMath::DegreesToRadians(Ends[Grp[0]].HeadingDeg);
		const FVector LeftDir(-FMath::Cos(HRad), FMath::Sin(HRad), 0.f); // 90deg CCW from landing direction = pilot's left
		Grp.Sort([&](int32 A, int32 B)
		{
			return FVector::DotProduct(Ends[A].Centre, LeftDir) > FVector::DotProduct(Ends[B].Centre, LeftDir);
		});
		if (Grp.Num() == 2)
		{
			Suffix[Grp[0]] = TEXT("L");
			Suffix[Grp[1]] = TEXT("R");
		}
		else if (Grp.Num() == 3)
		{
			Suffix[Grp[0]] = TEXT("L");
			Suffix[Grp[1]] = TEXT("C");
			Suffix[Grp[2]] = TEXT("R");
		}
		else
		{
			for (int32 i = 0; i < Grp.Num(); ++i) { Suffix[Grp[i]] = FString::Printf(TEXT("%d"), i + 1); }
		}
	}

	TMap<TPair<AClearanceRunway*, bool>, FString> Labels;
	for (int32 i = 0; i < Ends.Num(); ++i)
	{
		Labels.Add({ Ends[i].Actor, Ends[i].bIsReciprocal },
			FString::Printf(TEXT("RWY %02d%s"), Ends[i].Number, *Suffix[i]));
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

		// Real-runway markings instead of one flat yellow rectangle. Warm cream for
		// the strip surface, a dashed centreline like real paint, and threshold piano
		// keys at both ends so each touchdown zone reads cleanly from above. - TripleA
		// Slow breathing pulse on the runway markings - dim to bright over ~1.6s. Real
		// time, not sim time, so pausing/scaling the sim doesn't freeze the visual. - TripleA
		const float RWT = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.f;
		const float Pulse = 0.7f + 0.3f * (0.5f + 0.5f * FMath::Sin(RWT * 4.f));
		auto Scaled = [&](int32 R, int32 G, int32 B) -> FColor
		{
			return FColor(
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(R * Pulse), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(G * Pulse), 0, 255)),
				static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(B * Pulse), 0, 255)));
		};
		const FColor Edge   = Scaled(255, 195, 80);   // amber edge paint
		const FColor Centre = Scaled(255, 180, 60);   // dashed centreline
		const FColor Keys   = Scaled(255, 220, 130);  // warm keys at the threshold
		const float EdgeThick = 90.f;
		const float DashThick = 80.f;

		// Edges + end caps - thinner than before, so the threshold markings read.
		DrawDebugLine(World, E1 + Side * HalfWidth, E2 + Side * HalfWidth, Edge, false, -1.f, 0, EdgeThick);
		DrawDebugLine(World, E1 - Side * HalfWidth, E2 - Side * HalfWidth, Edge, false, -1.f, 0, EdgeThick);
		DrawDebugLine(World, E1 + Side * HalfWidth, E1 - Side * HalfWidth, Edge, false, -1.f, 0, EdgeThick);
		DrawDebugLine(World, E2 + Side * HalfWidth, E2 - Side * HalfWidth, Edge, false, -1.f, 0, EdgeThick);

		// Dashed centreline - on for half a segment, off for half. Reads as paint
		// at any scale, not a single laser line. - TripleA
		{
			const int32 DashCount = 24;
			for (int32 i = 0; i < DashCount; ++i)
			{
				const float T0 = (i + 0.15f) / DashCount;
				const float T1 = (i + 0.65f) / DashCount;
				const FVector P0 = FMath::Lerp(E1, E2, T0);
				const FVector P1 = FMath::Lerp(E1, E2, T1);
				DrawDebugLine(World, P0, P1, Centre, false, -1.f, 0, DashThick);
			}
		}

		// Threshold piano keys - 8 short bars near each end, spanning most of the width
		// and offset slightly INBOARD from the threshold. - TripleA
		{
			const int32 KeyCount = 8;
			const float KeyInsetLen = HalfLen * 0.08f;            // how far inboard the keys sit
			const float KeyLen      = HalfLen * 0.10f;            // length of each key along the strip
			const float KeySpread   = HalfWidth * 0.85f;
			for (int32 i = 0; i < KeyCount; ++i)
			{
				const float t = (i + 0.5f) / KeyCount;            // 0..1 across width
				const float Off = (t * 2.f - 1.f) * KeySpread;     // -spread .. +spread
				// Inboard from E1
				const FVector P0 = E1 + Dir * KeyInsetLen + Side * Off;
				const FVector P1 = P0 + Dir * KeyLen;
				DrawDebugLine(World, P0, P1, Keys, false, -1.f, 0, 60.f);
				// Inboard from E2 (other end)
				const FVector Q0 = E2 - Dir * KeyInsetLen + Side * Off;
				const FVector Q1 = Q0 - Dir * KeyLen;
				DrawDebugLine(World, Q0, Q1, Keys, false, -1.f, 0, 60.f);
			}
		}

		// Label each end with the runway designator from the pre-computed table, so
		// parallel runways get the L/C/R suffix per ICAO. - TripleA
		if (const FString* L1 = Labels.Find({ *It, false }))
		{
			DrawDebugString(World, E1 + FVector(0, 0, 1.5f * S), *L1, nullptr, FColor::Yellow, 0.f, true, 1.2f);
		}
		if (It->bAllowReciprocal)
		{
			if (const FString* L2 = Labels.Find({ *It, true }))
			{
				DrawDebugString(World, E2 + FVector(0, 0, 1.5f * S), *L2, nullptr, FColor::Yellow, 0.f, true, 1.2f);
			}
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
	if (DISEmitter && DISEmitter->IsRunning())
	{
		AARTag += FString::Printf(TEXT("  |  [DIS %d/s]"), DISEmitter->GetLastPacketsSent());
	}
	if (DISReceiver && DISReceiver->IsRunning())
	{
		AARTag += FString::Printf(TEXT("  |  [DIS-RX %d/s ext:%d]"),
			DISReceiver->GetLastPacketsReceived(), DISReceiver->GetExternalAircraftCount());
	}
	if (Radar && Radar->IsEnabled())
	{
		AARTag += FString::Printf(TEXT("  |  [RADAR %.0fnm]"), Radar->RangeNm);
	}
	if (bGCIMode)
	{
		AARTag += TEXT("  |  [GCI]");
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
		int32 nLand = 0, nDep = 0, nRes = 0, nGA = 0, nSep = 0, nExit = 0, nWake = 0, nTCAS = 0, nInt = 0, nMisID = 0, nViol = 0, nEmer = 0, nCrash = 0, nBust = 0;
		for (const FIncidentRecord& R : Scoring->GetSessionLog())
		{
			switch (R.Type)
			{
			case EIncidentType::SuccessfulLanding:    ++nLand; break;
			case EIncidentType::SuccessfulDeparture:  ++nDep;  break;
			case EIncidentType::SuccessfulResolution: ++nRes;  break;
			case EIncidentType::SuccessfulIntercept:  ++nInt;  break;
			case EIncidentType::GoAroundTriggered:    ++nGA;   break;
			case EIncidentType::SeparationLoss:       ++nSep;  break;
			case EIncidentType::UnresolvedExit:       ++nExit; break;
			case EIncidentType::WakeEncounter:        ++nWake; break;
			case EIncidentType::TCASResolutionAdvisory: ++nTCAS; break;
			case EIncidentType::MisidentifiedCivilian: ++nMisID; break;
			case EIncidentType::ViolationZoneBreached: ++nViol; break;
			case EIncidentType::SuccessfulEmergencyHandling: ++nEmer; break;
			case EIncidentType::AircraftCrashed:       ++nCrash; break;
			case EIncidentType::RestrictedAirspaceBust: ++nBust; break;
			default: break;
			}
		}
		Readout += FString::Printf(TEXT("SCORING  total %d   |   +land %d  +dep %d  +resolved %d  +intercept %d  +emer %d   |   -go-around %d  -sep-loss %d  -wake %d  -tcas %d  -strayed %d  -misID %d  -violated %d  -crashed %d  -busted %d   |   next spawn %.0fs\n"),
			Scoring->GetCurrentScore(),
			nLand, nDep, nRes, nInt, nEmer, nGA, nSep, nWake, nTCAS, nExit, nMisID, nViol, nCrash, nBust, Scoring->GetCurrentSpawnInterval());
	}

	// Radar runs as a pure sensor logic layer: it ticks, tracks, and produces what the
	// operator should see - but the sweep arm and range ring are NOT drawn in the 3D
	// world (the operator doesn't see physical green lines in the sky). The radar's
	// view belongs on the operator's scope - the future 2D UI on the player's display.
	// For dev visibility, what the radar "knows" is still surfaced as text in the
	// readout below. - TripleA
	// Sensor fusion: gather every UClearanceRadar (the Controller's own + every
	// placed RadarSite actor), build one merged track per callsign by picking the
	// most-recently-painted return, count how many sites currently see each
	// contact, surface as [N/M] confidence in the readout. This is the "combined
	// operator picture" - track quality scales with how many sensors agree. - TripleA
	{
		TArray<UClearanceRadar*> Radars;
		if (Radar && Radar->IsEnabled()) { Radars.Add(Radar); }
		if (UWorld* W = GetWorld())
		{
			for (TActorIterator<AClearanceRadarSite> SIt(W); SIt; ++SIt)
			{
				if (*SIt && SIt->Radar && SIt->Radar->IsEnabled()) { Radars.Add(SIt->Radar); }
			}
		}

		if (Radars.Num() > 0)
		{
			TMap<FName, FRadarTrack> Fused;
			TMap<FName, int32> Sightings;
			for (UClearanceRadar* R : Radars)
			{
				for (const FRadarTrack& T : R->GetTracks())
				{
					Sightings.FindOrAdd(T.TruthCallsign) += 1;
					if (FRadarTrack* Existing = Fused.Find(T.TruthCallsign))
					{
						if (T.LastPaintTime > Existing->LastPaintTime) { *Existing = T; }
					}
					else
					{
						Fused.Add(T.TruthCallsign, T);
					}
				}
			}

			for (const TPair<FName, FRadarTrack>& Pair : Fused)
			{
				const FRadarTrack& Trk = Pair.Value;
				const int32 Seen = Sightings[Pair.Key];
				const EAlertLevel Alert = ConflictDetector ? ConflictDetector->GetAlertLevelFor(Trk.TruthCallsign) : EAlertLevel::None;
				const FString IdLabel = Trk.bHasSecondary ? Trk.DisplayCallsign.ToString() : FString(TEXT("PRI"));
				Readout += FString::Printf(TEXT("RDR %s [%d/%d]  hdg %3.0f  alt %5.0f  spd %3.0f  conf %.0f%%%s\n"),
					*IdLabel, Seen, Radars.Num(), Trk.Heading, Trk.Altitude, Trk.Speed,
					Trk.Confidence * 100.f, Alert != EAlertLevel::None ? TEXT(" <CONF>") : TEXT(""));
			}
		}
	}

	{
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

			// In GCI mode prefix the line with a NATO-style threat tag so the operator can
			// see classification at a glance. - TripleA
			const TCHAR* GCITag = TEXT("");
			if (bGCIMode)
			{
				switch (A.ThreatClass)
				{
				case EThreatClass::Friendly: GCITag = TEXT("[FRI] "); break;
				case EThreatClass::Hostile:  GCITag = TEXT("[HOS] "); break;
				case EThreatClass::Neutral:  GCITag = TEXT("[NEU] "); break;
				default:                     GCITag = TEXT("[UNK] "); break;
				}
			}

			// Emergency tag (squawk + countdown where applicable) so the operator
			// has all the time-pressure info on one line. - TripleA
			FString EmTag;
			switch (A.ActiveEmergency)
			{
			case EEmergencyType::GeneralMayday:
				EmTag = A.EmergencyDetail.IsEmpty()
					? FString::Printf(TEXT(" <MAYDAY sq7700 %.1fmin>"), FMath::Max(0.f, A.FuelRemainingMinutes))
					: FString::Printf(TEXT(" <MAYDAY sq7700 %s %.1fmin>"), *A.EmergencyDetail, FMath::Max(0.f, A.FuelRemainingMinutes));
				break;
			case EEmergencyType::CommsFailure:
				EmTag = TEXT(" <NORDO sq7600>");
				break;
			case EEmergencyType::Hijack:
				EmTag = TEXT(" <HIJACK sq7500>");
				break;
			case EEmergencyType::FuelLow:
				EmTag = FString::Printf(TEXT(" <FUEL %.1fmin>"), FMath::Max(0.f, A.FuelRemainingMinutes));
				break;
			default: break;
			}

			Readout += FString::Printf(TEXT("%s%s  hdg %3.0f>%3.0f  alt %5.0f>%5.0f  spd %3.0f>%3.0f  vs%+5.0f%s%s\n"),
				GCITag,
				*A.Callsign.ToString(),
				A.Heading, A.TargetHeading,
				A.Altitude, A.TargetAltitude,
				A.Speed, A.TargetSpeed,
				A.ClimbRate,
				Alert != EAlertLevel::None ? TEXT(" <CONF>") : TEXT(""),
				*EmTag);
		}
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

	TSet<FName> InterceptCredited; // so we log Successful Intercept once per pair this pass

	// GetAllAircraftStates returns a copy, so deregistering inside the loop is safe.
	for (const FAircraftState& State : AirspaceManager->GetAllAircraftStates())
	{
		const float Dist = FVector2D(State.Position.X, State.Position.Y).Size();

		// GCI-controlled aircraft leaving: a flight (bandit + 1-3 fighters) shares one
		// bandit key. Credit Successful Intercept once per bandit and deregister the
		// whole flight. Unjoined / lone aircraft just leave quietly. - TripleA
		if (State.bUnderGCIControl && Dist > ExitRadiusNm)
		{
			FName BanditCs;
			if (ActiveIntercepts.Contains(State.Callsign))
			{
				// state is a fighter -> bandit is the value
				BanditCs = ActiveIntercepts[State.Callsign];
			}
			else
			{
				// state may be the bandit itself (a value in the map)
				for (const TPair<FName, FName>& P : ActiveIntercepts)
				{
					if (P.Value == State.Callsign) { BanditCs = State.Callsign; break; }
				}
			}

			TArray<FName> Fighters;
			bool bAnyJoined = false;
			if (!BanditCs.IsNone())
			{
				for (const TPair<FName, FName>& P : ActiveIntercepts)
				{
					if (P.Value == BanditCs)
					{
						Fighters.Add(P.Key);
						if (JoinedIntercepts.Contains(P.Key)) { bAnyJoined = true; }
					}
				}
			}

			if (bAnyJoined && !BanditCs.IsNone() && !InterceptCredited.Contains(BanditCs))
			{
				InterceptCredited.Add(BanditCs);
				if (Scoring)
				{
					Scoring->LogIncident(EIncidentType::SuccessfulIntercept, BanditCs, NAME_None,
						FString::Printf(TEXT("GCI: %d-ship escort of %s out of sector"), Fighters.Num(), *BanditCs.ToString()));
				}
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green,
						FString::Printf(TEXT("INTERCEPT SUCCESSFUL  %d-ship escorted %s out  (+%d)"),
							Fighters.Num(), *BanditCs.ToString(),
							Scoring ? Scoring->PointsIntercept : 0));
				}
				AirspaceManager->DeregisterAircraft(BanditCs);
				for (const FName& F : Fighters) { AirspaceManager->DeregisterAircraft(F); }
				for (const FName& F : Fighters) { ActiveIntercepts.Remove(F); JoinedIntercepts.Remove(F); SettledInFormation.Remove(F); }
			}
			else
			{
				AirspaceManager->DeregisterAircraft(State.Callsign);
				if (ActiveIntercepts.Contains(State.Callsign))
				{
					ActiveIntercepts.Remove(State.Callsign);
					JoinedIntercepts.Remove(State.Callsign);
				}
			}
			continue;
		}

		// Pull it off only once it's on the deck AND has braked to a FULL STOP, so the
		// whole roll-out plays out - touch down, brake, slow, stop - before it's
		// removed, instead of vanishing while still rolling. - TripleA
		if (State.FlightPhase == EFlightPhase::Landing && State.Altitude <= 100.f && State.Speed <= 1.f)
		{
			if (Scoring)
			{
				Scoring->LogIncident(EIncidentType::SuccessfulLanding, State.Callsign, NAME_None, TEXT("Landed"));
				// Emergency aircraft that lands cleanly gets the handling bonus on top
				// of the normal landing reward. - TripleA
				if (State.ActiveEmergency != EEmergencyType::None)
				{
					Scoring->LogIncident(EIncidentType::SuccessfulEmergencyHandling, State.Callsign, NAME_None,
						FString::Printf(TEXT("Emergency %s safely landed"), *UEnum::GetValueAsString(State.ActiveEmergency)));
				}
			}
			AirspaceManager->DeregisterAircraft(State.Callsign);
		}
		else if (Dist > ExitRadiusNm)
		{
			// Cleared to leave = a clean departure; drifting out otherwise is a miss.
			// An emergency aircraft drifting out unhandled is a catastrophic loss. - TripleA
			EIncidentType Outcome;
			if (State.ActiveEmergency != EEmergencyType::None && State.FlightPhase != EFlightPhase::Exiting)
			{
				Outcome = EIncidentType::AircraftCrashed;
			}
			else if (State.FlightPhase == EFlightPhase::Exiting)
			{
				Outcome = (State.ActiveEmergency != EEmergencyType::None)
					? EIncidentType::SuccessfulEmergencyHandling
					: EIncidentType::SuccessfulDeparture;
			}
			else
			{
				Outcome = EIncidentType::UnresolvedExit;
			}
			if (Scoring) { Scoring->LogIncident(Outcome, State.Callsign, NAME_None, TEXT("Left sector")); }
			AirspaceManager->DeregisterAircraft(State.Callsign);
		}
	}
}

EInstructionResult AClearanceSimulationController::PlayerIssueInstruction(const FAircraftInstruction& Instruction)
{
	// Civilian ATC can't command anything under air defence control, and can't
	// command external (federated) aircraft either - those belong to whichever
	// peer sim is publishing them. NORDO contacts (IFF off, not declared friendly)
	// also reject silently - the non-response is the operator's clue. - TripleA
	if (AirspaceManager)
	{
		const FAircraftState Target = AirspaceManager->GetAircraftState(Instruction.TargetCallsign);
		if (Target.bIsValid && !Target.bIFFOperational && Target.ThreatClass != EThreatClass::Friendly)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow,
					FString::Printf(TEXT("%s, NO RESPONSE"), *Instruction.TargetCallsign.ToString()));
			}
			return EInstructionResult::Rejected_NoResponse;
		}
		// Comms-failure (squawk 7600): the IFF is still squawking but the radio is
		// broken - aircraft can't hear us. ATC has to clear airspace and watch them
		// fly the published lost-comms procedure. - TripleA
		if (Target.bIsValid && Target.ActiveEmergency == EEmergencyType::CommsFailure)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow,
					FString::Printf(TEXT("%s, NO RADIO (squawk 7600)"), *Instruction.TargetCallsign.ToString()));
			}
			return EInstructionResult::Rejected_NoResponse;
		}
		// Hijack (squawk 7500): the hijackers are flying it now - ATC instructions
		// don't get followed. Silent rejection is the cognitive-fidelity choice:
		// the operator notices the aircraft isn't doing what was asked. - TripleA
		if (Target.bIsValid && Target.ActiveEmergency == EEmergencyType::Hijack)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow,
					FString::Printf(TEXT("%s, NOT COMPLYING (squawk 7500)"), *Instruction.TargetCallsign.ToString()));
			}
			return EInstructionResult::Rejected_NoResponse;
		}
		if (Target.bIsValid && Target.bUnderGCIControl)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow,
					FString::Printf(TEXT("%s is under GCI control - civilian ATC can't command"),
						*Instruction.TargetCallsign.ToString()));
			}
			return EInstructionResult::Rejected_PhysicallyImpossible;
		}
		if (Target.bIsValid && Target.bIsExternal)
		{
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow,
					FString::Printf(TEXT("%s is external traffic - command its owning sim instead"),
						*Instruction.TargetCallsign.ToString()));
			}
			return EInstructionResult::Rejected_PhysicallyImpossible;
		}
	}

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

bool AClearanceSimulationController::StartDIS(const FString& Host, int32 Port)
{
	if (!DISEmitter) { return false; }
	const bool bOk = DISEmitter->Start(Host, Port);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, bOk ? FColor::Cyan : FColor::Red,
			bOk ? FString::Printf(TEXT("DIS: emitting on %s:%d"), (Host.IsEmpty() ? TEXT("broadcast") : *Host), Port)
			    : FString::Printf(TEXT("DIS: failed to start (%s:%d)"), *Host, Port));
	}
	return bOk;
}

void AClearanceSimulationController::StopDIS()
{
	if (DISEmitter) { DISEmitter->Stop(); }
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, TEXT("DIS: stopped")); }
}

bool AClearanceSimulationController::StartDISReceiver(int32 Port)
{
	if (!DISReceiver) { return false; }
	// Keep the receiver's loopback-skip identity in sync with our own emitter's.
	if (DISEmitter)
	{
		DISReceiver->LocalSiteId        = DISEmitter->SiteId;
		DISReceiver->LocalApplicationId = DISEmitter->ApplicationId;
	}
	const bool bOk = DISReceiver->Start(Port);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, bOk ? FColor::Cyan : FColor::Red,
			bOk ? FString::Printf(TEXT("DIS: listening on port %d"), Port)
			    : FString::Printf(TEXT("DIS: failed to bind port %d"), Port));
	}
	return bOk;
}

void AClearanceSimulationController::StopDISReceiver()
{
	if (DISReceiver) { DISReceiver->Stop(); }
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, TEXT("DIS: receiver stopped")); }
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
	// External aircraft are driven by an outside source (a DIS feed). Skipping the
	// local Behaviour and Comms registration here means we mirror their truth on
	// the scope but never try to fly them or accept commands on their behalf - we
	// still spawn the visual underneath so the operator sees the mesh. - TripleA
	const bool bExternal = AirspaceManager
		? AirspaceManager->GetAircraftState(Callsign).bIsExternal
		: false;

	if (!bExternal)
	{
		UClearanceAircraftBehaviour* Behaviour = NewObject<UClearanceAircraftBehaviour>(this);
		Behaviour->Initialise(AirspaceManager, Callsign);
		Behaviour->TouchdownZoneOffsetNm = FMath::Max(0.f, TouchdownZoneMeters) / 1852.f; // metres -> nm
		BehaviourMap.Add(Callsign, Behaviour);
		if (CommsRouter) { CommsRouter->RegisterBehaviour(Callsign, Behaviour); }

		// If the freshly-spawned aircraft is a bandit profile (NORDO, not declared
		// friendly), redirect it AT a random violation zone instead of leaving it
		// pointed at sector centre. A real intruder has an objective - that's what
		// makes the operator's intercept call meaningful. - TripleA
		if (AirspaceManager && GetWorld())
		{
			const FAircraftState S = AirspaceManager->GetAircraftState(Callsign);
			if (S.bIsValid && !S.bIFFOperational && S.ThreatClass != EThreatClass::Friendly)
			{
				TArray<AClearanceViolationZone*> Zones;
				for (TActorIterator<AClearanceViolationZone> ZIt(GetWorld()); ZIt; ++ZIt) { Zones.Add(*ZIt); }
				if (Zones.Num() > 0)
				{
					AClearanceViolationZone* Z = Zones[FMath::RandRange(0, Zones.Num() - 1)];
					if (Z)
					{
						const FVector ZW = Z->GetActorLocation();
						const FVector OriginW = GetActorLocation();
						const float W = FMath::Max(1.f, WorldUnitsPerNm);
						const FVector2D ZNm((ZW.X - OriginW.X) / W, (ZW.Y - OriginW.Y) / W);
						const FVector2D ToZone = ZNm - FVector2D(S.Position.X, S.Position.Y);
						float Hdg = FMath::RadiansToDegrees(FMath::Atan2(ToZone.X, ToZone.Y));
						if (Hdg < 0.f) { Hdg += 360.f; }
						FAircraftState NewS = S;
						NewS.Heading = Hdg;
						NewS.TargetHeading = Hdg;
						AirspaceManager->RequestStateUpdate(NewS);
					}
				}
			}
		}
	}

	// Spawn a visual for this aircraft's category, picking a random variant.
	if (GetWorld() && AirspaceManager)
	{
		const FAircraftState State = AirspaceManager->GetAircraftState(Callsign);
		// Hostiles get the MiG pool, friendly military gets the F-35 pool, civilians
		// pick by wake category. Each rung falls back to the next if its pool is empty,
		// so a half-configured project still spawns something. - TripleA
		const bool bHostile = (State.ThreatClass == EThreatClass::Hostile);
		const TArray<FAircraftVisualVariant>& Variants =
			(bHostile && HostileVariants.Num() > 0)             ? HostileVariants  :
			(State.bIsMilitary && FighterVariants.Num() > 0)    ? FighterVariants  :
			                                                      VariantsFor(State.WakeCategory);
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

	// Return this aircraft's voice slot to the pool so a future aircraft can use it.
	if (GetWorld())
	{
		for (TActorIterator<AClearanceVoiceOutput> VoIt(GetWorld()); VoIt; ++VoIt)
		{
			if (*VoIt) { VoIt->ReleaseVoiceForCallsign(Callsign); break; }
		}
	}
	ShadowTargets.Remove(Callsign);
	UrgencyThresholdsHit.Remove(Callsign);

	// Drop any TCAS pair entries involving this aircraft so the set doesn't leak.
	const FString CallStr = Callsign.ToString();
	for (auto It = TCASPairsAwaitingResolution.CreateIterator(); It; ++It)
	{
		if (It->Contains(CallStr)) { It.RemoveCurrent(); }
	}

	// Same for any active intercepts referencing this callsign.
	TArray<FName> InterceptKeysToRemove;
	for (const TPair<FName, FName>& P : ActiveIntercepts)
	{
		if (P.Key == Callsign || P.Value == Callsign) { InterceptKeysToRemove.Add(P.Key); }
	}
	for (const FName& K : InterceptKeysToRemove)
	{
		ActiveIntercepts.Remove(K);
		JoinedIntercepts.Remove(K);
		SettledInFormation.Remove(K);
	}
	SettledInFormation.Remove(Callsign);

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

// --- GCI / Air Defence console commands ---------------------------------------
static EThreatClass ParseThreatClass(const FString& T)
{
	const FString U = T.ToLower();
	if (U.StartsWith(TEXT("fri"))) return EThreatClass::Friendly;
	if (U.StartsWith(TEXT("hos"))) return EThreatClass::Hostile;
	if (U.StartsWith(TEXT("neu"))) return EThreatClass::Neutral;
	return EThreatClass::Unknown;
}

static FAutoConsoleCommandWithWorldAndArgs GClearanceGCICmd(
	TEXT("clearance.gci"),
	TEXT("clearance.gci <on|off> - toggle GCI / air defence mode"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			const bool bOn = (Args.Num() < 1) ? true : (Args[0].ToLower() != TEXT("off") && Args[0] != TEXT("0"));
			C->SetGCIModeEnabled(bOn);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceClassifyCmd(
	TEXT("clearance.classify"),
	TEXT("clearance.classify <callsign> <friendly|hostile|neutral|unknown>"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2) { return; }
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->ClassifyAircraft(FName(*Args[0]), ParseThreatClass(Args[1]));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceIFFCmd(
	TEXT("clearance.iff"),
	TEXT("clearance.iff <callsign> - interrogate IFF (returns class + squawk if responsive)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { return; }
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			EThreatClass Out; int32 Sq;
			C->InterrogateIFF(FName(*Args[0]), Out, Sq);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceInterceptCmd(
	TEXT("clearance.intercept"),
	TEXT("clearance.intercept <fighter> <target> - vector a fighter onto an intercept course"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2) { return; }
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->VectorIntercept(FName(*Args[0]), FName(*Args[1]));
		}
	}));

// Drop a fighter + a hostile contact into the sector for a quick GCI demo. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceGCITestCmd(
	TEXT("clearance.gci.test"),
	TEXT("clearance.gci.test - drop a friendly fighter + a hostile contact for an intercept demo"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C || !C->GetAirspaceManager()) { return; }
		AClearanceAirspaceManager* M = C->GetAirspaceManager();
		C->ClearTraffic();

		// Hostile: heading west, no IFF response.
		FAircraftState H;
		H.Callsign = TEXT("BANDIT");
		H.Position = FVector(20.f, 5.f, 0.f);
		H.Altitude = 15000.f;
		H.Heading = 270.f;
		H.Speed = 360.f;
		H.WakeCategory = EWakeCategory::Medium;
		H.FlightPhase = EFlightPhase::Enroute;
		H.ThreatClass = EThreatClass::Hostile;
		H.SquawkCode = 7777;
		H.bIFFOperational = false;
		H.bIsMilitary = true;
		M->RegisterAircraft(H);

		// Three-ship friendly fighter flight ready to intercept (VIPER01/02/03).
		auto Viper = [&](const TCHAR* Cs, FVector Pos, float Hdg, int32 Sq)
		{
			FAircraftState F;
			F.Callsign = Cs;
			F.Position = Pos;
			F.Altitude = 15000.f;
			F.Heading = Hdg;
			F.Speed = 620.f;
			F.WakeCategory = EWakeCategory::Medium;
			F.FlightPhase = EFlightPhase::Enroute;
			F.ThreatClass = EThreatClass::Friendly;
			F.SquawkCode = Sq;
			F.bIFFOperational = true;
			F.bIsMilitary = true;
			M->RegisterAircraft(F);
		};
		Viper(TEXT("VIPER01"), FVector(-12.f, -8.f,  0.f),  45.f, 2200);
		Viper(TEXT("VIPER02"), FVector( 10.f, -10.f, 0.f), 320.f, 2201);
		Viper(TEXT("VIPER03"), FVector(  0.f, -14.f, 0.f),   0.f, 2202);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 7.f, FColor::Cyan,
				TEXT("GCI TEST: BANDIT (hostile, IFF off) inbound. 3-ship VIPER flight ready. Try: clearance.gci on; clearance.iff BANDIT; clearance.intercept.flight BANDIT"));
		}
	}));

// Scramble a fresh 3-ship from the sector boundary onto a bandit. The vipers
// spawn from random points on the edge, get full military fit (IFF on, friendly,
// F-35 mesh), and are auto-vectored on the target. Closes the natural gameplay
// loop: bandit drops in -> player interrogates -> declares hostile -> SCRAMBLE.
// The "from boundary not from runway" choice is deliberate; this fits the
// cognitive model of fighters arriving on station from elsewhere. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceScrambleCmd(
	TEXT("clearance.scramble"),
	TEXT("clearance.scramble <bandit> - launch a 3-ship intercept flight from the sector boundary onto a target"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { return; }
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C) { return; }
		const FName BanditCs(*Args[0]);
		const int32 N = C->ScrambleInterceptors(BanditCs);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.f, N > 0 ? FColor::Cyan : FColor::Red,
				N > 0 ? FString::Printf(TEXT("SCRAMBLE: %d-ship VIPER flight inbound on %s"), N, *BanditCs.ToString())
				      : FString::Printf(TEXT("SCRAMBLE: no such contact '%s'"), *Args[0]));
		}
	}));

// Launch a shadow escort flight on a hijacked aircraft (must be squawking 7500).
// The fighters tail without forcing the aircraft onto a heading - real doctrine. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceShadowCmd(
	TEXT("clearance.shadow"),
	TEXT("clearance.shadow <hijack> - launch a 3-ship shadow escort onto a 7500-squawking aircraft (no declare-hostile required)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { return; }
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C) { return; }
		const FName Cs(*Args[0]);
		const int32 N = C->ShadowEscort(Cs);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 6.f, N > 0 ? FColor::Cyan : FColor::Red,
				N > 0 ? FString::Printf(TEXT("SHADOW: %d-ship VIPER flight tailing %s"), N, *Cs.ToString())
				      : FString::Printf(TEXT("SHADOW: refused (target must be squawking 7500)")));
		}
	}));

// Send every available friendly military aircraft at this bandit at once. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceInterceptFlightCmd(
	TEXT("clearance.intercept.flight"),
	TEXT("clearance.intercept.flight <bandit> - vector ALL friendly military aircraft onto an intercept"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { return; }
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C || !C->GetAirspaceManager()) { return; }
		const FName BanditCs(*Args[0]);
		int32 N = 0;
		for (const FAircraftState& S : C->GetAirspaceManager()->GetAllAircraftStates())
		{
			if (S.bIsMilitary && S.ThreatClass == EThreatClass::Friendly && S.Callsign != BanditCs)
			{
				if (C->VectorIntercept(S.Callsign, BanditCs)) { ++N; }
			}
		}
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan,
			FString::Printf(TEXT("FLIGHT VECTORED: %d fighter(s) intercepting %s"), N, *BanditCs.ToString())); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceRadarCmd(
	TEXT("clearance.radar"),
	TEXT("clearance.radar <on|off> - toggle the modelled radar view (vs god's-eye truth)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			const bool bOn = (Args.Num() < 1) ? true : (Args[0].ToLower() != TEXT("off") && Args[0] != TEXT("0"));
			C->SetRadarEnabled(bOn);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceDISStartCmd(
	TEXT("clearance.dis.start"),
	TEXT("clearance.dis.start [host] [port] - publish DIS Entity State PDUs over UDP. Defaults: broadcast on port 3000."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			const FString Host = (Args.Num() > 0) ? Args[0] : TEXT("broadcast");
			const int32 Port = (Args.Num() > 1) ? FCString::Atoi(*Args[1]) : 3000;
			C->StartDIS(Host, Port);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceDISStopCmd(
	TEXT("clearance.dis.stop"),
	TEXT("clearance.dis.stop - stop emitting DIS PDUs"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World)) { C->StopDIS(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceDISListenCmd(
	TEXT("clearance.dis.listen"),
	TEXT("clearance.dis.listen [port] - listen for DIS Entity State PDUs from peers. Default port 3000."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			const int32 Port = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 3000;
			C->StartDISReceiver(Port);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceDISUnlistenCmd(
	TEXT("clearance.dis.unlisten"),
	TEXT("clearance.dis.unlisten - stop listening for DIS PDUs"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World)) { C->StopDISReceiver(); }
	}));

// Change this instance's DIS Site ID at runtime. Two copies of the sim on the
// same network need different Site IDs or each will filter the other's traffic
// as its own broadcast loopback. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceDISSiteCmd(
	TEXT("clearance.dis.site"),
	TEXT("clearance.dis.site <N> - set this instance's DIS Site ID (default 1). Set different IDs on two instances so they can hear each other."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { return; }
		const int32 NewSite = FCString::Atoi(*Args[0]);
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C) { return; }
		if (UClearanceDISEmitter* E = C->GetDISEmitter())   { E->SiteId        = NewSite; }
		if (UClearanceDISReceiver* R = C->GetDISReceiver()) { R->LocalSiteId   = NewSite; }
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan,
				FString::Printf(TEXT("DIS Site ID = %d (peers must use a different number)"), NewSite));
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
