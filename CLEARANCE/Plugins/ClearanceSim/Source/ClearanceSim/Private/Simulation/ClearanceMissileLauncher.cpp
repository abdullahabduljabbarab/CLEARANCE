#include "Simulation/ClearanceMissileLauncher.h"
#include "Components/StaticMeshComponent.h"

AClearanceMissileLauncher::AClearanceMissileLauncher()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("RootComp"));
	SetRootComponent(Root);

	LauncherMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LauncherMesh"));
	LauncherMesh->SetupAttachment(Root);
	LauncherMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LauncherMesh->SetMobility(EComponentMobility::Static);
}

FVector AClearanceMissileLauncher::GetMuzzleWorldLocation() const
{
	return GetActorTransform().TransformPosition(MuzzleOffsetCm);
}
