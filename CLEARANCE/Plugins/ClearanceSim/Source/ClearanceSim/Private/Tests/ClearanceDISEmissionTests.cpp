// Emission PDU (Type 23) wire-format tests. Exercises the pure ClearanceDIS
// module directly - no Unreal types, no UClearanceDISEmitter - so the wire
// format layer is provably testable in isolation from the render engine. The
// Unreal automation framework is only used as the test runner. - TripleA

#include "Misc/AutomationTest.h"
#include "ClearanceDIS/ClearanceDISPDU.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISEmissionPDURoundtripTest,
	"Clearance.DIS.EmissionPDU.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISEmissionPDURoundtripTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FEmissionSnapshot S;
	S.EmittingEntity = 42;
	S.EmitterName    = 8790;    // ASR-9 (SISO-REF-010 UID 75)
	S.EmitterFunction = 22;   // Air Traffic Control (SISO-REF-010 UID 76)
	S.FrequencyLowHz  = 9.4e9f;
	S.FrequencyHighHz = 9.6e9f;
	S.EffectiveRadiatedPowerDbm = 88.0f;
	S.PulseRepetitionFreqHz = 1000.f;
	S.PulseWidthMicrosec    = 1.0f;
	S.BeamAzimuthRad        = 1.5708f;         // ~90 deg
	S.BeamFunction          = 1;     // Search (SISO-REF-010 UID 78)
	S.PaintedEntityNumbers  = { 100, 200, 300, 400 };

	ClearanceDIS::FWireParams P;
	P.ExerciseId = 1; P.SiteId = 42; P.ApplicationId = 7; P.SimTimeSeconds = 128.5;

	const std::vector<std::uint8_t> Wire = ClearanceDIS::BuildEmissionPDU(S, P);

	TestEqual(TEXT("Emission PDU length = 100 + 8 * NTargets"),
		int32(Wire.size()), 100 + 8 * 4);
	TestEqual(TEXT("PDU type is 23 (Emission)"), int32(Wire[2]), 23);
	TestEqual(TEXT("Proto family is 6 (Distributed Emission)"), int32(Wire[3]), 6);

	ClearanceDIS::FEmissionSnapshot Out;
	TestTrue(TEXT("Parser accepts well-formed Emission PDU"),
		ClearanceDIS::ParseEmissionPDU(Wire.data(), Wire.size(), Out));

	TestEqual(TEXT("Emitting entity round-trips"), int32(Out.EmittingEntity), int32(S.EmittingEntity));
	TestEqual(TEXT("Emitter Name round-trips"),   int32(Out.EmitterName),     int32(S.EmitterName));
	TestEqual(TEXT("Emitter Function round-trips"), int32(Out.EmitterFunction), int32(S.EmitterFunction));
	TestEqual(TEXT("Beam Function round-trips"),  int32(Out.BeamFunction),    int32(S.BeamFunction));
	TestEqual(TEXT("Track/Jam count round-trips"), int32(Out.PaintedEntityNumbers.size()), 4);
	TestEqual(TEXT("Track/Jam entity[0] round-trips"), int32(Out.PaintedEntityNumbers[0]), 100);
	TestEqual(TEXT("Track/Jam entity[3] round-trips"), int32(Out.PaintedEntityNumbers[3]), 400);

	TestTrue(TEXT("FrequencyLow within 100 kHz"),
		FMath::IsNearlyEqual(Out.FrequencyLowHz, S.FrequencyLowHz, 100000.f));
	TestTrue(TEXT("FrequencyHigh within 100 kHz"),
		FMath::IsNearlyEqual(Out.FrequencyHighHz, S.FrequencyHighHz, 100000.f));
	TestTrue(TEXT("PRF within 0.01 Hz"),
		FMath::IsNearlyEqual(Out.PulseRepetitionFreqHz, S.PulseRepetitionFreqHz, 0.01f));
	TestTrue(TEXT("Pulse width within 0.001 us"),
		FMath::IsNearlyEqual(Out.PulseWidthMicrosec, S.PulseWidthMicrosec, 0.001f));
	TestTrue(TEXT("ERP within 0.1 dBm"),
		FMath::IsNearlyEqual(Out.EffectiveRadiatedPowerDbm, S.EffectiveRadiatedPowerDbm, 0.1f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISEmissionPDUEmptyTargetListTest,
	"Clearance.DIS.EmissionPDU.EmptyTargetList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISEmissionPDUEmptyTargetListTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FEmissionSnapshot S;
	S.EmittingEntity = 10;

	ClearanceDIS::FWireParams P;
	const std::vector<std::uint8_t> Wire = ClearanceDIS::BuildEmissionPDU(S, P);

	TestEqual(TEXT("Empty Track/Jam = fixed 100 bytes"), int32(Wire.size()), 100);

	ClearanceDIS::FEmissionSnapshot Out;
	TestTrue(TEXT("Parses empty-target Emission PDU"),
		ClearanceDIS::ParseEmissionPDU(Wire.data(), Wire.size(), Out));
	TestEqual(TEXT("Zero painted entities"), int32(Out.PaintedEntityNumbers.size()), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISEmissionPDUMalformedRejectionTest,
	"Clearance.DIS.EmissionPDU.MalformedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISEmissionPDUMalformedRejectionTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FEmissionSnapshot Out;

	std::vector<std::uint8_t> Bad(100, 0);
	Bad[0] = 6;
	Bad[2] = 1;    // Entity State posing as Emission
	Bad[3] = 6;
	TestFalse(TEXT("Rejects non-Emission PDU type"),
		ClearanceDIS::ParseEmissionPDU(Bad.data(), Bad.size(), Out));

	Bad.resize(10);
	TestFalse(TEXT("Rejects truncated buffer"),
		ClearanceDIS::ParseEmissionPDU(Bad.data(), Bad.size(), Out));

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
