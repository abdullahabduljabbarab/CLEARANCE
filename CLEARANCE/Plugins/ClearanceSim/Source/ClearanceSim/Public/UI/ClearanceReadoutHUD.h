// HUD that pulls the latest readout text from the local SimulationController
// and draws it via Canvas. Each PlayerController gets its own HUD instance, so
// this renders per-viewport reliably in PIE multi-window - unlike GEngine's
// on-screen-debug queue which the PIE renderer was eating on the client side. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ClearanceReadoutHUD.generated.h"

class AClearanceSimulationController;

UCLASS()
class CLEARANCESIM_API AClearanceReadoutHUD : public AHUD
{
    GENERATED_BODY()

public:
    virtual void DrawHUD() override;

private:
    // Cached so we don't TActorIterator every frame. Refresh if it goes stale. - TripleA
    UPROPERTY()
    TObjectPtr<AClearanceSimulationController> CachedController;

    AClearanceSimulationController* FindController();
};
