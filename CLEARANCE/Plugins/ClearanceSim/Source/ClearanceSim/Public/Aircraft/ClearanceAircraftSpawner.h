#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceAircraftSpawner.generated.h"

class AClearanceAirspaceManager;

// Feeds aircraft into the sector at the difficulty-driven interval. It only
// controls entry - once an aircraft is registered, the Behaviour flies it. The
// Controller drives the timing so spawn order stays deterministic. - TripleA
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceAircraftSpawner : public AActor
{
	GENERATED_BODY()

public:
	AClearanceAircraftSpawner();

	UFUNCTION(BlueprintCallable, Category = "Spawner")
	void SetReferences(AClearanceAirspaceManager* InManager);

	// Called by the Simulation Controller each tick (step 1 of the pipeline).
	UFUNCTION(BlueprintCallable, Category = "Spawner")
	void TickSpawning(float DeltaTime);

	// Spawn one now (also the manual/test entry point). False if at cap or unwired.
	UFUNCTION(BlueprintCallable, Category = "Spawner")
	bool SpawnAircraft();

	UFUNCTION(BlueprintCallable, Category = "Spawner")
	void SetSpawnInterval(float Seconds);

	UFUNCTION(BlueprintCallable, Category = "Spawner")
	void SetAutoSpawn(bool bEnabled);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Settings")
	int32 MaxConcurrentAircraft = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Settings")
	float EntryRadiusNm = 40.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Settings")
	float MinEntryAltitudeFt = 6000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawner|Settings")
	float MaxEntryAltitudeFt = 35000.f;

private:
	UPROPERTY()
	TObjectPtr<AClearanceAirspaceManager> Manager;

	bool bAutoSpawn = true;
	float CurrentSpawnIntervalSeconds = 30.f;
	float SpawnTimer = 0.f;
	int32 CallsignCounter = 0;

	FAircraftSpawnData GenerateSpawnData();
	FName GenerateCallsign();
	EWakeCategory RandomCategory() const;
};
