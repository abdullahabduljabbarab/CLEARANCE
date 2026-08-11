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
#include "Simulation/ClearanceMissile.h"

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
