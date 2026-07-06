// Signal PDU (Type 26) wire-format tests. Pure ClearanceDIS module, no Unreal
// types. Covers roundtrip, 32-bit padding boundary, operator-entity routing,
// and malformed-buffer rejection. - TripleA

#include "Misc/AutomationTest.h"
#include "ClearanceDIS/ClearanceDISPDU.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	inline std::vector<std::uint8_t> AsciiBytes(const char* Text)
	{
		std::vector<std::uint8_t> Out;
		for (const char* P = Text; *P; ++P) { Out.push_back(static_cast<std::uint8_t>(*P)); }
		return Out;
	}
	inline std::string AsciiString(const std::vector<std::uint8_t>& Bytes)
	{
		return std::string(reinterpret_cast<const char*>(Bytes.data()), Bytes.size());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISSignalPDURoundtripTest,
	"Clearance.DIS.SignalPDU.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISSignalPDURoundtripTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FSignalEvent E;
	E.OwnerEntity = ClearanceDIS::HashCallsignToEntityNumber("BAW472");
	E.RadioId     = 1;
	E.Data        = AsciiBytes("Speedbird 472, turn right heading 270, descend flight level 100.");

	ClearanceDIS::FWireParams P;
	P.ExerciseId = 1; P.SiteId = 42; P.ApplicationId = 7; P.SimTimeSeconds = 300.0;
	const std::vector<std::uint8_t> Wire = ClearanceDIS::BuildSignalPDU(E, P);

	// 32-byte fixed body + 64-byte payload (already 32-bit aligned) = 96.
	TestEqual(TEXT("Signal PDU length = 32 + aligned payload"), int32(Wire.size()), 32 + 64);
	TestEqual(TEXT("PDU type is 26 (Signal)"), int32(Wire[2]), 26);
	TestEqual(TEXT("Proto family is 4 (Radio Communications)"), int32(Wire[3]), 4);

	ClearanceDIS::FSignalEvent Out;
	TestTrue(TEXT("Parser accepts well-formed Signal PDU"),
		ClearanceDIS::ParseSignalPDU(Wire.data(), Wire.size(), Out));

	TestEqual(TEXT("Owner entity round-trips"), int32(Out.OwnerEntity), int32(E.OwnerEntity));
	TestEqual(TEXT("Radio ID round-trips"),     int32(Out.RadioId),     int32(E.RadioId));
	TestEqual(TEXT("Transcript byte length round-trips"),
		int32(Out.Data.size()), int32(E.Data.size()));
	TestTrue(TEXT("Transcript payload round-trips byte-for-byte"),
		AsciiString(Out.Data) == AsciiString(E.Data));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISSignalPDUPaddingTest,
	"Clearance.DIS.SignalPDU.PaddingBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISSignalPDUPaddingTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FSignalEvent E;
	E.OwnerEntity = ClearanceDIS::HashCallsignToEntityNumber("VIPER01");
	E.RadioId     = 2;
	E.Data        = AsciiBytes("Fox 2");            // 5 bytes forces 3-byte pad

	ClearanceDIS::FWireParams P;
	const std::vector<std::uint8_t> Wire = ClearanceDIS::BuildSignalPDU(E, P);

	TestEqual(TEXT("Padded to 32-bit boundary"), int32(Wire.size()), 40);

	ClearanceDIS::FSignalEvent Out;
	TestTrue(TEXT("Parser accepts padded PDU"),
		ClearanceDIS::ParseSignalPDU(Wire.data(), Wire.size(), Out));
	TestEqual(TEXT("Transcript excludes padding"), int32(Out.Data.size()), 5);
	TestTrue(TEXT("Transcript content preserved"), AsciiString(Out.Data) == "Fox 2");

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISSignalPDUOperatorTest,
	"Clearance.DIS.SignalPDU.OperatorEntity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISSignalPDUOperatorTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FSignalEvent E;
	E.OwnerEntity = ClearanceDIS::kOperatorGroundStationEntity;
	E.Data        = AsciiBytes("Traffic in your vicinity, one o'clock, five miles.");

	ClearanceDIS::FWireParams P;
	const std::vector<std::uint8_t> Wire = ClearanceDIS::BuildSignalPDU(E, P);

	ClearanceDIS::FSignalEvent Out;
	TestTrue(TEXT("Parser accepts operator PDU"),
		ClearanceDIS::ParseSignalPDU(Wire.data(), Wire.size(), Out));
	TestEqual(TEXT("Operator uses fixed reserved entity number"), int32(Out.OwnerEntity), 60000);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISSignalPDUMalformedRejectionTest,
	"Clearance.DIS.SignalPDU.MalformedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISSignalPDUMalformedRejectionTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FSignalEvent Out;

	std::vector<std::uint8_t> Bad(96, 0);
	Bad[0] = 6;
	Bad[2] = 2; Bad[3] = 2;                        // Fire posing as Signal
	TestFalse(TEXT("Rejects non-Signal PDU type"),
		ClearanceDIS::ParseSignalPDU(Bad.data(), Bad.size(), Out));

	Bad.resize(5);
	TestFalse(TEXT("Rejects truncated buffer"),
		ClearanceDIS::ParseSignalPDU(Bad.data(), Bad.size(), Out));

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
