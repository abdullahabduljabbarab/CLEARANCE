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
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

// Meta XR Interaction SDK is shipped separately by Meta and lives outside
// git.  Auto-detected at build time by ClearanceSim.Build.cs: when the
// plugin is present the hand ABPs get real controller state; when absent
// the pump is a no-op and the hands stay in their default rest pose. - TripleA
#if CLEARANCE_HAS_OCULUS_INTERACTION
#include "Animation/QuestControllerAnimInstance.h"
#endif

// XR motion source names come from IXRSystemIdentifier - hardcoded per
// OpenXR spec so we don't need the identifier lookup at runtime. - TripleA
namespace ClearanceVR
{
	static const FName kLeftMotionSource(TEXT("Left"));
	static const FName kRightMotionSource(TEXT("Right"));
}

AClearanceVROperatorPawn::AClearanceVROperatorPawn()
{
	// Tick enabled so we can pump raw OculusTouch controller state into the
	// Meta XR Interaction SDK QuestControllerAnimInstance every frame. The
	// hand ABPs read those input properties to blend finger poses on grip /
	// trigger / touch; without a per-tick pump the fingers stay frozen. - TripleA
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
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

	// VR Preview launches the game viewport in a separate popped-out window.
	// That window loses BOTH OS-level activation AND Slate keyboard focus
	// to the main editor tab, so every keystroke + Enhanced Input axis
	// event lands on the editor instead of the game. Physically clicking
	// the viewport transfers both - reproduce that programmatically.
	//
	// Retry across several frames because the game viewport widget doesn't
	// exist synchronously at BeginPlay time; the window is spawned by
	// SGameLayerManager on a subsequent Slate tick. First attempt at 100 ms
	// usually lands the widget; the extras cover slow shader-compile
	// launches where the window creation is delayed. Cheap - once focus
	// is captured the retries are functionally no-ops. - TripleA
	if (UWorld* World = GetWorld())
	{
		auto FocusKick = [WeakThis = TWeakObjectPtr<AClearanceVROperatorPawn>(this)]()
		{
			if (!WeakThis.IsValid() || !FSlateApplication::IsInitialized()) { return; }

			FSlateApplication& Slate = FSlateApplication::Get();

			// 1. Slate-side focus - covers keyboard events routed via Slate.
			Slate.SetAllUserFocusToGameViewport(EFocusCause::SetDirectly);

			// 2. OS-level activation - Windows sends WM_ACTIVATE to the window
			//    which is what OpenXR / EI actually watch to unblock the input
			//    session. Without this the Slate focus above is a no-op for
			//    keys that route through the OS input stack. - TripleA
			if (GEngine && GEngine->GameViewport)
			{
				if (TSharedPtr<SWindow> GameWindow = GEngine->GameViewport->GetWindow())
				{
					GameWindow->BringToFront(/*bForce*/ true);
					GameWindow->HACK_ForceToFront();
					if (TSharedPtr<SViewport> ViewportWidget = GEngine->GameViewport->GetGameViewportWidget())
					{
						Slate.SetKeyboardFocus(ViewportWidget, EFocusCause::SetDirectly);
					}
				}
			}
		};

		// Fire the kick at 100/300/700/1500 ms. Any of them can be the one
		// that lands after the popped-out window finishes initialising. - TripleA
		for (float Delay : { 0.10f, 0.30f, 0.70f, 1.50f })
		{
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(FocusKick), Delay, /*bLoop*/ false);
		}
	}
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

		// Trigger dispatch is redundant on top of the Tick-based poll below
		// (Tick reads the EI action value directly and edge-detects) but
		// Started + Completed are kept as a belt-and-suspenders path in case
		// the poll ever misses an edge on a hitchy frame. Triggered / Ongoing
		// are NOT bound here - those fire every frame while the trigger is
		// held and would cause the button handler to be re-invoked ~60 times
		// per second per press, spamming the sound + haptic. The button itself
		// guards against duplicate calls but there is no reason to make the
		// noise. - TripleA
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

