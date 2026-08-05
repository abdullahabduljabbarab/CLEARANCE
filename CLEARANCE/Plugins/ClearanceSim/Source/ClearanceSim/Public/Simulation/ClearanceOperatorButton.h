#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClearanceOperatorButton.generated.h"

class UBoxComponent;

// Which operator-console action this physical button triggers when a
// motion-controller fingertip fires the trigger while overlapping its
// collision volume. Kinds correspond one-to-one with helpers on
// AClearanceOperatorPC so wiring is trivial: place the actor over the
// tower-mesh button, set Kind, done. Reserved slots at the end are
// stubbed for the wider mimic-panel work (emergency ack, priority land,
// runway request, crash callout, freq presets). - TripleA
UENUM(BlueprintType)
enum class EOperatorButtonKind : uint8
{
	None,
	PushToTalk,
	SelectNextAircraft,
	SelectPreviousAircraft,
	ClearAircraftSelection,
	// Reserved for later mimic-panel work:
	AcknowledgeEmergency,
	TogglePrioritySelected,
	RequestRunwayNorth,
	RequestRunwaySouth,
	FireCrashCallout,
	FreqTower,
	FreqApproach,
	FreqEmergency,
	FreqGuard,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOperatorButtonEvent,
	class AClearanceOperatorButton*, Button);

// Physical console button on the tower mesh. Placed one instance per
// pressable shape in the mesh (Neo positions + sizes the collision box
// over each button). Fired by AClearanceVROperatorPawn when a motion
// controller's fingertip sphere overlaps and the trigger presses.
// Dispatches to the operator PC based on Kind; broadcasts BP delegates
// for SFX / press animation / LED emissive changes. - TripleA
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceOperatorButton : public AActor
{
	GENERATED_BODY()

public:
	AClearanceOperatorButton();

	// Which action this button fires. Set per instance in the level details
	// panel. Kind = None makes the button a no-op (useful as a decorative
	// placeholder while wiring up).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operator|Button")
	EOperatorButtonKind Kind = EOperatorButtonKind::None;

	// True = press event fires once on trigger down and release is ignored.
	// False = press event on trigger down, release event on trigger up.
	// PTT sets this false so the mic stays open only while held; select
	// buttons stay true for click-and-forget semantics. Auto-defaulted in
	// the ctor based on Kind but overridable per instance if a specific
	// button wants different behaviour. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operator|Button")
	bool bIsMomentary = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Operator|Button")
	TObjectPtr<USceneComponent> Root;

	// Fingertip overlap volume. Neo sizes this to match the physical button
	// on the tower mesh in the level editor. Default is a small cube that
	// the operator can enlarge or shrink in Details. - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Operator|Button")
	TObjectPtr<UBoxComponent> InteractionVolume;

	// Called by the VR pawn when a trigger press or release occurs while
	// the passed pawn's fingertip is currently overlapping this button.
	UFUNCTION(BlueprintCallable, Category = "Operator|Button")
	void HandlePress(APawn* PressingPawn);

	UFUNCTION(BlueprintCallable, Category = "Operator|Button")
	void HandleRelease(APawn* PressingPawn);

	// BP hooks for cosmetic feedback: press animation, click SFX, LED
	// emissive toggle. Fired regardless of whether Kind actually
	// dispatched to a helper (so a Kind = None decorative button still
	// makes a click when pressed). Named with the "Button" prefix so they
	// don't shadow AActor::OnReleased / OnClicked which UE reserves for
	// mouse/touch input on the actor. - TripleA
	UPROPERTY(BlueprintAssignable, Category = "Operator|Button")
	FOperatorButtonEvent OnButtonPressed;

	UPROPERTY(BlueprintAssignable, Category = "Operator|Button")
	FOperatorButtonEvent OnButtonReleased;
};
