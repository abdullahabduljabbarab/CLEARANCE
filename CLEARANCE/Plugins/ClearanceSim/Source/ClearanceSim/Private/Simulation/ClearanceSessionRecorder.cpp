#include "Simulation/ClearanceSessionRecorder.h"
#include "Airspace/ClearanceAirspaceManager.h"

void UClearanceSessionRecorder::StartRecording()
{
	bRecording = true;
}

void UClearanceSessionRecorder::StopRecording()
{
	bRecording = false;
}

void UClearanceSessionRecorder::ClearRecording()
{
	Snapshots.Reset();
	Events.Reset();
}

void UClearanceSessionRecorder::CaptureSnapshot(float SimTimeSeconds, const TArray<FAircraftState>& States)
{
	if (!bRecording) { return; }
	FRecordedSnapshot S;
	S.TimeStamp = SimTimeSeconds;
	S.States = States;
	Snapshots.Add(MoveTemp(S));
}

void UClearanceSessionRecorder::LogEvent(float SimTimeSeconds, const FString& Description)
{
	if (!bRecording) { return; }
	FRecordedEvent E;
	E.TimeStamp = SimTimeSeconds;
	E.Description = Description;
	Events.Add(MoveTemp(E));
}

float UClearanceSessionRecorder::GetDurationSeconds() const
{
	if (Snapshots.Num() == 0) { return 0.f; }
	return Snapshots.Last().TimeStamp - Snapshots[0].TimeStamp;
}

const FRecordedSnapshot* UClearanceSessionRecorder::FindSnapshotAt(float SecondsFromStart) const
{
	if (Snapshots.Num() == 0) { return nullptr; }

	// Lookups are "seconds into the recording" so the caller doesn't need to know the
	// absolute SessionTime the recording was taken at. Convert to absolute internally. - TripleA
	const float TargetAbs = Snapshots[0].TimeStamp + FMath::Max(0.f, SecondsFromStart);

	// Binary search for the latest snapshot with TimeStamp <= target.
	int32 Lo = 0, Hi = Snapshots.Num() - 1, BestIdx = 0;
	while (Lo <= Hi)
	{
		const int32 Mid = (Lo + Hi) / 2;
		if (Snapshots[Mid].TimeStamp <= TargetAbs)
		{
			BestIdx = Mid;
			Lo = Mid + 1;
		}
		else
		{
			Hi = Mid - 1;
		}
	}
	return &Snapshots[BestIdx];
}

void UClearanceSessionRecorder::ApplySnapshotTo(AClearanceAirspaceManager* Manager, const FRecordedSnapshot& Snapshot) const
{
	if (!Manager) { return; }

	// 1) Which callsigns should exist at this moment in time?
	TSet<FName> Wanted;
	Wanted.Reserve(Snapshot.States.Num());
	for (const FAircraftState& S : Snapshot.States)
	{
		Wanted.Add(S.Callsign);
	}

	// 2) Drop anything currently in the sector that wasn't there back then
	// (handles landings/exits that haven't happened yet in the replay).
	const TArray<FAircraftState> Current = Manager->GetAllAircraftStates();
	for (const FAircraftState& S : Current)
	{
		if (!Wanted.Contains(S.Callsign))
		{
			Manager->DeregisterAircraft(S.Callsign);
		}
	}

	// 3) Register fresh ones and update existing ones to the recorded state.
	for (const FAircraftState& S : Snapshot.States)
	{
		if (!Manager->IsCallsignRegistered(S.Callsign))
		{
			Manager->RegisterAircraft(S);
		}
		else
		{
			Manager->RequestStateUpdate(S);
		}
	}
}

TArray<FRecordedEvent> UClearanceSessionRecorder::GetEventsInRange(float FromSeconds, float ToSeconds) const
{
	TArray<FRecordedEvent> Out;
	for (const FRecordedEvent& E : Events)
	{
		if (E.TimeStamp >= FromSeconds && E.TimeStamp <= ToSeconds)
		{
			Out.Add(E);
		}
	}
	return Out;
}
