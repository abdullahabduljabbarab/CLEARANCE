#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClearanceMissileLauncher.generated.h"

// -----------------------------------------------------------------------
// AClearanceMissileLauncher - placeable ground SAM battery. Drop one at
// Warton (or wherever the scenario wants the launcher) and Fire() picks
// it up automatically. The launcher's world location is converted back
// into sim-frame metres via SimulationController::WorldToSimMeters, so
// the missile spawns exactly where the mesh sits in the level and the
// engagement geometry produces a proper arc instead of the debug
// "spawn behind the target" fallback. - TripleA
// -----------------------------------------------------------------------

UCLASS(Blueprintable, BlueprintType)
class CLEARANCESIM_API AClearanceMissileLauncher : public AActor
{
	GENERATED_BODY()

public:
	AClearanceMissileLauncher();

	// Federation-facing callsign for Fire / Detonation PDUs. Multiple
	// launchers in the same scenario should each get a distinct callsign
	// so an external observer can tell which battery fired. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launcher")
	FName LauncherCallsign = FName(TEXT("SAM_01"));

	// Where the missile spawns relative to the launcher root (UE cm).
	// Default 2 m above ground so the missile clears any launch rail
	// mesh Neo drops in via the BP child. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Launcher")
	FVector MuzzleOffsetCm = FVector(0.f, 0.f, 200.f);

	// Static mesh subcomponent - visible in the level, replaceable in a
	// BP child. Default empty so a raw C++ placement still has a
	// selectable root; Neo can plug a launch-rail mesh in later. - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Launcher")
	TObjectPtr<class UStaticMeshComponent> LauncherMesh;

	// World-space location the missile should spawn from (root + offset).
	UFUNCTION(BlueprintPure, Category = "Launcher")
	FVector GetMuzzleWorldLocation() const;
};
