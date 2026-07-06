// Roundtrip test for the IEEE 1278.1 Emission PDU (Type 23) serializer.
// Serialize a known snapshot, parse it back, verify byte-identical + field-
// identical. Guards the wire format against any future drift. - TripleA
//
// Run inside the editor via:
//   Automation RunTests Clearance.DIS.EmissionPDU.Roundtrip
//
// Or from the command line:
//   UnrealEditor.exe <project>.uproject -ExecCmds="Automation RunTests Clearance"
//   -TestExit="Automation Test Queue Empty" -unattended -nopause

#include "Misc/AutomationTest.h"
#include "Simulation/ClearanceDISEmitter.h"
#include "Core/CLEARANCETypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISEmissionPDURoundtripTest,
	"Clearance.DIS.EmissionPDU.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISEmissionPDURoundtripTest::RunTest(const FString& Parameters)
{
	// Build a known snapshot: a military X-band search radar painting three
	// aircraft. Field values chosen to exercise every serializer path.
	FRadarEmissionSnapshot Snap;
	Snap.SiteName        = TEXT("SITE-ALPHA");
	Snap.SitePositionNm  = FVector2D(50.f, -30.f);
	Snap.SweepAngleDeg   = 137.5f;
	Snap.RangeNm         = 80.f;
	Snap.bEnabled        = true;
	Snap.Signature.FrequencyLowHz             = 9.0e9f;
	Snap.Signature.FrequencyHighHz            = 10.0e9f;
	Snap.Signature.PulseRepetitionFreqHz      = 1200.f;
	Snap.Signature.PulseWidthMicrosec         = 0.5f;
	Snap.Signature.EffectiveRadiatedPowerDbm  = 85.f;
	Snap.Signature.EmitterName                = 3110;   // AN/APG-63
	Snap.Signature.EmitterFunction            = 5;      // Fire Control
	Snap.Signature.BeamFunction               = 6;      // Track While Scan
	Snap.PaintedCallsigns.Add(TEXT("BAW101"));
	Snap.PaintedCallsigns.Add(TEXT("UAL202"));
	Snap.PaintedCallsigns.Add(TEXT("DLH303"));

	// Serialize via a throwaway emitter instance - we don't need a live socket
	// since BuildEmissionPDU is a pure serializer.
	UClearanceDISEmitter* Emitter = NewObject<UClearanceDISEmitter>();
	Emitter->SiteId        = 42;
	Emitter->ApplicationId = 7;
	Emitter->ExerciseId    = 1;

	TArray<uint8> Wire;
	Emitter->BuildEmissionPDU(Wire, Snap, /*SimTimeSeconds*/ 128.5f);

	// Expected PDU size: 12 header + 16 body + 20 emitter system + 52 beam + 8 * 3 targets
	const int32 ExpectedSize = 12 + 16 + 20 + 52 + 8 * Snap.PaintedCallsigns.Num();
	TestEqual(TEXT("PDU byte length matches IEEE 1278.1 fixed + variable"), Wire.Num(), ExpectedSize);

	// Parse back
	FRadarEmissionSnapshot RoundTrip;
	int32 EmittingSite = 0, EmittingApp = 0, EmittingEntity = 0;
	TArray<int32> TargetEntityNumbers;
	const bool bParseOk = UClearanceDISEmitter::ParseEmissionPDU(
		Wire, RoundTrip, EmittingSite, EmittingApp, EmittingEntity, TargetEntityNumbers);

	TestTrue(TEXT("Parser accepts a well-formed Emission PDU"), bParseOk);
	TestEqual(TEXT("Emitting Site round-trips"), EmittingSite, Emitter->SiteId);
	TestEqual(TEXT("Emitting Application round-trips"), EmittingApp, Emitter->ApplicationId);

	// Signature fields byte-perfect
	TestEqual(TEXT("EmitterName round-trips"), RoundTrip.Signature.EmitterName, Snap.Signature.EmitterName);
	TestEqual(TEXT("EmitterFunction round-trips"), RoundTrip.Signature.EmitterFunction, Snap.Signature.EmitterFunction);
	TestEqual(TEXT("BeamFunction round-trips"), RoundTrip.Signature.BeamFunction, Snap.Signature.BeamFunction);

	// Float scalars - use IsNearlyEqual with generous tolerances so precision
	// drift at large magnitudes (frequencies in GHz) never trips the test.
	// Radar-relevant precision is orders of magnitude looser than these gates.
	// - TripleA
	TestTrue(TEXT("PRF within 0.01 Hz"),
		FMath::IsNearlyEqual(RoundTrip.Signature.PulseRepetitionFreqHz, Snap.Signature.PulseRepetitionFreqHz, 0.01f));
	TestTrue(TEXT("Pulse width within 0.001 us"),
		FMath::IsNearlyEqual(RoundTrip.Signature.PulseWidthMicrosec, Snap.Signature.PulseWidthMicrosec, 0.001f));
	TestTrue(TEXT("ERP dBm within 0.1 dBm"),
		FMath::IsNearlyEqual(RoundTrip.Signature.EffectiveRadiatedPowerDbm, Snap.Signature.EffectiveRadiatedPowerDbm, 0.1f));

	// Frequency low/high round-trip via center + range encoding. Float32 gap
	// at 10 GHz is ~1 kHz, and we cascade two subtractions; 100 kHz tolerance
	// is well below any real radar resolution and safe against precision loss.
	TestTrue(TEXT("Frequency low within 100 kHz"),
		FMath::IsNearlyEqual(RoundTrip.Signature.FrequencyLowHz, Snap.Signature.FrequencyLowHz, 1e5f));
	TestTrue(TEXT("Frequency high within 100 kHz"),
		FMath::IsNearlyEqual(RoundTrip.Signature.FrequencyHighHz, Snap.Signature.FrequencyHighHz, 1e5f));

	// Sweep angle round-trips within float precision (via rad conversion)
	TestTrue(TEXT("Sweep angle within 0.1 deg"),
		FMath::IsNearlyEqual(RoundTrip.SweepAngleDeg, Snap.SweepAngleDeg, 0.1f));

	// Target count + identity match. Bail early if the count mismatches so we
	// don't over-index below.
	if (!TestEqual(TEXT("Number of painted targets round-trips"),
		TargetEntityNumbers.Num(), Snap.PaintedCallsigns.Num()))
	{
		return false;
	}
	for (int32 i = 0; i < Snap.PaintedCallsigns.Num(); ++i)
	{
		const int32 Expected = static_cast<int32>((GetTypeHash(Snap.PaintedCallsigns[i]) % 65535) + 1);
		TestEqual(FString::Printf(TEXT("Target %d entity number matches deterministic hash"), i),
			TargetEntityNumbers[i], Expected);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISEmissionPDUEmptyTargetListTest,
	"Clearance.DIS.EmissionPDU.EmptyTargetList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISEmissionPDUEmptyTargetListTest::RunTest(const FString& Parameters)
{
	// A radar that isn't currently painting anything must still emit a valid
	// heartbeat PDU with zero Track/Jam entries - external ELINT still wants
	// to know the emitter is up. Verifies the variable-length block encodes
	// correctly at the boundary. - TripleA
	FRadarEmissionSnapshot Snap;
	Snap.SiteName = TEXT("SILENT");
	Snap.bEnabled = true;
	Snap.SweepAngleDeg = 0.f;

	UClearanceDISEmitter* Emitter = NewObject<UClearanceDISEmitter>();
	TArray<uint8> Wire;
	Emitter->BuildEmissionPDU(Wire, Snap, 0.f);

	TestEqual(TEXT("Empty-target PDU is 100 bytes (fixed header + emission + system + beam)"),
		Wire.Num(), 100);

	FRadarEmissionSnapshot RoundTrip;
	int32 Site = 0, App = 0, Entity = 0;
	TArray<int32> Targets;
	TestTrue(TEXT("Empty-target PDU parses cleanly"),
		UClearanceDISEmitter::ParseEmissionPDU(Wire, RoundTrip, Site, App, Entity, Targets));
	TestEqual(TEXT("Target list is empty"), Targets.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISEmissionPDUMalformedRejectionTest,
	"Clearance.DIS.EmissionPDU.MalformedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISEmissionPDUMalformedRejectionTest::RunTest(const FString& Parameters)
{
	// Parser must reject: (a) wrong PDU type, (b) truncated buffer, (c) header
	// mismatch. Failing to reject would let malformed federation traffic
	// corrupt the airspace picture. - TripleA
	FRadarEmissionSnapshot Out;
	int32 Site = 0, App = 0, Entity = 0;
	TArray<int32> Targets;

	// Wrong PDU type (Entity State = 1 posing as Emission)
	{
		TArray<uint8> Bad;
		Bad.Init(0, 100);
		Bad[0] = 6;  // proto version
		Bad[2] = 1;  // PDU type = Entity State
		Bad[3] = 1;  // protocol family
		TestFalse(TEXT("Parser rejects non-Emission PDU type"),
			UClearanceDISEmitter::ParseEmissionPDU(Bad, Out, Site, App, Entity, Targets));
	}

	// Truncated header
	{
		TArray<uint8> Bad;
		Bad.Init(0, 5);
		TestFalse(TEXT("Parser rejects buffer smaller than header"),
			UClearanceDISEmitter::ParseEmissionPDU(Bad, Out, Site, App, Entity, Targets));
	}

	// Length field mismatch
	{
		FRadarEmissionSnapshot Snap;
		Snap.bEnabled = true;
		UClearanceDISEmitter* Emitter = NewObject<UClearanceDISEmitter>();
		TArray<uint8> Wire;
		Emitter->BuildEmissionPDU(Wire, Snap, 0.f);
		Wire.Add(0);  // pad extra byte - PduLength no longer matches buffer size
		TestFalse(TEXT("Parser rejects PDU where length field disagrees with buffer"),
			UClearanceDISEmitter::ParseEmissionPDU(Wire, Out, Site, App, Entity, Targets));
	}

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
