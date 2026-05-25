#include "Airspace/ClearanceRunway.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"

AClearanceRunway::AClearanceRunway()
{
	PrimaryActorTick.bCanEverTick = false;

	// The threshold is the root, so the actor's location stays the touchdown point
	// no matter where the mesh is dragged. The mesh is a child for visuals only.
	Threshold = CreateDefaultSubobject<USceneComponent>(TEXT("Threshold"));
	RootComponent = Threshold;

	RunwayMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RunwayMesh"));
	RunwayMesh->SetupAttachment(Threshold);
	RunwayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}
