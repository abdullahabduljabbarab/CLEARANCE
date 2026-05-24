#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceCommsRouter.generated.h"

class AClearanceAirspaceManager;
class UClearanceInstructionValidator;
class UClearanceAircraftBehaviour;

// The gate every instruction passes through: look up the aircraft, ask the
// Validator if it's allowed, and either hand it to that aircraft's mover or
// report why it was refused. It moves nothing itself.
UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceCommsRouter : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Comms|Events")
	FOnInstructionResult OnInstructionResult;

	UPROPERTY(BlueprintAssignable, Category = "Comms|Events")
	FOnAdvisoryWarning OnAdvisoryWarning;

	UFUNCTION(BlueprintCallable, Category = "Comms")
	void SetReferences(AClearanceAirspaceManager* InManager, UClearanceInstructionValidator* InValidator);

	// The Controller calls these as aircraft come and go, so the router always
	// knows which mover to hand an instruction to. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Comms")
	void RegisterBehaviour(FName Callsign, UClearanceAircraftBehaviour* Behaviour);

	UFUNCTION(BlueprintCallable, Category = "Comms")
	void UnregisterBehaviour(FName Callsign);

	// Main entry for a player command. Validates, routes to the aircraft's mover,
	// broadcasts the result. Never touches aircraft state directly.
	UFUNCTION(BlueprintCallable, Category = "Comms")
	EInstructionResult IssueInstruction(const FAircraftInstruction& Instruction);

	// Conflict-driven go-around: routed through the mover, not slammed into state.
	UFUNCTION(BlueprintCallable, Category = "Comms")
	void RouteGoAround(FName Callsign);

	UFUNCTION(BlueprintCallable, Category = "Comms")
	void ReceiveAdvisory(const FString& Message, EAlertLevel Level);

	// Seconds between accepted instructions per aircraft. 0 = off (hook for later).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Comms|Tuning")
	float MinInstructionInterval = 0.f;

private:
	UPROPERTY()
	TObjectPtr<AClearanceAirspaceManager> Manager;

	UPROPERTY()
	TObjectPtr<UClearanceInstructionValidator> Validator;

	UPROPERTY()
	TMap<FName, TObjectPtr<UClearanceAircraftBehaviour>> Behaviours;

	TMap<FName, float> LastInstructionTime;
};
