// Scoring subsystem tests. Exercises UClearanceScoring directly - it's a plain
// UObject with no actor dependency so NewObject<>() in the test transient
// package is enough to instantiate. No world, no controller, no timers. - TripleA
//
// Covers requirements:
//   REQ-SCORE-001  Every EIncidentType shall map to a point delta that matches
//                  the scoring table.
//   REQ-SCORE-002  LogIncident shall append one FIncidentRecord to the session
//                  log per call.
//   REQ-SCORE-003  ResetSession shall clear score, log, and derived counters.
//   REQ-SCORE-004  Successful handoffs shall shrink the spawn interval toward
//                  MinSpawnIntervalSeconds by DifficultySecondsPerHandled.
//   REQ-SCORE-005  Difficulty shall clamp to [MinSpawnIntervalSeconds,
//                  MaxSpawnIntervalSeconds].

#include "Misc/AutomationTest.h"
#include "Scoring/ClearanceScoring.h"
#include "Core/CLEARANCETypes.h"

#if WITH_DEV_AUTOMATION_TESTS

// Covers: REQ-SCORE-002 - single LogIncident appends one record.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceScoringIncidentLogTest,
	"Clearance.Scoring.IncidentLog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceScoringIncidentLogTest::RunTest(const FString& Parameters)
{
	UClearanceScoring* Scoring = NewObject<UClearanceScoring>();
	TestNotNull(TEXT("Scoring instance"), Scoring);
	TestEqual(TEXT("Fresh log is empty"), Scoring->GetSessionLog().Num(), 0);

	Scoring->LogIncident(EIncidentType::SeparationLoss,
		FName("DLH101"), FName("BAW102"), TEXT("2.4 nm"));

	const TArray<FIncidentRecord> Log = Scoring->GetSessionLog();
	TestEqual(TEXT("Log has one entry after one LogIncident"), Log.Num(), 1);
	TestEqual(TEXT("Entry type is SeparationLoss"),
		int32(Log[0].Type), int32(EIncidentType::SeparationLoss));
	TestEqual(TEXT("Entry AircraftA callsign"), Log[0].AircraftA, FName("DLH101"));
	TestEqual(TEXT("Entry AircraftB callsign"), Log[0].AircraftB, FName("BAW102"));

	Scoring->LogIncident(EIncidentType::SuccessfulLanding,
		FName("BAW107"), NAME_None, TEXT(""));
	TestEqual(TEXT("Two LogIncident calls -> log size 2"),
		Scoring->GetSessionLog().Num(), 2);

	return true;
}

// Covers: REQ-SCORE-001 - each EIncidentType applies the correct point delta.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceScoringPointsPerIncidentTest,
	"Clearance.Scoring.PointsPerIncident",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceScoringPointsPerIncidentTest::RunTest(const FString& Parameters)
{
	// Reward events first - each should push score into positive territory
	// by the exact defined magnitude. - TripleA
	{
		UClearanceScoring* S = NewObject<UClearanceScoring>();
		S->LogIncident(EIncidentType::SuccessfulLanding, NAME_None, NAME_None, TEXT(""));
		TestEqual(TEXT("SuccessfulLanding adds PointsLanding"),
			S->GetCurrentScore(), S->PointsLanding);
	}
	{
		UClearanceScoring* S = NewObject<UClearanceScoring>();
		S->LogIncident(EIncidentType::SuccessfulHandoff, NAME_None, NAME_None, TEXT(""));
		TestEqual(TEXT("SuccessfulHandoff adds PointsHandoff"),
			S->GetCurrentScore(), S->PointsHandoff);
	}
	{
		UClearanceScoring* S = NewObject<UClearanceScoring>();
		S->LogIncident(EIncidentType::SuccessfulResolution, NAME_None, NAME_None, TEXT(""));
		TestEqual(TEXT("SuccessfulResolution adds PointsResolution"),
			S->GetCurrentScore(), S->PointsResolution);
	}
	{
		UClearanceScoring* S = NewObject<UClearanceScoring>();
		S->LogIncident(EIncidentType::SuccessfulIntercept, NAME_None, NAME_None, TEXT(""));
		TestEqual(TEXT("SuccessfulIntercept adds PointsIntercept"),
			S->GetCurrentScore(), S->PointsIntercept);
	}

	// Penalty events - score goes negative by the configured penalty.
	{
		UClearanceScoring* S = NewObject<UClearanceScoring>();
		S->LogIncident(EIncidentType::SeparationLoss, NAME_None, NAME_None, TEXT(""));
		TestEqual(TEXT("SeparationLoss subtracts PenaltySeparationLoss"),
			S->GetCurrentScore(), -S->PenaltySeparationLoss);
	}
	{
		UClearanceScoring* S = NewObject<UClearanceScoring>();
		S->LogIncident(EIncidentType::GoAroundTriggered, NAME_None, NAME_None, TEXT(""));
		TestEqual(TEXT("GoAroundTriggered subtracts PenaltyGoAround"),
			S->GetCurrentScore(), -S->PenaltyGoAround);
	}
	{
		UClearanceScoring* S = NewObject<UClearanceScoring>();
		S->LogIncident(EIncidentType::WakeEncounter, NAME_None, NAME_None, TEXT(""));
		TestEqual(TEXT("WakeEncounter subtracts PenaltyWakeEncounter"),
			S->GetCurrentScore(), -S->PenaltyWakeEncounter);
	}
	{
		UClearanceScoring* S = NewObject<UClearanceScoring>();
		S->LogIncident(EIncidentType::TCASResolutionAdvisory, NAME_None, NAME_None, TEXT(""));
		TestEqual(TEXT("TCAS RA subtracts PenaltyTCASResolutionAdvisory"),
			S->GetCurrentScore(), -S->PenaltyTCASResolutionAdvisory);
	}
	{
		UClearanceScoring* S = NewObject<UClearanceScoring>();
		S->LogIncident(EIncidentType::MisidentifiedCivilian, NAME_None, NAME_None, TEXT(""));
		TestEqual(TEXT("MisidentifiedCivilian subtracts catastrophic PenaltyMisidentifiedCivilian"),
			S->GetCurrentScore(), -S->PenaltyMisidentifiedCivilian);
	}
	{
		UClearanceScoring* S = NewObject<UClearanceScoring>();
		S->LogIncident(EIncidentType::ViolationZoneBreached, NAME_None, NAME_None, TEXT(""));
		TestEqual(TEXT("ViolationZoneBreached subtracts catastrophic PenaltyViolationZoneBreached"),
			S->GetCurrentScore(), -S->PenaltyViolationZoneBreached);
	}

	return true;
}

