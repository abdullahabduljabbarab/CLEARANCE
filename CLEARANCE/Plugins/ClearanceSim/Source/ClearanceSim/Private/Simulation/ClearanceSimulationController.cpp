#include "Simulation/ClearanceSimulationController.h"
#include "Simulation/ClearanceMissile.h"
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
#include "Scenario/ClearanceScenarioRunner.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "Net/UnrealNetwork.h"
#include "Components/LineBatchComponent.h"
#include "Safety/ClearanceRadar.h"
#include "Scoring/ClearanceScoring.h"
#include "Simulation/ClearanceSessionRecorder.h"
#include "Simulation/ClearanceDISEmitter.h"
#include "Simulation/ClearanceDDSEmitter.h"
#include "Simulation/ClearanceRTIEmitter.h"
#include "Simulation/ClearanceHLAEmitter.h"
#include "Simulation/ClearanceDDSReceiver.h"
#include "Simulation/ClearanceDISReceiver.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "ContentStreaming.h"
#include "Kismet/GameplayStatics.h"
#include "Core/ClearanceConstants.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
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

	// MIL-STD-2525C affiliation frame for an air track, drawn flat on the XY
	// plane so it reads like an icon on the operator's top-down scope. Shape
	// + colour both encode the threat class - shape is the strict standard
	// (friend = rectangle, hostile = diamond, unknown = quatrefoil-ish, neutral
	// = square), colour reinforces it for fast read. A short bearing vector
	// extends from centre. Conflict level rings the symbol when present, the
	// military modifier drops a small "+" below for combatant equipment. - TripleA
	void DrawMIL2525CAir(UWorld* World, const FVector& Centre, float Size,
		EThreatClass Threat, float HeadingDeg, bool bIsMilitary, EAlertLevel Alert)
	{
		if (!World) { return; }

		FColor Frame;
		switch (Threat)
		{
		case EThreatClass::Friendly: Frame = FColor(80, 200, 255);  break;  // cyan
		case EThreatClass::Hostile:  Frame = FColor(255, 60, 60);   break;  // red
		case EThreatClass::Unknown:  Frame = FColor(255, 220, 60);  break;  // amber
		case EThreatClass::Neutral:  Frame = FColor(80, 255, 120);  break;  // green
		default:                     Frame = FColor(220, 220, 220); break;
		}

		const float H = Size * 0.5f;
		const float Thick = FMath::Max(20.f, Size * 0.04f);
		auto L = [&](const FVector& A, const FVector& B)
		{
			DrawDebugLine(World, A, B, Frame, false, -1.f, 0, Thick);
		};

		switch (Threat)
		{
		case EThreatClass::Friendly:
		{
			const FVector W(H, 0, 0);
			const FVector T(0, H * 0.65f, 0);
			L(Centre - W + T, Centre + W + T);
			L(Centre + W + T, Centre + W - T);
			L(Centre + W - T, Centre - W - T);
			L(Centre - W - T, Centre - W + T);
			break;
		}
		case EThreatClass::Hostile:
		{
			const FVector N(0, H, 0);
			const FVector E(H, 0, 0);
			L(Centre + N, Centre + E);
			L(Centre + E, Centre - N);
			L(Centre - N, Centre - E);
			L(Centre - E, Centre + N);
			break;
		}
		case EThreatClass::Unknown:
		{
			// Octagonal stand-in for the 2525C four-lobe quatrefoil - cheap to
			// draw with line primitives and still reads as "not square, not
			// diamond, not rectangle" at a glance. - TripleA
			const FVector Pts[] = {
				Centre + FVector( H,           H * 0.4f,    0),
				Centre + FVector( H * 0.4f,    H,           0),
				Centre + FVector(-H * 0.4f,    H,           0),
				Centre + FVector(-H,           H * 0.4f,    0),
				Centre + FVector(-H,          -H * 0.4f,    0),
				Centre + FVector(-H * 0.4f,   -H,           0),
				Centre + FVector( H * 0.4f,   -H,           0),
				Centre + FVector( H,          -H * 0.4f,    0),
			};
			for (int32 i = 0; i < 8; ++i) { L(Pts[i], Pts[(i + 1) % 8]); }
			break;
		}
		case EThreatClass::Neutral:
		{
			const FVector W(H * 0.85f, 0, 0);
			const FVector T(0, H * 0.85f, 0);
			L(Centre - W + T, Centre + W + T);
			L(Centre + W + T, Centre + W - T);
			L(Centre + W - T, Centre - W - T);
			L(Centre - W - T, Centre - W + T);
			break;
		}
		default: break;
		}

		// Bearing vector - sticks out past the frame so it reads as motion.
		const float Rad = FMath::DegreesToRadians(HeadingDeg);
		const FVector Tip = Centre + FVector(FMath::Sin(Rad), FMath::Cos(Rad), 0) * H * 1.6f;
		L(Centre, Tip);

		// Conflict ring - circles the symbol with the alert colour when active.
		if (Alert != EAlertLevel::None)
		{
			const FColor AlertCol = (Alert == EAlertLevel::Critical) ? FColor(255, 50, 50)
				: (Alert == EAlertLevel::Warning) ? FColor(255, 140, 0)
				:                                   FColor(255, 220, 60);
			DrawDebugCircle(World, Centre, H * 1.4f, 24, AlertCol, false, -1.f, 0, Thick,
				FVector(1, 0, 0), FVector(0, 1, 0), false);
		}

		// Military equipment modifier - small "+" below the frame.
		if (bIsMilitary)
		{
			const float Below = -H * 1.15f;
			const float Sz = H * 0.18f;
			L(Centre + FVector(-Sz, Below, 0), Centre + FVector(Sz, Below, 0));
			L(Centre + FVector(0, Below - Sz, 0), Centre + FVector(0, Below + Sz, 0));
		}
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

	// Networked instructor station: the controller exists on clients as a stub so
	// they can find it via TActorIterator and call replicated functions on it.
	// All simulation work still happens server-side; clients just render. - TripleA
	bReplicates = true;
	bAlwaysRelevant = true;

	// Instructor PIP capture. Sits on the controller and teleports per-tick to
	// the active preset camera's transform - one component, four viewpoints. We
	// disable bCaptureEveryFrame so it only renders when the instructor flips
	// the panel to camera mode, and we manually CaptureScene() to throttle to
	// the configured rate. - TripleA
	InstructorPipCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("InstructorPipCapture"));
	InstructorPipCapture->SetupAttachment(RootComponent);
	InstructorPipCapture->bCaptureEveryFrame = false;
	InstructorPipCapture->bCaptureOnMovement = false;
	// Persist the renderer state across manual captures - without this, each
	// CaptureScene call has to re-initialise transient render data and the
	// first few frames after enable come back black or stale. - TripleA
	InstructorPipCapture->bAlwaysPersistRenderingState = true;
	// FinalToneCurveHDR is the closest match to what the main viewport
	// renders - includes post-processing, tone mapping, exposure, and
	// importantly the HDR pipeline so reflections / GI lookups produce the
	// right colours. SCS_FinalColorLDR drops too much information. - TripleA
	InstructorPipCapture->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
	InstructorPipCapture->FOVAngle = 80.f;
	InstructorPipCapture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;

	// Quality overrides: SceneCapture defaults to a stripped render path for
	// perf reasons (no TAA, no AntiAliasing, no Lumen GI, no SSR, biased LOD).
	// Re-enable what's needed so aircraft look like proper meshes instead of
	// Roblox blobs. Without these, the same mesh that renders fine in the
	// main viewport ends up flat-shaded and reflection-less in the PIP. - TripleA
	InstructorPipCapture->ShowFlags.SetAntiAliasing(true);
	InstructorPipCapture->ShowFlags.SetTemporalAA(true);
	InstructorPipCapture->ShowFlags.SetMotionBlur(false);   // off for scope clarity
	InstructorPipCapture->ShowFlags.SetTonemapper(true);
	InstructorPipCapture->ShowFlags.SetEyeAdaptation(true);
	InstructorPipCapture->ShowFlags.SetBloom(true);
	InstructorPipCapture->ShowFlags.SetAmbientOcclusion(true);
	InstructorPipCapture->ShowFlags.SetLighting(true);
	InstructorPipCapture->ShowFlags.SetReflectionEnvironment(true);
	InstructorPipCapture->ShowFlags.SetScreenSpaceReflections(true);
	InstructorPipCapture->ShowFlags.SetGlobalIllumination(true);
	InstructorPipCapture->ShowFlags.SetLumenGlobalIllumination(true);
	InstructorPipCapture->ShowFlags.SetLumenReflections(true);
	InstructorPipCapture->ShowFlags.SetSkyLighting(true);
	InstructorPipCapture->ShowFlags.SetDirectLighting(true);
	InstructorPipCapture->ShowFlags.SetIndirectLightingCache(true);
	InstructorPipCapture->ShowFlags.SetTexturedLightProfiles(true);
	InstructorPipCapture->ShowFlags.SetVolumetricFog(true);

	// Post-process volume contribution. Without this, post-process volumes in
	// the level (like exposure curves and color grading) don't apply to the
	// capture, so it looks washed out vs the main view. - TripleA
	InstructorPipCapture->PostProcessBlendWeight = 1.f;

	// LODDistanceFactor < 1 forces higher-detail LODs at the same distance.
	// Aircraft meshes drop to low-poly potatoes pretty aggressively under the
	// default 1.0 bias - 0.1 forces near-LOD-0 everywhere. Costs more but
	// only one capture is running. - TripleA
	InstructorPipCapture->LODDistanceFactor = 0.1f;
}

void AClearanceSimulationController::BeginPlay()
{
	Super::BeginPlay();

	// Force the high replication rate on every active SimulationController
	// instance regardless of what the level-placed actor was serialised with.
	// 100Hz NetUpdateFrequency means clients see aircraft state changes within
	// ~10ms of the server applying them. - TripleA
	SetNetUpdateFrequency(100.f);
	SetMinNetUpdateFrequency(60.f);

	// Main menu bakes ?autonomous=1 into the OpenLevel URL for solo
	// OPERATOR SESSION. Read it here so bAutonomousMode reflects the
	// launch mode from the very first tick, before the emergency
	// injector runs. Absent option leaves whatever the level-placed
	// actor was serialised with (usually false). - TripleA
	if (UWorld* W = GetWorld())
	{
		const FString UrlOption = UGameplayStatics::ParseOption(W->URL.ToString(false), TEXT("autonomous"));
		if (!UrlOption.IsEmpty())
		{
			bAutonomousMode = (UrlOption == TEXT("1") || UrlOption.Equals(TEXT("true"), ESearchCase::IgnoreCase));
		}
	}

	// Force the PIP capture's quality settings here regardless of what got
	// serialised on the level-placed component. The constructor sets sensible
	// defaults but the level keeps whatever was saved when the actor was
	// placed; re-applying in BeginPlay guarantees the live values match the
	// latest code. - TripleA
	if (InstructorPipCapture)
	{
		InstructorPipCapture->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
		InstructorPipCapture->bAlwaysPersistRenderingState = true;
		InstructorPipCapture->LODDistanceFactor = 0.1f;
		InstructorPipCapture->PostProcessBlendWeight = 1.f;
		InstructorPipCapture->ShowFlags.SetAntiAliasing(true);
		InstructorPipCapture->ShowFlags.SetTemporalAA(true);
		InstructorPipCapture->ShowFlags.SetMotionBlur(false);
		InstructorPipCapture->ShowFlags.SetTonemapper(true);
		InstructorPipCapture->ShowFlags.SetEyeAdaptation(true);
		InstructorPipCapture->ShowFlags.SetBloom(true);
		InstructorPipCapture->ShowFlags.SetAmbientOcclusion(true);
		InstructorPipCapture->ShowFlags.SetLighting(true);
		InstructorPipCapture->ShowFlags.SetReflectionEnvironment(true);
		InstructorPipCapture->ShowFlags.SetScreenSpaceReflections(true);
		InstructorPipCapture->ShowFlags.SetGlobalIllumination(true);
		InstructorPipCapture->ShowFlags.SetLumenGlobalIllumination(true);
		InstructorPipCapture->ShowFlags.SetLumenReflections(true);
		InstructorPipCapture->ShowFlags.SetSkyLighting(true);
		InstructorPipCapture->ShowFlags.SetDirectLighting(true);
		InstructorPipCapture->ShowFlags.SetIndirectLightingCache(true);
		InstructorPipCapture->ShowFlags.SetTexturedLightProfiles(true);
		InstructorPipCapture->ShowFlags.SetVolumetricFog(true);
	}

	// Allocate the PIP render target on every instance (host + clients). The RT
	// is a per-machine resource; the capture only produces a texture where
	// the panel actually wants to render it. - TripleA
	if (InstructorPipCapture && !InstructorPipRT)
	{
		InstructorPipRT = NewObject<UTextureRenderTarget2D>(this, TEXT("InstructorPipRT"));
		InstructorPipRT->InitAutoFormat(
			FMath::Max(64, InstructorPipResolution.X),
			FMath::Max(64, InstructorPipResolution.Y));
		InstructorPipRT->UpdateResource();
		InstructorPipCapture->TextureTarget = InstructorPipRT;
	}

	// Recorder runs on EVERY instance (server + client) so the instructor
	// panel - which lives on the client - can scrub through the same replay
	// buffer the server has, without needing a server RPC just to read the
	// snapshot count. Server records authoritative truth; client records
	// what it received via replication. - TripleA
	if (!Recorder)
	{
		Recorder = NewObject<UClearanceSessionRecorder>(this);
		if (Recorder && bAutoStartRecording) { Recorder->StartRecording(); }
	}

	// Server-only: the simulation lives here. Clients receive the world via the
	// replicated AirspaceManager + Controller state and only render. - TripleA
	if (!HasAuthority()) { return; }
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
	if (!Recorder) { Recorder = NewObject<UClearanceSessionRecorder>(this); }
	DISEmitter = NewObject<UClearanceDISEmitter>(this);
	DISReceiver = NewObject<UClearanceDISReceiver>(this);
	DDSEmitter = NewObject<UClearanceDDSEmitter>(this);
	DDSReceiver = NewObject<UClearanceDDSReceiver>(this);
	RTIEmitter = NewObject<UClearanceRTIEmitter>(this);
	HLAEmitter = NewObject<UClearanceHLAEmitter>(this);
	Radar = NewObject<UClearanceRadar>(this);
	if (Radar)
	{
		Radar->SetReferences(AirspaceManager);
		Radar->RangeNm               = CentralRadarRangeNm;
		Radar->SweepRpm              = CentralRadarSweepRpm;
		Radar->SecondaryReturnChance = CentralRadarSecondaryReturnChance;
		Radar->PositionJitterNm      = CentralRadarPositionJitterNm;
		Radar->TrackFadeSeconds      = CentralRadarTrackFadeSeconds;
		Radar->SitePositionNm        = FVector2D::ZeroVector; // sector origin
		Radar->SiteName              = CentralRadarSiteName;
	}

	ScenarioRunner = NewObject<UClearanceScenarioRunner>(this);
	if (ScenarioRunner) { ScenarioRunner->SetReferences(this, AirspaceManager); }

	if (AirspaceManager)
	{
		// Mirror the world-frame offset onto the Manager so per-aircraft
		// Behaviour UObjects can convert operator magnetic-frame instructions
		// back to the sim's internal frame at the point they hit the aircraft
		// state, without needing a controller reference. - TripleA
		AirspaceManager->WorldNorthOffsetDeg = WorldNorthOffsetDeg;

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
			// LandingHeadingDeg is authored in the sim's internal frame (i.e. the
			// value that makes the strip visually align with the ground below).
			// The magnetic designator falls out of the same value via
			// ApplyWorldNorthOffset at display time, so ATC readbacks still say
			// "RWY 07" even when the internal heading is 340. - TripleA
			const float H = It->LandingHeadingDeg;
			const float HRad = FMath::DegreesToRadians(H);
			const FVector2D Inbound(FMath::Sin(HRad), FMath::Cos(HRad)); // direction flown to land on H

			// Centre, length and ground height all come from the runway MESH bounds, so
			// placing and scaling the mesh moves and sizes the runway - one object drives
			// the touchdown points, the markers and everything else. When the actor has
			// explicit OverrideLengthUnits / OverrideWidthUnits set, take them directly
			// rather than reconstructing from the AABB - the AABB of an oriented long-and-
			// thin rectangle at an oblique heading (Warton's 290 is a 50° tilt from world
			// axes) is nearly square, so projecting it back leaks length into width and
			// the sim ends up drawing a 1500 m wide runway. - TripleA
			// Threshold is the actor's root (Threshold component) - that's what
			// moves when the level designer drags the actor. Mesh is decorative,
			// used only for length/width; using the mesh's world centre here
			// leaked mesh-local offsets into the threshold and made the runway
			// "not move in game" when the actor was repositioned. - TripleA
			const FVector ActorLoc = It->GetActorLocation();
			float LengthW = 1600.f; // fallback (~1.6nm) until a mesh is assigned
			float WidthW  = 4500.f; // fallback ~45m strip
			float TopZ = ActorLoc.Z;

			if (It->OverrideLengthUnits > 0.f && It->OverrideWidthUnits > 0.f)
			{
				LengthW = It->OverrideLengthUnits;
				WidthW  = It->OverrideWidthUnits;
			}
			else
			{
				FVector MeshCentre, MeshExtent;
				if (It->GetRunwayBounds(MeshCentre, MeshExtent))
				{
					LengthW = 2.f * (MeshExtent.X * FMath::Abs(Inbound.X) + MeshExtent.Y * FMath::Abs(Inbound.Y));
					// Perpendicular to the heading - swaps the axis the projection
					// reads. - TripleA
					const FVector Perp(FMath::Cos(HRad), -FMath::Sin(HRad), 0.f);
					WidthW = 2.f * (MeshExtent.X * FMath::Abs(Perp.X) + MeshExtent.Y * FMath::Abs(Perp.Y));
					TopZ = MeshCentre.Z + MeshExtent.Z;
				}
			}

			// Actor location IS the near threshold. Reciprocal end sits one
			// full length inbound down the runway. - TripleA
			const FVector2D ThresholdNearNm((ActorLoc.X - Origin.X) / WorldUnitsPerNm, (ActorLoc.Y - Origin.Y) / WorldUnitsPerNm);
			const float FullLenNm = LengthW / WorldUnitsPerNm;
			const FVector2D CentreNm = ThresholdNearNm + Inbound * (FullLenNm * 0.5f);
			const float HalfNm = FullLenNm * 0.5f;

			// Landing on H, you cross the near threshold (behind the centre) and roll
			// through; the reciprocal lands the other way from the far end. - TripleA
			// The actor's DesignatorOverride (1-36) forces the primary label at
			// this end; reciprocal = (this + 18) mod 36 with the 0->36 fold. When
			// the override is 0 the draw code falls back to the heading-based
			// formula. - TripleA
			auto ReciprocalOf = [](int32 N) -> int32
			{
				if (N <= 0) { return 0; }
				int32 R = (N + 18) % 36;
				return (R == 0) ? 36 : R;
			};

			FRunwayInfo A;
			A.ThresholdNm = CentreNm - Inbound * HalfNm;
			A.HeadingDeg = H;
			A.LengthUnits = LengthW;
			A.WidthUnits  = WidthW;
			A.DesignatorOverride = FMath::Clamp(It->DesignatorNumberOverride, 0, 36);
			UE_LOG(LogTemp, Warning, TEXT("[Runway] REGISTER actor=%s LandingHeadingDeg=%.1f OverrideProp=%d -> RunwayInfo{HeadingDeg=%.1f, DesignatorOverride=%d}"),
				*It->GetName(), It->LandingHeadingDeg, It->DesignatorNumberOverride,
				A.HeadingDeg, A.DesignatorOverride);
			RunwayInfos.Add(A);
			if (It->bAllowReciprocal)
			{
				FRunwayInfo B;
				B.ThresholdNm = CentreNm + Inbound * HalfNm;
				B.HeadingDeg = FMath::Fmod(H + 180.f, 360.f);
				B.LengthUnits = LengthW;
				B.WidthUnits  = WidthW;
				B.DesignatorOverride = ReciprocalOf(A.DesignatorOverride);
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
	if (CommsRouter)
	{
		CommsRouter->SetReferences(AirspaceManager, Validator);
		// Subscribe to instruction results so the comms transcript can capture
		// the operator command + auto-generate the pilot readback / refusal.
		// AddUniqueDynamic so re-initialising doesn't double-up. - TripleA
		CommsRouter->OnInstructionResult.AddUniqueDynamic(this, &AClearanceSimulationController::HandleInstructionResult);
	}

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

	// Intentionally NOT resetting SessionTime - it ticks from server BeginPlay
	// so the HUD timer shows total elapsed PIE time, surviving multiple
	// StartSession calls. Per-scenario timing relative to start is captured
	// elsewhere (recorder, scoring incidents stamp themselves at the current
	// SessionTime so durations come out correct regardless of absolute value).
	// - TripleA
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

void AClearanceSimulationController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AClearanceSimulationController, AirspaceManager);
	DOREPLIFETIME(AClearanceSimulationController, bRepScenarioRunning);
	DOREPLIFETIME(AClearanceSimulationController, RepScenarioName);
	DOREPLIFETIME(AClearanceSimulationController, RepScenarioElapsedSec);
	DOREPLIFETIME(AClearanceSimulationController, RepScenarioFiredEvents);
	DOREPLIFETIME(AClearanceSimulationController, RepScenarioTotalEvents);
	DOREPLIFETIME(AClearanceSimulationController, RepScenarioFiredTriggers);
	DOREPLIFETIME(AClearanceSimulationController, RepScenarioTotalTriggers);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreTotal);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreEfficiencyPct);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreLandings);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreHandoffs);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreResolved);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreIntercepts);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreEmergencies);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreGoArounds);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreSepLoss);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreWake);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreTCAS);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreStrayed);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreMisID);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreViolated);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreCrashed);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreBusted);
	DOREPLIFETIME(AClearanceSimulationController, RepScoreNextSpawnSec);
	DOREPLIFETIME(AClearanceSimulationController, RepScoringLog);
	DOREPLIFETIME(AClearanceSimulationController, RepOperatorTracks);
	DOREPLIFETIME(AClearanceSimulationController, RepCheckpoints);
	DOREPLIFETIME(AClearanceSimulationController, RepDISPacketsSent);
	DOREPLIFETIME(AClearanceSimulationController, RepDISPacketsReceived);
	DOREPLIFETIME(AClearanceSimulationController, RepDDSPacketsSent);
	DOREPLIFETIME(AClearanceSimulationController, RepDDSPacketsReceived);
	DOREPLIFETIME(AClearanceSimulationController, bRepDISEmitting);
	DOREPLIFETIME(AClearanceSimulationController, bRepDISReceiving);
	DOREPLIFETIME(AClearanceSimulationController, bRepDDSEmitting);
	DOREPLIFETIME(AClearanceSimulationController, bRepDDSReceiving);
	DOREPLIFETIME(AClearanceSimulationController, RepRTIPacketsSent);
	DOREPLIFETIME(AClearanceSimulationController, bRepRTIEmitting);
	DOREPLIFETIME(AClearanceSimulationController, RepHLAUpdatesSent);
	DOREPLIFETIME(AClearanceSimulationController, bRepHLAJoined);
	DOREPLIFETIME(AClearanceSimulationController, SessionTime);
	DOREPLIFETIME(AClearanceSimulationController, RepNotifications);
	DOREPLIFETIME(AClearanceSimulationController, CameraOverview);
	DOREPLIFETIME(AClearanceSimulationController, CameraTower);
	DOREPLIFETIME(AClearanceSimulationController, CameraApproach);
	DOREPLIFETIME(AClearanceSimulationController, CameraFollow);
	DOREPLIFETIME(AClearanceSimulationController, OperatorViewRotation);
	DOREPLIFETIME(AClearanceSimulationController, OperatorViewLocation);
	DOREPLIFETIME(AClearanceSimulationController, bReplayMode);
	DOREPLIFETIME(AClearanceSimulationController, bReplayPaused);
	DOREPLIFETIME(AClearanceSimulationController, ReplayTime);
	DOREPLIFETIME(AClearanceSimulationController, ReplaySpeed);
	DOREPLIFETIME(AClearanceSimulationController, ReplayDuration);
	DOREPLIFETIME(AClearanceSimulationController, ReplaySegmentSeams);
	DOREPLIFETIME(AClearanceSimulationController, Transcript);
}

void AClearanceSimulationController::PushNotification(const FString& Text, FColor Colour, float LifetimeSec)
{
	// Server is the sole writer; clients receive via replication. Trim oldest to
	// keep the buffer bounded - the HUD only draws the recent dozen anyway. - TripleA
	if (!HasAuthority()) { return; }

	FClearanceNotification N;
	N.Text = Text;
	// Session clock (freezes on pause / scales in replay), not engine world time -
	// so event-log rows display the same MM:SS as the session timer. - TripleA
	N.ServerTimeAdded = SessionTime;
	N.Colour = Colour;
	N.LifetimeSec = LifetimeSec;
	RepNotifications.Add(N);

	// Cap replicated ring buffer size. Client instructor panel event log
	// caps its own render at MaxNotifications (currently 16), so anything
	// above ~40 wastes bandwidth without being visible. - TripleA
	while (RepNotifications.Num() > 40)
	{
		RepNotifications.RemoveAt(0);
	}

	// Anything escalated to a HUD notification (conflicts, separation losses,
	// intercept results, emergency advisories, scoring events) is by definition
	// significant enough for the AAR transcript. Logged as System since these
	// originate from the simulation, not a specific operator/pilot transmission.
	// - TripleA
	AppendTranscriptEntry(EClearanceCommsRole::System, NAME_None, Text);
}

// Multicast: pilot voice + mayday lines. Fires on the server and every
// connected client. Each peer finds its own placed VoiceOutput actor (which
// owns a local TTS server) and synthesises the line locally - no PCM ships
// over the wire. Silent no-op on a peer that has no VoiceOutput placed. - TripleA
void AClearanceSimulationController::Multicast_PlayTTS_Implementation(FName Callsign, const FString& Text, const FString& VoiceTag, bool bPanic)
{
	UWorld* World = GetWorld();
	if (!World || Text.IsEmpty()) { return; }

	// Single transcription point - anything voiced over the radio lands in the
	// transcript. Server-only so peers don't duplicate.
	// Role rules:
	//   - no callsign           -> System (controller voice, broadcasts)
	//   - registered aircraft   -> Pilot (the actual aircraft is speaking)
	//   - facility callsign     -> that facility's own role (Tower / Acc / Awacs
	//     / Gci / Atis / Met) so the transcript shows each in its own color
	//     instead of collapsing them all under SYS. - TripleA
	if (HasAuthority())
	{
		EClearanceCommsRole TranscriptRole = EClearanceCommsRole::System;
		if (!Callsign.IsNone())
		{
			// bPanic = crash panic + final-words lines. The final line fires
			// on a 14-second delayed timer, by which point the aircraft has
			// already been deregistered from AirspaceManager (the crash
			// itself deregisters), so IsCallsignRegistered would falsely
			// route it to System. But the speaker is still the pilot - dying
			// over the radio is a Pilot transmission, not a system event.
			// - TripleA
			const bool bIsAircraft = bPanic
				|| (AirspaceManager && AirspaceManager->IsCallsignRegistered(Callsign));
			if (bIsAircraft)
			{
				TranscriptRole = EClearanceCommsRole::Pilot;
			}
			else
			{
				const FString Cs = Callsign.ToString().ToUpper();
				if      (Cs == TEXT("TOWER") || Cs == TEXT("TWR"))    { TranscriptRole = EClearanceCommsRole::Tower; }
				else if (Cs == TEXT("ACC"))                            { TranscriptRole = EClearanceCommsRole::Acc; }
				else if (Cs == TEXT("AWACS"))                          { TranscriptRole = EClearanceCommsRole::Awacs; }
				else if (Cs == TEXT("GCI"))                            { TranscriptRole = EClearanceCommsRole::Gci; }
				else if (Cs == TEXT("ATIS"))                           { TranscriptRole = EClearanceCommsRole::Atis; }
				else if (Cs == TEXT("MET"))                            { TranscriptRole = EClearanceCommsRole::Met; }
				else                                                    { TranscriptRole = EClearanceCommsRole::System; }
			}
		}
		AppendTranscriptEntry(TranscriptRole, Callsign, Text);
	}

	for (TActorIterator<AClearanceVoiceOutput> It(World); It; ++It)
	{
		if (!*It) { break; }
		if (bPanic) { It->SpeakPanic(Callsign, Text, VoiceTag); }
		else        { It->Speak(Callsign, Text, VoiceTag); }
		break;
	}
}

// Multicast: non-spoken cockpit cues - radio static (Kind=0), GPWS terrain
// alarm start (Kind=1), GPWS stop on impact (Kind=2). Same lookup pattern as
// Multicast_PlayTTS. - TripleA
void AClearanceSimulationController::Multicast_PlayCockpitCue_Implementation(FName Callsign, uint8 Kind, float Duration)
{
	UWorld* World = GetWorld();
	if (!World) { return; }
	for (TActorIterator<AClearanceVoiceOutput> It(World); It; ++It)
	{
		if (!*It) { break; }
		switch (Kind)
		{
		case 0: It->PlayStatic(Duration > 0.f ? Duration : 1.f); break;
		case 1: It->PlayGPWS(Callsign);                          break;
		case 2: It->StopGPWS(Callsign);                          break;
		default: break;
		}
		break;
	}
}

void AClearanceSimulationController::OnRep_AirspaceManager()
{
	// Client-only: as soon as the replicated Manager ref arrives, bind the
	// visual-spawning delegates so meshes appear when aircraft replicate in. - TripleA
	if (HasAuthority() || !AirspaceManager) { return; }
	AirspaceManager->OnAircraftRegistered.AddDynamic(this, &AClearanceSimulationController::HandleAircraftRegistered);
	AirspaceManager->OnAircraftDeregistered.AddDynamic(this, &AClearanceSimulationController::HandleAircraftDeregistered);

	// Any aircraft already replicated before this OnRep fired never sent us a
	// register event - call the handler for each so meshes spawn retroactively. - TripleA
	for (const FAircraftState& S : AirspaceManager->GetAllAircraftStates())
	{
		HandleAircraftRegistered(S.Callsign);
	}
}

