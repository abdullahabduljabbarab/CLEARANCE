#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClearanceOperatorButton.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UHapticFeedbackEffect_Base;
class USoundBase;

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
	UnlockCrashCover,       // Break-glass unlock button paired with FireCrashCallout
	FreqTower,
	FreqApproach,
	FreqEmergency,
	FreqGuard,
};

// Which motion controller hand initiated the interaction. Threaded through
// the hover / press / release dispatch so the button can play haptics on
// the correct controller. EControllerHand from InputCoreTypes would work
// too but is oversized for our purposes (Pad, Gun, Special_1, etc); a
// simple Left/Right enum keeps the BP details panel readable. - TripleA
UENUM(BlueprintType)
enum class EOperatorButtonHand : uint8
{
	Left  UMETA(DisplayName = "Left"),
	Right UMETA(DisplayName = "Right"),
};

// Coarse visual state the button broadcasts to Blueprint so the BP subclass
// can drive material emissive, mesh press-down translate, LED brightness,
// etc. Kept intentionally small - four states cover 100% of button feel
// scenarios and keep the BP switch trivial. Momentary buttons cycle
// Idle -> Hovered -> Pressed -> Hovered / Idle; latched buttons (PTT)
// cycle Idle -> Hovered -> Active -> Hovered / Idle on release. - TripleA
UENUM(BlueprintType)
enum class EOperatorButtonVisualState : uint8
{
	Idle    UMETA(DisplayName = "Idle"),
	Hovered UMETA(DisplayName = "Hovered"),
	Pressed UMETA(DisplayName = "Pressed"),
	Active  UMETA(DisplayName = "Active (Latched)"),
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

	virtual void BeginPlay() override;

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

	// Cap mesh authored in C++. Substrate + PIE has a duplication bug where
	// any material assigned to a component during the actor's construction
	// (SCS or CreateDefaultSubobject) renders as solid black at runtime.
	// Materials assigned purely at runtime via CreateDynamicMaterialInstance
	// glow correctly. Fix: Cap is a CreateDefaultSubobject (so it shows in
	// the BP editor for per-instance placement) but its mesh AND material
	// are assigned at BeginPlay via runtime CreateDynamicMaterialInstance,
	// never at construction. The default assets are loaded via the
	// UPROPERTYs below, tunable per-instance if a button wants a custom
	// mesh / base material. - TripleA
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Operator|Button")
	TObjectPtr<UStaticMeshComponent> Cap;

	// Default assets applied to Cap at runtime (BeginPlay), never at
	// construction. Set via ConstructorHelpers in the ctor so the defaults
	// resolve at build time; per-instance overrides in Details work
	// (EditAnywhere) if a specific button wants a custom cap. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operator|Button")
	TObjectPtr<UStaticMesh> CapMeshAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operator|Button")
	TObjectPtr<UMaterialInterface> CapBaseMaterial;

	// Labels are Neo's responsibility per-instance via DecalActors placed on
	// the tower panel next to each button (dymo tape / gaffer tape look).
	// Not part of this class - keeps the button actor lightweight and lets
	// Neo tweak label appearance per placement without touching C++. - TripleA

	// --- Feel: haptics --------------------------------------------------------
	// Haptic effect played on the pressing controller when the trigger fires
	// down. Short sharp pulse is the right idea (25-50 ms, ~200 Hz feels
	// mechanical). Leave null for a silent-haptic button. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Haptics")
	TObjectPtr<UHapticFeedbackEffect_Base> PressHaptic;

	// Played on trigger release. Usually a softer / lower pulse than press
	// so the ear-brain-hand loop learns the two edges apart. Momentary
	// buttons that don't care about release can leave this null. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Haptics")
	TObjectPtr<UHapticFeedbackEffect_Base> ReleaseHaptic;

	// Played on the hovering controller the moment the fingertip enters the
	// button hitbox. Very subtle rumble (soft, ~50 ms) so the operator
	// feels the button before pressing it - meaningful for VR because the
	// visual is often occluded by the finger itself. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Haptics")
	TObjectPtr<UHapticFeedbackEffect_Base> HoverHaptic;

	// --- Feel: audio ----------------------------------------------------------
	// Diegetic 3D-positional click played at the button's world position on
	// press. Something crunchy and mechanical works far better than a UI
	// beep - the ear wants to hear plastic on plastic, not a synthesised
	// blip. Leave null for silent. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Audio")
	TObjectPtr<USoundBase> PressSound;

