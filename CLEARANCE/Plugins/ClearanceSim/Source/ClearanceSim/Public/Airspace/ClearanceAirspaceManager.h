#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceAirspaceManager.generated.h"

// The authoritative owner of all aircraft state and sector environment.
// Nothing else is allowed to store its own copy of aircraft state - other
// systems read snapshots from here and route updates back through it. - TripleA
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
	// bIsValid == false, so callers check that instead of relying on a null. - TripleA
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

	// Set the wind up front (runways are provided separately via SetRunways).
	UFUNCTION(BlueprintCallable, Category = "Airspace|Environment")
	void InitialiseEnvironment(float WindDir, float WindSpeed);

	// The runways to choose between, built from placed runway actors. Empty =
	// fall back to a single 09/27 strip at the sector centre.
	UFUNCTION(BlueprintCallable, Category = "Airspace|Environment")
	void SetRunways(const TArray<FRunwayInfo>& InRunways);

	UFUNCTION(BlueprintCallable, Category = "Airspace|Environment")
	void UpdateWindConditions(float NewWindDirection, float NewWindSpeed);

	UFUNCTION(BlueprintCallable, Category = "Airspace|Environment")
	float GetActiveRunway() const;

	// All runway thresholds the manager knows about (each end of each strip is its own
	// entry). Used by the debug draw so every glideslope shows up, not just the one
	// the wind picked as active. - TripleA
	const TArray<FRunwayInfo>& GetAllRunways() const { return Runways; }

	// Spawn a chaff cloud at the given sector-relative XY (nm) and altitude.
	// Server-only; replicates down to clients so their scope shows the same
	// ghost contacts. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Airspace|EW")
	void DropChaff(const FVector& PositionNm, float AltitudeFt);

	// Active chaff clouds, with anything past its lifetime trimmed. Radars iterate
	// this each tick and inject ghost tracks at each cloud's position. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Airspace|EW")
	TArray<FChaffCloud> GetActiveChaffClouds() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Settings", meta = (ClampMin = "1"))
	int32 MaxAircraftCount = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Settings")
	float MinSafeAltitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Settings")
	float MaxSafeAltitude = 60000.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Settings")
	float MinSafeSpeed = 0.f;

	// Global cap covering civilian + military envelopes. Per-category clamps in
	// the Behaviour layer keep airliners at their own MaxOperatingSpeed; this is
	// only the airspace-wide sanity bound. Military Vmax is 1050kts so 1100
	// leaves headroom for transonic overshoot during accel. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Settings")
	float MaxSafeSpeed = 1100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Environment")
	float DefaultWindDirection = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Environment")
	float DefaultWindSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Airspace|Environment")
	TArray<float> AvailableRunwayHeadings;

	// Mirrored from the SimulationController at init so per-aircraft Behaviour
	// UObjects can convert magnetic-frame instructions to the sim's internal
	// frame without having to reach up the actor hierarchy. Set once by the
	// Controller in BeginPlay - never edited after. Plain C++ field (no UPROPERTY)
	// so Live Coding recompiles pick it up without a full rebuild. - TripleA
	float WorldNorthOffsetDeg = 0.f;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// Authoritative server-side store - fast O(1) lookup by callsign. Clients
	// don't see this; they read ReplicatedAircraft via OnRep. - TripleA
	UPROPERTY()
	TMap<FName, FAircraftState> AircraftStates;

	// Flat replication mirror. Every server-side write to AircraftStates writes
	// here too; clients receive this and rebuild their local TMap in OnRep so
	// non-rendering systems that read it (visual layer, scope) keep working. - TripleA
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedAircraft)
	TArray<FAircraftState> ReplicatedAircraft;

	UFUNCTION()
	void OnRep_ReplicatedAircraft();

	UPROPERTY(Replicated)
	FSectorEnvironment SectorEnvironment;

	// Replicated so the client's debug draw (sector boundary, glideslope, runway
	// markings) lines up with where the server placed the strips. - TripleA
	UPROPERTY(Replicated)
	TArray<FRunwayInfo> Runways;

	// Active chaff clouds. Server pushes via DropChaff, clients receive via
	// replication. Trimmed by GetActiveChaffClouds so callers see only live
	// entries. - TripleA
	UPROPERTY(Replicated)
	TArray<FChaffCloud> ChaffClouds;

	bool ValidateState(const FAircraftState& State) const;
	void ClampStateValues(FAircraftState& State) const;

	// Keeps ReplicatedAircraft in lockstep with AircraftStates. Called after every
	// server-side mutation. - TripleA
	void RebuildReplicatedArray();

	// Picks the runway with the least crosswind for the current wind, with a
	// dead-band so a wind hovering near a boundary doesn't flip the runway every
	// tick (Risk R18). Broadcasts OnRunwayChanged only when the choice changes.
	void RecalculateActiveRunway();
};
