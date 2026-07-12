#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "InputActionValue.h"
#include "ClearanceVROperatorPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UWidgetInteractionComponent;
class UStaticMeshComponent;
class UInputAction;
class UInputMappingContext;

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

protected:
	// EnhancedInput handlers
	void HandleMove(const FInputActionValue& Value);
	void HandleSnapTurn(const FInputActionValue& Value);

private:
	// True when the snap-turn stick has returned to centre and the next
	// deflection is allowed to fire. Prevents holding the stick to spin. - TripleA
	bool bSnapTurnReady = true;
};
