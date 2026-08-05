#include "Simulation/ClearanceVROperatorPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "GameFramework/PlayerController.h"
#include "MotionControllerComponent.h"
#include "Simulation/ClearanceOperatorButton.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "UI/ClearanceInstructorPanel.h"
#include "Blueprint/UserWidget.h"

// XR motion source names come from IXRSystemIdentifier - hardcoded per
// OpenXR spec so we don't need the identifier lookup at runtime. - TripleA
namespace ClearanceVR
{
	static const FName kLeftMotionSource(TEXT("Left"));
	static const FName kRightMotionSource(TEXT("Right"));
}

AClearanceVROperatorPawn::AClearanceVROperatorPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// Root is a plain scene component. Not a capsule - the operator doesn't
	// collide with anything, they sit at a desk. A capsule would trigger
	// unwanted overlaps with the tower geometry. - TripleA
	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	SetRootComponent(VROrigin);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VROrigin);
	// HMD tracking drives the camera transform in world space; the pawn only
	// positions the tracking origin. UsePawnControlRotation off so the yaw
	// isn't double-applied on top of the HMD's own rotation. - TripleA
	Camera->bUsePawnControlRotation = false;
	Camera->bLockToHmd = true;

	LeftController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftController"));
	LeftController->SetupAttachment(VROrigin);
	LeftController->SetTrackingMotionSource(ClearanceVR::kLeftMotionSource);

	RightController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightController"));
	RightController->SetupAttachment(VROrigin);
	RightController->SetTrackingMotionSource(ClearanceVR::kRightMotionSource);

	// Placeholder controller meshes. Blueprint child class swaps these for
	// the Quest 3 Touch Plus mesh at design time. - TripleA
	LeftControllerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftControllerMesh"));
	LeftControllerMesh->SetupAttachment(LeftController);
	LeftControllerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	RightControllerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightControllerMesh"));
	RightControllerMesh->SetupAttachment(RightController);
	RightControllerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Widget-interaction lasers. Trace forward from each controller and hit
	// the WidgetComponent on the diegetic scope; a trigger press then routes
	// through the same UMG click path the desktop scope uses. - TripleA
	LeftPointer = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("LeftPointer"));
	LeftPointer->SetupAttachment(LeftController);
	LeftPointer->InteractionDistance = 500.f;   // 5 m of reach, generous for a seated pose
	LeftPointer->bShowDebug = false;

	RightPointer = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("RightPointer"));
	RightPointer->SetupAttachment(RightController);
	RightPointer->InteractionDistance = 500.f;
	RightPointer->bShowDebug = false;

	// Optional hand meshes. Cosmetic only. Meta XR Interaction SDK ships
	// SK_OpenXRHand_Left / _Right + ABP_ControllerDrivenHand_Left / _Right;
	// wire both in the BP subclass Details panel. No collision so the
	// visual hands don't accidentally trip button overlaps (fingertip
	// spheres below do that job). - TripleA
	LeftHand = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("LeftHand"));
	LeftHand->SetupAttachment(LeftController);
	LeftHand->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	RightHand = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RightHand"));
	RightHand->SetupAttachment(RightController);
	RightHand->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Fingertip collision spheres. Parented to the controller (not to the
	// hand mesh) so interaction works whether or not the hand mesh is
	// assigned. Overlap-only so the sphere passes through the button hit
	// volume without physical resistance. - TripleA
	LeftFingertip = CreateDefaultSubobject<USphereComponent>(TEXT("LeftFingertip"));
	LeftFingertip->SetupAttachment(LeftController);
	LeftFingertip->SetSphereRadius(FingertipRadius);
	LeftFingertip->SetRelativeLocation(FingertipOffset);
	LeftFingertip->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	LeftFingertip->SetCollisionObjectType(ECC_Pawn);
	LeftFingertip->SetCollisionResponseToAllChannels(ECR_Ignore);
	LeftFingertip->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	LeftFingertip->SetGenerateOverlapEvents(true);

	RightFingertip = CreateDefaultSubobject<USphereComponent>(TEXT("RightFingertip"));
	RightFingertip->SetupAttachment(RightController);
	RightFingertip->SetSphereRadius(FingertipRadius);
	RightFingertip->SetRelativeLocation(FingertipOffset);
	RightFingertip->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RightFingertip->SetCollisionObjectType(ECC_Pawn);
	RightFingertip->SetCollisionResponseToAllChannels(ECR_Ignore);
	RightFingertip->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Overlap);
	RightFingertip->SetGenerateOverlapEvents(true);

	LeftFingertip->OnComponentBeginOverlap.AddDynamic(this, &AClearanceVROperatorPawn::OnLeftFingertipBeginOverlap);
	LeftFingertip->OnComponentEndOverlap.AddDynamic(this, &AClearanceVROperatorPawn::OnLeftFingertipEndOverlap);
	RightFingertip->OnComponentBeginOverlap.AddDynamic(this, &AClearanceVROperatorPawn::OnRightFingertipBeginOverlap);
	RightFingertip->OnComponentEndOverlap.AddDynamic(this, &AClearanceVROperatorPawn::OnRightFingertipEndOverlap);
}