void AClearanceSimulationController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Instructor PIP feed runs everywhere - host renders its own, each client
	// renders its own. The RT is a per-machine resource so the capture has to
	// happen wherever the panel wants to display it. - TripleA
	UpdateInstructorPip(DeltaTime);


	// Client-side render path: just paint the world from the replicated state.
	// No sim mutations; the server is the only writer. - TripleA
	if (!HasAuthority())
	{
		if (AirspaceManager) // replicated ref may take a tick or two to arrive
		{
			UpdateVisuals();
			UpdateFollowCamera();
			DrawDebugView();

			// Client-side snapshot capture: feeds the local recorder buffer
			// with whatever airspace state the client received via replication
			// this frame, so the panel's REPLAY tab can scrub through history
			// without an RPC round-trip per click. - TripleA
			if (Recorder && Recorder->IsRecording() && GetWorld())
			{
				Recorder->CaptureSnapshot(GetWorld()->GetTimeSeconds(), AirspaceManager->GetAllAircraftStates());
			}
		}
		return;
	}

	// SessionTime ticks pre-StartSession so the HUD shows elapsed time the
	// moment the controller is alive on the server (instructor watches it
	// count up while setting up a scenario, no "press start" friction).
	// Frozen during in-sim pause and during replay - that's why the increment
	// sits between those two gates and the bSessionActive gate, not below
	// them. - TripleA
	if (!bPaused && !bReplayMode)
	{
		SessionTime += DeltaTime;
	}

	if (!bSessionActive || bPaused)
	{
		return;
	}

	if (bReplayMode)
	{
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
		// Re-fuse operator tracks each replay tick so the operator scope on
		// the instructor panel follows the replayed world. Without this,
		// RepOperatorTracks goes stale at the moment EnterReplay fires and
		// the operator scope appears frozen during replay even though the
		// radar sites + central radar are repainting. - TripleA
		RefreshOperatorTracks();
		// DIS receiver still polls in replay - a partner sim watching the same debrief
		// can still drop traffic on our scope. - TripleA
		if (DISReceiver && DISReceiver->IsRunning() && AirspaceManager && GetWorld())
		{
			DISReceiver->Poll(AirspaceManager, GetWorld()->GetRealTimeSeconds());
		}
		if (DDSReceiver && DDSReceiver->IsRunning() && AirspaceManager && GetWorld())
		{
			DDSReceiver->TickDrain(AirspaceManager, GetWorld()->GetRealTimeSeconds());
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
	if (DDSReceiver && DDSReceiver->IsRunning() && AirspaceManager && GetWorld())
	{
		DDSReceiver->TickDrain(AirspaceManager, GetWorld()->GetRealTimeSeconds());
	}

	const float SimDelta = DeltaTime * FMath::Max(0.f, SimulationTimeScale);

	// Scenario runner advances on sim time so a 10x time-scaled session reaches
	// scripted moments 10x faster - matches authoring intuition. - TripleA
	if (ScenarioRunner && ScenarioRunner->IsRunning()) { ScenarioRunner->Tick(SimDelta, DeltaTime); }

	// Phase 3: mirror ScenarioRunner state to replicated UPROPERTYs so clients
	// see the SCENARIO readout line without needing a ScenarioRunner instance. - TripleA
	if (ScenarioRunner)
	{
		bRepScenarioRunning      = ScenarioRunner->IsRunning();
		RepScenarioName          = ScenarioRunner->GetLoadedName();
		RepScenarioElapsedSec    = ScenarioRunner->GetElapsedSeconds();
		RepScenarioFiredEvents   = ScenarioRunner->GetFiredEventCount();
		RepScenarioTotalEvents   = ScenarioRunner->GetTotalEventCount();
		RepScenarioFiredTriggers = ScenarioRunner->GetFiredTriggerCount();
		RepScenarioTotalTriggers = ScenarioRunner->GetTotalTriggerCount();
	}
	else
	{
		bRepScenarioRunning = false;
	}

	// Phase 4: mirror Scoring state to replicated UPROPERTYs so clients see
	// the SCORING line + main CLEARANCE score/eff with server truth. Per-incident
	// counters aren't exposed as getters - tally them from the session log here
	// (same loop the readout used to do client-side) and ship the totals. - TripleA
	if (Scoring)
	{
		RepScoreTotal         = Scoring->GetCurrentScore();
		RepScoreEfficiencyPct = Scoring->GetEfficiency() * 100.f;
		RepScoreNextSpawnSec  = Scoring->GetCurrentSpawnInterval();

		int32 nLand = 0, nHand = 0, nRes = 0, nGA = 0, nSep = 0, nExit = 0, nWake = 0, nTCAS = 0, nInt = 0, nMisID = 0, nViol = 0, nEmer = 0, nCrash = 0, nBust = 0;
		for (const FIncidentRecord& R : Scoring->GetSessionLog())
		{
			switch (R.Type)
			{
			case EIncidentType::SuccessfulLanding:    ++nLand; break;
			case EIncidentType::SuccessfulHandoff:    ++nHand; break;
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
		RepScoreLandings    = nLand;
		RepScoreHandoffs    = nHand;
		RepScoreResolved    = nRes;
		RepScoreIntercepts  = nInt;
		RepScoreEmergencies = nEmer;
		RepScoreGoArounds   = nGA;
		RepScoreSepLoss     = nSep;
		RepScoreWake        = nWake;
		RepScoreTCAS        = nTCAS;
		RepScoreStrayed     = nExit;
		RepScoreMisID       = nMisID;
		RepScoreViolated    = nViol;
		RepScoreCrashed     = nCrash;
		RepScoreBusted      = nBust;

		// Mirror the full ordered scoring log to clients. Cheap - one TArray copy
		// per scoring update, log only grows on actual incidents. Performance tab
		// reads from this to render per-category "[mm:ss] CALLSIGN" rows. - TripleA
		RepScoringLog = Scoring->GetSessionLog();
	}

	// Operator scope mirror - fuse every enabled radar site's tracks into one
	// list per truth callsign and replicate. Instructor panel's operator-scope
	// sub-mode paints from this, so the instructor sees the trainee's actual
	// radar (degraded by EW, populated with chaff ghosts) instead of truth.
	// - TripleA
	RefreshOperatorTracks();

	// Violation zone check: any declared-hostile aircraft inside a protected zone
	// fires a catastrophic ViolationZoneBreached incident (mirror of mis-ID). One-
	// shot per (zone, aircraft) pair so a stuck hostile doesn't spam the score. - TripleA
	if (AirspaceManager && GetWorld() && !bZoneChecksSuspended)
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
					{
						const FString NMsg = FString::Printf(TEXT("VIOLATION: hostile %s reached %s (-%d)"),
							*S.Callsign.ToString(), *Z->ZoneName.ToString(),
							Scoring ? Scoring->PenaltyViolationZoneBreached : 1000);
						PushNotification(NMsg, FColor::Red, 30.f);
						if (GEngine)
						{
							GEngine->AddOnScreenDebugMessage(102, 30.f, FColor::Red, NMsg);
						}
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
	if (AirspaceManager && GetWorld() && !bZoneChecksSuspended)
	{
		TArray<AClearanceRestrictedArea*> Areas;
		for (TActorIterator<AClearanceRestrictedArea> AIt(GetWorld()); AIt; ++AIt) { Areas.Add(*AIt); }
		if (Areas.Num() > 0)
		{
			const FVector OriginW = GetActorLocation();
			const float W = FMath::Max(1.f, WorldUnitsPerNm);
			for (const FAircraftState& S : AirspaceManager->GetAllAircraftStates())
			{
				// Only civilian traffic - military, hostiles, hijacks etc are
				// handled by other paths (or are LEGITIMATELY in the area).
				// Civilians are Neutral per MIL-STD-2525C affiliation. - TripleA
				if (S.bIsMilitary || S.bIsExternal || S.ThreatClass != EThreatClass::Neutral) { continue; }
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
					{
						const FString NMsg = FString::Printf(TEXT("AIRSPACE BUST: %s entered %s (-%d)"),
							*S.Callsign.ToString(), *A->AreaName.ToString(),
							Scoring ? Scoring->PenaltyRestrictedAirspaceBust : 150);
						PushNotification(NMsg, FColor(255, 140, 0), 6.f);
						if (GEngine)
						{
							GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor(255, 140, 0), NMsg);
						}
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
						Recorder->LogEvent(SessionTime, FString::Printf(TEXT("EMERGENCY - %s declared %s"),
							*S.Callsign.ToString(), *UEnum::GetDisplayValueAsText(S.ActiveEmergency).ToString()));
					}
					{
						const FColor Col = (S.ActiveEmergency == EEmergencyType::Hijack) ? FColor::Red : FColor::Yellow;
						const FString NMsg = FString::Printf(TEXT("EMERGENCY: %s %s"),
							*S.Callsign.ToString(), *UEnum::GetDisplayValueAsText(S.ActiveEmergency).ToString());
						PushNotification(NMsg, Col, 8.f);
						if (GEngine)
						{
							GEngine->AddOnScreenDebugMessage(-1, 8.f, Col, NMsg);
						}
					}

					// Audio cue via any placed VoiceOutput:
					//   Mayday / FuelLow -> voice the declaration (different lines)
					//   CommsFailure     -> short static burst (broken radio)
					//   Hijack           -> very brief static (pilot keyed mic and was cut off)
					// The static cue for hijack is a small concession to gameplay over
					// strict doctrine; pure silence is more authentic but leaves the
					// player wondering if anything happened. Routed through the
					// multicast so every peer hears the declaration, not just whichever
					// machine the server-side iterator happened to land on. - TripleA
					AnnounceEmergency(S.Callsign, S.ActiveEmergency, S.EmergencyDetail);
					continue;
				}
			}

			// Countdown tick + crash for any emergency carrying a timer (fuel or
			// mayday). Hijack and comms failure don't time out by themselves.
			// Deliberately uses wall-clock DeltaTime, NOT SimDelta - the timer
			// measures operator decision time, not sim-world fuel burn. Instructor's
			// speed changes shouldn't shrink or stretch how long the trainee has
			// to work the emergency. - TripleA
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
							Multicast_PlayTTS(Cs, Line, FString(), false);
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

	if (AirspaceManager) { AirspaceManager->SetSimulationTimeScale(SimulationTimeScale); }
	StepSimulation(SimDelta);

	// Capture the post-tick state into the recording timeline.
	if (Recorder && Recorder->IsRecording() && AirspaceManager)
	{
		Recorder->CaptureSnapshot(SessionTime, AirspaceManager->GetAllAircraftStates());
	}

	// Publish each aircraft as a DIS Entity State PDU AND a DDS AircraftState
	// topic sample. Runs in both live and replay. Two wires (DIS legacy +
	// DDS pub/sub), same six data primitives, single tick. - TripleA
	// Mirror server-side subobject counts onto replicated fields so the
	// client's instructor panel can read them without dereferencing the
	// unreplicated emitter/receiver pointers. Runs unconditionally each tick
	// so counts also drop back to 0 when a wire is Stop'd (the underlying
	// emitter resets its counter on Stop). - TripleA
	RepDISPacketsSent     = DISEmitter  ? DISEmitter->GetLastPacketsSent()      : 0;
	RepDISPacketsReceived = DISReceiver ? DISReceiver->GetLastPacketsReceived() : 0;
	RepDDSPacketsSent     = DDSEmitter  ? DDSEmitter->GetTotalPublishedCount()  : 0;
	RepDDSPacketsReceived = DDSReceiver ? DDSReceiver->GetTotalIngestedCount()  : 0;
	RepRTIPacketsSent     = RTIEmitter  ? RTIEmitter->GetTotalPublishedCount()  : 0;
	RepHLAUpdatesSent     = HLAEmitter  ? HLAEmitter->GetTotalUpdatesCount()    : 0;
	bRepDISEmitting  = DISEmitter  && DISEmitter->IsRunning();
	bRepDISReceiving = DISReceiver && DISReceiver->IsRunning();
	bRepDDSEmitting  = DDSEmitter  && DDSEmitter->IsRunning();
	bRepDDSReceiving = DDSReceiver && DDSReceiver->IsRunning();
	bRepRTIEmitting  = RTIEmitter  && RTIEmitter->IsRunning();
	bRepHLAJoined    = HLAEmitter  && HLAEmitter->IsJoined();

	const bool bDISOn = DISEmitter && DISEmitter->IsRunning();
	const bool bDDSOn = DDSEmitter && DDSEmitter->IsRunning();
	const bool bRTIOn = RTIEmitter && RTIEmitter->IsRunning();
	const bool bHLAOn = HLAEmitter && HLAEmitter->IsJoined();
	if ((bDISOn || bDDSOn || bRTIOn || bHLAOn) && AirspaceManager)
	{
		if (bDISOn) { DISEmitter->EmitStates(AirspaceManager->GetAllAircraftStates(), SessionTime); }
		if (bDDSOn) { DDSEmitter->EmitStates(AirspaceManager->GetAllAircraftStates(), SessionTime); }
		if (bRTIOn) { RTIEmitter->EmitStates(AirspaceManager->GetAllAircraftStates(), SessionTime); }
		if (bHLAOn) { HLAEmitter->EmitStates(AirspaceManager->GetAllAircraftStates(), SessionTime); }

		// Also publish an Emission PDU per active radar so external ELINT
		// receivers see WHICH radars are up, what they look like on RF, and
		// which aircraft each one is currently painting. Ground truth of the
		// sensor picture, mirrored onto the federation. - TripleA
		TArray<FRadarEmissionSnapshot> Emissions;
		auto CaptureFromRadar = [&](UClearanceRadar* R)
		{
			if (!R || !R->IsEnabled()) { return; }
			FRadarEmissionSnapshot S;
			S.SiteName        = R->SiteName;
			S.SitePositionNm  = R->SitePositionNm;
			S.Signature       = R->EmissionSignature;
			S.SweepAngleDeg   = R->GetSweepAngleDeg();
			S.RangeNm         = R->RangeNm;
			S.bEnabled        = true;
			for (const FRadarTrack& T : R->GetTracks())
			{
				if (!T.TruthCallsign.IsNone())
				{
					S.PaintedCallsigns.Add(T.TruthCallsign);
				}
			}
			Emissions.Add(MoveTemp(S));
		};

		CaptureFromRadar(Radar);   // controller-owned centre radar
		if (UWorld* W = GetWorld())
		{
			for (TActorIterator<AClearanceRadarSite> SIt(W); SIt; ++SIt)
			{
				if (AClearanceRadarSite* Site = *SIt)
				{
					CaptureFromRadar(Site->Radar);
				}
			}
		}
		if (bDISOn) { DISEmitter->EmitEmissions(Emissions, SessionTime); }
		if (bDDSOn) { DDSEmitter->EmitEmissions(Emissions, SessionTime); }

		// Publish a Transmitter PDU per active radio so a federation receiver
		// knows who is on the air and can tune to their frequency before
		// hearing the audio traffic carried by the Signal PDU. The transmit
		// state is derived from PendingVoiceEvents: any owner with a pending
		// voice line this tick is currently transmitting; everyone else is
		// on-idle. Operator gets one heartbeat radio too. §7.7.2. - TripleA
		{
			TSet<FName> ActiveTransmitters;
			bool bOperatorTransmitting = false;
			for (const FVoiceCommsEvent& V : PendingVoiceEvents)
			{
				if (V.SpeakerCallsign.IsNone()) { bOperatorTransmitting = true; }
				else                            { ActiveTransmitters.Add(V.SpeakerCallsign); }
			}

			TArray<FRadioTransmitter> Radios;
			Radios.Reserve(AirspaceManager->GetAllAircraftStates().Num() + 1);
			for (const FAircraftState& S : AirspaceManager->GetAllAircraftStates())
			{
				if (!S.bIsValid) { continue; }
				FRadioTransmitter R;
				R.OwnerCallsign      = S.Callsign;
				R.RadioId            = 1;
				R.FrequencyHz        = 121500000;   // ATC guard channel default
				R.BandwidthHz        = 25000.f;     // VHF AM airband spacing
				R.PowerDbm           = 43.f;        // ~20 W typical airliner VHF
				R.TransmitState      = ActiveTransmitters.Contains(S.Callsign) ? 2 : 1;
				R.AntennaWorldMeters = FVector(S.Position.X * 1852.0, S.Position.Y * 1852.0, S.Altitude * 0.3048);
				Radios.Add(MoveTemp(R));
			}
			// Operator / ground-station heartbeat.
			{
				FRadioTransmitter R;
				R.OwnerCallsign      = NAME_None;
				R.RadioId            = 1;
				R.FrequencyHz        = 121500000;
				R.BandwidthHz        = 25000.f;
				R.PowerDbm           = 50.f;        // 100 W tower transmitter
				R.TransmitState      = bOperatorTransmitting ? 2 : 1;
				R.AntennaWorldMeters = FVector::ZeroVector;   // sector centre
				Radios.Add(MoveTemp(R));
			}
			if (bDISOn) { DISEmitter->EmitTransmitters(Radios, SessionTime); }
			if (bDDSOn) { DDSEmitter->EmitTransmitters(Radios, SessionTime); }
		}

		// Drain queued Fire + Detonation + Voice events. Sim event points
		// (scramble launch, intercept resolution, transcribed comms) enqueue
		// into these buffers; the emit tick publishes them once per active
		// wire, then clears the queues. Both wires see the same events. - TripleA
		if (PendingFireEvents.Num() > 0)
		{
			if (bDISOn) { DISEmitter->EmitFireEvents(PendingFireEvents, SessionTime); }
			if (bDDSOn) { DDSEmitter->EmitFireEvents(PendingFireEvents, SessionTime); }
			PendingFireEvents.Reset();
		}
		if (PendingDetonationEvents.Num() > 0)
		{
			if (bDISOn) { DISEmitter->EmitDetonationEvents(PendingDetonationEvents, SessionTime); }
			if (bDDSOn) { DDSEmitter->EmitDetonationEvents(PendingDetonationEvents, SessionTime); }
			PendingDetonationEvents.Reset();
		}
		if (PendingVoiceEvents.Num() > 0)
		{
			if (bDISOn) { DISEmitter->EmitVoiceEvents(PendingVoiceEvents, SessionTime); }
			if (bDDSOn) { DDSEmitter->EmitVoiceEvents(PendingVoiceEvents, SessionTime); }
			PendingVoiceEvents.Reset();
		}
	}

	// Radar sees truth and produces tracks (what the operator gets to see).
	if (Radar && Radar->IsEnabled()) { Radar->Tick(DeltaTime); }

	UpdateFollowCamera();
}

// Hostiles (and military Unknowns) react to closing friendly interceptors:
// jammer on at 25nm, chaff at 10nm with an 8s reload, jammer off if everyone
// disengages out past 40nm. Cooldowns are wall-clock so a tight intercept
// chain doesn't trigger ten cycles in a row. - TripleA
void AClearanceSimulationController::TickBanditEW(float DeltaTime)
{
	if (!AirspaceManager) { return; }

	const TArray<FAircraftState> All = AirspaceManager->GetAllAircraftStates();

	TArray<FVector> InterceptorPositions;
	for (const FAircraftState& A : All)
	{
		if (A.ThreatClass == EThreatClass::Friendly && A.bIsMilitary && A.bUnderGCIControl)
		{
			InterceptorPositions.Add(A.Position);
		}
	}
	if (InterceptorPositions.Num() == 0) { return; }

	constexpr float JamOnRangeNm    = 25.f;
	constexpr float ChaffRangeNm    = 10.f;
	constexpr float JamOffRangeNm   = 40.f;
	constexpr float JamCooldownSec  =  5.f;
	constexpr float ChaffReloadSec  =  8.f;
	const float Now = SessionTime;

	for (const FAircraftState& B : All)
	{
		const bool bEligible = (B.ThreatClass == EThreatClass::Hostile)
			|| (B.ThreatClass == EThreatClass::Unknown && B.bIsMilitary);
		if (!bEligible) { continue; }

		float ClosestNm = TNumericLimits<float>::Max();
		for (const FVector& P : InterceptorPositions)
		{
			const float D = FVector::Dist2D(B.Position, P);
			if (D < ClosestNm) { ClosestNm = D; }
		}

		FBanditEWState& Tac = BanditEWStates.FindOrAdd(B.Callsign);

		if (!B.bJammingOn && ClosestNm <= JamOnRangeNm
			&& (Now - Tac.LastJamToggleTime) > JamCooldownSec)
		{
			FAircraftState U = B;
			U.bJammingOn = true;
			AirspaceManager->RequestStateUpdate(U);
			Tac.LastJamToggleTime = Now;
			PushNotification(
				FString::Printf(TEXT("EW: %s lit up jammers"), *B.Callsign.ToString()),
				FColor(255, 80, 80), 5.f);
		}
		else if (B.bJammingOn && ClosestNm > JamOffRangeNm
			&& (Now - Tac.LastJamToggleTime) > JamCooldownSec * 2.f)
		{
			FAircraftState U = B;
			U.bJammingOn = false;
			AirspaceManager->RequestStateUpdate(U);
			Tac.LastJamToggleTime = Now;
		}

		if (ClosestNm <= ChaffRangeNm
			&& (Now - Tac.LastChaffDropTime) > ChaffReloadSec)
		{
			AirspaceManager->DropChaff(B.Position, B.Altitude);
			Tac.LastChaffDropTime = Now;
			PushNotification(
				FString::Printf(TEXT("EW: %s released chaff"), *B.Callsign.ToString()),
				FColor(255, 220, 80), 4.f);
		}
	}
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

			// Lead viper radio call the moment the flight forms up on
			// the target. Operator hears the successful join over the
			// radio, matches real GCI convention. - TripleA
			if (Grp.Value.Num() > 0)
			{
				const FName Lead = Grp.Value[0];
				const TCHAR* CallVerb = bShadow ? TEXT("shadowing") : TEXT("escorting");
				const FString Call = FString::Printf(
					TEXT("%s, formation established on %s, %s heading %03d"),
					*Lead.ToString(), *BanditCs.ToString(), CallVerb, FMath::RoundToInt(OutHdg));
				Multicast_PlayTTS(Lead, Call, FString(), /*bPanic=*/false);
				LogTranscriptLine(EClearanceCommsRole::Pilot, Lead, Call);
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
					// Stays in supersonic-dash territory all the way to the slot - the lerp
					// only bleeds it off inside the last 0.6nm. Matches the spawn-speed
					// bump to 900 kts so wingmen don't fall behind. - TripleA
					const float PursueKt = 900.f;
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

void AClearanceSimulationController::ClassifyAircraft(FName Callsign, EThreatClass NewClass, bool bAsInstructor)
{
	if (!AirspaceManager) { return; }
	FAircraftState S = AirspaceManager->GetAircraftState(Callsign);
	if (!S.bIsValid) { return; }

	// CATASTROPHIC DOCTRINE FAILURE: declaring a truly civilian contact as
	// hostile. Vincennes / KAL-007 territory. Reads ground truth (TrueAffiliation)
	// not the operator's belief so a bandit faking civilian IFF wouldn't make
	// the correct Hostile call count as a misID. The classification still goes
	// through - the player committed to it - but the consequences are permanent
	// and unmistakable.
	// Skipped when bAsInstructor is true: the instructor reclassifying via the
	// inject panel is god-mode scenario shaping, not the trainee committing a
	// doctrinal call. - TripleA
	if (!bAsInstructor &&
		NewClass == EThreatClass::Hostile && S.TrueAffiliation == EThreatClass::Neutral &&
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

	if (bAsInstructor)
	{
		// God-view shaping: the instructor is rewriting the ground truth, NOT
		// the operator's view. Truth scope reads TrueAffiliation so its symbol
		// updates; operator scope reads ThreatClass so it keeps whatever the
		// trainee last identified. This preserves the training scenario where
		// the instructor flips a contact's true disposition mid-session
		// without telegraphing it to the trainee. - TripleA
		S.TrueAffiliation = NewClass;
	}
	else
	{
		// Operator's own classification - changes their view only. TrueAffiliation
		// stays put so any mis-ID is still detected against the ground truth. - TripleA
		S.ThreatClass = NewClass;
	}
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

void AClearanceSimulationController::AnnounceEmergency(FName Callsign, EEmergencyType Kind, const FString& EmergencyDetail)
{
	// The static cue for hijack is a small concession to gameplay over strict
	// doctrine; pure silence is more authentic but leaves the player wondering
	// if anything happened. Routed through Multicast so every peer hears the
	// declaration, not just whichever machine the server-side iterator happened
	// to land on. - TripleA
	switch (Kind)
	{
	case EEmergencyType::GeneralMayday:
		Multicast_PlayTTS(Callsign,
			EmergencyDetail.IsEmpty()
				? FString::Printf(TEXT("Mayday, mayday, mayday, %s, declaring emergency, request immediate landing"), *Callsign.ToString())
				: FString::Printf(TEXT("Mayday, mayday, mayday, %s, %s, request immediate landing"), *Callsign.ToString(), *EmergencyDetail),
			FString(), false);
		break;
	case EEmergencyType::FuelLow:
		Multicast_PlayTTS(Callsign,
			FString::Printf(TEXT("Mayday, mayday, mayday, %s, fuel emergency, request immediate landing"), *Callsign.ToString()),
			FString(), false);
		break;
	case EEmergencyType::CommsFailure:
		Multicast_PlayCockpitCue(Callsign, 0, 2.0f);
		LogTranscriptLine(EClearanceCommsRole::System, Callsign,
			FString::Printf(TEXT("[radio silence - comms failure suspected, %s squawking 7600]"), *Callsign.ToString()));
		break;
	case EEmergencyType::Hijack:
		Multicast_PlayCockpitCue(Callsign, 0, 0.6f);
		LogTranscriptLine(EClearanceCommsRole::System, Callsign,
			FString::Printf(TEXT("[brief carrier - %s squawking 7500]"), *Callsign.ToString()));
		break;
	default: break;
	}
}

bool AClearanceSimulationController::DeclareEmergencyOn(FName Callsign, EEmergencyType Kind, float TimerMinutes)
{
	if (!AirspaceManager || Kind == EEmergencyType::None) { return false; }
	FAircraftState S = AirspaceManager->GetAircraftState(Callsign);
	if (!S.bIsValid) { return false; }
	// Missiles are inert wrt the emergency system - no squawk, no fuel,
	// no mayday phraseology - so quietly reject any injection attempt
	// rather than corrupting the missile state / triggering scoring
	// downstream. - TripleA
	if (S.bIsMissile) { return false; }
	if (S.ActiveEmergency != EEmergencyType::None) { return false; } // already emergency

	// Instructor override wins if > 0; otherwise fall through to the
	// class-default for the given emergency type. - TripleA
	const bool bHasOverride = TimerMinutes > 0.f;

	int32 NewSquawk = S.SquawkCode;
	switch (Kind)
	{
	case EEmergencyType::GeneralMayday:
		S.ActiveEmergency = EEmergencyType::GeneralMayday;
		NewSquawk = 7700;
		// Empty detail -> TTS uses the natural "declaring emergency" phrasing.
		// Scenario authors can seed a specific reason ("engine failure",
		// "hydraulic loss", etc) via Params if they want colour. - TripleA
		S.EmergencyDetail.Empty();
		S.FuelRemainingMinutes = bHasOverride ? TimerMinutes : MaydayTimeoutMinutes;
		break;
	case EEmergencyType::CommsFailure:
		S.ActiveEmergency = EEmergencyType::CommsFailure;
		NewSquawk = 7600;
		break;
	case EEmergencyType::Hijack:
		S.ActiveEmergency = EEmergencyType::Hijack;
		NewSquawk = 7500;
		break;
	case EEmergencyType::FuelLow:
		S.ActiveEmergency = EEmergencyType::FuelLow;
		S.FuelRemainingMinutes = bHasOverride ? TimerMinutes : FuelEmergencyMinutes;
		break;
	default:
		return false;
	}
	S.SquawkCode = NewSquawk;
	S.EmergencyDeclaredAtSeconds = SessionTime;

	// 7600 follows the same lost-comms auto-procedure as the randomly-injected path:
	// turn for the active runway, descend to pattern altitude, FlightPhase=Approach. - TripleA
	if (S.ActiveEmergency == EEmergencyType::CommsFailure)
	{
		const FSectorEnvironment Env = AirspaceManager->GetCurrentEnvironment();
		if (Env.ActiveRunwayHeading >= 0.f)
		{
			const FVector2D Threshold(Env.ActiveRunwayThreshold.X, Env.ActiveRunwayThreshold.Y);
			const FVector2D Here(S.Position.X, S.Position.Y);
			const float HRad = FMath::DegreesToRadians(Env.ActiveRunwayHeading);
			const FVector2D Loc(FMath::Sin(HRad), FMath::Cos(HRad));
			const FVector2D Gate = Threshold - Loc * 10.f;
			const FVector2D Toward = (Gate - Here).GetSafeNormal();
			S.TargetHeading = FMath::RadiansToDegrees(FMath::Atan2(Toward.X, Toward.Y));
			if (S.TargetHeading < 0.f) { S.TargetHeading += 360.f; }
			S.TargetAltitude = 3000.f;
			S.FlightPhase = EFlightPhase::Approach;
		}
	}

	AirspaceManager->RequestStateUpdate(S);
	if (Recorder)
	{
		Recorder->LogEvent(SessionTime, FString::Printf(
			TEXT("EMERGENCY (scripted) - %s declared %d squawk %d"),
			*Callsign.ToString(), (int32)Kind, NewSquawk));
	}

	// Voice / cockpit-cue audio - same call the random-tick path makes, so
	// instructor-injected emergencies sound identical to organic ones. - TripleA
	AnnounceEmergency(Callsign, Kind, S.EmergencyDetail);
	return true;
}

bool AClearanceSimulationController::ClearEmergencyOn(FName Callsign)
{
	if (!AirspaceManager) { return false; }
	FAircraftState S = AirspaceManager->GetAircraftState(Callsign);
	if (!S.bIsValid || S.ActiveEmergency == EEmergencyType::None) { return false; }

	S.ActiveEmergency = EEmergencyType::None;
	S.SquawkCode = 1200;            // VFR / civilian default
	S.EmergencyDetail.Empty();
	S.EmergencyDeclaredAtSeconds = 0.f;
	AirspaceManager->RequestStateUpdate(S);
	return true;
}

TArray<FOperatorEmergencyEntry> AClearanceSimulationController::GetActiveEmergencies() const
{
	TArray<FOperatorEmergencyEntry> Out;
	if (!AirspaceManager) { return Out; }

	for (const FAircraftState& S : AirspaceManager->GetAllAircraftStates())
	{
		if (!S.bIsValid || S.ActiveEmergency == EEmergencyType::None) { continue; }

		FOperatorEmergencyEntry E;
		E.Callsign    = S.Callsign;
		E.Type        = S.ActiveEmergency;
		E.SquawkCode  = S.SquawkCode;
		E.ThreatClass = S.ThreatClass;
		E.Detail      = S.EmergencyDetail;
		// Only the two timed emergencies expose a countdown - Hijack (7500)
		// and CommsFailure (7600) are indefinite states with no clock, so
		// they carry -1 and the widget shows "--:--". - TripleA
		E.TimerMinutesRemaining =
			(S.ActiveEmergency == EEmergencyType::GeneralMayday ||
			 S.ActiveEmergency == EEmergencyType::FuelLow)
			? S.FuelRemainingMinutes
			: -1.f;
		Out.Add(E);
	}

	// Sort by urgency: timerless emergencies (Hijack, NORDO) pinned at the
	// top; timed emergencies below them shortest-first. Two-key comparator
	// via a synthetic sort key - negative timers get a large positive
	// sentinel so they sort AHEAD of any real countdown. - TripleA
	Out.Sort([](const FOperatorEmergencyEntry& A, const FOperatorEmergencyEntry& B)
	{
		const float KA = (A.TimerMinutesRemaining < 0.f) ? -1.f : A.TimerMinutesRemaining;
		const float KB = (B.TimerMinutesRemaining < 0.f) ? -1.f : B.TimerMinutesRemaining;
		return KA < KB;
	});
	return Out;
}

// ============================================================================
// Instructor station - server-side RPCs called by client console / UMG. Each is
// a thin wrapper around the existing single-machine API; all sim writes stay
// authoritative server-side. - TripleA
// ============================================================================

bool AClearanceSimulationController::Server_InjectEmergency_Validate(FName Callsign, EEmergencyType Kind, float TimerMinutes)
{
	return Callsign != NAME_None;
}
void AClearanceSimulationController::Server_InjectEmergency_Implementation(FName Callsign, EEmergencyType Kind, float TimerMinutes)
{
	if (!DeclareEmergencyOn(Callsign, Kind, TimerMinutes)) { return; }
	const TCHAR* KindStr = TEXT("EMERGENCY");
	switch (Kind)
	{
		case EEmergencyType::GeneralMayday: KindStr = TEXT("General Mayday (7700)"); break;
		case EEmergencyType::CommsFailure:  KindStr = TEXT("Comms Failure (7600)");  break;
		case EEmergencyType::Hijack:        KindStr = TEXT("Hijack (7500)");         break;
		case EEmergencyType::FuelLow:       KindStr = TEXT("Fuel Emergency");        break;
		default: break;
	}
	PushNotification(FString::Printf(TEXT("EMERGENCY: %s %s"), *Callsign.ToString(), KindStr), FColor::Red, 6.f);
}

bool AClearanceSimulationController::Server_InjectClassify_Validate(FName Callsign, EThreatClass NewClass)
{
	return Callsign != NAME_None;
}
void AClearanceSimulationController::Server_InjectClassify_Implementation(FName Callsign, EThreatClass NewClass)
{
	ClassifyAircraft(Callsign, NewClass, /*bAsInstructor=*/true);
	const TCHAR* ClassStr = TEXT("UNKNOWN");
	switch (NewClass)
	{
		case EThreatClass::Friendly: ClassStr = TEXT("FRIENDLY"); break;
		case EThreatClass::Hostile:  ClassStr = TEXT("HOSTILE");  break;
		case EThreatClass::Neutral:  ClassStr = TEXT("NEUTRAL");  break;
		case EThreatClass::Unknown:  ClassStr = TEXT("UNKNOWN");  break;
		default: break;
	}
	PushNotification(FString::Printf(TEXT("CLASSIFY: %s -> %s"), *Callsign.ToString(), ClassStr), FColor::Cyan, 6.f);
}

bool AClearanceSimulationController::Server_InjectScramble_Validate(FName BanditCallsign)
{
	return BanditCallsign != NAME_None;
}
void AClearanceSimulationController::Server_InjectScramble_Implementation(FName BanditCallsign)
{
	const int32 N = ScrambleInterceptors(BanditCallsign);
	if (N > 0)
	{
		PushNotification(FString::Printf(TEXT("SCRAMBLE: %d interceptor%s on %s"),
			N, (N == 1 ? TEXT("") : TEXT("s")), *BanditCallsign.ToString()),
			FColor(255, 140, 0), 6.f);
	}
}

bool AClearanceSimulationController::Server_InjectSetWind_Validate(float DirectionDeg, float SpeedKts)
{
	return SpeedKts >= 0.f && SpeedKts <= 200.f;
}
void AClearanceSimulationController::Server_InjectSetWind_Implementation(float DirectionDeg, float SpeedKts)
{
	SetWind(DirectionDeg, SpeedKts);
	PushNotification(FString::Printf(TEXT("WIND %03d/%d"),
		FMath::RoundToInt(DirectionDeg) % 360, FMath::RoundToInt(SpeedKts)),
		FColor::Cyan, 6.f);
}

bool AClearanceSimulationController::Server_InjectSpawn_Validate() { return true; }
void AClearanceSimulationController::Server_InjectSpawn_Implementation()
{
	if (SpawnOne())
	{
		PushNotification(TEXT("SPAWN: +1 aircraft"), FColor::Cyan, 4.f);
	}
}

bool AClearanceSimulationController::Server_InjectClearTraffic_Validate() { return true; }
void AClearanceSimulationController::Server_InjectClearTraffic_Implementation()
{
	ClearTraffic();
	PushNotification(TEXT("CLEAR TRAFFIC: all aircraft removed"), FColor::Cyan, 6.f);
}

bool AClearanceSimulationController::Server_InjectLoadScenario_Validate(const FString& ScenarioName)
{
	return !ScenarioName.IsEmpty();
}
void AClearanceSimulationController::Server_InjectLoadScenario_Implementation(const FString& ScenarioName)
{
	if (!ScenarioRunner) { return; }
	const FString Name = ScenarioName.EndsWith(TEXT(".json")) ? ScenarioName : ScenarioName + TEXT(".json");
	const FString Path = FPaths::ProjectPluginsDir() / TEXT("ClearanceSim/Scenarios") / Name;
	FString Err;
	if (!ScenarioRunner->LoadFromFile(Path, Err))
	{
		UE_LOG(LogTemp, Error, TEXT("[Instructor] scenario load failed: %s"), *Err);
		PushNotification(FString::Printf(TEXT("SCENARIO LOAD FAILED: %s"), *Err), FColor::Red, 8.f);
		return;
	}
	SetSpawnerScenarioLocked(true); // pre-clear safety - matches the console path
	ClearTraffic();
	ScenarioRunner->Start();
	PushNotification(FString::Printf(TEXT("SCENARIO LOADED: %s"), *Name), FColor::Cyan, 6.f);
}

bool AClearanceSimulationController::Server_InjectStopScenario_Validate() { return true; }
void AClearanceSimulationController::Server_InjectStopScenario_Implementation()
{
	if (ScenarioRunner)
	{
		ScenarioRunner->Stop();
		PushNotification(TEXT("SCENARIO STOPPED"), FColor::Cyan, 6.f);
	}
}

bool AClearanceSimulationController::Server_InjectSetPaused_Validate(bool bNewPaused) { return true; }
void AClearanceSimulationController::Server_InjectSetPaused_Implementation(bool bNewPaused)
{
	if (bPaused == bNewPaused) { return; }
	bPaused = bNewPaused;
	PushNotification(bNewPaused ? TEXT("SESSION PAUSED") : TEXT("SESSION RESUMED"), FColor::Cyan, 4.f);
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
		{
			const FString NMsg = FString::Printf(TEXT("IFF %s: NO RESPONSE"), *Callsign.ToString());
			PushNotification(NMsg, FColor::Red, 4.f);
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, NMsg); }
		}
		// Audible "dead air" cue - short static burst so the operator hears the silence,
		// not just reads it. Routes through any placed VoiceOutput. - TripleA
		if (GetWorld())
		{
			for (TActorIterator<AClearanceVoiceOutput> VIt(GetWorld()); VIt; ++VIt)
			{
				if (*VIt) { VIt->PlayStatic(0.8f); break; }
			}
		}
		return false;
	}
	OutClass = S.ThreatClass;
	OutSquawk = S.SquawkCode;
	{
		const TCHAR* L = OutClass == EThreatClass::Friendly ? TEXT("FRIENDLY")
			: OutClass == EThreatClass::Hostile ? TEXT("HOSTILE")
			: OutClass == EThreatClass::Neutral ? TEXT("NEUTRAL") : TEXT("UNKNOWN");
		const FString NMsg = FString::Printf(TEXT("IFF %s: squawk %04d  %s"), *Callsign.ToString(), OutSquawk, L);
		PushNotification(NMsg, FColor::Green, 4.f);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green, NMsg);
		}
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
		const FString NMsg = FString::Printf(TEXT("INTERCEPT: %s -> %s no solution (too slow)"),
			*FighterCallsign.ToString(), *TargetCallsign.ToString());
		PushNotification(NMsg, FColor::Red, 4.f);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Red, NMsg); }
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

	if (bFirstTime)
	{
		const FString NMsg = FString::Printf(TEXT("INTERCEPT: %s -> %s vector %03.0f ETA %.0fs"),
			*FighterCallsign.ToString(), *TargetCallsign.ToString(), HeadingDeg, TimeToIntercept);
		PushNotification(NMsg, FColor::Cyan, 5.f);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, NMsg);
		}
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
	// like one pilot, not two. Voice tag is chosen on the server then passed
	// through the multicast so every peer renders the same pilot. - TripleA
	if (UWorld* W = GetWorld())
	{
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

		// Lock the voice tag on the server's VoiceOutput; the tag string is the
		// only thing the multicast needs to keep every peer in sync. - TripleA
		FString Voice;
		for (TActorIterator<AClearanceVoiceOutput> VoIt(W); VoIt; ++VoIt)
		{
			if (*VoIt) { Voice = VoIt->PickVoiceForCallsign(S.Callsign); }
			break;
		}

		const int32 P = FMath::RandRange(0, UE_ARRAY_COUNT(PanicLines) - 1);
		Multicast_PlayTTS(S.Callsign, PanicLines[P], Voice, /*bPanic*/ true);

		// GPWS cockpit alarm runs UNDER the pilot's voice, in parallel - bypasses
		// the half-duplex queue. Mirrors a real CVR where you hear the airframe
		// shouting "TERRAIN, PULL UP" while the crew panics. Tracked by callsign
		// so we can stop it on impact. - TripleA
		Multicast_PlayCockpitCue(S.Callsign, 1, 0.f);

		const FName Cs = S.Callsign;
		TWeakObjectPtr<AClearanceSimulationController> WeakSelf(this);
		FTimerHandle& Handle = PendingPanicTimers.FindOrAdd(Cs);
		W->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(
			[WeakSelf, Cs, Voice]()
			{
				if (!WeakSelf.IsValid()) { return; }
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
				WeakSelf->Multicast_PlayTTS(Cs, FinalLines[Fi], Voice, /*bPanic*/ true);
			}), 14.f, false);
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
		// Mirror the GPWS start: every peer started it, every peer needs to stop. - TripleA
		Multicast_PlayCockpitCue(S.Callsign, 2, 0.f);
	}

	// SAM kill on a confirmed hostile is a doctrine SUCCESS, not a crash
	// penalty. Detect via the Reason string (missile pipeline stamps
	// "Missile intercept ...") plus TrueAffiliation - both required so a
	// missile stray that hits a mis-ID'd friendly still logs as a crash
	// and takes the penalty it deserves. - TripleA
	const bool bMissileKill  = Reason.Contains(TEXT("Missile"));
	const bool bHostileTarget = (S.TrueAffiliation == EThreatClass::Hostile);
	const bool bSuccessfulIntercept = bMissileKill && bHostileTarget;

	if (Scoring)
	{
		if (bSuccessfulIntercept)
		{
			Scoring->LogIncident(EIncidentType::SuccessfulIntercept, S.Callsign, NAME_None,
				FString::Printf(TEXT("SAM kill on confirmed hostile: %s"), *S.Callsign.ToString()));
		}
		else
		{
			Scoring->LogIncident(EIncidentType::AircraftCrashed, S.Callsign, NAME_None, Reason);
		}
	}
	if (Recorder)
	{
		Recorder->LogEvent(SessionTime,
			bSuccessfulIntercept
				? FString::Printf(TEXT("INTERCEPT - %s (%s)"), *S.Callsign.ToString(), *Reason)
				: FString::Printf(TEXT("CRASH - %s (%s)"),     *S.Callsign.ToString(), *Reason));
	}
	FCrashSite Site;
	Site.PositionNm = S.Position;
	Site.SessionSeconds = SessionTime;
	Site.Callsign = S.Callsign;
	CrashSites.Add(Site);
	{
		if (bSuccessfulIntercept)
		{
			const FString NMsg = FString::Printf(TEXT("INTERCEPT: %s destroyed"), *S.Callsign.ToString());
			PushNotification(NMsg, FColor::Green, 30.f);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Green, NMsg);
			}
		}
		else
		{
			const FString NMsg = FString::Printf(TEXT("CRASH: %s - %s (-%d)"),
				*S.Callsign.ToString(), *Reason,
				Scoring ? Scoring->PenaltyAircraftCrashed : 500);
			PushNotification(NMsg, FColor::Red, 30.f);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 30.f, FColor::Red, NMsg);
			}
		}
	}

	// Controller's deadpan "Lost contact" on the en_US voice, ~1.5s after impact
	// so the listener hears the silence first. Multicast so every peer hears the
	// same call instead of only whichever machine the server happens to be. - TripleA
	if (UWorld* W = GetWorld())
	{
		const FName Cs = S.Callsign;
		TWeakObjectPtr<AClearanceSimulationController> WeakSelf(this);
		FTimerHandle Handle;
		W->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(
			[WeakSelf, Cs]()
			{
				if (WeakSelf.IsValid())
				{
					WeakSelf->Multicast_PlayTTS(NAME_None,
						FString::Printf(TEXT("Lost contact, %s"), *Cs.ToString()),
						TEXT("en-US-EricNeural"),  // deadpan controller voice
						/*bPanic*/ false);
				}
			}), 1.5f, false);
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
		V.TrueAffiliation  = EThreatClass::Friendly;   // Own-force fighter - operator view matches truth
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

	// SCRAMBLE requires a positively-identified hostile. Either path counts:
	//   - Operator's ThreatClass (what the trainee sees on the scope), OR
	//   - Instructor's TrueAffiliation (god-view knowledge, set via the
	//     instructor RECLASSIFY panel which deliberately does NOT propagate
	//     into ThreatClass so it can't telegraph to the operator).
	// This is the doctrine guardrail that stops fighters being launched on
	// civilian traffic - the identification has to be a positive act by
	// somebody with authority, either on-station or god-view. - TripleA
	const bool bOperatorSaysHostile   = Bandit.ThreatClass    == EThreatClass::Hostile;
	const bool bInstructorSaysHostile = Bandit.TrueAffiliation == EThreatClass::Hostile;
	if (!bOperatorSaysHostile && !bInstructorSaysHostile)
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
	// Spawn the 3-ship at the sector boundary on the same bearing from origin as
	// the bandit - the edge closest to the contact, reading as "the alert flight
	// just crossed into our airspace." Head-on closure with the inbound bandit
	// closes any meaningful gap at fighter speeds. - TripleA
	const float SpawnR = FMath::Max(10.f, ExitRadiusNm);
	const float BanditBearingDeg = FMath::Fmod(
		FMath::RadiansToDegrees(FMath::Atan2(Bandit.Position.X, Bandit.Position.Y)) + 360.f, 360.f);
	const float FanDeg = 8.f;
	int32 Launched = 0;
	for (int32 i = 0; i < 3; ++i)
	{
		const float ADeg = FMath::Fmod(BanditBearingDeg + (i - 1) * (FanDeg * 0.5f) + 360.f, 360.f);
		const float ARad = FMath::DegreesToRadians(ADeg);
		const FVector Pos(SpawnR * FMath::Sin(ARad), SpawnR * FMath::Cos(ARad), 0.f);

		const float Inbound = FMath::RadiansToDegrees(FMath::Atan2(Bandit.Position.X - Pos.X, Bandit.Position.Y - Pos.Y));
		const float Hdg = FMath::Fmod(Inbound + 360.f, 360.f);

		const int32 Num = NextViperNumber++;
		FAircraftState V;
		V.Callsign         = FName(*FString::Printf(TEXT("VIPER%02d"), Num));
		V.Position         = Pos;
		V.Altitude         = Bandit.Altitude;
		V.Heading          = Hdg;
		// Supersonic intercept dash - real alert-flight doctrine. F-22/F-35 dash at
		// ~M1.3 (~900 kts true) on scramble; F-16 closer to M1.1. The 620 default
		// was subsonic cruise and made every intercept feel anaemic. - TripleA
		V.Speed            = 900.f;
		V.TargetSpeed      = 900.f;
		V.WakeCategory     = EWakeCategory::Medium;
		V.FlightPhase      = EFlightPhase::Enroute;
		V.ThreatClass      = EThreatClass::Friendly;
		V.TrueAffiliation  = EThreatClass::Friendly;   // Own-force fighter - operator view matches truth
		V.SquawkCode       = 2200 + Num;
		V.bIFFOperational  = true;
		V.bIsMilitary      = true;
		V.bUnderGCIControl = true; // skip the civilian safety net for the formation + the engagement
		if (!AirspaceManager->RegisterAircraft(V)) { continue; }
		if (VectorIntercept(V.Callsign, BanditCallsign)) { ++Launched; }

		// Publish a DIS Fire PDU per launched interceptor - the fighter is
		// "firing" its intercept authority against the bandit. External
		// federates (AWACS, radar operator sims) see a Fire event and can
		// pair it with the later Detonation PDU by EventNumber. - TripleA
		{
			FWeaponsFireEvent FE;
			FE.FiringCallsign = V.Callsign;
			FE.TargetCallsign = BanditCallsign;
			FE.LocationNm     = FVector2D(V.Position.X, V.Position.Y);
			FE.AltitudeFt     = V.Altitude;
			const float HdgRad = FMath::DegreesToRadians(V.Heading);
			FE.VelocityXKts   = V.Speed * FMath::Sin(HdgRad);
			FE.VelocityYKts   = V.Speed * FMath::Cos(HdgRad);
			FE.VelocityZKts   = 0.f;
			FE.MunitionKind   = 1;   // Guided missile
			FE.WarheadKind    = 1000; // High-explosive
			FE.FuseKind       = 1000; // Contact
			FE.Quantity       = 1;
			FE.RangeMeters    = 20000.f;  // 20 km typical intercept envelope
			FE.EventNumber    = NextFireEventNumber++;
			PendingFireEvents.Add(MoveTemp(FE));
		}
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
	// Skip the spawner entirely while a scenario is running so it physically
	// cannot inject random traffic - no flag-juggling, no races. Resume the
	// instant the scenario stops. - TripleA
	const bool bScenarioRunning = ScenarioRunner && ScenarioRunner->IsRunning();
	if (Spawner && !bScenarioRunning) { Spawner->TickSpawning(DeltaTime); } // 1. entry

	for (const TPair<FName, TObjectPtr<UClearanceAircraftBehaviour>>& Pair : BehaviourMap)
	{
		if (Pair.Value) { Pair.Value->UpdateMovement(DeltaTime); } // 2-4. move + commit
	}

	if (ConflictDetector) { ConflictDetector->DetectConflicts(); } // 5. monitor (6-8 fire via delegates)

	// 5b. Stamp the highest current alert level onto each aircraft's state so
	// the replication stream carries it down to clients. They have no detector
	// of their own and were drawing every aircraft in safe-green. - TripleA
	if (AirspaceManager && ConflictDetector)
	{
		for (const FAircraftState& A : AirspaceManager->GetAllAircraftStates())
		{
			const EAlertLevel L = ConflictDetector->GetAlertLevelFor(A.Callsign);
			if (A.CurrentAlertLevel != L)
			{
				FAircraftState Updated = A;
				Updated.CurrentAlertLevel = L;
				AirspaceManager->RequestStateUpdate(Updated);
			}
		}
	}

	TickGCIIntercepts(DeltaTime);                                  // join-up + escort
	TickBanditEW(DeltaTime);                                       // hostiles react to closing interceptors
	TickCrashingAircraft(DeltaTime);                               // drop falling aircraft to the ground
	CheckExits();                                                  // landings / handoffs / strays

	UpdateVisuals();
	DrawDebugView();
}

