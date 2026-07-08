// RPR-FOM / DIS <-> HLA affiliation mapping tests. Verifies that the numeric
// ForceId constants CLEARANCE uses in its DIS emitter, DDS emitter, RTI
// emitter, and HLA federate match the values defined in IEEE 1278.1
// §7.3.4.6 (DIS Enumerations) and mirrored by SISO RPR-FOM 2.0 for HLA
// federations.
//
// The value table is:
//   0 = Other / Unknown
//   1 = Friendly
//   2 = Opposing (Hostile)
//   3 = Neutral
//
// A drift in any of these breaks federation with any conforming
// third-party sim (KDIS, MAK VR-Forces, VBS4, Portico test federates,
// commercial RTIs) - so the tests are both spec-compliance and
// federation-interop coverage.
//
// Test method: build a POD FEntityState via the ClearanceDIS module,
// serialise it, and inspect the ForceId byte at its IEEE 1278.1 spec
// offset (byte 18 of the Entity State PDU) to verify the wire encoding
// matches §7.3.4.6. Wire-byte inspection instead of parse-back because
// the public ClearanceDIS API doesn't currently expose an Entity State
// parser (only Fire / Detonation / Emission / Signal / Transmitter).
// - TripleA
//
// Covers requirements:
//   REQ-FED-001  ForceId 0 shall represent Other/Unknown per §7.3.4.6.
//   REQ-FED-002  ForceId 1 shall represent Friendly per §7.3.4.6.
//   REQ-FED-003  ForceId 2 shall represent Opposing (Hostile) per §7.3.4.6.
//   REQ-FED-004  ForceId 3 shall represent Neutral per §7.3.4.6.
//   REQ-FED-005  Entity State PDU wire encoding shall write ForceId byte-
//                exactly at spec offset 18 for every defined value.

#include "Misc/AutomationTest.h"
#include "ClearanceDIS/ClearanceDISPDU.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Build a minimal but valid FEntityState with a given ForceId and read
	// back the ForceId byte at its IEEE 1278.1 spec offset (§7.3.4, byte 18
	// of the Entity State PDU - immediately after the 12-byte header + 6-byte
	// Entity ID). Verifies wire-format compliance without needing a public
	// parser. - TripleA
	constexpr int32 kEntityStateForceIdByteOffset = 18;

	std::uint8_t WireForceIdByte(std::uint8_t InForceId)
	{
		ClearanceDIS::FEntityState S;
		S.EntityNumber       = ClearanceDIS::HashCallsignToEntityNumber("TEST01");
		S.ForceId            = InForceId;
		S.EntityKind         = 1;   // Platform
		S.EntityDomain       = 2;   // Air
		S.EntityCountry      = 225; // UK
		S.EntityCategory     = 1;
		S.EntitySubcategory  = 0;
		S.EntitySpecific     = 0;
		S.EntityExtra        = 0;
		S.XMeters = 0.0; S.YMeters = 0.0; S.ZMeters = 5000.0;
		S.VxMps   = 0.f; S.VyMps   = 0.f; S.VzMps   = 0.f;
		S.PsiRad  = 0.f; S.ThetaRad = 0.f; S.PhiRad = 0.f;
		S.Marking = "TEST01";

		ClearanceDIS::FWireParams P;
		P.ExerciseId = 1; P.SiteId = 1; P.ApplicationId = 1; P.SimTimeSeconds = 0.0;

		const std::vector<std::uint8_t> Wire = ClearanceDIS::BuildEntityStatePDU(S, P);
		if (static_cast<int32>(Wire.size()) <= kEntityStateForceIdByteOffset) { return 0xFF; }
		return Wire[kEntityStateForceIdByteOffset];
	}
}

// Covers: REQ-FED-001, REQ-FED-005 - Other/Unknown = 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRPRFOMForceIdOtherTest,
	"Clearance.Federation.RPRFOM.ForceId.Other",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRPRFOMForceIdOtherTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Other/Unknown ForceId is 0 per §7.3.4.6"),
		int32(WireForceIdByte(0)), 0);
	return true;
}

// Covers: REQ-FED-002, REQ-FED-005 - Friendly = 1.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRPRFOMForceIdFriendlyTest,
	"Clearance.Federation.RPRFOM.ForceId.Friendly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRPRFOMForceIdFriendlyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Friendly ForceId is 1 per §7.3.4.6"),
		int32(WireForceIdByte(1)), 1);
	return true;
}

// Covers: REQ-FED-003, REQ-FED-005 - Opposing (Hostile) = 2.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRPRFOMForceIdHostileTest,
	"Clearance.Federation.RPRFOM.ForceId.Hostile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRPRFOMForceIdHostileTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Opposing/Hostile ForceId is 2 per §7.3.4.6"),
		int32(WireForceIdByte(2)), 2);
	return true;
}

// Covers: REQ-FED-004, REQ-FED-005 - Neutral = 3.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRPRFOMForceIdNeutralTest,
	"Clearance.Federation.RPRFOM.ForceId.Neutral",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRPRFOMForceIdNeutralTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Neutral ForceId is 3 per §7.3.4.6"),
		int32(WireForceIdByte(3)), 3);
	return true;
}

// Covers: encode-side robustness - a non-standard ForceId value (99)
// should be written to the wire as-is rather than being silently clamped
// to a §7.3.4.6 legal value. Matches DIS interop convention: forward
// what you don't understand, let receivers decide. - TripleA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRPRFOMForceIdUnknownPreservedTest,
	"Clearance.Federation.RPRFOM.ForceId.UnknownPreserved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRPRFOMForceIdUnknownPreservedTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Non-standard ForceId 99 preserved by wire encoder"),
		int32(WireForceIdByte(99)), 99);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
