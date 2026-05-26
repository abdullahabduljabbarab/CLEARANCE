#include "Airspace/ClearanceRunway.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"

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

bool AClearanceRunway::GetRunwayBounds(FVector& OutCentre, FVector& OutExtent) const
{
	if (!RunwayMesh || !RunwayMesh->GetStaticMesh())
	{
		return false;
	}

	// Transform the asset's local AABB by the mesh's current world transform, so the
	// result reflects exactly where the mesh is drawn - no reliance on cached bounds.
	const FBox WorldBox = RunwayMesh->GetStaticMesh()->GetBoundingBox().TransformBy(RunwayMesh->GetComponentTransform());
	OutCentre = WorldBox.GetCenter();
	OutExtent = WorldBox.GetExtent();
	return true;
}
