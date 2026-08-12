#include "ClearanceMenuPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"

AClearanceMenuPawn::AClearanceMenuPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	// Bare scene root so the camera can attach to something. No collision,
	// no physics, no movement - this pawn only exists as an anchor for the
	// camera and to be possessed by the menu PC.
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->FieldOfView = 65.f;

	// Auto-possess makes the PC own this pawn the instant the game mode
	// spawns it at PlayerStart, no manual Possess() call needed. - TripleA
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	// No input reaches the pawn - the menu PC is in UIOnly mode anyway,
	// but belt-and-braces: don't let stray gamepad axes rotate the view.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw   = false;
	bUseControllerRotationRoll  = false;
}
