// Copyright Epic Games, Inc. All Rights Reserved.

#include "CLEARANCEGameMode.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "Simulation/ClearanceVROperatorPawn.h"
#include "UI/ClearanceReadoutHUD.h"

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
