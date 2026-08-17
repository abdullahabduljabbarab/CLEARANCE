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
#include "Components/WidgetComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"
#include "Simulation/ClearanceOperatorButton.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "Simulation/ClearanceSimulationController.h"
#include "UI/ClearanceInstructorPanel.h"
#include "Components/PanelWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Border.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "TimerManager.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "Layout/WidgetPath.h"
#include "Components/StereoLayerComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "IHeadMountedDisplay.h"
#include "IXRTrackingSystem.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Styling/SlateTypes.h"

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
	LeftControllerMesh->SetOnlyOwnerSee(false);
	LeftControllerMesh->SetOwnerNoSee(false);

	RightControllerMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightControllerMesh"));
	RightControllerMesh->SetupAttachment(RightController);
	RightControllerMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RightControllerMesh->SetOnlyOwnerSee(false);
	RightControllerMesh->SetOwnerNoSee(false);

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
	// Explicitly visible to non-owner cameras so the instructor's Operator
	// PIP scene capture picks up the hands - default OnlyOwnerSee that some
	// Meta XR / SDK plugins set on hand meshes would hide them from the
	// SceneCapture (which has no "owner"). - TripleA
	LeftHand->SetOnlyOwnerSee(false);
	LeftHand->SetOwnerNoSee(false);

	RightHand = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("RightHand"));
	RightHand->SetupAttachment(RightController);
	RightHand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RightHand->SetOnlyOwnerSee(false);
	RightHand->SetOwnerNoSee(false);

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

	// Fingertip WidgetInteractionComponents: short-range trace forward
	// from each fingertip sphere so a poked strip-monitor button fires
	// its OnClicked via the trigger press. Matches the LeftPointer /
	// RightPointer config that works for the scope monitor - same
	// defaults, only attachment point and distance differ (fingertip
	// vs controller, 60 cm vs 500 cm) so this reads as "touch reach"
	// instead of a laser. - TripleA
	LeftFingertipInteraction = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("LeftFingertipInteraction"));
	LeftFingertipInteraction->SetupAttachment(LeftFingertip);
	LeftFingertipInteraction->InteractionDistance = 60.f;
	LeftFingertipInteraction->bShowDebug = false;

	RightFingertipInteraction = CreateDefaultSubobject<UWidgetInteractionComponent>(TEXT("RightFingertipInteraction"));
	RightFingertipInteraction->SetupAttachment(RightFingertip);
	RightFingertipInteraction->InteractionDistance = 60.f;
	RightFingertipInteraction->bShowDebug = false;

	// Laser meshes for the pause menu. Hidden by default; TogglePauseMenu
	// flips them visible + stretches them per tick to terminate at the
	// widget hit point. Auto-assigns the engine's default cylinder mesh
	// as a working placeholder so the lasers are visible without a BP
	// subclass having to override a native-component StaticMesh property.
	// Default cylinder is Z-up 100cm tall x 100cm diameter - we rotate it
	// Pitch=90 so its Z-axis becomes the actor's forward +X, then scale to
	// (LengthPerTick, 0.005, 0.005) making a thin 0.5cm-radius beam that
	// stretches in world along the aim direction. Mesh + material are
	// overridable per subclass; the default gets a visible laser today. - TripleA
	LeftLaser = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftLaser"));
	LeftLaser->SetupAttachment(LeftController);
	LeftLaser->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LeftLaser->SetVisibility(false);
	LeftLaser->SetGenerateOverlapEvents(false);
	LeftLaser->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	LeftLaser->SetRelativeScale3D(FVector(1.f, 0.005f, 0.005f));

	RightLaser = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightLaser"));
	RightLaser->SetupAttachment(RightController);
	RightLaser->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RightLaser->SetVisibility(false);
	RightLaser->SetGenerateOverlapEvents(false);
	RightLaser->SetRelativeRotation(FRotator(90.f, 0.f, 0.f));
	RightLaser->SetRelativeScale3D(FVector(1.f, 0.005f, 0.005f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		LeftLaser ->SetStaticMesh(CylinderMesh.Object);
		RightLaser->SetStaticMesh(CylinderMesh.Object);
	}
	// EmissiveMeshMaterial is the shipped unlit-with-emissive-vertex-color
	// material - works at runtime (unlike /Engine/EditorMaterials/ paths
	// which are cooked out in packaged builds and often missing at PIE
	// time too). Overridable per subclass with a proper thin cyan tube
	// material later; this at least makes the lasers visibly cyan-ish. - TripleA
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LaserMat(
		TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial"));
	if (LaserMat.Succeeded())
	{
		LeftLaser ->SetMaterial(0, LaserMat.Object);
		RightLaser->SetMaterial(0, LaserMat.Object);
	}

	// Pause menu draws via a face-locked UStereoLayerComponent so the HMD
	// compositor renders it at real depth (not glued to the near-plane the
	// way AddToViewport HUD does in stereo). The stereo layer is attached
	// to Camera so it follows the HMD, hidden by default. Its texture
	// comes from a hidden UWidgetComponent's render target - see below.
	//
	// This bypasses the Substrate / Lumen / scene-shading path entirely -
	// the compositor writes the texture straight into the eye buffers. - TripleA
	PauseMenuStereoLayer = CreateDefaultSubobject<UStereoLayerComponent>(TEXT("PauseMenuStereoLayer"));
	PauseMenuStereoLayer->SetupAttachment(Camera);
	PauseMenuStereoLayer->SetRelativeLocation(FVector(PauseMenuDistanceCm, 0.f, 0.f));
	// StereoLayerType defaults to SLT_FaceLocked in the parent constructor -
	// no setter is exposed so the default is what we want anyway. - TripleA
	PauseMenuStereoLayer->SetPriority(100);
	PauseMenuStereoLayer->SetQuadSize(PauseMenuWorldSizeCm);
	PauseMenuStereoLayer->SetVisibility(false);

	// Hidden WidgetComponent that hosts the pause menu UUserWidget and
	// keeps its Slate render target updated every frame. Not rendered in
	// the scene - only its texture is used, fed straight to the stereo
	// layer above. The previous approach (FWidgetRenderer + SVirtualWindow)
	// painted an orphaned Slate tree that never received the normal
	// invalidation broadcasts, so per-tick style mutations (highlight
	// tinting, render scale) failed to reach the actual draw. Routing
	// through a WidgetComponent puts the widget back on the standard
	// Slate lifecycle where invalidations, prepass and paint all work
	// as designed. - TripleA
	PauseMenuWidgetComp = CreateDefaultSubobject<UWidgetComponent>(TEXT("PauseMenuWidgetComp"));
	PauseMenuWidgetComp->SetupAttachment(VROrigin);
	PauseMenuWidgetComp->SetWidgetSpace(EWidgetSpace::World);
	PauseMenuWidgetComp->SetDrawSize(PauseMenuDrawSizePx);
	PauseMenuWidgetComp->SetTickMode(ETickMode::Enabled);
	PauseMenuWidgetComp->SetTickWhenOffscreen(true);
	// Must stay visible AND not hidden-in-game so the RT actually updates
	// (both flags gate WidgetComponent::ShouldDrawWidget). Hiding it any
	// way we tried killed the render target, which killed the stereo
	// layer texture, which killed the menu. Instead we shove the world-
	// space quad 100m below the pawn's floor - it still renders and
	// updates its RT normally, but no camera ever sees it because it's
	// buried underground. The stereo layer picks up the RT and shows it
	// at proper depth in front of the operator. - TripleA
	PauseMenuWidgetComp->SetVisibility(true);
	PauseMenuWidgetComp->SetHiddenInGame(false);
	PauseMenuWidgetComp->SetRelativeLocation(FVector(0.f, 0.f, -10000.f));
	PauseMenuWidgetComp->SetRelativeScale3D(FVector(0.1f));
	PauseMenuWidgetComp->SetTwoSided(true);
	PauseMenuWidgetComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Operator strip monitor: lives on a diegetic BP_Monitor actor in the
	// tower level (replaces the original emergency monitor). No pawn-side
	// components needed - the monitor's own WidgetComponent hosts the
	// strip widget, and the widget calls back into the pawn helpers
	// (GetAircraftRowsForStrip / Strip*) for data + button dispatch. - TripleA
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

	// (World-space pause menu Substrate workaround removed - the pause
	// menu is now a HUD overlay, no WidgetComponent involved.) - TripleA

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

		// Subscribe to the end-session report the OperatorPC broadcasts
		// after Server_EndSessionAndReport finishes writing the AAR. The
		// callback swaps the stereo layer to WBP_VREndSessionReport so
		// the operator sees the score + time + restart / exit choices
		// without leaving VR. - TripleA
		OpPC->OnEndSessionReport.AddDynamic(this, &AClearanceVROperatorPawn::HandleEndSessionReport);
	}


	// (Operator strip monitor is a BP_Monitor actor in the level, not a
	// pawn component - no init needed here. Widget calls the pawn's
	// Strip* helpers directly via BP.) - TripleA

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
		if (PauseMenuAction)
		{
			// Started only - Toggle should fire once per press, not per hold. - TripleA
			EIC->BindAction(PauseMenuAction, ETriggerEvent::Started, this, &AClearanceVROperatorPawn::HandlePauseMenuPressed);
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
	// Suppress snap-turn while the pause menu is up - right stick is
	// repurposed for menu selection during that window (see the poll
	// block in Tick). Otherwise a scroll would also spin the player. - TripleA
	if (IsPauseMenuShowing()) { return; }

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

// Output-log trace for the fingertip + trigger chain. Flip to 1 to re-enable
// hover + press event tracing when a diegetic control stops responding. - TripleA
#ifndef CLEARANCE_LOG_OPERATOR_INTERACTION
#define CLEARANCE_LOG_OPERATOR_INTERACTION 0
#endif

#if CLEARANCE_LOG_OPERATOR_INTERACTION
static void ClearanceInteractionDebug(int32 /*Key*/, const FColor& /*C*/, const FString& Msg)
{
	UE_LOG(LogTemp, Warning, TEXT("[VRInteraction] %s"), *Msg);
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

	// Also route to the fingertip's WidgetInteractionComponent - if the
	// fingertip is over a strip button (or any world-space widget), this
	// fires the UMG click via the standard Slate mouse event pipeline. - TripleA
	if (LeftFingertipInteraction)
	{
		LeftFingertipInteraction->PressPointerKey(EKeys::LeftMouseButton);
	}

	if (!LeftHoveredButton) { return; }
	LeftPressedButton = LeftHoveredButton;
	LeftPressedButton->HandlePress(this, EOperatorButtonHand::Left);
}

void AClearanceVROperatorPawn::HandleTriggerLeftReleased(const FInputActionValue&)
{
	ClearanceInteractionDebug(2201, FColor::Blue,
		FString::Printf(TEXT("L trigger RELEASED (pressed: %s)"),
			LeftPressedButton ? *LeftPressedButton->GetName() : TEXT("<none>")));

	if (LeftFingertipInteraction)
	{
		LeftFingertipInteraction->ReleasePointerKey(EKeys::LeftMouseButton);
	}

	if (!LeftPressedButton) { return; }
	LeftPressedButton->HandleRelease(this, EOperatorButtonHand::Left);
	LeftPressedButton = nullptr;
}

void AClearanceVROperatorPawn::HandleTriggerRightPressed(const FInputActionValue&)
{
	ClearanceInteractionDebug(2300, FColor::Blue,
		FString::Printf(TEXT("R trigger PRESSED (hovered: %s)"),
			RightHoveredButton ? *RightHoveredButton->GetName() : TEXT("<none>")));

	if (RightFingertipInteraction)
	{
		RightFingertipInteraction->PressPointerKey(EKeys::LeftMouseButton);
	}

	if (!RightHoveredButton) { return; }
	RightPressedButton = RightHoveredButton;
	RightPressedButton->HandlePress(this, EOperatorButtonHand::Right);
}

void AClearanceVROperatorPawn::HandleTriggerRightReleased(const FInputActionValue&)
{
	ClearanceInteractionDebug(2301, FColor::Blue,
		FString::Printf(TEXT("R trigger RELEASED (pressed: %s)"),
			RightPressedButton ? *RightPressedButton->GetName() : TEXT("<none>")));

	if (RightFingertipInteraction)
	{
		RightFingertipInteraction->ReleasePointerKey(EKeys::LeftMouseButton);
	}

	if (!RightPressedButton) { return; }
	RightPressedButton->HandleRelease(this, EOperatorButtonHand::Right);
	RightPressedButton = nullptr;
}

void AClearanceVROperatorPawn::HandlePauseMenuPressed(const FInputActionValue&)
{
	TogglePauseMenu();
}

void AClearanceVROperatorPawn::TogglePauseMenu()
{
	// HUD overlay path. World-space WidgetComponent under Substrate + VR
	// rendered as black / grid checker despite the widget being authored
	// with bright content in the WBP designer. Switched to AddToViewport
	// which uses the same Slate-to-framebuffer path as the working main
	// menu - proven to render in VR. Tradeoff: menu is head-locked HUD
	// rather than a floating panel in the world, but it actually appears
	// and matches the pattern every commercial VR title uses for pause
	// menus (Half-Life: Alyx, Beat Saber, Boneworks). - TripleA

	if (PauseMenuInstance)
	{
		HidePauseMenu();
		return;
	}

	if (!PauseMenuWidgetClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PAUSE] PauseMenuWidgetClass is NULL - assign WBP_VRPauseMenu on BP_VROperatorPawn Details."));
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !Camera) { return; }

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) { return; }

	if (!PauseMenuWidgetComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PAUSE] PauseMenuWidgetComp null"));
		return;
	}

	// Always open on the pause-menu screen, regardless of what was
	// showing when the menu was last hidden. - TripleA
	CurrentScreen = EPauseScreen::PauseMenu;
	if (PauseMenuWidgetComp->GetWidgetClass() != PauseMenuWidgetClass)
	{
		PauseMenuWidgetComp->SetWidgetClass(PauseMenuWidgetClass);
	}
	PauseMenuWidgetComp->SetDrawSize(PauseMenuDrawSizePx);
	PauseMenuWidgetComp->InitWidget();
	PauseMenuInstance = Cast<UUserWidget>(PauseMenuWidgetComp->GetWidget());
	if (!PauseMenuInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PAUSE] PauseMenuWidgetComp->GetWidget() returned null"));
		return;
	}

	// Reset selection so the menu always opens with row 0 highlighted.
	PauseMenuSelectedIndex = 0;

	if (PauseMenuStereoLayer)
	{
		// GetRenderTarget only becomes non-null after the WidgetComponent
		// has ticked at least once (its RT is created lazily in tick).
		// The stereo layer picks the current texture up next frame if
		// it's null now - safe either way. - TripleA
		PauseMenuStereoLayer->SetTexture(PauseMenuWidgetComp->GetRenderTarget());
		PauseMenuStereoLayer->SetQuadSize(PauseMenuWorldSizeCm);
		PauseMenuStereoLayer->SetRelativeLocation(FVector(PauseMenuDistanceCm, 0.f, 0.f));
		PauseMenuStereoLayer->SetVisibility(true);
	}

	if (AClearanceSimulationController* SC = Cast<AClearanceSimulationController>(
			UGameplayStatics::GetActorOfClass(World, AClearanceSimulationController::StaticClass())))
	{
		SC->PauseSession();
	}
}