FVector AClearanceSimulationController::WorldPositionFor(const FAircraftState& State) const
{
	const FVector Origin = GetActorLocation();
	const double WorldX = Origin.X + State.Position.X * WorldUnitsPerNm;
	const double WorldY = Origin.Y + State.Position.Y * WorldUnitsPerNm;

	// Ground reference is a downward trace from well above so it picks up
	// whatever's actually beneath the aircraft - Cesium tile at whatever LOD
	// streamed in, or a placed collision pad. Fixed GroundWorldZ misses the
	// streaming height changes and the aircraft ends up underground when
	// tiles resolve higher than the reference. - TripleA
	float GroundZ = GroundZAtXY(FVector2D(WorldX, WorldY));

	return FVector(WorldX, WorldY,
		GroundZ + AltitudeToWorldZOffset(State.Altitude));
}

float AClearanceSimulationController::GroundZAtXY(const FVector2D& WorldXY) const
{
	if (const UWorld* World = GetWorld())
	{
		const FVector Start(WorldXY.X, WorldXY.Y, GroundWorldZ + 200000.f);
		const FVector End  (WorldXY.X, WorldXY.Y, GroundWorldZ - 200000.f);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(SimGroundTrace), /*bTraceComplex=*/true);
		Params.bReturnPhysicalMaterial = false;
		if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			return Hit.ImpactPoint.Z;
		}
	}
	return GroundWorldZ;
}

