// Loads, advances, and fires a scripted training scenario. The scenario file is
// the unit of authoring; the runner is the unit of execution. Owned by the
// Simulation Controller, ticked from the same loop. Pure backend - no UI. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Scenario/ClearanceScenarioTypes.h"
#include "ClearanceScenarioRunner.generated.h"

class AClearanceSimulationController;
class AClearanceAirspaceManager;

UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceScenarioRunner : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Scenario")
	void SetReferences(AClearanceSimulationController* InController, AClearanceAirspaceManager* InManager);

	// Parse a JSON file from disk into the in-memory scenario. Does not start it. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Scenario")
	bool LoadFromFile(const FString& AbsolutePath, FString& OutError);

	// Apply environment, fire initial spawns, arm timed events + triggers, start the clock.
	UFUNCTION(BlueprintCallable, Category = "Scenario")
	void Start();

	UFUNCTION(BlueprintCallable, Category = "Scenario")
	void Stop();

	UFUNCTION(BlueprintCallable, Category = "Scenario")
	bool IsRunning() const { return bRunning; }

	// Wall-clock seconds since StartScenario, used for display ("T+02:30").
	// Unaffected by SimulationTimeScale so the displayed counter matches the
	// instructor's wristwatch. Trigger evaluations use ElapsedSec (scaled)
	// internally - authored event timestamps stay in scenario-seconds. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Scenario")
	float GetElapsedSeconds() const { return WallClockElapsedSec; }

	UFUNCTION(BlueprintCallable, Category = "Scenario")
	float GetScenarioSeconds() const { return ElapsedSec; }

	UFUNCTION(BlueprintCallable, Category = "Scenario")
	FName GetLoadedId() const { return Scenario.Metadata.Id; }

	UFUNCTION(BlueprintCallable, Category = "Scenario")
	FString GetLoadedName() const { return Scenario.Metadata.Name; }

	UFUNCTION(BlueprintCallable, Category = "Scenario")
	int32 GetFiredEventCount() const { return FiredEvents; }

	UFUNCTION(BlueprintCallable, Category = "Scenario")
	int32 GetFiredTriggerCount() const { return FiredTriggers; }

	UFUNCTION(BlueprintCallable, Category = "Scenario")
	int32 GetTotalEventCount() const { return Scenario.TimedEvents.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Scenario")
	int32 GetTotalTriggerCount() const { return Scenario.Triggers.Num(); }

	// Boolean flag store the scenario can set via SetFlag action and read via FlagSet condition.
	// Lets the JSON author build small state machines without touching aircraft state. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Scenario")
	bool IsFlagSet(FName FlagName) const { return Flags.Contains(FlagName); }

	// Advance scenario time. SimDeltaSeconds drives event-trigger comparisons
	// (so a 10x speed session reaches scripted moments 10x faster as authored).
	// WallClockDeltaSeconds drives the displayed counter so the instructor's
	// "T+mm:ss" matches their wristwatch regardless of speed. - TripleA
	void Tick(float SimDeltaSeconds, float WallClockDeltaSeconds);

private:
	UPROPERTY()
	TObjectPtr<AClearanceSimulationController> Controller;

	UPROPERTY()
	TObjectPtr<AClearanceAirspaceManager> Manager;

	UPROPERTY()
	FClearanceScenario Scenario;

	UPROPERTY()
	TSet<FName> Flags;

	// Active pursuits: hunter callsign -> target callsign. Each tick we steer the
	// hunter onto a vector toward the target. Lets the scenario script chase
	// behaviour without an AI subsystem. - TripleA
	UPROPERTY()
	TMap<FName, FName> ActivePursuits;

	bool bRunning = false;
	float ElapsedSec = 0.f;          // scenario time (sim-scaled) - drives trigger evaluation
	float WallClockElapsedSec = 0.f; // wall-clock seconds since StartScenario - drives display
	int32 FiredEvents = 0;
	int32 FiredTriggers = 0;

	// The scenario lock on the spawner is independent of bAutoSpawn, so we no
	// longer need to save/restore the free-play preference - it stays untouched. - TripleA

	void UpdatePursuits();

	void ApplyEnvironment();
	void FireInitialSpawns();
	void EvaluateTimedEvents();
	void EvaluateTriggers();
	bool EvaluateCondition(const FScenarioCondition& Cond) const;
	void ExecuteAction(const FScenarioAction& Act);
};
