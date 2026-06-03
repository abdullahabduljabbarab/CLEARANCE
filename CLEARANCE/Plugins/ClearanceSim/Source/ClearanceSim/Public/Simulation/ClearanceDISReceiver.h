#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceDISReceiver.generated.h"

class FSocket;
class AClearanceAirspaceManager;

// Passive listener for IEEE 1278 DIS Entity State PDUs. Anything sent to the
// bound port - by a partner trainer, another instance of this sim, or a
// recorded DIS log being replayed - is decoded and injected as an external
// aircraft. External aircraft aren't flown by the local Behaviour, can't be
// commanded by the local player, and don't get re-emitted: they're a mirror of
// somebody else's truth. - TripleA
UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceDISReceiver : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "DIS")
	bool Start(int32 Port);

	UFUNCTION(BlueprintCallable, Category = "DIS")
	void Stop();

	UFUNCTION(BlueprintCallable, Category = "DIS")
	bool IsRunning() const { return Socket != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "DIS")
	int32 GetLastPacketsReceived() const { return LastPacketsReceived; }

	UFUNCTION(BlueprintCallable, Category = "DIS")
	int32 GetExternalAircraftCount() const { return LastSeenSeconds.Num(); }

	// Drain pending packets, inject/update aircraft, expire stale ones. Called by
	// the Controller each tick. WorldTimeSeconds is real wall-clock time.
	void Poll(AClearanceAirspaceManager* Manager, double WorldTimeSeconds);

	// Identity of the LOCAL emitter, so we can skip our own broadcast loopback.
	// Should be kept in sync with the controller's UClearanceDISEmitter values.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DIS|Identity")
	int32 LocalSiteId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DIS|Identity")
	int32 LocalApplicationId = 1;

	// An external aircraft not refreshed in this many real-world seconds is
	// dropped from the local scope.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DIS")
	float StaleTimeoutSeconds = 10.f;

private:
	FSocket* Socket = nullptr;
	int32 LastPacketsReceived = 0;
	double LastPollSeconds = -1.0;

	// Real-time of the last update per external aircraft.
	TMap<FName, double> LastSeenSeconds;

	bool ParseAndInject(const uint8* Data, int32 Length, AClearanceAirspaceManager* Manager,
		double WorldTimeSeconds, TSet<FName>& OutFreshThisPoll);

	// Standard DIS dead reckoning (algorithm 2 - constant linear velocity): advance
	// each known external by its last-known velocity for the time since we last saw
	// a packet for it, so they fly smoothly between sparse incoming updates. - TripleA
	void DeadReckonStale(AClearanceAirspaceManager* Manager, double WorldTimeSeconds,
		float DeltaSeconds, const TSet<FName>& FreshThisPoll);

	// Big-endian readers - DIS is network byte order.
	static uint16 ReadU16BE(const uint8* P);
	static uint32 ReadU32BE(const uint8* P);
	static float  ReadFloatBE(const uint8* P);
	static double ReadDoubleBE(const uint8* P);
};