FVector AClearanceSimulationController::WorldToSimMeters(const FVector& WorldLocation) const
{
	// Inverse of WorldPositionFor. XY uses the sim origin + WorldUnitsPerNm.
	// Z inverts AltitudeToWorldZOffset: linear branch is exact; power branch
	// is approximated as linear because launchers sit near the deck where
	// the power curve == linear anyway (0 ft -> 0 offset in either). - TripleA
	const FVector Origin = GetActorLocation();
	const float UnitsPerNm = FMath::Max(1.f, WorldUnitsPerNm);
	const double NmX = (WorldLocation.X - Origin.X) / UnitsPerNm;
	const double NmY = (WorldLocation.Y - Origin.Y) / UnitsPerNm;
	constexpr double MetersPerNm = 1852.0;
	constexpr double MetersPerFt = 0.3048;

	const double ZOffset = static_cast<double>(WorldLocation.Z - GroundWorldZ);
	const double AltFt   = (AltitudeWorldScale > 1e-3f)
		? ZOffset / static_cast<double>(AltitudeWorldScale)
		: 0.0;
	const double AltM = AltFt * MetersPerFt;

	return FVector(NmX * MetersPerNm, NmY * MetersPerNm, AltM);
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

void AClearanceSimulationController::RefreshOperatorTracks()
{
	if (!HasAuthority()) { return; }
	UWorld* World = GetWorld();
	if (!World) { RepOperatorTracks.Reset(); return; }

	// Gather every enabled radar. Centre-of-sector radar (the controller's
	// own UClearanceRadar) feeds in alongside any placed AClearanceRadarSite
	// actors. - TripleA
	TArray<UClearanceRadar*> Radars;
	if (Radar && Radar->IsEnabled()) { Radars.Add(Radar); }
	for (TActorIterator<AClearanceRadarSite> SIt(World); SIt; ++SIt)
	{
		if (*SIt && SIt->Radar && SIt->Radar->IsEnabled()) { Radars.Add(SIt->Radar); }
	}

	if (Radars.Num() == 0)
	{
		RepOperatorTracks.Reset();
		return;
	}

	// Latest-paint-wins merge keyed on TruthCallsign. A track painted by
	// multiple sites takes the freshest paint - that's the data the operator
	// would see on their fused scope. - TripleA
	TMap<FName, FRadarTrack> Fused;
	for (UClearanceRadar* R : Radars)
	{
		for (const FRadarTrack& T : R->GetTracks())
		{
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

	RepOperatorTracks.Reset(Fused.Num());
	AClearanceAirspaceManager* AM = GetAirspaceManager();
	for (const TPair<FName, FRadarTrack>& Pair : Fused)
	{
		FRadarTrack Out = Pair.Value;
		// Populate ThreatClass from the operator-facing classification on the
		// truth aircraft state. ChaffFalse ghost tracks (GHOST_* callsigns) and
		// stale entries whose aircraft has since deregistered leave it at
		// FRadarTrack's default (Unknown / amber), which reads as "no IFF
		// resolved" on the operator scope. Without this the BP paint chain
		// had no per-track threat data and every symbol drew as enum-default
		// Friendly / blue. - TripleA
		if (AM)
		{
			const FAircraftState S = AM->GetAircraftState(Out.TruthCallsign);
			if (S.bIsValid) { Out.ThreatClass = S.ThreatClass; }
		}
		RepOperatorTracks.Add(Out);
	}
}

// Global kill-switch for the whole DrawDebugView pass. Default off - the
// debug layer was a scaffolding aid before the instructor scope and the
// VR diegetic scope existed, and each frame it drew hundreds of circles,
// lines and strings that ate the game thread (~15ms with a normal
// aircraft count). Console: clearance.WorldDebugDraw 1 to bring it back
// for sim testing. - TripleA
static TAutoConsoleVariable<int32> CVarClearanceWorldDebugDraw(
	TEXT("clearance.WorldDebugDraw"),
	0,
	TEXT("Draw the SimulationController's world-space debug primitives "
	     "(sector rings, runway edges, zone circles, aircraft callsigns, "
	     "chaff/jamming markers). 0 = off (default), 1 = on."),
	ECVF_Cheat);

void AClearanceSimulationController::DrawDebugView()
{
	UWorld* World = GetWorld();
	if (CVarClearanceWorldDebugDraw.GetValueOnGameThread() == 0 ||
		!bDrawDebug || !AirspaceManager || !World)
	{
		return;
	}

	// The instructor / client-peer window doesn't want world-space DrawDebug
	// primitives bleeding through its UMG scope. Skip the whole pass on any
	// non-authority world. The operator (listen-server / authority) window
	// still gets the full debug layer for its free-cam view.
	//
	// Note: SceneCaptureComponent2D doesn't render DrawDebugLine primitives by
	// default (LineBatcher isn't in the capture render path), so ungating this
	// gate does not feed debug into the instructor PIP - it just re-introduces
	// the bleed-through. Doing it "properly" means replacing the DrawDebug
	// calls with billboard / mesh actors that render via the normal scene
	// pass; that's a separate task. - TripleA
	if (!HasAuthority())
	{
		return;
	}

	// Wipe persistent debug strings + lines at the top of every frame. UE's
	// DrawDebugString stores text on the PlayerController's HUD via AddDebugText
	// with broken duration semantics. FlushDebugStrings iterates PCs and calls
	// RemoveAllDebugStrings on each HUD - that's the supported clear path. - TripleA
	FlushDebugStrings(World);
	FlushPersistentDebugLines(World);

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
			FString::Printf(TEXT("WRECK %s"), *Cr.Callsign.ToString()), nullptr, FColor(220, 60, 60), 0.05f, true, 1.0f);
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
				FString::Printf(TEXT("[%s]"), *ZIt->ZoneName.ToString()), nullptr, ZoneCol, 0.05f, true, 1.3f);
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
				FString::Printf(TEXT("[%s]"), *AIt->AreaName.ToString()), nullptr, AreaLabel, 0.05f, true, 1.2f);
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
			FString::Printf(TEXT("RDR %s"), *SIt->SiteName.ToString()), nullptr, SIt->CoverageColour, 0.05f, true, 1.1f);
	}

	// Compass rose: a tick + heading number every 30deg around the boundary, cardinals
	// called out, so headings are readable in the world. "Vector 090" = send it toward
	// the 090 mark. Heading 0=North, 90=East (X=East, Y=North). - TripleA
	for (int32 Deg = 0; Deg < 360; Deg += 30)
	{
		const float R = FMath::DegreesToRadians((float)Deg);
		// The overview camera's projection puts world +X on the LEFT of the
		// rendered frame at Warton's georef; negating the east component here
		// puts each tick on the compass side of the sector where a viewer
		// EXPECTS to read that bearing (090 on the right, 270 on the left). - TripleA
		const FVector Dir(-FMath::Sin(R), FMath::Cos(R), 0.f);
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
		DrawDebugString(World, Edge + FVector(0, 0, 2.f * S), Label, nullptr, FColor::Cyan, 0.05f, true, 1.3f);
	}

	const TArray<FAircraftState> States = AirspaceManager->GetAllAircraftStates();
	const FSectorEnvironment Env = AirspaceManager->GetCurrentEnvironment();

	// One-off draw-pass diagnostic so I can confirm both server and client worlds
	// are reaching DrawDebugView. Distinct keys per role so the calls don't
	// overwrite each other in the shared GEngine queue. - TripleA
	{
		FString AllCallsigns;
		for (const FAircraftState& A : States) { AllCallsigns += FString::Printf(TEXT(" %s"), *A.Callsign.ToString()); }
		const FString DiagLine = FString::Printf(TEXT("[Draw %s] States.Num=%d %s"),
			HasAuthority() ? TEXT("SRV") : TEXT("CLI"),
			AirspaceManager ? AirspaceManager->GetAircraftCount() : -1,
			*AllCallsigns);
		CurrentDiagLine = DiagLine;
	}

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
					FString::Printf(TEXT("%.0fnm"), D), nullptr, LineCol, 0.05f, true, 0.9f);
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
	// Reciprocal of a designator: (N + 18) mod 36, with 0 folded up to 36. So 07
	// becomes 25, 34 becomes 16, 18 becomes 36. - TripleA
	auto ReciprocalNumber = [](int32 N)
	{
		int32 R = (N + 18) % 36;
		return (R == 0) ? 36 : R;
	};

	TArray<FRwyEnd> Ends;
	for (TActorIterator<AClearanceRunway> EIt(World); EIt; ++EIt)
	{
		FVector C = EIt->GetActorLocation();
		FVector MC, ME;
		if (EIt->GetRunwayBounds(MC, ME)) { C = MC; }
		const int32 PrimaryNum = (EIt->DesignatorNumberOverride > 0)
			? EIt->DesignatorNumberOverride
			: Designator(EIt->LandingHeadingDeg);
		Ends.Add({ *EIt, EIt->LandingHeadingDeg, C, PrimaryNum, false });
		if (EIt->bAllowReciprocal)
		{
			const float H2 = FMath::Fmod(EIt->LandingHeadingDeg + 180.f, 360.f);
			const int32 RecipNum = (EIt->DesignatorNumberOverride > 0)
				? ReciprocalNumber(EIt->DesignatorNumberOverride)
				: Designator(H2);
			Ends.Add({ *EIt, H2, C, RecipNum, true });
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
		const FVector LeftDir(-FMath::Sin(HRad), FMath::Cos(HRad), 0.f); // 90deg CCW from landing direction = pilot's left
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
		// LandingHeadingDeg is authored in the sim internal frame - use it as-is
		// so the draw sits at the same world direction as the sim math. - TripleA
		const float HRad = FMath::DegreesToRadians(It->LandingHeadingDeg);
		const FVector Dir(FMath::Sin(HRad), FMath::Cos(HRad), 0.f);
		const FVector Side(FMath::Cos(HRad), -FMath::Sin(HRad), 0.f);

		FVector Cw = It->GetActorLocation();
		float HalfLen = 800.f, HalfWidth = 0.025f * S, Zc = Cw.Z;
		// Prefer the actor's oriented Override dimensions when set. Reconstructing
		// runway half-length / half-width from the axis-aligned MeshExtent projected
		// onto Dir/Side is only correct when the runway is axis-aligned (headings
		// like 0/90/180/270). At any oblique heading (e.g. Warton's 070) that
		// projection leaks length into width and vice versa, producing a huge
		// square blob instead of a thin strip. - TripleA
		if (It->OverrideLengthUnits > 0.f && It->OverrideWidthUnits > 0.f)
		{
			HalfLen = It->OverrideLengthUnits * 0.5f;
			HalfWidth = It->OverrideWidthUnits * 0.5f;
			Zc = Cw.Z;
		}
		FVector MeshCentre, MeshExtent;
		if (It->GetRunwayBounds(MeshCentre, MeshExtent) &&
			!(It->OverrideLengthUnits > 0.f && It->OverrideWidthUnits > 0.f))
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
			DrawDebugString(World, E1 + FVector(0, 0, 1.5f * S), *L1, nullptr, FColor::Yellow, 0.05f, true, 1.2f);
		}
		if (It->bAllowReciprocal)
		{
			if (const FString* L2 = Labels.Find({ *It, true }))
			{
				DrawDebugString(World, E2 + FVector(0, 0, 1.5f * S), *L2, nullptr, FColor::Yellow, 0.05f, true, 1.2f);
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
		RepScoreTotal,
		RepScoreEfficiencyPct,
		States.Num(),
		Env.WindDirection, Env.WindSpeed, Env.ActiveRunwayHeading,
		*AARTag);

	// Scoring breakdown - read from replicated fields (server mirrors Scoring state
	// + per-incident tallies, client gets it via replication). - TripleA
	Readout += FString::Printf(TEXT("SCORING  total %d   |   +land %d  +handoff %d  +resolved %d  +intercept %d  +emer %d   |   -go-around %d  -sep-loss %d  -wake %d  -tcas %d  -strayed %d  -misID %d  -violated %d  -crashed %d  -busted %d   |   next spawn %.0fs\n"),
		RepScoreTotal,
		RepScoreLandings, RepScoreHandoffs, RepScoreResolved, RepScoreIntercepts, RepScoreEmergencies,
		RepScoreGoArounds, RepScoreSepLoss, RepScoreWake, RepScoreTCAS, RepScoreStrayed, RepScoreMisID, RepScoreViolated, RepScoreCrashed, RepScoreBusted,
		RepScoreNextSpawnSec);

	// Read from replicated fields (server mirrors ScenarioRunner state, client gets it via replication). - TripleA
	if (bRepScenarioRunning)
	{
		Readout += FString::Printf(TEXT("SCENARIO  %s  |  T+%02d:%02d  |  events %d/%d  triggers %d/%d\n"),
			*RepScenarioName,
			FMath::FloorToInt(RepScenarioElapsedSec / 60.f),
			FMath::FloorToInt(RepScenarioElapsedSec) % 60,
			RepScenarioFiredEvents,  RepScenarioTotalEvents,
			RepScenarioFiredTriggers, RepScenarioTotalTriggers);
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
		int32 PlacedSiteCount = 0;
		int32 PlacedSiteEnabled = 0;
		if (Radar && Radar->IsEnabled()) { Radars.Add(Radar); }
		if (UWorld* W = GetWorld())
		{
			for (TActorIterator<AClearanceRadarSite> SIt(W); SIt; ++SIt)
			{
				if (!*SIt) { continue; }
				++PlacedSiteCount;
				if (SIt->Radar && SIt->Radar->IsEnabled())
				{
					++PlacedSiteEnabled;
					Radars.Add(SIt->Radar);
				}
			}
		}

		// Always surface the fleet status so it's obvious whether placed sites
		// are even being seen by the controller. - TripleA
		Readout += FString::Printf(TEXT("RDR fleet  centre:%s  placed:%d (enabled %d)  active:%d\n"),
			(Radar && Radar->IsEnabled()) ? TEXT("ON") : TEXT("off"),
			PlacedSiteCount, PlacedSiteEnabled, Radars.Num());

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
				// Read the alert from the (replicated) state, not the local detector,
				// so client windows show the same conflict colouring. - TripleA
				const EAlertLevel Alert = AirspaceManager
					? AirspaceManager->GetAircraftState(Trk.TruthCallsign).CurrentAlertLevel
					: EAlertLevel::None;
				const FString IdLabel = Trk.bHasSecondary ? Trk.DisplayCallsign.ToString() : FString(TEXT("PRI"));
				Readout += FString::Printf(TEXT("RDR %s [%d/%d]  hdg %3.0f  alt %5.0f  spd %3.0f  conf %.0f%%%s\n"),
					*IdLabel, Seen, Radars.Num(), Trk.Heading, Trk.Altitude, Trk.Speed,
					Trk.Confidence * 100.f, Alert != EAlertLevel::None ? TEXT(" <CONF>") : TEXT(""));
			}
		}
	}

	// Chaff clouds - low-confidence ghost contacts drawn as fading amber rings
	// where each cloud was dropped. The radar paints them as ghost tracks; the
	// scope shows the physical cloud here too so the operator can correlate. - TripleA
	if (AirspaceManager)
	{
		const float NowS = World->GetTimeSeconds();
		for (const FChaffCloud& Cloud : AirspaceManager->GetActiveChaffClouds())
		{
			const float Age = NowS - Cloud.DropSessionTime;
			const float Frac = FMath::Clamp(Age / FMath::Max(0.1f, Cloud.LifetimeSec), 0.f, 1.f);
			const FVector ChaffPos(
				Origin.X + Cloud.PositionNm.X * S,
				Origin.Y + Cloud.PositionNm.Y * S,
				GroundWorldZ + AltitudeToWorldZOffset(Cloud.AltitudeFt));
			const FColor ChaffCol(255, static_cast<uint8>(220 * (1.f - Frac * 0.7f)), 60, 255);
			DrawDebugCircle(World, ChaffPos, 1200.f - 800.f * Frac, 16, ChaffCol,
				false, -1.f, 0, 60.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
			DrawDebugString(World, ChaffPos + FVector(0, 0, 800),
				TEXT("CHAFF"), nullptr, ChaffCol, 0.05f, true, 0.9f);
		}
	}

	{
		for (const FAircraftState& A : States)
		{
			// Read from state, not the local detector - the field is set by the
			// server's detector and replicates down. - TripleA
			const EAlertLevel Alert = A.CurrentAlertLevel;
			const FColor C = ColourFor(Alert);

			// MIL-STD-2525C tactical symbol drawn for every aircraft regardless
			// of whether a 3D mesh exists. The symbol carries the threat read
			// at a glance for the operator's top-down scope; the mesh is for
			// the "out the tower window" view we'll wire later. - TripleA
			const FVector P(Origin.X + A.Position.X * S, Origin.Y + A.Position.Y * S, GroundWorldZ + AltitudeToWorldZOffset(A.Altitude));
			DrawMIL2525CAir(World, P, 1600.f, A.ThreatClass, A.Heading, A.bIsMilitary, Alert);

			// Jamming indicator - jaggy lightning-bolt mark from the aircraft so
			// the operator can spot the jammer even when its own track is degraded
			// in the radar feed. - TripleA
			if (A.bJammingOn)
			{
				const float Wedge = 600.f;
				DrawDebugLine(World, P + FVector(-Wedge, Wedge, 0), P + FVector(0, Wedge * 0.4f, 0), FColor::Red, false, -1.f, 0, 50.f);
				DrawDebugLine(World, P + FVector(0, Wedge * 0.4f, 0), P + FVector(-Wedge * 0.3f, -Wedge * 0.3f, 0), FColor::Red, false, -1.f, 0, 50.f);
				DrawDebugLine(World, P + FVector(-Wedge * 0.3f, -Wedge * 0.3f, 0), P + FVector(Wedge, -Wedge, 0), FColor::Red, false, -1.f, 0, 50.f);
				DrawDebugString(World, P + FVector(0, 0, 1400),
					TEXT("JAM"), nullptr, FColor::Red, 0.05f, true, 1.0f);
			}

			// Float the callsign + current>target heading over each aircraft. - TripleA
			DrawDebugString(World, WorldPositionFor(A) + FVector(0, 0, 1.2f * S),
				FString::Printf(TEXT("%s  hdg %03.0f>%03.0f"), *A.Callsign.ToString(), A.Heading, A.TargetHeading),
				nullptr, C, 0.05f, true, 1.1f);

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

	// Main HUD readout. GEngine queue is shared across PIE windows and renders
	// inconsistently per viewport; stash on the controller so the HUD can pull
	// it into the right canvas. - TripleA
	CurrentReadout = Readout;
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

		// Flag entry the first time an aircraft is inside the sector. The exit-as-
		// stray check below is gated on this so aircraft spawned outside the boundary
		// (scenario incoming traffic) aren't instant-deregistered before they get to
		// fly in. - TripleA
		if (Dist <= ExitRadiusNm) { EverEnteredSector.Add(State.Callsign); }

		// GCI-controlled aircraft leaving: a flight (bandit + 1-3 fighters) shares one
		// bandit key. Credit Successful Intercept once per bandit and deregister the
		// whole flight. Unjoined / lone aircraft just leave quietly.
		//
		// Gate on EverEnteredSector - fighters spawn EXACTLY at the boundary (SpawnR ==
		// ExitRadiusNm), so without the entry gate the first-tick position round can put
		// them narrowly outside and the "attached fighters count as an escort" fallback
		// below fires a fake Successful Intercept the instant the scramble launches. - TripleA
		if (State.bUnderGCIControl && Dist > ExitRadiusNm && EverEnteredSector.Contains(State.Callsign))
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

			// If the bandit is exiting under GCI control with fighters still
			// attached in ActiveIntercepts, count that as a successful escort
			// even if the strict 0.8 nm / 1500 ft join gate never pinged during
			// the chase. The gate was meant to distinguish "vipers actually
			// caught up" from "vipers dispatched but never closed" - and if
			// they're still attached at sector exit, they clearly caught up.
			// This also unblocks the Fire/Detonation PDU pairs on the DIS
			// wire that the wider federation demo needs. - TripleA
			if (!bAnyJoined && Fighters.Num() > 0) { bAnyJoined = true; }

			if (bAnyJoined && !BanditCs.IsNone() && !InterceptCredited.Contains(BanditCs))
			{
				InterceptCredited.Add(BanditCs);

				// EW bonus: if this bandit was actively jamming when we caught
				// it, the operator held the track through the jam - real skill,
				// real payoff. - TripleA
				const FAircraftState BanditAtKill = AirspaceManager->GetAircraftState(BanditCs);
				const bool bThroughEW = BanditAtKill.bIsValid && BanditAtKill.bJammingOn;

				if (Scoring)
				{
					const FString Detail = bThroughEW
						? FString::Printf(TEXT("GCI: %d-ship escort of %s out of sector (jammer active - EW bonus)"), Fighters.Num(), *BanditCs.ToString())
						: FString::Printf(TEXT("GCI: %d-ship escort of %s out of sector"), Fighters.Num(), *BanditCs.ToString());
					Scoring->LogIncident(EIncidentType::SuccessfulIntercept, BanditCs, NAME_None, Detail);
					if (bThroughEW)
					{
						// Mirrors a SuccessfulIntercept entry to double the points without
						// inventing a new incident type. Cheap, accurate to the logic. - TripleA
						Scoring->LogIncident(EIncidentType::SuccessfulIntercept, BanditCs, NAME_None,
							FString::Printf(TEXT("EW bonus for %s"), *BanditCs.ToString()));
					}
				}
				{
					const int32 Base  = Scoring ? Scoring->PointsIntercept : 0;
					const int32 Total = bThroughEW ? Base * 2 : Base;
					const FString NMsg = bThroughEW
						? FString::Printf(TEXT("INTERCEPT: %d-ship escorted %s out through EW (+%d)"),
							Fighters.Num(), *BanditCs.ToString(), Total)
						: FString::Printf(TEXT("INTERCEPT: %d-ship escorted %s out (+%d)"),
							Fighters.Num(), *BanditCs.ToString(), Total);
					PushNotification(NMsg, FColor::Green, 5.f);
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, NMsg);
					}
				}
				// Publish a DIS Detonation PDU per fighter to pair with the
				// earlier Fire PDU. The scramble/intercept resolution IS the
				// "detonation" in DIS semantics - the munition event closes.
				// Result = 2 = Entity Proximate Detonation, symbolic of the
				// non-lethal "escorted out" outcome standard in ATC / air-
				// defence training. - TripleA
				for (const FName& F : Fighters)
				{
					const FAircraftState FS = AirspaceManager->GetAircraftState(F);
					FWeaponsDetonationEvent DE;
					DE.FiringCallsign    = F;
					DE.TargetCallsign    = BanditCs;
					DE.LocationNm        = FS.bIsValid ? FVector2D(FS.Position.X, FS.Position.Y)
					                                    : FVector2D(BanditAtKill.Position.X, BanditAtKill.Position.Y);
					DE.AltitudeFt        = FS.bIsValid ? FS.Altitude : BanditAtKill.Altitude;
					DE.MunitionKind      = 1;
					DE.WarheadKind       = 1000;
					DE.FuseKind          = 1000;
					DE.Quantity          = 1;
					DE.DetonationResult  = 2;   // Entity Proximate Detonation
					DE.EventNumber       = NextFireEventNumber++;   // Detonation gets its own event ID; a real impl would map back to the Fire's EventNumber
					PendingDetonationEvents.Add(MoveTemp(DE));
				}

				BanditEWStates.Remove(BanditCs); // tidy
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
						FString::Printf(TEXT("Emergency %s safely landed"), *UEnum::GetDisplayValueAsText(State.ActiveEmergency).ToString()));
				}
			}
			AirspaceManager->DeregisterAircraft(State.Callsign);
		}
		else if (Dist > ExitRadiusNm && EverEnteredSector.Contains(State.Callsign))
		{
			// Cleared to leave = a clean handoff to the next sector; drifting out otherwise is a miss.
			// An emergency aircraft drifting out unhandled is a catastrophic loss.
			// Non-civilian contacts (hostile / unknown / military) exiting are GCI's
			// problem and don't count as "strayed". Civilians are Neutral per
			// MIL-STD-2525C affiliation. Only fires if the aircraft was actually
			// inside at some point - protects scenario incoming traffic from being
			// insta-killed at spawn. - TripleA
			const bool bNonCivilian = (State.ThreatClass != EThreatClass::Neutral)
			                         || State.bIsMilitary || State.bUnderGCIControl;

			EIncidentType Outcome;
			bool bLogIncident = true;
			if (State.ActiveEmergency != EEmergencyType::None && State.FlightPhase != EFlightPhase::Exiting)
			{
				Outcome = EIncidentType::AircraftCrashed;
			}
			else if (State.FlightPhase == EFlightPhase::Exiting)
			{
				Outcome = (State.ActiveEmergency != EEmergencyType::None)
					? EIncidentType::SuccessfulEmergencyHandling
					: EIncidentType::SuccessfulHandoff;
			}
			else if (bNonCivilian)
			{
				// GCI contact left the sector under its own steam - no civilian scoring event.
				bLogIncident = false;
				Outcome = EIncidentType::UnresolvedExit; // unused
			}
			else
			{
				Outcome = EIncidentType::UnresolvedExit;
			}
			if (bLogIncident && Scoring) { Scoring->LogIncident(Outcome, State.Callsign, NAME_None, TEXT("Left sector")); }
			AirspaceManager->DeregisterAircraft(State.Callsign);
		}
	}
}

bool AClearanceSimulationController::SetAircraftAutopilotEngaged(FName Callsign, bool bEngaged)
{
	TObjectPtr<UClearanceAircraftBehaviour>* Found = BehaviourMap.Find(Callsign);
	if (!Found || !*Found) { return false; }
	(*Found)->SetAutopilotEngaged(bEngaged);
	UE_LOG(LogTemp, Log, TEXT("[Autopilot] %s -> %s"),
		*Callsign.ToString(), bEngaged ? TEXT("ENGAGED") : TEXT("disengaged"));
	return true;
}

// ---- Weapon-event queue accessors --------------------------------------
// Thin forwarders so the internal PendingFireEvents / PendingDetonationEvents
// arrays and NextFireEventNumber counter stay private. Any actor that needs
// to raise a weapon event calls through these instead of reaching into the
// controller's guts. - TripleA

int32 AClearanceSimulationController::AllocateFireEventNumber()
{
	if (!HasAuthority()) { return 0; }
	// 16-bit wrap matches DIS FiringEntityID.EventNumber width.
	return static_cast<int32>((NextFireEventNumber++) & 0xFFFFu);
}

void AClearanceSimulationController::QueueFireEvent(const FWeaponsFireEvent& Event)
{
	if (!HasAuthority()) { return; }
	PendingFireEvents.Add(Event);
}

void AClearanceSimulationController::QueueDetonationEvent(const FWeaponsDetonationEvent& Event)
{
	if (!HasAuthority()) { return; }
	PendingDetonationEvents.Add(Event);
}

AClearanceMissile* AClearanceSimulationController::Server_InjectFireMissile(FName TargetCallsign)
{
	if (!HasAuthority()) { return nullptr; }
	// AClearanceMissile::Fire resolves the target callsign, computes the
	// ground launch site, spawns the actor, and queues the Fire event.
	// This method is a thin BP-friendly forwarder for panel UIs. - TripleA
	return AClearanceMissile::Fire(this, TargetCallsign);
}

void AClearanceSimulationController::MissileHit(FName TargetCallsign, const FString& Reason)
{
	if (!HasAuthority() || !AirspaceManager) { return; }
	const FAircraftState Target = AirspaceManager->GetAircraftState(TargetCallsign);
	if (!Target.bIsValid)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MissileHit] Target %s not in airspace at intercept time; nothing to crash."),
			*TargetCallsign.ToString());
		return;
	}
	// Route through the same crash entry the existing mayday / GPWS
	// pipeline uses. BeginCrash starts the visible descent; the per-tick
	// TickCrashingAircraft path eventually calls CrashAircraft when the
	// wreck reaches the ground. - TripleA
	BeginCrash(Target, Reason);
}

EInstructionResult AClearanceSimulationController::PlayerIssueInstruction(const FAircraftInstruction& Instruction)
{
	// Civilian ATC can't command anything under air defence control, and can't
	// command external (federated) aircraft either - those belong to whichever
	// peer sim is publishing them. NORDO contacts (IFF off, not friendly OR
	// neutral) also reject silently - the non-response is the operator's clue
	// they're dealing with an unknown / hostile. - TripleA
	if (AirspaceManager)
	{
		const FAircraftState Target = AirspaceManager->GetAircraftState(Instruction.TargetCallsign);

		// Frequency gate: if the instruction carries a source channel (set at
		// the phraseology-parse callsite from the operator's active tx freq)
		// and the target aircraft is tuned to a DIFFERENT channel, the
		// aircraft doesn't hear it. Silent rejection with the same NO RESPONSE
		// path NORDO uses - the non-response is the operator's cue that they
		// need to switch channels (or hand the aircraft off). Instructions
		// without a SourceFrequency (scripted / instructor injects) bypass
		// the gate. Guard is a universal listen-only monitor so operator
		// transmissions on Guard don't reach specific aircraft - Guard exists
		// for the operator to hear incoming distress, not to command. - TripleA
		if (Instruction.SourceFrequency != ECommsFrequency::None
			&& Instruction.SourceFrequency != ECommsFrequency::Guard
			&& Target.bIsValid
			&& Target.AssignedFrequency != Instruction.SourceFrequency)
		{
			// Speak a helpful "wrong frequency, try X" system response so the
			// operator learns which channel to switch to instead of chasing
			// a silent NORDO. Real training tools do this; production ATC
			// doesn't but this is a training sim. The response goes through
			// the SYSTEM voice (not the aircraft's) because the aircraft
			// literally didn't hear the call. - TripleA
			auto FreqName = [](ECommsFrequency F) -> const TCHAR*
			{
				switch (F)
				{
				case ECommsFrequency::Tower:     return TEXT("TOWER");
				case ECommsFrequency::Approach:  return TEXT("APPROACH");
				case ECommsFrequency::Emergency: return TEXT("EMERGENCY");
				case ECommsFrequency::Guard:     return TEXT("GUARD");
				default:                         return TEXT("UNKNOWN");
				}
			};
			const FString SysMsg = FString::Printf(
				TEXT("%s is on %s, switch frequency"),
				*Instruction.TargetCallsign.ToString(),
				FreqName(Target.AssignedFrequency));

			// TTS via the SimController's own multicast TTS pipe using the
			// controller voice ("en-US-EricNeural") - the aircraft literally
			// didn't hear the call, so this comes from SYSTEM, not the pilot.
			// Also logs to transcript so the AAR shows the training aid
			// intervention. - TripleA
			Multicast_PlayTTS(NAME_None, SysMsg, TEXT("en-US-EricNeural"), /*bPanic=*/ false);
			LogTranscriptLine(EClearanceCommsRole::System, Instruction.TargetCallsign, SysMsg);

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, SysMsg);
			}
			return EInstructionResult::Rejected_NoResponse;
		}

		if (Target.bIsValid && !Target.bIFFOperational
			&& Target.ThreatClass != EThreatClass::Friendly
			&& Target.ThreatClass != EThreatClass::Neutral)
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
			*UEnum::GetDisplayValueAsText(Instruction.Type).ToString(), *Instruction.TargetCallsign.ToString(), Instruction.TargetValue));
	}

	// Operator-side "no joy" / "track lost" - drops the contact without the
	// strayed penalty IF EW (jamming or active chaff near the contact) plausibly
	// caused the loss. Without EW it scores as a normal unresolved-exit so the
	// operator can't just dismiss difficult contacts for free. - TripleA
	if (Instruction.Type == EInstructionType::DeclareTrackLost && AirspaceManager)
	{
		const FAircraftState S = AirspaceManager->GetAircraftState(Instruction.TargetCallsign);
		if (!S.bIsValid) { return EInstructionResult::Rejected_InvalidCallsign; }

		bool bEWPresent = S.bJammingOn;
		if (!bEWPresent)
		{
			for (const FChaffCloud& C : AirspaceManager->GetActiveChaffClouds())
			{
				const float DistNm = FVector::Dist2D(S.Position, C.PositionNm);
				if (DistNm < 3.f) { bEWPresent = true; break; }
			}
		}

		if (Scoring)
		{
			if (bEWPresent)
			{
				Scoring->LogIncident(EIncidentType::SuccessfulIntercept, Instruction.TargetCallsign, NAME_None,
					FString::Printf(TEXT("track lost to EW - %s released"), *Instruction.TargetCallsign.ToString()));
			}
			else
			{
				Scoring->LogIncident(EIncidentType::UnresolvedExit, Instruction.TargetCallsign, NAME_None,
					FString::Printf(TEXT("track declared lost without EW - %s dropped"), *Instruction.TargetCallsign.ToString()));
			}
		}

		PushNotification(
			bEWPresent
				? FString::Printf(TEXT("%s released (EW - no penalty)"), *Instruction.TargetCallsign.ToString())
				: FString::Printf(TEXT("%s dropped (no EW - counted as strayed)"), *Instruction.TargetCallsign.ToString()),
			bEWPresent ? FColor(80, 200, 255) : FColor(255, 140, 60), 5.f);

		BanditEWStates.Remove(Instruction.TargetCallsign);
		AirspaceManager->DeregisterAircraft(Instruction.TargetCallsign);
		return EInstructionResult::Accepted;
	}

	if (CommsRouter)
	{
		return CommsRouter->IssueInstruction(Instruction);
	}
	return EInstructionResult::Rejected_InvalidCallsign;
}

