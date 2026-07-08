// UClearanceSessionRecorder tests. Recorder is a UObject data layer for the
// After-Action Review timeline - captures aircraft-state snapshots per tick
// + timestamped events, replays them by seeking. Testable without a world
// or a controller since ApplySnapshotTo (the only actor-touching method) is
// out of scope for these unit tests. - TripleA
//
// Covers requirements:
//   REQ-SIM-001  StartRecording/StopRecording shall toggle IsRecording flag.
//   REQ-SIM-002  CaptureSnapshot shall append one snapshot entry per call
//                during recording, and be a no-op when not recording.
//   REQ-SIM-003  GetDurationSeconds shall return (last-timestamp minus
//                first-timestamp); zero for empty timelines.
//   REQ-SIM-004  FindSnapshotAt(secondsFromStart) shall return the most
//                recent snapshot whose absolute timestamp is at-or-before
//                (firstTimestamp + secondsFromStart). Clamps to first
//                snapshot on negative input, clamps to last snapshot on
//                overshoots. Returns nullptr only when the timeline is
//                empty.
//   REQ-SIM-005  Snapshot data shall round-trip: for every aircraft in a
//                captured snapshot, seek back and verify the retrieved
//                FAircraftState fields are identical to the input.
//   REQ-SIM-006  LogEvent shall append to Events; GetEventsInRange shall
//                filter by SimTime window inclusively.
//   REQ-SIM-007  ClearRecording shall empty snapshots + events. It leaves
//                the IsRecording flag untouched - callers who want to
//                fully stop should call StopRecording() first.

#include "Misc/AutomationTest.h"
#include "Simulation/ClearanceSessionRecorder.h"
#include "Core/CLEARANCETypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FAircraftState MakeStateAt(const FString& Callsign, float Alt, float Spd, float Hdg)
	{
		FAircraftState S;
		S.bIsValid   = true;
		S.Callsign   = FName(*Callsign);
		S.Altitude   = Alt;
		S.Speed      = Spd;
		S.Heading    = Hdg;
		S.FlightPhase = EFlightPhase::Enroute;
		return S;
	}
}

// Covers: REQ-SIM-001, REQ-SIM-002 - toggle recording, capture only while on.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRecorderStartStopTest,
	"Clearance.Recorder.StartStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRecorderStartStopTest::RunTest(const FString& Parameters)
{
	UClearanceSessionRecorder* R = NewObject<UClearanceSessionRecorder>();
	TestFalse(TEXT("Recorder starts idle"), R->IsRecording());
	TestEqual(TEXT("Fresh snapshot count zero"), R->GetSnapshotCount(), 0);

	R->StartRecording();
	TestTrue(TEXT("IsRecording true after StartRecording"), R->IsRecording());

	R->CaptureSnapshot(1.0f, { MakeStateAt(TEXT("DLH101"), 30000.f, 280.f, 90.f) });
	TestEqual(TEXT("Snapshot appended while recording"), R->GetSnapshotCount(), 1);

	R->StopRecording();
	TestFalse(TEXT("IsRecording false after StopRecording"), R->IsRecording());

	R->CaptureSnapshot(2.0f, { MakeStateAt(TEXT("DLH101"), 30000.f, 280.f, 90.f) });
	TestEqual(TEXT("Snapshot NOT appended after StopRecording (still 1)"),
		R->GetSnapshotCount(), 1);
	return true;
}

// Covers: REQ-SIM-003 - duration tracks the latest snapshot time.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRecorderDurationTest,
	"Clearance.Recorder.Duration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRecorderDurationTest::RunTest(const FString& Parameters)
{
	UClearanceSessionRecorder* R = NewObject<UClearanceSessionRecorder>();
	TestTrue(TEXT("Empty duration is zero"),
		FMath::IsNearlyZero(R->GetDurationSeconds()));

	R->StartRecording();
	R->CaptureSnapshot(1.5f,  { MakeStateAt(TEXT("DLH101"), 30000.f, 280.f, 90.f) });
	R->CaptureSnapshot(4.25f, { MakeStateAt(TEXT("DLH101"), 30500.f, 285.f, 92.f) });
	R->CaptureSnapshot(10.0f, { MakeStateAt(TEXT("DLH101"), 31000.f, 290.f, 95.f) });

	// Duration is last-first: 10.0 - 1.5 = 8.5. - TripleA
	TestTrue(TEXT("Duration is (last-first) timestamp span"),
		FMath::IsNearlyEqual(R->GetDurationSeconds(), 8.5f, 0.001f));
	return true;
}

