#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceInstructionValidator.generated.h"

// Stateless gatekeeper: decides whether an instruction is physically possible for
// a given aircraft before it ever reaches the mover. Owns nothing, reads the
// proposed instruction against the aircraft's current state and category limits.
UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceInstructionValidator : public UObject
{
	GENERATED_BODY()

public:
	// Accepted, or a Rejected_* reason. The Comms Router layers callsign/conflict
	// checks on top; this only judges physical feasibility for the aircraft. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Validation")
	EInstructionResult ValidateInstruction(const FAircraftInstruction& Instruction, const FAircraftState& State) const;
};