void AClearanceVROperatorPawn::OnPauseMenuConfirm_Implementation(int32 SelectedIndex)
{
	// End-session report screen: two rows, both terminal.
	//   0 = RESTART SESSION   -> Server_InjectResetScenario, dismiss
	//   1 = EXIT TO MENU      -> disable HMD + OpenLevel(LVL_MainMenu)
	if (CurrentScreen == EPauseScreen::EndSessionReport)
	{
		switch (SelectedIndex)
		{
		case 0:
		{
			if (APlayerController* PC = Cast<APlayerController>(GetController()))
			{
				if (AClearanceOperatorPC* OpPC = Cast<AClearanceOperatorPC>(PC))
				{
					OpPC->Server_InjectResetScenario();
				}
			}
			HidePauseMenu();
			return;
		}
		case 1:
		default:
		{
			if (GEngine && GEngine->XRSystem.IsValid() && GEngine->XRSystem->GetHMDDevice())
			{
				GEngine->XRSystem->GetHMDDevice()->EnableHMD(false);
			}
			HidePauseMenu();
			UGameplayStatics::OpenLevel(
				this,
				FName(TEXT("/Game/LVL_MainMenu.LVL_MainMenu")),
				/*bAbsolute=*/true);
			return;
		}
		}
	}

	// Dispatch routes through the CurrentScreen state - trigger presses
	// on the pause menu do one thing, trigger presses on the options
	// screen do another. All screens share the same stereo layer +
	// WidgetComponent, so the only visible difference is which widget
	// class is bound. - TripleA
	if (CurrentScreen == EPauseScreen::Options)
	{
		// Options widget row order:
		//   0 = RECENTER VIEW    -> IXRTrackingSystem::ResetOrientationAndPosition
		//   1 = SNAP TURN        -> cycle 30 / 45 / 90 deg
		//   2 = RADIO VOLUME     -> cycle Low / Med / High
		//   3 = BACK             -> return to pause menu screen
		switch (SelectedIndex)
		{
		case 0:
		{
			if (GEngine && GEngine->XRSystem.IsValid())
			{
				GEngine->XRSystem->ResetOrientationAndPosition(0.f);
			}
			return;
		}
		case 1:
		{
			SnapTurnStepIndex = (SnapTurnStepIndex + 1) % 3;
			ApplySnapTurnStep();
			return;
		}
		case 2:
		{
			RadioVolumeStepIndex = (RadioVolumeStepIndex + 1) % 3;
			ApplyRadioVolumeStep();
			return;
		}
		case 3:
		default:
		{
			// Back to the pause menu, land the highlight on OPTIONS row
			// (index 1) so a second trigger press re-enters options
			// without wading past Resume. - TripleA
			ShowPauseMenuScreen();
			PauseMenuSelectedIndex = 1;
			return;
		}
		}
	}

	// PauseMenu screen row order:
	//   0 = RESUME          -> HidePauseMenu
	//   1 = OPTIONS         -> swap widget to VROptions, land on row 0
	//   2 = END SESSION     -> Server_EndSessionAndReport on the operator PC
	//   3 = EXIT TO MENU    -> disable HMD + OpenLevel(LVL_MainMenu)
	switch (SelectedIndex)
	{
	case 0:
	{
		HidePauseMenu();
		return;
	}

	case 1:
	{
		if (!VROptionsWidgetClass)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[PAUSE] OPTIONS confirmed but VROptionsWidgetClass is not set on BP_VROperatorPawn."));
			return;
		}
		ShowOptionsScreen();
		return;
	}

	case 2:
	{
		// End the session cleanly through the OperatorPC RPC - same call
		// path as the desktop End Session button on the instructor panel.
		// Server writes the AAR, halts the sim, and RPCs back a report
		// which the instructor panel modal already handles on the desktop
		// side. In VR this just dismisses the pause menu; the desktop
		// twin (if any) surfaces the modal. - TripleA
		APlayerController* PC = Cast<APlayerController>(GetController());
		AClearanceOperatorPC* OpPC = Cast<AClearanceOperatorPC>(PC);
		// Order matters. Server RPC on listen-server host invokes
		// synchronously - HandleEndSessionReport runs INSIDE the
		// Server_EndSessionAndReport call. If we set placeholders after
		// the RPC, the placeholder assignment clobbers the real values
		// the callback just wrote. So: show placeholders + swap the
		// widget FIRST, then fire the RPC and let its callback overwrite
		// the fields in place. The tick loop will push the real text
		// on the very next frame. - TripleA
		EndSessionReportScore          = 0;
		EndSessionReportSessionSeconds = 0.f;
		EndSessionReportPath           = TEXT("(saving...)");
		ShowEndSessionReportScreen();
		if (OpPC)
		{
			OpPC->Server_EndSessionAndReport();
		}
		return;
	}

	case 3:
	default:
	{
		// Exit: disable HMD so the flat menu isn't rendered stereoscopically,
		// then OpenLevel to the main menu map. Safe no-op if HMD wasn't
		// already enabled. Mirrors UClearanceMenuFunctionLibrary::ExitToMainMenu
		// without pulling the game module in as a plugin dependency. - TripleA
		// Force the HMD off before OpenLevel so the flat main menu renders
		// on the desktop monitor, not stereoscopically. Same call the
		// desktop UClearanceMenuFunctionLibrary::ExitToMainMenu uses -
		// UHeadMountedDisplayFunctionLibrary::EnableHMD is the high-level
		// path that also stops stereo rendering, drops OpenXR session
		// focus and returns spectator screen control to the monitor.
		// A raw GetHMDDevice()->EnableHMD(false) call skipped some of
		// those steps and left the main menu stuck in stereo. - TripleA
		UHeadMountedDisplayFunctionLibrary::EnableHMD(false);
		HidePauseMenu();
		UGameplayStatics::OpenLevel(
			this,
			FName(TEXT("/Game/LVL_MainMenu.LVL_MainMenu")),
			/*bAbsolute=*/true);
		return;
	}
	}
}

