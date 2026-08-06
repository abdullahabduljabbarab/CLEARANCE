// Copyright Epic Games, Inc. All Rights Reserved.

#include "CLEARANCEGameMode.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "Simulation/ClearanceVROperatorPawn.h"
#include "UI/ClearanceReadoutHUD.h"
#include "EngineUtils.h"

ACLEARANCEGameMode::ACLEARANCEGameMode()
{
	// Plugin's networked operator PlayerController hosts the instructor-station
	// Server RPCs. Setting it here means every PIE / shipping play session uses
	// it automatically without needing a BP GameMode subclass. - TripleA
	PlayerControllerClass = AClearanceOperatorPC::StaticClass();

	// Custom HUD draws the simulation readout via Canvas, per-viewport - the old
	// GEngine on-screen-debug queue was lost on the client side in PIE multi-window. - TripleA
	HUDClass = AClearanceReadoutHUD::StaticClass();

	// Default pawn is the diegetic tower operator (root is a bare SceneComponent
	// with no visual, so nothing renders in-world). Without this line UE falls
	// back to ADefaultPawn - the wireframe sphere + directional arrow gizmo
	// that used to spawn on the server / listen-server host on every play (the
	// instructor client destroys its pawn in BeginPlay but the host doesn't).
	// The VR pawn works transparently in a non-VR flat play too because its
	// visuals are all HMD / motion-controller driven. - TripleA
	DefaultPawnClass = AClearanceVROperatorPawn::StaticClass();
}

void ACLEARANCEGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer) { return; }

	// Skip if this controller is already sitting on a pawn (re-entry from
	// UE's own retry loop, or a checkpoint restore that already possessed). - TripleA
	if (NewPlayer->GetPawn()) { return; }

	// LVL_Warton hand-places the VR operator pawn at the tower-desk seat with
	// hand mesh + input mapping context assignments. Prefer it over spawning
	// a fresh DefaultPawnClass at a PlayerStart - the fresh spawn would land
	// at origin with none of the level's per-instance configuration, and the
	// hand-placed pawn would sit unpossessed with the actual controller
	// binding on a phantom copy. - TripleA
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AClearanceVROperatorPawn> It(World); It; ++It)
		{
			AClearanceVROperatorPawn* Existing = *It;
			if (!Existing || Existing->IsPendingKillPending()) { continue; }
			if (Existing->GetController()) { continue; } // already owned

			NewPlayer->Possess(Existing);
			return;
		}
	}

	// No hand-placed VR pawn in this level (empty scene, test map, etc).
	// Fall through to the default spawn-at-PlayerStart flow. - TripleA
	Super::RestartPlayer(NewPlayer);
}
