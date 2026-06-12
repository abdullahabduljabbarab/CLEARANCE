#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClearanceWaypoint.generated.h"

class USceneComponent;

// A named waypoint / fix on the airway grid. Drop these in the level wherever
// you want a navigation point - they show up on the instructor scope as a small
// triangle + label. Connect them to other waypoints via ConnectedWaypoints to
// draw an airway between them. The connection is undirected; each pair only
// draws once even if both ends list each other. - TripleA
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceWaypoint : public AActor
{
	GENERATED_BODY()

public:
	AClearanceWaypoint();

	// Identifier shown on the scope. Real ATC fixes are 5-letter
	// pronounceable names (KEGUM, ALLER, BUFFY). - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint")
	FName Name = TEXT("WPT");

	// Names of other AClearanceWaypoint actors this one is connected to via an
	// airway. Each direction is fine to specify; duplicates are de-duped at
	// render. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint")
	TArray<FName> ConnectedWaypoints;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waypoint")
	TObjectPtr<USceneComponent> Root;
};