void AClearanceVROperatorPawn::ShowPauseMenuScreen()
{
	CurrentScreen = EPauseScreen::PauseMenu;
	if (PauseMenuWidgetComp && PauseMenuWidgetClass)
	{
		PauseMenuWidgetComp->SetWidgetClass(PauseMenuWidgetClass);
		PauseMenuInstance = Cast<UUserWidget>(PauseMenuWidgetComp->GetWidget());
	}
}

void AClearanceVROperatorPawn::ShowOptionsScreen()
{
	CurrentScreen = EPauseScreen::Options;
	PauseMenuSelectedIndex = 0;
	if (PauseMenuWidgetComp && VROptionsWidgetClass)
	{
		PauseMenuWidgetComp->SetWidgetClass(VROptionsWidgetClass);
		PauseMenuInstance = Cast<UUserWidget>(PauseMenuWidgetComp->GetWidget());
	}
}

void AClearanceVROperatorPawn::ShowEndSessionReportScreen()
{
	if (!VREndSessionReportWidgetClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PAUSE] End-session report ready but VREndSessionReportWidgetClass is not set on BP_VROperatorPawn."));
		return;
	}
	CurrentScreen = EPauseScreen::EndSessionReport;
	PauseMenuSelectedIndex = 0;

	// Swap the widget class in place. The stereo layer is already
	// showing the pause menu at this point (End Session dispatch skips
	// HidePauseMenu specifically so the compositor keeps the layer
	// alive), so the operator just sees the panel contents change from
	// the pause menu to the report - no visibility toggle, no compositor
	// tear-down. - TripleA
	if (PauseMenuWidgetComp)
	{
		PauseMenuWidgetComp->SetWidgetClass(VREndSessionReportWidgetClass);
		PauseMenuInstance = Cast<UUserWidget>(PauseMenuWidgetComp->GetWidget());
	}

	// Belt-and-braces: if we somehow got here with the layer hidden
	// (edge cases outside the End Session path, e.g. BP calls this
	// helper directly), re-show it. Cheap no-op when already visible. - TripleA
	if (PauseMenuStereoLayer && !PauseMenuStereoLayer->IsVisible())
	{
		PauseMenuStereoLayer->SetTexture(PauseMenuWidgetComp ? PauseMenuWidgetComp->GetRenderTarget() : nullptr);
		PauseMenuStereoLayer->bLiveTexture = true;
		PauseMenuStereoLayer->SetVisibility(true);
		PauseMenuStereoLayer->MarkTextureForUpdate();
	}
}

