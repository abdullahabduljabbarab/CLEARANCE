#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClearanceRestrictedArea.generated.h"

class USceneComponent;

// Drop one (or more) in the level to mark restricted airspace - military
// training zones, prohibited (P-area) sites, nuclear plants, etc. CIVILIAN
// aircraft entering trigger a RestrictedAirspaceBust incident. Distinct from
// AClearanceViolationZone (which is for HOSTILES reaching a critical target).
// The controller's job is vectoring civilians around these. - TripleA
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceRestrictedArea : public AActor
{
	GENERATED_BODY()

public:
	AClearanceRestrictedArea();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Restricted Area")
	FName AreaName = TEXT("RESTRICTED");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Restricted Area")
	float RadiusNm = 8.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Restricted Area")
	TObjectPtr<USceneComponent> Root;
};