UClearanceSessionRecorder* AClearanceSimulationController::GetRecorder()
{
	if (!Recorder)
	{
		Recorder = NewObject<UClearanceSessionRecorder>(this);
		if (Recorder) { Recorder->StartRecording(); }
	}
	return Recorder;
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
	// Freeze the recorded duration so the client UI has a stable scrub-bar
	// maximum. Without this, the client's own recorder keeps capturing past
	// EnterReplay and the duration value drifts. - TripleA
	ReplayDuration = Recorder->GetDurationSeconds();
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
	// Resume the recording buffer where it left off. Calls the raw recorder
	// directly so we don't go through the controller's StartRecording()
	// wrapper, which would ClearRecording() and wipe the pre-replay history.
	// The next EnterReplay() then sees the original + post-replay portion
	// concatenated. - TripleA
	if (Recorder)
	{
		// Tag the boundary between segments so the scrub bar can render a
		// tick where this "Go Live" happened. Stored as seconds-into-the-
		// recording so it stays valid even after the buffer keeps growing. - TripleA
		ReplaySegmentSeams.Add(Recorder->GetDurationSeconds());
		Recorder->StartRecording();
	}
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

// --- Comms transcript ------------------------------------------------------

namespace
{
	// Phraseology renderers for the comms transcript. Map each
	// EInstructionType / value pair onto a short ATC-style line. Not full
	// ICAO phraseology (e.g. "two seven zero" instead of "270"), but readable
	// at a glance and matches what the player would actually say. - TripleA
	FString FormatHeading(int32 Hdg)
	{
		return FString::Printf(TEXT("%03d"), ((Hdg % 360) + 360) % 360);
	}

	FString FormatAltitude(int32 AltFt)
	{
		if (AltFt >= 18000)
		{
			return FString::Printf(TEXT("FL%03d"), FMath::RoundToInt(AltFt / 100.f));
		}
		return FString::Printf(TEXT("%d ft"), AltFt);
	}

	FString RenderOperatorPhraseology(const FAircraftInstruction& Inst)
	{
		const FString Callsign = Inst.TargetCallsign.ToString();
		switch (Inst.Type)
		{
		case EInstructionType::HeadingChange:
		{
			const TCHAR* Dir = (Inst.TurnDirection < 0) ? TEXT("left ") : (Inst.TurnDirection > 0) ? TEXT("right ") : TEXT("");
			return FString::Printf(TEXT("%s, turn %sheading %s"), *Callsign, Dir, *FormatHeading(FMath::RoundToInt(Inst.TargetValue)));
		}
		case EInstructionType::AltitudeChange:
		{
			const TCHAR* Exp = Inst.bExpedite ? TEXT(", expedite") : TEXT("");
			return FString::Printf(TEXT("%s, climb / descend %s%s"), *Callsign, *FormatAltitude(FMath::RoundToInt(Inst.TargetValue)), Exp);
		}
		case EInstructionType::SpeedChange:
			return FString::Printf(TEXT("%s, speed %d knots"), *Callsign, FMath::RoundToInt(Inst.TargetValue));
		case EInstructionType::Hold:
			return FString::Printf(TEXT("%s, hold present position"), *Callsign);
		case EInstructionType::ApproachClearance:
			return FString::Printf(TEXT("%s, cleared ILS approach"), *Callsign);
		case EInstructionType::TakeoffClearance:
			return FString::Printf(TEXT("%s, cleared for takeoff"), *Callsign);
		case EInstructionType::ExitSector:
			return FString::Printf(TEXT("%s, contact next sector, frequency change approved"), *Callsign);
		case EInstructionType::DeclareTrackLost:
			return FString::Printf(TEXT("%s, no joy, breaking off"), *Callsign);
		default:
			return Callsign;
		}
	}

	FString RenderPilotReadback(const FAircraftInstruction& Inst)
	{
		const FString Callsign = Inst.TargetCallsign.ToString();
		switch (Inst.Type)
		{
		case EInstructionType::HeadingChange:
		{
			const TCHAR* Dir = (Inst.TurnDirection < 0) ? TEXT("left ") : (Inst.TurnDirection > 0) ? TEXT("right ") : TEXT("");
			return FString::Printf(TEXT("%sheading %s, %s"), Dir, *FormatHeading(FMath::RoundToInt(Inst.TargetValue)), *Callsign);
		}
		case EInstructionType::AltitudeChange:
		{
			const TCHAR* Exp = Inst.bExpedite ? TEXT(" expedite") : TEXT("");
			return FString::Printf(TEXT("%s%s, %s"), *FormatAltitude(FMath::RoundToInt(Inst.TargetValue)), Exp, *Callsign);
		}
		case EInstructionType::SpeedChange:
			return FString::Printf(TEXT("speed %d knots, %s"), FMath::RoundToInt(Inst.TargetValue), *Callsign);
		case EInstructionType::Hold:
			return FString::Printf(TEXT("holding present position, %s"), *Callsign);
		case EInstructionType::ApproachClearance:
			return FString::Printf(TEXT("cleared ILS approach, %s"), *Callsign);
		case EInstructionType::TakeoffClearance:
			return FString::Printf(TEXT("cleared for takeoff, %s"), *Callsign);
		case EInstructionType::ExitSector:
			return FString::Printf(TEXT("frequency change approved, %s"), *Callsign);
		case EInstructionType::DeclareTrackLost:
			return FString::Printf(TEXT("roger, %s"), *Callsign);
		default:
			return Callsign;
		}
	}

	FString RenderPilotRefusal(const FAircraftInstruction& Inst, EInstructionResult Result)
	{
		const FString Callsign = Inst.TargetCallsign.ToString();
		switch (Result)
		{
		case EInstructionResult::Rejected_PhysicallyImpossible:
			return FString::Printf(TEXT("unable, %s"), *Callsign);
		case EInstructionResult::Rejected_ConflictAdvisory:
			return FString::Printf(TEXT("negative, traffic, %s"), *Callsign);
		case EInstructionResult::Rejected_AircraftExited:
			return FString::Printf(TEXT("out of sector, %s"), *Callsign);
		default:
			return FString();
		}
	}
}

void AClearanceSimulationController::HandleInstructionResult(FName Callsign, FAircraftInstruction Instruction, EInstructionResult Result)
{
	UE_LOG(LogTemp, Warning, TEXT("[Transcript] HandleInstructionResult fired: %s result=%s auth=%d"),
		*Callsign.ToString(),
		*UEnum::GetDisplayValueAsText(Result).ToString(),
		HasAuthority() ? 1 : 0);

	// Operator's outbound transmission - always logged. Operator's voice is the
	// trainee themselves (typed / spoken into the parser), so it never goes
	// through Multicast_PlayTTS like the pilot voice does. Has to be logged
	// here explicitly. - TripleA
	AppendTranscriptEntry(EClearanceCommsRole::Operator, Callsign, RenderOperatorPhraseology(Instruction));

	// Pilot readbacks / refusals for the Accepted / Rejected_PhysicallyImpossible
	// / Rejected_ConflictAdvisory / Rejected_AircraftExited cases all run through
	// the parser's assembled SpeakOut -> Multicast_PlayTTS path, which now
	// auto-logs to transcript at the source. No per-instruction Pilot log here
	// (would double-up with the TTS log). - TripleA
	switch (Result)
	{
	case EInstructionResult::Rejected_NoResponse:
		// Deliberate silence - NORDO contact. No TTS, no parser readback, so log
		// a System line so the trainee can see they got nothing back. - TripleA
		AppendTranscriptEntry(EClearanceCommsRole::System, Callsign, FString::Printf(TEXT("[no response from %s]"), *Callsign.ToString()));
		break;
	case EInstructionResult::Rejected_InvalidCallsign:
		AppendTranscriptEntry(EClearanceCommsRole::System, Callsign, FString::Printf(TEXT("Unknown callsign %s"), *Callsign.ToString()));
		break;
	default:
		break;
	}
}

void AClearanceSimulationController::LogTranscriptSystem(FName Callsign, const FString& Text)
{
	AppendTranscriptEntry(EClearanceCommsRole::System, Callsign, Text);
}

void AClearanceSimulationController::LogTranscriptLine(EClearanceCommsRole InRole, FName Callsign, const FString& Text)
{
	AppendTranscriptEntry(InRole, Callsign, Text);
}

namespace
{
	FString MMSS(float Seconds)
	{
		const int32 S = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FString::Printf(TEXT("%02d:%02d"), S / 60, S % 60);
	}

	const TCHAR* IncidentDisplayName(EIncidentType T)
	{
		switch (T)
		{
		case EIncidentType::SuccessfulLanding:           return TEXT("Landing");
		case EIncidentType::SuccessfulHandoff:           return TEXT("Handoff");
		case EIncidentType::SuccessfulResolution:        return TEXT("Conflict resolved");
		case EIncidentType::SuccessfulIntercept:         return TEXT("Intercept");
		case EIncidentType::SuccessfulEmergencyHandling: return TEXT("Emergency handled");
		case EIncidentType::GoAroundTriggered:           return TEXT("Go-around");
		case EIncidentType::SeparationLoss:              return TEXT("Separation loss");
		case EIncidentType::UnresolvedExit:              return TEXT("Strayed");
		case EIncidentType::WakeEncounter:               return TEXT("Wake encounter");
		case EIncidentType::TCASResolutionAdvisory:      return TEXT("TCAS RA");
		case EIncidentType::MisidentifiedCivilian:       return TEXT("Mis-ID");
		case EIncidentType::ViolationZoneBreached:       return TEXT("Protected-zone breach");
		case EIncidentType::AircraftCrashed:             return TEXT("Aircraft crashed");
		case EIncidentType::RestrictedAirspaceBust:      return TEXT("Restricted-zone bust");
		case EIncidentType::MissedHandoff:               return TEXT("Missed handoff");
		case EIncidentType::LateInstruction:             return TEXT("Late instruction");
		default:                                          return TEXT("Event");
		}
	}

	const TCHAR* RoleTag(EClearanceCommsRole R)
	{
		switch (R)
		{
		case EClearanceCommsRole::Operator:   return TEXT("ATC");
		case EClearanceCommsRole::Pilot:      return TEXT("PILOT");
		case EClearanceCommsRole::System:     return TEXT("SYS");
		case EClearanceCommsRole::Instructor: return TEXT("INSTR");
		case EClearanceCommsRole::Tower:      return TEXT("TWR");
		case EClearanceCommsRole::Acc:        return TEXT("ACC");
		case EClearanceCommsRole::Awacs:      return TEXT("AWACS");
		case EClearanceCommsRole::Gci:        return TEXT("GCI");
		case EClearanceCommsRole::Atis:       return TEXT("ATIS");
		case EClearanceCommsRole::Met:        return TEXT("MET");
		default:                              return TEXT("");
		}
	}
}

bool AClearanceSimulationController::ExportAARReport(FString& OutPath)
{
	if (!HasAuthority()) { return false; }

	const FDateTime Now = FDateTime::Now();
	const FString Stamp = Now.ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString DirPath = FPaths::ProjectSavedDir() / TEXT("Reports");
	IFileManager::Get().MakeDirectory(*DirPath, /*Tree=*/true);
	const FString FullPath = DirPath / FString::Printf(TEXT("Session_%s.md"), *Stamp);

	const TArray<FIncidentRecord>& Log = RepScoringLog;
	const TArray<FCommsTranscriptEntry>& T = Transcript;

	// Header
	FString Md;
	Md.Reserve(64 * 1024);
	Md += FString::Printf(TEXT("# CLEARANCE - Session AAR\n\n"));
	Md += FString::Printf(TEXT("**Generated:** %s\n"), *Now.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
	Md += FString::Printf(TEXT("**Session duration:** %s\n"), *MMSS(SessionTime));
	if (bRepScenarioRunning && !RepScenarioName.IsEmpty())
	{
		Md += FString::Printf(TEXT("**Scenario:** %s (T+%s)\n"), *RepScenarioName, *MMSS(RepScenarioElapsedSec));
	}
	Md += FString::Printf(TEXT("**Conditions:** Wind %03.0f / %.0fkt\n\n"), WindDirectionDeg, WindSpeedKts);
	Md += TEXT("---\n\n");

	// Summary
	const int32 OpsHandled  = RepScoreLandings + RepScoreHandoffs;
	const int32 Failures    = RepScoreSepLoss + RepScoreStrayed + RepScoreMisID + RepScoreViolated + RepScoreCrashed + RepScoreBusted;
	Md += TEXT("## Summary\n\n");
	Md += FString::Printf(TEXT("- **Total score:** %d (efficiency %.0f%%)\n"), RepScoreTotal, RepScoreEfficiencyPct);
	Md += FString::Printf(TEXT("- **Handled:** %d (%d landings + %d handoffs)\n"), OpsHandled, RepScoreLandings, RepScoreHandoffs);
	Md += FString::Printf(TEXT("- **Failures:** %d (sep loss / strayed / mis-ID / violated / crashed / busted)\n"), Failures);
	Md += FString::Printf(TEXT("- **Resolutions / intercepts / emergencies handled:** %d / %d / %d\n\n"),
		RepScoreResolved, RepScoreIntercepts, RepScoreEmergencies);
	Md += TEXT("---\n\n");

	// Timeline
	Md += TEXT("## Timeline\n\n");
	if (Log.Num() == 0)
	{
		Md += TEXT("_No scored events this session._\n\n");
	}
	else
	{
		Md += TEXT("| Time  | Event                  | Aircraft        | Detail |\n");
		Md += TEXT("|-------|------------------------|-----------------|--------|\n");
		for (const FIncidentRecord& R : Log)
		{
			const FString Acft = R.AircraftB.IsNone()
				? R.AircraftA.ToString()
				: FString::Printf(TEXT("%s / %s"), *R.AircraftA.ToString(), *R.AircraftB.ToString());
			FString Detail = R.Details.Replace(TEXT("|"), TEXT("/")); // table-safe
			Md += FString::Printf(TEXT("| %s | %s | %s | %s |\n"),
				*MMSS(R.TimeStamp), IncidentDisplayName(R.Type), *Acft, *Detail);
		}
		Md += TEXT("\n");
	}
	Md += TEXT("---\n\n");

	// Critical incidents drilldown - top penalties with 60-second comms window
	Md += TEXT("## Critical incidents\n\n");
	TArray<int32> CriticalIdx;
	for (int32 i = 0; i < Log.Num(); ++i)
	{
		const EIncidentType Tp = Log[i].Type;
		if (Tp == EIncidentType::MisidentifiedCivilian
		 || Tp == EIncidentType::ViolationZoneBreached
		 || Tp == EIncidentType::AircraftCrashed
		 || Tp == EIncidentType::SeparationLoss
		 || Tp == EIncidentType::RestrictedAirspaceBust)
		{
			CriticalIdx.Add(i);
		}
	}
	if (CriticalIdx.Num() == 0)
	{
		Md += TEXT("_No catastrophic incidents this session._\n\n");
	}
	else
	{
		for (int32 Idx : CriticalIdx)
		{
			const FIncidentRecord& R = Log[Idx];
			Md += FString::Printf(TEXT("### [%s] %s - %s\n\n"),
				*MMSS(R.TimeStamp), IncidentDisplayName(R.Type), *R.AircraftA.ToString());
			if (!R.Details.IsEmpty()) { Md += FString::Printf(TEXT("- %s\n"), *R.Details); }
			// 60-second comms window leading up to the incident
			const float WindowStart = R.TimeStamp - 60.f;
			Md += TEXT("- **60-second comms window:**\n");
			bool bAnyComms = false;
			for (const FCommsTranscriptEntry& E : T)
			{
				if (E.TimeSec >= WindowStart && E.TimeSec <= R.TimeStamp)
				{
					Md += FString::Printf(TEXT("    - [%s] **%s** %s: %s\n"),
						*MMSS(E.TimeSec), RoleTag(E.Role), *E.Speaker, *E.Text);
					bAnyComms = true;
				}
			}
			if (!bAnyComms) { Md += TEXT("    - _(no transmissions in the 60 seconds before this incident)_\n"); }
			Md += TEXT("\n");
		}
	}
	Md += TEXT("---\n\n");

	// Score breakdown
	Md += TEXT("## Score breakdown\n\n");
	Md += TEXT("**Operations (+):**\n");
	Md += FString::Printf(TEXT("- Landings: %d\n"), RepScoreLandings);
	Md += FString::Printf(TEXT("- Handoffs: %d\n"), RepScoreHandoffs);
	Md += FString::Printf(TEXT("- Conflicts resolved: %d\n"), RepScoreResolved);
	Md += FString::Printf(TEXT("- Intercepts: %d\n"), RepScoreIntercepts);
	Md += FString::Printf(TEXT("- Emergencies handled: %d\n\n"), RepScoreEmergencies);
	Md += TEXT("**Incidents (-):**\n");
	Md += FString::Printf(TEXT("- Go-arounds: %d\n"), RepScoreGoArounds);
	Md += FString::Printf(TEXT("- Separation losses: %d\n"), RepScoreSepLoss);
	Md += FString::Printf(TEXT("- Wake busts: %d\n"), RepScoreWake);
	Md += FString::Printf(TEXT("- TCAS RAs: %d\n"), RepScoreTCAS);
	Md += FString::Printf(TEXT("- Strayed: %d\n"), RepScoreStrayed);
	Md += FString::Printf(TEXT("- Mis-IDs: %d\n"), RepScoreMisID);
	Md += FString::Printf(TEXT("- Protected-zone breaches: %d\n"), RepScoreViolated);
	Md += FString::Printf(TEXT("- Aircraft crashed: %d\n"), RepScoreCrashed);
	Md += FString::Printf(TEXT("- Restricted-zone busts: %d\n\n"), RepScoreBusted);
	Md += TEXT("---\n\n");

	// Full transcript
	Md += FString::Printf(TEXT("## Comms transcript (%d entries)\n\n"), T.Num());
	if (T.Num() == 0)
	{
		Md += TEXT("_No transmissions logged._\n");
	}
	else
	{
		for (const FCommsTranscriptEntry& E : T)
		{
			Md += FString::Printf(TEXT("- [%s] **%s** %s: %s\n"),
				*MMSS(E.TimeSec), RoleTag(E.Role), *E.Speaker, *E.Text);
		}
	}

	const bool bOk = FFileHelper::SaveStringToFile(Md, *FullPath);
	if (bOk)
	{
		// Resolve to an absolute, openable path so the popup + log show a
		// clean Windows / VS Code-clickable string instead of UE's relative
		// "../../../../" prefix. - TripleA
		OutPath = FPaths::ConvertRelativePathToFull(FullPath);
		// One announcement only - PushNotification auto-logs as System so the
		// transcript captures it without a separate explicit log call (would
		// double-up and pollute the very report the next export reads). - TripleA
		PushNotification(FString::Printf(TEXT("AAR exported: %s"), *OutPath), FColor(80, 200, 255), 8.f);
		UE_LOG(LogTemp, Display, TEXT("[AAR] Exported -> %s"), *OutPath);
	}
	else
	{
		PushNotification(FString::Printf(TEXT("AAR export FAILED -> %s"), *FullPath), FColor::Red, 8.f);
		UE_LOG(LogTemp, Warning, TEXT("ExportAARReport: failed to write %s"), *FullPath);
	}
	return bOk;
}

void AClearanceSimulationController::SaveCheckpoint(FName Name)
{
	if (!HasAuthority() || Name.IsNone() || !AirspaceManager) { return; }

	FCheckpointPayload Payload;
	Payload.AircraftStates    = AirspaceManager->GetAllAircraftStates();
	Payload.SessionTime       = SessionTime;
	Payload.WindDirectionDeg  = WindDirectionDeg;
	Payload.WindSpeedKts      = WindSpeedKts;
	Payload.ScoreAtSave       = Scoring ? Scoring->GetCurrentScore() : 0;
	Payload.ScoringLog        = Scoring ? Scoring->GetSessionLog() : TArray<FIncidentRecord>();

	CheckpointStore.Add(Name, MoveTemp(Payload));
	RebuildRepCheckpoints();

	PushNotification(FString::Printf(TEXT("Checkpoint saved: %s (%d aircraft, score %d)"),
		*Name.ToString(),
		AirspaceManager->GetAllAircraftStates().Num(),
		Scoring ? Scoring->GetCurrentScore() : 0),
		FColor(80, 200, 255), 5.f);
	LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None, FString::Printf(TEXT("Checkpoint saved: \"%s\""), *Name.ToString()));
}

bool AClearanceSimulationController::LoadCheckpoint(FName Name)
{
	if (!HasAuthority() || Name.IsNone() || !AirspaceManager) { return false; }
	const FCheckpointPayload* Payload = CheckpointStore.Find(Name);
	if (!Payload)
	{
		PushNotification(FString::Printf(TEXT("Checkpoint not found: %s"), *Name.ToString()), FColor::Red, 5.f);
		return false;
	}

	// Clear all live aircraft then re-register from the snapshot. ClearTraffic
	// handles deregistration which cascades to visual actor + behaviour
	// teardown; RegisterAircraft on each state re-spawns + re-wires. - TripleA
	AirspaceManager->ClearAllAircraft();
	for (const FAircraftState& S : Payload->AircraftStates)
	{
		AirspaceManager->RegisterAircraft(S);
	}

	SessionTime      = Payload->SessionTime;
	WindDirectionDeg = Payload->WindDirectionDeg;
	WindSpeedKts     = Payload->WindSpeedKts;
	if (Scoring)
	{
		Scoring->RestoreFromCheckpoint(Payload->ScoreAtSave, Payload->ScoringLog);
	}

	// Don't touch the transcript - keep all attempts in the AAR so the
	// instructor can review the trainee's progress across multiple tries
	// at the same scenario. - TripleA

	PushNotification(FString::Printf(TEXT("Checkpoint loaded: %s (%d aircraft restored)"),
		*Name.ToString(), Payload->AircraftStates.Num()),
		FColor(80, 200, 255), 5.f);
	LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None, FString::Printf(TEXT("Checkpoint loaded: \"%s\""), *Name.ToString()));
	return true;
}

void AClearanceSimulationController::DeleteCheckpoint(FName Name)
{
	if (!HasAuthority() || Name.IsNone()) { return; }
	if (CheckpointStore.Remove(Name) > 0)
	{
		RebuildRepCheckpoints();
		LogTranscriptLine(EClearanceCommsRole::Instructor, NAME_None, FString::Printf(TEXT("Checkpoint deleted: \"%s\""), *Name.ToString()));
	}
}

void AClearanceSimulationController::RebuildRepCheckpoints()
{
	RepCheckpoints.Reset(CheckpointStore.Num());
	for (const TPair<FName, FCheckpointPayload>& Pair : CheckpointStore)
	{
		FClearanceCheckpointInfo Info;
		Info.Name               = Pair.Key;
		Info.SessionTimeAtSave  = Pair.Value.SessionTime;
		Info.AircraftCount      = Pair.Value.AircraftStates.Num();
		Info.ScoreAtSave        = Pair.Value.ScoreAtSave;
		RepCheckpoints.Add(Info);
	}
}

void AClearanceSimulationController::AppendTranscriptEntry(EClearanceCommsRole InRole, FName Callsign, const FString& Text)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Transcript] Append SKIPPED - not authority. Role=%d cs=%s text=%s"),
			static_cast<int32>(InRole), *Callsign.ToString(), *Text);
		return;
	}
	if (Text.IsEmpty())
	{
		return;
	}

	// Republish this transmission as a DIS Signal PDU. Every transcript line
	// that represents actual radio traffic - operator commands, pilot readbacks,
	// facility broadcasts, instructor injects on the frequency - goes out as
	// federated voice. System messages (score updates, sim housekeeping) don't
	// belong on the radio and are filtered out. - TripleA
	const bool bAnyEmitterRunning =
		(DISEmitter && DISEmitter->IsRunning()) ||
		(DDSEmitter && DDSEmitter->IsRunning());
	if (bAnyEmitterRunning && InRole != EClearanceCommsRole::System)
	{
		FVoiceCommsEvent VE;
		VE.SpeakerCallsign = (InRole == EClearanceCommsRole::Pilot) ? Callsign : NAME_None;
		VE.Transcript      = Text;
		VE.RadioId         = 1;
		PendingVoiceEvents.Add(MoveTemp(VE));
	}

	// Speaker label per role:
	//   Operator   -> "ATC"
	//   Pilot      -> aircraft callsign
	//   System     -> Callsign if present, else "SYS"
	//   Instructor -> "INSTR"
	//   Tower / Acc / Awacs / Gci / Atis / Met -> the role's short tag
	//     (TWR / ACC / AWACS / GCI / ATIS / MET) - facility name IS the speaker
	// - TripleA
	FString SpeakerLabel;
	switch (InRole)
	{
	case EClearanceCommsRole::Operator:   SpeakerLabel = TEXT("ATC");   break;
	case EClearanceCommsRole::Pilot:      SpeakerLabel = Callsign.ToString(); break;
	case EClearanceCommsRole::Instructor: SpeakerLabel = TEXT("INSTR"); break;
	case EClearanceCommsRole::Tower:      SpeakerLabel = TEXT("TWR");   break;
	case EClearanceCommsRole::Acc:        SpeakerLabel = TEXT("ACC");   break;
	case EClearanceCommsRole::Awacs:      SpeakerLabel = TEXT("AWACS"); break;
	case EClearanceCommsRole::Gci:        SpeakerLabel = TEXT("GCI");   break;
	case EClearanceCommsRole::Atis:       SpeakerLabel = TEXT("ATIS");  break;
	case EClearanceCommsRole::Met:        SpeakerLabel = TEXT("MET");   break;
	case EClearanceCommsRole::System:
	default:
		SpeakerLabel = Callsign.IsNone() ? FString(TEXT("SYS")) : Callsign.ToString();
		break;
	}
	FCommsTranscriptEntry Entry;
	Entry.TimeSec = SessionTime;
	Entry.Role = InRole;
	Entry.Callsign = Callsign;
	Entry.Speaker = SpeakerLabel;
	Entry.Text = Text;
	Transcript.Add(MoveTemp(Entry));
	// Cap the replicated buffer to the last 500 entries - keeps net cost
	// bounded and the panel's scroll list only renders the tail. - TripleA
	constexpr int32 MaxTranscript = 500;
	if (Transcript.Num() > MaxTranscript)
	{
		Transcript.RemoveAt(0, Transcript.Num() - MaxTranscript);
	}
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

bool AClearanceSimulationController::StartDDSEmitter(int32 DomainId)
{
	if (!DDSEmitter)
	{
		UE_LOG(LogTemp, Error, TEXT("[DDS] StartDDSEmitter: DDSEmitter is null - controller not initialised"));
		return false;
	}
	const bool bOk = DDSEmitter->Start(DomainId);
	UE_LOG(LogTemp, Display, TEXT("[DDS] Start on domain %d -> %s"), DomainId, bOk ? TEXT("OK") : TEXT("FAILED"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, bOk ? FColor::Cyan : FColor::Red,
			bOk ? FString::Printf(TEXT("DDS: publishing on domain %d"), DomainId)
			    : FString::Printf(TEXT("DDS: failed to start on domain %d"), DomainId));
	}
	return bOk;
}

void AClearanceSimulationController::StopDDSEmitter()
{
	if (DDSEmitter) { DDSEmitter->Stop(); }
	UE_LOG(LogTemp, Display, TEXT("[DDS] Stopped"));
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, TEXT("DDS: stopped")); }
}

bool AClearanceSimulationController::StartRTIEmitter(int32 DomainId)
{
	if (!RTIEmitter)
	{
		UE_LOG(LogTemp, Error, TEXT("[RTI] StartRTIEmitter: RTIEmitter is null - controller not initialised"));
		return false;
	}
	// Keep RTI identity in lockstep with DIS/DDS so the same SiteId flows
	// to all three wires without a second command needing to be routed. - TripleA
	if (DISEmitter)
	{
		RTIEmitter->SiteId        = DISEmitter->SiteId;
		RTIEmitter->ApplicationId = DISEmitter->ApplicationId;
		RTIEmitter->ExerciseId    = DISEmitter->ExerciseId;
	}
	const bool bOk = RTIEmitter->Start(DomainId);
	UE_LOG(LogTemp, Display, TEXT("[RTI] Start on domain %d -> %s"), DomainId, bOk ? TEXT("OK") : TEXT("FAILED"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, bOk ? FColor::Green : FColor::Red,
			bOk ? FString::Printf(TEXT("RTI: publishing on domain %d"), DomainId)
			    : FString::Printf(TEXT("RTI: failed to start on domain %d"), DomainId));
	}
	return bOk;
}

void AClearanceSimulationController::StopRTIEmitter()
{
	if (RTIEmitter) { RTIEmitter->Stop(); }
	UE_LOG(LogTemp, Display, TEXT("[RTI] Stopped"));
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Green, TEXT("RTI: stopped")); }
}

bool AClearanceSimulationController::IsRTIEmitting() const
{
	return RTIEmitter && RTIEmitter->IsRunning();
}

int32 AClearanceSimulationController::GetRTIPacketsSent() const
{
	return RTIEmitter ? RTIEmitter->GetTotalPublishedCount() : 0;
}

bool AClearanceSimulationController::StartHLAFederate(const FString& FederationName, const FString& FederateName, const FString& FomModulePath)
{
	if (!HLAEmitter)
	{
		UE_LOG(LogTemp, Error, TEXT("[HLA] StartHLAFederate: HLAEmitter is null - controller not initialised"));
		return false;
	}
	// Federation identity syncs across all four wires so EntityIdentifier
	// attributes on HLA carry the same {Site, App} tuple DIS/DDS/RTI use. - TripleA
	if (DISEmitter)
	{
		HLAEmitter->SiteId        = DISEmitter->SiteId;
		HLAEmitter->ApplicationId = DISEmitter->ApplicationId;
	}
	const bool bOk = HLAEmitter->Join(FederationName, FederateName, FomModulePath);
	UE_LOG(LogTemp, Display, TEXT("[HLA] Join '%s' as '%s' -> %s"),
		*FederationName, *FederateName, bOk ? TEXT("OK") : TEXT("FAILED"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, bOk ? FColor::Purple : FColor::Red,
			bOk ? FString::Printf(TEXT("HLA: joined '%s'"), *FederationName)
			    : FString::Printf(TEXT("HLA: join failed on '%s'"), *FederationName));
	}
	return bOk;
}

void AClearanceSimulationController::StopHLAFederate()
{
	if (HLAEmitter) { HLAEmitter->Resign(); }
	UE_LOG(LogTemp, Display, TEXT("[HLA] Resigned"));
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Purple, TEXT("HLA: resigned")); }
}

bool AClearanceSimulationController::IsHLAJoined() const
{
	return HLAEmitter && HLAEmitter->IsJoined();
}

int32 AClearanceSimulationController::GetHLAUpdatesSent() const
{
	return HLAEmitter ? HLAEmitter->GetTotalUpdatesCount() : 0;
}

bool AClearanceSimulationController::IsDDSEmitting() const
{
	return DDSEmitter && DDSEmitter->IsRunning();
}

int32 AClearanceSimulationController::GetDDSPacketsSent() const
{
	return DDSEmitter ? DDSEmitter->GetTotalPublishedCount() : 0;
}

bool AClearanceSimulationController::StartDDSReceiver(int32 DomainId)
{
	if (!DDSReceiver) { return false; }
	// Keep the receiver's loopback identity in sync with the emitter so our
	// own publications don't get re-ingested. Matches the DIS pattern. - TripleA
	if (DDSEmitter)
	{
		DDSReceiver->LocalSiteId        = DDSEmitter->SiteId;
		DDSReceiver->LocalApplicationId = DDSEmitter->ApplicationId;
	}
	const bool bOk = DDSReceiver->Start(DomainId);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.f, bOk ? FColor::Cyan : FColor::Red,
			bOk ? FString::Printf(TEXT("DDS: listening on domain %d"), DomainId)
			    : FString::Printf(TEXT("DDS: failed to bind domain %d"), DomainId));
	}
	return bOk;
}

void AClearanceSimulationController::StopDDSReceiver()
{
	if (DDSReceiver) { DDSReceiver->Stop(); }
	if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan, TEXT("DDS: recv stopped")); }
}

bool AClearanceSimulationController::IsDDSReceiving() const
{
	return DDSReceiver && DDSReceiver->IsRunning();
}