	// Softer click on release. Same 3D position. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Audio")
	TObjectPtr<USoundBase> ReleaseSound;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Audio",
		meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SoundVolumeMultiplier = 1.f;

	// --- Interaction dispatch -------------------------------------------------
	// Called by the VR pawn when a fingertip sphere begins / ends overlap with
	// this button. Drives Hovered state and fires HoverHaptic. Hand is tracked
	// so a two-hand hover reads as Hovered until both hands leave. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Operator|Button")
	void HandleHoverBegin(APawn* HoveringPawn, EOperatorButtonHand Hand);

	UFUNCTION(BlueprintCallable, Category = "Operator|Button")
	void HandleHoverEnd(APawn* HoveringPawn, EOperatorButtonHand Hand);

	// Called by the VR pawn when a trigger press / release occurs while the
	// passed pawn's fingertip is currently overlapping this button. Hand
	// param routes press/release haptics to the correct controller. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Operator|Button")
	void HandlePress(APawn* PressingPawn, EOperatorButtonHand Hand);

	UFUNCTION(BlueprintCallable, Category = "Operator|Button")
	void HandleRelease(APawn* PressingPawn, EOperatorButtonHand Hand);

	// --- Cap material (DMI-driven emissive glow) ------------------------------
	// The Cap StaticMeshComponent is a C++ default subobject (see the Cap
	// UPROPERTY above under Operator|Button). BeginPlay creates the DMI on
	// it and applies the Idle state; SetVisualState re-applies on every
	// transition. The BP subclass sees an inherited Cap and can rewire its
	// press-down animation to it via OnVisualStateChanged. - TripleA

	// Per-state EmissiveColor + EmissiveMultiplier written to the Cap DMI on
	// every SetVisualState. Defaults tuned for Lumen-off, auto-exposure-on
	// scenes where a small emissive area needs to punch through exposure to
	// visibly light up. Tweak per-instance for coloured buttons (red
	// emergency, green normal, amber caution). - TripleA
	// EmissiveColor uses SATURATED colours (0.05 blue, 0.7 blue, etc) rather
	// than pure white because Cesium Sun Sky's auto-exposure adapts to bright
	// outdoor daylight and crushes low-brightness HDR white to invisible;
	// same numeric brightness with a saturated hue reads much stronger to
	// the eye because it doesn't compete with the sky's white balance.
	// Multipliers are cranked high (Idle 50, Hovered 100, Pressed 200) so
	// the cap punches through exposure even when the operator is looking at
	// bright sky. Tweak per-instance in Details for coloured buttons. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Visual")
	FLinearColor IdleEmissiveColor = FLinearColor(0.05f, 0.15f, 0.35f, 1.f);   // dim blue

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Visual", meta = (ClampMin = "0.0"))
	float IdleEmissiveMultiplier = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Visual")
	FLinearColor HoveredEmissiveColor = FLinearColor(0.2f, 0.6f, 1.f, 1.f);    // cyan glow

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Visual", meta = (ClampMin = "0.0"))
	float HoveredEmissiveMultiplier = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Visual")
	FLinearColor PressedEmissiveColor = FLinearColor(0.5f, 0.9f, 1.f, 1.f);    // bright cyan

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Visual", meta = (ClampMin = "0.0"))
	float PressedEmissiveMultiplier = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Visual")
	FLinearColor ActiveEmissiveColor = FLinearColor(1.f, 0.05f, 0.05f, 1.f);   // deep red

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Visual", meta = (ClampMin = "0.0"))
	float ActiveEmissiveMultiplier = 200.f;

	// Cap press-down offset applied on Pressed / Active states. Sub-3mm feels
	// right for a plastic pushbutton - deeper reads as a mechanical toggle
	// switch and less doesn't register as a press at all. Applied along Cap's
	// LOCAL Z axis so re-orienting the Cap in the level (side-mounted buttons
	// on a vertical panel etc.) presses in the correct direction. C++ applies
	// this ON TOP of the authored Cap relative location so Neo can position
	// each button freely and the press animation always moves down FROM
	// wherever the cap sits at rest. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Feel|Visual")
	float CapPressDownOffset = -0.3f;

	// Populated in BeginPlay. The MID clones from Cap's assigned material at
	// element 0 (defaults to M_OperatorButton, loaded via ConstructorHelpers
	// in the ctor). - TripleA
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Feel|Visual")
	TObjectPtr<UMaterialInstanceDynamic> CapMID;

