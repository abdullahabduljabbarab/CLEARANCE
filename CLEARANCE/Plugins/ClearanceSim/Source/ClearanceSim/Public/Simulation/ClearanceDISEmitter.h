#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceDISEmitter.generated.h"

class FSocket;
class FInternetAddr;

// Emits IEEE 1278 DIS (Distributed Interactive Simulation) Entity State PDUs
// over UDP, one per active aircraft per tick. One-way broadcaster - any DIS-
// compliant viewer or simulation on the same network sees the traffic. This is
// the defence/aerospace interop standard, and the whole point is that you can
// publish your sim's entities to anything else in a DIS federation. - TripleA
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

	// Build one Entity State PDU per state and fire it out. Called by the Controller
	// each tick when this emitter is running. SimTimeSeconds drives the DIS timestamp.
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitStates(const TArray<FAircraftState>& States, float SimTimeSeconds);

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

	void BuildEntityStatePDU(TArray<uint8>& Out, const FAircraftState& State, float SimTimeSeconds) const;

	// Big-endian writers - DIS is network byte order. - TripleA
	static void WriteU8(TArray<uint8>& B, uint8 V);
	static void WriteU16BE(TArray<uint8>& B, uint16 V);
	static void WriteU32BE(TArray<uint8>& B, uint32 V);
	static void WriteFloatBE(TArray<uint8>& B, float V);
	static void WriteDoubleBE(TArray<uint8>& B, double V);
};
