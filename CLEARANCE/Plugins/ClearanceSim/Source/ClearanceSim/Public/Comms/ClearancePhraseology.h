#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ClearancePhraseology.generated.h"

class AClearanceSimulationController;

// Turns an ATC transmission (typed now, spoken later) into instructions: resolve
// the callsign, parse heading/altitude/speed clearances, issue them through the
// controller, and return a readback. This is the layer voice input will feed. - TripleA
UCLASS()
class CLEARANCESIM_API UClearancePhraseology : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Returns a spoken-style readback of what was understood/accepted, or a
	// "say again" style message if nothing parsed.
	UFUNCTION(BlueprintCallable, Category = "Clearance|Voice")
	static FString Interpret(AClearanceSimulationController* Controller, const FString& Transmission);
};
