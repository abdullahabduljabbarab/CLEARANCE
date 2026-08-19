# Replay, checkpoints, and after-action reporting

CLEARANCE is built to be used as a trainer, not only to be flown.
That means every session records everything the trainee did, an
instructor can rewind and replay, a scenario can be paused at a
critical instant and re-attempted, and the session ends with a
written report.

This document covers the four surfaces that together make CLEARANCE
usable for training rather than only for demoing:

1. Session recording.
2. Replay and scrub.
3. Checkpoints.
4. After Action Report export.

## Contents

- [What gets recorded](#what-gets-recorded)
- [Recorder architecture](#recorder-architecture)
- [Replay and the scrub bar](#replay-and-the-scrub-bar)
- [Checkpoints](#checkpoints)
- [After Action Report](#after-action-report)
- [Automation coverage](#automation-coverage)

## What gets recorded

`UClearanceSessionRecorder` captures a continuous stream of typed
records during a session:

- **Snapshots.** Full aircraft state for every registered aircraft at
  the recorder's snapshot rate (default 10 Hz). Position, velocity,
  heading, altitude, speed, flight phase, squawk, active clearance,
  affiliation, radar track confidence, ownership site ID.
- **Discrete events.** Instructions issued (accepted and rejected),
  conflict alerts and their level transitions, TCAS Resolution
  Advisories, wake advisories, emergencies declared, threat
  reclassifications, checkpoints saved, replay entered and exited,
  weapons fired and detonated, radio transmissions on managed
  frequencies.
- **Sim environment.** Wind, active runway, sector configuration.
- **Voice transcript entries.** Timestamped rows from
  `EClearanceCommsRole` (Operator, Pilot, System, Instructor, Tower,
  ACC, AWACS, GCI, ATIS, MET).
- **Score events.** Every scoring incident with its time, category,
  severity, and context.

Everything above is stored in an in-memory ring by default; a session
that ends triggers a serialisation pass into a session file for
subsequent replay and AAR generation.

## Recorder architecture

The recorder runs server-authoritative. All state written into it
comes from the authoritative tick pipeline, so a listen-server or
dedicated-server session and a Play-Standalone session produce
identical recordings for identical inputs.

- Snapshots are keyed by seconds-from-session-start. `FindSnapshotAt`
  returns the snapshot at or immediately before a given time.
- Discrete events keep their own ring, filtered by time range for
  replay window queries via `GetEventsBetween`.
- Storage is bounded by wallclock rather than record count; a long
  hold at pause does not evict aircraft snapshots that would be
  needed for a replay.

Lifecycle:

- `Start()` clears prior data and records the session start time.
- `Stop()` freezes the ring at the current tail.
- `Clear()` drops recorded data without touching lifecycle flags
  (used by unit tests).
- `GetDuration()` returns the total recorded seconds.

## Replay and the scrub bar

`SeekReplay(TimeSeconds)` on the simulation controller poses the
world back to the target time. The replay path:

1. Locate the snapshot at or immediately before the target time.
2. Restore every recorded aircraft to that snapshot's state.
3. Restore sim environment (wind, active runway).
4. Roll forward from the snapshot to the exact target time using the
   deterministic per-tick interpolation that would have run live at
   that instant.
5. Replay the discrete events that occurred between the snapshot and
   the target time so that transcript rows, scoring, and alerts
   render at the correct spot.

The instructor panel exposes:

- **Play / pause** and **speed control** (0.25× through 4×).
- **Scrub bar** normalised over `GetReplayDuration()`. During drag,
  a `bScrubbing` flag gates the tick-side value updates so the slider
  does not fight the pointer.
- **Go live** button that leaves replay and returns to authoritative
  live state.
- **Seam tick marks** on the scrub bar at every scenario inject and
  every scoring incident, so an instructor can jump directly to
  significant moments during review.

Time text reads from `GetReplayDuration()` so the label agrees with
the slider under all recording lengths.

## Checkpoints

Checkpoints are a separate workflow from full session replay. A
checkpoint captures a single snapshot of the entire simulation state
under a named tag. It is intended for scenario rehearsal: an
instructor saves state immediately before a critical event, allows a
trainee to attempt it, then restores the same state for another
attempt without wiping the full session recording.

- `Server_InjectSaveCheckpoint(FName Tag)` captures a snapshot and
  attaches the tag.
- `Server_InjectLoadCheckpoint(FName Tag)` restores the snapshot.
- Multiple checkpoints per session are supported and appear on the
  instructor timeline alongside inject markers.

Checkpoints share the same snapshot format as the recorder but do not
depend on the recorder being running.

## After Action Report

At session end, the instructor can export the session as a Markdown
After Action Report via `Server_InjectExportAAR`. Output lands under
`<ProjectSavedDir>/Reports/Session_<timestamp>.md`.

Report structure:

- **Session summary.** Duration, scenario, active runway, wind,
  operator identity, final score.
- **Performance totals.** Successful clearances, exits credited,
  incident counts by severity, TCAS resolution rate.
- **Incident chronology.** Every scoring event in order with a short
  human-readable line and the timestamp.
- **Critical events with communications context.** For each incident
  above a severity threshold, the surrounding communications rows are
  included so the reader can see what the operator was doing and what
  the aircraft was saying at the moment the incident triggered.
- **Score breakdown.** Points earned and lost by category.
- **Full communications transcript.** Every row from the session,
  role-coloured in the source Markdown so a reader can also spot
  patterns across facilities.

Markdown is the deliberate choice. It renders on any GitHub, GitLab,
or file viewer without a proprietary reader, it round-trips through
copy-paste into an email or a lesson plan, and it survives sitting
in a shared training folder for years without a format migration.

## Automation coverage

The recorder is one of the more thoroughly tested subsystems because
the replay and AAR paths depend on it silently doing the right thing.
`ClearanceSessionRecorderTests.cpp` covers:

- Start / Stop lifecycle transitions.
- Duration semantics under paused, running, and stopped states.
- `FindSnapshotAt` with the seconds-from-start convention including
  the "target before first snapshot" and "target after last snapshot"
  edges.
- `GetEventsBetween` with inclusive-start / exclusive-end range.
- `Clear` as a data-only operation that does not toggle the running
  flag.

The tests map to REQ-SIM-001 through REQ-SIM-008 in
[Requirements](../Verification/Requirements.md).
