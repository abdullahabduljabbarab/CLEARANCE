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
	// works regardless of which hand mesh (or none) is assigned. If Neo
	// wants finger-perfect precision later, reparent these to a hand
	// socket in the BP subclass. - TripleA
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

protected:
	// EnhancedInput handlers
	void HandleMove(const FInputActionValue& Value);
	void HandleSnapTurn(const FInputActionValue& Value);
	void HandleTriggerLeftPressed(const FInputActionValue& Value);
	void HandleTriggerLeftReleased(const FInputActionValue& Value);
	void HandleTriggerRightPressed(const FInputActionValue& Value);
	void HandleTriggerRightReleased(const FInputActionValue& Value);

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
