#include "Simulation/ClearanceOperatorButton.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Haptics/HapticFeedbackEffect_Base.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

// Green on-screen prints on the whole console-button chain. Flip to 1 to
// re-enable if a diegetic button stops firing and you need to trace the
// press dispatch back through hover / trigger / handler. - TripleA
#ifndef CLEARANCE_LOG_OPERATOR_BUTTONS
#define CLEARANCE_LOG_OPERATOR_BUTTONS 0
#endif

#if CLEARANCE_LOG_OPERATOR_BUTTONS
static void ClearanceButtonDebug(int32 Key, const FColor& C, const FString& Msg)
{
	UE_LOG(LogTemp, Warning, TEXT("[OperatorButton] %s"), *Msg);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, 3.f, C, Msg);
	}
}
#else
static void ClearanceButtonDebug(int32, const FColor&, const FString&) {}
#endif

AClearanceOperatorButton::AClearanceOperatorButton()
	: bLeftHovering(0)
	, bRightHovering(0)
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	InteractionVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionVolume"));
	InteractionVolume->SetupAttachment(Root);
	// Small default hit box; Neo scales per-instance to match the physical
	// button geometry. Overlap-only (no block) so the fingertip sphere
	// can pass through without physically stopping the controller. - TripleA
	InteractionVolume->SetBoxExtent(FVector(3.f, 3.f, 3.f));
	InteractionVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionVolume->SetCollisionObjectType(ECC_WorldStatic);
	InteractionVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionVolume->SetGenerateOverlapEvents(true);

	// Cap mesh - Substrate + PIE has a duplication bug where materials AND
	// meshes assigned to a component in the constructor fail to render at
	// runtime. CreateDefaultSubobject the empty component here so it exists
	// in the BP editor for per-instance placement, but do NOT set mesh or
	// material yet - both are applied in BeginPlay via the runtime path
	// (proven working by the diagnostic cube). - TripleA
	Cap = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Cap"));
	Cap->SetupAttachment(Root);
	Cap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Cap->SetMobility(EComponentMobility::Movable);
	// Cylinder default is 100cm dia x 200cm tall - scale to a 15mm dia x
	// 3mm tall disc (real ATC button cap). Rotate 90deg to lie flat if
	// needed at per-instance placement. - TripleA
	Cap->SetRelativeScale3D(FVector(0.015f, 0.015f, 0.0015f));

	// Assign the mesh at construction (so Cap is visible in the editor
	// viewport when the actor is placed) but SKIP the material - material
	// assignment at construction hits the Substrate + PIE duplication bug
	// that renders it black at runtime. Material is applied via runtime
	// CreateDynamicMaterialInstance in BeginPlay. - TripleA
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CapMeshFinder(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CapMeshFinder.Succeeded())
	{
		Cap->SetStaticMesh(CapMeshFinder.Object);
		CapMeshAsset = CapMeshFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BtnMatFinder(
		TEXT("/Game/VR/Materials/M_OperatorButton.M_OperatorButton"));
	if (BtnMatFinder.Succeeded())
	{
		CapBaseMaterial = BtnMatFinder.Object;
	}

}

// --- Cap material (C++-owned DMI) ------------------------------------------

