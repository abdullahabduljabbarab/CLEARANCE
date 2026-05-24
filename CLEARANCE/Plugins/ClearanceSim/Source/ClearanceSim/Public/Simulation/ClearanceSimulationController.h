#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceSimulationController.generated.h"

class AClearanceAirspaceManager;
class AClearanceAircraftSpawner;
class UClearanceAircraftBehaviour;
class UClearanceInstructionValidator;
class UClearanceConflictDetector;
class UClearanceCommsRouter;
class UClearanceScoring;

// The conductor. Creates and owns every system, binds them together with
// delegates, owns the per-aircraft Behaviour objects, and runs the authoritative
// tick order each frame. This is the one Actor you drop in a level to run the sim.
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceSimulationController : public AActor
{
	GENERATED_BODY()

public:
	AClearanceSimulationController();

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void StartSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void PauseSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void ResumeSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	void EndSession();

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	bool IsSessionActive() const { return bSessionActive && !bPaused; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	float GetSessionTime() const { return SessionTime; }

	// The UI's single way in: build an instruction, send it here. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Simulation")
	EInstructionResult PlayerIssueInstruction(const FAircraftInstruction& Instruction);

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	AClearanceAirspaceManager* GetAirspaceManager() const { return AirspaceManager; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceScoring* GetScoring() const { return Scoring; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceCommsRouter* GetCommsRouter() const { return CommsRouter; }

	UFUNCTION(BlueprintCallable, Category = "Simulation")
	UClearanceConflictDetector* GetConflictDetector() const { return ConflictDetector; }

	// Start automatically on BeginPlay (handy for testing - just press Play).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	bool bAutoStart = true;

	// Optional: assign placed actors in the level; otherwise they're spawned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Refs")
	TObjectPtr<AClearanceAirspaceManager> AirspaceManager;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Refs")
	TObjectPtr<AClearanceAircraftSpawner> Spawner;

	// Distance from sector centre at which an aircraft is considered to have left.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
	float ExitRadiusNm = 50.f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<UClearanceInstructionValidator> Validator;

	UPROPERTY()
	TObjectPtr<UClearanceConflictDetector> ConflictDetector;

	UPROPERTY()
	TObjectPtr<UClearanceCommsRouter> CommsRouter;

	UPROPERTY()
	TObjectPtr<UClearanceScoring> Scoring;

	UPROPERTY()
	TMap<FName, TObjectPtr<UClearanceAircraftBehaviour>> BehaviourMap;

	bool bSessionActive = false;
	bool bPaused = false;
	float SessionTime = 0.f;
	bool bInitialised = false;

	void InitialiseSystems();
	void BindDelegates();
	void StepSimulation(float DeltaTime);
	void CheckExits();

	UFUNCTION()
	void HandleAircraftRegistered(FName Callsign);

	UFUNCTION()
	void HandleAircraftDeregistered(FName Callsign);

	UFUNCTION()
	void HandleConflictDetected(FConflictEvent Conflict);

	UFUNCTION()
	void HandleGoAroundRequired(FName Callsign);

	UFUNCTION()
	void HandleWakeAdvisory(FName FollowingCallsign, FName LeadingCallsign, float RequiredSeparationNm);

	UFUNCTION()
	void HandleDifficultyAdjusted(float NewSpawnRate);
};