int32 AClearanceSimulationController::GetDDSAircraftIngested() const
{
	return DDSReceiver ? DDSReceiver->GetTotalIngestedCount() : 0;
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

	// Missiles need 3D-aware camera framing - a straight-up VLS booster
	// has no meaningful yaw so the aircraft heading-based math would
	// point the camera at nothing. Use the missile's actual world-space
	// velocity direction as Forward instead. Aircraft keep the horizontal
	// heading-based framing they've always had. - TripleA
	FVector Forward;
	FVector Right;
	if (S.bIsMissile && !S.Velocity.IsNearlyZero(1e-4f))
	{
		Forward = S.Velocity.GetSafeNormal();               // full 3D
		const FVector WorldUp(0.f, 0.f, 1.f);
		Right = FVector::CrossProduct(WorldUp, Forward).GetSafeNormal();
		if (Right.IsNearlyZero(1e-3f))                       // Forward parallel to Up
		{
			Right = FVector(1.f, 0.f, 0.f);
		}
	}
	else
	{
		const float HeadingRad = FMath::DegreesToRadians(S.Heading);
		Forward = FVector(FMath::Sin(HeadingRad), FMath::Cos(HeadingRad), 0.f);
		Right   = FVector(FMath::Cos(HeadingRad), -FMath::Sin(HeadingRad), 0.f);
	}
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

void AClearanceSimulationController::SetInstructorPipView(EClearanceCameraView View)
{
	// Default isn't a preset transform - the operator's pawn isn't useful as
	// a PIP source - so fall back to Tower in that case. - TripleA
	InstructorPipView = (View == EClearanceCameraView::Default)
		? EClearanceCameraView::Tower
		: View;
}

void AClearanceSimulationController::CycleInstructorPipView()
{
	switch (InstructorPipView)
	{
	case EClearanceCameraView::Tower:    InstructorPipView = EClearanceCameraView::Follow;   break;
	case EClearanceCameraView::Follow:   InstructorPipView = EClearanceCameraView::Approach; break;
	case EClearanceCameraView::Approach: InstructorPipView = EClearanceCameraView::Overview; break;
	case EClearanceCameraView::Overview: InstructorPipView = EClearanceCameraView::Tower;    break;
	default:                              InstructorPipView = EClearanceCameraView::Tower;    break;
	}
}

void AClearanceSimulationController::SetInstructorPipEnabled(bool bEnabled)
{
	bInstructorPipEnabled = bEnabled;
	// Reset the throttle so the first frame after re-enable captures immediately
	// instead of waiting up to a full interval. - TripleA
	InstructorPipCaptureAccum = 1.f / FMath::Max(1.f, InstructorPipCaptureRateHz);
}

void AClearanceSimulationController::SetInstructorPipFollowCallsign(FName Callsign)
{
	InstructorPipFollowCallsign = Callsign;
}

void AClearanceSimulationController::ApplyTowerYawDelta(float DeltaDeg)
{
	InstructorTowerYawDeg = FMath::Fmod(InstructorTowerYawDeg + DeltaDeg, 360.f);
	if (InstructorTowerYawDeg < 0.f) { InstructorTowerYawDeg += 360.f; }
}

void AClearanceSimulationController::AddOverviewPan(FVector2D PanDeltaUv)
{
	// Convert normalized image-space delta into world units based on the
	// area currently visible at the active zoom level. 1:1 drag means
	// when the cursor moves N% of the image, the camera shifts N% of the
	// visible ground. - TripleA
	const float DefaultAlt = ExitRadiusNm * WorldUnitsPerNm * 1.45f;
	const float CurrentAlt = DefaultAlt / FMath::Max(0.001f, InstructorOverviewZoomLevel);
	const float HalfFOVRad = FMath::DegreesToRadians(45.f); // overview FOV is fixed at 90
	const float VisibleRadius = CurrentAlt * FMath::Tan(HalfFOVRad);

	InstructorOverviewPanOffsetUnits.X += PanDeltaUv.X * (VisibleRadius * 2.f);
	InstructorOverviewPanOffsetUnits.Y += PanDeltaUv.Y * (VisibleRadius * 2.f);

	// Pan extent scales with zoom: zero at zoom 1 (sector already fits) and
	// grows toward the full sector radius as the user zooms in, so they
	// can still reach the corners + edges of the sector at high zoom
	// instead of being trapped near the centre. - TripleA
	const float SectorRadius = ExitRadiusNm * WorldUnitsPerNm;
	const float MaxOffset = SectorRadius * FMath::Max(0.f, 1.f - 1.f / FMath::Max(0.001f, InstructorOverviewZoomLevel));
	InstructorOverviewPanOffsetUnits.X = FMath::Clamp(InstructorOverviewPanOffsetUnits.X, -MaxOffset, MaxOffset);
	InstructorOverviewPanOffsetUnits.Y = FMath::Clamp(InstructorOverviewPanOffsetUnits.Y, -MaxOffset, MaxOffset);
}

void AClearanceSimulationController::AddOverviewZoom(float ZoomDelta)
{
	// Min = 1.0 so the camera can never zoom out further than the default
	// sector-fits-the-frame altitude. Zooming further out just shows empty
	// terrain past ExitRadiusNm with no useful info. - TripleA
	InstructorOverviewZoomLevel = FMath::Clamp(InstructorOverviewZoomLevel + ZoomDelta, 1.0f, 4.0f);

	// Re-clamp the pan offset to the new zoom's allowed extent so zooming
	// back out drags the camera toward centre instead of holding a corner
	// that's no longer reachable. - TripleA
	const float SectorRadius = ExitRadiusNm * WorldUnitsPerNm;
	const float MaxOffset = SectorRadius * FMath::Max(0.f, 1.f - 1.f / FMath::Max(0.001f, InstructorOverviewZoomLevel));
	InstructorOverviewPanOffsetUnits.X = FMath::Clamp(InstructorOverviewPanOffsetUnits.X, -MaxOffset, MaxOffset);
	InstructorOverviewPanOffsetUnits.Y = FMath::Clamp(InstructorOverviewPanOffsetUnits.Y, -MaxOffset, MaxOffset);
}

void AClearanceSimulationController::ResetOverviewView()
{
	InstructorOverviewPanOffsetUnits = FVector2D::ZeroVector;
	InstructorOverviewZoomLevel = 1.f;
}

TArray<FString> AClearanceSimulationController::GetApproachRunwayLabels() const
{
	TArray<FString> Out;
	if (!AirspaceManager) { return Out; }
	const TArray<FRunwayInfo>& All = AirspaceManager->GetAllRunways();

	// Bucket the runways by base designator (heading rounded to nearest 10),
	// then for each group with siblings, project the threshold onto the
	// "right" vector relative to landing direction and tag L / R / C from
	// rightmost to leftmost. Single-runway designators get no suffix. - TripleA
	const int32 N = All.Num();
	TArray<int32> Designator;
	Designator.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		int32 D;
		if (All[i].DesignatorOverride > 0)
		{
			D = All[i].DesignatorOverride;
		}
		else
		{
			const float MagBearingI = FMath::Fmod(FMath::Fmod(360.f - All[i].HeadingDeg, 360.f) + 360.f, 360.f);
			D = FMath::RoundToInt(MagBearingI / 10.f);
		}
		if (D <= 0) { D = 36; }
		if (D > 36) { D = D % 36; if (D == 0) { D = 36; } }
		Designator[i] = D;
	}

	for (int32 i = 0; i < N; ++i)
	{
		const FRunwayInfo& Me = All[i];
		const int32 MyDes = Designator[i];

		TArray<int32> Group;
		for (int32 j = 0; j < N; ++j)
		{
			if (Designator[j] == MyDes) { Group.Add(j); }
		}

		FString Suffix;
		if (Group.Num() > 1)
		{
			// Right vector when facing the landing direction.
			const float Rad = FMath::DegreesToRadians(Me.HeadingDeg);
			const FVector2D RightDir(FMath::Cos(Rad), -FMath::Sin(Rad));
			const float MyProj = FVector2D::DotProduct(Me.ThresholdNm, RightDir);
			int32 MoreRight = 0, MoreLeft = 0;
			for (int32 j : Group)
			{
				if (j == i) { continue; }
				const float P = FVector2D::DotProduct(All[j].ThresholdNm, RightDir);
				if (P > MyProj) { ++MoreRight; }
				if (P < MyProj) { ++MoreLeft; }
			}
			if (Group.Num() == 2)
			{
				Suffix = (MoreRight == 0) ? TEXT("R") : TEXT("L");
			}
			else
			{
				if (MoreLeft == 0)       { Suffix = TEXT("L"); }
				else if (MoreRight == 0) { Suffix = TEXT("R"); }
				else                     { Suffix = TEXT("C"); }
			}
		}

		Out.Add(FString::Printf(TEXT("RWY %02d%s"), MyDes, *Suffix));
	}
	return Out;
}

FString AClearanceSimulationController::GetActiveRunwayLabel() const
{
	if (!AirspaceManager) { return TEXT("--"); }
	const float ActiveHdg = AirspaceManager->GetActiveRunway();
	if (ActiveHdg < 0.f) { return TEXT("--"); }
	const TArray<FRunwayInfo>& All = AirspaceManager->GetAllRunways();
	for (const FRunwayInfo& R : All)
	{
		if (!FMath::IsNearlyEqual(R.HeadingDeg, ActiveHdg, 0.5f)) { continue; }
		int32 Des;
		if (R.DesignatorOverride > 0) { Des = R.DesignatorOverride; }
		else
		{
			const float MagBearing = FMath::Fmod(FMath::Fmod(360.f - R.HeadingDeg, 360.f) + 360.f, 360.f);
			Des = FMath::RoundToInt(MagBearing / 10.f);
		}
		if (Des <= 0)  { Des = 36; }
		if (Des > 36)  { Des = Des % 36; if (Des == 0) { Des = 36; } }
		return FString::Printf(TEXT("%02d"), Des);
	}
	return TEXT("--");
}

void AClearanceSimulationController::SetInstructorPipApproachRunway(int32 Index)
{
	if (!AirspaceManager) { return; }
	const int32 N = AirspaceManager->GetAllRunways().Num();
	if (N <= 0) { return; }
	InstructorApproachRunwayIndex = FMath::Clamp(Index, 0, N - 1);
	InstructorPipView = EClearanceCameraView::Approach;
	UE_LOG(LogTemp, Warning, TEXT("[PIP] SetApproachRunway: this=%p auth=%d storedIdx=%d totalRunways=%d"),
		this, HasAuthority() ? 1 : 0, InstructorApproachRunwayIndex, N);
}

void AClearanceSimulationController::PickApproachRunwayByLabel(const FString& Label)
{
	const TArray<FString> Labels = GetApproachRunwayLabels();
	const int32 Idx = Labels.IndexOfByPredicate([&Label](const FString& S) { return S == Label; });
	UE_LOG(LogTemp, Warning, TEXT("[PIP] PickByLabel: requested='%s' available=[%s] foundIdx=%d"),
		*Label, *FString::Join(Labels, TEXT(", ")), Idx);
	if (Idx == INDEX_NONE) { return; }
	SetInstructorPipApproachRunway(Idx);
}

void AClearanceSimulationController::SetInstructorPipFollowAngle(EClearanceFollowAngle Angle)
{
	InstructorPipFollowAngle = Angle;
}

void AClearanceSimulationController::CycleInstructorPipFollowAngleNext()
{
	switch (InstructorPipFollowAngle)
	{
	case EClearanceFollowAngle::Chase:   InstructorPipFollowAngle = EClearanceFollowAngle::Cockpit; break;
	case EClearanceFollowAngle::Cockpit: InstructorPipFollowAngle = EClearanceFollowAngle::Side;    break;
	case EClearanceFollowAngle::Side:    InstructorPipFollowAngle = EClearanceFollowAngle::Top;     break;
	case EClearanceFollowAngle::Top:     InstructorPipFollowAngle = EClearanceFollowAngle::Chase;   break;
	}
}

void AClearanceSimulationController::CycleInstructorPipFollowAnglePrev()
{
	switch (InstructorPipFollowAngle)
	{
	case EClearanceFollowAngle::Chase:   InstructorPipFollowAngle = EClearanceFollowAngle::Top;     break;
	case EClearanceFollowAngle::Top:     InstructorPipFollowAngle = EClearanceFollowAngle::Side;    break;
	case EClearanceFollowAngle::Side:    InstructorPipFollowAngle = EClearanceFollowAngle::Cockpit; break;
	case EClearanceFollowAngle::Cockpit: InstructorPipFollowAngle = EClearanceFollowAngle::Chase;   break;
	}
}

void AClearanceSimulationController::SetOperatorViewRotation(FRotator NewRot)
{
	OperatorViewRotation = NewRot;
}

void AClearanceSimulationController::SetOperatorViewLocation(FVector NewLoc)
{
	OperatorViewLocation = NewLoc;
}

TArray<FInstructorCameraLabel> AClearanceSimulationController::GetCameraLabels() const
{
	TArray<FInstructorCameraLabel> Out;
	if (!AirspaceManager || !InstructorPipCapture || !InstructorPipRT) { return Out; }

	const FVector ViewLocation = InstructorPipCapture->GetComponentLocation();
	const FRotator ViewRotation = InstructorPipCapture->GetComponentRotation();
	const FVector ViewForward = ViewRotation.Vector();
	const float FOVDegrees = InstructorPipCapture->FOVAngle;
	const int32 Width = InstructorPipRT->SizeX;
	const int32 Height = InstructorPipRT->SizeY;
	if (Width <= 0 || Height <= 0) { return Out; }

	// FLookFromMatrix needs an up vector that isn't parallel to forward, or
	// the X-axis derivation (Up x Forward) is zero and the matrix breaks.
	// Top-down chase view has forward = (0,0,-1) which is parallel to
	// world up - that produced wrong labels for every aircraft in the Top
	// sub-angle. Using the camera's OWN local up axis (which is the
	// rotation applied to +Z) is guaranteed perpendicular to its forward
	// for any valid rotation, including straight down. - TripleA
	const FVector ViewUp = ViewRotation.RotateVector(FVector::UpVector);
	const FMatrix ViewMatrix = FLookFromMatrix(ViewLocation, ViewForward, ViewUp);

	const float HalfFOVRad = FMath::DegreesToRadians(FOVDegrees * 0.5f);
	const FMatrix ProjMatrix = FPerspectiveMatrix(
		HalfFOVRad,
		static_cast<float>(Width),
		static_cast<float>(Height),
		GNearClippingPlane);

	const FMatrix ViewProj = ViewMatrix * ProjMatrix;

	for (const FAircraftState& State : AirspaceManager->GetAllAircraftStates())
	{
		if (!State.bIsValid) { continue; }
		const FVector WorldPos = WorldPositionFor(State);

		// Skip behind camera via dot product against forward (faster than
		// trusting Clip.W which can be unreliable for near-parallel cases). - TripleA
		if (FVector::DotProduct(WorldPos - ViewLocation, ViewForward) <= 0.f) { continue; }

		const FPlane Clip = ViewProj.TransformFVector4(FVector4(WorldPos, 1.f));
		if (Clip.W <= KINDA_SMALL_NUMBER) { continue; }

		const float NDC_X = Clip.X / Clip.W;
		const float NDC_Y = Clip.Y / Clip.W;
		const float UV_X = (NDC_X + 1.f) * 0.5f;
		// Flip Y so 0 = top edge (UMG convention). - TripleA
		const float UV_Y = 1.f - (NDC_Y + 1.f) * 0.5f;

		FInstructorCameraLabel Label;
		Label.Callsign = State.Callsign;
		Label.ScreenUV = FVector2D(UV_X, UV_Y);
		Label.ThreatClass = State.TrueAffiliation;
		Label.AlertLevel = State.CurrentAlertLevel;
		Label.FlightLevel = FMath::RoundToInt(State.Altitude / 100.f);
		// Cesium mirror to match scope. - TripleA
		const float HdgMirrored = FMath::Fmod(FMath::Fmod(360.f - State.Heading, 360.f) + 360.f, 360.f);
		Label.HeadingDeg = FMath::RoundToInt(HdgMirrored) % 360;
		Label.SpeedKts = FMath::RoundToInt(State.Speed);
		Out.Add(Label);
	}

	return Out;
}

TArray<FInstructorCameraLine> AClearanceSimulationController::GetCameraOverlayLines() const
{
	TArray<FInstructorCameraLine> Out;
	if (!AirspaceManager || !InstructorPipCapture || !InstructorPipRT) { return Out; }

	const FVector ViewLocation = InstructorPipCapture->GetComponentLocation();
	const FRotator ViewRotation = InstructorPipCapture->GetComponentRotation();
	const FVector ViewForward = ViewRotation.Vector();
	const FVector ViewUp = ViewRotation.RotateVector(FVector::UpVector);
	const float FOVDegrees = InstructorPipCapture->FOVAngle;
	const int32 Width = InstructorPipRT->SizeX;
	const int32 Height = InstructorPipRT->SizeY;
	if (Width <= 0 || Height <= 0) { return Out; }

	const FMatrix ViewMatrix = FLookFromMatrix(ViewLocation, ViewForward, ViewUp);
	const float HalfFOVRad = FMath::DegreesToRadians(FOVDegrees * 0.5f);
	const FMatrix ProjMatrix = FPerspectiveMatrix(HalfFOVRad,
		static_cast<float>(Width), static_cast<float>(Height), GNearClippingPlane);
	const FMatrix ViewProj = ViewMatrix * ProjMatrix;

	// Anything closer than this along the camera forward axis is treated as
	// behind the near plane. Bigger than the actual GNearClippingPlane to
	// keep the W division well-conditioned (a point that's technically in
	// front but only by a few cm projects to ridiculous UVs). - TripleA
	constexpr float NearClipDist = 1000.f; // 10 m

	auto ProjectToUV = [&](const FVector& WorldPos, FVector2D& OutUV) -> bool
	{
		if (FVector::DotProduct(WorldPos - ViewLocation, ViewForward) < NearClipDist) { return false; }
		const FPlane Clip = ViewProj.TransformFVector4(FVector4(WorldPos, 1.f));
		if (Clip.W <= KINDA_SMALL_NUMBER) { return false; }
		OutUV.X = (Clip.X / Clip.W + 1.f) * 0.5f;
		OutUV.Y = 1.f - (Clip.Y / Clip.W + 1.f) * 0.5f;
		return true;
	};

	// Liang-Barsky line clip slightly inside [0, 1] - a 1.5% inset on each
	// side absorbs line thickness (a 3 px line at the very edge would
	// otherwise extend ~1.5 px past the image boundary because Slate
	// strokes are centred on the endpoint). Returns false if the segment
	// misses the image entirely. - TripleA
	auto ClipUV = [](FVector2D& P1, FVector2D& P2) -> bool
	{
		constexpr float Inset = 0.015f;
		constexpr float Xmin = Inset, Xmax = 1.f - Inset;
		constexpr float Ymin = Inset, Ymax = 1.f - Inset;

		const float Dx = P2.X - P1.X;
		const float Dy = P2.Y - P1.Y;

		float Tin = 0.f, Tout = 1.f;
		const float P[4] = {-Dx, Dx, -Dy, Dy};
		const float Q[4] = {P1.X - Xmin, Xmax - P1.X, P1.Y - Ymin, Ymax - P1.Y};

		for (int32 i = 0; i < 4; ++i)
		{
			if (FMath::Abs(P[i]) < KINDA_SMALL_NUMBER)
			{
				if (Q[i] < 0.f) { return false; }
				continue;
			}
			const float U = Q[i] / P[i];
			if (P[i] < 0.f) { Tin = FMath::Max(Tin, U); }
			else { Tout = FMath::Min(Tout, U); }
		}

		if (Tin > Tout) { return false; }

		const FVector2D Original = P1;
		P1 = Original + FVector2D(Tin * Dx, Tin * Dy);
		P2 = Original + FVector2D(Tout * Dx, Tout * Dy);
		return true;
	};

	// Project a line segment. World-space clip against the near plane so
	// both endpoints sit at >= NearClipDist before they hit ProjectToUV,
	// then UV-space Liang-Barsky clip so the segment is bounded to the
	// visible image (plus a one-viewport pad) - lines crossing the camera
	// view are kept regardless of how far their far end projects to. - TripleA
	auto AddLine = [&](FVector A, FVector B, const FLinearColor& Color, float Thickness)
	{
		const float DotA = FVector::DotProduct(A - ViewLocation, ViewForward);
		const float DotB = FVector::DotProduct(B - ViewLocation, ViewForward);
		if (DotA < NearClipDist && DotB < NearClipDist) { return; }

		if (DotA < NearClipDist)
		{
			const float T = (NearClipDist - DotA) / (DotB - DotA);
			A = A + FMath::Clamp(T, 0.f, 1.f) * (B - A);
		}
		else if (DotB < NearClipDist)
		{
			const float T = (NearClipDist - DotB) / (DotA - DotB);
			B = B + FMath::Clamp(T, 0.f, 1.f) * (A - B);
		}

		FVector2D StartUV, EndUV;
		if (!ProjectToUV(A, StartUV) || !ProjectToUV(B, EndUV)) { return; }
		if (!ClipUV(StartUV, EndUV)) { return; }

		FInstructorCameraLine Line;
		Line.StartUV = StartUV;
		Line.EndUV = EndUV;
		Line.Color = Color;
		Line.Thickness = Thickness;
		Out.Add(Line);
	};

	const FVector Origin = GetActorLocation();
	const float S = WorldUnitsPerNm;

	// Approach corridor extends from threshold all the way out to the
	// top-of-descent. 100 nm on a 3° slope reaches ~31,800 ft - matches
	// where arrival traffic spawns, so the glide line actually meets the
	// sky instead of dying just above the runway. - TripleA
	constexpr float ApproachLengthNm = 100.f;

	// Runway outline pulses too - gentle, narrow alpha range so it always
	// reads as the primary structural element. Corridor + glide-slope use
	// a wider/deeper pulse so they feel like secondary guidance overlays.
	// Same sine source so they breathe in sync. Tick marks + dashed
	// centerline stay steady - they're reference paint, not guides. - TripleA
	const float NowSec = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f);
	const float Pulse01 = 0.5f + 0.5f * FMath::Sin(NowSec * 2.f * PI * 0.7f); // 0..1 @ 0.7 Hz
	const float RunwayAlpha   = 0.65f + 0.30f * Pulse01;   // 0.65–0.95
	const float ApproachAlpha = 0.20f + 0.40f * Pulse01;   // 0.20–0.60

	const FLinearColor RunwayColor(0.16f, 0.86f, 1.f, RunwayAlpha);
	const FLinearColor CenterDashColor(1.f, 1.f, 1.f, 0.95f);
	// Magenta for the descent guides - VFR sectional convention for
	// controlled airspace, and the complementary contrast against warm
	// golden-hour skies keeps the lines readable when orange got eaten
	// by the sunset. - TripleA
	const FLinearColor ApproachLineColor(1.0f, 0.18f, 0.85f, ApproachAlpha);
	const FLinearColor ApproachTickColor(1.0f, 0.18f, 0.85f, 0.55f);
	const FLinearColor GlideLineColor   (0.85f, 0.20f, 1.0f, ApproachAlpha);
	const FLinearColor GlideTickColor   (0.85f, 0.20f, 1.0f, 0.55f);

	// Suppress on Overview (top-down map - the guides clutter without
	// adding info) and Chase (riding the aircraft - the line would streak
	// across the frame). Everything else - Tower, Approach, Operator -
	// gets the guides. - TripleA
	const bool bShowApproachGuides =
		(InstructorPipView != EClearanceCameraView::Overview) &&
		(InstructorPipView != EClearanceCameraView::Follow);

	// Runway outline rectangle reads as a tactical-map element - useful
	// from above (Overview, Chase) but in the first-person 3D views
	// (Tower, Approach, Operator) the asphalt is already obvious from the
	// mesh and the cyan box looks like a debug overlay. Same logic for
	// the dashed centreline against the Operator view: the player is
	// looking at real painted markings, no need to overdraw them. - TripleA
	const bool bShowRunwayOutline =
		(InstructorPipView != EClearanceCameraView::Tower) &&
		(InstructorPipView != EClearanceCameraView::Approach) &&
		(InstructorPipView != EClearanceCameraView::Operator);
	// Centerline hides on Tower, Approach, and Operator - all three are
	// first-person views looking down or along the strip, and any Z drift
	// between the mesh top (which the overlay uses as its ground plane) and
	// what the eye sees on the terrain projects to a visible screen-space
	// offset at oblique angles. Overview and Chase keep it because top-down
	// or elevated-behind angles cancel that error. - TripleA
	const bool bShowRunwayCenterline =
		(InstructorPipView != EClearanceCameraView::Operator) &&
		(InstructorPipView != EClearanceCameraView::Tower) &&
		(InstructorPipView != EClearanceCameraView::Approach);

	// Wind picks the landing end - only that runway gets the corridor /
	// glide-slope / fix labels, the reciprocal end stays clean. Updates
	// live when the instructor changes wind because GetActiveRunway()
	// is recalculated on each SetWind call. - TripleA
	const float ActiveRwyHdg = AirspaceManager->GetActiveRunway();

	for (const FRunwayInfo& Rwy : AirspaceManager->GetAllRunways())
	{
		const FVector ThrW(Origin.X + Rwy.ThresholdNm.X * S,
			Origin.Y + Rwy.ThresholdNm.Y * S, GroundWorldZ);

		const float RadHeading = FMath::DegreesToRadians(Rwy.HeadingDeg);
		const FVector InboundDir(FMath::Sin(RadHeading), FMath::Cos(RadHeading), 0.f);
		const FVector RightDir(FMath::Cos(RadHeading), -FMath::Sin(RadHeading), 0.f);
		// Fallbacks if the runway has no mesh bounds yet - rather than draw
		// a zero-sized rectangle, ship something visible. - TripleA
		const float RwyLen = (Rwy.LengthUnits > 0.f) ? Rwy.LengthUnits : 30000.f;
		const float RwyWid = (Rwy.WidthUnits  > 0.f) ? Rwy.WidthUnits  : 4500.f;
		const float HalfW  = RwyWid * 0.5f;

		// Four corners of the runway rectangle, near edge then far. - TripleA
		const FVector NL = ThrW - RightDir * HalfW;
		const FVector NR = ThrW + RightDir * HalfW;
		const FVector FL = NL + InboundDir * RwyLen;
		const FVector FR = NR + InboundDir * RwyLen;

		// Depth-aware occlusion is off across the board. Overview used to
		// depth-trace so the outline "hid behind" terrain in a stylised map
		// view, but the line trace hits Cesium tile collision meshes long
		// before it reaches the tarmac at Warton - every segment fails the
		// visibility check and the entire blue rectangle vanishes. Chase and
		// the first-person views already wanted the outline to act as a
		// persistent HUD element regardless of geometry between camera and
		// asphalt, so disabling depth-occlude everywhere is the consistent
		// fix. - TripleA
		const bool bDepthOcclude = false;
		auto SegmentVisible = [&](const FVector& A, const FVector& B) -> bool
		{
			if (!bDepthOcclude || !GetWorld()) { return true; }
			const FVector Mid = (A + B) * 0.5f;
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(InstructorRunwayOverlayVis), false);
			Params.AddIgnoredActor(this);
			if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, Mid, ECC_Visibility, Params))
			{
				return true;
			}
			const float DistMid = FVector::Distance(ViewLocation, Mid);
			const float DistHit = FVector::Distance(ViewLocation, Hit.ImpactPoint);
			return DistHit >= DistMid - 200.f;
		};
		if (bShowRunwayOutline)
		{
			if (SegmentVisible(NL, NR)) { AddLine(NL, NR, RunwayColor, 3.f); } // near edge (threshold bar)
			if (SegmentVisible(FL, FR)) { AddLine(FL, FR, RunwayColor, 3.f); } // far edge
			if (SegmentVisible(NL, FL)) { AddLine(NL, FL, RunwayColor, 3.f); } // left edge
			if (SegmentVisible(NR, FR)) { AddLine(NR, FR, RunwayColor, 3.f); } // right edge
		}

		// Dashed centerline. ICAO Annex 14 spec: 30m stripe + 20m gap. Evenly
		// re-tile across whatever length the runway actually is so the pattern
		// always fits, instead of clipping the last stripe mid-paint. - TripleA
		if (bShowRunwayCenterline)
		{
			constexpr float DashLen = 3000.f; // 30 m
			constexpr float GapLen  = 2000.f; // 20 m
			const int32 DashCount = FMath::Max(1, FMath::FloorToInt(RwyLen / (DashLen + GapLen)));
			const float Cycle = RwyLen / static_cast<float>(DashCount);
			const float DashFrac = DashLen / (DashLen + GapLen);
			const float DashLenAdj = Cycle * DashFrac;
			const float Margin = (Cycle - DashLenAdj) * 0.5f; // start the first dash off the threshold by half a gap
			for (int32 i = 0; i < DashCount; ++i)
			{
				const float StartT = Margin + i * Cycle;
				const float EndT   = StartT + DashLenAdj;
				AddLine(ThrW + InboundDir * StartT, ThrW + InboundDir * EndT, CenterDashColor, 2.f);
			}
		}

		if (!bShowApproachGuides) { continue; }
		// Skip the reciprocal end - wind says this isn't the landing
		// direction. Heading-equality tolerance handles the 360<->0 wrap. - TripleA
		const float HdgDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(Rwy.HeadingDeg, ActiveRwyHdg));
		if (HdgDelta > 0.5f) { continue; }

		const FVector AppEnd = ThrW - InboundDir * (ApproachLengthNm * S);
		AddLine(ThrW, AppEnd, ApproachLineColor, 2.f);

		// Range tick marks perpendicular to the corridor at standard ILS
		// fix distances - 1 / 3 / 5 / 10 nm. Gives the instructor a quick
		// "how far out is he?" reference without a separate readout. - TripleA
		const float TickHalfWid = 1500.f; // 15 m to either side of centerline
		const float RangeNm[] = {1.f, 3.f, 5.f, 10.f, 20.f, 50.f, 100.f};
		for (float R : RangeNm)
		{
			if (R > ApproachLengthNm) { continue; }
			const FVector TickCentre = ThrW - InboundDir * (R * S);
			const FVector TickL = TickCentre - RightDir * TickHalfWid;
			const FVector TickR = TickCentre + RightDir * TickHalfWid;
			AddLine(TickL, TickR, ApproachTickColor, 1.5f);
		}

		// 3° glide-slope from the threshold extending back along the
		// approach. Standard ILS slope - 31,800 ft at 100 nm. World Z runs
		// through AltitudeToWorldZOffset so the line tracks the same
		// exaggerated-altitude curve aircraft do - a plane on the slope
		// sits ON the line, not far above it. Drawn as multiple segments
		// so the non-linear power curve renders smoothly instead of as a
		// straight line that diverges from the true profile. - TripleA
		constexpr float GlideSlopeDeg = 3.f;
		constexpr int32 GlideSegments = 24;
		constexpr float FtPerNm = 6076.12f;
		const float GlideTan = FMath::Tan(FMath::DegreesToRadians(GlideSlopeDeg));
		FVector GlidePrev = ThrW;
		for (int32 i = 1; i <= GlideSegments; ++i)
		{
			const float SegFrac = static_cast<float>(i) / static_cast<float>(GlideSegments);
			const float DistNm = SegFrac * ApproachLengthNm;
			const float DistUnits = DistNm * S;
			const float AltFt = DistNm * FtPerNm * GlideTan;
			const float WorldZ = AltitudeToWorldZOffset(AltFt);
			const FVector GlidePoint(
				ThrW.X - InboundDir.X * DistUnits,
				ThrW.Y - InboundDir.Y * DistUnits,
				ThrW.Z + WorldZ);
			AddLine(GlidePrev, GlidePoint, GlideLineColor, 2.f);
			GlidePrev = GlidePoint;
		}

		// Altitude ticks on the glide slope at the same ranges as the
		// horizontal corridor ticks - little horizontal bars across the
		// slope at each ATC reference distance, perpendicular to the
		// runway's right axis (same orientation as the ground tick
		// marks), so the instructor reads "10 nm out the aircraft should
		// be at this altitude" straight off the line. Labels come from
		// GetCameraOverlayText using the same range table. - TripleA
		const float AltTickHalfWid = 1500.f; // 15 m bar each side
		for (float R : RangeNm)
		{
			if (R > ApproachLengthNm) { continue; }
			const float DistUnits = R * S;
			const float AltFt = R * FtPerNm * GlideTan;
			const float WorldZ = AltitudeToWorldZOffset(AltFt);
			const FVector TickCentre(
				ThrW.X - InboundDir.X * DistUnits,
				ThrW.Y - InboundDir.Y * DistUnits,
				ThrW.Z + WorldZ);
			const FVector TickL = TickCentre - RightDir * AltTickHalfWid;
			const FVector TickR = TickCentre + RightDir * AltTickHalfWid;
			AddLine(TickL, TickR, GlideTickColor, 1.5f);
		}
	}

	// Restricted + protected airspace zones - rendered as ground-level
	// circles so the instructor can see hostile no-fly bubbles and
	// civilian avoid areas overlaid on the camera feed. Colours follow
	// MIL-STD-2525 affiliation: red for protected (hostile-must-not-
	// reach), amber for restricted (civilian-must-avoid). Same gentle
	// pulse as the runway so they read as airspace structure rather
	// than debug draws. Shown on every camera view - tactical
	// boundaries you always want visible. - TripleA
	if (UWorld* W = GetWorld())
	{
		const FLinearColor ProtectedColor (1.f, 0.18f, 0.18f, 0.40f + 0.45f * Pulse01);
		const FLinearColor RestrictedColor(1.f, 0.72f, 0.05f, 0.40f + 0.45f * Pulse01);
		constexpr int32 ZoneSegments = 36;

		auto DrawZone = [&](const FVector& Centre, float RadiusW, const FLinearColor& Color)
		{
			if (RadiusW <= 0.f) { return; }
			FVector PrevPt(Centre.X + RadiusW, Centre.Y, Centre.Z);
			for (int32 i = 1; i <= ZoneSegments; ++i)
			{
				const float Theta = (static_cast<float>(i) / ZoneSegments) * 2.f * PI;
				const FVector NextPt(
					Centre.X + RadiusW * FMath::Cos(Theta),
					Centre.Y + RadiusW * FMath::Sin(Theta),
					Centre.Z);
				AddLine(PrevPt, NextPt, Color, 2.f);
				PrevPt = NextPt;
			}
		};

		// Wireframe-cylinder rendering: ground ring + ceiling ring + four
		// vertical spokes at the cardinal points. Communicates that the
		// zone is an airspace volume, not just a painted line on the dirt.
		// Skipped on Overview - from straight above the ceiling ring lands
		// on top of the ground ring and just reads as two overlapping
		// circles. Ceiling sits at FL500 worth of world Z (passes through
		// the same altitude curve aircraft use) so the column reaches the
		// top of the airspace anything realistic can fly in. - TripleA
		const bool bDrawColumn = (InstructorPipView != EClearanceCameraView::Overview);
		const float ZoneCeilingZ = AltitudeToWorldZOffset(50000.f);

		auto DrawZoneShape = [&](const FVector& GroundCentre, float RadiusW, const FLinearColor& Color)
		{
			if (RadiusW <= 0.f) { return; }
			DrawZone(GroundCentre, RadiusW, Color);
			if (!bDrawColumn) { return; }
			const FVector CeilingCentre(GroundCentre.X, GroundCentre.Y, GroundCentre.Z + ZoneCeilingZ);
			DrawZone(CeilingCentre, RadiusW, Color);
			for (int32 i = 0; i < 4; ++i)
			{
				const float Theta = static_cast<float>(i) * (PI * 0.5f);
				const FVector SpokeBase(
					GroundCentre.X + RadiusW * FMath::Cos(Theta),
					GroundCentre.Y + RadiusW * FMath::Sin(Theta),
					GroundCentre.Z);
				const FVector SpokeTop(SpokeBase.X, SpokeBase.Y, SpokeBase.Z + ZoneCeilingZ);
				AddLine(SpokeBase, SpokeTop, Color, 1.5f);
			}
		};

		for (TActorIterator<AClearanceViolationZone> It(W); It; ++It)
		{
			if (!*It) { continue; }
			const FVector Loc = It->GetActorLocation();
			DrawZoneShape(FVector(Loc.X, Loc.Y, GroundWorldZ), It->RadiusNm * S, ProtectedColor);
		}
		for (TActorIterator<AClearanceRestrictedArea> It(W); It; ++It)
		{
			if (!*It) { continue; }
			const FVector Loc = It->GetActorLocation();
			DrawZoneShape(FVector(Loc.X, Loc.Y, GroundWorldZ), It->RadiusNm * S, RestrictedColor);
		}
	}

	// Sector boundary - the outer edge of the instructor's controlled
	// airspace at ExitRadiusNm. Drawn as a ring on the ground in a
	// tactical green so it's distinct from runway cyan / zone red. Same
	// gentle pulse as the rest of the structural overlays. Heading
	// labels around the perimeter come from GetCameraOverlayText. - TripleA
	if (ExitRadiusNm > 0.f)
	{
		const FLinearColor SectorRingColor(0.30f, 0.90f, 0.55f, 0.30f + 0.35f * Pulse01);
		constexpr int32 SectorSegments = 72;
		const float SectorRadiusW = ExitRadiusNm * S;
		FVector PrevPt(Origin.X + SectorRadiusW, Origin.Y, GroundWorldZ);
		for (int32 i = 1; i <= SectorSegments; ++i)
		{
			const float Theta = (static_cast<float>(i) / SectorSegments) * 2.f * PI;
			const FVector NextPt(
				Origin.X + SectorRadiusW * FMath::Cos(Theta),
				Origin.Y + SectorRadiusW * FMath::Sin(Theta),
				GroundWorldZ);
			AddLine(PrevPt, NextPt, SectorRingColor, 2.f);
			PrevPt = NextPt;
		}
	}

	return Out;
}