void AClearanceVROperatorPawn::HandleEndSessionReport(const FString& FilePath, int32 Score, float SessionTimeSeconds)
{
	EndSessionReportPath           = FilePath;
	EndSessionReportScore          = Score;
	EndSessionReportSessionSeconds = SessionTimeSeconds;
	ShowEndSessionReportScreen();
}

int32 AClearanceVROperatorPawn::GetCurrentScreenOptionCount() const
{
	return (CurrentScreen == EPauseScreen::EndSessionReport) ? 2 : PauseMenuOptionCount;
}

void AClearanceVROperatorPawn::ApplySnapTurnStep()
{
	static const float Steps[] = { 30.f, 45.f, 90.f };
	const int32 Idx = FMath::Clamp(SnapTurnStepIndex, 0, 2);
	SnapTurnDegrees = Steps[Idx];
}

void AClearanceVROperatorPawn::ApplyRadioVolumeStep()
{
	// au.MasterVolume is the same console variable the desktop options
	// widget writes to - keeping the two options screens on the same
	// underlying knob so a change made in one persists in the other. - TripleA
	static const float Steps[] = { 0.3f, 0.6f, 1.0f };
	const int32 Idx = FMath::Clamp(RadioVolumeStepIndex, 0, 2);
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->ConsoleCommand(FString::Printf(TEXT("au.MasterVolume %.2f"), Steps[Idx]), /*bWriteToLog=*/false);
	}
}

void AClearanceVROperatorPawn::EndPlay(const EEndPlayReason::Type Reason)
{
	// FWidgetRenderer routes its own deletion through the Slate deferred
	// cleanup queue (safe for pending render commands referencing it),
	// so BeginCleanup is the correct release call, not plain delete. - TripleA
	if (PauseMenuRenderer)
	{
		BeginCleanup(PauseMenuRenderer);
		PauseMenuRenderer = nullptr;
	}
	Super::EndPlay(Reason);
}

void AClearanceVROperatorPawn::HidePauseMenu()
{
	if (PauseMenuStereoLayer)
	{
		// Hide the layer but keep its texture reference alive - clearing
		// the texture caused the HMD compositor to fully deactivate the
		// layer, and it wouldn't reliably re-register on a subsequent
		// SetVisibility(true) when the end-session report screen tried
		// to show. Keeping the texture pointer live lets the compositor
		// just toggle the layer visibility instead of tearing it down. - TripleA
		PauseMenuStereoLayer->SetVisibility(false);
	}

	// PauseMenuInstance is owned by PauseMenuWidgetComp - do NOT call
	// RemoveFromParent (would strip it out of the WidgetComponent tree).
	// Just clear our cached pointer; the WidgetComponent holds the widget
	// itself for the next TogglePauseMenu. - TripleA
	PauseMenuInstance = nullptr;

	if (UWorld* World = GetWorld())
	{
		if (AClearanceSimulationController* SC = Cast<AClearanceSimulationController>(
				UGameplayStatics::GetActorOfClass(World, AClearanceSimulationController::StaticClass())))
		{
			SC->ResumeSession();
		}
	}
}

// --- Operator strip monitor helpers ----------------------------------------

TArray<FInstructorAircraftRow> AClearanceVROperatorPawn::GetAircraftRowsForStrip() const
{
	TArray<FInstructorAircraftRow> Rows;
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) { return Rows; }
	AClearanceOperatorPC* OpPC = Cast<AClearanceOperatorPC>(PC);
	if (!OpPC) { return Rows; }
	UClearanceInstructorPanel* Panel = OpPC->GetInstructorPanel();
	if (!Panel) { return Rows; }
	return Panel->GetAircraftRows();
}

void AClearanceVROperatorPawn::StripCycleThreatClass(FName Callsign)
{
	if (Callsign == NAME_None) { return; }
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) { return; }
	AClearanceOperatorPC* OpPC = Cast<AClearanceOperatorPC>(PC);
	if (!OpPC) { return; }
	UClearanceInstructorPanel* Panel = OpPC->GetInstructorPanel();
	if (!Panel) { return; }

	// Find the current classification so we can advance to the next one.
	// Row build already resolves the operator-side classification (which
	// respects Hijack -> Hostile view override), so no separate live-state
	// lookup is needed. Cycle order F -> H -> N -> U -> F. - TripleA
	const TArray<FInstructorAircraftRow> Rows = Panel->GetAircraftRows();
	EThreatClass Cur = EThreatClass::Unknown;
	for (const FInstructorAircraftRow& R : Rows)
	{
		if (R.Callsign == Callsign) { Cur = R.OperatorClassification; break; }
	}
	// Cycle order tuned so the common workflow "civilian aircraft turns
	// out to be a bandit, mark it Hostile" is a single tap. Aircraft
	// spawn Neutral by default; one T poke escalates straight to
	// Hostile. Subsequent pokes cycle down through Friendly / Unknown
	// and back to Neutral for the downgrade / re-ID cases. - TripleA
	EThreatClass Next = EThreatClass::Hostile;
	switch (Cur)
	{
	case EThreatClass::Neutral:  Next = EThreatClass::Hostile;  break;
	case EThreatClass::Hostile:  Next = EThreatClass::Friendly; break;
	case EThreatClass::Friendly: Next = EThreatClass::Unknown;  break;
	case EThreatClass::Unknown:  Next = EThreatClass::Neutral;  break;
	default:                     Next = EThreatClass::Hostile;  break;
	}
	// Diegetic operator call - updates the operator's view + runs the
	// mis-ID scoring path if this promotes a civilian to Hostile.
	// Server_InjectClassify would rewrite TrueAffiliation instead, which
	// the strip doesn't display (strip shows OperatorClassification). - TripleA
	OpPC->Server_OperatorClassify(Callsign, Next);
}

void AClearanceVROperatorPawn::StripInterrogate(FName Callsign)
{
	if (Callsign == NAME_None) { return; }
	if (AClearanceOperatorPC* OpPC = Cast<AClearanceOperatorPC>(GetController()))
	{
		OpPC->Server_InjectInterrogate(Callsign);
	}
}

void AClearanceVROperatorPawn::StripAckEmergency(FName Callsign)
{
	if (Callsign == NAME_None) { return; }
	if (AClearanceOperatorPC* OpPC = Cast<AClearanceOperatorPC>(GetController()))
	{
		OpPC->Server_InjectClearEmergency(Callsign);
	}
}

static constexpr int32 kStripRowPoolSize = 20;

