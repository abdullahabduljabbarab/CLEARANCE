#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceConflictDetector.generated.h"

class AClearanceAirspaceManager;

// Watches every aircraft pair each tick for lost separation, oncoming conflicts,
// and wake turbulence. It only ever READS state and raises alarms - it never
// moves or edits an aircraft. Driven by the Simulation Controller.
UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceConflictDetector : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Conflict|Events")
	FOnConflictDetected OnConflictDetected;

	UPROPERTY(BlueprintAssignable, Category = "Conflict|Events")
	FOnConflictResolved OnConflictResolved;

	UPROPERTY(BlueprintAssignable, Category = "Conflict|Events")
	FOnGoAroundRequired OnGoAroundRequired;

	UPROPERTY(BlueprintAssignable, Category = "Conflict|Events")
	FOnWakeTurbulenceAdvisory OnWakeTurbulenceAdvisory;

	// Fires once per imminent collision (Critical alert), with the climber/descender
	// split and the target altitudes they should split to. Last-resort safety net. - TripleA
	UPROPERTY(BlueprintAssignable, Category = "Conflict|Events")
	FOnTCASResolutionAdvisory OnTCASResolutionAdvisory;

	UFUNCTION(BlueprintCallable, Category = "Conflict")
	void SetReferences(AClearanceAirspaceManager* InManager);

	// One monitoring pass over all aircraft. Fires enter/resolve events as the
	// situation changes, so the UI and Scoring just react to the deltas. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Conflict")
	void DetectConflicts();

	// Highest active alert level any conflict involving this aircraft is at.
	UFUNCTION(BlueprintCallable, Category = "Conflict")
	EAlertLevel GetAlertLevelFor(FName Callsign) const;

	// True while this aircraft is the follower caught in another's wake (read-only -
	// the visual layer uses it to rock the airframe). - TripleA
	UFUNCTION(BlueprintCallable, Category = "Conflict")
	bool IsInWakeTurbulence(FName Callsign) const;

	// 0 = not in wake; up to 1 = severe. Scales with how much heavier the leader is
	// than this follower, so a Light behind a Super is thrown around far more than a
	// Heavy behind a Heavy. The visual layer scales the wing-rock by this.
	UFUNCTION(BlueprintCallable, Category = "Conflict")
	float GetWakeIntensity(FName Callsign) const;

	UFUNCTION(BlueprintCallable, Category = "Conflict")
	void RemoveAircraft(FName Callsign);

	// How far ahead (seconds) to project tracks when looking for oncoming conflicts.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conflict|Tuning")
	float ProjectionLookaheadSeconds = 60.f;

private:
	UPROPERTY()
	TObjectPtr<AClearanceAirspaceManager> Manager;

	// Keyed by an order-independent pair key, so a pair is one conflict either way.
	TMap<FString, FConflictEvent> ActiveConflicts;
	TSet<FString> ActiveWakeAdvisories;
	// Pairs currently under a TCAS RA - so we fire the RA once per imminent collision
	// instead of every tick they stay Critical.
	TSet<FString> ActiveTCAS;
	// Followers currently caught in a wake this pass -> intensity (0..1). Queried by
	// IsInWakeTurbulence / GetWakeIntensity.
	TMap<FName, float> WakeAffected;

	static float HorizontalSeparationNm(const FAircraftState& A, const FAircraftState& B);
	static float VerticalSeparationFt(const FAircraftState& A, const FAircraftState& B);
	static EAlertLevel AlertFromSeparation(float HorizNm, float VertFt);
	static FString PairKey(FName A, FName B);
	static bool FollowerIsBehindLeader(const FAircraftState& Leader, const FAircraftState& Follower);
	static float RequiredWakeSeparationNm(EWakeCategory Leader, EWakeCategory Follower);
};
