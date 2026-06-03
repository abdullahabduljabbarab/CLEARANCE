#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClearanceRunway.generated.h"

class UStaticMeshComponent;
class USceneComponent;

// Drop one (or more) of these in the level to define real runways. Everything the
// sim needs - the strip's centre, its length and the ground height - is read from
// the RunwayMesh's bounds, so you just place and scale the mesh and the touchdown
// points follow it. LandingHeadingDeg is one of the two directions flown to land;
// with bAllowReciprocal on, the strip also offers the opposite direction
// (heading + 180), so traffic can land either way and the Airspace Manager picks
// whichever end is into-wind. The Simulation Controller discovers these. - TripleA
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceRunway : public AActor
{
	GENERATED_BODY()

public:
	AClearanceRunway();

	// One of the two compass headings aircraft fly to land here (0=N, 90=E). The
	// reciprocal (this + 180) is the other direction.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runway")
	float LandingHeadingDeg = 270.f;

	// Let traffic land from either end. Off = this strip is one-way (LandingHeadingDeg only).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runway")
	bool bAllowReciprocal = true;

	// Use these to size the runway when the actor has no mesh of its own (the visual
	// is somewhere else in the level). When > 0 they override any mesh bounds, so the
	// strip is whatever you type even if a mesh IS attached. Length is along the
	// heading; width is perpendicular. World units (UE cm in a stock project). - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runway")
	float OverrideLengthUnits = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runway")
	float OverrideWidthUnits = 0.f;

	// World-space centre + half-extents of the runway, computed from the mesh ASSET
	// bounds and the mesh's current transform - reliable even at BeginPlay, unlike the
	// cached component Bounds which can read stale/zero. Returns false if no mesh is
	// assigned. Both the sim and the debug draw go through this so they always agree.
	bool GetRunwayBounds(FVector& OutCentre, FVector& OutExtent) const;

	// The touchdown threshold / runway zone. This is the actor's root, so the
	// actor's location is the threshold the sim reads. Don't parent the mesh to
	// the actor - it hangs off this, so it can be offset independently.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runway")
	TObjectPtr<USceneComponent> Threshold;

	// Visual only - reposition this relative to the Threshold without moving the
	// touchdown point.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Runway")
	TObjectPtr<UStaticMeshComponent> RunwayMesh;
};
