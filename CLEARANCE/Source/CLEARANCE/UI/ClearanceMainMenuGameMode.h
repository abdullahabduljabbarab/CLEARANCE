// Main-menu game mode. Trivially thin: fixes the pawn class to a plain
// spectator (fixed camera in the level does all the visuals, no player
// pawn needed) and swaps in the menu player controller. Everything else
// happens in the PC's BeginPlay + the UMG widget. - TripleA

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ClearanceMainMenuGameMode.generated.h"

UCLASS()
class CLEARANCE_API AClearanceMainMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AClearanceMainMenuGameMode();

	virtual void BeginPlay() override;
};