// Colour palette shared across all strip rows. - TripleA
static const FLinearColor kStripBgNormal   (0.03f, 0.06f, 0.08f, 0.85f);
static const FLinearColor kStripBgEmergency(0.40f, 0.05f, 0.05f, 0.95f);
static const FLinearColor kStripTextDefault(0.85f, 0.90f, 0.95f, 1.f);
static const FLinearColor kStripTextEmergency(1.f, 0.35f, 0.35f, 1.f);
static const FLinearColor kStripTextAmber    (1.f, 0.65f, 0.f,   1.f);
static const FLinearColor kStripTextCyan     (0.20f, 0.85f, 1.0f, 1.f);
static const FLinearColor kStripTextGreen    (0.20f, 0.90f, 0.30f, 1.f);
static const FLinearColor kStripTextRed      (0.95f, 0.25f, 0.25f, 1.f);

static const TCHAR* StripFreqLabel(ECommsFrequency F)
{
	switch (F)
	{
	case ECommsFrequency::Tower:     return TEXT("TWR");
	case ECommsFrequency::Approach:  return TEXT("APP");
	case ECommsFrequency::Emergency: return TEXT("EMRG");
	case ECommsFrequency::Guard:     return TEXT("GRD");
	default:                         return TEXT("---");
	}
}

static const TCHAR* StripThreatSymbol(EThreatClass T)
{
	switch (T)
	{
	case EThreatClass::Friendly: return TEXT("▭");   // rectangle
	case EThreatClass::Hostile:  return TEXT("◆");   // diamond
	case EThreatClass::Unknown:  return TEXT("⬡");   // hexagon
	case EThreatClass::Neutral:  return TEXT("◻");   // square
	default:                     return TEXT("?");
	}
}

static FLinearColor StripThreatColor(EThreatClass T)
{
	switch (T)
	{
	case EThreatClass::Friendly: return kStripTextCyan;
	case EThreatClass::Hostile:  return kStripTextRed;
	case EThreatClass::Unknown:  return kStripTextAmber;
	case EThreatClass::Neutral:  return kStripTextGreen;
	default:                     return kStripTextDefault;
	}
}