// Covers: REQ-SCORE-003 - ResetSession wipes score + log + counters.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceScoringResetSessionTest,
	"Clearance.Scoring.ResetSession",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceScoringResetSessionTest::RunTest(const FString& Parameters)
{
	UClearanceScoring* S = NewObject<UClearanceScoring>();

	// Rack up some state.
	S->LogIncident(EIncidentType::SuccessfulLanding, FName("DLH101"), NAME_None, TEXT(""));
	S->LogIncident(EIncidentType::SuccessfulHandoff, FName("BAW102"), NAME_None, TEXT(""));
	S->LogIncident(EIncidentType::SeparationLoss,   FName("UAL103"), FName("AAL104"), TEXT(""));

	TestTrue(TEXT("Score non-zero before reset"), S->GetCurrentScore() != 0);
	TestTrue(TEXT("Log non-empty before reset"), S->GetSessionLog().Num() > 0);

	S->ResetSession();

	TestEqual(TEXT("Score reset to zero"), S->GetCurrentScore(), 0);
	TestEqual(TEXT("Log cleared to zero entries"), S->GetSessionLog().Num(), 0);
	TestTrue(TEXT("Spawn interval reset to base"),
		FMath::IsNearlyEqual(S->GetCurrentSpawnInterval(), S->BaseSpawnIntervalSeconds, 0.01f));

	return true;
}

// Covers: REQ-SCORE-004, REQ-SCORE-005 - repeated successful handoffs
// shrink the spawn interval toward the floor without going below it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceScoringDifficultyRampTest,
	"Clearance.Scoring.DifficultyRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceScoringDifficultyRampTest::RunTest(const FString& Parameters)
{
	UClearanceScoring* S = NewObject<UClearanceScoring>();

	const float InitialInterval = S->GetCurrentSpawnInterval();
	TestTrue(TEXT("Initial interval at base"),
		FMath::IsNearlyEqual(InitialInterval, S->BaseSpawnIntervalSeconds, 0.01f));

	// One successful handoff should shrink the interval by exactly
	// DifficultySecondsPerHandled. - TripleA
	S->LogIncident(EIncidentType::SuccessfulHandoff, NAME_None, NAME_None, TEXT(""));
	const float AfterOne = S->GetCurrentSpawnInterval();
	TestTrue(TEXT("Interval shrank after one handoff"), AfterOne < InitialInterval);
	TestTrue(TEXT("Interval shrank by DifficultySecondsPerHandled"),
		FMath::IsNearlyEqual(InitialInterval - AfterOne, S->DifficultySecondsPerHandled, 0.01f));

	// Ramp far enough to hit the floor, verify it clamps not goes negative.
	for (int32 i = 0; i < 1000; ++i)
	{
		S->LogIncident(EIncidentType::SuccessfulHandoff, NAME_None, NAME_None, TEXT(""));
	}
	const float Floored = S->GetCurrentSpawnInterval();
	TestTrue(TEXT("Interval clamps at MinSpawnIntervalSeconds"),
		Floored >= S->MinSpawnIntervalSeconds - 0.01f);
	TestTrue(TEXT("Interval does not go below floor"),
		Floored <= S->MinSpawnIntervalSeconds + 0.01f);

	return true;
}

// Covers: REQ-SCORE-002 (log accumulation) + basic RecordInstruction path.
// A separate small test so RecordInstruction isn't left silently untested. - TripleA
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceScoringRecordInstructionTest,
	"Clearance.Scoring.RecordInstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceScoringRecordInstructionTest::RunTest(const FString& Parameters)
{
	UClearanceScoring* S = NewObject<UClearanceScoring>();
	// RecordInstruction alone does not directly change the score - it's used
	// as the denominator in efficiency. Efficiency should stay bounded. - TripleA
	for (int32 i = 0; i < 20; ++i) { S->RecordInstruction(); }
	const float Eff = S->GetEfficiency();
	TestTrue(TEXT("Efficiency >= 0"), Eff >= 0.f);
	TestTrue(TEXT("Efficiency <= 100"), Eff <= 100.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
