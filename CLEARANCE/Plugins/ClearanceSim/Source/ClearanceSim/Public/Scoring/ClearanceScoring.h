#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceScoring.generated.h"

// Keeps the session's tally: logs every outcome, keeps a running score and
// efficiency, and ramps difficulty (spawn interval) as the player handles more
// traffic. Owns session data only - it never touches aircraft state.
UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceScoring : public UObject
{
	GENERATED_BODY()

public:
	UClearanceScoring();

	UPROPERTY(BlueprintAssignable, Category = "Scoring|Events")
	FOnScoreUpdated OnScoreUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Scoring|Events")
	FOnDifficultyAdjusted OnDifficultyAdjusted;

	// The one logger the rest of the sim calls; reward or penalty is decided by
	// the incident type, then score + difficulty update off the back of it. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Scoring")
	void LogIncident(EIncidentType Type, FName AircraftA, FName AircraftB, const FString& Details);

	UFUNCTION(BlueprintCallable, Category = "Scoring")
	void RecordInstruction();

	UFUNCTION(BlueprintCallable, Category = "Scoring")
	int32 GetCurrentScore() const { return CurrentScore; }

	UFUNCTION(BlueprintCallable, Category = "Scoring")
	float GetEfficiency() const;

	UFUNCTION(BlueprintCallable, Category = "Scoring")
	float GetCurrentSpawnInterval() const { return CurrentSpawnIntervalSeconds; }

	UFUNCTION(BlueprintCallable, Category = "Scoring")
	TArray<FIncidentRecord> GetSessionLog() const { return IncidentLog; }

	UFUNCTION(BlueprintCallable, Category = "Scoring")
	void ResetSession();

	// Rewind state to a saved checkpoint snapshot. Restores the score, the
	// incident log, and the per-category tallies derived from it. Spawn-
	// interval difficulty is recomputed from the restored log. Called by
	// AClearanceSimulationController::LoadCheckpoint - the trainee can retry
	// the same scenario without being penalised twice for actions on the
	// first attempt. - TripleA
	void RestoreFromCheckpoint(int32 InScore, const TArray<FIncidentRecord>& InLog);

	// Rewards / penalties (positive = reward).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PointsLanding = 100;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PointsIntercept = 150;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PointsHandoff = 80;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PointsResolution = 50;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltySeparationLoss = 200;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltyGoAround = 50;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltyUnresolvedExit = 100;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltyMissedHandoff = 75;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltyLateInstruction = 25;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltyWakeEncounter = 75;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltyTCASResolutionAdvisory = 100;
	// Catastrophic doctrine failure - declaring a confirmed civilian (IFF on,
	// not military) as hostile. The penalty has to dwarf everything else in the
	// scoring table or the cognitive-fidelity lesson is lost. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltyMisidentifiedCivilian = 1000;
	// Mirror of MisidentifiedCivilian: a declared-hostile aircraft reached a
	// protected violation zone. Same catastrophic weight - the operator failed
	// to intercept (or declared too late) and a real target got through. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltyViolationZoneBreached = 1000;
	// Reward for landing or safely exiting an emergency-declaring aircraft. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PointsEmergencyHandled = 200;
	// Catastrophic loss - fuel exhaustion, mid-air collision while in emergency,
	// or any other crash. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltyAircraftCrashed = 500;
	// Civilian aircraft entered a restricted area - the controller should have
	// vectored them around it. Bad mistake, not catastrophic. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Points")
	int32 PenaltyRestrictedAirspaceBust = 150;

	// Difficulty: spawn interval (seconds) shrinks toward the minimum as more
	// aircraft are handled successfully.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Difficulty")
	float BaseSpawnIntervalSeconds = 30.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Difficulty")
	float MinSpawnIntervalSeconds = 10.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Difficulty")
	float MaxSpawnIntervalSeconds = 45.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoring|Difficulty")
	float DifficultySecondsPerHandled = 1.f;

private:
	UPROPERTY()
	TArray<FIncidentRecord> IncidentLog;

	int32 CurrentScore = 0;
	int32 TotalLandings = 0;
	int32 TotalHandoffs = 0;
	int32 TotalGoArounds = 0;
	int32 TotalSeparationLosses = 0;
	int32 TotalInstructions = 0;
	int32 TotalHandled = 0; // landings + handoffs
	int32 TotalFailures = 0; // separation losses + unresolved exits + missed handoffs

	float CurrentSpawnIntervalSeconds = 30.f;

	int32 PointsForIncident(EIncidentType Type) const;
	void AdjustDifficulty();
};
