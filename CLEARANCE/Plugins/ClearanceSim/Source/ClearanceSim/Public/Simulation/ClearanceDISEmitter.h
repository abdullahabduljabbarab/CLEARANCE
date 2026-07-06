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

	// Build one Emission PDU (Type 23) per active radar and fire it out. Each PDU
	// carries the radar's emitting-entity ID, one Emitter System block for the
	// radar, one Beam block for the search beam (with all the ELINT fingerprint
	// data - freq / PRF / pulse width / ERP), and a Track/Jam block per aircraft
	// the radar is currently painting. Turns each radar into a first-class
	// federation citizen visible to any DIS ELINT receiver. - TripleA
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitEmissions(const TArray<FRadarEmissionSnapshot>& Radars, float SimTimeSeconds);

	// Build a Fire PDU (Type 2) per event and fire it out. Signals "aircraft
	// A just launched a munition at aircraft B", carrying the launch position,
	// velocity vector, munition catalogue codes, and a unique EventNumber that
	// a later Detonation PDU can reference. §7.3.3 IEEE 1278.1. - TripleA
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitFireEvents(const TArray<FWeaponsFireEvent>& Events, float SimTimeSeconds);

	// Build a Detonation PDU (Type 3) per event. Signals "the munition from
	// event N just hit / missed / dud", carrying the detonation position + a
	// result code (entity impact, ground impact, dud, etc). Pairs with the
	// originating Fire PDU by EventNumber. §7.3.4 IEEE 1278.1. - TripleA
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitDetonationEvents(const TArray<FWeaponsDetonationEvent>& Events, float SimTimeSeconds);

	// Build a Signal PDU (Type 26, Radio Communications family) per voice
	// event and fire it out. Broadcasts a radio transmission from the speaker's
	// entity + radio ID, with the transcript carried as raw-binary payload so
	// a federation observer can see live comms traffic without needing a codec
	// dependency. §7.7.3 IEEE 1278.1. - TripleA
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitVoiceEvents(const TArray<FVoiceCommsEvent>& Events, float SimTimeSeconds);

	// Build a Transmitter PDU (Type 25, Radio Communications family) per
	// active radio and fire it out. Announces the radio's frequency, power,
	// modulation, and current transmit state so a federation receiver can
	// tune before hearing the audio traffic carried by the Signal PDU. This
	// is the heartbeat companion to Signal - Transmitter says "there is a
	// radio here doing X"; Signal says "here's the actual bytes on it".
	// §7.7.2 IEEE 1278.1. - TripleA
	UFUNCTION(BlueprintCallable, Category = "DIS")
	void EmitTransmitters(const TArray<FRadioTransmitter>& Transmitters, float SimTimeSeconds);

	// Public for test-suite access. The runtime path is through EmitEmissions
	// which fires each Buf out over the wire; this variant is exposed so the
	// automation tests can verify byte-level format without a socket. - TripleA
	void BuildEmissionPDU(TArray<uint8>& Out, const FRadarEmissionSnapshot& Radar, float SimTimeSeconds) const;

	// Public for test-suite access - same rationale as BuildEmissionPDU.
	void BuildFirePDU(TArray<uint8>& Out, const FWeaponsFireEvent& Event, float SimTimeSeconds) const;
	void BuildDetonationPDU(TArray<uint8>& Out, const FWeaponsDetonationEvent& Event, float SimTimeSeconds) const;
	void BuildSignalPDU(TArray<uint8>& Out, const FVoiceCommsEvent& Event, float SimTimeSeconds) const;
	void BuildTransmitterPDU(TArray<uint8>& Out, const FRadioTransmitter& Radio, float SimTimeSeconds) const;

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

public:
	// Static parser for the Emission PDU wire format. Fills Out on success,
	// returns false if the buffer is truncated or malformed. Used both by the
	// receiver-side federation ingest and by the roundtrip automation test to
	// verify byte-perfect serialize -> parse -> serialize equality.
	// - TripleA
	static bool ParseEmissionPDU(const TArray<uint8>& In, FRadarEmissionSnapshot& Out,
		int32& OutEmittingSite, int32& OutEmittingApp, int32& OutEmittingEntity,
		TArray<int32>& OutTargetEntityNumbers);

	// Parsers for Fire and Detonation PDUs - same contract. OutFiringEntity /
	// OutMunitionEntity carry the entity numbers so tests can verify the
	// stable per-callsign hash mapping round-trips. - TripleA
	static bool ParseFirePDU(const TArray<uint8>& In, FWeaponsFireEvent& Out,
		int32& OutFiringEntity, int32& OutTargetEntity, int32& OutMunitionEntity);
	static bool ParseDetonationPDU(const TArray<uint8>& In, FWeaponsDetonationEvent& Out,
		int32& OutFiringEntity, int32& OutTargetEntity, int32& OutMunitionEntity);

	// Parser for Signal PDU (Type 26). OutSpeakerEntity is the entity number the
	// speaker was hashed to; OutTranscript is the ASCII payload extracted from
	// the raw-binary data block. - TripleA
	static bool ParseSignalPDU(const TArray<uint8>& In, FVoiceCommsEvent& Out,
		int32& OutSpeakerEntity);

	// Parser for Transmitter PDU (Type 25). OutOwnerEntity is the entity
	// number the owner was hashed to; frequency / bandwidth / power / state /
	// antenna location roundtrip through Out. - TripleA
	static bool ParseTransmitterPDU(const TArray<uint8>& In, FRadioTransmitter& Out,
		int32& OutOwnerEntity);

private:
	// Big-endian writers - DIS is network byte order. - TripleA
	static void WriteU8(TArray<uint8>& B, uint8 V);
	static void WriteU16BE(TArray<uint8>& B, uint16 V);
	static void WriteU32BE(TArray<uint8>& B, uint32 V);
	static void WriteFloatBE(TArray<uint8>& B, float V);
	static void WriteDoubleBE(TArray<uint8>& B, double V);

	// Big-endian readers - paired with the writers so the roundtrip test can
	// verify byte-perfect symmetry. - TripleA
	static uint8  ReadU8    (const TArray<uint8>& B, int32& Cursor, bool& bOk);
	static uint16 ReadU16BE (const TArray<uint8>& B, int32& Cursor, bool& bOk);
	static uint32 ReadU32BE (const TArray<uint8>& B, int32& Cursor, bool& bOk);
	static float  ReadFloatBE(const TArray<uint8>& B, int32& Cursor, bool& bOk);
};
