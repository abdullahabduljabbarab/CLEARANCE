#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceSessionRecorder.generated.h"

class AClearanceAirspaceManager;

// Records the session as a structured timeline (per-tick snapshots of every
// aircraft + timestamped events), and on playback drives the Airspace Manager
// back to those snapshots. It's the data layer for After-Action Review - the
// Simulation Controller suspends its own ticking and lets the recorder pose the
// world for the chosen replay time. - TripleA
UCLASS(BlueprintType)
class CLEARANCESIM_API UClearanceSessionRecorder : public UObject
{
	GENERATED_BODY()

public:
	// --- Recording (called from the Controller while playing live) -----------

	UFUNCTION(BlueprintCallable, Category = "Recording")
	void StartRecording();

	UFUNCTION(BlueprintCallable, Category = "Recording")
	void StopRecording();

	UFUNCTION(BlueprintCallable, Category = "Recording")
	bool IsRecording() const { return bRecording; }

	// Snapshot the world right now, with the given session-time stamp.
	UFUNCTION(BlueprintCallable, Category = "Recording")
	void CaptureSnapshot(float SimTimeSeconds, const TArray<FAircraftState>& States);

	UFUNCTION(BlueprintCallable, Category = "Recording")
	void LogEvent(float SimTimeSeconds, const FString& Description);

	UFUNCTION(BlueprintCallable, Category = "Recording")
	int32 GetSnapshotCount() const { return Snapshots.Num(); }

	UFUNCTION(BlueprintCallable, Category = "Recording")
	float GetDurationSeconds() const;

	UFUNCTION(BlueprintCallable, Category = "Recording")
	void ClearRecording();

	// --- Playback ------------------------------------------------------------

	// Find the snapshot at-or-before the given time; null if no recording.
	const FRecordedSnapshot* FindSnapshotAt(float SimTimeSeconds) const;

	// Apply a snapshot to the Airspace Manager: register missing aircraft,
	// update existing ones, deregister those no longer in the snapshot. Lets the
	// world "pose" itself to that moment in time.
	UFUNCTION(BlueprintCallable, Category = "Recording")
	void ApplySnapshotTo(AClearanceAirspaceManager* Manager, const FRecordedSnapshot& Snapshot) const;

	// All events between two times - handy for the debrief overlay.
	UFUNCTION(BlueprintCallable, Category = "Recording")
	TArray<FRecordedEvent> GetEventsInRange(float FromSeconds, float ToSeconds) const;

	UFUNCTION(BlueprintCallable, Category = "Recording")
	TArray<FRecordedEvent> GetAllEvents() const { return Events; }

private:
	UPROPERTY()
	TArray<FRecordedSnapshot> Snapshots;

	UPROPERTY()
	TArray<FRecordedEvent> Events;

	bool bRecording = false;
};
