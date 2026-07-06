// Fire (Type 2) and Detonation (Type 3) wire-format tests. Exercises the pure
// ClearanceDIS module directly - proves the Warfare-family serialisation is
// spec-conformant without touching any Unreal type. - TripleA

#include "Misc/AutomationTest.h"
#include "ClearanceDIS/ClearanceDISPDU.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISFirePDURoundtripTest,
	"Clearance.DIS.FirePDU.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISFirePDURoundtripTest::RunTest(const FString& Parameters)
{
	const std::uint16_t FiringEnt = ClearanceDIS::HashCallsignToEntityNumber("VIPER01");
	const std::uint16_t TargetEnt = ClearanceDIS::HashCallsignToEntityNumber("UNK001");

	ClearanceDIS::FFireEvent E;
	E.FiringEntity   = FiringEnt;
	E.TargetEntity   = TargetEnt;
	E.EventNumber    = 42;
	E.MunitionEntity = ClearanceDIS::DeriveMunitionEntityNumber(FiringEnt, E.EventNumber);
	E.XMeters = 120.0 * 1852.0;
	E.YMeters =  80.0 * 1852.0;
	E.ZMeters = 25000.0 * 0.3048;
	E.VxMps   = 500.f * 0.514444f;
	E.VyMps   = 300.f * 0.514444f;
	E.VzMps   = -50.f * 0.514444f;
	E.MunitionKind = 1;
	E.WarheadKind  = 1000;
	E.FuseKind     = 1000;
	E.Quantity     = 1;
	E.Rate         = 0;
	E.RangeMeters  = 20000.f;

	ClearanceDIS::FWireParams P;
	P.ExerciseId = 1; P.SiteId = 42; P.ApplicationId = 7; P.SimTimeSeconds = 128.5;
	const std::vector<std::uint8_t> Wire = ClearanceDIS::BuildFirePDU(E, P);

	TestEqual(TEXT("Fire PDU length matches §7.3.3 fixed 96 bytes"), int32(Wire.size()), 96);
	TestEqual(TEXT("PDU type is 2 (Fire)"), int32(Wire[2]), 2);
	TestEqual(TEXT("Proto family is 2 (Warfare)"), int32(Wire[3]), 2);

	ClearanceDIS::FFireEvent Out;
	TestTrue(TEXT("Parser accepts well-formed Fire PDU"),
		ClearanceDIS::ParseFirePDU(Wire.data(), Wire.size(), Out));

	TestEqual(TEXT("Firing entity round-trips"),   int32(Out.FiringEntity),   int32(E.FiringEntity));
	TestEqual(TEXT("Target entity round-trips"),   int32(Out.TargetEntity),   int32(E.TargetEntity));
	TestEqual(TEXT("Munition entity round-trips"), int32(Out.MunitionEntity), int32(E.MunitionEntity));
	TestEqual(TEXT("EventNumber round-trips"),     int32(Out.EventNumber),    int32(E.EventNumber));
	TestEqual(TEXT("MunitionKind round-trips"),    int32(Out.MunitionKind),   int32(E.MunitionKind));
	TestEqual(TEXT("WarheadKind round-trips"),     int32(Out.WarheadKind),    int32(E.WarheadKind));
	TestEqual(TEXT("FuseKind round-trips"),        int32(Out.FuseKind),       int32(E.FuseKind));
	TestEqual(TEXT("Quantity round-trips"),        int32(Out.Quantity),       int32(E.Quantity));
	TestTrue(TEXT("RangeMeters within 0.1"),       FMath::IsNearlyEqual(Out.RangeMeters, E.RangeMeters, 0.1f));

	TestTrue(TEXT("Location X within 0.001 m"), FMath::IsNearlyEqual(Out.XMeters, E.XMeters, 0.001));
	TestTrue(TEXT("Location Y within 0.001 m"), FMath::IsNearlyEqual(Out.YMeters, E.YMeters, 0.001));
	TestTrue(TEXT("Location Z within 0.001 m"), FMath::IsNearlyEqual(Out.ZMeters, E.ZMeters, 0.001));
	TestTrue(TEXT("Velocity X within 0.05 m/s"), FMath::IsNearlyEqual(Out.VxMps, E.VxMps, 0.05f));
	TestTrue(TEXT("Velocity Y within 0.05 m/s"), FMath::IsNearlyEqual(Out.VyMps, E.VyMps, 0.05f));
	TestTrue(TEXT("Velocity Z within 0.05 m/s"), FMath::IsNearlyEqual(Out.VzMps, E.VzMps, 0.05f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISFirePDUMalformedRejectionTest,
	"Clearance.DIS.FirePDU.MalformedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISFirePDUMalformedRejectionTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FFireEvent Out;

	std::vector<std::uint8_t> Bad(96, 0);
	Bad[0] = 6;
	Bad[2] = 23; Bad[3] = 6;                       // Emission posing as Fire
	TestFalse(TEXT("Rejects non-Fire PDU type"),
		ClearanceDIS::ParseFirePDU(Bad.data(), Bad.size(), Out));

	Bad.resize(10);
	TestFalse(TEXT("Rejects truncated buffer"),
		ClearanceDIS::ParseFirePDU(Bad.data(), Bad.size(), Out));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISDetonationPDURoundtripTest,
	"Clearance.DIS.DetonationPDU.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISDetonationPDURoundtripTest::RunTest(const FString& Parameters)
{
	const std::uint16_t FiringEnt = ClearanceDIS::HashCallsignToEntityNumber("VIPER01");
	const std::uint16_t TargetEnt = ClearanceDIS::HashCallsignToEntityNumber("UNK001");

	ClearanceDIS::FDetonationEvent E;
	E.FiringEntity   = FiringEnt;
	E.TargetEntity   = TargetEnt;
	E.EventNumber    = 42;
	E.MunitionEntity = ClearanceDIS::DeriveMunitionEntityNumber(FiringEnt, E.EventNumber);
	E.XMeters = 140.0 * 1852.0;
	E.YMeters =  90.0 * 1852.0;
	E.ZMeters = 22000.0 * 0.3048;
	E.VxMps   = 450.f * 0.514444f;
	E.VyMps   = 280.f * 0.514444f;
	E.VzMps   = -30.f * 0.514444f;
	E.MunitionKind = 1;
	E.WarheadKind  = 1000;
	E.FuseKind     = 1000;
	E.Quantity     = 1;
	E.DetonationResult = 2;                        // Entity Proximate Detonation

	ClearanceDIS::FWireParams P;
	P.ExerciseId = 1; P.SiteId = 42; P.ApplicationId = 7; P.SimTimeSeconds = 200.0;
	const std::vector<std::uint8_t> Wire = ClearanceDIS::BuildDetonationPDU(E, P);

	TestEqual(TEXT("Detonation PDU length matches §7.3.4 fixed 104 bytes"),
		int32(Wire.size()), 104);
	TestEqual(TEXT("PDU type is 3 (Detonation)"), int32(Wire[2]), 3);
	TestEqual(TEXT("Proto family is 2 (Warfare)"), int32(Wire[3]), 2);

	ClearanceDIS::FDetonationEvent Out;
	TestTrue(TEXT("Parser accepts well-formed Detonation PDU"),
		ClearanceDIS::ParseDetonationPDU(Wire.data(), Wire.size(), Out));

	TestEqual(TEXT("Firing entity round-trips"),   int32(Out.FiringEntity),   int32(E.FiringEntity));
	TestEqual(TEXT("Target entity round-trips"),   int32(Out.TargetEntity),   int32(E.TargetEntity));
	TestEqual(TEXT("Munition entity round-trips"), int32(Out.MunitionEntity), int32(E.MunitionEntity));
	TestEqual(TEXT("Event ID matches originating Fire"), int32(Out.EventNumber), int32(E.EventNumber));
	TestEqual(TEXT("Detonation Result round-trips"), int32(Out.DetonationResult), int32(E.DetonationResult));

	TestTrue(TEXT("Location X within 0.001 m"), FMath::IsNearlyEqual(Out.XMeters, E.XMeters, 0.001));
	TestTrue(TEXT("Location Y within 0.001 m"), FMath::IsNearlyEqual(Out.YMeters, E.YMeters, 0.001));
	TestTrue(TEXT("Location Z within 0.001 m"), FMath::IsNearlyEqual(Out.ZMeters, E.ZMeters, 0.001));
	TestTrue(TEXT("Velocity X within 0.05 m/s"), FMath::IsNearlyEqual(Out.VxMps, E.VxMps, 0.05f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISDetonationPDUMalformedRejectionTest,
	"Clearance.DIS.DetonationPDU.MalformedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISDetonationPDUMalformedRejectionTest::RunTest(const FString& Parameters)
{
	ClearanceDIS::FDetonationEvent Out;

	std::vector<std::uint8_t> Bad(104, 0);
	Bad[0] = 6;
	Bad[2] = 1; Bad[3] = 1;                        // Entity State posing as Detonation
	TestFalse(TEXT("Rejects non-Detonation PDU type"),
		ClearanceDIS::ParseDetonationPDU(Bad.data(), Bad.size(), Out));

	Bad.resize(5);
	TestFalse(TEXT("Rejects truncated buffer"),
		ClearanceDIS::ParseDetonationPDU(Bad.data(), Bad.size(), Out));

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
