// Static helpers the main-menu UMG widgets call for hardware detection,
// local IP lookup, and level open / client travel with the correct role
// baked into the URL. Everything is BlueprintCallable / BlueprintPure so
// the menu widgets can wire straight to nodes without a supporting
// C++ shim per button. - TripleA

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ClearanceMenuFunctionLibrary.generated.h"

class APlayerController;

// Fires when a PTT key capture completes. CapturedKey is the FKey the user
// pressed. If the user pressed Escape (cancel), CapturedKey is EKeys::Invalid
// so the BP handler can distinguish "user picked a key" from "user cancelled".
DECLARE_DYNAMIC_DELEGATE_OneParam(FClearancePTTKeyCapturedDelegate, FKey, CapturedKey);

UCLASS()
class CLEARANCE_API UClearanceMenuFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Returns true if the engine has an XR system loaded AND that system
	// reports an HMD physically connected right now. Both checks are
	// necessary - the XR module can load without a headset plugged in.
	UFUNCTION(BlueprintPure, Category = "Clearance|Menu")
	static bool IsHMDConnected();

	// Returns true if a microphone-capable audio capture device is
	// present. Uses the Voice module's capability query; conservative
	// (returns false if the module isn't loaded rather than assuming
	// yes) so we don't gate players INTO a session they can't actually
	// talk in.
	UFUNCTION(BlueprintPure, Category = "Clearance|Menu")
	static bool IsMicrophoneAvailable();

	// First non-loopback IPv4 address bound on any local adapter, as a
	// display string ("192.168.0.47"). Returns "127.0.0.1" if the host
	// genuinely has no LAN interface (unlikely in the target deployment
	// but a sane fallback).
	UFUNCTION(BlueprintPure, Category = "Clearance|Menu")
	static FString GetLocalIPv4();

	// Solo instructor: opens LVL_Warton with ?role=instructor. No listen
	// server - the sim runs single-player on this box.
	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu", meta = (WorldContext = "WorldContextObject"))
	static void OpenInstructorSessionSolo(UObject* WorldContextObject);

	// Solo operator (VR): opens LVL_Warton with ?role=operator?autonomous=1.
	// The autonomous flag tells AClearanceSimulationController to run the
	// probabilistic emergency injector, since there is no instructor at
	// the panel to trigger anything.
	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu", meta = (WorldContext = "WorldContextObject"))
	static void OpenOperatorSessionSolo(UObject* WorldContextObject);

	// Combined solo (flat, no VR): one player at a flat desk sees both the
	// operator VR pawn's camera view AND the instructor panel overlaid on
	// top. GameAndUI input mode lets mouse hit the panel while keyboard /
	// gamepad reach the pawn. Autonomous emergency injector on since the
	// same player can't inject events with one hand and react with the
	// other. Use case: solo dev testing, portfolio demos on machines
	// without VR, single-person walkthroughs. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu", meta = (WorldContext = "WorldContextObject"))
	static void OpenCombinedSessionSolo(UObject* WorldContextObject);

	// LAN host (as instructor): OpenLevel with ?listen?role=instructor so
	// the level comes up as a listen server accepting clients. The VR
	// operator on the other PC joins via JoinLANSession below.
	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu", meta = (WorldContext = "WorldContextObject"))
	static void HostLANSession(UObject* WorldContextObject);

	// LAN join (as VR operator): client-travels this PC to the given
	// host IP. Role option isn't sent - the host is authoritative and
	// this client possesses whatever pawn the host allocates for a
	// non-authority connection (operator pawn by convention).
	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu")
	static void JoinLANSession(APlayerController* PlayerController, const FString& HostIP);

	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu")
	static void QuitToDesktop(APlayerController* PlayerController);

	// Return to the launch-time main menu level. Disables the HMD first so a
	// player who was in a VR session lands on the flat desktop menu, not a
	// stereo view of it. Called by the End Session modal's EXIT button and
	// by any other "back to menu" flow. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu", meta = (WorldContext = "WorldContextObject"))
	static void ExitToMainMenu(UObject* WorldContextObject);

	// ----- Options persistence -----------------------------------------
	// All writes go through GConfig to GGameUserSettingsIni so they persist
	// across sessions the same way UE's built-in graphics settings do. No
	// custom UGameUserSettings subclass required. - TripleA

	UFUNCTION(BlueprintPure, Category = "Clearance|Menu|Options")
	static TArray<FString> GetInputDeviceNames();

	UFUNCTION(BlueprintPure, Category = "Clearance|Menu|Options")
	static int32 LoadSelectedInputDeviceIndex();

	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu|Options")
	static void SaveSelectedInputDeviceIndex(int32 DeviceIndex);

	UFUNCTION(BlueprintPure, Category = "Clearance|Menu|Options")
	static float LoadMasterVolume();

	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu|Options")
	static void SaveMasterVolume(float Volume0To1);

	UFUNCTION(BlueprintPure, Category = "Clearance|Menu|Options")
	static FKey LoadPushToTalkKey();

	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu|Options")
	static void SavePushToTalkKey(FKey Key);

	UFUNCTION(BlueprintPure, Category = "Clearance|Menu|Options")
	static bool LoadStartInVR();

	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu|Options")
	static void SaveStartInVR(bool bStartInVR);

	// Arms a global Slate input pre-processor that captures the very next
	// key the user presses (regardless of which widget has keyboard focus)
	// and fires OnCaptured with that FKey. Pressing Escape reports back
	// EKeys::Invalid so the BP handler can treat it as a cancel. The
	// capture unregisters itself after firing once, so it's safe to call
	// again on the next rebind click without any teardown from BP. If a
	// previous capture is still armed when this is called, the previous
	// one is cancelled silently. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu|Options")
	static void BeginPTTKeyCapture(const FClearancePTTKeyCapturedDelegate& OnCaptured);

	// Cancels an in-flight PTT key capture without firing the delegate.
	// Use if the user closes the options widget mid-rebind.
	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu|Options")
	static void CancelPTTKeyCapture();

	// Convenience wrapper around BeginPTTKeyCapture that binds the delegate
	// by reflection instead of needing a K2 CreateEvent node on the BP side.
	// Call as `BeginPTTKeyCaptureByName(self, "HandlePTTKeyCaptured")` from
	// the widget's rebind-button OnClicked. The named function on Target
	// must be a UFUNCTION and must accept a single FKey parameter matching
	// FClearancePTTKeyCapturedDelegate. If binding fails (bad name, wrong
	// signature) the call logs a warning and no capture is armed. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Clearance|Menu|Options", meta = (AdvancedDisplay = "0"))
	static void BeginPTTKeyCaptureByName(UObject* Target, FName FunctionName);
};
