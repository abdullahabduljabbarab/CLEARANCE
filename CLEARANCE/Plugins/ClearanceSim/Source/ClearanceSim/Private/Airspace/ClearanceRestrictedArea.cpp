#include "Airspace/ClearanceRestrictedArea.h"
#include "Components/SceneComponent.h"

AClearanceRestrictedArea::AClearanceRestrictedArea()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);
}
