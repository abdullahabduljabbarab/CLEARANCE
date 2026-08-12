// Copyright Epic Games, Inc. All Rights Reserved.

#include "CLEARANCEGameMode.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "Simulation/ClearanceVROperatorPawn.h"
#include "UI/ClearanceReadoutHUD.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/SpectatorPawn.h"
#include "Kismet/GameplayStatics.h"

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

void ACLEARANCEGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	// Cache the role from the URL query string. Base class also stashes it
	// into AGameModeBase::OptionsString for anyone who wants to re-parse
	// later (e.g. AClearanceOperatorPC does exactly this from the plugin
	// side, since it can't include this class's header directly). - TripleA
	const FString RoleOption = UGameplayStatics::ParseOption(Options, TEXT("role"));
	bLaunchedAsInstructor = RoleOption.Equals(TEXT("instructor"), ESearchCase::IgnoreCase);

	UE_LOG(LogTemp, Log,
		TEXT("[ClearanceGameMode] InitGame Options='%s' role='%s' bLaunchedAsInstructor=%s OptionsString='%s'"),
		*Options, *RoleOption, bLaunchedAsInstructor ? TEXT("true") : TEXT("false"), *OptionsString);
}

void ACLEARANCEGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer) { return; }

	// Skip if this controller is already sitting on a pawn (re-entry from
	// UE's own retry loop, or a checkpoint restore that already possessed). - TripleA
	if (NewPlayer->GetPawn()) { return; }

	UWorld* World = GetWorld();

	// Instructor players must NOT possess the level-placed VR pawn - that
	// pawn owns the tower-desk hand meshes, subsystem hooks, and motion-
	// controller delegates. Possessing then destroying it mid-BeginPlay
	// crashes. Give the instructor a bare ASpectatorPawn instead, leave the
	// VR pawn alone in the level. Role comes from bLaunchedAsInstructor,
	// captured in InitGame from the URL query string. - TripleA
	if (bLaunchedAsInstructor)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASpectatorPawn* Spectator = World
			? World->SpawnActor<ASpectatorPawn>(ASpectatorPawn::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params)
			: nullptr;
		if (Spectator)
		{
			NewPlayer->Possess(Spectator);
		}
		return;
	}

	// Operator path: LVL_Warton hand-places the VR operator pawn at the
	// tower-desk seat with hand mesh + input mapping context assignments.
	// Prefer it over spawning a fresh DefaultPawnClass at a PlayerStart -
	// the fresh spawn would land at origin with none of the level's per-
	// instance configuration, and the hand-placed pawn would sit unpossessed
	// with the actual controller binding on a phantom copy. - TripleA
	if (World)
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
