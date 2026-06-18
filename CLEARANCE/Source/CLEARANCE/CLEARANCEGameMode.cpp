// Copyright Epic Games, Inc. All Rights Reserved.

#include "CLEARANCEGameMode.h"
#include "Simulation/ClearanceOperatorPC.h"
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
}