void AClearanceOperatorButton::BeginPlay()
{
	Super::BeginPlay();

	if (!Cap) { return; }

	// Substrate + PIE breaks the CDO Cap's render proxy on actor duplication
	// (empirically: cap goes black on Play). Workaround: capture the CDO
	// Cap's authored transform, spawn a fresh runtime Cap with the working
	// render path, then DESTROY the CDO Cap so it can't render at all in
	// PIE. Editor viewport uses the CDO Cap for placement (which is why
	// we don't touch it at construction), PIE gets a clean runtime replica.
	// Hiding via SetHiddenInGame + SetVisibility isn't reliable in every
	// mesh / Substrate combination, so we just remove the source. - TripleA
	// Capture whatever Neo authored on the CDO Cap (transform + mesh +
	// material). This lets Neo configure Cap via the ordinary editor
	// workflow - set the Cap component's Static Mesh slot in Details,
	// drag it around in the viewport, override the material - and PIE
	// still picks it up. Falls back to the CapMeshAsset / CapBaseMaterial
	// UPROPERTYs if Neo left the component slots at their defaults. - TripleA
	const FTransform SavedTransform = Cap->GetRelativeTransform();
	CapAuthoredRelativeLocation = SavedTransform.GetLocation();
	UStaticMesh* SourceMesh = Cap->GetStaticMesh();
	if (!SourceMesh) { SourceMesh = CapMeshAsset; }
	UMaterialInterface* SourceMaterial = Cap->GetMaterial(0);
	if (!SourceMaterial) { SourceMaterial = CapBaseMaterial; }

	UStaticMeshComponent* OldCap = Cap;

	UStaticMeshComponent* RuntimeCap = NewObject<UStaticMeshComponent>(
		this, UStaticMeshComponent::StaticClass(), NAME_None, RF_Transient);
	if (!RuntimeCap) { return; }

	RuntimeCap->SetupAttachment(Root);
	RuntimeCap->RegisterComponent();
	RuntimeCap->SetRelativeTransform(SavedTransform);
	RuntimeCap->SetMobility(EComponentMobility::Movable);
	RuntimeCap->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	if (SourceMesh)
	{
		RuntimeCap->SetStaticMesh(SourceMesh);
	}
	if (SourceMaterial)
	{
		// Explicit source-material variant of CreateDynamicMaterialInstance -
		// same call the working diagnostic cube used, guaranteed clean render
		// proxy since RuntimeCap has no CDO-serialized material at all. - TripleA
		CapMID = RuntimeCap->CreateDynamicMaterialInstance(0, SourceMaterial);
	}

	// Re-point Cap so BP references (OnVisualStateChanged Set-Relative-
	// Location on Cap) target the visible RuntimeCap, not the destroyed
	// CDO component. - TripleA
	Cap = RuntimeCap;

	// Now destroy the CDO Cap - RuntimeCap is fully set up and Cap points
	// at it, so BP has a valid target for its Set-Relative-Location. - TripleA
	if (OldCap)
	{
		OldCap->DestroyComponent();
	}

	if (CapMID)
	{
		ApplyVisualToCapMID(VisualState);
	}
}

void AClearanceOperatorButton::ApplyVisualToCapMID(EOperatorButtonVisualState State)
{
	FLinearColor Color = IdleEmissiveColor;
	float        Mult  = IdleEmissiveMultiplier;
	bool         bPressed = false;
	switch (State)
	{
	case EOperatorButtonVisualState::Idle:
		Color = IdleEmissiveColor;    Mult = IdleEmissiveMultiplier;    break;
	case EOperatorButtonVisualState::Hovered:
		Color = HoveredEmissiveColor; Mult = HoveredEmissiveMultiplier; break;
	case EOperatorButtonVisualState::Pressed:
		Color = PressedEmissiveColor; Mult = PressedEmissiveMultiplier; bPressed = true; break;
	case EOperatorButtonVisualState::Active:
		Color = ActiveEmissiveColor;  Mult = ActiveEmissiveMultiplier;  bPressed = true; break;
	}

	if (CapMID)
	{
		// Param names must match the material graph exactly - see
		// M_OperatorButton (Multiply(EmissiveColor, EmissiveMultiplier) ->
		// Emissive Color output). - TripleA
		static const FName kEmissiveColor(TEXT("EmissiveColor"));
		static const FName kEmissiveMultiplier(TEXT("EmissiveMultiplier"));
		CapMID->SetVectorParameterValue(kEmissiveColor, Color);
		CapMID->SetScalarParameterValue(kEmissiveMultiplier, Mult);
	}

	// Press-down animation: offset Cap Z by CapPressDownOffset on Pressed /
	// Active states, snap back to the authored resting position on Idle /
	// Hovered. Applied ON TOP of the authored resting location captured at
	// BeginPlay so Neo can position each button freely without breaking
	// the animation. No interp - snap is intentional, feels more mechanical
	// than an eased translate. - TripleA
	if (Cap)
	{
		FVector Target = CapAuthoredRelativeLocation;
		if (bPressed) { Target.Z += CapPressDownOffset; }
		Cap->SetRelativeLocation(Target);
	}
}

