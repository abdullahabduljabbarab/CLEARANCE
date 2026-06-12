#include "Airspace/ClearanceWaypoint.h"
#include "Components/SceneComponent.h"

AClearanceWaypoint::AClearanceWaypoint()
{
	PrimaryActorTick.bCanEverTick = false;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}