void AClearanceVROperatorPawn::BeginPlay()
{
	Super::BeginPlay();

	// The operator role is server-authoritative: this pawn is possessed on
	// the server by AClearanceOperatorPC. In a listen-server PIE both the
	// authoritative host pawn AND a client-side proxy pass IsLocallyControlled,
	// so we also gate on Role and NetMode to make sure only the real host
	// touches the Enhanced Input subsystem and input mode. - TripleA
	if (GetLocalRole() != ROLE_Authority ||
		GetNetMode() == NM_DedicatedServer ||
		!IsLocallyControlled())
	{
		return;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) { return; }

	// Register the mapping context in BeginPlay (not SetupPlayerInputComponent)
	// so the OpenXR session is initialised and ready to accept the action set
	// from Enhanced Input. Adding it during SetupPlayerInputComponent meant
	// OpenXR's action attachment happened before the suggested bindings landed
	// and the runtime never wired /input/thumbstick/* to any action. - TripleA
	if (ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->ClearAllMappings();
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, /*Priority*/ 100);
			}
		}
	}

	// The parent OperatorPC forces FInputModeUIOnly at HUD setup which
	// silently drops all gameplay axis input from motion controllers. Flip
	// to GameOnly now that a VR pawn is possessed - no focused widget, no
	// UI capture, motion-controller axes definitively reach the pawn's
	// InputComponent. The diegetic scope in VR uses WidgetInteractionComponent
	// lasers which route through the game input path, not the UI focus stack.
	// Also dismiss the desktop InstructorPanel widget - it was added to the
	// viewport with focus by OperatorPC::BeginPlay and continues to capture
	// keyboard focus even after the InputMode changes. - TripleA
	if (AClearanceOperatorPC* OpPC = Cast<AClearanceOperatorPC>(PC))
	{
		if (UClearanceInstructorPanel* Panel = OpPC->GetInstructorPanel())
		{
			Panel->RemoveFromParent();
		}
	}

	FInputModeGameOnly GameMode;
	PC->SetInputMode(GameMode);
	PC->bShowMouseCursor = false;
}

void AClearanceVROperatorPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Same role gate as BeginPlay. Ghost pawns and remote-authority proxies
	// still receive SetupPlayerInputComponent when replicated possession
	// fires and their EIC bindings would stomp the real player's wiring. - TripleA
	if (GetLocalRole() != ROLE_Authority ||
		GetNetMode() == NM_DedicatedServer ||
		!IsLocallyControlled())
	{
		return;
	}

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (MoveAction)
		{
			EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AClearanceVROperatorPawn::HandleMove);
		}
		if (SnapTurnAction)
		{
			EIC->BindAction(SnapTurnAction, ETriggerEvent::Triggered, this, &AClearanceVROperatorPawn::HandleSnapTurn);
		}

		// Trigger axis with Started (press edge) + Completed (release edge)
		// events. Press dispatches to whichever console button is currently
		// under the fingertip; release dispatches to the button that WAS
		// pressed (which may no longer be under the finger if the operator
		// slid off - press-and-hold semantics rely on the release edge
		// firing regardless). - TripleA
		if (TriggerLeftAction)
		{
			EIC->BindAction(TriggerLeftAction, ETriggerEvent::Started,   this, &AClearanceVROperatorPawn::HandleTriggerLeftPressed);
			EIC->BindAction(TriggerLeftAction, ETriggerEvent::Completed, this, &AClearanceVROperatorPawn::HandleTriggerLeftReleased);
			EIC->BindAction(TriggerLeftAction, ETriggerEvent::Canceled,  this, &AClearanceVROperatorPawn::HandleTriggerLeftReleased);
		}
		if (TriggerRightAction)
		{
			EIC->BindAction(TriggerRightAction, ETriggerEvent::Started,   this, &AClearanceVROperatorPawn::HandleTriggerRightPressed);
			EIC->BindAction(TriggerRightAction, ETriggerEvent::Completed, this, &AClearanceVROperatorPawn::HandleTriggerRightReleased);
			EIC->BindAction(TriggerRightAction, ETriggerEvent::Canceled,  this, &AClearanceVROperatorPawn::HandleTriggerRightReleased);
		}
	}
}