// --- Feel helpers ----------------------------------------------------------

void AClearanceOperatorButton::PlayHapticOn(APawn* Pawn, EOperatorButtonHand Hand,
	UHapticFeedbackEffect_Base* Effect) const
{
	if (!Effect || !Pawn) { return; }
	APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
	if (!PC) { return; }
	const EControllerHand ControllerHand = (Hand == EOperatorButtonHand::Left)
		? EControllerHand::Left
		: EControllerHand::Right;
	PC->PlayHapticEffect(Effect, ControllerHand, /*Scale*/ 1.f, /*bLoop*/ false);
}

void AClearanceOperatorButton::PlaySoundAtButton(USoundBase* Sound) const
{
	if (!Sound) { return; }
	// 3D-positional so the click actually appears to come from the button
	// mesh in world space; the operator's ears pinpoint which button
	// clicked without looking. - TripleA
	UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(),
		SoundVolumeMultiplier, /*PitchMultiplier*/ 1.f);
}

// --- Visual state ----------------------------------------------------------

void AClearanceOperatorButton::SetVisualState(EOperatorButtonVisualState NewState)
{
	if (VisualState == NewState) { return; }
	const EOperatorButtonVisualState Old = VisualState;
	VisualState = NewState;
	// Apply C++-owned material state BEFORE the BP event, so any BP logic
	// that reads the current emissive params (e.g. spawning a Niagara at
	// the same colour) sees the up-to-date values. - TripleA
	ApplyVisualToCapMID(NewState);
	OnVisualStateChanged(NewState, Old);
}

// --- Hover -----------------------------------------------------------------

void AClearanceOperatorButton::HandleHoverBegin(APawn* HoveringPawn, EOperatorButtonHand Hand)
{
	if (Hand == EOperatorButtonHand::Left)  { bLeftHovering  = 1; }
	else                                    { bRightHovering = 1; }

	// Don't demote a Pressed or latched-Active state back to Hovered when a
	// second finger drifts onto the same button - the current press takes
	// visual priority. State recomputes on release. - TripleA
	if (VisualState != EOperatorButtonVisualState::Pressed &&
	    VisualState != EOperatorButtonVisualState::Active)
	{
		SetVisualState(EOperatorButtonVisualState::Hovered);
	}

	PlayHapticOn(HoveringPawn, Hand, HoverHaptic);
}

void AClearanceOperatorButton::HandleHoverEnd(APawn* /*HoveringPawn*/, EOperatorButtonHand Hand)
{
	if (Hand == EOperatorButtonHand::Left)  { bLeftHovering  = 0; }
	else                                    { bRightHovering = 0; }

	// Drop to Idle only if no hand is still on us AND we're not mid-press or
	// latched. A press-in-progress that the operator slid off keeps state
	// pinned to Pressed / Active until the release edge fires. - TripleA
	if (!bLeftHovering && !bRightHovering &&
	    VisualState != EOperatorButtonVisualState::Pressed &&
	    VisualState != EOperatorButtonVisualState::Active)
	{
		SetVisualState(EOperatorButtonVisualState::Idle);
	}
}

// --- Press / release -------------------------------------------------------

