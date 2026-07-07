// Unreal-side transport wrapper around the ClearanceDDS pub/sub module.
// Sibling to UClearanceDISEmitter - same emit-tick shape, same instructor
// panel controls, but pushes over DDS instead of UDP. Every Emit* method
// converts its Unreal-shape input (FAircraftState / FWeaponsFireEvent /
// ...) into the POD IDL types from AirspaceTelemetry.hpp, calls the pure
// FClearancePublisher API, and returns.
//
// DDS makes the DIS work multimodal - defence primes are moving new
// systems onto DDS-based middleware (Tempest / FCAS sensor fusion, FACE-
// standard avionics). Publishing the same six DIS PDU shapes as DDS topics
// puts CLEARANCE on both wires. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceDDS/ClearanceDDSPublisher.h"
#include "ClearanceDDSEmitter.generated.h"

UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceDDSEmitter : public UObject
{
	GENERATED_BODY()

public:
	UClearanceDDSEmitter();
	virtual ~UClearanceDDSEmitter();

	// Create DDS participant + all six writers on the given domain.
	// Returns false on RTI failure. Domain 0 = Fast DDS default.
	UFUNCTION(BlueprintCallable, Category = "DDS")
	bool Start(int32 DomainId = 0);

	UFUNCTION(BlueprintCallable, Category = "DDS")
	void Stop();

	UFUNCTION(BlueprintCallable, Category = "DDS")
	bool IsRunning() const;

	UFUNCTION(BlueprintCallable, Category = "DDS")
	int32 GetTotalPublishedCount() const;

	// One AircraftState per active aircraft per tick.
	UFUNCTION(BlueprintCallable, Category = "DDS")
	void EmitStates(const TArray<FAircraftState>& States, float SimTimeSeconds);

	// One EmissionSnapshot per active radar per tick.
	UFUNCTION(BlueprintCallable, Category = "DDS")
	void EmitEmissions(const TArray<FRadarEmissionSnapshot>& Radars, float SimTimeSeconds);

	// FireEvent burst per SCRAMBLE launch.
	UFUNCTION(BlueprintCallable, Category = "DDS")
	void EmitFireEvents(const TArray<FWeaponsFireEvent>& Events, float SimTimeSeconds);

	// DetonationEvent burst per intercept resolution.
	UFUNCTION(BlueprintCallable, Category = "DDS")
	void EmitDetonationEvents(const TArray<FWeaponsDetonationEvent>& Events, float SimTimeSeconds);

	// SignalEvent per transcribed voice line.
	UFUNCTION(BlueprintCallable, Category = "DDS")
	void EmitVoiceEvents(const TArray<FVoiceCommsEvent>& Events, float SimTimeSeconds);

	// TransmitterState heartbeat per active radio per tick.
	UFUNCTION(BlueprintCallable, Category = "DDS")
	void EmitTransmitters(const TArray<FRadioTransmitter>& Transmitters, float SimTimeSeconds);

	// Federation identity - matches the DIS emitter's for consistency.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DDS|Identity")
	int32 SiteId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DDS|Identity")
	int32 ApplicationId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DDS|Identity")
	int32 ExerciseId = 1;

private:
	// std::unique_ptr forward-declared - full definition needed in .cpp.
	TUniquePtr<ClearanceDDS::FClearancePublisher> Publisher;
};