// Debug prints for the fingertip + trigger chain. Flip to 1 to re-enable
// yellow / blue on-screen text tracking hover + press events. - TripleA
#ifndef CLEARANCE_LOG_OPERATOR_INTERACTION
#define CLEARANCE_LOG_OPERATOR_INTERACTION 0
#endif

#if CLEARANCE_LOG_OPERATOR_INTERACTION
static void ClearanceInteractionDebug(int32 Key, const FColor& C, const FString& Msg)
{
	UE_LOG(LogTemp, Warning, TEXT("[VRInteraction] %s"), *Msg);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(Key, 3.f, C, Msg);
	}
}
#else
static void ClearanceInteractionDebug(int32, const FColor&, const FString&) {}
#endif

void AClearanceVROperatorPawn::OnLeftFingertipBeginOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (AClearanceOperatorButton* Btn = Cast<AClearanceOperatorButton>(OtherActor))
	{
		LeftHoveredButton = Btn;
		Btn->HandleHoverBegin(this, EOperatorButtonHand::Left);
		ClearanceInteractionDebug(2000, FColor::Yellow,
			FString::Printf(TEXT("L fingertip HOVER: %s"), *Btn->GetName()));
	}
	else if (OtherActor)
	{
		// Overlapping SOMETHING but it isn't a button. Rare, but useful to know
		// if the box is intersecting stray world geometry. - TripleA
		ClearanceInteractionDebug(2001, FColor::Cyan,
			FString::Printf(TEXT("L fingertip touched non-button: %s"), *OtherActor->GetName()));
	}
}

void AClearanceVROperatorPawn::OnLeftFingertipEndOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32)
{
	if (LeftHoveredButton == OtherActor)
	{
		if (AClearanceOperatorButton* Btn = Cast<AClearanceOperatorButton>(OtherActor))
		{
			Btn->HandleHoverEnd(this, EOperatorButtonHand::Left);
		}
		ClearanceInteractionDebug(2000, FColor::Silver,
			FString::Printf(TEXT("L fingertip LEFT: %s"), *OtherActor->GetName()));
		LeftHoveredButton = nullptr;
	}
}

void AClearanceVROperatorPawn::OnRightFingertipBeginOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (AClearanceOperatorButton* Btn = Cast<AClearanceOperatorButton>(OtherActor))
	{
		RightHoveredButton = Btn;
		Btn->HandleHoverBegin(this, EOperatorButtonHand::Right);
		ClearanceInteractionDebug(2100, FColor::Yellow,
			FString::Printf(TEXT("R fingertip HOVER: %s"), *Btn->GetName()));
	}
	else if (OtherActor)
	{
		ClearanceInteractionDebug(2101, FColor::Cyan,
			FString::Printf(TEXT("R fingertip touched non-button: %s"), *OtherActor->GetName()));
	}
}

void AClearanceVROperatorPawn::OnRightFingertipEndOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32)
{
	if (RightHoveredButton == OtherActor)
	{
		if (AClearanceOperatorButton* Btn = Cast<AClearanceOperatorButton>(OtherActor))
		{
			Btn->HandleHoverEnd(this, EOperatorButtonHand::Right);
		}
		ClearanceInteractionDebug(2100, FColor::Silver,
			FString::Printf(TEXT("R fingertip LEFT: %s"), *OtherActor->GetName()));
		RightHoveredButton = nullptr;
	}
}

void AClearanceVROperatorPawn::HandleTriggerLeftPressed(const FInputActionValue&)
{
	ClearanceInteractionDebug(2200, FColor::Blue,
		FString::Printf(TEXT("L trigger PRESSED (hovered: %s)"),
			LeftHoveredButton ? *LeftHoveredButton->GetName() : TEXT("<none>")));
	if (!LeftHoveredButton) { return; }
	LeftPressedButton = LeftHoveredButton;
	LeftPressedButton->HandlePress(this, EOperatorButtonHand::Left);
}

void AClearanceVROperatorPawn::HandleTriggerLeftReleased(const FInputActionValue&)
{
	ClearanceInteractionDebug(2201, FColor::Blue,
		FString::Printf(TEXT("L trigger RELEASED (pressed: %s)"),
			LeftPressedButton ? *LeftPressedButton->GetName() : TEXT("<none>")));
	if (!LeftPressedButton) { return; }
	LeftPressedButton->HandleRelease(this, EOperatorButtonHand::Left);
	LeftPressedButton = nullptr;
}

