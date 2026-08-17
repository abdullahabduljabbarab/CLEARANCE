#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "AutopilotWrapper.h"
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
	float HeadingToleranceDeg = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behaviour|Tuning")
	float AltitudeToleranceFt = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behaviour|Tuning")
	float SpeedToleranceKnots = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Behaviour|Tuning")
	float GoAroundClimbFt = 3000.f;

	// How far past the threshold (nm) to aim the touchdown - the touchdown zone, so it
	// plants in the runway rather than on the lip. Set by the Controller. - TripleA
	float TouchdownZoneOffsetNm = 0.16f;

	// --- Simulink autopilot integration -------------------------------------
	// When engaged, UpdateMovement bypasses the built-in Step* kinematics
	// and hands the throttle / elevator / aileron / bank / pitch loop off
	// to the Embedded-Coder-generated autopilot in ClearanceAutopilotMBD.
	// Toggle via clearance.autopilot.engage / disengage in the console. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Behaviour|Autopilot")
	void SetAutopilotEngaged(bool bEngaged) { bAutopilotEngaged = bEngaged; }

	UFUNCTION(BlueprintCallable, Category = "Behaviour|Autopilot")
	bool IsAutopilotEngaged() const { return bAutopilotEngaged; }

private:
	UPROPERTY()
	TObjectPtr<AClearanceAirspaceManager> Manager;

	FName Callsign;
	TArray<FAircraftInstruction> Pending;
	bool bGoingAround = false;
	int32 ActiveTurnDirection = 0; // -1 left / +1 right / 0 shortest, for the current turn
	bool bExpediting = false;      // boosted climb/descent for the current altitude change
	bool bApproachCaptured = false;// true once established on the localiser (then it flies the ILS)
	float ApproachSlopeFtPerNm = 212.f; // personal glide gradient snapshotted at clearance so aircraft descends from where cleared, not where the textbook 3deg slope catches

	// Simulink cascade autopilot state - the wrapper owns the model's
	// internal timekeeping so each aircraft gets its own PID history.
	// Default ON: every aircraft flies under the Embedded-Coder-
	// generated autopilot. Toggle via clearance.autopilot.disengage
	// to hand a specific aircraft back to the built-in slew logic
	// for A/B comparison. - TripleA
	bool bAutopilotEngaged = true;
	FAutopilotWrapper AutopilotWrapper;

	void ApplyInstruction(const FAircraftInstruction& Instruction, FAircraftState& State);
	// True once the aircraft is positioned to intercept the localiser (the
	// controller's vectoring did its job) - only then does the ILS capture.
	bool IsEstablishedOnApproach(const FAircraftState& State, const FSectorEnvironment& Env) const;

	// True if a cleared-but-not-yet-established aircraft has flown past the
	// threshold - it can't make the strip from here, so it calls unable. - TripleA
	bool HasMissedApproach(const FAircraftState& State, const FSectorEnvironment& Env) const;

	// When established: track the runway centreline and 3-deg glideslope down to a
	// touchdown at the selected runway's threshold. - TripleA
	void RunApproachGuidance(FAircraftState& State, const FSectorEnvironment& Env);
	void StepHeading(FAircraftState& State, float DeltaTime);
	void StepAltitude(FAircraftState& State, float DeltaTime);
	void StepSpeed(FAircraftState& State, float DeltaTime) const;
	void StepPosition(FAircraftState& State, const FSectorEnvironment& Env, float DeltaTime) const;

	// Alternate stepping path when bAutopilotEngaged: pushes state into
	// the Simulink model, reads throttle / elevator / aileron back, and
	// applies them to bank angle, climb rate, and speed. Kinematics then
	// integrate heading from bank and position from speed + heading. - TripleA
	void StepWithAutopilot(FAircraftState& State, const FSectorEnvironment& Env, float DeltaTime);

	// Standard-rate-ish turn: rate falls off as speed rises for a fixed bank limit,
	// which is why a Heavy turns lazier than a Light at the same heading change. - TripleA
	float TurnRateDegPerSec(const FAircraftState& State) const;
	float DensityAdjustedClimbRate(const FAircraftState& State) const;

	static float ISADensityRatio(float AltitudeFt);
};
