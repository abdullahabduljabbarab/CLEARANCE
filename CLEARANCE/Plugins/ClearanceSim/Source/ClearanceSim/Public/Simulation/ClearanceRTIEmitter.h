// Unreal-side transport wrapper around the ClearanceRTI pub/sub module.
// Third sibling to UClearanceDISEmitter + UClearanceDDSEmitter - same
// emit-tick shape, same instructor panel controls, same six-topic schema.
// Just bound against RTI's commercial Connext runtime instead of eProsima
// Fast DDS or a raw UDP DIS socket. All three can run simultaneously on
// disjoint transports (multicast IPs / DDS domains) so a federation
// observer sees every data primitive on every wire concurrently.
//
// RTI Connext is what BAE Warton's Tempest combat cloud, Lockheed's next-
// gen F-35 pipeline, and most tier-one defence primes standardise on for
// new middleware work. Pairing an RTI adapter next to the Fast DDS one
// demonstrates familiarity with BOTH the open-source and the commercial
// DDS runtime - table-stakes for a modelling-and-simulation conversation
// with any of them. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceRTIEmitter.generated.h"

// Deliberately NOT including "ClearanceRTI/ClearanceRTIPublisher.h" here -
// that would pull the RTI-generated hpp into every TU that includes this
// header, and RTI's transitive <windows.h> inclusion pollutes UE headers
// (breaks UE::Cook::FCookDependency, UTextureRenderTarget2D::UpdateResource,
// etc.). Forward-declare the publisher instead; full definition lives in
// ClearanceRTIEmitter.cpp where the pollution is contained. - TripleA
namespace ClearanceRTI { class FClearanceRTIPublisher; }

UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceRTIEmitter : public UObject
{
	GENERATED_BODY()

public:
	UClearanceRTIEmitter();
	virtual ~UClearanceRTIEmitter();

	// Create the RTI participant + all six writers on the given domain.
	// Default is 1 so RTI sits next to Fast DDS (domain 0) without stepping
	// on its discovery traffic. Returns false on Connext initialisation
	// failure (license expired, RTI runtime resource missing, etc.). - TripleA
	UFUNCTION(BlueprintCallable, Category = "RTI")
	bool Start(int32 DomainId = 1);

	UFUNCTION(BlueprintCallable, Category = "RTI")
	void Stop();

	UFUNCTION(BlueprintCallable, Category = "RTI")
	bool IsRunning() const;

	UFUNCTION(BlueprintCallable, Category = "RTI")
	int32 GetTotalPublishedCount() const;

	// One AircraftState sample per active aircraft per tick, same shape as
	// the DIS Entity State PDU + the ClearanceDDS AircraftState topic.
	// Only the runtime differs. - TripleA
	UFUNCTION(BlueprintCallable, Category = "RTI")
	void EmitStates(const TArray<FAircraftState>& States, float SimTimeSeconds);

	// Federation identity - kept in sync with DIS/DDS emitters so all three
	// wires publish under the same site/app identity. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTI|Identity")
	int32 SiteId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTI|Identity")
	int32 ApplicationId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RTI|Identity")
	int32 ExerciseId = 1;

private:
	// Raw pointer to opaque publisher. Deleted manually in BeginDestroy.
	// Raw over TUniquePtr because the UHT-generated .gen.cpp instantiates
	// UClearanceRTIEmitter's destructor in a TU that sees only the forward
	// decl of FClearanceRTIPublisher, which is fatal for TUniquePtr<T>
	// (needs T's destructor at instantiation site). - TripleA
	ClearanceRTI::FClearanceRTIPublisher* Publisher = nullptr;
};
