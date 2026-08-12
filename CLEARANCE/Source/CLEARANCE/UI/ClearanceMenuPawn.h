// Minimum-viable pawn for the main menu. Just a root + camera component,
// no movement, no collision, no input. Exists so the menu PC has a
// player-owned camera to possess, which auto-registers with Cesium's
// tile streamer via UsePlayerCameras. Framing is controlled entirely by
// the level-placed PlayerStart's position and rotation. - TripleA

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "ClearanceMenuPawn.generated.h"

class UCameraComponent;

UCLASS()
class CLEARANCE_API AClearanceMenuPawn : public APawn
{
	GENERATED_BODY()

public:
	AClearanceMenuPawn();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Menu")
	TObjectPtr<UCameraComponent> Camera = nullptr;
};