void AClearanceVROperatorPawn::HandleTriggerRightPressed(const FInputActionValue&)
{
	ClearanceInteractionDebug(2300, FColor::Blue,
		FString::Printf(TEXT("R trigger PRESSED (hovered: %s)"),
			RightHoveredButton ? *RightHoveredButton->GetName() : TEXT("<none>")));
	if (!RightHoveredButton) { return; }
	RightPressedButton = RightHoveredButton;
	RightPressedButton->HandlePress(this, EOperatorButtonHand::Right);
}

void AClearanceVROperatorPawn::HandleTriggerRightReleased(const FInputActionValue&)
{
	ClearanceInteractionDebug(2301, FColor::Blue,
		FString::Printf(TEXT("R trigger RELEASED (pressed: %s)"),
			RightPressedButton ? *RightPressedButton->GetName() : TEXT("<none>")));
	if (!RightPressedButton) { return; }
	RightPressedButton->HandleRelease(this, EOperatorButtonHand::Right);
	RightPressedButton = nullptr;
}

void AClearanceVROperatorPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Same locally-controlled gate as BeginPlay / SetupPlayerInputComponent -
	// the pump is a client-side visual effect; only the operator's own hands
	// need to animate on their own machine. Remote proxies would just miss
	// input reads (there's no controller state on the proxy anyway). - TripleA
	if (GetLocalRole() != ROLE_Authority ||
		GetNetMode() == NM_DedicatedServer ||
		!IsLocallyControlled())
	{
		return;
	}

	UpdateHandAnimInputs();

	// --- Trigger edge detection ---------------------------------------------
	//
	// EI's dispatch to pawn BindAction handlers is unreliable across PIE
	// sessions on Windows: the subsystem confirms the action fires
	// (visible in `showdebug enhancedinput`) but the pawn's InputComponent
	// bindings never invoke. Root cause is a Slate/OpenXR possession-focus
	// race that our C++ can't intercept. Reading the action value directly
	// out of the EI subsystem's accumulator (which IS updated correctly)
	// and edge-detecting here dispatches presses reliably regardless.
	// EI BindAction is left registered as belt-and-suspenders - handlers
	// no-op when there's no hovered button, so a duplicate call is safe. - TripleA
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) { return; }

	// Per-instance edge state kept in a static map keyed by the pawn -
	// declaring plain bool members would need a header change (class layout
	// bump) which blocks Live Coding on the whole thing. Static local +
	// weak ptr key survives a hot patch and safely coexists with future
	// split-screen / pawn swaps: entries for dead pawns quietly return
	// default false. - TripleA
	struct FTriggerEdgeState { bool bLeftDown = false; bool bRightDown = false; };
	static TMap<TWeakObjectPtr<AClearanceVROperatorPawn>, FTriggerEdgeState> EdgeStateByPawn;
	FTriggerEdgeState& Edge = EdgeStateByPawn.FindOrAdd(TWeakObjectPtr<AClearanceVROperatorPawn>(this));

	// Read trigger axis THROUGH the Enhanced Input subsystem, not via
	// PC->GetInputAnalogKeyState. When an IMC binds a key to an action,
	// EI consumes it out of the legacy PlayerInput->KeyStateMap and the
	// legacy poll reports zero even though the axis is definitely moving.
	// The subsystem's own GetActionValue accumulator IS updated per tick
	// and is the correct source for a poll of a mapped analog key. - TripleA
	float LAxisVal = 0.f;
	float RAxisVal = 0.f;
	if (ULocalPlayer* LP = PC->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			if (UEnhancedPlayerInput* EPI = Sub->GetPlayerInput())
			{
				if (TriggerLeftAction)  { LAxisVal = EPI->GetActionValue(TriggerLeftAction ).Get<float>(); }
				if (TriggerRightAction) { RAxisVal = EPI->GetActionValue(TriggerRightAction).Get<float>(); }
			}
		}
	}

	// 0.5 matches the Down trigger config on the IA assets, so press/release
	// timing feels identical whether EI or this poll delivered the event. - TripleA
	constexpr float kPressThreshold = 0.5f;
	const bool bLeftNow  = LAxisVal  >= kPressThreshold;
	const bool bRightNow = RAxisVal >= kPressThreshold;

	FInputActionValue Dummy;
	if (bLeftNow != Edge.bLeftDown)
	{
		Edge.bLeftDown = bLeftNow;
		if (bLeftNow) { HandleTriggerLeftPressed(Dummy); }
		else          { HandleTriggerLeftReleased(Dummy); }
	}
	if (bRightNow != Edge.bRightDown)
	{
		Edge.bRightDown = bRightNow;
		if (bRightNow) { HandleTriggerRightPressed(Dummy); }
		else           { HandleTriggerRightReleased(Dummy); }
	}
}

