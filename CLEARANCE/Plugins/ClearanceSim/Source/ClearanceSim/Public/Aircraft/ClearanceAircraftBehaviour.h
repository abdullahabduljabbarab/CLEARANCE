#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceAircraftBehaviour.generated.h"

class AClearanceAirspaceManager;

// One of these exists per aircraft, owned by the Simulation Controller. It's the
// only thing allowed to move an aircraft: each tick it reads the aircraft's state
// from the Airspace Manager, slews it toward its targets within the performance
// envelope, then commits the new state back. It never owns state itself. - TripleA
UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceAircraftBehaviour : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Behaviour")
	void Initialise(AClearanceAirspaceManager* InManager, FName InCallsign);

	UFUNCTION(BlueprintCallable, Category = "Behaviour")
	void UpdateMovement(float DeltaTime);

	UFUNCTION(BlueprintCallable, Category = "Behaviour")
	void QueueInstruction(const FAircraftInstruction& Instruction);

	UFUNCTION(BlueprintCallable, Category = "Behaviour")
	bool HasActiveInstruction() const;

	UFUNCTION(BlueprintCallable, Category = "Behaviour")
	void ClearInstructions();

	UFUNCTION(BlueprintCallable, Category = "Behaviour")
	void ExecuteGoAround();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behaviour|Tuning")
	float AccelerationKnotsPerSec = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behaviour|Tuning")
	float HeadingToleranceDeg = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behaviour|Tuning")
	float AltitudeToleranceFt = 50.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behaviour|Tuning")
	float SpeedToleranceKnots = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behaviour|Tuning")
	float GoAroundClimbFt = 3000.f;

private:
	UPROPERTY()
	TObjectPtr<AClearanceAirspaceManager> Manager;

	FName Callsign;
	TArray<FAircraftInstruction> Pending;
	bool bGoingAround = false;

	void ApplyInstruction(const FAircraftInstruction& Instruction, FAircraftState& State) const;
	void StepHeading(FAircraftState& State, float DeltaTime) const;
	void StepAltitude(FAircraftState& State, float DeltaTime) const;
	void StepSpeed(FAircraftState& State, float DeltaTime) const;
	void StepPosition(FAircraftState& State, const FSectorEnvironment& Env, float DeltaTime) const;

	// Standard-rate-ish turn: rate falls off as speed rises for a fixed bank limit,
	// which is why a Heavy turns lazier than a Light at the same heading change. - TripleA
	float TurnRateDegPerSec(const FAircraftState& State) const;
	float DensityAdjustedClimbRate(const FAircraftState& State) const;

	static float ISADensityRatio(float AltitudeFt);
	static void GetCategoryLimits(EWakeCategory Category, float& OutCeiling, float& OutMinSpeed, float& OutMaxSpeed, float& OutClimbRate, float& OutBankLimitDeg);
};
