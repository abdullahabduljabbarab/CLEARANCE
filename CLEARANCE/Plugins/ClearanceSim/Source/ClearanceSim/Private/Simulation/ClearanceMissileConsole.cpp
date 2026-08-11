// Console-command entry points for the missile subsystem. Kept in its
// own TU so the SimulationController stays lean and the missile actor
// header doesn't have to worry about IConsoleManager. - TripleA
//
// Two commands:
//   clearance.missile.fire <launcher> <target>
//       Server-only. Spawns a missile flying from launcher aircraft
//       toward target aircraft via prop-nav guidance. Both callsigns
//       must resolve in the AirspaceManager.
//
//   clearance.missile.abort <launcher>
//       Kills every in-flight missile launched by the given launcher.
//       Useful during scenario cleanup or if a test scenario runs wild.

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Simulation/ClearanceMissile.h"
#include "Simulation/ClearanceMissileLauncher.h"
#include "Simulation/ClearanceSimulationController.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Core/CLEARANCETypes.h"

namespace ClearanceMissileConsole
{
	static UWorld* GetCommandWorld()
	{
		if (!GEngine) { return nullptr; }
		// Prefer the PIE / running-game world over the editor world so a
		// console command issued from the in-game console targets the
		// live sim, not an editor preview.
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game)
			{
				if (Ctx.World()) { return Ctx.World(); }
			}
		}
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.World()) { return Ctx.World(); }
		}
		return nullptr;
	}

	static void CmdFire(const TArray<FString>& Args)
	{
		if (Args.Num() < 1)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Usage: clearance.missile.fire <target_callsign>  (ground SAM launch)"));
			return;
		}
		UWorld* World = GetCommandWorld();
		if (!World)
		{
			UE_LOG(LogTemp, Warning, TEXT("[clearance.missile.fire] No live world found."));
			return;
		}
		const FName Target(*Args[0]);
		AClearanceMissile* Missile = AClearanceMissile::Fire(World, Target);
		if (Missile)
		{
			UE_LOG(LogTemp, Log,
				TEXT("[clearance.missile.fire] SAM launched at %s (event #%d)"),
				*Target.ToString(), Missile->GetFireEventNumber());
		}
	}

	static void CmdAbort(const TArray<FString>& Args)
	{
		UWorld* World = GetCommandWorld();
		if (!World) { return; }
		int32 Killed = 0;
		for (TActorIterator<AClearanceMissile> It(World); It; ++It)
		{
			if (It->IsInFlight())
			{
				It->Destroy();
				++Killed;
			}
		}
		UE_LOG(LogTemp, Log,
			TEXT("[clearance.missile.abort] Aborted %d in-flight SAM(s)"), Killed);
	}

	// -------------------------------------------------------------------
	// clearance.missile.spawn_test_target [range_km] [bearing_deg]
	//
	// Drops a fake civilian aircraft near the placed launcher so a
	// subsequent clearance.missile.fire produces sensible short-range
	// PDU coordinates for demo captures. Without this the sim's normal
	// aircraft-spawner scatters callsigns across the whole sector, so
	// even the "closest" existing aircraft can be 300+ km from the
	// launcher and the resulting Detonation PDU location numbers read
	// as absurd for a portfolio screenshot.
	//
	// Defaults: 50 km range, bearing 090 (east) from launcher, altitude
	// FL200 (6096 m), speed 350 kt, heading 270. Callsign is
	// DEMO_TARGET so the row is visually obvious. - TripleA
	// -------------------------------------------------------------------
	static void CmdSpawnTestTarget(const TArray<FString>& Args)
	{
		UWorld* World = GetCommandWorld();
		if (!World) { return; }

		AClearanceSimulationController* Ctrl = Cast<AClearanceSimulationController>(
			UGameplayStatics::GetActorOfClass(World, AClearanceSimulationController::StaticClass()));
		if (!Ctrl || !Ctrl->GetAirspaceManager())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[clearance.missile.spawn_test_target] No SimulationController / AirspaceManager."));
			return;
		}
		AClearanceMissileLauncher* Launcher = Cast<AClearanceMissileLauncher>(
			UGameplayStatics::GetActorOfClass(World, AClearanceMissileLauncher::StaticClass()));
		if (!Launcher)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[clearance.missile.spawn_test_target] No AClearanceMissileLauncher in the level."));
			return;
		}

		const float RangeKm   = (Args.Num() > 0) ? FCString::Atof(*Args[0]) :  50.f;
		const float BearingD  = (Args.Num() > 1) ? FCString::Atof(*Args[1]) :  90.f;

		// Launcher's sim-space position in nm (via the same inverse
		// projection the missile Fire path uses).
		const FVector LauncherMeters = Ctrl->WorldToSimMeters(Launcher->GetMuzzleWorldLocation());
		constexpr double kMetersPerNm = 1852.0;
		const double LauncherNmX = LauncherMeters.X / kMetersPerNm;
		const double LauncherNmY = LauncherMeters.Y / kMetersPerNm;

		const double RangeNm    = RangeKm * 1000.0 / kMetersPerNm;
		const double BearingRad = FMath::DegreesToRadians(BearingD);
		// atan2(X, Y) convention across the sim: bearing 0 = +Y.
		const double TgtNmX = LauncherNmX + RangeNm * FMath::Sin(BearingRad);
		const double TgtNmY = LauncherNmY + RangeNm * FMath::Cos(BearingRad);

		FAircraftState S;
		S.Callsign          = FName(TEXT("DEMO_TARGET"));
		S.Position          = FVector(TgtNmX, TgtNmY, 0.0);
		S.Altitude          = 20000.f;
		S.Speed             = 350.f;
		S.Heading           = 270.f;
		S.FlightPhase       = EFlightPhase::Enroute;
		S.WakeCategory      = EWakeCategory::Medium;
		S.ThreatClass       = EThreatClass::Neutral;
		S.TrueAffiliation   = EThreatClass::Neutral;
		S.bIFFOperational   = true;
		S.SquawkCode        = 1200;
		S.bIsValid          = true;

		if (Ctrl->GetAirspaceManager()->RegisterAircraft(S))
		{
			UE_LOG(LogTemp, Log,
				TEXT("[clearance.missile.spawn_test_target] DEMO_TARGET spawned at (%.2f, %.2f) nm "
				     "(%.1f km at bearing %.0f from launcher). Fire with: clearance.missile.fire DEMO_TARGET"),
				TgtNmX, TgtNmY, RangeKm, BearingD);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[clearance.missile.spawn_test_target] AirspaceManager rejected DEMO_TARGET "
				     "(already registered? cap hit?). Try clearance.missile.abort + retry."));
		}
	}

	static FAutoConsoleCommand CmdSpawnTestTargetHandle(
		TEXT("clearance.missile.spawn_test_target"),
		TEXT("Spawn DEMO_TARGET near the placed launcher for short-range demo shots. "
			 "Usage: clearance.missile.spawn_test_target [range_km=50] [bearing_deg=90]"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CmdSpawnTestTarget),
		ECVF_Default);

	static FAutoConsoleCommand CmdFireHandle(
		TEXT("clearance.missile.fire"),
		TEXT("Launch a ground SAM at the given aircraft. Usage: clearance.missile.fire <target_callsign>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CmdFire),
		ECVF_Default);

	static FAutoConsoleCommand CmdAbortHandle(
		TEXT("clearance.missile.abort"),
		TEXT("Abort every in-flight SAM. Usage: clearance.missile.abort"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&CmdAbort),
		ECVF_Default);
}
