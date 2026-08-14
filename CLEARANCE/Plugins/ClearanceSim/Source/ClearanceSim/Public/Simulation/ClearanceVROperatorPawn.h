#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "ClearanceVROperatorPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UWidgetInteractionComponent;
class UStaticMeshComponent;
class USkeletalMeshComponent;
class USphereComponent;
class UInputAction;
class UInputMappingContext;
class AClearanceOperatorButton;
class UPrimitiveComponent;
class UStereoLayerComponent;
class UTextureRenderTarget2D;
class FWidgetRenderer;

// Seated VR pawn for the operator role. HMD drives the camera, motion
// controllers drive left/right hand transforms, widget-interaction
// components fire a laser at the diegetic scope so the operator can
// click buttons with the trigger.
//
// Instructor stays on desktop; this pawn only exists on the operator
// client. Sitting at a virtual tower desk, so the tracking origin is
// eye-level not floor. Physical desk = the player's real desk chair,
// virtual desk = the mesh this pawn is standing next to on spawn.
// - TripleA
UCLASS(BlueprintType, Blueprintable)
class CLEARANCESIM_API AClearanceVROperatorPawn : public APawn
{
	GENERATED_BODY()

public:
	AClearanceVROperatorPawn();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	// --- Locomotion tuning -----------------------------------------------
	// Stick locomotion speed, cm/s. Real-world walking pace (~140) feels
	// like quicksand in VR because the vestibular system expects body
	// exertion to match world speed and there's none. 300 cm/s reads as
	// "comfortable brisk walk" for a seated operator crossing the cab.
	// Drop to 200 for a slower creep; push to 400 for a jog. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion")
	float MoveSpeed = 300.f;

	// Snap turn angle in degrees. 45 deg is the sweet spot: big enough to
	// feel like you've turned, small enough that you don't lose spatial
	// awareness. 30 is subtler; 90 is jarring. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion")
	float SnapTurnDegrees = 45.f;

	// Stick deflection required to trigger a snap turn. Below this, the
	// stick is considered released and the turn re-arms. Prevents rapid-
	// fire turns from a resting thumb. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Locomotion")
	float SnapTurnThreshold = 0.7f;

	// --- Enhanced Input assets (assigned in Blueprint child) -------------
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Input")
	TObjectPtr<UInputAction> MoveAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Input")
	TObjectPtr<UInputAction> SnapTurnAction;

	// --- Components ------------------------------------------------------
	// Root of the VR play space. Camera + controllers are children so
	// the whole rig moves as a unit if the pawn is teleported / possessed
	// on a new scenario. - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<USceneComponent> VROrigin;

	// HMD position. Tracks the physical headset via IXRTrackingSystem.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UMotionControllerComponent> LeftController;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UMotionControllerComponent> RightController;

	// Optional controller meshes so the operator sees plastic in-hand,
	// not a floating point in space. Assigned in Blueprint child class or
	// left null for placeholder debug spheres.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UStaticMeshComponent> LeftControllerMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UStaticMeshComponent> RightControllerMesh;

	// UI raycasters. Point at a WidgetComponent (the diegetic scope) and
	// call PressPointerKey on trigger press to fake a mouse click on the
	// UMG widget. Same widget code as the desktop scope, no rewrite. - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UWidgetInteractionComponent> LeftPointer;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<UWidgetInteractionComponent> RightPointer;

	// Hand mesh slots. Assign the Meta XR Interaction SDK skeletal meshes
	// (SK_OpenXRHand_Left / _Right) + the matching ABP_ControllerDrivenHand
	// animation blueprints in the BP subclass. Purely cosmetic: the
	// operator-console interaction runs off the fingertip spheres below,
	// not off the mesh, so a null hand still leaves interaction working
	// (with an invisible finger). - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<USkeletalMeshComponent> LeftHand;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR")
	TObjectPtr<USkeletalMeshComponent> RightHand;

