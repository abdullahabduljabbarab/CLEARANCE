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
	// Explicit override wins. Lets the actor define the strip without holding any
	// mesh of its own - useful when the visual runway is a separate asset placed
	// elsewhere in the level. - TripleA
	if (OverrideLengthUnits > 0.f && OverrideWidthUnits > 0.f)
	{
		// ENU frame: Forward = (X=E=sin, Y=N=cos). Right is Forward rotated
		// 90 clockwise, which in ENU is (cos, -sin). - TripleA
		const float HRad = FMath::DegreesToRadians(LandingHeadingDeg);
		const FVector Forward(FMath::Sin(HRad), FMath::Cos(HRad), 0.f);
		const FVector Right(FMath::Cos(HRad), -FMath::Sin(HRad), 0.f);
		const float HalfLen = OverrideLengthUnits * 0.5f;
		const float HalfWid = OverrideWidthUnits  * 0.5f;
		const FVector Centre = GetActorLocation();

		FBox Box(EForceInit::ForceInit);
		for (int32 i = 0; i < 4; ++i)
		{
			const float sx = (i & 1) ? 1.f : -1.f;
			const float sy = (i & 2) ? 1.f : -1.f;
			Box += Centre + Forward * (HalfLen * sx) + Right * (HalfWid * sy);
		}
		OutCentre = Box.GetCenter();
		OutExtent = Box.GetExtent();
		return true;
	}

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
