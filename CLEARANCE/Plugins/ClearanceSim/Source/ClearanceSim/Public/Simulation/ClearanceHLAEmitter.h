// Unreal-side adapter around ClearanceHLA. Fourth sibling to
// UClearanceDISEmitter + UClearanceDDSEmitter + UClearanceRTIEmitter -
// same emit-tick shape, same instructor panel controls, but bound
// against OpenRTI's IEEE 1516-2010 ("HLA Evolved") runtime instead of
// UDP + DDS wires.
//
// HLA is what most NATO / MOD / DoD legacy training rigs actually
// federate over. Pairing an HLA adapter alongside DIS + DDS + RTI is
// the "full four-wire interop" portfolio flex - defensible in a Warton
// interview against literally any question about which
// interoperability standards CLEARANCE speaks. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceHLAEmitter.generated.h"

// Forward decl - full HLA publisher definition is confined to
// ClearanceHLAEmitter.cpp so the RTI ambassador headers (and their
// transitive <windows.h>) never leak into ClearanceSim's other TUs.
// Same isolation pattern used for the RTI publisher. - TripleA
namespace ClearanceHLA { class FClearanceHLAFederate; }

UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceHLAEmitter : public UObject
{
	GENERATED_BODY()

public:
	UClearanceHLAEmitter();
	virtual ~UClearanceHLAEmitter();

	// Join an HLA federation execution. FederationName is the string the
	// rtinode advertises the exec under (default "CLEARANCE"). FederateName
	// is our own federate handle within it (default "CLEARANCE-Instructor").
	// FomModulePath is the FDD/FOM XML the RTI parses to build the
	// federation's object model - defaults to the CLEARANCE RPR-FOM
	// extension shipped at Plugins/ClearanceSim/FOM/ClearanceRPR-FOM.xml.
	// Returns false if the rtinode isn't running or the FOM fails to
	// parse. - TripleA
	UFUNCTION(BlueprintCallable, Category = "HLA")
	bool Join(const FString& FederationName, const FString& FederateName, const FString& FomModulePath);

	UFUNCTION(BlueprintCallable, Category = "HLA")
	void Resign();

	UFUNCTION(BlueprintCallable, Category = "HLA")
	bool IsJoined() const;

	UFUNCTION(BlueprintCallable, Category = "HLA")
	int32 GetTotalUpdatesCount() const;

	// One attribute-update per active aircraft per tick, publishing
	// ATCManagedAircraft.{Marking,EntityIdentifier,WorldLocation*,SquawkCode}
	// via the RTIambassador. Same tick shape as DIS/DDS/RTI EmitStates. - TripleA
	UFUNCTION(BlueprintCallable, Category = "HLA")
	void EmitStates(const TArray<FAircraftState>& States, float SimTimeSeconds);

	// Federation identity - kept in sync with the DIS/DDS/RTI emitters so
	// EntityIdentifier attribute updates carry the same {Site, App, Entity}
	// tuple the DIS wire uses. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HLA|Identity")
	int32 SiteId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HLA|Identity")
	int32 ApplicationId = 1;

private:
	// Raw pointer to opaque federate - same pattern the RTI emitter uses
	// to sidestep UHT .gen.cpp destructor instantiation of TUniquePtr<T>
	// with a forward-declared T. Managed via delete in the destructor. - TripleA
	ClearanceHLA::FClearanceHLAFederate* Federate = nullptr;
};
