#include "ClearanceMainMenuGameMode.h"

#include "ClearanceMainMenuPC.h"
#include "ClearanceMenuPawn.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "Engine/Engine.h"

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

void AClearanceMainMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Main menu is always desktop. If the player just exited a VR session
	// and returned here, the HMD would still be active and the menu would
	// render stereoscopically inside the headset. Force stereo off on
	// entry so the menu always appears on the flat monitor. Safe no-op
	// when no HMD is connected. Belt-and-braces: hit both the high-level
	// EnableHMD and the low-level EnableStereo, and issue a console
	// stereo-off for good measure - individually any of these can be
	// ignored by the active XR runtime mid-session. - TripleA
	UHeadMountedDisplayFunctionLibrary::EnableHMD(false);
	if (GEngine && GEngine->StereoRenderingDevice.IsValid())
	{
		GEngine->StereoRenderingDevice->EnableStereo(false);
	}
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		if (IHeadMountedDisplay* HMD = GEngine->XRSystem->GetHMDDevice())
		{
			HMD->EnableHMD(false);
		}
	}
	if (UWorld* W = GetWorld())
	{
		if (APlayerController* PC = W->GetFirstPlayerController())
		{
			PC->ConsoleCommand(TEXT("stereo off"), /*bWriteToLog=*/false);
		}
	}
}