void AClearanceOperatorButton::HandlePress(APawn* PressingPawn, EOperatorButtonHand Hand)
{
	// Idempotency guard. HandlePress is reached from two paths that both
	// intentionally fire on the same physical trigger event: the Tick-based
	// EI ActionValue poll on the pawn, and the belt-and-suspenders EI
	// BindAction registration in SetupPlayerInputComponent. Without this
	// guard a held trigger would replay the click sound + haptic + state
	// change every frame (60x per second at 60fps). Visual state doubles
	// as the "already pressed" flag - if we're in Pressed or Active, some
	// earlier call already did the work. - TripleA
	if (VisualState == EOperatorButtonVisualState::Pressed ||
	    VisualState == EOperatorButtonVisualState::Active)
	{
		return;
	}

	const uint8 KindByte = static_cast<uint8>(Kind);
	ClearanceButtonDebug(1000 + GetUniqueID(), FColor::Green,
		FString::Printf(TEXT("Button PRESSED: %s (Kind=%d Hand=%s)"),
			*GetName(), KindByte,
			Hand == EOperatorButtonHand::Left ? TEXT("L") : TEXT("R")));

	// Visual + haptic + audio always fire, even for Kind = None decorative
	// buttons - the operator still hears / feels the click. - TripleA
	SetVisualState(bIsMomentary
		? EOperatorButtonVisualState::Pressed
		: EOperatorButtonVisualState::Active);
	PlayHapticOn(PressingPawn, Hand, PressHaptic);
	PlaySoundAtButton(PressSound);

	OnButtonPressed.Broadcast(this);

	if (!PressingPawn)
	{
		ClearanceButtonDebug(1100 + GetUniqueID(), FColor::Red,
			TEXT("  ...but PressingPawn is null"));
		return;
	}
	AClearanceOperatorPC* PC = Cast<AClearanceOperatorPC>(PressingPawn->GetController());
	if (!PC)
	{
		ClearanceButtonDebug(1100 + GetUniqueID(), FColor::Red,
			TEXT("  ...but pawn has no ClearanceOperatorPC controller"));
		return;
	}

	switch (Kind)
	{
	case EOperatorButtonKind::PushToTalk:
		PC->StartPTT();
		break;
	case EOperatorButtonKind::SelectNextAircraft:
		PC->SelectNextAircraft();
		break;
	case EOperatorButtonKind::SelectPreviousAircraft:
		PC->SelectPreviousAircraft();
		break;
	case EOperatorButtonKind::ClearAircraftSelection:
		PC->ClearAircraftSelection();
		break;
	// Later mimic-panel kinds fall through as no-ops for now. Wiring the
	// helpers on the operator PC lights them up without touching this
	// switch (add a case here). - TripleA
	default:
		break;
	}
}

void AClearanceOperatorButton::HandleRelease(APawn* PressingPawn, EOperatorButtonHand Hand)
{
	// Symmetric idempotency guard - only fire on the first release call
	// after a press. Duplicate release calls from EI + poll are silently
	// dropped. - TripleA
	if (VisualState != EOperatorButtonVisualState::Pressed &&
	    VisualState != EOperatorButtonVisualState::Active)
	{
		return;
	}

	// Visual + haptic + audio for the release edge fire for every button
	// regardless of Kind, matching the press-side behaviour. - TripleA
	const bool bStillHovered = bLeftHovering || bRightHovering;
	SetVisualState(bStillHovered
		? EOperatorButtonVisualState::Hovered
		: EOperatorButtonVisualState::Idle);
	PlayHapticOn(PressingPawn, Hand, ReleaseHaptic);
	PlaySoundAtButton(ReleaseSound);

	OnButtonReleased.Broadcast(this);

	// Only press-and-hold kinds care about the release edge on the operator
	// PC side (PTT closes the mic). Momentary buttons already fired their
	// action on press. - TripleA
	if (bIsMomentary) { return; }
	if (!PressingPawn) { return; }

	AClearanceOperatorPC* PC = Cast<AClearanceOperatorPC>(PressingPawn->GetController());
	if (!PC) { return; }

	switch (Kind)
	{
	case EOperatorButtonKind::PushToTalk:
		PC->StopPTT();
		break;
	default:
		break;
	}
}