	// --- Visual state ---------------------------------------------------------
	// Fired every time the visual state changes AFTER the Cap DMI colours have
	// been applied by C++. Bind in the BP subclass for anything the C++ pass
	// doesn't handle: press-down cap-mesh translate (Z -= few mm on Pressed /
	// Active then back to 0), Niagara effect spawn on Pressed, custom sound
	// pitch per-state, LED emissive on a SEPARATE indicator mesh, etc. - TripleA
	UFUNCTION(BlueprintImplementableEvent, Category = "Feel|Visual")
	void OnVisualStateChanged(EOperatorButtonVisualState NewState,
		EOperatorButtonVisualState OldState);

	UFUNCTION(BlueprintPure, Category = "Feel|Visual")
	EOperatorButtonVisualState GetVisualState() const { return VisualState; }

	// Force the visual state from external code. Broadcasts OnVisualStateChanged
	// as if driven internally. Useful for latched-state driving decoupled from
	// a physical press: e.g. a mic-status LED on the PTT button when the mic
	// opens for reasons other than a trigger (radio check, incoming call). - TripleA
	UFUNCTION(BlueprintCallable, Category = "Feel|Visual")
	void SetVisualState(EOperatorButtonVisualState NewState);

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

	// --- Break-glass cover (for crash-alarm and other guarded buttons) --------
	// Two-button interaction: an outside "unlock" button (Kind = UnlockCrashCover)
	// with its TargetButton pointing at THIS button (the alarm, Kind =
	// FireCrashCallout) opens the glass cover for CoverUnlockDurationSec. The
	// alarm button silently rejects presses while the cover is locked, so the
	// operator has to physically unlock first, then press within the window.
	// Auto-relock timer fires OnCoverLockChanged(false) so the BP visual
	// (glass mesh material / animation) tracks state without any BP tick. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Operator|Button|Cover",
		meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float CoverUnlockDurationSec = 4.f;

	// Set on an UnlockCrashCover button - points at the FireCrashCallout button
	// to unlock. Soft ref so the level can wire it cleanly without a cyclic
	// hard dependency. - TripleA
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Operator|Button|Cover")
	TSoftObjectPtr<AClearanceOperatorButton> TargetButton;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Operator|Button|Cover")
	bool bCoverUnlocked = false;

	// Fires whenever the cover state flips (unlock button pressed, auto-relock
	// timer, or manual LockCover call). BP subclass binds this to swap the
	// glass mesh material / play open-slide animation / play glass-latch SFX. - TripleA
	UFUNCTION(BlueprintImplementableEvent, Category = "Operator|Button|Cover")
	void OnCoverLockChanged(bool bNowUnlocked);

	// Unlocks this button's cover for CoverUnlockDurationSec seconds. Called
	// from an UnlockCrashCover button's HandlePress dispatch, or from BP if
	// custom unlock flow is wired. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Operator|Button|Cover")
	void UnlockCover();

	UFUNCTION(BlueprintCallable, Category = "Operator|Button|Cover")
	void LockCover();

private:
	// Cover auto-relock timer handle. Set by UnlockCover, fires LockCover after
	// CoverUnlockDurationSec so the operator has a bounded window. - TripleA
	FTimerHandle CoverRelockTimer;

	// Current visual state. Kept private so external callers go through
	// SetVisualState (which handles the change-broadcast + no-op guards). - TripleA
	UPROPERTY(Transient) EOperatorButtonVisualState VisualState = EOperatorButtonVisualState::Idle;

	// Per-hand hover tracking. Both hands can hover the same button (a
	// two-handed operator resting fingers on adjacent buttons); state
	// stays Hovered until BOTH hands leave. Held as private bit-fields
	// so no header bloat. - TripleA
	uint8 bLeftHovering  : 1;
	uint8 bRightHovering : 1;

	// Cap's resting relative location captured at BeginPlay. Press states
	// offset from this; release snaps back. Captured from the CDO Cap's
	// authored transform before we recreate Cap at runtime, so per-instance
	// placement is preserved. - TripleA
	FVector CapAuthoredRelativeLocation = FVector::ZeroVector;

	void PlayHapticOn(APawn* Pawn, EOperatorButtonHand Hand, UHapticFeedbackEffect_Base* Effect) const;
	void PlaySoundAtButton(USoundBase* Sound) const;

	// Push the per-state EmissiveColor + EmissiveMultiplier onto the Cap DMI.
	// Called from BeginPlay (initial seed) and from SetVisualState on every
	// transition. No-op when CapMID is null. - TripleA
	void ApplyVisualToCapMID(EOperatorButtonVisualState State);
};
