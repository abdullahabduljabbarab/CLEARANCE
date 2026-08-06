#include "Simulation/ClearanceOperatorButton.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

// Debug prints on the whole console-button chain. Set to 0 to silence
// once the buttons work in VR; leaving them on has essentially zero
// perf cost (only fires on human-timescale press events). - TripleA
#ifndef CLEARANCE_LOG_OPERATOR_BUTTONS
#define CLEARANCE_LOG_OPERATOR_BUTTONS 1
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
}

void AClearanceOperatorButton::HandlePress(APawn* PressingPawn)
{
	const uint8 KindByte = static_cast<uint8>(Kind);
	ClearanceButtonDebug(1000 + GetUniqueID(), FColor::Green,
		FString::Printf(TEXT("Button PRESSED: %s (Kind=%d)"),
			*GetName(), KindByte));

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

void AClearanceOperatorButton::HandleRelease(APawn* PressingPawn)
{
	OnButtonReleased.Broadcast(this);

	// Only press-and-hold kinds care about the release edge. Momentary
	// buttons already fired their action on press. - TripleA
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
