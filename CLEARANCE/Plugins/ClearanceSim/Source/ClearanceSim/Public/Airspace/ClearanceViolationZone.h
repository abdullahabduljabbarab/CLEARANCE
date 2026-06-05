#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClearanceViolationZone.generated.h"

class USceneComponent;

// Drop one (or more) in the level to mark protected airspace - a city, an
// airbase, a nuclear site. A declared-hostile aircraft entering the radius
// triggers a catastrophic incident (mirror of misidentification). The actor
// location is the zone centre; radius is in nautical miles. The Simulation
// Controller discovers all of these. - TripleA
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceViolationZone : public AActor
{
	GENERATED_BODY()

public:
	AClearanceViolationZone();

	// Operator-facing label ("DOWNTOWN", "AIRBASE BRAVO"). Shown in incident
	// log + on-screen banner when breached.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Violation Zone")
	FName ZoneName = TEXT("PROTECTED");

	// Horizontal radius in nautical miles. 2D check - no altitude ceiling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Violation Zone")
	float RadiusNm = 5.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Violation Zone")
	TObjectPtr<USceneComponent> Root;
};
