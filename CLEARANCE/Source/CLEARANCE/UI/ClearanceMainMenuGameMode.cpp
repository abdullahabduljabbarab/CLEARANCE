#include "ClearanceMainMenuGameMode.h"

#include "ClearanceMainMenuPC.h"
#include "ClearanceMenuPawn.h"

AClearanceMainMenuGameMode::AClearanceMainMenuGameMode()
{
	// Menu pawn = a plain APawn with a UCameraComponent, positioned by the
	// level-placed PlayerStart. Player-owned camera auto-registers with
	// Cesium's tile streamer (UsePlayerCameras=true), so we don't need to
	// register level-placed CameraActors manually - dodges the Cesium 5.7
	// custom-camera streaming issues. - TripleA
	DefaultPawnClass = AClearanceMenuPawn::StaticClass();
	PlayerControllerClass = AClearanceMainMenuPC::StaticClass();
}
