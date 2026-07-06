// Roundtrip tests for IEEE 1278.1 Transmitter PDU (Type 25). Companion to
// the Signal PDU tests - Transmitter announces the radio; Signal delivers
// the audio. Same pattern: serialise a known snapshot, parse back, verify
// byte-perfect round-trip. - TripleA

#include "Misc/AutomationTest.h"
#include "Simulation/ClearanceDISEmitter.h"
#include "Core/CLEARANCETypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISTransmitterPDURoundtripTest,
	"Clearance.DIS.TransmitterPDU.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISTransmitterPDURoundtripTest::RunTest(const FString& Parameters)
{
	// An airliner's VHF radio, actively transmitting on the ATC guard channel.
	FRadioTransmitter R;
	R.OwnerCallsign      = TEXT("BAW472");
	R.RadioId            = 1;
	R.FrequencyHz        = 121500000;   // 121.5 MHz
	R.BandwidthHz        = 25000.f;
	R.PowerDbm           = 43.f;
	R.TransmitState      = 2;           // on-transmitting
	R.AntennaWorldMeters = FVector(120000.0, 80000.0, 10000.0);

	UClearanceDISEmitter* Emitter = NewObject<UClearanceDISEmitter>();
	Emitter->SiteId        = 42;
	Emitter->ApplicationId = 7;
	Emitter->ExerciseId    = 1;

	TArray<uint8> Wire;
	Emitter->BuildTransmitterPDU(Wire, R, /*SimTime*/ 250.0f);

	TestEqual(TEXT("Transmitter PDU length matches IEEE 1278.1 §7.7.2 fixed 104 bytes"),
		Wire.Num(), 104);

	// PDU header sanity
	TestEqual(TEXT("Protocol version is 6"), int32(Wire[0]), 6);
	TestEqual(TEXT("PDU type is 25 (Transmitter)"), int32(Wire[2]), 25);
	TestEqual(TEXT("Protocol family is 4 (Radio Communications)"), int32(Wire[3]), 4);

	FRadioTransmitter RoundTrip;
	int32 OwnerEntity = 0;
	const bool bParseOk = UClearanceDISEmitter::ParseTransmitterPDU(Wire, RoundTrip, OwnerEntity);
	TestTrue(TEXT("Parser accepts a well-formed Transmitter PDU"), bParseOk);

	// Owner entity number derived from callsign hash.
	const int32 ExpectedOwner = static_cast<int32>((GetTypeHash(R.OwnerCallsign) % 65535) + 1);
	TestEqual(TEXT("Owner entity number matches stable hash"), OwnerEntity, ExpectedOwner);

	// Field roundtrip
	TestEqual(TEXT("Radio ID round-trips"), RoundTrip.RadioId, R.RadioId);
	TestEqual(TEXT("Transmit state round-trips"), int32(RoundTrip.TransmitState), int32(R.TransmitState));
	TestEqual(TEXT("Frequency (Hz) round-trips exactly"), RoundTrip.FrequencyHz, R.FrequencyHz);
	TestTrue(TEXT("Bandwidth (Hz) within 0.1"),
		FMath::IsNearlyEqual(RoundTrip.BandwidthHz, R.BandwidthHz, 0.1f));
	TestTrue(TEXT("Power (dBm) within 0.01"),
		FMath::IsNearlyEqual(RoundTrip.PowerDbm, R.PowerDbm, 0.01f));

	// Antenna location - doubles round-trip losslessly through the wire.
	TestTrue(TEXT("Antenna X within 0.001 m"),
		FMath::IsNearlyEqual(RoundTrip.AntennaWorldMeters.X, R.AntennaWorldMeters.X, 0.001));
	TestTrue(TEXT("Antenna Y within 0.001 m"),
		FMath::IsNearlyEqual(RoundTrip.AntennaWorldMeters.Y, R.AntennaWorldMeters.Y, 0.001));
	TestTrue(TEXT("Antenna Z within 0.001 m"),
		FMath::IsNearlyEqual(RoundTrip.AntennaWorldMeters.Z, R.AntennaWorldMeters.Z, 0.001));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISTransmitterPDUOperatorTest,
	"Clearance.DIS.TransmitterPDU.OperatorEntity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISTransmitterPDUOperatorTest::RunTest(const FString& Parameters)
{
	// Operator / ground-station radio (NAME_None owner) routes to the same
	// reserved entity number as the Signal PDU emitter uses, so federation
	// receivers can pair Transmitter and Signal traffic for the ground
	// station by entity number alone. - TripleA
	FRadioTransmitter R;
	R.OwnerCallsign      = NAME_None;
	R.TransmitState      = 1;           // idle heartbeat

	UClearanceDISEmitter* Emitter = NewObject<UClearanceDISEmitter>();
	TArray<uint8> Wire;
	Emitter->BuildTransmitterPDU(Wire, R, 0.f);

	FRadioTransmitter RoundTrip;
	int32 OwnerEntity = 0;
	TestTrue(TEXT("Parser accepts operator Transmitter PDU"),
		UClearanceDISEmitter::ParseTransmitterPDU(Wire, RoundTrip, OwnerEntity));
	TestEqual(TEXT("Operator uses fixed reserved entity number (60000)"),
		OwnerEntity, 60000);
	TestEqual(TEXT("Idle transmit state round-trips"),
		int32(RoundTrip.TransmitState), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISTransmitterPDUMalformedRejectionTest,
	"Clearance.DIS.TransmitterPDU.MalformedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISTransmitterPDUMalformedRejectionTest::RunTest(const FString& Parameters)
{
	FRadioTransmitter Out;
	int32 Entity = 0;

	// Wrong PDU type - a well-formed Signal PDU shape should be rejected by
	// the Transmitter parser.
	TArray<uint8> Bad;
	Bad.Init(0, 104);
	Bad[0] = 6;
	Bad[2] = 26;  // Signal posing as Transmitter
	Bad[3] = 4;
	TestFalse(TEXT("Parser rejects non-Transmitter PDU type"),
		UClearanceDISEmitter::ParseTransmitterPDU(Bad, Out, Entity));

	// Truncated - buffer smaller than the fixed header.
	Bad.SetNum(4);
	TestFalse(TEXT("Parser rejects truncated buffer"),
		UClearanceDISEmitter::ParseTransmitterPDU(Bad, Out, Entity));

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