TArray<FInstructorCameraText> AClearanceSimulationController::GetCameraOverlayText() const
{
	TArray<FInstructorCameraText> Out;
	if (!AirspaceManager || !InstructorPipCapture || !InstructorPipRT) { return Out; }

	const FVector ViewLocation = InstructorPipCapture->GetComponentLocation();
	const FRotator ViewRotation = InstructorPipCapture->GetComponentRotation();
	const FVector ViewForward = ViewRotation.Vector();
	const FVector ViewUp = ViewRotation.RotateVector(FVector::UpVector);
	const float FOVDegrees = InstructorPipCapture->FOVAngle;
	const int32 Width = InstructorPipRT->SizeX;
	const int32 Height = InstructorPipRT->SizeY;
	if (Width <= 0 || Height <= 0) { return Out; }

	const FMatrix ViewMatrix = FLookFromMatrix(ViewLocation, ViewForward, ViewUp);
	const float HalfFOVRad = FMath::DegreesToRadians(FOVDegrees * 0.5f);
	const FMatrix ProjMatrix = FPerspectiveMatrix(HalfFOVRad,
		static_cast<float>(Width), static_cast<float>(Height), GNearClippingPlane);
	const FMatrix ViewProj = ViewMatrix * ProjMatrix;

	auto ProjectToUV = [&](const FVector& WorldPos, FVector2D& OutUV) -> bool
	{
		if (FVector::DotProduct(WorldPos - ViewLocation, ViewForward) < 1000.f) { return false; }
		const FPlane Clip = ViewProj.TransformFVector4(FVector4(WorldPos, 1.f));
		if (Clip.W <= KINDA_SMALL_NUMBER) { return false; }
		OutUV.X = (Clip.X / Clip.W + 1.f) * 0.5f;
		OutUV.Y = 1.f - (Clip.Y / Clip.W + 1.f) * 0.5f;
		return true;
	};

	const TArray<FRunwayInfo>& All = AirspaceManager->GetAllRunways();
	const int32 N = All.Num();

	// Bucket runways by base designator (heading rounded to nearest 10) so
	// parallels get L/C/R suffixes - identical convention to
	// GetApproachRunwayLabels so the on-camera text and the picker buttons
	// agree. - TripleA
	TArray<int32> Designator;
	Designator.SetNum(N);
	for (int32 i = 0; i < N; ++i)
	{
		int32 D;
		if (All[i].DesignatorOverride > 0)
		{
			D = All[i].DesignatorOverride;
		}
		else
		{
			const float MagBearingI = FMath::Fmod(FMath::Fmod(360.f - All[i].HeadingDeg, 360.f) + 360.f, 360.f);
			D = FMath::RoundToInt(MagBearingI / 10.f);
		}
		if (D <= 0) { D = 36; }
		if (D > 36) { D = D % 36; if (D == 0) { D = 36; } }
		Designator[i] = D;
	}

	const FVector Origin = GetActorLocation();
	const float S = WorldUnitsPerNm;
	// Pure white for the reciprocal / inactive designator - matches real-world
	// runway paint. Active end (the one wind picked for landing) gets amber so
	// it's obvious at a glance which threshold traffic is using. - TripleA
	const FLinearColor TextColor      (1.00f, 1.00f, 1.00f, 0.95f);
	const FLinearColor ActiveTextColor(1.00f, 0.60f, 0.10f, 1.00f);
	const float ActiveRunwayHdg = AirspaceManager->GetActiveRunway();

	for (int32 i = 0; i < N; ++i)
	{
		const FRunwayInfo& Me = All[i];
		const int32 MyDes = Designator[i];

		TArray<int32> Group;
		for (int32 j = 0; j < N; ++j)
		{
			if (Designator[j] == MyDes) { Group.Add(j); }
		}

		FString Suffix;
		if (Group.Num() > 1)
		{
			const float Rad = FMath::DegreesToRadians(Me.HeadingDeg);
			const FVector2D RightDir(FMath::Cos(Rad), -FMath::Sin(Rad));
			const float MyProj = FVector2D::DotProduct(Me.ThresholdNm, RightDir);
			int32 MoreRight = 0, MoreLeft = 0;
			for (int32 j : Group)
			{
				if (j == i) { continue; }
				const float P = FVector2D::DotProduct(All[j].ThresholdNm, RightDir);
				if (P > MyProj) { ++MoreRight; }
				if (P < MyProj) { ++MoreLeft; }
			}
			if (Group.Num() == 2)
			{
				Suffix = (MoreRight == 0) ? TEXT("R") : TEXT("L");
			}
			else
			{
				if (MoreLeft == 0)       { Suffix = TEXT("L"); }
				else if (MoreRight == 0) { Suffix = TEXT("R"); }
				else                     { Suffix = TEXT("C"); }
			}
		}

		const float RadHeading = FMath::DegreesToRadians(Me.HeadingDeg);
		const FVector InboundDir(FMath::Sin(RadHeading), FMath::Cos(RadHeading), 0.f);
		const FVector ThrW(Origin.X + Me.ThresholdNm.X * S,
			Origin.Y + Me.ThresholdNm.Y * S, GroundWorldZ);

		FVector2D ThrUV;
		if (!ProjectToUV(ThrW, ThrUV)) { continue; }

		// Perpendicular to the runway direction in screen space - puts the
		// label to the SIDE of the runway rather than at the end (which
		// is where the approach corridor extends to on the active end).
		// Pick whichever perpendicular has the larger downward component
		// so labels sit consistently below the runway, never floating
		// where the corridor / glide-slope live. Offset magnitude scales
		// with the runway's apparent size in screen (Len) - a fixed 4%
		// UV nudge throws the label miles off in Chase view where the
		// runway might only span 1% of frame at 12 km distance. - TripleA
		FVector2D OutwardDir(0.f, 1.f);
		float OffsetMag = 0.04f;
		FVector2D InsideUV;
		if (ProjectToUV(ThrW + InboundDir * 10000.f, InsideUV))
		{
			const FVector2D Delta = InsideUV - ThrUV;
			const float Len = Delta.Length();
			if (Len > KINDA_SMALL_NUMBER)
			{
				FVector2D Perp(-Delta.Y, Delta.X);
				if (Perp.Y < 0.f) { Perp = -Perp; }
				OutwardDir = Perp / Len;
				OffsetMag = FMath::Clamp(Len * 0.8f, 0.005f, 0.04f);
			}
		}

		FVector2D UV = ThrUV + OutwardDir * OffsetMag;
		if (UV.X < 0.02f || UV.X > 0.98f || UV.Y < 0.02f || UV.Y > 0.98f) { continue; }

		const bool bIsActive = (ActiveRunwayHdg >= 0.f) && FMath::IsNearlyEqual(Me.HeadingDeg, ActiveRunwayHdg, 0.5f);

		FInstructorCameraText Entry;
		Entry.Text = FString::Printf(TEXT("%02d%s"), MyDes, *Suffix);
		Entry.ScreenUV = UV;
		Entry.Color = bIsActive ? ActiveTextColor : TextColor;
		Entry.FontSize = 24;
		Out.Add(Entry);

		// Approach-guide text labels stripped - the tick marks themselves
		// already convey "this is a reference distance" without needing
		// numeric overlays to decode. Keeps the camera feed cleaner for
		// portfolio capture; FL / NM digits read as clutter to anyone
		// who isn't an aviation native. - TripleA
	}

	// Zone designators - name floats over the centre of each protected /
	// restricted circle so the instructor reads what they're looking at
	// without having to memorise the actor list. Same red / amber palette
	// as the circle rings, solid alpha so they're always legible (no
	// pulse - text breathing is harder to read than steady). - TripleA
	if (UWorld* W = GetWorld())
	{
		const FLinearColor ProtectedTextColor (1.f, 0.40f, 0.40f, 0.95f);
		const FLinearColor RestrictedTextColor(1.f, 0.78f, 0.20f, 0.95f);

		auto EmitZoneLabel = [&](const FVector& Centre, const FString& Text, const FLinearColor& Color)
		{
			FVector2D ZoneUV;
			if (!ProjectToUV(Centre, ZoneUV)) { return; }
			if (ZoneUV.X < 0.02f || ZoneUV.X > 0.98f || ZoneUV.Y < 0.02f || ZoneUV.Y > 0.98f) { return; }

			FInstructorCameraText ZoneEntry;
			ZoneEntry.Text = Text;
			ZoneEntry.ScreenUV = ZoneUV;
			ZoneEntry.Color = Color;
			ZoneEntry.FontSize = 16;
			Out.Add(ZoneEntry);
		};

		for (TActorIterator<AClearanceViolationZone> It(W); It; ++It)
		{
			if (!*It) { continue; }
			const FVector Loc = It->GetActorLocation();
			const FVector Centre(Loc.X, Loc.Y, GroundWorldZ);
			const FString Name = It->ZoneName.IsNone() ? TEXT("PROTECTED") : It->ZoneName.ToString().ToUpper();
			EmitZoneLabel(Centre, Name, ProtectedTextColor);
		}
		for (TActorIterator<AClearanceRestrictedArea> It(W); It; ++It)
		{
			if (!*It) { continue; }
			const FVector Loc = It->GetActorLocation();
			const FVector Centre(Loc.X, Loc.Y, GroundWorldZ);
			const FString Name = It->AreaName.IsNone() ? TEXT("RESTRICTED") : It->AreaName.ToString().ToUpper();
			EmitZoneLabel(Centre, Name, RestrictedTextColor);
		}
	}

	// Compass headings around the sector ring at every 30°. Same green
	// as the ring itself so they read as the boundary's annotation.
	// Position is just inside the ring (90% radius) so the digits sit
	// on the asphalt-side rather than floating in the void. - TripleA
	if (ExitRadiusNm > 0.f)
	{
		const FVector RingOrigin = GetActorLocation();
		const FLinearColor SectorTextColor(0.45f, 1.f, 0.65f, 0.85f);
		const float SectorRadiusW = ExitRadiusNm * S * 0.90f;
		for (int32 Hdg = 0; Hdg < 360; Hdg += 30)
		{
			// Cesium at Warton mirrors world +X to visual west. Place label
			// "090" at world -X so it visually reads on the east side of the
			// sector ring (screen right on Overview). Matches the scope's
			// compass which uses the same mirror convention. - TripleA
			const float HdgRad = FMath::DegreesToRadians(static_cast<float>(Hdg));
			const FVector HdgPos(
				RingOrigin.X + -FMath::Sin(HdgRad) * SectorRadiusW,
				RingOrigin.Y +  FMath::Cos(HdgRad) * SectorRadiusW,
				GroundWorldZ + 1500.f);

			FVector2D HdgUV;
			if (!ProjectToUV(HdgPos, HdgUV)) { continue; }
			if (HdgUV.X < 0.02f || HdgUV.X > 0.98f || HdgUV.Y < 0.02f || HdgUV.Y > 0.98f) { continue; }

			FInstructorCameraText HdgEntry;
			HdgEntry.Text = FString::Printf(TEXT("%03d"), Hdg);
			HdgEntry.ScreenUV = HdgUV;
			HdgEntry.Color = SectorTextColor;
			HdgEntry.FontSize = 14;
			Out.Add(HdgEntry);
		}
	}

	return Out;
}

void AClearanceSimulationController::UpdateInstructorPip(float DeltaSeconds)
{
	if (!bInstructorPipEnabled || !InstructorPipCapture || !InstructorPipRT)
	{
		return;
	}
	if (!AirspaceManager)
	{
		return;
	}

	// Compute the camera transform inline from replicated state instead of
	// looking up an ACameraActor pointer. The CameraActor pointers don't
	// resolve reliably on clients (replication of references to non-replicated
	// actors is a known UE gotcha) so we derive the same transforms from
	// data that IS replicated: sector centre (this actor's location),
	// SectorEnvironment (replicated on AirspaceManager), and class-default
	// UPROPERTYs which are identical on every machine. - TripleA
	const FVector Origin = GetActorLocation();
	const float S = WorldUnitsPerNm;
	const FSectorEnvironment Env = AirspaceManager->GetCurrentEnvironment();

	const FVector ThrW(Origin.X + Env.ActiveRunwayThreshold.X * S,
		Origin.Y + Env.ActiveRunwayThreshold.Y * S,
		GroundWorldZ);
	const float FAC = (Env.ActiveRunwayHeading >= 0.f) ? Env.ActiveRunwayHeading : 270.f;
	const float FacRad = FMath::DegreesToRadians(FAC);
	const FVector InboundDir(FMath::Sin(FacRad), FMath::Cos(FacRad), 0.f);

	FVector TargetLoc;
	FRotator TargetRot;
	float TargetFOV = 80.f;

	switch (InstructorPipView)
	{
	case EClearanceCameraView::Overview:
	{
		// Strict top-down centred on the sector origin, north-up. Default
		// altitude tuned so the sector ring fills ~85% of the frame.
		// Pan offset + zoom level are mutated by AddOverviewPan /
		// AddOverviewZoom from UMG drag + scroll events. - TripleA
		const float DefaultAlt = ExitRadiusNm * S * 1.45f;
		const float ZoomedAlt = DefaultAlt / FMath::Max(0.001f, InstructorOverviewZoomLevel);
		TargetLoc = Origin + FVector(InstructorOverviewPanOffsetUnits.X,
			InstructorOverviewPanOffsetUnits.Y, ZoomedAlt);
		TargetRot = FRotator(-90.f, 90.f, 0.f);
		TargetFOV = 90.f;
		break;
	}
	case EClearanceCameraView::Tower:
	{
		// If the designer has tagged an actor in the level with "ClearanceTower"
		// (a tower building mesh, a placed marker, anything) the camera uses
		// THAT actor's world transform. Otherwise fall back to 50m above the
		// runway threshold looking down the approach corridor. Tag-based so
		// no new class is needed - just drop a tag on whatever mesh you've
		// got. - TripleA
		AActor* TowerAnchor = nullptr;
		if (UWorld* W = GetWorld())
		{
			static const FName TowerTag(TEXT("ClearanceTower"));
			for (TActorIterator<AActor> It(W); It; ++It)
			{
				if (*It && It->ActorHasTag(TowerTag))
				{
					TowerAnchor = *It;
					break;
				}
			}
		}

		if (TowerAnchor)
		{
			// Camera sits exactly where the tagged actor is. Place a
			// TargetPoint (or any empty actor) at the cab / window position
			// you want and tag it; the camera transform is literal. - TripleA
			TargetLoc = TowerAnchor->GetActorLocation();
			FRotator BaseRot = TowerAnchor->GetActorRotation();
			BaseRot.Yaw += InstructorTowerYawDeg;
			TargetRot = BaseRot;
		}
		else
		{
			TargetLoc = ThrW + FVector(0.f, 0.f, 5000.f);
			const FVector LookAt = ThrW - InboundDir * (20.f * S);
			FRotator BaseRot = (LookAt - TargetLoc).Rotation();
			BaseRot.Yaw += InstructorTowerYawDeg;
			TargetRot = BaseRot;
		}
		// Tower-cab wide field. Real ATC tower windows are panoramic;
		// 110 deg gives the instructor most of the apron + the active
		// runway + a chunk of approach in one frame. - TripleA
		TargetFOV = 110.f;
		break;
	}
	case EClearanceCameraView::Approach:
	{
		// Use the selected runway from GetAllRunways instead of the wind-active
		// one in Env, so the runway picker selects which one to frame. - TripleA
		const TArray<FRunwayInfo>& AllRunways = AirspaceManager->GetAllRunways();
		FVector2D RwyThrNm(Env.ActiveRunwayThreshold.X, Env.ActiveRunwayThreshold.Y);
		float RwyHeading = FAC;
		if (AllRunways.Num() > 0)
		{
			const int32 Idx = FMath::Clamp(InstructorApproachRunwayIndex, 0, AllRunways.Num() - 1);
			RwyThrNm = AllRunways[Idx].ThresholdNm;
			RwyHeading = AllRunways[Idx].HeadingDeg;
		}
		static int32 ApproachLogTick = 0;
		if (++ApproachLogTick % 60 == 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[PIP] Approach: this=%p auth=%d idx=%d total=%d thr=(%.0f,%.0f) hdg=%.0f"),
				this, HasAuthority() ? 1 : 0,
				InstructorApproachRunwayIndex, AllRunways.Num(),
				RwyThrNm.X, RwyThrNm.Y, RwyHeading);
		}
		const FVector RwyThrW(Origin.X + RwyThrNm.X * S, Origin.Y + RwyThrNm.Y * S, GroundWorldZ);
		const float RwyRad = FMath::DegreesToRadians(RwyHeading);
		const FVector RwyInboundDir(FMath::Sin(RwyRad), FMath::Cos(RwyRad), 0.f);
		const FVector RwyRightPerp(FMath::Cos(RwyRad), -FMath::Sin(RwyRad), 0.f);

		// 3/4 angle: elevated, off to the side of the threshold, looking at
		// a point along the runway. Frames the runway extending into the
		// distance with the corridor visible behind it. Aircraft on final
		// approach come from the back of the frame toward the threshold. - TripleA
		// 3/4 frame centred on the threshold, pulled further back so the
		// corridor + glide-slope have room to extend behind. Looking AT
		// the threshold keeps the runway as the foreground anchor; the
		// wider 100 deg FOV + bigger side / elevation reads more like a
		// proper approach-controller observation post than a close
		// fly-by. - TripleA
		const float SideDistance = 250000.f;  // 2.5 km off to the side
		const float Elevation    = 120000.f;  // 1.2 km up
		TargetLoc = RwyThrW + RwyRightPerp * SideDistance + FVector(0.f, 0.f, Elevation);
		TargetRot = (RwyThrW - TargetLoc).Rotation();
		TargetFOV = 100.f;
		break;
	}
	case EClearanceCameraView::Follow:
	{
		const FName Follow = !InstructorPipFollowCallsign.IsNone()
			? InstructorPipFollowCallsign
			: FollowTargetCallsign;
		if (Follow.IsNone())
		{
			return;
		}
		const FAircraftState St = AirspaceManager->GetAircraftState(Follow);
		if (!St.bIsValid)
		{
			return;
		}
		// Read POSITION from the visual actor (whatever the renderer is about
		// to draw the mesh at) so the camera and the mesh stay perfectly
		// locked - no per-frame drift within the chase frame.
		// Read HEADING from the replicated state (compass convention) - it's
		// the same value UpdateVisuals used to compute the actor's rotation,
		// and computing the camera Forward vector from compass heading is
		// straightforward. Mixing in the actor's UE-Yaw + YawOffsetDeg here
		// gets the angles wrong. - TripleA
		FVector Aircraft;
		float MeshHalfLength = 750.f; // medium-aircraft default if no visual yet
		const FSpawnedAircraftVisual* Visual = VisualActors.Find(Follow);
		if (Visual && Visual->Actor)
		{
			Aircraft = Visual->Actor->GetActorLocation();
			// Estimate the aircraft's half-length from its world-axis bounds.
			// max(X, Y) catches whichever axis the plane is aligned along; we
			// pad 1.3x to compensate for the bounding box shrinking when the
			// plane is rotated off-axis. Used to scale chase pull-back and
			// cockpit forward-offset so big planes don't end up in-frame too
			// close, and so the cockpit cam reaches the nose on a 747. - TripleA
			FVector BoundsOrigin, BoundsExtent;
			Visual->Actor->GetActorBounds(false, BoundsOrigin, BoundsExtent);
			MeshHalfLength = FMath::Max(BoundsExtent.X, BoundsExtent.Y) * 1.3f;
		}
		else
		{
			Aircraft = WorldPositionFor(St);
		}
		const float HeadingRad = FMath::DegreesToRadians(St.Heading);
		const FVector Forward(FMath::Sin(HeadingRad), FMath::Cos(HeadingRad), 0.f);
		const FVector Right(FMath::Cos(HeadingRad), -FMath::Sin(HeadingRad), 0.f);
		const FVector Up(0.f, 0.f, 1.f);
		switch (InstructorPipFollowAngle)
		{
		case EClearanceFollowAngle::Cockpit:
		{
			// Push forward to where the cockpit windows actually are - on a
			// typical airliner that's ~80% of the half-length ahead of the
			// mesh origin. Plus a small height offset for the windscreen
			// height. - TripleA
			const float CockpitFwd = FMath::Max(80.f, MeshHalfLength * 0.8f);
			const float CockpitUp  = FMath::Max(60.f, MeshHalfLength * 0.12f);
			TargetLoc = Aircraft + Forward * CockpitFwd + Up * CockpitUp;
			TargetRot = Forward.Rotation();
			break;
		}
		case EClearanceFollowAngle::Side:
			TargetLoc = Aircraft + Right * 1800.f + Up * 500.f;
			TargetRot = (Aircraft - TargetLoc).Rotation();
			break;
		case EClearanceFollowAngle::Top:
			TargetLoc = Aircraft + Up * 3500.f;
			TargetRot = (Aircraft - TargetLoc).Rotation();
			break;
		case EClearanceFollowAngle::Chase:
		default:
		{
			// Pull back ~2.2x half-length so a 747 frames properly without the
			// camera being inside the fuselage. Floor at 1500 so a tiny
			// Cessna doesn't end up infinitely close. Height proportional too
			// so the angle stays cinematic. - TripleA
			const float ChaseDist   = FMath::Max(1500.f, MeshHalfLength * 2.2f);
			const float ChaseHeight = FMath::Max(600.f,  MeshHalfLength * 0.45f);
			TargetLoc = Aircraft - Forward * ChaseDist + Up * ChaseHeight;
			TargetRot = (Aircraft - TargetLoc).Rotation();
			break;
		}
		}
		break;
	}
	case EClearanceCameraView::Operator:
	{
		// Use the operator's pushed view transform directly. AClearanceOperatorPC
		// updates OperatorViewRotation/Location every ~30ms from its local
		// GetControlRotation() and pawn eye location - that bypasses every UE
		// rotation-replication footgun (yaw-only Character replication, missing
		// pitch for non-Character pawns, no roll anywhere) and gives the
		// instructor exactly what the operator's camera sees. - TripleA
		if (OperatorViewLocation.IsZero())
		{
			return; // no data yet - operator PC hasn't ticked
		}
		TargetLoc = OperatorViewLocation;
		TargetRot = OperatorViewRotation;
		TargetFOV = 90.f;
		break;
	}
	default:
	{
		TargetLoc = ThrW + FVector(0.f, 0.f, 600.f);
		TargetRot = FRotator::ZeroRotator;
		break;
	}
	}

	InstructorPipCapture->SetWorldLocationAndRotation(TargetLoc, TargetRot);
	InstructorPipCapture->FOVAngle = TargetFOV;

	// Tell the texture streamer about this view BEFORE capturing. Without this
	// the SceneCapture renders against whatever low-mip placeholder textures
	// happen to be in memory - which is why aircraft appeared as flat white
	// blobs while the same mesh looks fine in the main viewport. Adding a
	// view location requests proper streaming for everything in this frustum. - TripleA
	if (InstructorPipRT)
	{
		const float ScreenSize = static_cast<float>(InstructorPipRT->SizeX);
		const float FOVScreenSize = ScreenSize / FMath::Max(0.001f, FMath::Tan(FMath::DegreesToRadians(TargetFOV * 0.5f)));
		IStreamingManager::Get().AddViewInformation(TargetLoc, ScreenSize, FOVScreenSize);
	}

	const float Interval = 1.f / FMath::Max(1.f, InstructorPipCaptureRateHz);
	InstructorPipCaptureAccum += DeltaSeconds;
	if (InstructorPipCaptureAccum >= Interval)
	{
		InstructorPipCaptureAccum = 0.f;
		InstructorPipCapture->CaptureScene();
	}
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

