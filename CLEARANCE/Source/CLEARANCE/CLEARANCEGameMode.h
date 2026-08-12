// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CLEARANCEGameMode.generated.h"

/**
 *  Simple GameMode for a first person game
 */
UCLASS()
class ACLEARANCEGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACLEARANCEGameMode();

	// LVL_Warton hand-places the VR operator pawn so the tower-desk transform,
	// hand mesh assignments, motion-controller wiring and IMC bindings all
	// carry through from the map. The default GameMode flow spawns a fresh
	// DefaultPawnClass at a PlayerStart and possesses THAT, leaving the
	// hand-placed pawn unpossessed (input gates never pass, triggers never
	// fire). Override RestartPlayer to hand possession to the pre-placed
	// VR pawn when one exists; falls through to the default spawn flow when
	// there isn't one, so PIE-in-an-empty-level or the flat instructor peer
	// still work as before. - TripleA
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	// True if this session was launched with ?role=instructor on the URL.
	// Read from InitGame's Options (which UE guarantees delivers the URL
	// query string on both listen-server hosts and PIE), so no dependency
	// on UWorld::URL being populated by the time RestartPlayer runs.
	UFUNCTION(BlueprintPure, Category = "Clearance|Session")
	bool IsInstructorRole() const { return bLaunchedAsInstructor; }

private:
	bool bLaunchedAsInstructor = false;
};



