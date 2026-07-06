// Transmitter PDU (Type 25) wire-format tests. Pure ClearanceDIS module.
// Companion to Signal - Transmitter announces the radio's tuning and state,
// Signal delivers the audio. - TripleA

#include "Misc/AutomationTest.h"
#include "ClearanceDIS/ClearanceDISPDU.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISTransmitterPDURoundtripTest,
	"Clearance.DIS.TransmitterPDU.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISTransmitterPDURoundtripTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FTransmitterState R;
	R.OwnerEntity      = ClearanceDIS::HashCallsignToEntityNumber("BAW472");
	R.RadioId          = 1;
	R.FrequencyHz      = 121500000;
	R.BandwidthHz      = 25000.f;
	R.PowerDbm         = 43.f;
	R.TransmitState    = 2;                        // on-transmitting
	R.AntennaXMeters   = 120000.0;
	R.AntennaYMeters   =  80000.0;
	R.AntennaZMeters   =  10000.0;

	ClearanceDIS::FWireParams P;
	P.ExerciseId = 1; P.SiteId = 42; P.ApplicationId = 7; P.SimTimeSeconds = 250.0;
	const std::vector<std::uint8_t> Wire = ClearanceDIS::BuildTransmitterPDU(R, P);

	TestEqual(TEXT("Transmitter PDU length matches §7.7.2 fixed 104 bytes"), int32(Wire.size()), 104);
	TestEqual(TEXT("PDU type is 25 (Transmitter)"), int32(Wire[2]), 25);
	TestEqual(TEXT("Proto family is 4 (Radio Communications)"), int32(Wire[3]), 4);

	ClearanceDIS::FTransmitterState Out;
	TestTrue(TEXT("Parser accepts well-formed Transmitter PDU"),
		ClearanceDIS::ParseTransmitterPDU(Wire.data(), Wire.size(), Out));

	TestEqual(TEXT("Owner entity round-trips"),   int32(Out.OwnerEntity),   int32(R.OwnerEntity));
	TestEqual(TEXT("Radio ID round-trips"),       int32(Out.RadioId),       int32(R.RadioId));
	TestEqual(TEXT("Transmit state round-trips"), int32(Out.TransmitState), int32(R.TransmitState));
	TestEqual(TEXT("Frequency round-trips exactly"), int64(Out.FrequencyHz), int64(R.FrequencyHz));
	TestTrue(TEXT("Bandwidth within 0.1 Hz"), FMath::IsNearlyEqual(Out.BandwidthHz, R.BandwidthHz, 0.1f));
	TestTrue(TEXT("Power within 0.01 dBm"),   FMath::IsNearlyEqual(Out.PowerDbm,    R.PowerDbm,    0.01f));
	TestTrue(TEXT("Antenna X within 0.001 m"), FMath::IsNearlyEqual(Out.AntennaXMeters, R.AntennaXMeters, 0.001));
	TestTrue(TEXT("Antenna Y within 0.001 m"), FMath::IsNearlyEqual(Out.AntennaYMeters, R.AntennaYMeters, 0.001));
	TestTrue(TEXT("Antenna Z within 0.001 m"), FMath::IsNearlyEqual(Out.AntennaZMeters, R.AntennaZMeters, 0.001));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISTransmitterPDUOperatorTest,
	"Clearance.DIS.TransmitterPDU.OperatorEntity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISTransmitterPDUOperatorTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FTransmitterState R;
	R.OwnerEntity   = ClearanceDIS::kOperatorGroundStationEntity;
	R.TransmitState = 1;                           // idle heartbeat

	ClearanceDIS::FWireParams P;
	const std::vector<std::uint8_t> Wire = ClearanceDIS::BuildTransmitterPDU(R, P);

	ClearanceDIS::FTransmitterState Out;
	TestTrue(TEXT("Parser accepts operator Transmitter PDU"),
		ClearanceDIS::ParseTransmitterPDU(Wire.data(), Wire.size(), Out));
	TestEqual(TEXT("Operator uses fixed reserved entity number (60000)"),
		int32(Out.OwnerEntity), 60000);
	TestEqual(TEXT("Idle transmit state round-trips"), int32(Out.TransmitState), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISTransmitterPDUMalformedRejectionTest,
	"Clearance.DIS.TransmitterPDU.MalformedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISTransmitterPDUMalformedRejectionTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FTransmitterState Out;

	std::vector<std::uint8_t> Bad(104, 0);
	Bad[0] = 6;
	Bad[2] = 26; Bad[3] = 4;                       // Signal posing as Transmitter
	TestFalse(TEXT("Rejects non-Transmitter PDU type"),
		ClearanceDIS::ParseTransmitterPDU(Bad.data(), Bad.size(), Out));

	Bad.resize(4);
	TestFalse(TEXT("Rejects truncated buffer"),
		ClearanceDIS::ParseTransmitterPDU(Bad.data(), Bad.size(), Out));

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
