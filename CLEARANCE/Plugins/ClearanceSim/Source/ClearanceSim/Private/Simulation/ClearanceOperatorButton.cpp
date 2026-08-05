#include "Simulation/ClearanceOperatorButton.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "Components/BoxComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"

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
	OnButtonPressed.Broadcast(this);

	if (!PressingPawn) { return; }
	AClearanceOperatorPC* PC = Cast<AClearanceOperatorPC>(PressingPawn->GetController());
	if (!PC) { return; }

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