FStripRowRefs AClearanceVROperatorPawn::BuildStripRow(UPanelWidget* Container, int32 RowIndex)
{
	FStripRowRefs Refs;
	if (!Container) { return Refs; }

	// WidgetTree ownership: use the widget hosting the Container so the
	// new widgets are properly parented in the WBP's tree and rendered
	// by the same WidgetComponent. - TripleA
	UUserWidget* HostWidget = Container->GetTypedOuter<UUserWidget>();
	if (!HostWidget) { return Refs; }

	auto MakeName = [RowIndex](const TCHAR* Suffix)
	{
		return FName(*FString::Printf(TEXT("Strip_Row%d_%s"), RowIndex, Suffix));
	};

	// NewObject with the host UUserWidget as outer, not WidgetTree->
	// ConstructWidget: the latter is the design-time construction path
	// UMG uses when compiling a WBP, and in UE 5.7 with Substrate the
	// resulting widgets don't get picked up by the live Slate hierarchy
	// the world-space WidgetComponent rasterizes. Plain NewObject +
	// AddChild is the correct runtime path. - TripleA
	Refs.Bg = NewObject<UBorder>(HostWidget, MakeName(TEXT("Bg")));
	Refs.Bg->SetPadding(FMargin(6.f, 3.f));
	Refs.Bg->SetBrushColor(kStripBgNormal);

	Refs.Root = NewObject<UHorizontalBox>(HostWidget, MakeName(TEXT("HBox")));
	Refs.Bg->SetContent(Refs.Root);

	// Compact sizing so each column fits its expected text width without
	// bleeding into the next. If everything still looks squished after a
	// pool rebuild, bump the WidgetComponent's DrawSize on the strip
	// monitor actor in the level - font size scales with pixel budget. - TripleA
	FSlateFontInfo RowFont = FCoreStyle::GetDefaultFontStyle("Regular", 14);

	auto MakeText = [&](const TCHAR* Suffix, float FillWidth, ETextJustify::Type Justify)
	{
		UTextBlock* T = NewObject<UTextBlock>(HostWidget, MakeName(Suffix));
		T->SetColorAndOpacity(FSlateColor(kStripTextDefault));
		T->SetFont(RowFont);
		T->SetJustification(Justify);
		// Clip overflow at the slot edge instead of letting long strings
		// bleed over the next column. - TripleA
		T->SetClipping(EWidgetClipping::ClipToBounds);
		UHorizontalBoxSlot* Slot = Refs.Root->AddChildToHorizontalBox(T);
		if (Slot)
		{
			FSlateChildSize Size;
			Size.SizeRule = ESlateSizeRule::Fill;
			Size.Value    = FillWidth;
			Slot->SetSize(Size);
			Slot->SetPadding(FMargin(4.f, 0.f));
			Slot->SetVerticalAlignment(VAlign_Center);
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return T;
	};

	auto MakeButton = [&](const TCHAR* Suffix, const FString& Label, float FillWidth, EStripButtonKind Kind)
	{
		UClearanceStripButton* B = NewObject<UClearanceStripButton>(HostWidget, MakeName(Suffix));
		B->Init(this, Kind);
		// Tint the existing default brush structure without replacing it
		// (replacing DrawAs / ResourceObject broke the SButton hit region
		// in a prior attempt). Just recolor the built-in Normal / Hovered
		// / Pressed brushes so the button visibly changes on approach. - TripleA
		FButtonStyle Style = B->GetStyle();
		Style.Normal .TintColor = FSlateColor(FLinearColor(0.20f, 0.30f, 0.40f, 1.f));
		Style.Hovered.TintColor = FSlateColor(FLinearColor(1.00f, 0.65f, 0.00f, 1.f));
		Style.Pressed.TintColor = FSlateColor(FLinearColor(0.95f, 0.25f, 0.25f, 1.f));
		B->SetStyle(Style);
		UTextBlock* LabelTxt = NewObject<UTextBlock>(HostWidget,
			MakeName(*FString::Printf(TEXT("%sLbl"), Suffix)));
		LabelTxt->SetText(FText::FromString(Label));
		LabelTxt->SetColorAndOpacity(FSlateColor(kStripTextDefault));
		LabelTxt->SetFont(RowFont);
		LabelTxt->SetJustification(ETextJustify::Center);
		B->AddChild(LabelTxt);
		UHorizontalBoxSlot* Slot = Refs.Root->AddChildToHorizontalBox(B);
		if (Slot)
		{
			FSlateChildSize Size;
			Size.SizeRule = ESlateSizeRule::Fill;
			Size.Value    = FillWidth;
			Slot->SetSize(Size);
			Slot->SetPadding(FMargin(4.f, 2.f));
			Slot->SetVerticalAlignment(VAlign_Fill);
			Slot->SetHorizontalAlignment(HAlign_Fill);
		}
		return B;
	};

	// Fill-widths sum to ~15 so each unit is ~1/15 of row width. Callsign
	// gets the most space (long strings like "BAW103"), buttons get
	// explicit fill so they're never zero-width. Alignments: left for
	// text labels, center for altitude / heading / freq / emergency /
	// timer so their fixed-width formats sit tidily. - TripleA
	Refs.Text_Callsign  = MakeText(TEXT("Callsign"),  3.0f, ETextJustify::Left);
	Refs.Text_Freq      = MakeText(TEXT("Freq"),      1.0f, ETextJustify::Center);
	Refs.Text_Emergency = MakeText(TEXT("Emergency"), 1.8f, ETextJustify::Center);
	Refs.Text_Timer     = MakeText(TEXT("Timer"),     1.2f, ETextJustify::Center);
	Refs.Text_Alt       = MakeText(TEXT("Alt"),       2.6f, ETextJustify::Center);
	Refs.Text_Hdg       = MakeText(TEXT("Hdg"),       2.0f, ETextJustify::Center);
	Refs.Btn_Threat     = MakeButton(TEXT("Threat"), TEXT("T"), 0.9f, EStripButtonKind::Threat);
	Refs.Btn_Int        = MakeButton(TEXT("Int"),    TEXT("I"), 0.9f, EStripButtonKind::Interrogate);
	Refs.Btn_Ack        = MakeButton(TEXT("Ack"),    TEXT("A"), 0.9f, EStripButtonKind::Ack);

	Container->AddChild(Refs.Bg);
	Refs.Bg->SetVisibility(ESlateVisibility::Collapsed);
	return Refs;
}

void AClearanceVROperatorPawn::UpdateStripRow(FStripRowRefs& Refs, const FInstructorAircraftRow& Data)
{
	if (!Refs.Bg) { return; }
	Refs.BoundCallsign = Data.Callsign;

	// Rebind every button to the current row's callsign so pool row
	// reuse still dispatches trigger presses to the right aircraft. - TripleA
	if (Refs.Btn_Threat) { Refs.Btn_Threat->BoundCallsign = Data.Callsign; }
	if (Refs.Btn_Int)    { Refs.Btn_Int->BoundCallsign    = Data.Callsign; }
	if (Refs.Btn_Ack)    { Refs.Btn_Ack->BoundCallsign    = Data.Callsign; }

	const bool bEmergency = (Data.ActiveEmergency != EEmergencyType::None);
	Refs.Bg->SetBrushColor(bEmergency ? kStripBgEmergency : kStripBgNormal);

	if (Refs.Text_Callsign)
	{
		Refs.Text_Callsign->SetText(FText::FromString(
			FString::Printf(TEXT("%s %s"), StripThreatSymbol(Data.OperatorClassification), *Data.Callsign.ToString())));
		Refs.Text_Callsign->SetColorAndOpacity(FSlateColor(StripThreatColor(Data.OperatorClassification)));
	}

	if (Refs.Text_Freq)
	{
		Refs.Text_Freq->SetText(FText::FromString(StripFreqLabel(Data.AssignedFrequency)));
	}

	if (Refs.Text_Emergency)
	{
		// Squawk digits dropped - ATC convention pairs code with type
		// (7700 <-> MAY, 7600 <-> NRD, 7500 <-> HIJ), operator already
		// knows the mapping and the strip's per-row real estate is at
		// a premium. Timer next to the code conveys urgency. - TripleA
		FString Label;
		switch (Data.ActiveEmergency)
		{
		case EEmergencyType::GeneralMayday: Label = TEXT("MAY");  break;
		case EEmergencyType::CommsFailure:  Label = TEXT("NRD");  break;
		case EEmergencyType::Hijack:        Label = TEXT("HIJ");  break;
		case EEmergencyType::FuelLow:       Label = TEXT("FUEL"); break;
		default: break;
		}
		Refs.Text_Emergency->SetText(FText::FromString(Label));
		Refs.Text_Emergency->SetColorAndOpacity(FSlateColor(bEmergency ? kStripTextEmergency : kStripTextDefault));
	}

	if (Refs.Text_Timer)
	{
		if (Data.EmergencyTimerMinutes >= 0.f)
		{
			const int32 TotalSecs = FMath::Max(0, FMath::FloorToInt(Data.EmergencyTimerMinutes * 60.f));
			const int32 Mins = TotalSecs / 60;
			const int32 Secs = TotalSecs % 60;
			Refs.Text_Timer->SetText(FText::FromString(FString::Printf(TEXT("%d:%02d"), Mins, Secs)));
			// Amber < 3 min, red < 1 min. - TripleA
			FLinearColor C = kStripTextDefault;
			if      (Data.EmergencyTimerMinutes < 1.f) { C = kStripTextRed; }
			else if (Data.EmergencyTimerMinutes < 3.f) { C = kStripTextAmber; }
			Refs.Text_Timer->SetColorAndOpacity(FSlateColor(C));
		}
		else
		{
			Refs.Text_Timer->SetText(FText::GetEmpty());
		}
	}

	if (Refs.Text_Alt)
	{
		// Only show target when it differs from current - reduces clutter
		// in the common case where the aircraft is on assigned altitude
		// and leaves more pixels for the text that actually needs them. - TripleA
		const int32 CurFL = FMath::RoundToInt(Data.Altitude / 100.f);
		const int32 TgtFL = FMath::RoundToInt(Data.TargetAltitude / 100.f);
		Refs.Text_Alt->SetText(FText::FromString(
			(CurFL == TgtFL)
				? FString::Printf(TEXT("FL%03d"), CurFL)
				: FString::Printf(TEXT("FL%03d→%03d"), CurFL, TgtFL)));
	}
	if (Refs.Text_Hdg)
	{
		const int32 CurHdg = FMath::RoundToInt(Data.Heading);
		const int32 TgtHdg = FMath::RoundToInt(Data.TargetHeading);
		Refs.Text_Hdg->SetText(FText::FromString(
			(CurHdg == TgtHdg)
				? FString::Printf(TEXT("%03d"), CurHdg)
				: FString::Printf(TEXT("%03d→%03d"), CurHdg, TgtHdg)));
	}
}

void AClearanceVROperatorPawn::RefreshStripRows(UPanelWidget* Container, UWidgetComponent* HostComponent)
{
	if (!Container) { return; }

	// First-call pool build. Constructs 20 rows into the container and
	// caches their widget refs. Subsequent calls skip this block. - TripleA
	if (BuiltStripRows.Num() == 0)
	{
		BuiltStripRows.Reserve(kStripRowPoolSize);
		for (int32 i = 0; i < kStripRowPoolSize; ++i)
		{
			BuiltStripRows.Add(BuildStripRow(Container, i));
		}

		// Force the WidgetComponent to fully re-init its SlateWindow so
		// the runtime-added children appear on the world-space render
		// target. Its cached Slate tree is a snapshot taken at first
		// InitWidget, and AddChild after that snapshot doesn't propagate
		// to the compositor render path. SetWidget(nullptr) tears down
		// the cached tree; SetWidget(HostWidget) rebuilds it fresh with
		// the new children included. - TripleA
		if (HostComponent)
		{
			if (UUserWidget* HostWidget = Cast<UUserWidget>(HostComponent->GetWidget()))
			{
				HostComponent->SetWidget(nullptr);
				HostComponent->SetWidget(HostWidget);
			}
		}
	}

	TArray<FInstructorAircraftRow> Rows = GetAircraftRowsForStrip();

	// Sort emergencies to the top: FuelLow / GeneralMayday first (sorted
	// by remaining timer, lowest = most urgent), then Hijack / Nordo, then
	// normal traffic in original order. Operator's eye lands on the top
	// of the strip so time-critical rows are always the first thing seen.
	// - TripleA
	auto EmergencyRank = [](const FInstructorAircraftRow& R)
	{
		if (R.ActiveEmergency == EEmergencyType::None) { return 2; }
		if (R.EmergencyTimerMinutes >= 0.f) { return 0; }
		return 1;
	};
	Rows.StableSort([&EmergencyRank](const FInstructorAircraftRow& A, const FInstructorAircraftRow& B)
	{
		const int32 RA = EmergencyRank(A);
		const int32 RB = EmergencyRank(B);
		if (RA != RB) { return RA < RB; }
		if (RA == 0) { return A.EmergencyTimerMinutes < B.EmergencyTimerMinutes; }
		return false;
	});

	const int32 NumRows = Rows.Num();

	for (int32 i = 0; i < BuiltStripRows.Num(); ++i)
	{
		FStripRowRefs& Refs = BuiltStripRows[i];
		if (!Refs.Bg) { continue; }

		if (i < NumRows)
		{
			Refs.Bg->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			UpdateStripRow(Refs, Rows[i]);
		}
		else
		{
			Refs.Bg->SetVisibility(ESlateVisibility::Collapsed);
			Refs.BoundCallsign = NAME_None;
		}
	}

	if (NumRows > BuiltStripRows.Num())
	{
		static bool bLoggedPoolExhausted = false;
		if (!bLoggedPoolExhausted)
		{
			bLoggedPoolExhausted = true;
			UE_LOG(LogTemp, Warning,
				TEXT("[STRIP] Aircraft count (%d) exceeded row pool (%d). Bump kStripRowPoolSize."),
				NumRows, BuiltStripRows.Num());
		}
	}
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

	// Manual strip-button hover feedback. WIC does hover-state updates
	// on the slate widget but its hover brush doesn't render on the
	// world-space WidgetComponent under Substrate for reasons that
	// aren't worth another day of debugging. Instead: per tick, walk
	// each fingertip's hovered widget path, find the UClearanceStripButton
	// whose cached Slate widget is in that path, and paint its Normal
	// brush amber. When hover ends, revert to the default navy tint.
	// Cheap - only mutates style when the hovered button actually
	// changes. - TripleA
	auto ResolveHoveredStripButton = [this](UWidgetInteractionComponent* WIC) -> UClearanceStripButton*
	{
		if (!WIC) { return nullptr; }
		const FWeakWidgetPath& WeakPath = WIC->GetHoveredWidgetPath();
		if (!WeakPath.IsValid()) { return nullptr; }
		FWidgetPath Path = WeakPath.ToWidgetPath(FWeakWidgetPath::EInterruptedPathHandling::Truncate);
		if (!Path.IsValid()) { return nullptr; }

		for (const FStripRowRefs& Row : BuiltStripRows)
		{
			for (UClearanceStripButton* Btn : { Row.Btn_Threat, Row.Btn_Int, Row.Btn_Ack })
			{
				if (!Btn) { continue; }
				TSharedPtr<SWidget> Slate = Btn->GetCachedWidget();
				if (!Slate.IsValid()) { continue; }
				for (int32 i = 0; i < Path.Widgets.Num(); ++i)
				{
					if (Path.Widgets[i].Widget == Slate)
					{
						return Btn;
					}
				}
			}
		}
		return nullptr;
	};

	auto ApplyStripHover = [this](TWeakObjectPtr<UClearanceStripButton>& Prev, UClearanceStripButton* Now)
	{
		if (Prev.Get() == Now) { return; }
		auto TintBrush = [](UClearanceStripButton* Btn, const FLinearColor& Color)
		{
			if (!Btn) { return; }
			FButtonStyle S = Btn->GetStyle();
			S.Normal.TintColor = FSlateColor(Color);
			Btn->SetStyle(S);
		};
		TintBrush(Prev.Get(), FLinearColor(0.20f, 0.30f, 0.40f, 1.f));  // revert to navy
		TintBrush(Now,        FLinearColor(1.00f, 0.65f, 0.00f, 1.f));  // amber hover
		Prev = Now;
	};

	static TWeakObjectPtr<UClearanceStripButton> LastLeftHover;
	static TWeakObjectPtr<UClearanceStripButton> LastRightHover;
	ApplyStripHover(LastLeftHover,  ResolveHoveredStripButton(LeftFingertipInteraction));
	ApplyStripHover(LastRightHover, ResolveHoveredStripButton(RightFingertipInteraction));

	// Highlight the selected row from C++. The UWidgetComponent path
	// ticks its child UUserWidget the normal way, so per-frame style
	// mutations reach the SButton paint via the standard Slate
	// invalidation graph. - TripleA
	if (PauseMenuInstance && PauseMenuWidgetComp && PauseMenuWidgetComp->GetWidget())
	{
		// Highlight the selected row directly from C++ by rewriting each
		// button's WidgetStyle every frame. BP Event Tick + SetColorAndOpacity
		// / SetBackgroundColor on UButton was not showing through in the
		// stereo-layer render path because the buttons' custom WidgetStyle
		// overrides the runtime tint multiplier. SetStyle replaces the whole
		// FButtonStyle struct, so a fresh Normal/Hovered/Pressed TintColor
		// wins outright and the widget's authored padding / brush / border
		// settings are preserved via the GetStyle() copy. - TripleA
		// Button names differ per screen. Pause menu + options have four
		// rows; end-session report has two (Restart / Exit). Highlight
		// logic below iterates GetCurrentScreenOptionCount so the same
		// loop drives all three screens. - TripleA
		static const FName PauseMenuButtonNames[] = {
			TEXT("Btn_Resume"),
			TEXT("Btn_Options"),
			TEXT("Btn_EndSession"),
			TEXT("Btn_ExitToMenu")
		};
		static const FName OptionsButtonNames[] = {
			TEXT("Btn_Recenter"),
			TEXT("Btn_SnapTurn"),
			TEXT("Btn_RadioVolume"),
			TEXT("Btn_Back")
		};
		static const FName ReportButtonNames[] = {
			TEXT("Btn_Restart"),
			TEXT("Btn_ReportExit"),
			NAME_None,
			NAME_None
		};
		const FName* ButtonNames = nullptr;
		switch (CurrentScreen)
		{
		case EPauseScreen::Options:          ButtonNames = OptionsButtonNames;   break;
		case EPauseScreen::EndSessionReport: ButtonNames = ReportButtonNames;    break;
		case EPauseScreen::PauseMenu:
		default:                             ButtonNames = PauseMenuButtonNames; break;
		}
		// Regular selected background = amber (ATC caution) - contrasts
		// with cyan button text. Destructive rows (END SESSION + EXIT
		// TO MENU) use a deep crimson so their bright-red text stays
		// readable on top - bright-red-on-bright-red collapses. - TripleA
		const FLinearColor SelectedTint          (1.00f, 0.65f, 0.00f, 1.f);
		const FLinearColor DestructiveSelectedTint(0.40f, 0.05f, 0.05f, 1.f);
		const FLinearColor DimTint               (0.08f, 0.15f, 0.20f, 1.f);

		const int32 NumButtons = GetCurrentScreenOptionCount();
		for (int32 i = 0; i < NumButtons; ++i)
		{
			UButton* Btn = Cast<UButton>(PauseMenuInstance->GetWidgetFromName(ButtonNames[i]));
			if (!Btn) { continue; }

			// Only the pause menu has destructive rows (End Session, Exit).
			// Options screen rows are all non-destructive - even Back just
			// pops the menu state, doesn't destroy anything. - TripleA
			const bool bSelected    = (i == PauseMenuSelectedIndex);
			const bool bDestructive = (CurrentScreen == EPauseScreen::PauseMenu) && (i >= 2);
			const FLinearColor Tint = bSelected
				? (bDestructive ? DestructiveSelectedTint : SelectedTint)
				: DimTint;

			// Nuclear tint: rebuild each brush from scratch as a plain box
			// with our tint, no source texture / material. The widget's
			// authored brushes may have a dark-baked resource that would multiply
			// our TintColor down to invisible; clearing ResourceObject
			// forces the brush to draw as a pure solid-colour box using
			// only TintColor. Loses any rounded-corner / custom-image
			// styling, but a flat coloured highlight is the goal here
			// anyway. Padding / hover behaviour / press behaviour from
			// GetStyle() are preserved. - TripleA
			FButtonStyle NewStyle = Btn->GetStyle();
			auto Retint = [Tint](FSlateBrush& B)
			{
				B.DrawAs    = ESlateBrushDrawType::Box;
				B.SetResourceObject(nullptr);
				B.TintColor = FSlateColor(Tint);
			};
			Retint(NewStyle.Normal);
			Retint(NewStyle.Hovered);
			Retint(NewStyle.Pressed);
			Btn->SetStyle(NewStyle);

			// Belt-and-braces: also poke the whole-button ColorAndOpacity
			// multiplier white and the deprecated BackgroundColor to white,
			// so any style-multiplier path that could tint our clean
			// brush a second time stays neutral. - TripleA
			Btn->SetColorAndOpacity(FLinearColor::White);
			Btn->SetBackgroundColor(FLinearColor::White);

			// (Scale + visibility diagnostics removed - proven the
			// paint pipeline updates now that the stereo layer is
			// marked bLiveTexture + MarkTextureForUpdate every frame.
			// The SetStyle tint above is now the sole highlight
			// mechanism.) - TripleA

			// Style mutation alone marks the widget dirty. The
			// WidgetComponent's own tick + Slate application invalidation
			// pipeline handle the actual repaint, so no manual Prepass /
			// DrawWidget calls are needed here. - TripleA
			Btn->InvalidateLayoutAndVolatility();
		}

		// Push end-session report text straight to the widget's TextBlocks
		// by name, instead of relying on a BP property binding on the
		// Text field. The direct-push path makes C++ the single source
		// of truth for these values and avoids the paint refresh
		// depending on Slate binding re-evaluation timing under the
		// stereo-layer render path. The TextBlocks can be left with no
		// binding at all - we overwrite the text every frame while the
		// report screen is up. - TripleA
		if (CurrentScreen == EPauseScreen::EndSessionReport)
		{
			auto SetText = [this](const TCHAR* Name, const FString& Value)
			{
				if (UTextBlock* T = Cast<UTextBlock>(PauseMenuInstance->GetWidgetFromName(FName(Name))))
				{
					T->SetText(FText::FromString(Value));
				}
			};

			SetText(TEXT("Text_Score"),
				FString::Printf(TEXT("SCORE: %d"), EndSessionReportScore));

			const int32 Mins = FMath::FloorToInt(EndSessionReportSessionSeconds / 60.f);
			const int32 Secs = FMath::FloorToInt(EndSessionReportSessionSeconds - Mins * 60.f);
			SetText(TEXT("Text_Time"),
				FString::Printf(TEXT("TIME: %d:%02d"), Mins, Secs));

			SetText(TEXT("Text_AARPath"),
				FString::Printf(TEXT("AAR: %s"), *EndSessionReportPath));
		}

		// Keep the stereo layer's texture pointer live in case the
		// WidgetComponent recreated its render target (happens if
		// SetDrawSize changed or the widget was rebuilt). Cheap no-op
		// when the pointer is stable. - TripleA
		if (PauseMenuStereoLayer)
		{
			UTextureRenderTarget2D* RT = PauseMenuWidgetComp->GetRenderTarget();
			if (RT && PauseMenuStereoLayer->GetTexture() != RT)
			{
				PauseMenuStereoLayer->SetTexture(RT);
			}

			// Force the HMD compositor to re-sample the RT every frame.
			// Without this the stereo layer holds onto its first-frame
			// snapshot of the texture and never picks up subsequent
			// widget updates - even though the RT itself IS being
			// redrawn correctly by the WidgetComponent. - TripleA
			if (PauseMenuStereoLayer->bLiveTexture == false)
			{
				PauseMenuStereoLayer->bLiveTexture = true;
			}
			PauseMenuStereoLayer->MarkTextureForUpdate();
		}
	}

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
	struct FTriggerEdgeState
	{
		bool  bLeftDown         = false;
		bool  bRightDown        = false;
		float PauseGripHoldSecs = 0.f;   // accumulator for left-grip long-press
		bool  bPauseMenuFired   = false; // set true after the hold-fire event to prevent auto-repeat
		bool  bMenuScrollReady  = true;  // re-arm gate for right-stick menu scroll
	};
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
		if (bRightNow)
		{
			// Right trigger PRESS while the pause menu is up = CONFIRM the
			// currently-highlighted option instead of firing the hovered
			// console button. Console-button dispatch is suppressed for the
			// press edge; release edge still runs so any button that was
			// already held stays consistent (rare - the menu is modal and
			// suppresses press below). - TripleA
			if (IsPauseMenuShowing())
			{
				OnPauseMenuConfirm(PauseMenuSelectedIndex);
			}
			else
			{
				HandleTriggerRightPressed(Dummy);
			}
		}
		else
		{
			HandleTriggerRightReleased(Dummy);
		}
	}

	// Right-stick Y drives menu scroll while the pause menu is showing.
	// SnapTurnAction is an Axis2D on the right thumbstick - Y drives
	// vertical option selection (up = previous, down = next), and
	// HandleSnapTurn early-outs while the menu is up so the player doesn't
	// spin at the same time. Same threshold + re-arm pattern as snap
	// turning: cross threshold once to tick, release to centre to re-arm. - TripleA
	if (IsPauseMenuShowing() && SnapTurnAction)
	{
		float StickY = 0.f;
		if (ULocalPlayer* LPS = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* SubS = LPS->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (UEnhancedPlayerInput* EPIS = SubS->GetPlayerInput())
				{
					StickY = EPIS->GetActionValue(SnapTurnAction).Get<FVector2D>().Y;
				}
			}
		}

		if (FMath::Abs(StickY) < SnapTurnThreshold)
		{
			Edge.bMenuScrollReady = true;
		}
		else if (Edge.bMenuScrollReady && GetCurrentScreenOptionCount() > 0)
		{
			Edge.bMenuScrollReady = false;
			// Stick UP (positive Y) = previous option, DOWN = next option.
			// If it feels inverted in the headset just flip the sign here. - TripleA
			const int32 Count = GetCurrentScreenOptionCount();
			const int32 Dir = (StickY > 0.f) ? -1 : +1;
			PauseMenuSelectedIndex = (PauseMenuSelectedIndex + Dir + Count) % Count;
		}
	}

	// Pause menu = LEFT-GRIP LONG-PRESS (~1s). Read via the SAME EI
	// GetActionValue<float> path the triggers use above - that path is
	// the only input read pattern confirmed working in this project's
	// OpenXR / EI setup. Legacy GetInputAnalogKeyState and IsInputKeyDown
	// both silently return zero for OpenXR-routed inputs. Grip axis is
	// analog so Value Type = Axis1D on the IA gives a 0-1 float via
	// Get<float>. Grip is unused elsewhere in CLEARANCE (no grabbables),
	// so a deliberate 1s hold is a clean menu-commit gesture with no
	// accidental-fire path. - TripleA
	if (GripLeftAction)
	{
		constexpr float kGripHeldThreshold = 0.90f;
		constexpr float kHoldSecondsToFire = 1.0f;

		float GripVal = 0.f;
		if (ULocalPlayer* LPG = PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* SubG = LPG->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (UEnhancedPlayerInput* EPIG = SubG->GetPlayerInput())
				{
					GripVal = EPIG->GetActionValue(GripLeftAction).Get<float>();
				}
			}
		}
		const bool bGripDown = GripVal >= kGripHeldThreshold;

		if (bGripDown)
		{
			Edge.PauseGripHoldSecs += DeltaSeconds;
			if (!Edge.bPauseMenuFired && Edge.PauseGripHoldSecs >= kHoldSecondsToFire)
			{
				Edge.bPauseMenuFired = true;
				HandlePauseMenuPressed(Dummy);
			}
		}
		else
		{
			Edge.PauseGripHoldSecs = 0.f;
			Edge.bPauseMenuFired   = false;
		}
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