void AClearanceVROperatorPawn::HandleMove(const FInputActionValue& Value)
{
	// Camera-relative locomotion: the direction the HMD is facing is
	// forward, not the pawn's yaw. Project camera forward + right onto the
	// horizontal plane so the operator can't fly by looking down. - TripleA
	const FVector2D RawInput = Value.Get<FVector2D>();

	// Radial deadzone. Stick centre often idles at ~0.02-0.05; without a
	// deadzone HandleMove fires every frame with sub-pixel motion, and the
	// slight noise makes the whole pawn micro-judder. Below 0.15 magnitude
	// = treat as no input. - TripleA
	constexpr float kMoveDeadzone = 0.15f;
	if (RawInput.SizeSquared() < kMoveDeadzone * kMoveDeadzone || !Camera)
	{
		return;
	}

	FVector CamForward = Camera->GetForwardVector();
	CamForward.Z = 0.f;
	CamForward.Normalize();

	FVector CamRight = Camera->GetRightVector();
	CamRight.Z = 0.f;
	CamRight.Normalize();

	const float Dt = GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const FVector Delta = (CamForward * RawInput.Y + CamRight * RawInput.X) * MoveSpeed * Dt;

	// Move the actor, not just VROrigin - HMD tracking origin follows the
	// actor transform, so this is what actually shifts the player's view in
	// world space. Sweep=false: the operator sits at the tower desk, there
	// is no walkable geometry yet, and a sweep per stick event costs a
	// physics query per frame that can reject motion by tiny amounts when
	// the ground plane is close - reads as rubber-banding. Turn sweep back
	// on once tower walls exist and we need real collision. - TripleA
	AddActorWorldOffset(Delta, /*bSweep*/ false);
}

void AClearanceVROperatorPawn::HandleSnapTurn(const FInputActionValue& Value)
{
	const float StickX = Value.Get<FVector2D>().X;

	// Re-arm when the stick returns to centre.
	if (FMath::Abs(StickX) < SnapTurnThreshold)
	{
		bSnapTurnReady = true;
		return;
	}
	if (!bSnapTurnReady || !Camera) { return; }

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) { return; }

	// Rotate the pawn + pivot around the camera so the player turns in place.
	// AddActorWorldRotation alone leaves them slid sideways because bLockToHmd
	// holds the camera at the HMD offset from the pawn origin; nudging the PC's
	// control rotation keeps any BP logic reading ControlRotation in sync, and
	// the position correction below restores the camera to its pre-rotation
	// world spot. - TripleA
	const FVector CameraBefore = Camera->GetComponentLocation();
	const float TurnDegrees = (StickX > 0.f) ? SnapTurnDegrees : -SnapTurnDegrees;

	AddActorWorldRotation(FRotator(0.f, TurnDegrees, 0.f));
	FRotator NewControlRot = PC->GetControlRotation();
	NewControlRot.Yaw += TurnDegrees;
	PC->SetControlRotation(NewControlRot);

	const FVector CameraAfter = Camera->GetComponentLocation();
	FVector Correction = CameraBefore - CameraAfter;
	Correction.Z = 0.f;
	AddActorWorldOffset(Correction);

	bSnapTurnReady = false;
}

// --- Operator console button interaction -----------------------------------
//
// Fingertip sphere overlap events update per-hand HoveredButton. Trigger
// press dispatches to that hovered button. Release fires on the previously
// pressed button regardless of current overlap (press-and-hold PTT needs
// the release edge even if the operator has since slid off). - TripleA

void AClearanceVROperatorPawn::OnLeftFingertipBeginOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (AClearanceOperatorButton* Btn = Cast<AClearanceOperatorButton>(OtherActor))
	{
		LeftHoveredButton = Btn;
	}
}

void AClearanceVROperatorPawn::OnLeftFingertipEndOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32)
{
	if (LeftHoveredButton == OtherActor) { LeftHoveredButton = nullptr; }
}

void AClearanceVROperatorPawn::OnRightFingertipBeginOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (AClearanceOperatorButton* Btn = Cast<AClearanceOperatorButton>(OtherActor))
	{
		RightHoveredButton = Btn;
	}
}

void AClearanceVROperatorPawn::OnRightFingertipEndOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32)
{
	if (RightHoveredButton == OtherActor) { RightHoveredButton = nullptr; }
}

void AClearanceVROperatorPawn::HandleTriggerLeftPressed(const FInputActionValue&)
{
	if (!LeftHoveredButton) { return; }
	LeftPressedButton = LeftHoveredButton;
	LeftPressedButton->HandlePress(this);
}

void AClearanceVROperatorPawn::HandleTriggerLeftReleased(const FInputActionValue&)
{
	if (!LeftPressedButton) { return; }
	LeftPressedButton->HandleRelease(this);
	LeftPressedButton = nullptr;
}

void AClearanceVROperatorPawn::HandleTriggerRightPressed(const FInputActionValue&)
{
	if (!RightHoveredButton) { return; }
	RightPressedButton = RightHoveredButton;
	RightPressedButton->HandlePress(this);
}

void AClearanceVROperatorPawn::HandleTriggerRightReleased(const FInputActionValue&)
{
	if (!RightPressedButton) { return; }
	RightPressedButton->HandleRelease(this);
	RightPressedButton = nullptr;
}
