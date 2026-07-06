// Roundtrip tests for IEEE 1278.1 Fire (Type 2) and Detonation (Type 3) PDUs.
// Same pattern as the Emission PDU tests: serialize a known snapshot, parse
// back, verify byte-perfect round-trip on every field that matters. - TripleA

#include "Misc/AutomationTest.h"
#include "Simulation/ClearanceDISEmitter.h"
#include "Core/CLEARANCETypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISFirePDURoundtripTest,
	"Clearance.DIS.FirePDU.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISFirePDURoundtripTest::RunTest(const FString& Parameters)
{
	// A VIPER launches a guided missile at a hostile bandit.
	FWeaponsFireEvent Event;
	Event.FiringCallsign = TEXT("VIPER01");
	Event.TargetCallsign = TEXT("UNK001");
	Event.LocationNm     = FVector2D(120.f, 80.f);
	Event.AltitudeFt     = 25000.f;
	Event.VelocityXKts   = 500.f;
	Event.VelocityYKts   = 300.f;
	Event.VelocityZKts   = -50.f;
	Event.MunitionKind   = 1;      // Guided missile
	Event.WarheadKind    = 1000;   // HE
	Event.FuseKind       = 1000;   // Contact
	Event.Quantity       = 1;
	Event.Rate           = 0;
	Event.RangeMeters    = 20000.f;
	Event.EventNumber    = 42;

	UClearanceDISEmitter* Emitter = NewObject<UClearanceDISEmitter>();
	Emitter->SiteId        = 42;
	Emitter->ApplicationId = 7;
	Emitter->ExerciseId    = 1;

	TArray<uint8> Wire;
	Emitter->BuildFirePDU(Wire, Event, /*SimTime*/ 128.5f);

	TestEqual(TEXT("Fire PDU byte length matches IEEE 1278.1 §7.3.3 fixed 96 bytes"),
		Wire.Num(), 96);

	FWeaponsFireEvent RoundTrip;
	int32 FiringEntity = 0, TargetEntity = 0, MunitionEntity = 0;
	const bool bParseOk = UClearanceDISEmitter::ParseFirePDU(
		Wire, RoundTrip, FiringEntity, TargetEntity, MunitionEntity);
	TestTrue(TEXT("Parser accepts a well-formed Fire PDU"), bParseOk);

	// Entity numbers stable per callsign hash
	const int32 ExpectedFiring = static_cast<int32>((GetTypeHash(Event.FiringCallsign) % 65535) + 1);
	const int32 ExpectedTarget = static_cast<int32>((GetTypeHash(Event.TargetCallsign) % 65535) + 1);
	TestEqual(TEXT("Firing entity number matches stable hash"), FiringEntity, ExpectedFiring);
	TestEqual(TEXT("Target entity number matches stable hash"), TargetEntity, ExpectedTarget);
	TestNotEqual(TEXT("Munition entity number is non-zero"), MunitionEntity, 0);

	// Field roundtrip
	TestEqual(TEXT("MunitionKind round-trips"), RoundTrip.MunitionKind, Event.MunitionKind);
	TestEqual(TEXT("WarheadKind round-trips"), RoundTrip.WarheadKind, Event.WarheadKind);
	TestEqual(TEXT("FuseKind round-trips"), RoundTrip.FuseKind, Event.FuseKind);
	TestEqual(TEXT("Quantity round-trips"), RoundTrip.Quantity, Event.Quantity);
	TestEqual(TEXT("Rate round-trips"), RoundTrip.Rate, Event.Rate);
	TestEqual(TEXT("EventNumber (low 16 bits) round-trips"),
		RoundTrip.EventNumber, Event.EventNumber);

	TestTrue(TEXT("RangeMeters within 0.1"),
		FMath::IsNearlyEqual(RoundTrip.RangeMeters, Event.RangeMeters, 0.1f));

	// Location goes through metres <-> nm double conversion - tolerance is very loose
	TestTrue(TEXT("Location X within 0.01 nm"),
		FMath::IsNearlyEqual(RoundTrip.LocationNm.X, Event.LocationNm.X, 0.01f));
	TestTrue(TEXT("Location Y within 0.01 nm"),
		FMath::IsNearlyEqual(RoundTrip.LocationNm.Y, Event.LocationNm.Y, 0.01f));
	TestTrue(TEXT("Altitude within 0.01 ft"),
		FMath::IsNearlyEqual(RoundTrip.AltitudeFt, Event.AltitudeFt, 0.01f));

	// Velocity kts -> m/s -> kts float precision
	TestTrue(TEXT("Velocity X within 0.1 kt"),
		FMath::IsNearlyEqual(RoundTrip.VelocityXKts, Event.VelocityXKts, 0.1f));
	TestTrue(TEXT("Velocity Y within 0.1 kt"),
		FMath::IsNearlyEqual(RoundTrip.VelocityYKts, Event.VelocityYKts, 0.1f));
	TestTrue(TEXT("Velocity Z within 0.1 kt"),
		FMath::IsNearlyEqual(RoundTrip.VelocityZKts, Event.VelocityZKts, 0.1f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISFirePDUMalformedRejectionTest,
	"Clearance.DIS.FirePDU.MalformedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISFirePDUMalformedRejectionTest::RunTest(const FString& Parameters)
{
	FWeaponsFireEvent Out;
	int32 F = 0, T = 0, M = 0;

	// Wrong PDU type
	TArray<uint8> Bad;
	Bad.Init(0, 96);
	Bad[0] = 6;
	Bad[2] = 23;  // Emission posing as Fire
	Bad[3] = 6;
	TestFalse(TEXT("Parser rejects non-Fire PDU type"),
		UClearanceDISEmitter::ParseFirePDU(Bad, Out, F, T, M));

	// Truncated
	Bad.SetNum(10);
	TestFalse(TEXT("Parser rejects truncated buffer"),
		UClearanceDISEmitter::ParseFirePDU(Bad, Out, F, T, M));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISDetonationPDURoundtripTest,
	"Clearance.DIS.DetonationPDU.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISDetonationPDURoundtripTest::RunTest(const FString& Parameters)
{
	FWeaponsDetonationEvent Event;
	Event.FiringCallsign    = TEXT("VIPER01");
	Event.TargetCallsign    = TEXT("UNK001");
	Event.LocationNm        = FVector2D(140.f, 90.f);
	Event.AltitudeFt        = 22000.f;
	Event.VelocityXKts      = 450.f;
	Event.VelocityYKts      = 280.f;
	Event.VelocityZKts      = -30.f;
	Event.MunitionKind      = 1;
	Event.WarheadKind       = 1000;
	Event.FuseKind          = 1000;
	Event.Quantity          = 1;
	Event.Rate              = 0;
	Event.DetonationResult  = 1;   // Entity Impact
	Event.EventNumber       = 42;

	UClearanceDISEmitter* Emitter = NewObject<UClearanceDISEmitter>();
	Emitter->SiteId        = 42;
	Emitter->ApplicationId = 7;
	Emitter->ExerciseId    = 1;

	TArray<uint8> Wire;
	Emitter->BuildDetonationPDU(Wire, Event, /*SimTime*/ 200.0f);

	TestEqual(TEXT("Detonation PDU byte length matches IEEE 1278.1 §7.3.4 fixed 104 bytes"),
		Wire.Num(), 104);

	FWeaponsDetonationEvent RoundTrip;
	int32 F = 0, T = 0, M = 0;
	const bool bParseOk = UClearanceDISEmitter::ParseDetonationPDU(Wire, RoundTrip, F, T, M);
	TestTrue(TEXT("Parser accepts a well-formed Detonation PDU"), bParseOk);

	TestEqual(TEXT("Firing entity matches stable hash"),
		F, static_cast<int32>((GetTypeHash(Event.FiringCallsign) % 65535) + 1));
	TestEqual(TEXT("Target entity matches stable hash"),
		T, static_cast<int32>((GetTypeHash(Event.TargetCallsign) % 65535) + 1));

	TestEqual(TEXT("MunitionKind round-trips"), RoundTrip.MunitionKind, Event.MunitionKind);
	TestEqual(TEXT("DetonationResult round-trips"), RoundTrip.DetonationResult, Event.DetonationResult);
	TestEqual(TEXT("EventNumber (low 16 bits) round-trips"),
		RoundTrip.EventNumber, Event.EventNumber);
	TestEqual(TEXT("WarheadKind round-trips"), RoundTrip.WarheadKind, Event.WarheadKind);
	TestEqual(TEXT("FuseKind round-trips"), RoundTrip.FuseKind, Event.FuseKind);
	TestEqual(TEXT("Quantity round-trips"), RoundTrip.Quantity, Event.Quantity);

	TestTrue(TEXT("Location X within 0.01 nm"),
		FMath::IsNearlyEqual(RoundTrip.LocationNm.X, Event.LocationNm.X, 0.01f));
	TestTrue(TEXT("Location Y within 0.01 nm"),
		FMath::IsNearlyEqual(RoundTrip.LocationNm.Y, Event.LocationNm.Y, 0.01f));
	TestTrue(TEXT("Altitude within 0.01 ft"),
		FMath::IsNearlyEqual(RoundTrip.AltitudeFt, Event.AltitudeFt, 0.01f));
	TestTrue(TEXT("Velocity X within 0.1 kt"),
		FMath::IsNearlyEqual(RoundTrip.VelocityXKts, Event.VelocityXKts, 0.1f));
	TestTrue(TEXT("Velocity Y within 0.1 kt"),
		FMath::IsNearlyEqual(RoundTrip.VelocityYKts, Event.VelocityYKts, 0.1f));
	TestTrue(TEXT("Velocity Z within 0.1 kt"),
		FMath::IsNearlyEqual(RoundTrip.VelocityZKts, Event.VelocityZKts, 0.1f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISDetonationPDUMalformedRejectionTest,
	"Clearance.DIS.DetonationPDU.MalformedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISDetonationPDUMalformedRejectionTest::RunTest(const FString& Parameters)
{
	FWeaponsDetonationEvent Out;
	int32 F = 0, T = 0, M = 0;

	TArray<uint8> Bad;
	Bad.Init(0, 104);
	Bad[0] = 6;
	Bad[2] = 1;   // Entity State posing as Detonation
	Bad[3] = 1;
	TestFalse(TEXT("Parser rejects non-Detonation PDU type"),
		UClearanceDISEmitter::ParseDetonationPDU(Bad, Out, F, T, M));

	Bad.SetNum(5);
	TestFalse(TEXT("Parser rejects truncated buffer"),
		UClearanceDISEmitter::ParseDetonationPDU(Bad, Out, F, T, M));

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
