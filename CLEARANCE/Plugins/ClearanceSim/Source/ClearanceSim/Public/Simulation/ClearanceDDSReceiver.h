// Unreal-side ingest for DDS aircraft telemetry from peer federates.
// Sibling to UClearanceDISReceiver - subscribes to the DDS
// clearance/aircraft/state topic, filters out its own SiteId+ApplicationId
// (loopback protection so we don't ingest our own aircraft), converts each
// received IDL POD sample back into CLEARANCE-native units, and registers
// the aircraft with the AirspaceManager as external (bIsExternal=true,
// not commandable, no local Behaviour). Matches DIS receiver behaviour so
// a peer federate's aircraft appear identically on the scope whether they
// arrived over DIS or DDS. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceDDS/ClearanceDDSSubscriber.h"
#include "ClearanceDDSReceiver.generated.h"

class AClearanceAirspaceManager;

UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceDDSReceiver : public UObject
{
	GENERATED_BODY()

public:
	UClearanceDDSReceiver();
	virtual ~UClearanceDDSReceiver();

	// Start subscribing on the given DDS domain (default 0). Returns false on
	// participant failure. Aircraft samples from peers on the same domain
	// start arriving immediately.
	UFUNCTION(BlueprintCallable, Category = "DDS")
	bool Start(int32 DomainId = 0);

	UFUNCTION(BlueprintCallable, Category = "DDS")
	void Stop();

	UFUNCTION(BlueprintCallable, Category = "DDS")
	bool IsRunning() const;

	// Drain the queue of samples received since the last tick and register
	// them with the AirspaceManager. Called from the SimulationController
	// tick so all AirspaceManager mutation happens on the game thread. - TripleA
	void TickDrain(AClearanceAirspaceManager* Manager, float WorldTimeSeconds);

	// Loopback identity - matches the emitter's SiteId + ApplicationId.
	// The SimulationController syncs these on Start so our own publications
	// don't get re-ingested. - TripleA
	UPROPERTY(BlueprintReadWrite, Category = "DDS|Identity")
	int32 LocalSiteId = 1;

	UPROPERTY(BlueprintReadWrite, Category = "DDS|Identity")
	int32 LocalApplicationId = 1;

	UFUNCTION(BlueprintPure, Category = "DDS")
	int32 GetTotalIngestedCount() const;

private:
	// Underlying pure-C++ subscriber. Owns the DomainParticipant + 6 readers.
	TUniquePtr<ClearanceDDS::FClearanceSubscriber> Subscriber;

	// Queue of AircraftState samples received on the transport thread,
	// drained on the game thread. Mutex-guarded because DDS on_data_available
	// callbacks fire on a Fast DDS worker thread that is NOT the UE game
	// thread - we cannot touch UObjects from there. - TripleA
	FCriticalSection QueueMutex;
	TArray<ClearanceDDS::AircraftState> PendingAircraft;

	// Last-seen tracker per callsign so we know whether to Register or
	// RequestStateUpdate on the AirspaceManager - identical to how the DIS
	// receiver handles it. - TripleA
	TMap<FName, float> LastSeenSeconds;

	int32 IngestedCount = 0;
};
