#include "Airspace/ClearanceViolationZone.h"
#include "Components/SceneComponent.h"

AClearanceViolationZone::AClearanceViolationZone()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);
}