// Covers: REQ-SIM-004, REQ-SIM-005 - seek roundtrip. Capture a snapshot,
// seek back, verify aircraft state fields identical.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRecorderPoseBackTest,
	"Clearance.Recorder.PoseBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRecorderPoseBackTest::RunTest(const FString& Parameters)
{
	UClearanceSessionRecorder* R = NewObject<UClearanceSessionRecorder>();
	R->StartRecording();

	// Capture at absolute t=5s (this becomes t=0 in the "seconds-from-start"
	// lookup coordinate), then again at t=10s (= seconds-from-start 5). The
	// FindSnapshotAt API is seconds-from-start, not absolute. - TripleA
	const FAircraftState Early = MakeStateAt(TEXT("DLH101"), 30000.f, 280.f, 90.f);
	R->CaptureSnapshot(5.0f, { Early });

	const FAircraftState Later = MakeStateAt(TEXT("DLH101"), 32000.f, 300.f, 180.f);
	R->CaptureSnapshot(10.0f, { Later });

	// Seek to seconds-from-start = 2 (absolute t=7, between the two snapshots).
	// Should return the earlier snapshot (most-recent-at-or-before rule). - TripleA
	const FRecordedSnapshot* Mid = R->FindSnapshotAt(2.0f);
	TestNotNull(TEXT("Snapshot exists at seconds-from-start=2"), Mid);
	if (Mid)
	{
		TestEqual(TEXT("Mid seek picks the earlier snapshot's aircraft count"),
			Mid->States.Num(), 1);
		if (Mid->States.Num() > 0)
		{
			TestEqual(TEXT("Mid seek altitude matches early snapshot"),
				Mid->States[0].Altitude, Early.Altitude);
			TestEqual(TEXT("Mid seek speed matches early snapshot"),
				Mid->States[0].Speed, Early.Speed);
			TestEqual(TEXT("Mid seek heading matches early snapshot"),
				Mid->States[0].Heading, Early.Heading);
			TestEqual(TEXT("Mid seek callsign matches early snapshot"),
				Mid->States[0].Callsign, Early.Callsign);
		}
	}

	// Seek past end (seconds-from-start=100 = absolute t=105): clamps to last.
	const FRecordedSnapshot* Late = R->FindSnapshotAt(100.0f);
	TestNotNull(TEXT("Snapshot exists at seconds-from-start=100 (past end)"), Late);
	if (Late && Late->States.Num() > 0)
	{
		TestEqual(TEXT("Late seek altitude matches later snapshot"),
			Late->States[0].Altitude, Later.Altitude);
	}

	// Negative seek: clamps to first. FindSnapshotAt uses FMath::Max(0, x)
	// internally, so any negative input maps to the first snapshot. - TripleA
	const FRecordedSnapshot* Neg = R->FindSnapshotAt(-5.0f);
	TestNotNull(TEXT("Negative seek clamps to first snapshot (non-null)"), Neg);
	if (Neg && Neg->States.Num() > 0)
	{
		TestEqual(TEXT("Negative seek returns early snapshot altitude"),
			Neg->States[0].Altitude, Early.Altitude);
	}

	// Empty timeline: should return nullptr.
	UClearanceSessionRecorder* Empty = NewObject<UClearanceSessionRecorder>();
	TestNull(TEXT("Empty timeline seek returns nullptr"), Empty->FindSnapshotAt(0.f));

	return true;
}

// Covers: REQ-SIM-006 - events filter by inclusive time range.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRecorderEventsInRangeTest,
	"Clearance.Recorder.EventsInRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRecorderEventsInRangeTest::RunTest(const FString& Parameters)
{
	UClearanceSessionRecorder* R = NewObject<UClearanceSessionRecorder>();
	R->StartRecording();
	R->LogEvent(1.0f,  TEXT("Scenario started"));
	R->LogEvent(5.0f,  TEXT("Vector DLH101 heading 210"));
	R->LogEvent(9.5f,  TEXT("Clear approach"));
	R->LogEvent(12.0f, TEXT("Landed"));

	// Range [4, 10] should include events at 5 and 9.5, exclude events at
	// 1 and 12.
	const TArray<FRecordedEvent> Mid = R->GetEventsInRange(4.f, 10.f);
	TestEqual(TEXT("Range [4,10] contains 2 events"), Mid.Num(), 2);

	// Wide range covers all.
	const TArray<FRecordedEvent> All = R->GetEventsInRange(0.f, 20.f);
	TestEqual(TEXT("Range [0,20] contains all 4 events"), All.Num(), 4);

	// Empty range.
	const TArray<FRecordedEvent> None = R->GetEventsInRange(100.f, 200.f);
	TestEqual(TEXT("Range [100,200] contains 0 events"), None.Num(), 0);

	return true;
}

// Covers: REQ-SIM-007 - ClearRecording wipes state.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRecorderClearTest,
	"Clearance.Recorder.Clear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRecorderClearTest::RunTest(const FString& Parameters)
{
	UClearanceSessionRecorder* R = NewObject<UClearanceSessionRecorder>();
	R->StartRecording();
	R->CaptureSnapshot(1.0f, { MakeStateAt(TEXT("DLH101"), 30000.f, 280.f, 90.f) });
	R->LogEvent(1.5f, TEXT("test event"));
	TestTrue(TEXT("Non-zero snapshot count before clear"), R->GetSnapshotCount() > 0);
	TestTrue(TEXT("Non-zero events before clear"), R->GetAllEvents().Num() > 0);

	R->ClearRecording();

	TestEqual(TEXT("Snapshot count zero after clear"), R->GetSnapshotCount(), 0);
	TestEqual(TEXT("Events empty after clear"), R->GetAllEvents().Num(), 0);
	// ClearRecording is data-only - leaves the IsRecording flag untouched.
	// Caller who wants to halt should StopRecording() first. - TripleA
	TestTrue(TEXT("Still recording after clear (data-only wipe)"), R->IsRecording());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