void AClearanceSimulationController::SetSpawnerScenarioLocked(bool bLocked)
{
	if (Spawner) { Spawner->SetScenarioLocked(bLocked); }
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

	// Missiles register themselves into the airspace so the instructor list
	// + camera modes pick them up, but they run their own guidance tick and
	// carry their own mesh - no Behaviour, no CommsRouter registration, no
	// aircraft-variant visual on top. - TripleA
	const bool bMissile = AirspaceManager
		? AirspaceManager->GetAircraftState(Callsign).bIsMissile
		: false;

	if (bMissile)
	{
		// Nothing else to wire; the AClearanceMissile actor owns everything.
		return;
	}

	if (!bExternal)
	{
		// Server-only: creates state-mutating Behaviour; clients receive state via OnRep only. - TripleA
		if (HasAuthority())
		{
			UClearanceAircraftBehaviour* Behaviour = NewObject<UClearanceAircraftBehaviour>(this);
			Behaviour->Initialise(AirspaceManager, Callsign);
			Behaviour->TouchdownZoneOffsetNm = FMath::Max(0.f, TouchdownZoneMeters) / 1852.f; // metres -> nm
			BehaviourMap.Add(Callsign, Behaviour);
			if (CommsRouter) { CommsRouter->RegisterBehaviour(Callsign, Behaviour); }
		}

		// If the freshly-spawned aircraft is a bandit profile (NORDO, not friendly
		// or neutral), redirect it AT a random violation zone instead of leaving
		// it pointed at sector centre. A real intruder has an objective - that's
		// what makes the operator's intercept call meaningful. Civilians (Neutral)
		// with broken IFF are NOT redirected - they're still civilian traffic. - TripleA
		// Server-only: bandit redirect mutates state via RequestStateUpdate. - TripleA
		if (HasAuthority() && AirspaceManager && GetWorld() && !bZoneChecksSuspended)
		{
			const FAircraftState S = AirspaceManager->GetAircraftState(Callsign);
			if (S.bIsValid && !S.bIFFOperational
				&& S.ThreatClass != EThreatClass::Friendly
				&& S.ThreatClass != EThreatClass::Neutral)
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
			// Deterministic pick by callsign hash so server + client land on the
			// same variant. FMath::RandRange rolled independently per peer and the
			// meshes drifted - same data, different model. - TripleA
			const int32 VariantIdx = static_cast<int32>(GetTypeHash(Callsign) % static_cast<uint32>(Variants.Num()));
			const FAircraftVisualVariant& Variant = Variants[VariantIdx];
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
	EverEnteredSector.Remove(Callsign); // reset entered-flag if the callsign reappears later
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
	{
		const FString NMsg = FString::Printf(TEXT("%s: %s / %s - %.1f nm, %.0f ft%s"), Lvl,
			*Conflict.AircraftA.ToString(), *Conflict.AircraftB.ToString(),
			Conflict.HorizontalSeparationNm, Conflict.VerticalSeparationFt,
			Conflict.bRequiresGoAround ? TEXT(" [GO-AROUND]") : TEXT(""));
		PushNotification(NMsg, ColourFor(Conflict.AlertLevel), 5.f);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, ColourFor(Conflict.AlertLevel), NMsg);
		}
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
		const FString NMsg = FString::Printf(TEXT("RESOLVED: %s / %s (+%d)"),
			*Conflict.AircraftA.ToString(), *Conflict.AircraftB.ToString(), Scoring->PointsResolution);
		PushNotification(NMsg, FColor::Green, 4.f);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green, NMsg);
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
		const FString Msg = FString::Printf(TEXT("%s %s %.0f -> %s"), Label, *Callsign.ToString(), Value, *UEnum::GetDisplayValueAsText(Result).ToString());
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

static void ClearanceAutopilotToggleFromConsole(const TArray<FString>& Args, UWorld* World, bool bEngage)
{
	const TCHAR* Label = bEngage ? TEXT("engage") : TEXT("disengage");
	if (Args.Num() < 1 || !World)
	{
		UE_LOG(LogTemp, Warning, TEXT("clearance.autopilot.%s <callsign>"), Label);
		return;
	}
	const FName Callsign(*Args[0]);
	for (TActorIterator<AClearanceSimulationController> It(World); It; ++It)
	{
		const bool bOk = It->SetAircraftAutopilotEngaged(Callsign, bEngage);
		const FString Msg = FString::Printf(TEXT("autopilot %s %s -> %s"), Label, *Callsign.ToString(),
			bOk ? TEXT("OK") : TEXT("callsign not found"));
		UE_LOG(LogTemp, Display, TEXT("%s"), *Msg);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, Msg); }
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("clearance.autopilot.%s: no SimulationController in the world"), Label);
}

static FAutoConsoleCommandWithWorldAndArgs GClearanceAutopilotEngageCmd(
	TEXT("clearance.autopilot.engage"),
	TEXT("clearance.autopilot.engage <callsign> - hand control to the Simulink cascade autopilot"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		ClearanceAutopilotToggleFromConsole(Args, World, true);
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceAutopilotDisengageCmd(
	TEXT("clearance.autopilot.disengage"),
	TEXT("clearance.autopilot.disengage <callsign> - return control to the built-in behaviour"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		ClearanceAutopilotToggleFromConsole(Args, World, false);
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

// --- Electronic Warfare console commands -------------------------------------
// Toggle jamming on an aircraft: degrades that radar's reads on it plus
// blankets the bearing arc to other contacts in the same wedge. Operator
// sees one radar lose the picture, fused track stays up because the other
// radars cover from a different angle. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceJamCmd(
	TEXT("clearance.ew.jam"),
	TEXT("clearance.ew.jam <callsign> <on|off> - toggle a jammer on the named aircraft"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("usage: clearance.ew.jam <callsign> <on|off>")); return; }
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C || !C->GetAirspaceManager()) { return; }

		const FName Callsign(*Args[0]);
		const bool bOn = (Args.Num() < 2) ? true : (Args[1].ToLower() != TEXT("off") && Args[1] != TEXT("0"));

		FAircraftState S = C->GetAirspaceManager()->GetAircraftState(Callsign);
		if (!S.bIsValid)
		{
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, FString::Printf(TEXT("ew.jam: no aircraft '%s'"), *Callsign.ToString())); }
			return;
		}
		S.bJammingOn = bOn;
		C->GetAirspaceManager()->RequestStateUpdate(S);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 4.f, bOn ? FColor::Red : FColor::Cyan,
				FString::Printf(TEXT("EW: jammer %s on %s"), bOn ? TEXT("ON") : TEXT("OFF"), *Callsign.ToString()));
		}
	}));

// Have an aircraft drop a chaff cloud at its current position. Every radar in
// line of sight reports it as a low-confidence ghost with no transponder for
// the next ~12 seconds. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceChaffCmd(
	TEXT("clearance.ew.chaff"),
	TEXT("clearance.ew.chaff <callsign> - drop a chaff cloud at the named aircraft's position"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("usage: clearance.ew.chaff <callsign>")); return; }
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C || !C->GetAirspaceManager()) { return; }

		const FName Callsign(*Args[0]);
		const FAircraftState S = C->GetAirspaceManager()->GetAircraftState(Callsign);
		if (!S.bIsValid)
		{
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Red, FString::Printf(TEXT("ew.chaff: no aircraft '%s'"), *Callsign.ToString())); }
			return;
		}
		C->GetAirspaceManager()->DropChaff(S.Position, S.Altitude);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor(255, 220, 0), FString::Printf(TEXT("EW: chaff dropped at %s"), *Callsign.ToString())); }
	}));

// Per-world mute - in PIE, run this in the server console to silence its
// VoiceOutput while still hearing the client's. World param routes naturally to
// the local instance. Args: on / off / toggle (default toggle). - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceAudioMuteCmd(
	TEXT("clearance.audio.mute"),
	TEXT("clearance.audio.mute [on|off|toggle] - mute this window's TTS output"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		int32 Found = 0;
		for (TActorIterator<AClearanceVoiceOutput> It(World); It; ++It)
		{
			AClearanceVoiceOutput* V = *It;
			if (!V) { continue; }
			bool bNew = !V->bMuted;
			if (Args.Num() >= 1)
			{
				const FString A = Args[0].ToLower();
				if (A == TEXT("on") || A == TEXT("1"))  { bNew = true;  }
				else if (A == TEXT("off") || A == TEXT("0")) { bNew = false; }
			}
			V->bMuted = bNew;
			++Found;
		}
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Cyan,
				FString::Printf(TEXT("audio mute applied to %d VoiceOutput(s)"), Found));
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

// ============================================================================
// Instructor console commands - sent from any client window, routed through the
// local PlayerController's Server RPC, executed on the server. - TripleA
// ============================================================================

static AClearanceOperatorPC* FindLocalOperatorPC(UWorld* World)
{
	if (!World) { return nullptr; }
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		return Cast<AClearanceOperatorPC>(PC);
	}
	return nullptr;
}

static FAutoConsoleCommandWithWorldAndArgs GClearanceInstrEmergencyCmd(
	TEXT("clearance.instructor.emergency"),
	TEXT("clearance.instructor.emergency <callsign> <Mayday|CommsFailure|Hijack|FuelLow> - inject an emergency"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2) { UE_LOG(LogTemp, Warning, TEXT("usage: clearance.instructor.emergency <callsign> <kind>")); return; }
		AClearanceOperatorPC* PC = FindLocalOperatorPC(World);
		if (!PC) { UE_LOG(LogTemp, Warning, TEXT("[Instructor] no AClearanceOperatorPC - set it as the default PlayerController in your GameMode")); return; }
		EEmergencyType Kind = EEmergencyType::None;
		const FString K = Args[1].ToLower();
		if      (K == TEXT("mayday") || K == TEXT("7700"))           { Kind = EEmergencyType::GeneralMayday; }
		else if (K == TEXT("commsfailure") || K == TEXT("7600"))     { Kind = EEmergencyType::CommsFailure; }
		else if (K == TEXT("hijack") || K == TEXT("7500"))           { Kind = EEmergencyType::Hijack; }
		else if (K == TEXT("fuellow") || K == TEXT("fuel"))          { Kind = EEmergencyType::FuelLow; }
		else { UE_LOG(LogTemp, Warning, TEXT("unknown emergency type: %s"), *Args[1]); return; }
		PC->Server_InjectEmergency(FName(*Args[0]), Kind);
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceInstrSpawnCmd(
	TEXT("clearance.instructor.spawn"),
	TEXT("clearance.instructor.spawn - inject a random civilian spawn"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AClearanceOperatorPC* PC = FindLocalOperatorPC(World)) { PC->Server_InjectSpawn(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceInstrClearCmd(
	TEXT("clearance.instructor.clear"),
	TEXT("clearance.instructor.clear - wipe all sector traffic"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AClearanceOperatorPC* PC = FindLocalOperatorPC(World)) { PC->Server_InjectClearTraffic(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceInstrWindCmd(
	TEXT("clearance.instructor.wind"),
	TEXT("clearance.instructor.wind <dirDeg> <speedKts> - set sector wind"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2) { return; }
		if (AClearanceOperatorPC* PC = FindLocalOperatorPC(World))
		{
			PC->Server_InjectSetWind(FCString::Atof(*Args[0]), FCString::Atof(*Args[1]));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceInstrLoadCmd(
	TEXT("clearance.instructor.scenario.load"),
	TEXT("clearance.instructor.scenario.load <name> - load a scenario on the server"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { return; }
		if (AClearanceOperatorPC* PC = FindLocalOperatorPC(World)) { PC->Server_InjectLoadScenario(Args[0]); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceInstrStopCmd(
	TEXT("clearance.instructor.scenario.stop"),
	TEXT("clearance.instructor.scenario.stop - stop the running scenario"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AClearanceOperatorPC* PC = FindLocalOperatorPC(World)) { PC->Server_InjectStopScenario(); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceInstrPauseCmd(
	TEXT("clearance.instructor.pause"),
	TEXT("clearance.instructor.pause <0|1> - pause/unpause the sim on the server"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		const bool b = (Args.Num() > 0) ? (Args[0] != TEXT("0")) : true;
		if (AClearanceOperatorPC* PC = FindLocalOperatorPC(World)) { PC->Server_InjectSetPaused(b); }
	}));

// Counts the number of Controllers + AirspaceManagers per world - if either is
// greater than 1 there's a duplicate that explains state-divergence bugs. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceNetActorsCmd(
	TEXT("clearance.net.actors"),
	TEXT("clearance.net.actors - dump count of Controller + AirspaceManager actors in the local world"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (!World) { return; }
		const TCHAR* Role = World->GetNetMode() == NM_Client ? TEXT("CLIENT") : TEXT("SERVER");
		int32 NumCtl = 0, NumMgr = 0;
		FString CtlSigs, MgrSigs;
		for (TActorIterator<AClearanceSimulationController> It(World); It; ++It)
		{
			++NumCtl;
			CtlSigs += FString::Printf(TEXT(" %s%s"), *It->GetName(), It->HasAuthority() ? TEXT("(A)") : TEXT(""));
		}
		for (TActorIterator<AClearanceAirspaceManager> It(World); It; ++It)
		{
			++NumMgr;
			MgrSigs += FString::Printf(TEXT(" %s[ac=%d]"), *It->GetName(), It->GetAircraftCount());
		}
		UE_LOG(LogTemp, Display, TEXT("[NET %s] Controllers=%d:%s   Managers=%d:%s"),
			Role, NumCtl, *CtlSigs, NumMgr, *MgrSigs);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Yellow,
				FString::Printf(TEXT("[NET %s] Ctl=%d Mgr=%d"), Role, NumCtl, NumMgr));
		}
	}));

// Diagnostic: prints aircraft count from BOTH the world-iterator-found Manager
// AND the Controller's UPROPERTY pointer. If they diverge, the Controller is
// holding a stale or different Manager than the world has. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceNetCountCmd(
	TEXT("clearance.net.count"),
	TEXT("clearance.net.count - prints AirspaceManager count from iterator + from Controller.AirspaceManager"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (!World) { return; }
		const TCHAR* Role = World->GetNetMode() == NM_Client ? TEXT("CLIENT")
		                  : World->GetNetMode() == NM_ListenServer ? TEXT("SERVER")
		                  : World->GetNetMode() == NM_DedicatedServer ? TEXT("DED-SERVER")
		                  : TEXT("STANDALONE");

		AClearanceAirspaceManager* MIter = nullptr;
		for (TActorIterator<AClearanceAirspaceManager> It(World); It; ++It) { MIter = *It; break; }

		AClearanceSimulationController* C = nullptr;
		for (TActorIterator<AClearanceSimulationController> It(World); It; ++It) { C = *It; break; }

		auto Dump = [](AClearanceAirspaceManager* M) -> FString {
			if (!M) { return TEXT("null"); }
			const TArray<FAircraftState> All = M->GetAllAircraftStates();
			FString Out = FString::Printf(TEXT("%s[ac=%d]"), *M->GetName(), All.Num());
			for (int32 i = 0; i < FMath::Min(5, All.Num()); ++i)
			{
				Out += FString::Printf(TEXT(" %s"), *All[i].Callsign.ToString());
			}
			return Out;
		};

		const FString IterStr = Dump(MIter);
		const FString PtrStr  = C ? Dump(C->GetAirspaceManager()) : FString(TEXT("no controller"));
		const bool bSame = MIter && C && (MIter == C->GetAirspaceManager());

		UE_LOG(LogTemp, Display, TEXT("[NET %s] iter=%s  ctrl=%s  match=%s"),
			Role, *IterStr, *PtrStr, bSame ? TEXT("YES") : TEXT("NO"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 10.f, bSame ? FColor::Green : FColor::Red,
				FString::Printf(TEXT("[NET %s] iter=%s ctrl=%s %s"),
					Role, *IterStr, *PtrStr, bSame ? TEXT("MATCH") : TEXT("DIVERGE")));
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

// Resolved at runtime so dev-builds + packaged builds find the same Scenarios folder. - TripleA
static FString ClearanceScenarioDir()
{
	return FPaths::ProjectPluginsDir() / TEXT("ClearanceSim/Scenarios");
}

static FAutoConsoleCommandWithWorldAndArgs GClearanceScenarioListCmd(
	TEXT("clearance.scenario.list"),
	TEXT("clearance.scenario.list - list scenario .json files in Plugins/ClearanceSim/Scenarios"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* /*World*/)
	{
		const FString Dir = ClearanceScenarioDir();
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Dir / TEXT("*.json")), true, false);
		if (Found.Num() == 0)
		{
			UE_LOG(LogTemp, Display, TEXT("[Scenario] no .json files in %s"), *Dir);
			return;
		}
		UE_LOG(LogTemp, Display, TEXT("[Scenario] %d scenario(s) in %s:"), Found.Num(), *Dir);
		for (const FString& F : Found) { UE_LOG(LogTemp, Display, TEXT("  - %s"), *F); }
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceScenarioLoadCmd(
	TEXT("clearance.scenario.load"),
	TEXT("clearance.scenario.load <name> - load + start a scenario (name without .json)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("usage: clearance.scenario.load <name>")); return; }
		AClearanceSimulationController* C = FindClearanceController(World);
		if (!C || !C->GetScenarioRunner()) { UE_LOG(LogTemp, Warning, TEXT("[Scenario] controller / runner missing")); return; }

		const FString Name = Args[0].EndsWith(TEXT(".json")) ? Args[0] : Args[0] + TEXT(".json");
		const FString Path = ClearanceScenarioDir() / Name;
		FString Err;
		if (!C->GetScenarioRunner()->LoadFromFile(Path, Err))
		{
			UE_LOG(LogTemp, Error, TEXT("[Scenario] load failed: %s"), *Err);
			return;
		}
		// Hard-lock the spawner BEFORE clearing so it can't tick in between and
		// repopulate the manager with pre-scenario traffic. The lock is independent
		// of bAutoSpawn, so free-play preference is preserved. - TripleA
		C->SetSpawnerScenarioLocked(true);
		C->ClearTraffic();
		C->GetScenarioRunner()->Start();
		UE_LOG(LogTemp, Display, TEXT("[Scenario] running: %s"), *C->GetScenarioRunner()->GetLoadedName());
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceScenarioStopCmd(
	TEXT("clearance.scenario.stop"),
	TEXT("clearance.scenario.stop - stop the running scenario"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& /*Args*/, UWorld* World)
	{
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			if (C->GetScenarioRunner()) { C->GetScenarioRunner()->Stop(); }
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

// DDS pub/sub controls. Route through the operator PC's Server RPC so the
// command works from either the server OR a client console in listen-server
// PIE mode - the server-authoritative SimulationController is the only one
// with a live DDSEmitter, and the client-side replicated stub has null. - TripleA
static AClearanceOperatorPC* FindClearanceOperatorPC(UWorld* World)
{
	if (!World) { return nullptr; }
	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		return Cast<AClearanceOperatorPC>(PC);
	}
	return nullptr;
}

static FAutoConsoleCommandWithWorldAndArgs GClearanceDDSStartCmd(
	TEXT("clearance.dds.start"),
	TEXT("clearance.dds.start [domain] - start publishing DDS topics on the given DDS domain (default 0)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		const int32 Domain = (Args.Num() >= 1) ? FCString::Atoi(*Args[0]) : 0;
		if (AClearanceOperatorPC* PC = FindClearanceOperatorPC(World))
		{
			PC->Server_InjectStartDDSEmit(Domain);   // Routes to server-authoritative controller
		}
		else if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->StartDDSEmitter(Domain);              // Standalone mode fallback
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceDDSStopCmd(
	TEXT("clearance.dds.stop"),
	TEXT("clearance.dds.stop - stop publishing DDS topics"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (AClearanceOperatorPC* PC = FindClearanceOperatorPC(World))
		{
			PC->Server_InjectStopDDSEmit();
		}
		else if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->StopDDSEmitter();
		}
	}));

// RTI Connext publish controls. Sits alongside Fast DDS as the third wire.
// Default domain is 1 so RTI and Fast DDS coexist without stepping on each
// other's discovery. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceRTIStartCmd(
	TEXT("clearance.rti.start"),
	TEXT("clearance.rti.start [domain] - start publishing RTI Connext DDS topics on the given domain (default 1)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		const int32 Domain = (Args.Num() >= 1) ? FCString::Atoi(*Args[0]) : 1;
		if (AClearanceOperatorPC* PC = FindClearanceOperatorPC(World))
		{
			PC->Server_InjectStartRTIEmit(Domain);
		}
		else if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->StartRTIEmitter(Domain);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceRTIStopCmd(
	TEXT("clearance.rti.stop"),
	TEXT("clearance.rti.stop - stop publishing RTI Connext DDS topics"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (AClearanceOperatorPC* PC = FindClearanceOperatorPC(World))
		{
			PC->Server_InjectStopRTIEmit();
		}
		else if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->StopRTIEmitter();
		}
	}));

// HLA federate controls - fourth wire, IEEE 1516-2010. Needs an rtinode
// listening on the loopback (or LAN) and a FOM XML the federation resolves
// to. Defaults join federation "CLEARANCE" as federate "CLEARANCE-Instructor"
// with the RPR-FOM extension XML shipped alongside the runtime. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceHLAJoinCmd(
	TEXT("clearance.hla.join"),
	TEXT("clearance.hla.join [federation] [federate] [fomPath] - join an HLA federation execution."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		const FString Federation = (Args.Num() >= 1) ? Args[0] : TEXT("CLEARANCE");
		const FString Federate   = (Args.Num() >= 2) ? Args[1] : TEXT("CLEARANCE-Instructor");
		const FString FomPath    = (Args.Num() >= 3) ? Args[2]
			: FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("ClearanceSim/FOM/ClearanceRPR-FOM.xml"));

		if (AClearanceOperatorPC* PC = FindClearanceOperatorPC(World))
		{
			PC->Server_InjectStartHLAJoin(Federation, Federate, FomPath);
		}
		else if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->StartHLAFederate(Federation, Federate, FomPath);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceHLAResignCmd(
	TEXT("clearance.hla.resign"),
	TEXT("clearance.hla.resign - resign from the HLA federation"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (AClearanceOperatorPC* PC = FindClearanceOperatorPC(World))
		{
			PC->Server_InjectStopHLAJoin();
		}
		else if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->StopHLAFederate();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceDDSListenCmd(
	TEXT("clearance.dds.listen"),
	TEXT("clearance.dds.listen [domain] - ingest DDS clearance/aircraft/state samples from peer federates on the given domain. Default 0."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		const int32 Domain = (Args.Num() >= 1) ? FCString::Atoi(*Args[0]) : 0;
		if (AClearanceOperatorPC* PC = FindClearanceOperatorPC(World))
		{
			PC->Server_InjectStartDDSRecv(Domain);
		}
		else if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->StartDDSReceiver(Domain);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceDDSUnlistenCmd(
	TEXT("clearance.dds.unlisten"),
	TEXT("clearance.dds.unlisten - stop ingesting DDS aircraft samples"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld* World)
	{
		if (AClearanceOperatorPC* PC = FindClearanceOperatorPC(World))
		{
			PC->Server_InjectStopDDSRecv();
		}
		else if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			C->StopDDSReceiver();
		}
	}));

// In-process DDS subscriber - creates its own participant on the given
// domain, subscribes to all six clearance/* topics, and logs each sample
// as it arrives. Proves the publish path end-to-end without needing an
// external process. Instantiate one instance globally so it survives past
// the console command scope. - TripleA
#include "ClearanceDDS/ClearanceDDSSubscriber.h"
static std::unique_ptr<ClearanceDDS::FClearanceSubscriber> GClearanceDDSSubscriberInstance;

static FAutoConsoleCommandWithWorldAndArgs GClearanceDDSSubscribeCmd(
	TEXT("clearance.dds.subscribe"),
	TEXT("clearance.dds.subscribe [domain] - start an in-process DDS subscriber that logs every received sample. Domain 0 default."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld*)
	{
		const std::uint32_t Domain = (Args.Num() >= 1)
			? static_cast<std::uint32_t>(FCString::Atoi(*Args[0]))
			: 0u;

		ClearanceDDS::FSubscriberHandlers H;
		H.OnAircraftState = [](const ClearanceDDS::AircraftState& S)
		{
			UE_LOG(LogTemp, Display, TEXT("[DDS-Sub] AircraftState entity=%u marking=%hs pos=(%.1f,%.1f,%.1f) v=(%.1f,%.1f,%.1f)"),
				S.EntityNumber(), S.Marking().c_str(),
				S.XMeters(), S.YMeters(), S.ZMeters(),
				S.VxMps(),   S.VyMps(),   S.VzMps());
		};
		H.OnFireEvent = [](const ClearanceDDS::FireEvent& E)
		{
			UE_LOG(LogTemp, Display, TEXT("[DDS-Sub] FireEvent firer=%u target=%u munition=%u event=%u"),
				E.FiringEntity(), E.TargetEntity(), E.MunitionEntity(), E.EventNumber());
		};
		H.OnDetonationEvent = [](const ClearanceDDS::DetonationEvent& E)
		{
			UE_LOG(LogTemp, Display, TEXT("[DDS-Sub] DetonationEvent firer=%u target=%u result=%u event=%u"),
				E.FiringEntity(), E.TargetEntity(), E.DetonationResult(), E.EventNumber());
		};
		H.OnEmissionSnapshot = [](const ClearanceDDS::EmissionSnapshot& S)
		{
			UE_LOG(LogTemp, Display, TEXT("[DDS-Sub] EmissionSnapshot entity=%u emitter=%u painted=%d"),
				S.EmittingEntity(), S.EmitterName(), int32(S.PaintedEntityNumbers().size()));
		};
		H.OnTransmitterState = [](const ClearanceDDS::TransmitterState& T)
		{
			UE_LOG(LogTemp, Display, TEXT("[DDS-Sub] TransmitterState entity=%u freq=%lluHz state=%u"),
				T.OwnerEntity(), (unsigned long long)T.FrequencyHz(), T.TransmitState());
		};
		H.OnSignalEvent = [](const ClearanceDDS::SignalEvent& E)
		{
			const std::string Text(reinterpret_cast<const char*>(E.Data().data()), E.Data().size());
			UE_LOG(LogTemp, Display, TEXT("[DDS-Sub] SignalEvent entity=%u radio=%u text='%hs'"),
				E.OwnerEntity(), E.RadioId(), Text.c_str());
		};

		GClearanceDDSSubscriberInstance = ClearanceDDS::FClearanceSubscriber::Create(Domain, std::move(H));
		UE_LOG(LogTemp, Display, TEXT("[DDS-Sub] subscriber on domain %u -> %s"),
			Domain, GClearanceDDSSubscriberInstance ? TEXT("OK") : TEXT("FAILED"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceDDSUnsubscribeCmd(
	TEXT("clearance.dds.unsubscribe"),
	TEXT("clearance.dds.unsubscribe - tear down the in-process DDS subscriber"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>&, UWorld*)
	{
		if (GClearanceDDSSubscriberInstance)
		{
			UE_LOG(LogTemp, Display, TEXT("[DDS-Sub] total received: %llu"),
				(unsigned long long)GClearanceDDSSubscriberInstance->GetTotalReceivedCount());
		}
		GClearanceDDSSubscriberInstance.reset();
		UE_LOG(LogTemp, Display, TEXT("[DDS-Sub] subscriber stopped"));
	}));

// Change this instance's DIS Site ID at runtime. Two copies of the sim on the
// same network need different Site IDs or each will filter the other's traffic
// as its own broadcast loopback. - TripleA
static FAutoConsoleCommandWithWorldAndArgs GClearanceDISSiteCmd(
	TEXT("clearance.dis.site"),
	TEXT("clearance.dis.site <N> - set this instance's federate identity (Site ID, default 1). Applies to BOTH DIS and DDS emitters/receivers - set different IDs on two instances so they can hear each other's traffic without loopback filtering."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { return; }
		const int32 NewSite = FCString::Atoi(*Args[0]);
		// Route through the OperatorPC's Server RPC so the update lands on the
		// server-authoritative controller. Client-side FindClearanceController
		// finds the replicated ghost which has null DIS/DDS emitters - updating
		// that would be a no-op, and the on-screen debug from GEngine would
		// falsely suggest success. Standalone mode (no PC) falls back to the
		// direct path. - TripleA
		if (AClearanceOperatorPC* PC = FindClearanceOperatorPC(World))
		{
			PC->Server_InjectSetFederateSiteId(NewSite);
		}
		else if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			if (UClearanceDISEmitter*  E = C->GetDISEmitter())  { E->SiteId      = NewSite; }
			if (UClearanceDISReceiver* R = C->GetDISReceiver()) { R->LocalSiteId = NewSite; }
			if (UClearanceDDSEmitter*  E = C->GetDDSEmitter())  { E->SiteId      = NewSite; }
			if (UClearanceDDSReceiver* R = C->GetDDSReceiver()) { R->LocalSiteId = NewSite; }
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan,
					FString::Printf(TEXT("Federate Site ID = %d (standalone path)"), NewSite));
			}
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
	TEXT("clearance.exit <callsign> - clear an aircraft to leave the sector (scores as a successful handoff when it crosses the ring)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 1) { UE_LOG(LogTemp, Warning, TEXT("usage: clearance.exit <callsign>")); return; }
		if (AClearanceSimulationController* C = FindClearanceController(World))
		{
			FAircraftInstruction I;
			I.TargetCallsign = FName(*Args[0]);
			I.Type = EInstructionType::ExitSector;
			const EInstructionResult Result = C->PlayerIssueInstruction(I);
			const FString Msg = FString::Printf(TEXT("exit %s -> %s"), *I.TargetCallsign.ToString(), *UEnum::GetDisplayValueAsText(Result).ToString());
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
		// Force sim time scale to 1x for the duration of the demo. Anything
		// higher (default is 10x, user often runs 20x) closes the pair in a
		// handful of real seconds so they look like they spawned on top of
		// each other. 1x gives a watchable ~40 sec dead-air before Advisory,
		// then the alert ladder plays out in real time. Operator can bump
		// the slider back up afterwards. - TripleA
		C->SimulationTimeScale = 1.f;
		AClearanceAirspaceManager* M = C->GetAirspaceManager();
		// 20 nm apart on the E-W line, same altitude, 340 kt each = 680 kt
		// closure. Wide enough to read as two very clearly separated symbols
		// on any scope range. At 1x sim time: Advisory ~42 sec, Warning ~58
		// sec, Critical ~68 sec, TCAS RA immediately, vertical split visible
		// well before horizontal merge. Both aircraft have MBD autopilot
		// disengaged so they use the analytic StepAltitude path which
		// respects bExpedite = true and climbs / descends at 1.5x max rate.
		// Sim default autopilot behaviour for scenario traffic is unchanged. - TripleA
		SpawnTestAircraft(M, TEXT("CONFL1"), FVector(-10.f, 0.f, 0.f), 10000.f,  90.f, 340.f, EWakeCategory::Medium);
		SpawnTestAircraft(M, TEXT("CONFL2"), FVector( 10.f, 0.f, 0.f), 10000.f, 270.f, 340.f, EWakeCategory::Medium);
		C->SetAircraftAutopilotEngaged(TEXT("CONFL1"), false);
		C->SetAircraftAutopilotEngaged(TEXT("CONFL2"), false);
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, TEXT("TEST: sim time forced 1x, CONFL1/CONFL2 head-on 20nm 340kt - ADVISORY ~42s, TCAS RA ~68s, vertical split visible before merge")); }
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
