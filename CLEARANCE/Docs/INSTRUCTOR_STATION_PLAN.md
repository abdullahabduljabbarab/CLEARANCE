# Instructor Station - Networked Architecture Plan

## Goal

Two-machine (or two-PIE-instance) setup:
- **Trainee** — plays the sim normally; eventually VR with diegetic radar scope
- **Instructor** — separate process, watches the trainee's world, injects
  failures, freezes sim, scores live

## Topology

**Listen server**. Trainee instance hosts; instructor joins as client. Simpler
than dedicated server, no third process to deploy, fine for portfolio demo +
training delivery up to a small operator pool.

Alternative (later): dedicated server when scaling to 4+ stations.

## Replication scope

### Server-authoritative

The simulation only RUNS on the server. Every system that currently mutates
state stays server-side:

- `AClearanceSimulationController` (orchestrator)
- `AClearanceAirspaceManager` (state store)
- `UClearanceAircraftBehaviour` (per-aircraft autopilot)
- `UClearanceConflictDetector`, `UClearanceScoring`, `UClearanceScenarioRunner`
- `UClearanceRadar` instances (sensor sweep + tracks)
- `AClearanceAircraftSpawner`, `AClearanceDISEmitter`, `AClearanceDISReceiver`

### Replicated to clients

What clients need to display:

- **Aircraft state list** — `TArray<FAircraftState>` replicated from Manager
- **Radar tracks (fused picture)** — either replicate `TArray<FRadarTrack>`
  per radar OR compute locally on client from replicated truth (more
  bandwidth-efficient, but loses the sensor-fusion narrative). **Replicate
  the fused picture**; preserves the defence story.
- **Score events** — `TArray<FIncidentLog>` replicated from Scoring
- **Scenario state** — running flag, name, elapsed sec, fired counts
- **Environment** — wind direction/speed, active runway
- **TTS audio events** — RPC trigger telling clients to play a sound (audio
  data NOT replicated; each client generates locally via TTS server)

### Client -> server RPCs

- `ServerVector_<HeadingChange/AltChange/etc.>` — player ATC commands
- `ServerInject_Spawn`, `ServerInject_Emergency`, `ServerInject_Classify`,
  `ServerInject_Scramble`, `ServerInject_Wind`, `ServerInject_SetPaused`
- `ServerInject_LoadScenario`, `ServerInject_StopScenario`

### Roles

A new `EOperatorRole` enum: `Trainee` (full ATC + voice access), `Instructor`
(read-only scope + inject panel + freeze/rewind). PlayerState carries it.

## Build phases

### Phase 1 - Foundation (single PIE, two players)
Goal: Get TWO instances running in PIE, both seeing the same aircraft.

- [ ] Enable `bReplicates = true` on `AClearanceSimulationController` and
  `AClearanceAirspaceManager`
- [ ] Convert `AirspaceManager::AircraftStates` from `TMap<FName, FAircraftState>`
  to dual storage: TMap for fast server lookup + `TArray<FAircraftState>` for
  replication. Server writes both; clients only read the TArray (via OnRep).
- [ ] Mark the TArray `Replicated` with `OnRep_AircraftStates` callback
- [ ] Verify in PIE with 2 players: both see aircraft populated
- [ ] Verify aircraft state updates propagate (server moves, client sees move)

### Phase 2 - Instructor role + console injects
Goal: Instructor client can fire injects via console.

- [ ] `APlayerState` subclass with `EOperatorRole` UPROPERTY (Replicated)
- [ ] Role assignment: first connected = Trainee, second = Instructor
  (simplest; later we'll add a role-selection menu)
- [ ] Add Server RPCs on Controller: `ServerInject_DeclareEmergency`,
  `ServerInject_Spawn`, `ServerInject_Classify`, etc.
- [ ] Wire the existing `clearance.instructor.*` console commands to the RPCs
  when client is Instructor role; reject if Trainee
- [ ] Verify: instructor types `clearance.instructor.emergency BAW123 Mayday`,
  trainee sees BAW123 squawk flip to 7700

### Phase 3 - Scenario runner replication
Goal: Loading a scenario on the server propagates to client UI.

- [ ] Mark scenario metadata + elapsed time as Replicated on the runner
- [ ] Replicate fired event/trigger counts
- [ ] `OnRep_ScenarioStarted` fires client-side UI banner
- [ ] Instructor RPC: `ServerInject_LoadScenario(FName)` and StopScenario

### Phase 4 - Score + incident log replication
Goal: Instructor sees live score breakdown updating.

- [ ] Replicate `FScoringSnapshot` (current totals) on Scoring UObject
- [ ] Replicate `TArray<FIncidentLog>` (recent N incidents)
- [ ] OnRep updates the instructor's score panel

### Phase 5 - Instructor UMG panel
Goal: Real UI, not console.

- [ ] `WBP_InstructorStation` widget with sections:
  - Inject menu (spawn / emergency / classify / wind / pause)
  - Live aircraft list with select-and-act
  - Scenario load/stop dropdown
  - Live score breakdown
  - Event log with annotations
- [ ] Bound to F12 hotkey when client is Instructor role
- [ ] All buttons call the Server RPCs from Phase 2

### Phase 6 - Voice/audio events
Goal: TTS injects (AWACS calls, pilot readbacks) play on all clients.

- [ ] Multicast RPC: `MulticastPlayVoice(FName Callsign, FString Text, FString VoiceTag)`
- [ ] Each client invokes its local VoiceOutput->Speak with the same params
- [ ] Server originator suppresses its own multicast echo

### Phase 7 - Polish
- [ ] Connection UI: enter IP / find on LAN
- [ ] Role-selection menu on join
- [ ] Late-join initial state sync (the joiner gets all current aircraft)
- [ ] Listen server vs dedicated server build variants
- [ ] Network diagnostics overlay (ping, packet loss, throughput)

## Test plan per phase

**PIE multi-player setup:** UE editor -> Number of Players: 2 -> Play. Two
windows spawn (one server + one client). Use this for all phase verification
before testing on actual two-machine LAN.

**LAN test:** Once Phase 5 is solid, package the editor build + run on two
machines, connect via IP. Record the demo reel from this setup.

## Open questions

- **Voice input replication** — does the trainee's mic transcript replicate
  to the server (where the phraseology parser runs), or does each client run
  STT locally and replicate the parsed command? Probably the latter (avoids
  shipping raw audio over the wire).
- **DIS federation under replication** — the DIS emitter / receiver only run
  server-side; the federated aircraft enter the replicated state and reach
  clients via the normal TArray replication. Should be transparent.
- **AAR replay under replication** — the recorder captures the server's
  authoritative state. Replay is a separate mode where the server posts
  recorded state from disk; clients see it as if live.
