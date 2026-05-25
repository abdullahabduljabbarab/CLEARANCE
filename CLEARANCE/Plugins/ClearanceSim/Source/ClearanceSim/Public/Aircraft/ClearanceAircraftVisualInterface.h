#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ClearanceAircraftVisualInterface.generated.h"

UINTERFACE(BlueprintType)
class CLEARANCESIM_API UClearanceAircraftVisualInterface : public UInterface
{
	GENERATED_BODY()
};

// Add this interface to an aircraft Blueprint to let the sim drive its moving
// parts. The Simulation Controller calls UpdateAircraftVisual every tick on any
// spawned visual that implements it - the C++ works out the gear/engine state from
// the flight model, the Blueprint just animates to match. - TripleA
class CLEARANCESIM_API IClearanceAircraftVisualInterface
{
	GENERATED_BODY()

public:
	// Implement in the aircraft Blueprint:
	//   bGearDeployed  - true on approach/landing/just after takeoff, false in cruise.
	//                    Deploy or retract the landing gear to match.
	//   EngineThrottle - 0..1, higher when climbing, lower descending, never 0 in the
	//                    air. Spin the prop/turbine at a speed scaled by this.
	//   bOnGround      - true once it's on the runway (idle the engine, gear down).
	UFUNCTION(BlueprintImplementableEvent, BlueprintCallable, Category = "Clearance|Aircraft Visual")
	void UpdateAircraftVisual(bool bGearDeployed, float EngineThrottle, bool bOnGround);
};
