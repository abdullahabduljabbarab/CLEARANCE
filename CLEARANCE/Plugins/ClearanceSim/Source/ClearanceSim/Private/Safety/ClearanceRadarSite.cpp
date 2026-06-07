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
	TryWireUp(); // first attempt - may fail if Controller hasn't BeginPlay'd yet
}

bool AClearanceRadarSite::TryWireUp()
{
	if (!Radar || Radar->IsEnabled()) { return Radar && Radar->IsEnabled(); }

	// Actor BeginPlay order is non-deterministic. If a site BeginPlays before the
	// Controller has spun up its airspace manager, GetAirspaceManager() is null
	// and we can't wire up yet - retry from Tick until both are alive. - TripleA
	AClearanceSimulationController* Controller = nullptr;
	for (TActorIterator<AClearanceSimulationController> It(GetWorld()); It; ++It) { Controller = *It; break; }
	if (!Controller) { return false; }

	AClearanceAirspaceManager* Manager = Controller->GetAirspaceManager();
	if (!Manager) { return false; }

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
	return true;
}

void AClearanceRadarSite::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Radar) { return; }
	if (!Radar->IsEnabled()) { TryWireUp(); return; }
	Radar->Tick(DeltaSeconds);
}
