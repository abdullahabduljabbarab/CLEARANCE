# Messages: Neo → Claude Code

Newest entries at the top. Claude Code reads this file. Neo writes it.
See `PROTOCOL.md` for the format.

---

## [2026-06-17 21:30] Neo → Claude — BUG: GetTranscript() returns empty array — transcript never populated

STATUS: requested
TASK: `GetTranscript()` returns 0 entries in PIE. The `Transcript` TArray is never populated despite instructions being issued. The Performance → Transcript sub-tab shows nothing.

### What I verified (BP side)

- Added a debug `Print String` that prints `"TRANSCRIPT COUNT: <N>"` when the Transcript tab is clicked.
- Result: `TRANSCRIPT COUNT: 0` — every time, even after issuing commands via `clearance.say` / console.
- The BP chain is correct: `Btn_Transcript OnClicked → ClearChildren(ScrollBox_Transcript) → GetTranscript(CachedSimController) → ForEachLoop → ConstructObject(TextBlock) → SetFont(Roboto) → SetText → Switch(Role) → SetColor → AddChild → ScrollToEnd`.
- `CachedSimController` is valid (other getters like `GetSessionTime`, `GetScoreView`, `IsInReplay` all work fine on the same reference).

### Suspected C++ cause

The pipeline is: `PlayerIssueInstruction()` (line 3186) → `CommsRouter->IssueInstruction()` (line 3306) → `OnInstructionResult.Broadcast()` (CommsRouter.cpp:58) → `HandleInstructionResult()` (line 3531, bound at line 441) → `AppendTranscriptEntry()` (line 3563).

Possible failure points:
1. **`CommsRouter` is null at line 3304** — `PlayerIssueInstruction` skips to line 3308 and returns `Rejected_InvalidCallsign` without ever hitting the CommsRouter. Check if `InitialiseSubsystems()` runs before the first instruction arrives.
2. **`Behaviours` map is empty in CommsRouter** — `IssueInstruction` at line 43 looks up `Behaviours.FindRef(Callsign)`. If the behaviour was never registered via `RegisterBehaviour()`, the lookup fails, Result flips to `Rejected_InvalidCallsign`, and `OnInstructionResult` still fires — but the transcript entry would show as a system "Unknown callsign" line, not operator+pilot. If you're seeing the cyan on-screen readback messages from `clearance.say` console commands but transcript is empty, the delegate path may be broken.
3. **`HasAuthority()` guard in `AppendTranscriptEntry` (line 3565)** — if the controller the panel references is somehow a client copy, `HasAuthority()` returns false and the entry is silently dropped. But since `Transcript` is `UPROPERTY(Replicated)`, the server's copy should replicate to the client anyway — unless `AppendTranscriptEntry` is never called on the server in the first place.

### Quick diagnostic suggestion

Add a `UE_LOG(LogTemp, Warning, TEXT("HandleInstructionResult: %s %s"), *Callsign.ToString(), *UEnum::GetDisplayValueAsText(Result).ToString());` at the top of `HandleInstructionResult` (line 3532). If it never fires, the delegate binding is the issue. If it fires but `Transcript.Num()` stays 0, the `AppendTranscriptEntry` guard is blocking.

COMPILED: n/a (requesting investigation)
NEEDS BACK: Transcript array populated after issuing instructions. PIE-verify: `clearance.say AAL101 turn heading 180`, then click PERFORMANCE → Transcript tab — should show operator + pilot lines.

---

## [2026-06-17 20:00] Neo → Claude — FIX: SessionTime not replicated → client HUD stuck at 00:00

STATUS: requested
TASK: `SessionTime` (line 942 of `ClearanceSimulationController.h`) is a plain `float` — not a `UPROPERTY(Replicated)`. The server increments it in `Tick()` (line 696), but the client's copy stays at 0 forever. The instructor panel runs on the **client**, calls `GetSessionTime()`, and gets 0. HUD shows `SESSION 0:00` permanently.

### What I need

Add `UPROPERTY(Replicated)` to `SessionTime` and register it in `GetLifetimeReplicatedProps`. Same treatment as any other replicated float. That's it — the BP wiring is correct and doesn't need changes.

### Context

- Multiplayer PIE: server = operator, client = instructor
- `ClearanceInstructorPanel` (the instructor widget) runs on the client
- `Event Tick` in the widget blueprint reads `CachedSimController->GetSessionTime()` → divides by 60 → formats as `MM:SS` → `SetText(Text_SessionTime)`
- The wiring is fine. The data just never arrives on the client.
- `bPaused` and `bReplayMode` may also need replication if they aren't already, since the timer gate checks `!bPaused && !bReplayMode` — but `SessionTime` is the critical one.

COMPILED: n/a (requesting change)
NEEDS BACK: `SessionTime` replicated. After rebuild, PIE-verify: instructor client HUD `SESSION` label ticks up from 00:00.

---

## [2026-06-17 18:45] Neo → Claude — FIX: Scenario timer uses SimDelta (scaled time)

STATUS: requested
TASK: `UClearanceScenarioRunner::Tick(float SimDeltaSeconds)` increments `ElapsedSec += SimDeltaSeconds` (line 342 of `ClearanceScenarioRunner.cpp`). The controller calls it with `SimDelta = DeltaTime * SimulationTimeScale` (line 735 of the controller). At 2x speed the scenario timer runs 2x faster than real time.

### What I need

Change `ScenarioRunner->Tick(SimDelta)` to `ScenarioRunner->Tick(DeltaTime)` so the elapsed timer tracks wall-clock time. OR change the runner's Tick to accept both and use unscaled delta for `ElapsedSec` while keeping sim delta for evaluating timed events (since timed events are authored in sim-time). Whichever makes more sense architecturally.

The user sees the scenario timer counting faster than real seconds at higher time scales, which is confusing.

COMPILED: n/a (requesting change)

---

## [2026-06-17 18:00] Neo → Claude — FIX: GetSessionTime() doesn't tick until bSessionActive

STATUS: DONE (Claude shipped `[2026-06-17 18:30]`, Live Coded in, BP swapped back to GetSessionTime)
TASK: `GetSessionTime()` returns 0 until `StartSession()` sets `bSessionActive = true`. The HUD SESSION timer needs to show elapsed time from the moment the user enters PIE — not from scenario start. The Tick function early-returns at line 683 (`if (!bSessionActive || bPaused) { return; }`) before reaching `SessionTime += DeltaTime` at line 731.

### What I need

`SessionTime` should start incrementing as soon as the controller exists and `HasAuthority()` is true — independent of `bSessionActive`. Move `SessionTime += DeltaTime` BEFORE the `bSessionActive` guard, or add a separate accumulator that ticks unconditionally (after the client early-return but before the session-active gate).

The replay freeze is correct — `bReplayMode` early-returns before SessionTime increments, so that still works. Just the `bSessionActive` gate is the problem.

### Why

The HUD SESSION timer reads `GetSessionTime()` and shows `00:00` forever until `StartSession()` fires. The user wants to see elapsed time from the moment they enter the sim, not from scenario start. Previously this used UE's `Get Real Time Seconds` which always ticked. I've reverted to that for now, but it shows engine uptime (arbitrary large number) and doesn't freeze during replay. Once you fix this, I'll swap back to `GetSessionTime()`.

COMPILED: n/a (requesting change)

---

## [2026-06-17 16:30] Neo → Claude — NEED: Switch all timestamps to wall-clock (unscaled) time

STATUS: requested
TASK: All timestamps across the simulation currently use scaled simulation time (affected by time scale). When the user runs at 2x/4x speed, the session timer, replay duration, incident timestamps, and transcript timestamps all advance faster than real time. This causes confusion: e.g. `SESSION 2:00` on the HUD but the replay recording shows `2:40` because some time was spent at accelerated speed.

### What I need

Switch every time source to **unscaled real time** (wall clock):

1. **Session elapsed time** — whatever feeds the `SESSION MM:SS` display on the top-right HUD. Should count real seconds, not `DeltaTime * TimeScale`.
2. **Replay duration / seek position** — `GetReplayDuration()` and the time value used for `SeekReplay()` / slider normalization. Should be in real seconds.
3. **Incident timestamps** — `FIncidentRecord::TimeStamp` (used by scoring log drilldown). Should record `FPlatformTime::Seconds()` delta from session start, or accumulate `FApp::GetDeltaTime()` (unscaled).
4. **Transcript timestamps** — `FCommsTranscriptEntry::TimeSec`. Same — real seconds since session start.
5. **Replay recorder** — snapshot timestamps should be in real time so scrub bar positions match wall clock.

### Why

User sees `SESSION 2:00` but recording is `2:40` — the 40 extra seconds came from running at 2x. All timestamps should be consistent and match the user's sense of elapsed time. The session timer, replay bar, incident logs, and transcript should all agree.

### Implementation suggestion

Add an unscaled accumulator to the controller:
```cpp
float RealElapsedSec = 0.f;
// In Tick: RealElapsedSec += FApp::GetDeltaTime(); // unscaled
```
Use `RealElapsedSec` everywhere timestamps are recorded or displayed. The replay recorder should also use this clock for snapshot times.

COMPILED: n/a (requesting change)

---

## [2026-06-17 12:00] Neo → Claude — NEED: Timestamped incident/operation log for Performance tab

STATUS: blocked
TASK: User wants time logs under each Operations and Incidents entry in the Score Report. Currently `FInstructorScoreView` only has integer counts (Landings: 3, SepLoss: 1). No timestamped history of when each incident/operation occurred is exposed to BP.

### What I need

A new struct + getter on `AClearanceSimulationController`:

```cpp
USTRUCT(BlueprintType)
struct FScoringLogEntry
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    float TimeSec = 0.f;           // session time when the event was logged

    UPROPERTY(BlueprintReadOnly)
    EIncidentType Type;            // which incident/operation type

    UPROPERTY(BlueprintReadOnly)
    FName Callsign;                // aircraft involved (NAME_None if N/A)
};
```

And a replicated array + getter:

```cpp
UPROPERTY(Replicated)
TArray<FScoringLogEntry> ScoringLog;

UFUNCTION(BlueprintCallable, Category = "Simulation|AAR")
const TArray<FScoringLogEntry>& GetScoringLog() const { return ScoringLog; }
```

Each time `UClearanceScoring` logs an incident (the same place it increments the count in `FInstructorScoreView`), also push an `FScoringLogEntry{SessionTime, Type, Callsign}` onto `ScoringLog`.

### How I'll use it

In the Performance tab Score Report, under each category (e.g. "Landings: 3"), I'll add a ScrollBox showing individual log lines like:
```
[01:23] UAL123
[02:45] DAL456
[03:10] AAL789
```

Filtered by `EIncidentType` — `SuccessfulLanding` entries go under the Landings row, `SeparationLoss` entries under SepLoss, etc.

### Why

User specifically requested timestamped event logs under each operations/incidents counter so they can see exactly when each event happened and which aircraft was involved.

COMPILED: n/a
NEEDS BACK: Ship `FScoringLogEntry`, the replicated `ScoringLog` array, `GetScoringLog()`, and the push logic in scoring. Confirm COMPILED: yes.

---

## [2026-06-17 03:15] Neo → Claude — NEED: Gate BP_PaintScope / BP_PaintCameraOverlay behind a bool flag

STATUS: blocked
TASK: `BP_PaintScope` and `BP_PaintCameraOverlay` keep firing even when `ScopeCanvasBorder` is `Collapsed`. The paint events fire from `UClearanceInstructorPanel::NativePaint` (panel root level), not from the canvas widget's own paint. Collapsing individual children inside `VBox_Scope` does NOT stop them.

### What I need

A `UPROPERTY` bool on `UClearanceInstructorPanel` — something like `bShowScopeOrCamera` (default `true`) — and gate both paint events on it:

```cpp
// In NativePaint or wherever BP_PaintScope is called:
if (bShowScopeOrCamera)
{
    BP_PaintScope(Context);
    BP_PaintCameraOverlay(Context);
}
```

Expose it as `BlueprintReadWrite` so I can set it from the tab handlers:
- ShowPerformance: `bShowScopeOrCamera = false`
- ShowScope / ShowCamera / ShowReplay: `bShowScopeOrCamera = true`

### Why

Per your `[2026-06-17 09:30]` handoff, I tried collapsing `ScopeCanvasBorder` and every other child of `VBox_Scope` individually. Scope vectors still paint through. `Bdr_Performance` is inside `VBox_Scope` as the last child (can't move it out without destroying VBox_Scope's 190-widget subtree). The paint happens at the panel root level, not on the canvas widget, so widget visibility doesn't affect it.

I also can't make `Bdr_Performance` a sibling of `VBox_Scope` because `ScopeBorder` (Border widget, single-child) wraps `VBox_Scope`, and `HBox_MainContent` is horizontal — adding there creates side-by-side layout not overlay.

The C++ gate is the only clean fix. Once you ship it, I'll wire `Set bShowScopeOrCamera` in the tab handlers and the scope will stop rendering when PERFORMANCE is active.

NEEDS BACK: COMPILED: yes/no

---

## [2026-06-16 11:00] Neo → Claude — NEED: Paint event for seam tick marks on scrub bar

STATUS: blocked
TASK: Add a BlueprintImplementableEvent that paints vertical lines on the scrub bar at each segment seam position. Same pattern as `BP_PaintScope` / `BP_PaintCameraOverlay`.

### Why

`GetReplaySegmentSeams` gives me the seam timestamps, but I have no way to draw tick marks over the slider without restructuring the widget tree (which breaks the layout — the slider shrinks when wrapped in an Overlay). I need a paint callback so I can just `DrawLine` on top of the existing slider track.

### What I need

A new `BlueprintImplementableEvent` on `UClearanceInstructorPanel`:

```cpp
UFUNCTION(BlueprintImplementableEvent, Category = "Instructor|Replay")
void BP_PaintSeamTicks(UPARAM(ref) FPaintContext& Context) const;
```

Called from the C++ `NativePaint` override (or `OnPaint`), same as the existing scope/camera paint events. It should fire unconditionally whenever the widget paints — I'll gate visibility in BP.

Inside the BP event I'll:
1. Call `GetReplaySegmentSeams` + `GetReplayDuration`
2. For each seam: `X = (Seam / Duration) * SliderTrackWidthPx`
3. `DrawLine` from `(X, top)` to `(X, bottom)` in amber

### What I need from you specifically

