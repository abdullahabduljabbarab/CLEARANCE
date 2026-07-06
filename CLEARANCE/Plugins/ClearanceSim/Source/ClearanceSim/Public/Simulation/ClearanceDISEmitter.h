// Unreal-side transport wrapper around the ClearanceDIS wire format module.
// Owns the UDP socket, target address, and packet counter. Every Emit* method
// converts its Unreal-shape input (FAircraftState / FVoiceCommsEvent / ...)
// into the POD types in the ClearanceDIS namespace, calls into the pure-C++
// Build*PDU functions, and pushes the resulting bytes over the wire. No
// serialisation logic lives here - this file is transport + type conversion
// only. Refactor isolates the spec-conformant wire code from the render
// layer. - TripleA

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceDISEmitter.generated.h"

class FSocket;
class FInternetAddr;

UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceDISEmitter : public UObject
{
	GENERATED_BODY()

public:
	// Open a UDP socket and start sending. Host = "broadcast" / "" for LAN broadcast,
	// or a specific IPv4 like "127.0.0.1". Standard DIS port is 3000.
	UFUNCTION(BlueprintCallable, Category = "DIS")
	bool Start(const FString& Host, int32 Port);

	UFUNCTION(BlueprintCallable, Category = "DIS")
	void Stop();

	UFUNCTION(BlueprintCallable, Category = "DIS")
	bool IsRunning() const { return Socket != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "DIS")
	int32 GetLastPacketsSent() const { return LastPacketsSent; }

	// One Entity State PDU (Type 1) per aircraft per tick.
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitStates(const TArray<FAircraftState>& States, float SimTimeSeconds);

	// One Emission PDU (Type 23) per active radar per tick, with Track/Jam
	// block enumerating painted entities. §7.6.2.
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitEmissions(const TArray<FRadarEmissionSnapshot>& Radars, float SimTimeSeconds);

	// One Fire PDU (Type 2) per weapons launch event. §7.3.3.
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitFireEvents(const TArray<FWeaponsFireEvent>& Events, float SimTimeSeconds);

	// One Detonation PDU (Type 3) per weapons event resolution. §7.3.4.
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitDetonationEvents(const TArray<FWeaponsDetonationEvent>& Events, float SimTimeSeconds);

	// One Signal PDU (Type 26) per transcribed radio transmission. §7.7.3.
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitVoiceEvents(const TArray<FVoiceCommsEvent>& Events, float SimTimeSeconds);

	// One Transmitter PDU (Type 25) per active radio per tick - heartbeat
	// companion to Signal. §7.7.2.
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitTransmitters(const TArray<FRadioTransmitter>& Transmitters, float SimTimeSeconds);

	// Standard DIS site / app / exercise identifiers - tells a federation who you are.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DIS|Identity")
	int32 SiteId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DIS|Identity")
	int32 ApplicationId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DIS|Identity")
	int32 ExerciseId = 1;

private:
	FSocket* Socket = nullptr;
	TSharedPtr<FInternetAddr> TargetAddr;
	int32 LastPacketsSent = 0;
};
