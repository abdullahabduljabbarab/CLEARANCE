// Roundtrip tests for IEEE 1278.1 Signal PDU (Type 26). Same pattern as the
// Fire / Detonation / Emission tests: serialise a known snapshot, parse back,
// verify byte-perfect round-trip on every field that matters. - TripleA

#include "Misc/AutomationTest.h"
#include "Simulation/ClearanceDISEmitter.h"
#include "Core/CLEARANCETypes.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISSignalPDURoundtripTest,
	"Clearance.DIS.SignalPDU.Roundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISSignalPDURoundtripTest::RunTest(const FString& Parameters)
{
	// An operator command that would normally hit the phraseology parser -
	// pick a line that exercises digits, spaces, and punctuation.
	FVoiceCommsEvent Event;
	Event.SpeakerCallsign = TEXT("BAW472");
	Event.Transcript      = TEXT("Speedbird 472, turn right heading 270, descend flight level 100.");
	Event.RadioId         = 1;

	UClearanceDISEmitter* Emitter = NewObject<UClearanceDISEmitter>();
	Emitter->SiteId        = 42;
	Emitter->ApplicationId = 7;
	Emitter->ExerciseId    = 1;

	TArray<uint8> Wire;
	Emitter->BuildSignalPDU(Wire, Event, /*SimTime*/ 300.0f);

	// Signal PDU fixed body is 32 bytes (12 header + 20 body); the transcript
	// above is 64 bytes ASCII, which is already 32-bit aligned so no padding.
	// Total = 32 + 64 = 96.
	TestEqual(TEXT("Signal PDU length matches header + body + aligned payload"),
		Wire.Num(), 32 + 64);

	// PDU header sanity - the dissector-facing fields.
	TestEqual(TEXT("Protocol version is 6 (IEEE 1278.1A)"), int32(Wire[0]), 6);
	TestEqual(TEXT("PDU type is 26 (Signal)"), int32(Wire[2]), 26);
	TestEqual(TEXT("Protocol family is 4 (Radio Communications)"), int32(Wire[3]), 4);

	FVoiceCommsEvent RoundTrip;
	int32 SpeakerEntity = 0;
	const bool bParseOk = UClearanceDISEmitter::ParseSignalPDU(Wire, RoundTrip, SpeakerEntity);
	TestTrue(TEXT("Parser accepts a well-formed Signal PDU"), bParseOk);

	// Speaker entity number - stable per-callsign hash.
	const int32 ExpectedSpeaker = static_cast<int32>((GetTypeHash(Event.SpeakerCallsign) % 65535) + 1);
	TestEqual(TEXT("Speaker entity number matches stable hash"),
		SpeakerEntity, ExpectedSpeaker);

	// Radio ID + transcript round-trip.
	TestEqual(TEXT("Radio ID round-trips"), RoundTrip.RadioId, Event.RadioId);
	TestEqual(TEXT("Transcript payload round-trips byte-for-byte"),
		RoundTrip.Transcript, Event.Transcript);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISSignalPDUPaddingTest,
	"Clearance.DIS.SignalPDU.PaddingBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISSignalPDUPaddingTest::RunTest(const FString& Parameters)
{
	// Confirm §7.7.3.9 padding: data length in bits is the *unpadded* count,
	// the data field itself is padded up to a 32-bit boundary. Use a 5-byte
	// payload so the padding pushes to 8 bytes and the boundary is exercised
	// non-trivially. - TripleA
	FVoiceCommsEvent Event;
	Event.SpeakerCallsign = TEXT("VIPER01");
	Event.Transcript      = TEXT("Fox 2");         // 5 bytes, forces 3 bytes of pad
	Event.RadioId         = 2;

	UClearanceDISEmitter* Emitter = NewObject<UClearanceDISEmitter>();
	Emitter->SiteId        = 1;
	Emitter->ApplicationId = 1;
	Emitter->ExerciseId    = 1;

	TArray<uint8> Wire;
	Emitter->BuildSignalPDU(Wire, Event, /*SimTime*/ 100.0f);

	// 32 fixed + 8 (5 payload + 3 pad) = 40.
	TestEqual(TEXT("Padded to 32-bit boundary"), Wire.Num(), 40);

	// Parse still returns the ORIGINAL unpadded transcript, not the padded
	// byte count - proves the parser respects the data-length-in-bits field
	// and doesn't append pad bytes into the transcript string.
	FVoiceCommsEvent RoundTrip;
	int32 Ent = 0;
	TestTrue(TEXT("Parser accepts padded PDU"),
		UClearanceDISEmitter::ParseSignalPDU(Wire, RoundTrip, Ent));
	TestEqual(TEXT("Transcript excludes padding"),
		RoundTrip.Transcript, Event.Transcript);
	TestEqual(TEXT("Transcript length is 5 bytes not 8"),
		RoundTrip.Transcript.Len(), 5);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISSignalPDUOperatorTest,
	"Clearance.DIS.SignalPDU.OperatorEntity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISSignalPDUOperatorTest::RunTest(const FString& Parameters)
{
	// Operator lines (NAME_None speaker) get the reserved ground-station
	// entity number instead of a hashed callsign. Federation receivers can
	// filter ground chatter from air chatter using this. - TripleA
	FVoiceCommsEvent Event;
	Event.SpeakerCallsign = NAME_None;             // Operator says
	Event.Transcript      = TEXT("Traffic in your vicinity, one o'clock, five miles.");

	UClearanceDISEmitter* Emitter = NewObject<UClearanceDISEmitter>();
	TArray<uint8> Wire;
	Emitter->BuildSignalPDU(Wire, Event, 0.f);

	FVoiceCommsEvent RoundTrip;
	int32 SpeakerEntity = 0;
	TestTrue(TEXT("Parser accepts operator PDU"),
		UClearanceDISEmitter::ParseSignalPDU(Wire, RoundTrip, SpeakerEntity));
	TestEqual(TEXT("Operator uses fixed reserved entity number"),
		SpeakerEntity, 60000);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceDISSignalPDUMalformedRejectionTest,
	"Clearance.DIS.SignalPDU.MalformedRejection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceDISSignalPDUMalformedRejectionTest::RunTest(const FString& Parameters)
{
	FVoiceCommsEvent Out;
	int32 Entity = 0;

	// Wrong PDU type - a well-formed Fire PDU should be rejected by the
	// Signal parser.
	TArray<uint8> Bad;
	Bad.Init(0, 96);
	Bad[0] = 6;
	Bad[2] = 2;   // Fire posing as Signal
	Bad[3] = 2;
	TestFalse(TEXT("Parser rejects non-Signal PDU type"),
		UClearanceDISEmitter::ParseSignalPDU(Bad, Out, Entity));

	// Truncated - buffer smaller than the fixed header.
	Bad.SetNum(5);
	TestFalse(TEXT("Parser rejects truncated buffer"),
		UClearanceDISEmitter::ParseSignalPDU(Bad, Out, Entity));

	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