void AClearanceVROperatorPawn::UpdateHandAnimInputs()
{
#if !CLEARANCE_HAS_OCULUS_INTERACTION
	// Meta XR Interaction SDK not installed on this build. Hand meshes (if
	// assigned) stay in their default anim-class rest pose; controller input
	// still reaches gameplay via the Enhanced Input trigger bindings. - TripleA
	return;
#else
	// Resolve anim instances lazily - LeftHand / RightHand are set in BP by
	// assigning SK_OpenXRHand_L/R + ABP_ControllerDrivenHand_L/R, so the
	// instance only exists after the mesh + anim class have been applied.
	// Cast to UQuestControllerAnimInstance so we can call the setters; if
	// the assigned anim class isn't one of the ISDK-provided ones the cast
	// returns null and this tick is a no-op. - TripleA
	UQuestControllerAnimInstance* AnimL =
		LeftHand  ? Cast<UQuestControllerAnimInstance>(LeftHand->GetAnimInstance())  : nullptr;
	UQuestControllerAnimInstance* AnimR =
		RightHand ? Cast<UQuestControllerAnimInstance>(RightHand->GetAnimInstance()) : nullptr;
	if (!AnimL && !AnimR) { return; }

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) { return; }

	// OculusTouch input keys resolved from strings so the code compiles even
	// when a specific EKeys::OculusTouch_* constant is missing from the UE
	// version in use. Unknown keys return 0 from the queries below, matching
	// the "no input" default and avoiding any hard failure. - TripleA
	static const FKey KLeftTriggerAxis   (TEXT("OculusTouch_Left_Trigger_Axis"));
	static const FKey KLeftGripAxis      (TEXT("OculusTouch_Left_Grip_Axis"));
	static const FKey KLeftTriggerTouch  (TEXT("OculusTouch_Left_Trigger_Touch"));
	static const FKey KLeftThumbX        (TEXT("OculusTouch_Left_Thumbstick_X"));
	static const FKey KLeftThumbY        (TEXT("OculusTouch_Left_Thumbstick_Y"));
	static const FKey KLeftThumbTouch    (TEXT("OculusTouch_Left_Thumbstick_Touch"));
	static const FKey KLeftXClick        (TEXT("OculusTouch_Left_X_Click"));
	static const FKey KLeftXTouch        (TEXT("OculusTouch_Left_X_Touch"));
	static const FKey KLeftYClick        (TEXT("OculusTouch_Left_Y_Click"));
	static const FKey KLeftYTouch        (TEXT("OculusTouch_Left_Y_Touch"));
	static const FKey KLeftMenu          (TEXT("OculusTouch_Left_Menu_Click"));

	static const FKey KRightTriggerAxis  (TEXT("OculusTouch_Right_Trigger_Axis"));
	static const FKey KRightGripAxis     (TEXT("OculusTouch_Right_Grip_Axis"));
	static const FKey KRightTriggerTouch (TEXT("OculusTouch_Right_Trigger_Touch"));
	static const FKey KRightThumbX       (TEXT("OculusTouch_Right_Thumbstick_X"));
	static const FKey KRightThumbY       (TEXT("OculusTouch_Right_Thumbstick_Y"));
	static const FKey KRightThumbTouch   (TEXT("OculusTouch_Right_Thumbstick_Touch"));
	static const FKey KRightAClick       (TEXT("OculusTouch_Right_A_Click"));
	static const FKey KRightATouch       (TEXT("OculusTouch_Right_A_Touch"));
	static const FKey KRightBClick       (TEXT("OculusTouch_Right_B_Click"));
	static const FKey KRightBTouch       (TEXT("OculusTouch_Right_B_Touch"));

	// The QuestControllerAnimInstance buttons + thumbsticks are shared
	// between both hands (left has X/Y + Left Menu, right has A/B), while
	// trigger + grip axes + thumbstick axes exist per-hand. Feed each anim
	// instance the same button state so a left-hand-only or right-hand-only
	// setup still animates the buttons its hand doesn't own (a controller
	// touching its own X still needs the left hand's thumb to move). - TripleA
	const float LTrig   = PC->GetInputAnalogKeyState(KLeftTriggerAxis);
	const float LGrip   = PC->GetInputAnalogKeyState(KLeftGripAxis);
	const bool  LTrigT  = PC->IsInputKeyDown(KLeftTriggerTouch);
	const float LThumbX = PC->GetInputAnalogKeyState(KLeftThumbX);
	const float LThumbY = PC->GetInputAnalogKeyState(KLeftThumbY);
	const bool  LThumbT = PC->IsInputKeyDown(KLeftThumbTouch);
	const bool  LXDown  = PC->IsInputKeyDown(KLeftXClick);
	const bool  LXTouch = PC->IsInputKeyDown(KLeftXTouch);
	const bool  LYDown  = PC->IsInputKeyDown(KLeftYClick);
	const bool  LYTouch = PC->IsInputKeyDown(KLeftYTouch);
	const bool  LMenu   = PC->IsInputKeyDown(KLeftMenu);

	const float RTrig   = PC->GetInputAnalogKeyState(KRightTriggerAxis);
	const float RGrip   = PC->GetInputAnalogKeyState(KRightGripAxis);
	const bool  RTrigT  = PC->IsInputKeyDown(KRightTriggerTouch);
	const float RThumbX = PC->GetInputAnalogKeyState(KRightThumbX);
	const float RThumbY = PC->GetInputAnalogKeyState(KRightThumbY);
	const bool  RThumbT = PC->IsInputKeyDown(KRightThumbTouch);
	const bool  RADown  = PC->IsInputKeyDown(KRightAClick);
	const bool  RATouch = PC->IsInputKeyDown(KRightATouch);
	const bool  RBDown  = PC->IsInputKeyDown(KRightBClick);
	const bool  RBTouch = PC->IsInputKeyDown(KRightBTouch);

	auto ApplyAll = [&](UQuestControllerAnimInstance* Anim)
	{
		if (!Anim) { return; }
		Anim->SetLeftFrontTriggerAxisValue(LTrig);
		Anim->SetLeftGripTriggerAxisValue(LGrip);
		Anim->SetLeftFrontTriggerTouched(LTrigT);
		Anim->SetLeftThumbstickXAxisValue(LThumbX);
		Anim->SetLeftThumbstickYAxisValue(LThumbY);
		Anim->SetLeftThumbstickTouched(LThumbT);
		Anim->SetXButtonDown(LXDown);
		Anim->SetXButtonTouched(LXTouch);
		Anim->SetYButtonDown(LYDown);
		Anim->SetYButtonTouched(LYTouch);
		Anim->SetLeftMenuButtonDown(LMenu);

		Anim->SetRightFrontTriggerAxisValue(RTrig);
		Anim->SetRightGripTriggerAxisValue(RGrip);
		Anim->SetRightFrontTriggerTouched(RTrigT);
		Anim->SetRightThumbstickXAxisValue(RThumbX);
		Anim->SetRightThumbstickYAxisValue(RThumbY);
		Anim->SetRightThumbstickTouched(RThumbT);
		Anim->SetAButtonDown(RADown);
		Anim->SetAButtonTouched(RATouch);
		Anim->SetBButtonDown(RBDown);
		Anim->SetBButtonTouched(RBTouch);
	};

	ApplyAll(AnimL);
	ApplyAll(AnimR);
#endif // CLEARANCE_HAS_OCULUS_INTERACTION
}
