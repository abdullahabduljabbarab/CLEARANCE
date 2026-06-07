#include "Safety/ClearanceRadarSite.h"
#include "Safety/ClearanceRadar.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Simulation/ClearanceSimulationController.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"

AClearanceRadarSite::AClearanceRadarSite()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Root->SetMobility(EComponentMobility::Movable);
	Radar = CreateDefaultSubobject<UClearanceRadar>(TEXT("Radar"));
}

void AClearanceRadarSite::BeginPlay()
{
	Super::BeginPlay();
	if (!Radar) { return; }

	// Find the Controller to get the airspace manager + sector origin so we can
	// translate this actor's world location into sim-nm coordinates. - TripleA
	AClearanceSimulationController* Controller = nullptr;
	for (TActorIterator<AClearanceSimulationController> It(GetWorld()); It; ++It) { Controller = *It; break; }
	if (!Controller) { return; }

	AClearanceAirspaceManager* Manager = Controller->GetAirspaceManager();
	if (!Manager) { return; }

	const FVector OriginW = Controller->GetActorLocation();
	const float W = FMath::Max(1.f, Controller->WorldUnitsPerNm);
	const FVector SiteW = GetActorLocation();
	const FVector2D SiteNm((SiteW.X - OriginW.X) / W, (SiteW.Y - OriginW.Y) / W);

	Radar->SetReferences(Manager);
	Radar->RangeNm = RangeNm;
	Radar->SweepRpm = SweepRpm;
	Radar->SecondaryReturnChance = SecondaryReturnChance;
	Radar->PositionJitterNm = PositionJitterNm;
	Radar->TrackFadeSeconds = TrackFadeSeconds;
	Radar->SitePositionNm = SiteNm;
	Radar->SiteName = SiteName;
	Radar->SetEnabled(true);
}

void AClearanceRadarSite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Radar && Radar->IsEnabled())
	{
		Radar->Tick(DeltaSeconds);
	}
}