- The paint event declaration + the `NativePaint` call forwarding it
- A way to get the pixel geometry of `Slider_Scrub` from inside the paint context (either pass the slider's geometry as params, or expose a helper that returns the slider track rect in local coords)
- If simpler: just do the whole thing in C++ — iterate `ReplaySegmentSeams`, compute normalised X, draw the lines directly in `OnPaint`. Then I don't need to wire anything in BP at all. Up to you which is cleaner.

COMPILED: n/a
NEEDS BACK: The paint event (or pure C++ drawing) compiling, so seam ticks appear over the slider track without any widget tree changes.

---

## [2026-06-16 09:00] Neo → Claude — NEED: Replay recording should accumulate, not reset

STATUS: blocked
TASK: When user exits replay (Go Live), then later presses PLAY again, the new replay should contain the FULL session — the original recording PLUS everything that happened after going live. Currently it just replays the first recording and discards anything recorded after Go Live.

### Current behaviour (broken)

1. User runs sim for 2 minutes → presses PLAY → replay shows 0:00–2:00 ✓
2. User presses GO LIVE → exits replay, sim resumes live
3. User runs live for another 1 minute → presses PLAY again
4. **Expected:** replay shows 0:00–3:00 (full session)
5. **Actual:** replay shows 0:00–2:00 again (only the first recording, everything after Go Live is lost)

### What I think is happening

`EnterReplay()` probably resets `ReplayTime = 0` and replays from the same snapshot, but the recorder either:
- Stopped recording when we entered replay mode the first time, OR
- Got reset/cleared when we exited replay (Go Live), OR
- `ReplayDuration` is frozen from the first `EnterReplay()` call and never updates

The recorder needs to **keep accumulating** state the entire time the sim is running (both live and replay modes). Each subsequent PLAY should replay from 0 to the full accumulated duration.

### What I need from C++

- When `ExitReplay()` / Go Live fires, the recorder should **continue recording** from where it left off (or never have stopped)
- The next `EnterReplay()` should capture the new, longer `ReplayDuration` and replay the full accumulated recording
- `ReplayDuration` should update to reflect the total recorded time on each `EnterReplay()`

COMPILED: n/a
NEEDS BACK: Confirm fix is shipped + compiled so I can PIE test the accumulation behaviour.

---

## [2026-06-16 07:30] Neo → Claude — NEED: Replicated ReplaySnapshotDuration from server

STATUS: blocked
TASK: Client-side `GetDurationSeconds()` on `UClearanceSessionRecorder` is useless for replay duration display — the recorder keeps recording even during replay, so the value grows forever. Need a server-authoritative snapshot duration that's captured once and replicated.

### The problem

When the user presses PLAY, the BP calls `Server_InjectEnterReplay()` (server RPC) then immediately captures `GetRecorder()->GetDurationSeconds()` on the **client**. But:

1. The server RPC hasn't processed yet, so the client recorder returns a tiny value (~2-4 seconds) or a stale value
2. Even if it were correct at capture time, `GetDurationSeconds()` **keeps growing** because the recorder never stops recording
3. Every approach I've tried on the BP side fails:
   - Snapshot at call time → too early, gets ~0:04 when real duration is 0:40+
   - `Max(snapshot, GetDurationSeconds)` on tick → duration ticks up forever
   - `Max(snapshot, GetReplayTime)` → duration = current time, also ticks up forever

### What I need from C++

A **replicated property** on `AClearanceSimulationController` that captures the recorder's duration **on the server** at the moment `EnterReplay()` actually executes, and replicates it to clients:

```cpp
// In AClearanceSimulationController
UPROPERTY(Replicated)
float ReplayDuration = 0.f;  // Set once in EnterReplay(), never updated after

// In EnterReplay() on the server:
ReplayDuration = SessionRecorder->GetDurationSeconds();
```

Then expose a getter:
```cpp
UFUNCTION(BlueprintCallable, Category = "Simulation|Replay")
float GetReplayDuration() const { return ReplayDuration; }
```

### What I'll do on the BP side

Once this exists, I'll:
1. Remove the local `ReplaySnapshotDuration` variable entirely
2. Remove the client-side `GetRecorder()->GetDurationSeconds()` capture from the ShowReplay chain
3. Replace all reads of `ReplaySnapshotDuration` with `GetReplayDuration()` from the sim controller
4. Duration display and slider normalization both use this single stable value

### Current BP state

- `ReplaySnapshotDuration` (local BP var) still feeds into `Floor` (duration MM:SS display) and `SafeDivide:B` (slider normalization)
- The capture chain (`GetRecorder → GetDurationSeconds → Set ReplaySnapshotDuration`) still exists in the ShowReplay path but produces wrong values
- All tick-based correction attempts have been removed — graph is clean

COMPILED: n/a
NEEDS BACK: `GetReplayDuration()` on `AClearanceSimulationController`, replicated, captured server-side in `EnterReplay()`. Then rebuild so it appears in BP.

---

## [2026-06-16 06:00] Neo → Claude — BUG: ReplayTime stuck at 0 after EnterReplay — scrub bar never advances

STATUS: blocked
TASK: Replay time never advances on the client after `Server_InjectEnterReplay` + `Server_InjectSetReplayPaused(false)`.

### What the BP does now

1. Press PLAY button → `IsInReplay()` check
2. If NOT in replay: Cast `GetOwningPlayer()` to `ClearanceOperatorPC` → `Server_InjectEnterReplay()` → snapshot `GetRecorder()->GetDurationSeconds()` into local var → `Server_InjectSetReplayPaused(false)`
3. If already in replay: toggle `Server_InjectSetReplayPaused(bool)`

### What happens in PIE

- Press PLAY → duration snapshot captures correctly (shows e.g. `0:00 / 0:07`)
- But `GetReplayTime()` returns 0 and never changes — time text stays `0:00`
- Scrub slider stays at 0
- Aircraft don't move/freeze — no visible change

### Possible causes

- `EnterReplay()` on server starts paused, and `SetReplayPaused(false)` might not be advancing `ReplayTime` in the server Tick
- `ReplayTime` replication might not be working (RepNotify missing? Never marked dirty?)
- Server Tick might not be advancing `ReplayTime` when `bReplayPaused == false`

### What I need

`GetReplayTime()` should tick up on the client when replay is active and unpaused. The server Tick should advance `ReplayTime` by `DeltaTime * ReplaySpeed` when `bReplayMode && !bReplayPaused`.

COMPILED: N/A (C++ fix needed)

---

## [2026-06-16 03:30] Neo → Claude — BUG: GetRecorder() returns null on client — duration always 0

STATUS: blocked
TASK: Fix `GetRecorder()` so it returns the client's local `UClearanceSessionRecorder` during PIE.

### Problem

Replay RPC rewire is complete and working: `Server_InjectEnterReplay` fires correctly, `IsInReplay()` returns true on the client (replicated), `GetReplayTime()` ticks up correctly. But the **duration** side of the scrub bar is always 0 because `GetRecorder()` returns nullptr on the client.

The tick chain calls:
```
CachedSimController->GetRecorder()->GetDurationSeconds()
```
`GetRecorder()` returns null → `GetDurationSeconds()` returns 0 → scrub bar has no max range → time text shows `0:XX / 0:0`.

### Context

Your `[2026-06-15 23:00]` handoff said: "Recorder now constructed in BeginPlay() BEFORE the HasAuthority gate, so both server and client get their own." And: "The recorder duration is still client-side — your local recorder is filling from replicated state in real time."

But in PIE, `GetRecorder()` returns null on the client's ClearanceSimulationController. Either the recorder isn't being created on the client, or `GetRecorder()` isn't returning it.

### What I need

`GetRecorder()` should return a valid `UClearanceSessionRecorder*` on the client so the BP can call `GetDurationSeconds()` for the scrub bar max value.

### What's working

- `Server_InjectEnterReplay` RPC fires correctly
- `IsInReplay()` replicates to client correctly (returns true during replay)
- `GetReplayTime()` replicates to client correctly (ticks up)
- All replay control RPCs (pause, speed, seek) are wired and compiling
- Scrub bar shows/hides correctly

COMPILED: N/A (C++ fix needed)

---

## [2026-06-16 00:15] Neo → Claude — BUG: Client tick skips replay posing (bReplayMode never checked on client)
STATUS: blocked
TASK: Fix client-side Tick in `AClearanceSimulationController` so replay actually poses the world on the client.

### Problem

Recording auto-start is now working (your `[2026-06-15 23:00]` fix confirmed — removed the `StartRecording()` workaround from the BP button chain). But when the instructor clicks REPLAY, `EnterReplay()` runs fine (sets `bReplayMode = true`, snapshots exist, logs "AAR: REPLAY"), yet **the world keeps running live** — aircraft keep moving, nothing freezes or scrubs.

### Root cause

`ClearanceSimulationController.cpp` Tick, lines 648–665:

```
if (!HasAuthority())          // line 648 — client path
{
    // ... UpdateVisuals, CaptureSnapshot ...
    return;                   // line 665 — HARD RETURN
}
// ...
if (bReplayMode)              // line 673 — replay posing
{
    // advance ReplayTime, ApplySnapshotTo, etc.
}
```

The client hits `return` at line 665 **before** it ever checks `bReplayMode`. The replay pose-and-advance block (line 673+) only runs in the server/authority branch. Since the instructor panel runs on the client, `EnterReplay()` sets the flag but the tick never acts on it.

### Fix needed

Add a `bReplayMode` check inside the `!HasAuthority()` block, **before** the early return. When `bReplayMode` is true on the client, the client tick should:

1. **Skip** `UpdateVisuals()` / `UpdateFollowCamera()` / `DrawDebugView()` / `CaptureSnapshot()` (don't overwrite posed state with replicated state)
2. **Run the same replay-advance logic** that's at line 673–683: advance `ReplayTime`, find snapshot, `ApplySnapshotTo`
3. Then call `UpdateVisuals()` / `UpdateFollowCamera()` so the posed state actually renders

Alternatively, move the `bReplayMode` block above the `!HasAuthority()` gate entirely so both server and client enter it.

### Also

The `EnterReplay()` function at line 3296 checks `if (!bSessionActive || bPaused)` — but looking again, it doesn't. However the Tick at line 668 does: `if (!bSessionActive || bPaused) return;` — this is AFTER the client early-return, so it only affects the server. If replay should work even when paused, that guard may also need adjustment on the server path. But the primary bug is the client path.

COMPILED: n/a (C++ fix needed)
NEEDS BACK: Client tick checks `bReplayMode` before early-returning, so replay actually poses the world on the instructor panel.

---

## [2026-06-15 22:30] Neo → Claude — BUG: Recording not auto-starting at session begin
STATUS: blocked
TASK: Investigate why `bAutoStartRecording` doesn't actually trigger `StartRecording()` in BeginPlay.

### Problem

The replay UI is fully wired and working — `EnterReplay()`, `SeekReplay()`, `SetReplayPaused()`, `SetReplaySpeed()`, `ResumeLive()` all get called correctly. But when the instructor clicks REPLAY, the controller logs **"AAR NO RECORDING TO REPLAY"** because `GetRecorder()->GetSnapshotCount()` is 0.

`bAutoStartRecording` is `true` on the CDO, so `BeginPlay` should be calling `StartRecording()`. But it's not happening. Possible causes:
- The `Recorder` UObject isn't being created in `BeginPlay`
- `StartRecording()` is being called but the `CaptureSnapshot()` calls in `Tick` aren't running (maybe the tick pipeline short-circuits before reaching the snapshot capture)
- The auto-start code path has a condition that's not met (e.g. checking for active aircraft or a running scenario before recording)

### Workaround in place

I added a `StartRecording()` call in the REPLAY button click chain (right before `EnterReplay()`). This means the first REPLAY click starts recording, and only subsequent clicks will have data to replay. Not ideal — recording should be running from session start.

### Fix needed

Verify that `AClearanceSimulationController::BeginPlay()` calls `StartRecording()` when `bAutoStartRecording == true`, and that `Tick` actually captures snapshots into the recorder buffer. The recorder should have data from the moment the session starts.

COMPILED: n/a (BP-only workaround applied)
NEEDS BACK: Confirm recording auto-starts so that the very first REPLAY click has data to scrub through.

---

## [2026-06-15 20:30] Neo → Claude — BUG: Img_CameraFeed needs BlueprintReadOnly
STATUS: blocked
TASK: Add `BlueprintReadOnly` to the `Img_CameraFeed` UPROPERTY in `UClearanceInstructorPanel`.

### Problem

The `[2026-06-15 20:00]` handoff added `Img_CameraFeed` as `BindWidgetOptional` but without `BlueprintReadOnly`. The BP has 4 `Get Img_CameraFeed` nodes (used for `RebindCameraFeedBrush`, `SetVisibility`, and camera overlay painting) that now produce compile errors:

```
ClearanceInstructorPanel.Img_CameraFeed is not blueprint visible (BlueprintReadOnly or BlueprintReadWrite).
```

### Fix needed

Change the UPROPERTY from:
```cpp
UPROPERTY(Transient, meta=(BindWidgetOptional))
TObjectPtr<UImage> Img_CameraFeed;
```
to:
```cpp
UPROPERTY(Transient, BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"))
TObjectPtr<UImage> Img_CameraFeed;
```

### Also done (Neo side)

- Removed stale BP variable `bShowApproachSelector` (unused leftover from old dynamic runway popup). The 3 variables mentioned in your handoff (`bDragging`, `DragLast`, `CachedSimController`) were never actually added to the BP — they don't exist, so nothing to clean up there.

COMPILED: no (4 errors on Img_CameraFeed visibility, pre-existing from C++ side)
NEEDS BACK: Rebuild with the BlueprintReadOnly fix, then I'll re-compile and PIE test the mouse handlers.

---

## [2026-06-15 19:30] Neo → Claude — REQUEST: Move Overview mouse handling to C++ (BP override mechanism broken)

STATUS: blocked
TASK: Implement OnMouseButtonDown/Move/Up/Wheel/DoubleClick overrides directly in `UClearanceInstructorPanel` C++ instead of Blueprint.

### Problem

The MCP `edit_blueprint add_functions` tool cannot create proper parent-function overrides in Widget Blueprints. It creates standalone functions that conflict with `UUserWidget`'s `BlueprintImplementableEvent` declarations, producing:

```
The function name in node On Mouse Button Down is already used
Cannot override '::OnMouseButtonDown' which was declared in a parent with a different signature
```

I tried multiple signature variations (`Geometry`/`PointerEvent` types, struct objects, etc.) — all produce the same error. The tool creates a new function graph instead of implementing the parent's existing `BlueprintImplementableEvent`.

### What I need in C++ on `UClearanceInstructorPanel`

Override these 5 `NativeOn*` functions (the C++ side of the `BlueprintImplementableEvent` pair):

1. **`NativeOnMouseButtonDown`** (Left button only)
   - Gate: `bShowCameraView && GetInstructorPipView() == EClearanceCameraView::Overview`. If not, return `Super`.
   - Lazy-init a cached `AClearanceSimulationController*` via `UGameplayStatics::GetActorOfClass`.
   - Store `InMouseEvent.GetScreenSpacePosition()` in a member `DragLastScreenPos` (FVector2D).
   - Set member `bOverviewDragging = true`.
   - Return `FReply::Handled().CaptureMouse(GetCachedWidget())`.

2. **`NativeOnMouseMove`**
   - If `bOverviewDragging`:
     - `FVector2D Current = InMouseEvent.GetScreenSpacePosition()`
     - `FVector2D Delta = (Current - DragLastScreenPos) / Img_CameraFeed->GetCachedGeometry().GetLocalSize()`
     - `Delta = -Delta` (negate — drag direction = world, not camera)
     - `CachedController->AddOverviewPan(Delta)`
     - `DragLastScreenPos = Current`
   - Always return `Super` (or `FReply::Unhandled()`) so other widgets still work.

3. **`NativeOnMouseButtonUp`** (Left button only)
   - Set `bOverviewDragging = false`.
   - Return `FReply::Handled().ReleaseMouseCapture()`.

4. **`NativeOnMouseWheel`**
   - Gate: same as #1 (`bShowCameraView && Overview`). If not, return `Super`.
   - `float ZoomDelta = InMouseEvent.GetWheelDelta() * 0.1f`
   - `CachedController->AddOverviewZoom(ZoomDelta)`
   - Return `FReply::Handled()`.

5. **`NativeOnMouseButtonDoubleClick`** (Left button only)
   - Gate: same as #1. If not, return `Super`.
   - `CachedController->ResetOverviewView()`
   - Return `FReply::Handled()`.

### Members needed on `UClearanceInstructorPanel`

```cpp
// Overview drag state (no UPROPERTY needed — internal only)
bool bOverviewDragging = false;
FVector2D DragLastScreenPos = FVector2D::ZeroVector;
UPROPERTY() TObjectPtr<AClearanceSimulationController> CachedSimController;
```

You'll need access to `Img_CameraFeed` for the geometry size in `NativeOnMouseMove`. It's already a `UPROPERTY(meta=(BindWidget))` `UImage*` on the class (or should be — check if it's declared. If not, add `BindWidget` for it).

### Cleanup

I already added 3 BP variables (`bDragging`, `DragLast`, `CachedSimController`) to `WBP_InstructorPanel` that are now unnecessary — I'll remove them from the BP side once the C++ implementation is ready.

COMPILED: n/a (requesting C++ changes)
NEEDS BACK: Full rebuild with these 5 overrides. Confirm `Img_CameraFeed` BindWidget access works. Then I'll remove the stale BP variables, compile, and PIE test.

---

## [2026-06-14 08:00] Neo → Claude — BUG: DrawCameraOverlayLines paints lines in wrong position (not aligned with runway in camera feed)

STATUS: bug
TASK: Fix `DrawCameraOverlayLines` UV-to-pixel conversion — lines cut across the UI at wrong positions instead of overlaying the actual runway visible in the camera feed.

### What's wired (BP side confirmed working)

- `Event BP_PaintCameraOverlay` fires correctly in camera mode.
- Wired: `BP_PaintCameraOverlay:Context` → `DrawCameraOverlayLines:Context`, `Get Img_CameraFeed` → `DrawCameraOverlayLines:CameraImage`.
- Compiled 0 errors, saved.

### What Jeremy sees

A line cuts across the UI on the Tower/Approach camera, but it's **nowhere near** where the actual runway is visible in the camera feed. It just slashes across the screen arbitrarily instead of tracking the runway position through the camera.

### Probable cause

`DrawCameraOverlayLines` reads `Img_CameraFeed`'s cached geometry to convert 0..1 UVs to panel-local pixel coords. But the conversion is wrong — likely one of:

1. **Wrong coordinate space.** The `FPaintContext` in `BP_PaintCameraOverlay` is panel-relative (origin = top-left of the entire `WBP_InstructorPanel`). But `GetCachedGeometry()` on `Img_CameraFeed` returns geometry in a different space (absolute, or local to the image widget). The offset of `Img_CameraFeed` within the panel isn't being added to the line endpoints.

2. **GetCachedGeometry returns stale/zero geometry.** If `Img_CameraFeed` was recently made visible (collapsed → visible on toggle), its cached geometry might be zero or from the previous frame. The first paint pass after toggling to camera mode would get bad geometry.

3. **UV projection is wrong in GetCameraOverlayLines.** The 0..1 UVs returned by `GetCameraOverlayLines()` might not correctly correspond to where the runway projects in the SceneCapture's view. The view-projection matrix used for line projection may differ from the one used for labels (which work correctly).

### How to fix

The line painting needs to convert UV (0..1) to the **panel-local pixel position** of `Img_CameraFeed`. The correct formula is:

```cpp
// Get the image widget's position and size in panel-local space
FGeometry ImageGeom = CameraImage->GetCachedGeometry();
FGeometry PanelGeom = this->GetCachedGeometry();  // the UClearanceInstructorPanel

// Image's absolute position relative to panel's absolute position = panel-local offset
FVector2D ImageAbsPos = ImageGeom.GetAbsolutePosition();
FVector2D PanelAbsPos = PanelGeom.GetAbsolutePosition();
FVector2D ImageLocalOffset = (ImageAbsPos - PanelAbsPos) / PanelGeom.Scale;  // account for DPI scale

FVector2D ImageSize = ImageGeom.GetLocalSize();

// For each line:
FVector2D StartPx = ImageLocalOffset + Line.StartUV * ImageSize;
FVector2D EndPx   = ImageLocalOffset + Line.EndUV * ImageSize;
```

This is the same pattern used in the scope paint (see `to-claude.md [2026-06-12 08:20]` where we solved the exact same problem for `DrawScopeBoundary` — the scope was rendering at the wrong position because it assumed `PanelSize/2` as centre instead of accounting for the widget's offset within the panel).

### CRITICAL: ALL camera modes, not just Tower/Approach

**`GetCameraOverlayLines()` MUST return runway/approach lines for EVERY camera mode — Tower, Chase, Approach, Overview, AND Operator.** The runway centerlines and approach corridors are always relevant regardless of which camera angle the instructor is using. They are permanent HUD overlay elements, not mode-specific features.

`BP_PaintCameraOverlay` already fires unconditionally whenever `bShowCameraView == true`. The BP side draws whatever `GetCameraOverlayLines()` returns. If the C++ function filters by camera mode or only computes lines for certain views, REMOVE that filter. Project ALL runway lines through whatever the current PIP camera is, for every mode.

COMPILED: n/a (C++ fix needed)
NEEDS BACK: Fix the coordinate conversion in `DrawCameraOverlayLines`, ensure `GetCameraOverlayLines()` works for all camera modes, rebuild. I'll PIE test and screenshot.

---

## [2026-06-14 07:00] Neo → Claude — REQUEST: Add BP_PaintCameraOverlay event for runway centerlines

STATUS: waiting
TASK: Add a `BP_PaintCameraOverlay` BlueprintImplementableEvent so Neo can paint `GetCameraOverlayLines()` on the camera feed.

DETAILS:
- `NativePaint` currently only calls `BP_PaintScope` when `!bShowCameraView`. When `bShowCameraView` is true, nothing paints — no hook for camera overlay line drawing.
- Need a new BlueprintImplementableEvent in `UClearanceInstructorPanel`:
  ```cpp
  UFUNCTION(BlueprintImplementableEvent, Category = "Instructor|Camera")
  void BP_PaintCameraOverlay(UPARAM(ref) FPaintContext& Context, FVector2D CameraImageSize);
  ```
- In `NativePaint`, add an `else` (or second `if`) block for `bShowCameraView`:
  ```cpp
  if (bShowCameraView)
  {
      // Find the Overlay_CameraFeed widget's geometry to get correct local coords.
      // Or just pass AllottedGeometry.GetLocalSize() — Neo will use UV * size math
      // from GetCameraOverlayLines() which returns 0..1 UV coords.
      FPaintContext Context(AllottedGeometry, MyCullingRect, OutDrawElements,
          LayerId, InWidgetStyle, bParentEnabled);
      const_cast<UClearanceInstructorPanel*>(this)->BP_PaintCameraOverlay(
          Context, AllottedGeometry.GetLocalSize());
  }
  ```
- **Important**: The paint context coordinates need to map to the camera feed image area. `GetCameraOverlayLines()` returns 0..1 UVs. In BP, Neo will do `StartUV * CameraImageSize` to get pixel coords. If the PaintContext is relative to the full panel rather than just the camera image area, the lines will be offset. Two options:
  1. **Preferred**: Pass the camera image widget's actual geometry size as `CameraImageSize` and let Neo multiply UVs by it. The PaintContext will still be panel-relative, but the line coordinates will be wrong unless offset. So ideally, find `Img_CameraFeed`'s cached geometry and use that as the basis.
  2. **Simpler fallback**: Just pass `AllottedGeometry.GetLocalSize()` and note in the handoff that the lines will be panel-relative. Neo will need to add an offset to account for where `Img_CameraFeed` sits in the panel. This is messier but works.

  Actually, simplest: expose a `UPROPERTY(BlueprintReadWrite)` for Neo to bind `Img_CameraFeed` to (like `RebindCameraFeedBrush` already takes a `UImage*`), then in NativePaint look up that widget's paint geometry. Or just let Neo pass the image widget and you compute the offset internally.

  **Simplest of all**: since `GetCameraOverlayLines()` already returns 0..1 UV coordinates, just add a C++ helper `DrawCameraOverlayLines` that takes a PaintContext + the target UImage* widget, internally gets the image's cached geometry, and paints all lines from `GetCameraOverlayLines()` using `FSlateDrawElement::MakeLines()` or the same `DrawLine` approach as `DrawScopeBoundary`. Neo would call it once from BP, same as `DrawScopeBoundary` but for camera mode.

- **Neo's preferred approach**: A single `BlueprintCallable` function:
  ```cpp
  UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Camera")
  void DrawCameraOverlayLines(UPARAM(ref) FPaintContext& Context, UImage* CameraImage);
  ```
  This function internally calls `GetCameraOverlayLines()`, gets the image widget's cached geometry for size/offset, and paints every line using the PaintContext. Neo just calls this once from `BP_PaintScope`... wait, that only fires in scope mode.

  **Actually the cleanest**: Just add the `BP_PaintCameraOverlay` event AND a `DrawCameraOverlayLines(Context, PanelSize)` callable. Neo wires: `BP_PaintCameraOverlay` → `DrawCameraOverlayLines(Context, PanelSize)`. The helper does `GetCameraOverlayLines()` internally and paints each line at `UV * PanelSize`.

COMPILED: n/a (this is a request for new C++ code)
NEEDS BACK: The new `BP_PaintCameraOverlay` event + `DrawCameraOverlayLines` helper, compiled and ready. After rebuild, Neo will wire it in BP and send PIE screenshots.

---

## [2026-06-14 05:00] Neo → Claude — BUG: Chase camera top-down view shows wrong aircraft label

STATUS: bug
TASK: In Chase camera mode, when the camera is in the top-down sub-angle, the label displayed over the aircraft is for the WRONG aircraft. Other camera modes (Tower, Approach, Overview, Operator) show correct labels.

### What's confirmed working (BP side)

- Camera labels are now fully functional: pool of 30 `WBP_CameraLabel` widgets, synced every tick from `GetCameraLabels()`, positioned at `ScreenUV * Img_CameraFeed.GetCachedGeometry().GetLocalSize()`, clipped to `CanvasCameraOverlay` bounds.
- Labels track correctly in Tower, Overview, and Approach views.
- Labels track correctly in Chase mode from the default behind-and-above angle.
- When switching to the **top-down** chase sub-angle (via `<`/`>` arrows cycling through `EClearanceFollowAngle`), the label shown over the followed aircraft displays the **wrong callsign/FL** — it shows data from a different aircraft.

### Probable C++ cause

`GetCameraLabels()` projects all aircraft through the PIP camera's view-projection matrix. In the top-down chase angle, the camera is directly above the followed aircraft looking straight down. The projection math may be:

1. **Misidentifying which aircraft is at which screen position** — if the projection flips or inverts in the top-down orientation (pitch = -90), the UV coordinates could map to the wrong aircraft.
2. **The followed aircraft's world position is slightly offset** from where the camera thinks it is — the camera might be positioned at the aircraft's location but the aircraft's label projection lands at a different UV than expected.
3. **Z-fighting in projection** — when looking straight down, multiple aircraft at different altitudes but similar XY positions could project to similar UVs, and the wrong one ends up "closest" or first in the array.

### How to verify

Add a diagnostic log in `GetCameraLabels()` when the PIP view is `Follow` and the follow angle is the top-down one:
```cpp
UE_LOG(LogTemp, Warning, TEXT("CamLabel: %s UV=(%.3f,%.3f) WorldPos=(%.0f,%.0f,%.0f)"),
    *Label.Callsign.ToString(), Label.ScreenUV.X, Label.ScreenUV.Y,
    AircraftWorldPos.X, AircraftWorldPos.Y, AircraftWorldPos.Z);
```

Check if the followed aircraft's callsign has a UV near (0.5, 0.5) (centre of screen) in top-down view. If it doesn't, the projection is wrong for that camera angle.

### BP side is done

No BP changes needed — the sync loop correctly displays whatever `GetCameraLabels()` returns. The bug is in the projection/identification logic in C++.

COMPILED: n/a (C++ investigation needed)
NEEDS BACK: Fix the projection for top-down chase angle so the correct callsign appears over the correct aircraft.

---

## [2026-06-14 03:30] Neo → Claude — REQUEST: GetCameraLabels should return ALL aircraft, not just frustum-visible ones

STATUS: request
TASK: Change `GetCameraLabels()` so it projects ALL aircraft in the simulation to the current PIP camera's screen space — not just the ones inside the frustum. Labels that are behind the camera or wildly off-screen can still be filtered, but everything within a generous FOV should be included.

### The problem (Jeremy's feedback)

The HUD overlay labels only show on 1-2 aircraft that happen to be directly in front of the current PIP camera. Switching between Tower/Chase/Approach/Overview modes doesn't help — most aircraft are missing labels. The instructor expects to see a label on EVERY aircraft from EVERY camera angle, like DCS's `labels=ON` or MSFS's traffic labels. The labels should work like world-space billboards: they're pinned to each aircraft's 3D position but rendered as 2D text on the overlay.

### What needs to change in C++

1. **Don't cull by frustum.** The current implementation filters to only aircraft inside the PIP camera's frustum. Remove that filter — project ALL aircraft through the camera's view-projection matrix to get screen UVs.

2. **Cull only behind-camera.** After projecting, discard aircraft where the depth (W component after projection, or the dot product with the camera forward) is negative (behind the camera). Everything else should be included even if the UV is outside 0..1 — the UMG canvas will naturally clip widgets outside the overlay bounds.

3. **Optional: clamp or mark edge labels.** If a projected UV is outside 0..1 but the aircraft is in front of the camera, you could clamp it to the edge of the screen so the label sits at the border pointing toward the off-screen aircraft. But that's a nice-to-have — for now, just returning all front-hemisphere projections is enough.

### Suggested projection approach

```cpp
TArray<FInstructorCameraLabel> UClearanceInstructorPanel::GetCameraLabels() const
{
    TArray<FInstructorCameraLabel> Out;
    if (!Controller || !Controller->SceneCaptureComponent) return Out;

    // Get the PIP camera's view-projection matrix
    const FTransform CamTransform = Controller->SceneCaptureComponent->GetComponentTransform();
    const FVector CamLocation = CamTransform.GetLocation();
    const FVector CamForward = CamTransform.GetUnitAxis(EAxis::X);

    // Build view + projection matrices from the capture component
    FMinimalViewInfo ViewInfo;
    ViewInfo.Location = CamLocation;
    ViewInfo.Rotation = CamTransform.GetRotation().Rotator();
    ViewInfo.FOV = Controller->SceneCaptureComponent->FOVAngle;

    FMatrix ViewMatrix, ProjectionMatrix, ViewProjectionMatrix;
    UGameplayStatics::GetViewProjectionMatrix(ViewInfo, ViewMatrix, ProjectionMatrix, ViewProjectionMatrix);
    // Or build manually from the component's transform + FOV

    for (const FAircraftState& Aircraft : GetAllAircraft())
    {
        FVector WorldPos = Aircraft.Position; // or however you get world location

        // Check if in front of camera
        if (FVector::DotProduct(WorldPos - CamLocation, CamForward) <= 0.f)
            continue; // behind camera, skip

        // Project to screen UV
        FVector4 Projected = ViewProjectionMatrix.TransformFVector4(FVector4(WorldPos, 1.0f));
        if (Projected.W <= 0.f) continue; // behind near plane

        FVector2D ScreenUV;
        ScreenUV.X = (Projected.X / Projected.W * 0.5f) + 0.5f;
        ScreenUV.Y = (-Projected.Y / Projected.W * 0.5f) + 0.5f; // flip Y for top-left origin

        // Include even if outside 0..1 — UMG will clip naturally
        FInstructorCameraLabel Label;
        Label.Callsign = Aircraft.Callsign;
        Label.ScreenUV = ScreenUV;
        Label.ThreatClass = Aircraft.ThreatClass;
        Label.FlightLevel = Aircraft.FlightLevel;
        Out.Add(Label);
    }
    return Out;
}
```

The exact details depend on your internal data structures — the key change is: iterate ALL aircraft, project through the PIP camera matrix, only skip behind-camera ones.

### BP side is done

The UMG overlay, widget pool (30 WBP_CameraLabel widgets), and tick sync loop are fully wired and compiled. The sync loop already handles:
- Pool widgets 0..N-1: visible, positioned at `ScreenUV * canvasSize`, callsign text, FL text, color by ThreatClass
- Pool widgets N..29: collapsed

No BP changes needed — just the C++ returning more labels will make them all appear.

COMPILED: n/a (C++ change needed)
NEEDS BACK: Updated `GetCameraLabels()` that returns all front-hemisphere aircraft. Rebuild. Then I'll PIE test and screenshot.

---

## [2026-06-13 04:00] Neo → Claude — REQUEST: Reset chase sub-angle to Chase when selected aircraft changes

STATUS: request
TASK: When `SetSelectedCallsign` is called (user clicks a different aircraft row), reset the PIP chase sub-angle back to `EClearanceFollowAngle::Chase` (the default behind-and-above view).

### Current behaviour

The chase sub-angle persists when switching aircraft. User clicks `<`/`>` arrows to cycle to e.g. `Top`, then clicks a different aircraft — the camera jumps to the new aircraft but stays in `Top` view. User expects it to reset to the default `Chase` angle each time they select a new plane.

### Desired behaviour

Inside `UClearanceInstructorPanel::SetSelectedCallsign` (or wherever the PIP follow target is updated), add a reset:

```cpp
// After pushing the new callsign to the controller's follow target:
if (Controller)
{
    Controller->SetInstructorPipFollowAngle(EClearanceFollowAngle::Chase);
}
```

Or if there's a stored angle on the panel side, reset that too.

### BP side is done

The `<`/`>` arrows on the camera feed overlay now call `CycleChaseAnglePrev`/`CycleChaseAngleNext` when `GetInstructorPipView() == Follow`. No BP changes needed for this reset — it's purely a C++ behaviour change in `SetSelectedCallsign`.

COMPILED: n/a (C++ change needed)
NEEDS BACK: Confirm the angle resets to `Chase` on aircraft selection change. Rebuild.

---

## [2026-06-13 02:30] Neo → Claude — BUG: PickApproachRunwayByLabel fires but does nothing visible

STATUS: bug
TASK: `PickApproachRunwayByLabel` is being called correctly from BP but has no visible effect — the PIP camera doesn't change to the selected runway approach view.

### What's confirmed working (BP side)

- All 4 static runway buttons are wired: `OnClicked(Btn_Rwy27R)` → `PickApproachRunwayByLabel("RWY 27R")` etc.
- Using the **Instructor|Camera** version (UClearanceInstructorPanel), self auto-resolves — no null, no crash.
- Added a `Print String("CLICKED RWY 27R")` after the 27R call — it prints on screen every click. The function IS being called.
- No errors in PIE log, no crash, just… nothing happens visually.

### What's likely wrong (C++ side)

The function probably looks up the label in `GetApproachRunwayLabels()`, finds the index, and calls `SetInstructorPipApproachRunway(idx)` internally. But either:
1. The label lookup fails (case mismatch? extra whitespace? labels changed since you wrote the function?)
2. The index is set but `UpdateInstructorPip` doesn't react because the PIP view isn't already in Approach mode when the button is clicked (the user clicks APPROACH first, THEN clicks a runway — but maybe the view enum needs to be Approach AND the runway index set?)
3. The function is a no-op / stub that was never implemented beyond the declaration

### How to verify

Add `UE_LOG(LogTemp, Warning, TEXT("PickApproachRunwayByLabel: Label='%s' found=%d idx=%d"), *Label, bFound, FoundIdx)` inside the function body. If the label is "RWY 27R" and the array contains "RWY 27R", it should match. If labels are different (e.g. "Rwy 27R" or "27R"), we need to know the exact format.

Also confirm the function actually calls `SetInstructorPipApproachRunway(FoundIdx)` after finding the match, not just storing it silently.

NEEDS BACK: Either a fix so the camera changes on click, or the exact labels from `GetApproachRunwayLabels()` so I can update the button strings to match.

---

## [2026-06-12 23:50] Neo → Claude — BUG: Operator PIP view doesn't rotate with the operator's camera

STATUS: bug
TASK: Operator PIP view tracks the operator pawn's position but not its rotation.

### What's working

- Operator view activates correctly when clicking OPERATOR button.
- Camera position follows the operator pawn as it moves around the level.

### What's broken

The PIP camera doesn't rotate when the operator looks around. The view stays at a fixed rotation while the position updates. The instructor sees the operator moving but always looking the same direction.

### Probable C++ cause

In `UpdateInstructorPip`, the Operator case likely sets `SceneCaptureComponent->SetWorldLocationAndRotation(...)` but the rotation is either:
1. Not being read from the pawn's view rotation (`GetViewRotation()`)
2. Using the actor rotation instead of the camera/controller rotation
3. The `UCameraComponent` path works but the fallback path (`GetPawnViewLocation` + `GetViewRotation`) isn't applying the rotation to the capture component

If using `UCameraComponent`, both `GetComponentLocation()` and `GetComponentRotation()` should be passed. If using the fallback, `Pawn->GetViewRotation()` (which reads from the controller) should be used for rotation.

COMPILED: n/a (BP side is correct — just calls `SetInstructorPipView(Operator)`)
NEEDS BACK: Fix rotation tracking in the Operator case of `UpdateInstructorPip`. Rebuild.

---

## [2026-06-12 23:45] Neo → Claude — BUG: Approach camera doesn't move when selecting different runways

STATUS: bug
TASK: `SetInstructorPipApproachRunway(int32 Index)` does not reposition the camera to the selected runway's approach path.

### What's working (BP side confirmed)

1. **Runway picker popup works.** Clicking APPROACH shows 4 runway buttons with correct distinct labels (e.g. "RWY 36R", "RWY 36L", "RWY 18R", "RWY 18L").
2. **Each button sends the correct index.** `Btn_Rwy0` → `SetInstructorPipApproachRunway(0)`, `Btn_Rwy1` → `SetInstructorPipApproachRunway(1)`, `Btn_Rwy2` → `SetInstructorPipApproachRunway(2)`, `Btn_Rwy3` → `SetInstructorPipApproachRunway(3)`. Verified via T3D export — `GetArrayItem` nodes have `DefaultValue` of 0, 1, 2, 3 respectively.
3. **Popup collapses after selection.** Each handler calls `SetVisibility(VBox_ApproachSelector, Collapsed)` after the runway call.
4. **`GetInstructorPipApproachRunwayIndex()`** presumably returns the updated index.

### What's broken

The camera stays on the same runway (appears to always show the first runway) regardless of which button is clicked. Clicking "RWY 36L" vs "RWY 18R" shows the same camera angle.

### Probable C++ cause

In `UpdateInstructorPip`, the Approach case likely positions the camera using a fixed runway transform (e.g. always index 0, or always the first threshold) instead of reading the stored runway index set by `SetInstructorPipApproachRunway`. The stored index needs to be used to look up the correct runway threshold position/rotation from the airspace data and position the capture camera accordingly.

COMPILED: n/a (BP side is correct)
NEEDS BACK: Fix `UpdateInstructorPip` Approach case to use the runway index from `SetInstructorPipApproachRunway` to position the camera at the selected runway's approach path. Rebuild.

---

## [2026-06-12 19:30] Neo → Claude — BUG: Camera feed shows black — RT not rendering

STATUS: bug
TASK: Debug why `Img_CameraFeed` shows solid black after clicking "CAMERA VIEW".

### What's working (BP side confirmed)

1. **Toggle works.** Clicking "CAMERA VIEW" correctly: collapses `ScopeCanvasBorder`, shows `Img_CameraFeed` (Visible), shows `HBox_CameraModes`, highlights "CAMERA VIEW" cyan, dims "TRUTH SCOPE" grey.
2. **RebindCameraFeedBrush is called.** It fires in the True path of both toggle handlers before the visibility swaps.
3. **Mode buttons work.** "OVERVIEW" highlights cyan (see screenshot). The 4 mode buttons all call `SetInstructorPipView()`.
4. **Default visibility fixed.** `Img_CameraFeed` and `HBox_CameraModes` now default to `Collapsed` — no more black showing behind the scope on startup.

### What's broken

The `Img_CameraFeed` image widget shows **solid black**. No camera feed visible in any mode (Tower/Chase/Approach/Overview tested via button clicks).

### Possible C++ causes

1. **RT not created.** `GetInstructorPipRT()` returns null → `RebindCameraFeedBrush` silently does nothing (no brush bound, image stays default black).
2. **SceneCapture not enabled.** `ToggleScopeCameraView()` calls `SetInstructorPipEnabled(true)` on the controller, but maybe the SceneCapture2D component doesn't exist or isn't configured.
3. **SceneCapture capturing nothing.** The capture component exists and has an RT, but its position/rotation points at empty space (or is at origin under the floor).
4. **RT size mismatch.** The RT is 0x0 or has an incompatible format for UMG Image display.

### Diagnostic suggestion

Add a `UE_LOG` or `Print String` inside `RebindCameraFeedBrush` to confirm:
- Is `GetInstructorPipRT()` returning a valid pointer or null?
- If valid, what's the RT's `SizeX` / `SizeY`?
- Is the FSlateBrush being built and applied?

Also confirm in `ToggleScopeCameraView`:
- Is `SetInstructorPipEnabled(true)` actually being called?
- Does the controller's `UpdateInstructorPip` tick function run after enable?
- Is there a SceneCapture2D actor/component in the level?

### Screenshot attached

Jeremy shared a PIE screenshot showing the panel with "CAMERA VIEW" active, solid black where the feed should be, mode buttons visible with "OVERVIEW" highlighted cyan.

COMPILED: n/a (BP side is correct)
NEEDS BACK: Debug the RT pipeline — confirm RT exists, SceneCapture is rendering, and `RebindCameraFeedBrush` receives a non-null RT. Add logging if needed and report back.

---

## [2026-06-12 19:00] Neo → Claude — DONE: RebindCameraFeedBrush wired, old Construct RT binding removed

STATUS: done
TASK: Apply the fix from your `[2026-06-12 18:15]` handoff — remove null-RT binding from Construct, add `RebindCameraFeedBrush` to toggle handler True paths.

### What changed

1. **Removed** the `Event Construct` → `GetInstructorPipRT` → `SetBrushResourceObject(Img_CameraFeed)` chain.
   - Deleted 3 nodes: `GetInstructorPipRT` (GUID `E89CF362`), `SetBrushResourceObject` (GUID `AE145367`), and the associated `Get Img_CameraFeed` (GUID `CF9DCD38`).
   - The Construct chain now ends after the combo box default-option setup (Set Scenario Option) with no further exec.

2. **Added** `RebindCameraFeedBrush(Img_CameraFeed)` in **both** toggle handler True paths:
   - **Btn_ShowScope handler** (Branch `265A1981`): True → `RebindCameraFeedBrush` (GUID `45C75702`) → SetVis Collapsed ScopeCanvasBorder → ...rest of True path
   - **Btn_ShowCamera handler** (Branch `CBDF3CD2`): True → `RebindCameraFeedBrush` (GUID `2EEE69CB`) → SetVis Collapsed ScopeCanvasBorder → ...rest of True path
   - Each has its own `Get Img_CameraFeed` feeding the `TargetImage` pin.

### Compiled & saved

- 0 errors, 1 warning (harmless ref-param warning on BP_PaintScope Context — same as always).
- Asset saved.

### Ready for PIE testing

The camera feed should now work:
1. User clicks "CAMERA VIEW" → `ToggleScopeCameraView()` → `IsScopeCameraViewActive()` returns True → `RebindCameraFeedBrush(Img_CameraFeed)` binds the RT → visibility swap shows the camera image.
2. Clicking "TRUTH SCOPE" → toggle fires → `IsScopeCameraViewActive()` returns True again (the first handler's branch) or False (the second handler's branch) → either way, the rebind only happens on the True path.

COMPILED: 0 errors
NEEDS BACK: PIE screenshot of the camera feed working. If still blank, I'll check the brush settings on `Img_CameraFeed`.

---

## [2026-06-12 18:00] Neo → Claude — DONE: PIP camera feed UI wired into WBP_InstructorPanel

STATUS: done
TASK: Wire the scope ↔ camera-feed swap UI + camera-mode button row per your `[2026-06-12 17:10]` handoff.

### What's wired

#### 1. Toggle buttons (replaces old `Text_ScopeTitle`)
- Old `Text_ScopeTitle` ("TRUTH SCOPE") **removed**.
- New `HBox_ScopeToggle` (HorizontalBox) in `VBox_Scope` contains:
  - `Btn_ShowScope` → `Text_ShowScope` ("TRUTH SCOPE")
  - `Btn_ShowCamera` → `Text_ShowCamera` ("CAMERA VIEW")
- Both buttons call `ToggleScopeCameraView()` on click.
- After toggle, a Branch on `IsScopeCameraViewActive()` sets:
  - **Camera active (True):** `ScopeCanvasBorder` = Collapsed, `Img_CameraFeed` = Visible, `HBox_CameraModes` = Visible, `HBox_ScopeControls` = Collapsed, `Text_ShowCamera` = cyan, `Text_ShowScope` = grey
  - **Scope active (False):** `ScopeCanvasBorder` = Visible, `Img_CameraFeed` = Collapsed, `HBox_CameraModes` = Collapsed, `HBox_ScopeControls` = Visible, `Text_ShowScope` = cyan, `Text_ShowCamera` = grey

#### 2. Camera feed image
- `Img_CameraFeed` (Image widget) added to `VBox_Scope`, same Fill slot as `ScopeCanvasBorder`.
- Default visibility: `Collapsed` (scope view is default).
- On `Event Construct`: `GetInstructorPipRT()` → `SetBrushResourceObject(Img_CameraFeed, RT)`.
  - This binds the render target to the Image widget's brush once at construction.

#### 3. Camera mode button row
- `HBox_CameraModes` (HorizontalBox) in `VBox_Scope`, default visibility `Collapsed`.
- 4 buttons with Fill sizing:
  - `Btn_Tower` → `Text_Tower` ("TOWER") → `SetInstructorPipView(Tower)`
  - `Btn_Chase` → `Text_Chase` ("CHASE") → `SetInstructorPipView(Follow)`
  - `Btn_Approach` → `Text_Approach` ("APPROACH") → `SetInstructorPipView(Approach)`
  - `Btn_Overview` → `Text_Overview` ("OVERVIEW") → `SetInstructorPipView(Overview)`
- After each click, the active button's text is set to cyan, the other 3 to grey.

### Widget variables added (all marked `bIsVariable`)
`Btn_ShowScope`, `Btn_ShowCamera`, `Text_ShowScope`, `Text_ShowCamera`, `Img_CameraFeed`, `HBox_CameraModes`, `HBox_ScopeToggle`, `Btn_Tower`, `Btn_Chase`, `Btn_Approach`, `Btn_Overview`, `Text_Tower`, `Text_Chase`, `Text_Approach`, `Text_Overview`, `HBox_ScopeControls`

### Compiled & saved
- 0 errors, 1 warning (harmless ref-param warning on BP_PaintScope Context — same as always).
- Asset saved.

### Notes
- The brush binding uses `SetBrushResourceObject` which sets the `ResourceObject` property on the Image's brush to the render target. If the RT is null at Construct time (because the controller hasn't created it yet), the image will be black. If this is a problem, we could move the brush binding to the toggle handler instead (set it each time camera view is activated). Let me know.
- The `CHASE` mode calls `SetInstructorPipView(Follow)` as per your spec (the enum value is `Follow`, the button label is `CHASE`).
- I did NOT wire `SetCameraView(EClearanceCameraView::Follow, GetSelectedCallsign())` on selection change for Chase tracking — your handoff mentioned this as optional. If you want it, let me know and I'll add it.

### Pending PIE verification
- Need to test in PIE that:
  1. Toggle buttons swap between scope and camera feed
  2. Camera feed shows the render target (not black/checker)
  3. All 4 mode buttons switch the camera view
  4. Mode button highlighting works correctly

COMPILED: 0 errors
NEEDS BACK: Confirm the RT is created before Construct runs, or tell me to move the brush binding to the toggle handler. Also confirm if you want Chase to auto-track the selected aircraft on selection change.

---

## [2026-06-12 13:00] Neo → Claude — BUG: Auto-fit ignores zones/runways, they go off-screen on scenario start

STATUS: bug
TASK: Make auto-fit `ScopeRangeNm` consider zone and runway distances, not just aircraft.

### Symptom

Jeremy reports: when a scenario starts, the runway, restricted zone, and protected zone go off-screen. Aircraft symbols and range rings render correctly inside the scope area.

### Root cause

The auto-fit logic at `ClearanceInstructorPanel.cpp:69-76` only iterates aircraft:

```cpp
if (bAutoFitScopeRange)
{
    float MaxDistNm = MinAutoFitRangeNm;
    for (const FInstructorAircraftRow& R : Rows)
    {
        MaxDistNm = FMath::Max(MaxDistNm, R.PositionNm.Size());
    }
    ScopeRangeNm = MaxDistNm * FMath::Max(1.f, AutoFitMarginFactor);
}
```

It doesn't account for zone centres + radii or runway threshold positions. If aircraft are at 50nm but a restricted zone is at 150nm, the scope zooms to ~57nm and the zone is projected off-screen.

### Suggested fix

Extend the `MaxDistNm` calculation to also consider zones and runways:

```cpp
if (bAutoFitScopeRange)
{
    float MaxDistNm = MinAutoFitRangeNm;

    // Aircraft
    for (const FInstructorAircraftRow& R : Rows)
    {
        MaxDistNm = FMath::Max(MaxDistNm, R.PositionNm.Size());
    }

    // Zones — include centre distance + radius so the full circle fits
    for (const FInstructorZoneMarker& Z : GetZoneMarkers())
    {
        MaxDistNm = FMath::Max(MaxDistNm, Z.PositionNm.Size() + Z.RadiusNm);
    }

    // Runways — include threshold position
    for (const FRunwayInfo& Rwy : GetRunwayMarkers())
    {
        MaxDistNm = FMath::Max(MaxDistNm, Rwy.ThresholdNm.Size());
    }

    ScopeRangeNm = MaxDistNm * FMath::Max(1.f, AutoFitMarginFactor);
}
```

This ensures the scope always zooms wide enough to show everything — aircraft, zones, and runways.

### Also confirmed

- `DrawAircraftLabel` wired and working — added after `DrawAffiliationSymbol` in the aircraft loop. Labels appear with callsign + FL. `bShowFullDataBlocks` getter connected for the toggle.
- Zone/runway proportional scaling fix (`[12:00]`) verified — scaling is correct when things are on-screen. The only issue is the auto-fit range being too narrow.

COMPILED: n/a (C++ fix needed)
NEEDS BACK: Extend auto-fit to include zones + runways, rebuild.

---

## [2026-06-12 11:30] Neo → Claude — BUG: Zone/runway markers don't scale proportionally with scope zoom

STATUS: bug
TASK: Fix `DrawZoneMarker` and `DrawRunwayMarker` so zone/runway markers scale proportionally when `ScopeRangeNm` changes.

### Symptom

Jeremy reports: zones and runways **move** when the scope zooms (auto-fit or manual), but they don't scale **proportionally** with the rest of the scope. Aircraft symbols stay correctly positioned relative to the range rings, but the zone circles and runway ticks drift relative to the aircraft.

### Root cause analysis

All three projection functions use `ScopeRangeNm` as the member variable:

```cpp
// ScopeNmToPixel (line 402) — used for aircraft positions
const float Range = FMath::Max(1.f, ScopeRangeNm);
const float Px = ScopeCentre.X + (PositionNm.X / Range) * ScopePixelRadius;

// DrawZoneMarker (line 581) — used for zone positions
const float Range = FMath::Max(1.f, ScopeRangeNm);
const FVector2D Centre = ScopeCentre + FVector2D(
    (Zone.PositionNm.X / Range) * ScopePixelRadius, ...);

// DrawRunwayMarker (line 610) — used for runway positions
const float Range = FMath::Max(1.f, ScopeRangeNm);
const FVector2D Threshold = ScopeCentre + FVector2D(
    (Runway.ThresholdNm.X / Range) * ScopePixelRadius, ...);
```

The math is the same, so in theory they should all scale identically. Possible causes of the proportional mismatch:

1. **Timing:** Auto-fit updates `ScopeRangeNm` during `RefreshPanel()`, but `BP_PaintScope` fires during `NativePaint()` which is a different frame phase. If `ScopeRangeNm` changes between when `ScopeNmToPixel` (pure, called lazily) evaluates vs when `DrawZoneMarker` (exec, called in sequence) evaluates, the two could see different `ScopeRangeNm` values within the same paint pass. This is unlikely but possible if auto-fit runs mid-paint.

2. **`ScopePixelRadius` vs `ScopeRangeNm` mismatch:** `ScopePixelRadius` is computed in BP from widget geometry (`Min(Width, Height) * 0.46`). The draw helpers take this as a parameter. But they also read `ScopeRangeNm` from the member. If `ScopePixelRadius` was computed in frame N and `ScopeRangeNm` was updated by auto-fit for frame N+1, the ratio is wrong.

### Suggested fix

Make the draw helpers take `ScopeRangeNm` as an explicit parameter instead of reading it from the member, so all calls within a single paint pass use the same snapshot:

```cpp
// Option A: Add ScopeRangeNm parameter to DrawZoneMarker / DrawRunwayMarker
void DrawZoneMarker(FPaintContext& Context, FVector2D ScopeCentre,
    float ScopePixelRadius, float ScopeRangeNm, const FInstructorZoneMarker& Zone);

void DrawRunwayMarker(FPaintContext& Context, FVector2D ScopeCentre,
    float ScopePixelRadius, float ScopeRangeNm, const FRunwayInfo& Runway);
```

```cpp
// Option B: Simpler — snapshot ScopeRangeNm at the start of NativePaint and
// store it as a temp member (or pass it into BP_PaintScope as a third param).
// Then all draw calls within that paint pass use the same value.
```

Option B is cleaner — add `ScopeRangeNm` as a third output parameter on `BP_PaintScope`, or just snapshot it in `NativePaint` before calling `BP_PaintScope` so the member value is stable for the entire paint pass.

Or, simplest of all: the draw helpers could just call `ScopeNmToPixel()` internally for their position projection instead of duplicating the formula inline. That way there's exactly one projection path and no divergence possible.

COMPILED: n/a
NEEDS BACK: Fix the proportional scaling in the draw helpers, rebuild. No BP changes needed unless you add a new parameter to the function signatures (in which case I'll rewire).

---

## [2026-06-12 11:00] Neo → Claude — DONE: Tier 1 scope overlay fully wired (zones, runways, chaff, selected ring)

STATUS: done
TASK: Wire up the full Tier 1 scope overlay per your `[2026-06-12 10:30]` handoff.

### What's wired

The `BP_PaintScope` graph in `WBP_InstructorPanel` now paints in the correct layer order (bottom to top):

```
Event BP_PaintScope(Context, PanelSize):
  // ... existing ScopeCentre / ScopePixelRadius computation (unchanged) ...

  // Layer 1 (bottom): Restricted/Protected zones
  ForEach Zone in GetZoneMarkers():
    DrawZoneMarker(Context, ScopeCentre, ScopePixelRadius, Zone)

  // Layer 2: Runway thresholds
  ForEach Rwy in GetRunwayMarkers():
    DrawRunwayMarker(Context, ScopeCentre, ScopePixelRadius, Rwy)

  // Layer 3: Chaff clouds
  ForEach Chaff in GetActiveChaffMarkers():
    ChaffPx = ScopeNmToPixel(Chaff.PositionNm, ScopeCentre, ScopePixelRadius)
    DrawScopeChaffCloud(Context, ChaffPx, Chaff.AgeFrac)

  // Layer 4: Sector boundary (on top of zones/runways for crispness)
  DrawScopeBoundary(Context, ScopeCentre, ScopePixelRadius)

  // Layer 5 (top): Aircraft symbols + selected highlight
  ForEach Row in GetAircraftRows():
    AcftPx = ScopeNmToPixel(Row.PositionNm, ScopeCentre, ScopePixelRadius)
    if (Row.Callsign == GetSelectedCallsign()):
      DrawSelectedRing(Context, AcftPx, 18.0)
    DrawAffiliationSymbol(Context, AcftPx, Row.ThreatClass, Row.bIsMilitary,
                          Row.Heading, Row.CurrentAlertLevel, 12.0)
```

### Key node GUIDs (new nodes)

- `GetZoneMarkers` — `FA9FFB64`
- `ForEachZone` — `33DD0DD2`
- `DrawZoneMarker` — `DB1F015F`
- `GetRunwayMarkers` — `DB12134D`
- `ForEachRunway` — `3183F131`
- `DrawRunwayMarker` — `51060800`
- `GetActiveChaffMarkers` — `26D8FBA1`
- `ForEachChaff` — `E8BF0E53`
- `BreakChaffMarker` — `1DD3E4B0`
- `ChaffNmToPixel` — `E9CAFB6A`
- `DrawScopeChaffCloud` — `B22DF4E3`
- `GetSelCallsign (for ring)` — `9864A96B`
- `EqualCallsign (Name)` — `AE586650`
- `BranchSelected` — `0F2B27E1`
- `DrawSelectedRing` — `96319C29`

### Exec chain

`BP_PaintScope:then` → `ForEachZone:Exec` → `ForEachZone:Completed` → `ForEachRunway:Exec` → `ForEachRunway:Completed` → `ForEachChaff:Exec` → `ForEachChaff:Completed` → `DrawScopeBoundary:execute` → `DrawScopeBoundary:then` → `ForEachLoop(aircraft):Exec` → `ForEachLoop:LoopBody` → `Branch(isSelected):execute` → `True: DrawSelectedRing:execute → DrawSelectedRing:then → DrawAffiliationSymbol:execute` / `False: → DrawAffiliationSymbol:execute`

### Compiled & saved

- 0 errors, 1 warning (harmless ref-param warning on BP_PaintScope Context — same as always)
- Asset saved.

### Notes

- The `Equal (==)` wildcard node resolved to `Equal (Name)` once connected to the FName pins from `BreakAircraftRow:Callsign` and `GetSelectedCallsign:ReturnValue`. It shows as `Equal (Name)` in the graph dump.
- The selected-ring Branch check only tests `Row.Callsign == GetSelectedCallsign()`. When no aircraft is selected, `GetSelectedCallsign()` returns `NAME_None` which won't match any real callsign, so the ring won't draw. No separate `!= None` check is needed.
- Auto-fit (`bAutoFitScopeRange`) acknowledged — no BP changes needed since `ScopeNmToPixel` already reads `ScopeRangeNm` internally.

COMPILED: 0 errors
NEEDS BACK: Test in PIE to confirm all layers render correctly. If the auto-fit C++ isn't linked yet, the scope may still be at whatever the current `ScopeRangeNm` default is — that's fine, the new layers will still paint.

---

## [2026-06-12 10:00] Neo → Claude — REQUEST: Auto-zoom ScopeRangeNm to fit all aircraft

STATUS: request
TASK: Make `ScopeRangeNm` auto-fit to the farthest aircraft each refresh so the TRUTH SCOPE always shows every aircraft in the sector.

### Problem

`ScopeRangeNm` is hardcoded to 80nm, but scenarios have wildly different extents — exit radius can be 300nm on one scenario and 2000nm on another. Aircraft outside the scope range get projected off-screen and are invisible on the TRUTH SCOPE. The instructor needs to see ALL aircraft at all times.

### Proposed solution

Add an auto-zoom function that runs each panel refresh (every `RefreshIntervalSec`). Pseudocode:

```cpp
void UClearanceInstructorPanel::AutoFitScopeRange()
{
    const TArray<FInstructorAircraftRow>& Rows = GetAircraftRows();
    float MaxDist = 40.f; // floor so scope doesn't collapse when empty
    for (const auto& Row : Rows)
    {
        float Dist = Row.PositionNm.Size(); // distance from sector origin
        MaxDist = FMath::Max(MaxDist, Dist);
    }
    ScopeRangeNm = MaxDist * 1.15f; // 15% margin so aircraft aren't on the edge
}
```

Call this from `RefreshPanel()` or `NativeTick()` before `BP_PaintScope` fires.

### Alternative: use scenario entry/exit radius

If you'd prefer not to recompute per-frame, you could set `ScopeRangeNm` once when a scenario loads, based on the scenario's `EntryRadiusNm` or exit radius (whichever is larger) with margin. But per-frame auto-fit is simpler and handles edge cases (aircraft drifting beyond entry radius, etc.).

### Where it lives

- `ScopeRangeNm` is already a `UPROPERTY(EditAnywhere, BlueprintReadWrite)` on `UClearanceInstructorPanel` (line 189 in the header)
- `ScopeNmToPixel()` already uses `ScopeRangeNm` for the projection — no BP changes needed, just set the value before paint

COMPILED: n/a (request only)
NEEDS BACK: The auto-zoom function, called before paint. No BP wiring needed — `ScopeNmToPixel` already reads `ScopeRangeNm`.

---

## [2026-06-12 09:00] Neo → Claude — DONE: Scope paint rewired with ScopeCentre + ScopePixelRadius

STATUS: done
TASK: Rewire `BP_PaintScope` graph to use the new `DrawScopeBoundary(Context, ScopeCentre, ScopePixelRadius)` and `ScopeNmToPixel(PositionNm, ScopeCentre, ScopePixelRadius)` signatures.

### What's wired

The full scope paint graph in `WBP_InstructorPanel` EventGraph is now:

```
Event BP_PaintScope(Context, PanelSize):
  ScopeGeom     = ScopeCanvasBorder.GetCachedGeometry()
  ScopeAbsPos   = LocalToAbsolute(ScopeGeom, (0,0))
  PanelAbsPos   = LocalToAbsolute(Self.GetCachedGeometry(), (0,0))
  ScopeLocalPos = ScopeAbsPos - PanelAbsPos
  ScopeSize     = GetLocalSize(ScopeGeom)
  ScopeCentre   = ScopeLocalPos + ScopeSize * 0.5
  ScopePixelRadius = Min(ScopeSize.X, ScopeSize.Y) * 0.46

  DrawScopeBoundary(Context, ScopeCentre, ScopePixelRadius)

  ForEach Row in GetAircraftRows():
    AcftPx = ScopeNmToPixel(Row.PositionNm, ScopeCentre, ScopePixelRadius)
    DrawAffiliationSymbol(Context, AcftPx, Row.ThreatClass, Row.bIsMilitary,
                          Row.Heading, Row.CurrentAlertLevel, 12.0)
```

### Exec chain

`BP_PaintScope:then` → `DrawScopeBoundary:execute` → `DrawScopeBoundary:then` → `ForEachLoop:Exec` → `ForEachLoop:LoopBody` → `DrawAffiliationSymbol:execute`

### Also confirmed

- `Btn_ClearEmergency` → `InjectClearEmergency(GetSelectedCallsign())` — still wired, survived the rebuild.
- Compiled: 0 errors, 1 warning (harmless ref-param warning on BP_PaintScope Context).
- Saved.

COMPILED: n/a (BP-side work)
NEEDS BACK: Test in PIE to verify the scope renders inside the TRUTH SCOPE area. If the position is still wrong, the `LocalToAbsolute` approach may need adjusting — but the math should be correct since both widgets share the same absolute coordinate space during NativePaint.

---

## [2026-06-12 08:20] Neo → Claude — FIX NEEDED: Draw helpers need explicit ScopeCentre parameter

STATUS: resolved (see [2026-06-12 09:00] above)
TASK: Add a `ScopeCentre` (FVector2D) parameter to `DrawScopeBoundary`, `DrawAffiliationSymbol`, and `ScopeNmToPixel` so the scope renders at the correct position within the panel.

### The problem

The `const` fix landed — all three draw helpers now have exec pins and are callable. The exec chain is fully wired:
`BP_PaintScope` → `DrawScopeBoundary` → `ForEachLoop(GetAircraftRows)` → `DrawAffiliationSymbol`

**But the scope renders in the wrong location.** The range rings appear in the bottom-left of the panel (over the aircraft list / event log area), not inside the TRUTH SCOPE centre area.

Root cause: `DrawScopeBoundary` and `ScopeNmToPixel` both assume `Centre = PanelSize / 2`. But `PanelSize` in `BP_PaintScope` is the **entire panel's** pixel size (~1500x800), and the paint coordinate origin is the panel's top-left corner. So `Centre = (750, 400)` lands roughly mid-panel — but the TRUTH SCOPE widget is positioned at roughly X=380, Y=100 within the panel layout.

I tried passing `ScopeCanvasBorder.GetCachedGeometry().GetLocalSize()` instead of `PanelSize`, which makes the circles the right **size** (~560x700), but they're still drawn from the panel origin (0,0) — so the circle centres at (280, 350) from panel top-left, which is still over the aircraft list area.

I also set `ClipToBoundsAlways` on `ScopeCanvasBorder` but that doesn't help — Slate clipping only clips a widget's own children, not paint calls made by the **parent's** NativePaint. The draws happen in the panel's paint context.

### What I need

Add an explicit `ScopeCentre` parameter to the three functions that currently assume `Centre = PanelSize / 2`:

```cpp
// DrawScopeBoundary — add ScopeCentre, use it instead of PanelSize/2
UFUNCTION(BlueprintCallable, Category = "Instructor|Scope")
void DrawScopeBoundary(FPaintContext& Context, FVector2D PanelSize, FVector2D ScopeCentre);

// ScopeNmToPixel — add ScopeCentre, offset the result by it
UFUNCTION(BlueprintCallable, Category = "Instructor|Scope")
FVector2D ScopeNmToPixel(FVector2D PositionNm, FVector2D PanelSize, FVector2D ScopeCentre);
```

DrawAffiliationSymbol doesn't need a change — it already takes `ScopePixelCentre` as an absolute pixel position (fed from `ScopeNmToPixel`'s return).

### How I'll wire it in BP

```
Event BP_PaintScope(Context, PanelSize):
  ScopeGeom = ScopeCanvasBorder.GetCachedGeometry()
  ScopeLocalPos = ScopeGeom.GetAbsolutePosition() - Self.GetCachedGeometry().GetAbsolutePosition()
  ScopeSize = ScopeGeom.GetLocalSize()
  ScopeCentre = ScopeLocalPos + ScopeSize * 0.5

  DrawScopeBoundary(Context, ScopeSize, ScopeCentre)

  ForEach Row in GetAircraftRows():
    AcftPx = ScopeNmToPixel(Row.PositionNm, ScopeSize, ScopeCentre)
    DrawAffiliationSymbol(Context, AcftPx, ...)
```

This way the range rings and aircraft symbols are centred on the TRUTH SCOPE widget's actual position within the panel, regardless of layout.

### Alternative (simpler, if you prefer)

Instead of adding a parameter, you could change the C++ to compute the centre internally:
- In `NativePaint`, before calling `BP_PaintScope`, compute the ScopeCanvasBorder's geometry offset and pass it as a third parameter to `BP_PaintScope`.
- Or add a new `UPROPERTY` `ScopeCentreOffset` that I set once.

Either way works. The key requirement is: **the draw functions need to know where the scope area is within the panel, not assume it's centred on PanelSize/2.**

COMPILED: n/a
NEEDS BACK: Add ScopeCentre parameter (or equivalent fix), rebuild, confirm compiled.

---

## [2026-06-12 07:15] Neo → Claude — FIX NEEDED: Draw helpers are Pure/Const — no exec pins, uncallable

STATUS: blocked
TASK: Fix `UFUNCTION` specifiers on the three scope draw helpers so they have exec pins in Blueprint.

### The problem

`DrawScopeBoundary`, `DrawAffiliationSymbol`, and `DrawScopeChaffCloud` are all marked `Pure, Const` in the editor. This means:
- They have **no exec pins** (no execute input, no then output).
- They have **no output pins** (no return value).
- A Pure function with no outputs is **never evaluated** by Blueprint — nothing reads from it, so the engine never calls it.

`BP_PaintScope` event landed correctly after the rebuild — I can see it and wire it. But I can't call any of the draw helpers from it because they're uncallable Pure nodes.

### The fix

Remove the `const` qualifier from each function signature. UE auto-infers `BlueprintPure` from `const` on `BlueprintCallable` functions. Without `const`, they'll get proper exec pins (execute → then) and become callable in sequence.

Change these three:

```cpp
// FROM (current — inferred BlueprintPure because of const):
UFUNCTION(BlueprintCallable, Category = "Instructor|Scope")
void DrawScopeBoundary(FPaintContext& Context, FVector2D PanelSize) const;

UFUNCTION(BlueprintCallable, Category = "Instructor|Scope")
void DrawAffiliationSymbol(FPaintContext& Context, ...) const;

UFUNCTION(BlueprintCallable, Category = "Instructor|Scope")
void DrawScopeChaffCloud(FPaintContext& Context, ...) const;

// TO (remove const — gets exec pins):
UFUNCTION(BlueprintCallable, Category = "Instructor|Scope")
void DrawScopeBoundary(FPaintContext& Context, FVector2D PanelSize);

UFUNCTION(BlueprintCallable, Category = "Instructor|Scope")
void DrawAffiliationSymbol(FPaintContext& Context, ...);

UFUNCTION(BlueprintCallable, Category = "Instructor|Scope")
void DrawScopeChaffCloud(FPaintContext& Context, ...);
```

Also remove `const` from the `.cpp` definitions to match.

### What's already wired (waiting for this fix)

I've added all the nodes in EventGraph:
- `Event BP_PaintScope` → (needs exec to) `DrawScopeBoundary(Context, PanelSize)`
- `GetAircraftRows()` → `ForEachLoop` → `Break InstructorAircraftRow` → `ScopeNmToPixel(PositionNm, PanelSize)` → `DrawAffiliationSymbol(Context, PixelPos, ThreatClass, bIsMilitary, Heading, AlertLevel, 12.0)`

All data pins are connected. The only missing connections are the exec pins on DrawScopeBoundary and DrawAffiliationSymbol — which don't exist yet because of the `const` issue.

### After rebuild

I'll reconnect the exec flow:
1. `EventPaintScope:then` → `DrawScopeBoundary:execute`
2. `DrawScopeBoundary:then` → `ForEachRow:Exec`
3. `ForEachRow:LoopBody` → `DrawSymbol:execute`

Then compile, save, and test in PIE.

COMPILED: n/a
NEEDS BACK: Remove `const` from all three draw helpers, rebuild, confirm compiled.

---

## [2026-06-12 06:30] Neo → Claude — BLOCKED: Mini-scope painting needs BlueprintImplementableEvent for OnPaint

STATUS: blocked
TASK: Wire mini-scope vector painting on `WBP_InstructorPanel` using `DrawScopeBoundary`, `DrawAffiliationSymbol`, `DrawScopeChaffCloud`.

### What's done

- Tasks 1 & 2 from CLAUDE.md are **complete and verified**:
  - `Btn_ClearEmergency` wired: `OnClicked → InjectClearEmergency(GetSelectedCallsign())` — compiled, saved.
  - Affiliation icons wired: `Text_ThreatIcon` in `WBP_InstructorAircraftRow` SetRowData does Switch on EThreatClass → sets unicode shape (▭/◆/⬡/◻) + palette color (cyan/red/amber/green). User verified working.

- All three C++ draw helpers are visible in the editor:
  - `DrawScopeBoundary` (ID `FUNC:WBP_InstructorPanel:Draw_Scope_Boundary:2036`)
  - `DrawAffiliationSymbol` (ID `FUNC:WBP_InstructorPanel:Draw_Affiliation_Symbol:2034`)
  - `DrawScopeChaffCloud` (ID `FUNC:WBP_InstructorPanel:Draw_Scope_Chaff_Cloud:2035`)
- `ScopeNmToPixel` and `GetAircraftRows` are also available.
- `ScopeCanvasBorder` (Border widget, marked as variable) is the target painting area.

### The problem

All three draw helpers require `FPaintContext&` as their first parameter. This type is **only available inside a `NativePaint` / `OnPaint` call** — it cannot be constructed or stored as a variable.

**There is no BlueprintImplementableEvent on `UClearanceInstructorPanel` that fires during painting and passes the `FPaintContext&` through to Blueprint.**

I tried:
1. `find_node` searching for `OnPaint`, `NativePaint`, `PaintScope`, `Paint`, category `Instructor|Scope` — nothing provides a `FPaintContext` to Blueprint.
2. `edit_blueprint add_functions` to create an `OnPaint` override — this creates a **custom BP function**, not a native paint override. BP cannot override `NativePaint` this way.
3. Exhaustive search of the base class events — no paint-related event exists.

### What I need

Add a `BlueprintImplementableEvent` on `UClearanceInstructorPanel` that fires during `NativePaint` and passes the paint context through. Something like:

```cpp
// In UClearanceInstructorPanel header:
protected:
    /** Called during NativePaint — override in Blueprint to draw scope contents. */
    UFUNCTION(BlueprintImplementableEvent, Category = "Instructor|Scope")
    void BP_PaintScope(UPARAM(ref) FPaintContext& Context, FVector2D PanelSize);

    // Override NativePaint to fire the BP event:
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
                              const FSlateRect& MyCullingRect, FSlateDrawElement& OutDrawElements,
                              int32 LayerId, const FWidgetStyle& InWidgetStyle,
                              bool bParentEnabled) const override;
```

```cpp
// In .cpp:
int32 UClearanceInstructorPanel::NativePaint(const FPaintArgs& Args,
    const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
    FSlateDrawElement& OutDrawElements, int32 LayerId,
    const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
    int32 Result = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
                                       OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

    FPaintContext Context(AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
    FVector2D PanelSize = AllottedGeometry.GetLocalSize();
    const_cast<UClearanceInstructorPanel*>(this)->BP_PaintScope(Context, PanelSize);

    return Result;
}
```

**Alternative simpler approach:** Instead of exposing `BP_PaintScope` to Blueprint, you could do the entire scope painting in C++ inside `NativePaint` directly — call `DrawScopeBoundary`, loop `GetAircraftRows` → `ScopeNmToPixel` → `DrawAffiliationSymbol` for each, and `DrawScopeChaffCloud` for jammed aircraft. This avoids the BP event entirely and keeps paint logic in C++ where `FPaintContext` naturally lives. The pseudocode from your `to-neo.md` entry `[2026-06-12 04:35]` is exactly what the loop would look like.

Either approach works for me. If you go the BP event route, I'll wire the scope drawing in Blueprint. If you do it all in C++, I just need confirmation and the scope will paint automatically.

COMPILED: n/a
NEEDS BACK: Either (a) add `BP_PaintScope` BlueprintImplementableEvent + NativePaint override and rebuild, or (b) implement scope painting entirely in C++ NativePaint and rebuild. Let me know which approach you chose.

---

## [2026-06-12 05:45] Neo → Claude — BLOCKED: `InjectClearEmergency` not visible in editor, need rebuild

STATUS: blocked
TASK: Wire `Btn_ClearEmergency` on `WBP_InstructorPanel`.

### What's done

- Badge restructure on `WBP_InstructorAircraftRow` is complete and working (all 5 badges per your spec).
- `Btn_ClearEmergency` button is added to the panel in `HBox_Emergency` next to `Btn_InjectEmergency`, amber "CLEAR EMER" label, marked as variable, compiled clean.
- `OnClicked` event handler node exists (`B52551294FE890D35A88B1AF62EFFC13`) but is **unwired** — no logic connected to it yet.

### What I need

`UClearanceInstructorPanel::InjectClearEmergency(FName Callsign)` does not appear in the editor's node search. It's not available via find_node — only `InjectEmergency`, `InjectClearTraffic`, etc. show up. This means the C++ addition hasn't been compiled into the editor yet.

**I need a full rebuild** (close editor → Build Solution → reopen) so `InjectClearEmergency` becomes visible as a BlueprintCallable node. Once it's there, I'll wire:

```
OnClicked(Btn_ClearEmergency) → InjectClearEmergency(GetSelectedCallsign())
```

NEEDS BACK: Do the rebuild and confirm `InjectClearEmergency` is compiled in. Then tell me to wire it.

---

## [2026-06-12 03:15] Neo → Claude — FIX: PopulateAircraftScrollBox stutters (destroys/recreates every refresh)

STATUS: waiting
TASK: Make `PopulateAircraftScrollBox` update existing rows in-place instead of clearing and recreating all children every call.
DETAILS:
- The aircraft list visually stutters because `PopulateAircraftScrollBox` calls `ScrollBox->ClearChildren()` and recreates every row widget every 0.2s refresh. Even with the tolerances in `AircraftListChanged`, aircraft turning/climbing cause frequent rebuilds.
- **Suggested fix:** Instead of clear+recreate, compare the existing children count to the new Rows count:
  1. If counts match and callsigns match, just call `SetRowData` + `SetSelected` on each existing child (update in-place)
  2. If counts or callsigns differ, do the full clear+recreate (aircraft spawned/departed)
- This way, the scroll box only rebuilds when aircraft actually enter/leave the list, not when their heading/altitude/speed changes.
- The `SetRowData` function I built on `WBP_InstructorAircraftRow` already handles all the text/badge updates, so calling it again on an existing widget will just refresh the display data without destroying/recreating the widget tree.

```cpp
void UClearanceInstructorPanel::PopulateAircraftScrollBox(UScrollBox* ScrollBox, const TArray<FInstructorAircraftRow>& Rows)
{
    if (!ScrollBox || !AircraftRowClass) { return; }

    // Check if we can update in-place (same count, same callsign order)
    bool bCanUpdateInPlace = (ScrollBox->GetChildrenCount() == Rows.Num());
    if (bCanUpdateInPlace)
    {
        for (int32 i = 0; i < Rows.Num(); ++i)
        {
            UUserWidget* Existing = Cast<UUserWidget>(ScrollBox->GetChildAt(i));
            if (!Existing) { bCanUpdateInPlace = false; break; }
            // Could also check callsign via FindFunction("GetCallsign") if desired
        }
    }

    if (!bCanUpdateInPlace)
    {
        // Full rebuild
        ScrollBox->ClearChildren();
        for (const FInstructorAircraftRow& Row : Rows)
        {
            UUserWidget* RowWidget = CreateWidget<UUserWidget>(this, AircraftRowClass);
            if (!RowWidget) { continue; }
            // ... SetRowData + SetSelected via reflection ...
            ScrollBox->AddChild(RowWidget);
        }
    }
    else
    {
        // Update in-place
        for (int32 i = 0; i < Rows.Num(); ++i)
        {
            UUserWidget* RowWidget = Cast<UUserWidget>(ScrollBox->GetChildAt(i));
            // ... SetRowData + SetSelected via reflection ...
        }
    }
}
```

- Also fixed the **click handler** on `WBP_InstructorAircraftRow`: replaced `GetOuterUserWidget` (which returned null because `CreateWidget` sets outer to LocalPlayer, not the panel) with `GetAllWidgetsOfClass(ClearanceInstructorPanel)` → `Get(0)` → `Cast` → `SetSelectedCallsign`. Clicks should now work.

COMPILED: n/a (BP-side fix for clicks is done; C++ fix for stuttering is your side)
NEEDS BACK: Update `PopulateAircraftScrollBox` to update in-place, compile, and confirm.

---

## [2026-06-12 02:30] Neo → Claude — NEED: AircraftRowClass default in C++ constructor

STATUS: blocked
TASK: Set default value for `AircraftRowClass` to `/Game/UI/WBP_InstructorAircraftRow` in the C++ constructor.
DETAILS:
- Your `PopulateAircraftScrollBox` helper reads `this->AircraftRowClass` to create row widgets. I built `SetRowData(FInstructorAircraftRow)` and `SetSelected(bool)` on `WBP_InstructorAircraftRow` as requested — both compiled, 0 errors.
- **The problem:** `TSubclassOf<UUserWidget>` is completely invisible to Python and to the MCP Blueprint tools. I cannot set `AircraftRowClass` via:
  - `cdo.set_editor_property('AircraftRowClass', ...)` → "Failed to find property"
  - `setattr(cdo, 'aircraft_row_class', ...)` → "no attribute"
  - Blueprint variable setter node → doesn't exist in the node palette
  - Widget schema → property doesn't appear at all
- **What I need:** One line in the `UClearanceInstructorPanel` constructor (or inline in the header):

```cpp
// In constructor or header default:
AircraftRowClass = FSoftClassPath(TEXT("/Game/UI/WBP_InstructorAircraftRow.WBP_InstructorAircraftRow_C")).TryLoadClass<UUserWidget>();
```

Or alternatively, use `ConstructorHelpers::FClassFinder` if you prefer:
```cpp
static ConstructorHelpers::FClassFinder<UUserWidget> RowBP(TEXT("/Game/UI/WBP_InstructorAircraftRow"));
if (RowBP.Succeeded()) AircraftRowClass = RowBP.Class;
```

- Either way, the goal is: when `WBP_InstructorPanel` is instantiated, `AircraftRowClass` already points to `WBP_InstructorAircraftRow_C` so `PopulateAircraftScrollBox` can create row widgets without me needing to set the property from the editor.
- Once this compiles, I can wire `OnAircraftListChanged` → `PopulateAircraftScrollBox(ScrollBox_Aircraft, Rows)` and the clickable aircraft list will be fully functional.

COMPILED: n/a
NEEDS BACK: Compile with the default set, then reply in `to-neo.md` so I can finish wiring.

---

## [2026-06-12 00:15] Neo → Claude — NEED C++ helper: PopulateAircraftScrollBox

STATUS: blocked
TASK: Add a BlueprintCallable function to `UClearanceInstructorPanel` that populates `ScrollBox_Aircraft` with clickable aircraft row widgets.
DETAILS:
- **The problem:** The instructor needs to click an aircraft in the list to select it as the inject target (for Jam, Chaff, Scramble, etc.). Currently the aircraft list is a single TextBlock — not clickable. I tried building it purely in Blueprint but MCP tools cannot call BP functions across different Blueprint assets (e.g. calling `InitRow` on `WBP_InstructorAircraftRow` from `WBP_InstructorPanel`). This is a tooling limitation I can't work around.
- **What I need from C++:** A `BlueprintCallable` function on `UClearanceInstructorPanel` that I can call from the `OnAircraftListChanged` BP event:

```cpp
UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
void PopulateAircraftScrollBox(UScrollBox* ScrollBox, const TArray<FInstructorAircraftRow>& Rows);
```

- **What it should do:**
  1. `ScrollBox->ClearChildren()`
  2. For each `FInstructorAircraftRow` in `Rows`:
     - Create a `UButton` widget (dark styled, like the rest of the panel)
     - Create a `UTextBlock` child inside the button with formatted text: `"CALLSIGN  FL###  ###kt  HDG###"` (same format as the current text list)
     - Style the text with the Roboto font, cyan color (`PaletteNormal()`)
     - Bind the button's `OnClicked` to call `SetSelectedCallsign(Row.Callsign)` on `this`
     - Add the button to the ScrollBox
  3. If `SelectedCallsign` is set and matches a row, visually highlight that button (e.g. brighter background or border)

- **Alternatively** if you prefer a simpler approach: the row widget `WBP_InstructorAircraftRow` already exists at `/Game/UI/WBP_InstructorAircraftRow` with a `Btn_Row` button, text labels (Text_Callsign, Text_FlightLevel, Text_Speed, Text_Heading), a `Callsign` variable (FName), and a BP function `InitRow(InCallsign: FName, InAltitude: float, InSpeed: float, InHeading: float)`. Its `Btn_Row.OnClicked` already calls `SetSelectedCallsign` on the parent panel. So the C++ function could instead:
  1. `ScrollBox->ClearChildren()`
  2. For each row: `CreateWidget<UUserWidget>(this, RowWidgetClass)` where `RowWidgetClass` is loaded from `/Game/UI/WBP_InstructorAircraftRow`
  3. Call `InitRow` on it via `FindFunction("InitRow")` + `ProcessEvent`, or just set the `Callsign` property directly and let the row's Event Construct handle text formatting
  4. `ScrollBox->AddChild(RowWidget)`

- **Either approach works.** The pure-UButton approach is self-contained (no dependency on the row BP). The row-widget approach reuses Neo's existing layout with badges (JAM/EMER/INT/MIL).

- **Include headers needed:** `Components/ScrollBox.h`, `Components/Button.h`, `Components/TextBlock.h`

COMPILED: n/a
NEEDS BACK: Compile the new function so I can wire `OnAircraftListChanged` → `Get ScrollBox_Aircraft` → `PopulateAircraftScrollBox(ScrollBox, Rows)` in the BP. Also let me know which approach you chose (pure UButton vs row widget) so I know what to expect.

---

## [2026-06-11 23:59] Neo → Claude — Instructor Panel UPDATED: combos, enums, count ✅

STATUS: done
TASK: Wire combo box population, string→enum conversions, and aircraft count per your handoff.
DETAILS:
- **Combo boxes now populated on Construct:**
  - `Event Construct` → ForEach `GetEmergencyTypeOptions()` → `AddOption(Combo_Emergency)` → ForEach `GetThreatClassOptions()` → `AddOption(Combo_Threat)` → ForEach `GetScenarioOptions()` → `AddOption(Combo_Scenario)`
  - Each combo has its `SelectedOption` set to the first entry (index 0) so they're never blank.
- **String→enum conversions wired:**
  - `Btn_InjectEmergency`: `Combo_Emergency.GetSelectedOption` → `StringToEmergencyType` → `InjectEmergency(..., Kind)` — Kind pin now connected.
  - `Btn_Reclassify`: `Combo_Threat.GetSelectedOption` → `StringToThreatClass` → `InjectClassify(..., NewClass)` — NewClass pin now connected.
- **Aircraft count updated:**
  - `OnAircraftListChanged` handler now chains: after setting aircraft list text → `Length(Rows)` → `Format "AIRCRAFT ({N})"` → `SetText(Text_AircraftCount)`. Header updates dynamically.
- **Compiled: 0 errors, 0 warnings. Saved.**

All gaps from your [2026-06-11 23:55] handoff are resolved except the two deferred items (mini-scope OnPaint and row sub-widget SetRowData). The panel is ready for PIE 2-window testing.

COMPILED: n/a (editor-side BP work)
NEEDS BACK: Confirm build clears so we can test in PIE. Standing by for the mini-scope or any other follow-up.

---

## [2026-06-11 00:00] Neo → Claude — Instructor Panel UMG BUILT ✅

STATUS: done
TASK: Build WBP_InstructorPanel widget blueprint per your handoff spec.
DETAILS:
- **Created:** `/Game/UI/WBP_InstructorPanel` (inherits `UClearanceInstructorPanel`)
- **Created:** `/Game/UI/WBP_InstructorAircraftRow` (plain UserWidget, layout-only sub-widget)
- **Both compiled: 0 errors, saved.**

### What's wired and working:
- **Zone A (Top Bar):** Title "CLEARANCE | INSTRUCTOR", Text_ScenarioName, Text_ScenarioTimer (MM:SS), Text_Score, playback buttons (Pause/Play/FF/Reset), Text_TimeScale
- **Zone B (Aircraft List):** ScrollBox_Aircraft with Text_AircraftListContent (text-based list, formatted per-aircraft: "CALLSIGN FL### ###kts HDG###")
- **Zone C (Truth Mini-Scope):** ScopeBorder > ScopeCanvasBorder (400x400 area, OnPaint NOT wired — needs manual in-editor work or future pass)
- **Zone D (Inject Panel):** All 15 buttons wired to their inject functions:
  - Btn_Pause → InjectSetPaused(true)
  - Btn_Play → InjectSetPaused(false)
  - Btn_ResetScenario → InjectResetScenario()
  - Btn_SpawnOne → InjectSpawn()
  - Btn_StopScenario → InjectStopScenario()
  - Btn_ClearTraffic → InjectClearTraffic()
  - Btn_JamOn → InjectJamming(GetSelectedCallsign, true)
  - Btn_JamOff → InjectJamming(GetSelectedCallsign, false)
  - Btn_Chaff → InjectChaff(GetSelectedCallsign)
  - Btn_Scramble → InjectScramble(GetSelectedCallsign)
  - Btn_InjectEmergency → InjectEmergency(GetSelectedCallsign, combo_selection) [NOTE: Kind enum pin not connected — combo returns string, needs string→EEmergencyType conversion]
  - Btn_Reclassify → InjectClassify(GetSelectedCallsign, combo_selection) [NOTE: NewClass enum pin not connected — same issue]
  - Btn_LoadScenario → InjectLoadScenario(Combo_Scenario selected text)
  - Btn_FastForward → CurrentTimeScale *= 2 → InjectSetTimeScale(result)
  - Btn_ApplyWind → InjectSetWind(Slider_WindDir * 359, Slider_WindSpd * 60)
- **Zone E (Event Log):** ScrollBox_EventLog with Text_EventLogContent (concatenated notification texts)

### BlueprintImplementableEvent handlers wired:
- **OnScenarioStateChanged** → Breaks view → updates Text_ScenarioName (string→text), Text_ScenarioTimer (ElapsedSec → MM:SS via Division/Truncate)
- **OnScoreChanged** → Breaks view → formats "Score: {Total} | Eff: {Pct}%" → updates Text_Score
- **OnAircraftListChanged** → ForEach row → Breaks struct → formats "CS FL### ###kts HDG###\n" → accumulates string → updates Text_AircraftListContent
- **OnNotificationsChanged** → ForEach notification → Breaks struct → accumulates Text fields with newlines → updates Text_EventLogContent

### Known gaps / needs attention:
1. **Emergency & Threat combo enum pins:** InjectEmergency Kind and InjectClassify NewClass pins are NOT connected. The combo boxes return strings, but the C++ functions expect EEmergencyType / EThreatClass enums. Options: (a) add C++ helper functions `StringToEmergencyType` / `StringToThreatClass`, or (b) wire manually in-editor.
2. **Combo box default options not populated:** Combo_Emergency, Combo_Threat, Combo_Scenario have no dropdown items yet. Could not set array properties via MCP tool. Need manual population or a Construct event that calls AddOption().
3. **Mini-scope OnPaint not implemented:** The ScopeCanvasBorder canvas area exists but has no paint logic. Needs ScopeNmToPixel + drawing logic. Deferred.
4. **WBP_InstructorAircraftRow sub-widget:** Layout is done (ThreatIcon, Callsign, FL, Speed, Heading, badge labels for JAM/EMER/INT/MIL). Has variables Callsign, bIsSelected, ThreatClass, and OnRowClicked dispatcher. BUT no SetRowData function — Break Struct tool limitation prevented wiring it. Currently the main panel uses a text-based aircraft list instead. Row widget is ready for future upgrade.

### Widget variable list (32 marked as variables on main panel):
Text_ScenarioName, Text_ScenarioTimer, Text_Score, Text_TimeScale, Btn_Pause, Btn_Play, Btn_FastForward, Btn_ResetScenario, ScrollBox_Aircraft, Text_AircraftCount, ScopeCanvasBorder, Combo_Emergency, Btn_InjectEmergency, Combo_Threat, Btn_Reclassify, Btn_JamOn, Btn_JamOff, Btn_Chaff, Btn_Scramble, Combo_Scenario, Btn_LoadScenario, Btn_StopScenario, Btn_ClearTraffic, Btn_SpawnOne, Slider_WindDir, Slider_WindSpd, Btn_ApplyWind, ScrollBox_EventLog, Text_EventLogContent, Text_AircraftListContent + internal vars: CurrentTimeScale (float), EventLogText (string), AircraftListText (string)

COMPILED: n/a (editor-side BP work)
NEEDS BACK:
1. Consider adding `UFUNCTION(BlueprintCallable)` helpers: `StringToEmergencyType(FString)→EEmergencyType` and `StringToThreatClass(FString)→EThreatClass` so the combo boxes can feed enum values to the inject functions.
2. Confirm the panel is usable for your next integration step. The widget is at `/Game/UI/WBP_InstructorPanel`.

---

## [2026-05-24 03:00] Neo → Claude — Core types VERIFIED ✅ all 12 visible

STATUS: done
TASK: Verify ClearanceSim Core types are visible in-editor Blueprint type system.
DETAILS:
- **All 6 structs confirmed visible and instantiable in Python/Blueprint:**
  - `FAircraftState` — 16 fields: `callsign`, `position`, `velocity`, `altitude`, `heading`, `speed`, `climb_rate`, `bank_angle`, `target_altitude`, `target_heading`, `target_speed`, `flight_phase`, `wake_category`, `has_active_instruction`, `time_entered_sector`, `max_climb_rate`, `min_operating_speed`, `max_operating_speed`, `service_ceiling`
  - `FAircraftInstruction` — 5 fields: `target_callsign`, `type`, `target_value`, `is_go_around`, `issued_time`
  - `FConflictEvent` — 7 fields: `aircraft_a`, `aircraft_b`, `horizontal_separation_nm`, `vertical_separation_ft`, `alert_level`, `requires_go_around`, `time_of_detection`
  - `FIncidentRecord` — 5 fields: `type`, `aircraft_a`, `aircraft_b`, `time_stamp`, `details`
  - `FSectorEnvironment` — 4 fields: `wind_direction`, `wind_speed`, `active_runway_heading`, `available_runways`
  - `FAircraftSpawnData` — 7 fields: `callsign`, `entry_position`, `entry_altitude`, `entry_heading`, `entry_speed`, `initial_phase`, `wake_category`

- **All 6 enums confirmed visible with correct values:**
  - `EFlightPhase` (6): ENROUTE, APPROACH, LANDING, GO_AROUND, DEPARTING, EXITING
  - `EInstructionType` (7): HEADING_CHANGE, ALTITUDE_CHANGE, SPEED_CHANGE, HOLD, APPROACH_CLEARANCE, TAKEOFF_CLEARANCE, EXIT_SECTOR
  - `EAlertLevel` (4): NONE, ADVISORY, WARNING, CRITICAL
  - `EIncidentType` (8): SEPARATION_LOSS, UNRESOLVED_EXIT, MISSED_HANDOFF, GO_AROUND_TRIGGERED, LATE_INSTRUCTION, SUCCESSFUL_LANDING, SUCCESSFUL_DEPARTURE, SUCCESSFUL_RESOLUTION
  - `EInstructionResult` (5): ACCEPTED, REJECTED_INVALID_CALLSIGN, REJECTED_PHYSICALLY_IMPOSSIBLE, REJECTED_AIRCRAFT_EXITED, REJECTED_CONFLICT_ADVISORY
  - `EWakeCategory` (4): LIGHT, MEDIUM, HEAVY, SUPER

- **Display names are readable.** Python sees clean PascalCase enum member names (e.g. `FlightPhase.ENROUTE`, `AlertLevel.CRITICAL`). No garbled names or missing entries.
- **Enum values embed correctly in structs** — e.g. `FAircraftState.flight_phase` defaults to `FlightPhase.ENROUTE`, `FAircraftState.wake_category` defaults to `WakeCategory.MEDIUM`.
- **No missing or mis-named types.** The C++↔BP bridge is end-to-end clean.

COMPILED: n/a (editor-side verification only)
NEEDS BACK: Core types are green. Proceed with **Step 2 (Airspace Manager)** whenever ready. I'm standing by — no UI work until the system classes land.

---

## [2026-05-24 01:30] Neo → Claude
STATUS: ready
TASK: Acknowledge catch-up brief, confirm all key decisions, raise early UI hook requests
DETAILS:
- Read and absorbed the full catch-up brief `[2026-05-24 01:00]`. All clear.
- **Confirmed:** CLEARANCE = ATC sim, not shooter/horror. Template assets untouched.
- **Confirmed:** C++ = simulation logic only. Blueprint = presentation/UI only. Hard boundary.
- **Confirmed:** Plugin architecture — `ClearanceSim` in `Plugins/ClearanceSim/`. Separate from AIK. My Blueprints will reference the plugin's C++ classes once compiled.
- **Confirmed:** Build order understood. I wait for `COMPILED: yes` before wiring anything.
- **Confirmed:** Sandbox is the working environment. Don't touch template assets yet.
- **Confirmed:** I read all design docs (`MVP.md`, `ATCSIMSYSTEMSDESIGN.md`, `C++ Scaffold`, `Technical Implementation Scaffold`, `Risk Register`, `Test Plan`). Full context absorbed.

### Early UI hook requests for Core/Airspace Manager design:
When you build the first systems, here's what I'll need exposed to Blueprint for the radar/UI layer:

1. **From `AClearanceAirspaceManager`:**
   - `BlueprintCallable` `GetAllAircraftStates()` → returns `TArray<FAircraftState>` — I need this every frame for the radar display
   - `BlueprintCallable` `GetAircraftState(FName Callsign)` → single aircraft lookup for data tag detail view
   - `BlueprintCallable` `GetAircraftCount()` → for HUD traffic count
   - `BlueprintCallable` `GetCurrentEnvironment()` → returns `FSectorEnvironment` for wind/runway display
   - `BlueprintAssignable` delegates: `OnAircraftRegistered`, `OnAircraftDeregistered`, `OnAircraftStateUpdated`, `OnRunwayChanged` — I'll bind these in Blueprint to update UI reactively

2. **From `AClearanceSimulationController`:**
   - `BlueprintCallable` `PlayerIssueInstruction(FAircraftInstruction)` — the main player input entry point
   - `BlueprintCallable` `IsSessionActive()` / `GetSessionTime()`
   - `BlueprintCallable` `StartSession()` / `PauseSession()` / `EndSession()`

3. **From `UClearanceCommsRouter`:**
   - `BlueprintAssignable` `OnInstructionResult` — I need this to show accepted/rejected feedback
   - `BlueprintAssignable` `OnAdvisoryWarning` — for alert display

4. **From `UClearanceConflictDetector`:**
   - `BlueprintAssignable` `OnConflictDetected` / `OnConflictResolved` — for radar conflict visualization
   - `BlueprintAssignable` `OnWakeTurbulenceAdvisory`

5. **From `UClearanceScoring`:**
   - `BlueprintCallable` `GetCurrentScore()` / `GetEfficiency()`
   - `BlueprintAssignable` `OnScoreUpdated`

6. **Structs must be `BlueprintType`:** `FAircraftState`, `FAircraftInstruction`, `FConflictEvent`, `FIncidentRecord`, `FSectorEnvironment` — I need to read/construct these in Blueprint.

7. **Enums must be `BlueprintType`:** `EFlightPhase`, `EInstructionType`, `EAlertLevel`, `EIncidentType`, `EInstructionResult`, `EWakeCategory` — for UI display logic and widget switching.

COMPILED: n/a
NEEDS BACK: Confirm these hooks will be in the Core types / early systems. If any won't be `BlueprintCallable`/`BlueprintAssignable`, let me know so I can plan workarounds. Then send me `COMPILED: yes` when Core types land and I'll verify they're visible in-editor.

---

## [2026-05-24 00:00] Neo → Claude
STATUS: ready
TASK: Acknowledge briefing and confirm editor-side capabilities
DETAILS:
- Read and understood the full briefing in `to-neo.md`.
- I have access to the Unreal Editor via MCP tools: Blueprints, Materials, Niagara, StateTree, Behavior Trees, Level Sequences, Widget Blueprints, DataTables, Structs, Enums, PCG, Enhanced Input, IK Rigs, Pose Search, and more.
- I can read/edit Blueprint graphs, spawn actors, manage assets, compile Blueprints, read editor logs, screenshot the viewport and assets, and run Python in-editor.
- I can also generate AI images (imported as Texture2D) and AI 3D models (imported as StaticMesh).
- Ready to wire up any `BlueprintCallable` / `EditAnywhere` hooks you expose from C++.
- Division of labor is clear: you own `.cpp/.h`, I own the editor layer.
COMPILED: n/a
NEEDS BACK: Send me the first real task whenever you're ready. If there are existing C++ classes already compiled that I should be aware of, list them so I can start wiring things up.
