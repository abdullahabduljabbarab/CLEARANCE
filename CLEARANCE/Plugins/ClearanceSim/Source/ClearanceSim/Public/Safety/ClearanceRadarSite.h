#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClearanceRadarSite.generated.h"

class USceneComponent;
class UClearanceRadar;
class AClearanceAirspaceManager;
class AClearanceSimulationController;

// Placeable radar site. Drop one (or more) per radar in the level - airport, a
// remote ATC site, a ship, an AWACS station. Each owns its own UClearanceRadar
// with independent range / sweep / position. The Simulation Controller polls
// every placed site each tick and fuses their tracks into one operator picture.
// Site position in the sim = actor location relative to the Controller. - TripleA
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceRadarSite : public AActor
{
	GENERATED_BODY()

public:
	AClearanceRadarSite();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Radar Site")
	TObjectPtr<USceneComponent> Root;

	// The radar instance this site owns. Tuning is editable on it directly.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Radar Site")
	TObjectPtr<UClearanceRadar> Radar;

	// Human label for this site - appears in fused track confidence tags. Falls
	// through to the radar's SiteName if not set on the actor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar Site")
	FName SiteName = TEXT("SITE");

	// Tuning forwarded to the owned radar at BeginPlay - exposed here so a
	// level designer can configure the site without drilling into the subobject.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar Site|Tuning")
	float RangeNm = 60.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar Site|Tuning")
	float SweepRpm = 12.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar Site|Tuning")
	float SecondaryReturnChance = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar Site|Tuning")
	float PositionJitterNm = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar Site|Tuning")
	float TrackFadeSeconds = 8.f;

	// Display colour for the site's coverage ring on the debug view.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar Site|Visual")
	FColor CoverageColour = FColor(80, 180, 220);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
};
