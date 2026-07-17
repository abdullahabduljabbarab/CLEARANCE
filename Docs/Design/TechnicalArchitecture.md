# Technical Architecture

**Project:** CLEARANCE
**Author:** Abdullah Ameed Abduljabbar
**Role:** Systems Designer

## Table of contents

- [Purpose](#purpose)
- [How to read this document](#how-to-read-this-document)
- [Architectural principles](#architectural-principles)
- [Class ownership model](#class-ownership-model)
- [Tick architecture](#tick-architecture)
- [Data flow](#data-flow)
- [C++ and Blueprint boundary](#c-and-blueprint-boundary)
- [Delegate map](#delegate-map)
- [Memory and lifecycle](#memory-and-lifecycle)
- [Source layout](#source-layout)
- [Model-based subsystems](#model-based-subsystems)
- [Networking and replication](#networking-and-replication)
- [Definition of readiness](#definition-of-readiness)
- [References](#references)

## Purpose

Architectural companion to the Systems Design and C++ Scaffold documents. Where the Systems Design describes what each system does and the C++ Scaffold documents the class-level API, this document describes how the sim is put together at runtime: who owns what, what ticks in what order, how data flows between systems, where the C++ and Blueprint boundaries sit, how objects are created and torn down, and how the network layer plugs in.

CLEARANCE is a portfolio demonstrator and training-simulation prototype. This document describes the runtime architecture that carries the sim, not the certification artefacts an operational programme would require.

## How to read this document

This is an architecture note, not a build guide. It is aimed at another engineer stepping into the codebase for the first time and wanting to understand the wiring in one pass. Class ownership, tick order, data direction, delegate map, lifecycle, and networking are the sections that matter most. Everything else is context.

For system behaviour see the Systems Design document. For enums, structs, and per-class API surfaces see the C++ Scaffold.

## Architectural principles

Six principles shape the runtime. They are enforced by class boundaries, not by convention.

1. **Single source of truth.** All aircraft state and all sector environmental state lives on `AClearanceAirspaceManager`. No other class holds a private copy.
2. **Single movement executor.** Every aircraft has one `UClearanceAircraftBehaviour` object, and that object is the only thing in the sim permitted to mutate position, altitude, speed, or heading.
3. **Read-only safety analysis.** `UClearanceConflictDetector` reads snapshots and broadcasts events. It does not mutate airspace state.
4. **Validate before execute.** Every instruction runs through `UClearanceInstructionValidator` before it reaches the behaviour object. Rejected instructions never touch the sim.
5. **Controller-driven tick.** `AClearanceSimulationController` drives the tick sequence every frame. No other class has independent tick authority for simulation logic.
6. **Strict presentation boundary.** Simulation logic lives in C++. Presentation is exposed through Blueprint/UMG and C++ widget paint hooks. Blueprint reads state through `BlueprintCallable` getters and sends operator intent through C++ RPCs; it does not own or mutate simulation state.

These principles are why the runtime architecture that follows looks the way it does. If any of them are relaxed later, the ownership model, tick pipeline, and delegate map will all need to be rechecked.

## Class ownership model

This section defines which classes own state and which classes read it.

### AClearanceAirspaceManager (AActor)

Owns all aircraft state and sector environmental state.

- **Owns:** `TMap<FName, FAircraftState> AircraftStates` (private), `FSectorEnvironment SectorEnvironment`, `TArray<FRunwayInfo> Runways`, `TArray<FChaffCloud> ChaffClouds`, replicated mirror `TArray<FAircraftState> ReplicatedAircraft`.
- **Reads:** incoming registration requests, state update requests from behaviour objects, wind updates from the instructor panel.
- **Broadcasts:** `OnAircraftRegistered`, `OnAircraftDeregistered`, `OnAircraftStateUpdated`, `OnRunwayChanged`.

Implemented as an `AActor` because it needs world presence and Tick participation as the sector's persistent runtime authority. No other class is permitted to own an alternative aircraft state store.

### UClearanceAircraftBehaviour (UObject, per aircraft)

Sole movement executor for one aircraft.

- **Owns:** pending instruction queue, transient movement state (bank angle, climb rate targets), Simulink autopilot wrapper instance when engaged.
- **Reads:** current aircraft state from the Airspace Manager at the start of its step; sector environment for wind and active runway.
- **Writes:** updated `FAircraftState` back to the Airspace Manager via `RequestStateUpdate` at the end of its step.

Implemented as a `UObject` rather than an actor because it does not need world presence, transform, or collision. It only needs a tick call from the controller. Held by the controller in a `TMap<FName, UClearanceAircraftBehaviour*>` keyed on callsign.

### UClearanceConflictDetector (UObject)

Pure conflict analysis logic. Also handles wake and TCAS-style RA broadcasts.

- **Owns:** `ActiveConflicts` map, `ActiveWakeAdvisories` set, `ActiveTCAS` set, per-aircraft wake intensity map. All internal.
- **Reads:** read-only snapshots from the Airspace Manager each detection cycle.
- **Broadcasts:** `OnConflictDetected`, `OnConflictResolved`, `OnGoAroundRequired`, `OnWakeTurbulenceAdvisory`, `OnTCASResolutionAdvisory`.

Pure logic; no world presence, no Tick of its own. Called by the controller during the tick sequence.

### UClearanceInstructionValidator (UObject)

Stateless validation logic.

- **Owns:** no persistent state.
- **Reads:** current aircraft state, proposed instruction, ICAO constants.
- **Returns:** `EInstructionResult` (accepted or rejection reason).

Reused across every instruction that enters the sim. Because it is stateless, it is safe to call repeatedly from the controller's simulation flow. If moved off-thread later, callers must pass immutable snapshots and avoid direct `UObject` or world access.

### UClearanceCommsRouter (UObject)

Instruction plumbing between the parser, validator, and behaviour objects.

- **Owns:** references to Airspace Manager, Validator, and the controller's behaviour map. Per-callsign last-instruction timestamps.
- **Reads:** instructions from the phraseology parser and from scripted scenario voice injects.
- **Broadcasts:** `OnInstructionResult`, `OnAdvisoryWarning`.

### UClearanceScoring (UObject)

Session-level scoring and assessment.

- **Owns:** incident log, running score, per-category counters, spawn interval, difficulty state.
- **Reads:** conflict, phase, instruction, and emergency events fired earlier in the tick.
- **Broadcasts:** `OnScoreUpdated`, `OnDifficultyAdjusted`.

### AClearanceAircraftSpawner (AActor)

Free-play traffic generator.

- **Owns:** callsign counter, spawn timer, spawn configuration.
- **Reads:** current spawn interval from Scoring, suspension flag from the controller when a scenario is running.
- **Writes:** new `FAircraftSpawnData` payloads into `AClearanceAirspaceManager::RegisterAircraft`.

Implemented as an `AActor` because it needs world and sector context for entry-point sampling. Spawn cadence is controlled through `TickSpawning`, called by the Simulation Controller during the authoritative tick pipeline, rather than through independent simulation authority.

### AClearanceSimulationController (AActor)

Simulation orchestration.

- **Owns:** references to every other system, the behaviour map, session state (`bSessionActive`, `SessionTime`, `SimulationTimeScale`, replay state), replicated arrays (`ReplicatedAircraft`, `RepScoringLog`, `RepOperatorTracks`, `RepNotifications`, `RepCheckpoints`, `Transcript`).
- **Reads:** every subsystem's outputs, delegate broadcasts, RPCs from the operator player controller.
- **Drives:** the 9-step tick pipeline, delegate binding at session start, `UObject` lifecycles for behaviour objects, instruction dispatch, TCAS RA execution, replay pose-back, checkpoint save and load, AAR export.

Implemented as an `AActor` because it orchestrates everything through Tick. It does not replace any other class's ownership boundary; it controls runtime sequencing, subsystem creation, delegate binding, and the lifecycle of the `UObject`-based systems above.

### Newer systems

Systems added during production sit alongside the original seven:

- `UClearanceScenarioRunner` (UObject, owned by controller): evaluates JSON scenarios, injects actions via the same RPCs the instructor uses.
- `AClearanceRadarSite` (AActor) + `UClearanceRadar` (UObject): each site owns a `FRadarWrapper` when the Simulink DSP path is engaged; the controller iterates every enabled site each tick.
- `UClearanceSessionRecorder` (UObject, owned by controller): captures per-tick snapshots and event log.
- `UClearanceInstructorPanel` (UUserWidget, presentation-side only): reads via `BlueprintCallable` getters, sends operator intent through `Server_Inject*` RPCs on `AClearanceOperatorPC`.

## Tick architecture

This section defines what ticks and in what order.

### Actor tick participants

Direct actor tick participants are:

- `AClearanceSimulationController`
- `AClearanceAirspaceManager`
- `AClearanceAircraftSpawner`
- `AClearanceRadarSite` for scan-interval scheduling only; radar painting itself is still called by the controller during the tick pipeline.

These are the only actor `Tick` participants in the simulation layer.

### Controller-called systems

Everything else is called by the Simulation Controller during its tick in dependency order:

- `UClearanceAircraftBehaviour` (per aircraft)
- `UClearanceConflictDetector`
- `UClearanceInstructionValidator` (on demand)
- `UClearanceCommsRouter` (on demand)
- `UClearanceScoring`
- `UClearanceScenarioRunner`
- `UClearanceSessionRecorder`

None of these tick independently.

### Authoritative tick pipeline

The tick sequence is fixed. The comment in the controller's source calls it "the authoritative tick order from the architecture doc":

1. **Spawner and scenario injection.** If no scenario is running, ask the spawner whether to add an aircraft this frame. If a scenario is running, evaluate the next timed action.
2. **Behaviour step.** For each aircraft, the behaviour object reads current state, applies pending instructions, integrates one frame under performance-constrained flight dynamics (or the Simulink autopilot when engaged), and commits back through `RequestStateUpdate`.
3. **Conflict detection.** Full snapshot analysis of horizontal, vertical, trajectory, and wake separation. Fires alerts and RAs.
4. **Alert stamping.** Each aircraft's `CurrentAlertLevel` is written back to the Airspace Manager so replication carries it to clients.
5. **Scoring and event consumption.** Scoring subscribes to conflict, phase, and instruction events fired earlier in the tick. Incidents logged, score recalculated, difficulty threshold checked.
6. **GCI intercept and bandit EW reactions.** Viper flights track assigned targets. Hostile aircraft under intercept respond with chaff or jammer activation.
7. **Radar paint and fusion.** Every enabled radar site paints tracks against the fresh airspace state. The controller fuses across sites into `RepOperatorTracks`.
8. **Exit and crash checks.** Aircraft crossing the boundary are handed off, exited, or scored as strayed. Aircraft in `Crashed` phase fall to the ground with the timed collapse.
9. **Replication and visual sync.** Replicated arrays refresh. Instructor panel and operator HUD read the fresh state and repaint.

The order is fixed and matters. Behaviour must run before conflict, because conflict analyses the committed state. Conflict must run before scoring, because scoring logs conflict events. Radar sites run after airspace commits, because radar reads the fresh aircraft positions. Replication runs last so every client reads a consistent snapshot. Slipping any step means downstream systems read stale data.

## Data flow

This section defines which direction data moves between systems.

### Airspace Manager outward flow

The Airspace Manager provides read-only snapshots outward:

- To the Communication System for instruction validation.
- To the Conflict Detection System for safety analysis.
- To the Scoring System for aircraft and event context.
- To the Scenario Runner for condition evaluation.
- To the Radar System for target lists.
- To the presentation layer (radar display, instructor panel) through `BlueprintCallable` accessors.
- To the Federation layer for outbound wire packets.

### Aircraft Behaviour inward flow

Aircraft Behaviour pushes state updates inward to the Airspace Manager. It receives instructions from the Comms Router, executes gradual motion, and submits the new `FAircraftState` back to the central authority. This is the only sanctioned write path for aircraft state.

### Communication flow

The Communication System:

- reads current aircraft state from the Airspace Manager;
- validates instructions against that state via the Instruction Validator;
- routes accepted instructions to the target's behaviour object.

It never mutates aircraft state directly.

### Conflict Detection flow

Conflict Detection:

- reads the full aircraft snapshot from the Airspace Manager each cycle;
- evaluates horizontal separation, projected conflicts, vertical separation, and wake separation;
- broadcasts alerts and TCAS RAs through delegates.

It does not write back into aircraft state.

### Scoring flow

Scoring receives events from:

- Conflict Detection (`OnConflictDetected`, `OnWakeTurbulenceAdvisory`, `OnTCASResolutionAdvisory`);
- Airspace-related outcomes (aircraft registered, deregistered, phase transitions on landing or handoff or exit);
- Communication (`OnInstructionResult`);
- Emergency handling (mayday timeouts, crash events).

It logs and evaluates the session and does not directly change aircraft state.

### Radar and sensor flow

Radar sites read the fresh airspace snapshot, produce `FRadarTrack` maps, and never write back. The controller fuses across sites into `RepOperatorTracks`.

### Federation flow

The federation emitters subscribe to `OnAircraftStateUpdated` (and similar events) and publish outward as DIS PDUs, DDS samples, RTI samples, or HLA updates. Incoming packets either register new external aircraft through the normal registration path or update existing external aircraft through the controlled state-update path, marked `bIsExternal = true` so ownership is preserved.

### No-bypass rule

No system bypasses the defined flow:

- No UI may mutate simulation state directly.
- No Conflict Detection logic may reposition aircraft.
- No scoring logic may alter aircraft state.
- No behaviour object may become a replacement source of truth.
- No dependent system may store its own authoritative copy of aircraft state.

## C++ and Blueprint boundary

The boundary is deliberate and enforced by which classes hold which responsibilities.

### C++ simulation layer

All simulation logic stays in C++:

- Airspace state ownership.
- Aircraft movement execution.
- Instruction validation.
- Instruction routing.
- Conflict detection.
- Scoring.
- Spawning.
- Scenario execution.
- Sensor layer (radar sites, radar equation, Simulink DSP wrapper).
- Simulation orchestration.
- Session recorder, checkpoint, AAR generation.
- Federation encode and decode.

### Blueprint presentation layer

Blueprint handles operator-facing UI:

- Radar display and scope perimeter.
- Aircraft data blocks.
- Alert glyphs.
- Instructor panel widgets.
- Emergency panel.
- Transcript display.
- Camera view widgets.
- Menu and HUD chrome.

### C++ widget paint hooks

A small number of paint-side operations live in C++ because they need direct Slate access, not because they own simulation logic:

- Scope paint (`NativePaint` on the instructor panel), which iterates data from `BlueprintCallable` getters and draws lines and text.
- Camera overlay paint (same pattern).
- Radar coverage heatmap (Slate custom vertex API).

These read from C++ getters; they do not write to sim state.

### Clean boundary rule

The boundary is one-directional:

- Blueprint reads sim state through `BlueprintCallable` getters.
- Blueprint sends operator intent through C++ RPCs (`Server_Inject*` on `AClearanceOperatorPC`).
- Blueprint never writes to sim state directly.

If the entire UMG layer were deleted, the simulation would still run correctly with no display.

## Delegate map

Cross-system events communicate through delegates. State reads, controlled mutations, and controller-driven sequencing use explicit function calls. The Simulation Controller binds every delegate listener at session start.

### Airspace Manager delegates

| Delegate | Listened to by | Purpose |
|---|---|---|
| `OnAircraftRegistered` | Simulation Controller | Create the per-aircraft behaviour object |
| `OnAircraftDeregistered` | Simulation Controller | Destroy the per-aircraft behaviour object |
| `OnAircraftStateUpdated` | Federation emitters, UI listeners | Surface committed aircraft state changes |
| `OnRunwayChanged` | Spawner, camera overlay, approach picker | Notify systems that active runway swapped |

### Conflict Detector delegates

| Delegate | Listened to by | Purpose |
|---|---|---|
| `OnConflictDetected` | Comms Router, Scoring | Trigger advisories and log conflict events |
| `OnConflictResolved` | Scoring, optional UI | Log successful conflict resolution |
| `OnGoAroundRequired` | Comms Router | Route go-around through the command path |
| `OnWakeTurbulenceAdvisory` | Comms Router, Scoring | Trigger wake separation advisory and log event |
| `OnTCASResolutionAdvisory` | Simulation Controller | Execute the coordinated vertical split |

### Communication delegates

| Delegate | Listened to by | Purpose |
|---|---|---|
| `OnInstructionResult` | Simulation Controller (transcript), UI | Feedback for accepted or rejected instructions |
| `OnAdvisoryWarning` | UI | Surface operator-facing advisory text |

### Scoring delegates

| Delegate | Listened to by | Purpose |
|---|---|---|
| `OnScoreUpdated` | UI | Update the score display |
| `OnDifficultyAdjusted` | Spawner | Adjust spawn pacing |

## Memory and lifecycle

This section defines how objects are created and destroyed.

### Actors

Actors are spawned by the world:

- `AClearanceSimulationController`
- `AClearanceAirspaceManager`
- `AClearanceAircraftSpawner`
- `AClearanceRadarSite` (one per placed antenna)
- `AClearanceOperatorPC` (per player)

These persist at world and session scope.

### UObjects

`UObject`-based systems are created with `NewObject` and owned by the Simulation Controller:

- `UClearanceInstructionValidator`
- `UClearanceCommsRouter`
- `UClearanceConflictDetector`
- `UClearanceScoring`
- `UClearanceScenarioRunner`
- `UClearanceSessionRecorder`

Per-aircraft behaviour objects (`UClearanceAircraftBehaviour`) are created dynamically on registration.

### Aircraft Behaviour lifecycle

1. Airspace Manager registers an aircraft and broadcasts `OnAircraftRegistered`.
2. Simulation Controller receives the event.
3. Controller creates a `UClearanceAircraftBehaviour` with `NewObject`.
4. Controller stores it in `TMap<FName, UClearanceAircraftBehaviour*> BehaviourMap`.
5. Behaviour object's `Initialise` reads current state, stamps performance envelope, and sets targets to current values so the aircraft holds until instructed.
6. Aircraft ticks through the controller until it deregisters (exit, landing, or crash).
7. Airspace Manager broadcasts `OnAircraftDeregistered`.
8. Controller removes the behaviour object from the map.
9. Once no longer referenced, the `UObject` is cleaned up by Unreal's garbage collector.

The behaviour map on the controller manages this lifecycle.

### Session lifecycle

At session start:

- World actors already exist (spawned by the game mode).
- Controller-owned `UObject` systems are created.
- Delegates are bound in `BindDelegates`.
- Session state is initialised.

At session end or reset:

- Simulation activity halts.
- Airspace Manager clears state.
- Controller clears `BehaviourMap`.
- Conflict Detector clears active conflict tracking.
- Scoring resets session data.
- Spawner resets timers and difficulty.
- Environmental state resets to configured defaults.

### Checkpoint lifecycle

Checkpoints do not tear down the session; they overwrite state within it:

1. `SaveCheckpoint(name)` captures a snapshot: every aircraft state, wind, active runway, scoring log.
2. Named snapshot appended to `Checkpoints` (server-side) and metadata replicated to clients as `RepCheckpoints`.
3. `LoadCheckpoint(name)` clears live traffic via `ClearAllAircraft` and re-registers from the snapshot.
4. Behaviour objects for the restored aircraft are created via the normal registration path.
5. Transcript and recorder are NOT touched, so the full attempt history stays in the AAR.

### Replay lifecycle

Replay is a temporary pose-back over the same session:

1. `EnterReplay` pauses the tick pipeline.
2. Session Recorder returns the snapshot at the current scrub position.
3. Controller poses the world to the snapshot (aircraft states, wind, etc).
4. `ResumeLive` returns the recorder to live capture without clearing the buffer, so the buffer accumulates across replay cycles.

## Source layout

```
Plugins/ClearanceSim/Source/ClearanceSim/
├── Public/
│   ├── Core/
│   │   ├── CLEARANCETypes.h        (enums, structs, delegates)
│   │   └── ClearanceConstants.h    (thresholds, per-category performance)
│   ├── Airspace/
│   │   ├── ClearanceAirspaceManager.h
│   │   ├── ClearanceRunway.h
│   │   └── ClearanceWaypoint.h
│   ├── Aircraft/
│   │   ├── ClearanceAircraftBehaviour.h
│   │   └── ClearanceAircraftSpawner.h
│   ├── Comms/
│   │   ├── ClearanceCommsRouter.h
│   │   ├── ClearanceInstructionValidator.h
│   │   ├── ClearancePhraseology.h
│   │   └── ClearanceVoiceOutput.h
│   ├── Safety/
│   │   ├── ClearanceConflictDetector.h
│   │   ├── ClearanceRadar.h
│   │   ├── ClearanceRadarSite.h
│   │   └── ClearanceRadarEquation.h
│   ├── Scoring/
│   │   └── ClearanceScoring.h
│   ├── Scenario/
│   │   └── ClearanceScenarioRunner.h
│   ├── Simulation/
│   │   ├── ClearanceSimulationController.h
│   │   ├── ClearanceSessionRecorder.h
│   │   ├── ClearanceOperatorPC.h
│   │   ├── ClearanceDISEmitter.h
│   │   ├── ClearanceDDSEmitter.h
│   │   └── ClearanceDDSReceiver.h
│   └── UI/
│       ├── ClearanceInstructorPanel.h
│       └── ClearanceInstructorTypes.h
├── Private/
│   └── (matching .cpp files + Tests/)
└── Build.cs
```

Related plugin modules alongside `ClearanceSim`:

- `ClearanceAutopilotMBD`: wraps the generated C from the Simulink cascade PID autopilot.
- `ClearanceRadarMBD`: wraps the generated C from the Simulink radar signal processor.
- `ClearanceDDS`: Fast DDS runtime.
- `ClearanceRTI`: RTI Connext runtime.
- `ClearanceHLA`: OpenRTI-based HLA federate.

Each MBD plugin's `Build.cs` auto-detects the presence of the generated C in `ThirdParty/<Name>Generated/{include,src}` and flips a preprocessor gate that switches the wrapper between stub and real code.

## Model-based subsystems

Two Simulink models integrate into the runtime through separate plugin modules. Each follows the same integration pattern:

1. **Wrapper.** Unreal-side C++ struct owning one instance of the generated model's runtime state (`RT_MODEL_autopilot_T` or `RT_MODEL_radar_T`).
2. **Input struct.** Operational-level inputs (targets, aircraft state, sensor inputs).
3. **Step call.** One call per tick against the wrapper: `autopilot_step` or `radar_step`.
4. **Output struct.** Model output (control-surface commands or CFAR detections).
5. **Integration.** The consuming system converts model output into simulation state deltas (bank rate, climb rate, throttle, or track fill).

The wrapper structure means each aircraft owns its own autopilot state and each radar site owns its own DSP state, with no shared globals. Reusable-function packaging in the Simulink codegen enforces this at the model level.

## Networking and replication

CLEARANCE runs server-authoritative. The player is the operator and hosts the running sim; an instructor joins as a second peer.

### Server-authoritative state

Every mutation to simulation state runs on the server. Clients see replicated mirrors:

- `ReplicatedAircraft` (from `AClearanceAirspaceManager::AircraftStates`).
- `RepScoringLog` (from `UClearanceScoring::IncidentLog`).
- `RepOperatorTracks` (from the fused radar picture).
- `RepNotifications` (from the notification ring).
- `RepCheckpoints` (metadata only; payloads stay server-side).
- `Transcript` (from `UClearanceCommsRouter` and system voice injects).
- `SectorEnvironment`, `Runways`, `ChaffClouds`.

### Operator player controller

`AClearanceOperatorPC` carries the `Server_Inject*` RPCs. Every instructor action, every operator scenario command, every federation start-stop RPC lands here. The RPC lands on the server, mutates state, and replicates the result.

### Federation as delegate subscriber

The federation emitters subscribe to `OnAircraftStateUpdated` and related delegates. Every simulation-state change becomes a wire packet:

- DIS emitter serialises to IEEE 1278.1 PDUs and sends over UDP.
- Fast DDS emitter publishes on Fast DDS topics.
- RTI Connext emitter publishes on RTI DataWriters.
- HLA broker updates HLA object attributes through OpenRTI.

Incoming packets either register new external aircraft through the normal registration path or update existing external aircraft through the controlled state-update path, marked `bIsExternal = true` so ownership is preserved.

## Definition of readiness

The runtime architecture is considered stable when the following are settled:

- Class ownership boundaries.
- Actor Tick participation list.
- Controller-called dependency order.
- Data flow directions.
- C++ and Blueprint boundaries.
- Delegate map.
- Lifecycle rules for actors, UObjects, aircraft behaviours, sessions, checkpoints, and replays.
- Tuning constants and per-category performance envelopes.
- Wire formats (for federation).
- Source layout.

At the time of writing, all of the above are stable in the shipped codebase. This document tracks the current architecture; the Systems Design document describes the systems that sit on top of it.

## References

Gregory, J. (2014) *Game Engine Architecture*, 2nd ed. CRC Press.

IEEE (2012) *IEEE Standard for Distributed Interactive Simulation: Application Protocols* (IEEE Std 1278.1-2012). IEEE Standards Association.

IEEE (2010) *IEEE Standard for Modeling and Simulation (M&S) High Level Architecture (HLA), Framework and Rules* (IEEE Std 1516-2010). IEEE Standards Association.

Salen, K. and Zimmerman, E. (2003) *Rules of Play: Game Design Fundamentals*. MIT Press.

SISO (2015) *Standard for Real-time Platform Reference Federation Object Model (RPR-FOM) 2.0* (SISO-STD-001-2015). Simulation Interoperability Standards Organization.
