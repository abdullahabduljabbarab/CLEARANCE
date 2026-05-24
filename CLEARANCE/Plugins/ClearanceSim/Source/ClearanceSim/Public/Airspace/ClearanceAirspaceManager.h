#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceAirspaceManager.generated.h"

// The authoritative owner of all aircraft state and sector environment.
// Nothing else is allowed to store its own copy of aircraft state - other
// systems read snapshots from here and route updates back through it.
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceAirspaceManager : public AActor
{
	GENERATED_BODY()

public:
	AClearanceAirspaceManager();

	UPROPERTY(BlueprintAssignable, Category = "Airspace|Events")
	FOnAircraftRegistered OnAircraftRegistered;

	UPROPERTY(BlueprintAssignable, Category = "Airspace|Events")
	FOnAircraftDeregistered OnAircraftDeregistered;

	UPROPERTY(BlueprintAssignable, Category = "Airspace|Events")
	FOnAircraftStateUpdated OnAircraftStateUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Airspace|Events")
	FOnRunwayChanged OnRunwayChanged;

	UFUNCTION(BlueprintCallable, Category = "Airspace")
	bool RegisterAircraft(const FAircraftState& NewAircraft);

	UFUNCTION(BlueprintCallable, Category = "Airspace")
	bool DeregisterAircraft(FName Callsign);

	// Commits a new state for an already-registered aircraft. This is the only
	// sanctioned write path - the Behaviour system calls it each tick.
	UFUNCTION(BlueprintCallable, Category = "Airspace")
	bool RequestStateUpdate(const FAircraftState& UpdatedState);

	// Returns a copy. If the callsign isn't registered the result has
	// bIsValid == false, so callers can check that instead of relying on a null.
	UFUNCTION(BlueprintCallable, Category = "Airspace")
	FAircraftState GetAircraftState(FName Callsign) const;

	UFUNCTION(BlueprintCallable, Category = "Airspace")
	TArray<FAircraftState> GetAllAircraftStates() const;

	UFUNCTION(BlueprintCallable, Category = "Airspace")
	int32 GetAircraftCount() const;

	UFUNCTION(BlueprintCallable, Category = "Airspace")
	bool IsCallsignRegistered(FName Callsign) const;

	UFUNCTION(BlueprintCallable, Category = "Airspace")
	void ClearAllAircraft();

	UFUNCTION(BlueprintCallable, Category = "Airspace|Environment")
	FSectorEnvironment GetCurrentEnvironment() const;

	UFUNCTION(BlueprintCallable, Category = "Airspace|Environment")
	void UpdateWindConditions(float NewWindDirection, float NewWindSpeed);

	UFUNCTION(BlueprintCallable, Category = "Airspace|Environment")
	float GetActiveRunway() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Settings", meta = (ClampMin = "1"))
	int32 MaxAircraftCount = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Settings")
	float MinSafeAltitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Settings")
	float MaxSafeAltitude = 60000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Settings")
	float MinSafeSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Settings")
	float MaxSafeSpeed = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Environment")
	float DefaultWindDirection = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Environment")
	float DefaultWindSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Environment")
	TArray<float> AvailableRunwayHeadings;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TMap<FName, FAircraftState> AircraftStates;

	UPROPERTY()
	FSectorEnvironment SectorEnvironment;

	bool ValidateState(const FAircraftState& State) const;
	void ClampStateValues(FAircraftState& State) const;

	// Picks the runway with the least crosswind for the current wind, with a
	// dead-band so a wind hovering near a boundary doesn't flip the runway every
	// tick (Risk R18). Broadcasts OnRunwayChanged only when the choice changes.
	void RecalculateActiveRunway();
};