	// Fingertip collision volumes. Sized to feel like an index-fingertip
	// touching a button on the console. Placed as children of the motion
	// controllers rather than sockets on the hand mesh so interaction
	// works regardless of which hand mesh (or none) is assigned. For
	// finger-perfect precision later, reparent these to a hand socket
	// in the BP subclass. - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Interaction")
	TObjectPtr<USphereComponent> LeftFingertip;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Interaction")
	TObjectPtr<USphereComponent> RightFingertip;

	// Forward offset of the fingertip from the motion-controller origin.
	// Default (5, 0, -3) puts it just beyond the front of a Meta Quest
	// Touch controller in the natural "index finger extended" pose. Tune
	// per Details panel if the hand mesh's index-tip position differs. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Interaction")
	FVector FingertipOffset = FVector(5.f, 0.f, -3.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Interaction")
	float FingertipRadius = 1.5f;

	// Trigger input for firing the currently-hovered console button.
	// Distinct from any grip / thumbstick input; these two actions are
	// dedicated to console-button dispatch. Wire in the BP subclass to
	// the motion-controller Trigger axes. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Input")
	TObjectPtr<UInputAction> TriggerLeftAction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Input")
	TObjectPtr<UInputAction> TriggerRightAction;

	// Left grip axis, used for the pause menu long-press. Assign a new
	// Input Action asset (Value Type = Axis1D) in the BP subclass and map
	// OculusTouch_Left_Grip_Axis -> that action in IMC_VROperator. Read
	// via the same EI GetActionValue<float> path the triggers use, which
	// is the only input read pattern proven to work in this project's
	// OpenXR / EI setup. When the value stays >= 0.9 for >= 1 second, the
	// pause menu fires exactly once. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Input")
	TObjectPtr<UInputAction> GripLeftAction;

	// Kept for legacy compatibility - previous button-based pause approach.
	// Not read anywhere any more; the grip long-press is the current input.
	// Left as an assignable UPROPERTY in case Neo wants to re-attach it to
	// a working input path later. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Input")
	TObjectPtr<UInputAction> PauseMenuAction;

	// Widget class the pause menu spawns. Set to WBP_VRPauseMenu in the BP
	// subclass. When null, TogglePauseMenu logs a warning and no-ops. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Pause Menu")
	TSubclassOf<UUserWidget> PauseMenuWidgetClass;

	// Widget class for the in-VR options screen shown when the OPTIONS
	// row is confirmed. Set to WBP_VROptions on the BP subclass. Same
	// stereo-layer pipeline as the main pause menu - we just swap the
	// UWidgetComponent's widget class in place. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Pause Menu")
	TSubclassOf<UUserWidget> VROptionsWidgetClass;

	// Distance in front of the HMD where the pause menu WidgetComponent
	// spawns. Default 150 = comfortable arm's length. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Pause Menu")
	float PauseMenuDistanceCm = 150.f;

	// Physical size of the pause menu widget in world units. Widget's own
	// draw resolution stays whatever WBP_VRPauseMenu is authored at; this
	// controls how big the panel is in the world. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Pause Menu")
	FVector2D PauseMenuWorldSizeCm = FVector2D(80.f, 100.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Pause Menu")
	FVector2D PauseMenuDrawSizePx = FVector2D(600.f, 750.f);

	// True while the pause menu is showing. Used by the tick logic to
	// redraw the UMG into the stereo layer's render target, and by external
	// systems to gate other input. - TripleA
	UFUNCTION(BlueprintPure, Category = "VR|Pause Menu")
	bool IsPauseMenuShowing() const { return PauseMenuInstance != nullptr; }

	// Show / hide the VR pause menu. Called from the PauseMenuAction input
	// binding. Also pauses / resumes the SimulationController's session
	// while the menu is showing so the world doesn't tick underneath the
	// operator. - TripleA
	UFUNCTION(BlueprintCallable, Category = "VR|Pause Menu")
	void TogglePauseMenu();

	// Force hide - used by the pause menu widget's own buttons (RESUME) so
	// they can dismiss without needing to re-toggle. - TripleA
	UFUNCTION(BlueprintCallable, Category = "VR|Pause Menu")
	void HidePauseMenu();

	// Currently-highlighted option index. Right-stick Y ticks it while the
	// menu is up (up = previous, down = next), wrapping at PauseMenuOptionCount.
	// The widget queries this via GetPauseMenuSelectedIndex() to paint its
	// highlight - the stereo layer path can't take Slate focus so the pawn
	// owns the selection state and the widget just reads it. - TripleA
	UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Pause Menu")
	int32 PauseMenuSelectedIndex = 0;

	// Number of options the widget presents. Default 4 matches the shipping
	// WBP_VRPauseMenu row set: RESUME / OPTIONS / END SESSION / EXIT TO MENU.
	// BP subclass Class Defaults can override, but this default is authoritative
	// enough that the highlight cycle works out of the box even if the BP
	// override slot is empty. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VR|Pause Menu")
	int32 PauseMenuOptionCount = 4;

	UFUNCTION(BlueprintPure, Category = "VR|Pause Menu")
	int32 GetPauseMenuSelectedIndex() const { return PauseMenuSelectedIndex; }

	UFUNCTION(BlueprintCallable, Category = "VR|Pause Menu")
	void SetPauseMenuOptionCount(int32 InCount) { PauseMenuOptionCount = FMath::Max(1, InCount); }

	// Fires on right-trigger press while the menu is showing. Default C++
	// implementation dispatches by index: 0 = HidePauseMenu, anything else
	// = ExitToMainMenu (open LVL_MainMenu with HMD off). Neo can override
	// in BP_VROperatorPawn to add options / change routing without touching
	// C++. - TripleA
	UFUNCTION(BlueprintNativeEvent, Category = "VR|Pause Menu")
	void OnPauseMenuConfirm(int32 SelectedIndex);
	virtual void OnPauseMenuConfirm_Implementation(int32 SelectedIndex);

	// Which screen the pause pipeline is currently displaying. Drives the
	// button-name array used by the highlight loop and the confirm
	// dispatch. Both screens share the same UWidgetComponent + stereo
	// layer; only the widget class swaps. - TripleA
	enum class EPauseScreen : uint8 { PauseMenu, Options };
	EPauseScreen CurrentScreen = EPauseScreen::PauseMenu;

	// Discrete-step state for cyclable options. Snap turn steps through
	// 30 / 45 / 90 deg, radio volume steps Low / Med / High. Widget
	// binds each row's text/label to the getters below via a Switch on
	// Int to display the current value. - TripleA
	UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Pause Menu")
	int32 SnapTurnStepIndex = 1;   // default 45 deg

	UPROPERTY(BlueprintReadOnly, Transient, Category = "VR|Pause Menu")
	int32 RadioVolumeStepIndex = 2; // default High

	UFUNCTION(BlueprintPure, Category = "VR|Pause Menu")
	int32 GetSnapTurnStepIndex() const { return SnapTurnStepIndex; }

	UFUNCTION(BlueprintPure, Category = "VR|Pause Menu")
	int32 GetRadioVolumeStepIndex() const { return RadioVolumeStepIndex; }

private:
	void ShowPauseMenuScreen();
	void ShowOptionsScreen();
	void ApplySnapTurnStep();
	void ApplyRadioVolumeStep();

public:

	// Optional visual laser mesh slots. Neo assigns a thin cyan cylinder
	// mesh in the BP subclass. Auto-scale-Z per tick to end at the widget
	// hit point while the pause menu is showing; hidden otherwise. - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Pause Menu")
	TObjectPtr<UStaticMeshComponent> LeftLaser;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Pause Menu")
	TObjectPtr<UStaticMeshComponent> RightLaser;

	// Runtime state: the spawned actor hosting the pause menu WidgetComponent.
	// nullptr when the menu is hidden. - TripleA
	UPROPERTY()
	TObjectPtr<AActor> PauseMenuActor;

	// Pre-created WidgetComponent for the pause menu (legacy - world-space
	// widget path). Kept as a UPROPERTY for the moment because there are
	// runtime destroy/recreate references to it elsewhere, but not used by
	// the shipped pause menu code any more (HUD overlay replaces it). - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Pause Menu")
	TObjectPtr<class UWidgetComponent> PauseMenuWidgetComp;

	// Live pause menu widget instance when the menu is showing. nullptr
	// when hidden. The pawn drives it via a UWidgetRenderer -> render
	// target -> UStereoLayerComponent pipeline: the widget draws into a
	// texture, and the HMD compositor puts that texture in front of the
	// player as a face-locked quad at real depth. Bypasses the scene
	// shading pipeline entirely, so Substrate / lighting / Lumen have no
	// say in how the menu appears. - TripleA
	UPROPERTY()
	TObjectPtr<class UUserWidget> PauseMenuInstance;

	// Face-locked quad in the HMD compositor. Assigned in the constructor,
	// hidden by default, shown while PauseMenuInstance != nullptr. QuadSize
	// is in cm at 1m distance (compositor-side units). - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VR|Pause Menu")
	TObjectPtr<UStereoLayerComponent> PauseMenuStereoLayer;

	// Backing render target that the widget draws into. Created lazily on
	// first Toggle so DPI/size can be resolved from the widget class. - TripleA
	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> PauseMenuRT;

	// Renderer that draws PauseMenuInstance into PauseMenuRT once per tick
	// while the menu is up. FWidgetRenderer is a plain C++ class (not a
	// UObject), so it can't be a UPROPERTY - held as a raw pointer,
	// created lazily on first show, deleted in EndPlay via the Slate
	// deferred-cleanup queue. - TripleA
	FWidgetRenderer* PauseMenuRenderer = nullptr;

protected:
	// EnhancedInput handlers
	void HandleMove(const FInputActionValue& Value);
	void HandleSnapTurn(const FInputActionValue& Value);
	void HandleTriggerLeftPressed(const FInputActionValue& Value);
	void HandleTriggerLeftReleased(const FInputActionValue& Value);
	void HandleTriggerRightPressed(const FInputActionValue& Value);
	void HandleTriggerRightReleased(const FInputActionValue& Value);
	void HandlePauseMenuPressed(const FInputActionValue& Value);

	UFUNCTION()
	void OnLeftFingertipBeginOverlap(UPrimitiveComponent* Self, AActor* OtherActor,
		UPrimitiveComponent* Other, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void OnLeftFingertipEndOverlap(UPrimitiveComponent* Self, AActor* OtherActor,
		UPrimitiveComponent* Other, int32 BodyIndex);

	UFUNCTION()
	void OnRightFingertipBeginOverlap(UPrimitiveComponent* Self, AActor* OtherActor,
		UPrimitiveComponent* Other, int32 BodyIndex, bool bFromSweep, const FHitResult& Sweep);

	UFUNCTION()
	void OnRightFingertipEndOverlap(UPrimitiveComponent* Self, AActor* OtherActor,
		UPrimitiveComponent* Other, int32 BodyIndex);

	// Read raw OculusTouch controller state via the local player controller
	// and pump it into the QuestControllerAnimInstance on each hand mesh.
	// Called from Tick; the Meta XR Interaction SDK hand ABPs read those
	// setter properties every frame to blend finger poses (grip flex, thumb
	// touch, trigger curl, etc). Without this the hands stay frozen in the
	// default pointing pose. - TripleA
	void UpdateHandAnimInputs();

private:
	// True when the snap-turn stick has returned to centre and the next
	// deflection is allowed to fire. Prevents holding the stick to spin. - TripleA
	bool bSnapTurnReady = true;

	// Currently-touched console button per controller. Updated by the
	// begin/end overlap callbacks. On trigger press, whichever button is
	// hovered by that hand gets HandlePress; on release, same button gets
	// HandleRelease (regardless of whether the finger has since moved away
	// - press-and-hold semantics need the release edge even if the operator
	// slid their finger off). - TripleA
	UPROPERTY(Transient) TObjectPtr<AClearanceOperatorButton> LeftHoveredButton;
	UPROPERTY(Transient) TObjectPtr<AClearanceOperatorButton> RightHoveredButton;
	UPROPERTY(Transient) TObjectPtr<AClearanceOperatorButton> LeftPressedButton;
	UPROPERTY(Transient) TObjectPtr<AClearanceOperatorButton> RightPressedButton;
};
