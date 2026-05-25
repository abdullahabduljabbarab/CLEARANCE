#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClearanceRunway.generated.h"

class UStaticMeshComponent;
class USceneComponent;

// Drop one (or more) of these in the level to define real runways. The actor's
// location (the Threshold root) is the CENTRE of the strip; LandingHeadingDeg is
// one of the two directions flown to land. With bAllowReciprocal on, the strip
// also offers the opposite direction (heading + 180) from the far end, so traffic
// can land either way - the Airspace Manager picks whichever end is into-wind.
// The Simulation Controller discovers these. RunwayMesh is purely the visual -
// move it about freely to line the mesh up without dragging the strip. - TripleA
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

	// Physical strip length. Sets how far apart the two thresholds sit either side
	// of the actor's centre - so each end's touchdown point lands on the real mesh.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runway")
	float RunwayLengthMeters = 3000.f;

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
