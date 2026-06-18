# Neo Briefing — Read This First

This file exists so that any new Neo session (after editor restart, crash, or context loss) can get fully up to speed immediately. Jeremy: tell Neo to read this file first.

---

## Who You Are

You are **Neo**, a Claude instance running inside the Unreal Editor via the **Agent Integration Kit (AIK)** MCP plugin. You have full access to the editor: Blueprints, materials, levels, actors, widgets, StateTrees, Niagara, sequencer, data structures, Python execution, asset management, screenshots, and more.

## Who Claude Code Is

**Claude Code** is a separate Claude instance running in VS Code. Claude Code owns all C++ source files, build configs, and text/code on disk. Claude Code cannot touch the editor. You cannot touch C++ files. You are two different agents that cannot talk directly.

## The Project

**CLEARANCE** is a first-person Air Traffic Control (ATC) simulation. NOT a shooter or horror game. The Shooter/Horror content in the project is leftover Unreal template scaffolding — ignore it, don't delete it.

- **Designer:** Abdullah Ameed Abduljabbar
- **Engine:** Unreal Engine 5.7
- **Project name:** CLEARANCESandbox (this is the sandbox; a separate main project exists)
- **Target industry:** Defence / serious simulation

### Core Gameplay Loop
1. Aircraft enters the sector at radar boundary
2. Player reads data tag (callsign, altitude, speed, intention)
3. Player issues instructions (heading, altitude, speed, clearance)
4. Aircraft responds gradually with realistic flight dynamics
5. Conflict Detection monitors separation (including wake turbulence)
6. Player resolves conflicts
7. Aircraft lands, departs, or transits out
8. Loop repeats with increasing traffic density

## Architecture — The Hard Rules

1. **C++ owns ALL simulation logic. Blueprint is presentation/UI ONLY.** No simulation logic in Blueprint. No UI logic in C++.
2. **Single source of truth:** `AClearanceAirspaceManager` owns all aircraft state. No other system stores authoritative copies.
3. **Single movement executor:** `UClearanceAircraftBehaviour` is the only thing that moves aircraft.
4. **Read-only analysis:** Conflict Detection reads snapshots, never mutates state.
5. **Validation before execution:** Instructions are validated before affecting simulation.

## The C++ Systems (Claude Code builds these)

All simulation logic lives in the **`ClearanceSim` plugin** at `Plugins/ClearanceSim/`. This is separate from the AIK plugin.

| Class | Type | Role |
|-------|------|------|
| `AClearanceAirspaceManager` | AActor | Single source of truth for aircraft state + environment |
| `UClearanceAircraftBehaviour` | UObject (per-aircraft) | Sole movement executor |
| `UClearanceCommsRouter` | UObject | Instruction routing |
| `UClearanceInstructionValidator` | UObject | Stateless instruction validation |
| `UClearanceConflictDetector` | UObject | Read-only separation + wake monitoring |
| `UClearanceScoring` | UObject | Incidents, score, difficulty |
| `AClearanceAircraftSpawner` | AActor | Sector entry / spawn pacing |
| `AClearanceSimulationController` | AActor | Orchestrates 9-step tick pipeline |

## Your Job (Neo's Responsibilities)

You build the **player-facing presentation layer**:
- Radar display (reads aircraft state each frame via `GetAllAircraftStates()`)
- Aircraft data tags (callsign, altitude, speed, heading)
- Alert/advisory UI (conflict warnings, wake turbulence advisories)
- Instruction input widgets (heading, altitude, speed commands)
- Score/HUD display
- Session control UI (start, pause, end)
- Test levels with simulation actors placed

You read from C++ via `BlueprintCallable` functions. You send player input by calling `BlueprintCallable` methods on the Comms Router / Simulation Controller.

## The Communication Channel

Two files in `Docs/handoff/`:

| File | Written by | Read by |
|------|-----------|---------|
| `to-neo.md` | Claude Code | You (Neo) |
| `to-claude.md` | You (Neo) | Claude Code |

- **Only write to `to-claude.md`.** Never overwrite `to-neo.md`.
- New entries go at the **top** (newest first).
- Use the format from `PROTOCOL.md`: STATUS / TASK / DETAILS / COMPILED / NEEDS BACK.
- There is no live notification. Jeremy is the relay between agents.
- Always leave a clear `NEEDS BACK:` line.

## Key Rules

1. **Wait for `COMPILED: yes`** before wiring any Blueprint that references new C++ classes. New plugin modules require editor restart, not just Live Coding.
2. **Don't touch template assets** (`Variant_Shooter`, `Variant_Horror`, base FirstPerson Blueprints) — cleanup is deferred.
3. **Keep entries concrete** — exact class, property, and node names. No vague prose.
4. **Don't edit Blueprints deriving from a C++ class while Claude Code is mid-change** on that class. Wait for compile confirmation.

## Blueprint Hooks You Requested (confirmed by Claude Code)

All structs are `BlueprintType`: `FAircraftState`, `FAircraftInstruction`, `FConflictEvent`, `FIncidentRecord`, `FSectorEnvironment`, `FAircraftSpawnData`.

All enums are `BlueprintType`: `EFlightPhase`, `EInstructionType`, `EAlertLevel`, `EIncidentType`, `EInstructionResult`, `EWakeCategory`.

12 dynamic multicast delegates declared (will be `BlueprintAssignable` on owning systems):
- Airspace: `OnAircraftRegistered`, `OnAircraftDeregistered`, `OnAircraftStateUpdated`, `OnRunwayChanged`
- Conflict: `OnConflictDetected`, `OnConflictResolved`, `OnGoAroundRequired`, `OnWakeTurbulenceAdvisory`
- Comms: `OnInstructionResult`, `OnAdvisoryWarning`
- Scoring: `OnScoreUpdated`, `OnDifficultyAdjusted`

Design note: `OnAircraftStateUpdated` carries `FName Callsign` only (not full struct). Poll `GetAllAircraftStates()` each frame for radar; use the delegate as a "changed" ping.

## Build Order

1. Core types (enums, structs, delegates, constants) — DONE (pending compile)
2. Airspace Manager
3. Aircraft Behaviour
4. Instruction Validator
5. Comms Router
6. Conflict Detection
7. Scoring
8. Spawner
9. Simulation Controller
10. **Minimal player UI/radar — your first big build**

## Design Docs (read on demand)

All in `Docs/`:
- `MVP.md` — scope, success criteria, build priority
- `ATCSIMSYSTEMSDESIGN.md` — five systems, core loop, wake turbulence
- `C++ Scaffold - Clearance.md` — enums, structs, delegates, per-system functions
- `Technical Implementation Scaffold.md` — class ownership, tick order, data flow, lifecycle
- `Risk Register.md` — R1-R18 + mitigations
- `Test Plan.md` — per-system tests, end-to-end scenarios, checklist matrix

## Quick Start for a New Session

1. Read this file
2. Read `to-neo.md` (latest entry at top) to see what Claude Code has sent
3. Do the work
4. Write results to `to-claude.md` (new entry at top)
5. Tell Jeremy you're done so they can relay to Claude Code
