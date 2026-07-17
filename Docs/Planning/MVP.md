# Minimum Viable Product

**Project:** CLEARANCE
**Author:** Abdullah Ameed Abduljabbar
**Role:** Systems Designer
**Status:** Pre-production planning. Superseded by the Docs/Design/ set once the described scope shipped.

## Table of contents

- [Purpose](#purpose)
- [Document status](#document-status)
- [MVP definition](#mvp-definition)
- [In scope for MVP](#in-scope-for-mvp)
- [Out of scope for MVP](#out-of-scope-for-mvp)
- [Success criteria](#success-criteria)
- [Build priority order](#build-priority-order)
- [Definition of done](#definition-of-done)
- [Risks to MVP delivery](#risks-to-mvp-delivery)
- [Post-MVP outlook](#post-mvp-outlook)
- [References](#references)

## Purpose

This document defines the scope, sequencing, and acceptance criteria for the CLEARANCE Minimum Viable Product. It is written before implementation begins and is intended to draw a line between what the project commits to delivering as MVP and what is deliberately deferred.

CLEARANCE is a portfolio demonstrator and training-simulation prototype. The MVP described here is the point at which the project should be considered functionally complete against its original brief; everything beyond that is discretionary polish or scope expansion.

## Document status

This is a **planning** document. It captures intent before the code exists. It uses aspirational language ("shall", "will") because at the time of writing the systems do not yet exist. Where reality has since diverged from this plan, the shipped design and architecture documents under `Docs/Design/` take precedence.

The document is preserved here to keep the design record honest: what was originally scoped, what shifted during production, and where the final shipped scope grew beyond the MVP boundary drawn in this file.

## MVP definition

The CLEARANCE MVP is a **single-player, single-session first-person Air Traffic Control simulation** in which a player runs a sector, issues instructions to aircraft, resolves conflicts, and is scored on the outcome. The MVP shall demonstrate:

- A working five-system architecture (Airspace Management, Aircraft Behaviour, Communication, Conflict Detection, Scoring) with the ownership and delegate map defined in the systems design.
- Cognitive fidelity: aircraft respond to instructions on realistic timescales, conflict alerts escalate through graded severity levels, and scoring reflects the quality of the operator's decisions rather than any single game-y metric.
- A radar display that the player interacts with as their primary interface, including per-aircraft data blocks, an operator-facing HUD, and a simple text or voice instruction entry path.
- A single authoritative airspace state on which all dependent systems agree.

The MVP does **not** commit to networked play, federation, multi-scenario libraries, sensor modelling, electronic warfare, model-based subsystem integration, or an instructor station. These are considered post-MVP and appear in the Post-MVP outlook.

## In scope for MVP

The MVP shall include the following:

### Systems

- **Airspace Management System.** Single source of truth for every aircraft's callsign, position, altitude, speed, heading, phase, and wake category. Owns sector environmental state (wind, active runway). Broadcasts registration, deregistration, and state-update events.
- **Aircraft Behaviour System.** Per-aircraft object executing gradual motion under simplified performance-constrained flight dynamics. Only class permitted to mutate aircraft state.
- **Communication System.** Player instruction entry, per-instruction validation against current aircraft state, dispatch to the correct behaviour object. Feedback to the player on accept or reject.
- **Conflict Detection System.** Read-only monitor of every aircraft pair each tick. Fires alerts at Advisory, Warning, and Critical severity levels. Wake turbulence separation included at MVP level.
- **Scoring System.** Session-scope incident log, running score, difficulty scaling on the aircraft spawn rate.

### Gameplay features

- Free-play traffic generation (aircraft entering the sector at a controllable spawn rate).
- Player-issued heading, altitude, and speed instructions.
- Approach clearance and simulated landing sequence.
- Go-around triggered by conflict detection on approach.
- Graded conflict alerts on the radar HUD.
- Session score with a summary at session end.

### Presentation

- Radar-style scope with aircraft symbols, data blocks, and range indicators.
- Score display and current alert-level readout on the HUD.
- Text or basic voice input for player instructions.
- Scenario-free session start, session pause, session reset.

### Content

- One playable sector (either abstract or geospatial; not required to be a real-world airport for MVP).
- Configurable wind and active runway.
- Traffic mix drawn from the four wake categories.

## Out of scope for MVP

The following are explicitly deferred beyond the MVP boundary. They may be added later, but the MVP release does not commit to them.

### Networking

- Networked instructor station.
- Server-authoritative replication across multiple peers.
- Federated play with peer simulators.
- DIS, DDS, RTI, HLA, or any other interoperability stack.

### Sensor modelling

- Placed radar sites with individual detection models.
- Radar range equation modelling.
- Coverage overlays or sensor confidence rendering.
- Model-based DSP integration (Simulink or otherwise).

### Electronic warfare

- Jamming.
- Chaff clouds and ghost tracks.
- EW-aware scoring.

### Scenario authoring

- JSON-driven scenario library.
- Scripted voice injects.
- Trigger-based events.
- Multi-attempt training scenarios with distinct rules of engagement.

### Advanced training features

- Session recorder and replay.
- Checkpoint save and load.
- After-Action Report generation.
- Instructor inject controls.
- Multi-user grading and debrief workflow.

### Environmental modelling

- Geospatial reconstruction of real airfields (Cesium tiles).
- VR operator station.
- Photogrammetric terrain.

### Model-based subsystems

- Simulink cascade autopilot.
- Simulink radar signal processor.
- Embedded Coder integration.

These are documented in the Post-MVP outlook and were later added to the shipped scope as the project matured beyond MVP.

## Success criteria

The MVP release shall satisfy the following at a minimum:

1. A player can start a session, spawn traffic, issue instructions, resolve at least one conflict, land at least one aircraft, and receive a session score.
2. Aircraft respond to instructions on realistic timescales with no teleportation or instantaneous state changes.
3. Conflict alerts fire at the correct thresholds (nominally 8, 5, 3 nautical miles for civil separation) and escalate in order without skipping a level.
4. Wake turbulence separation is enforced for at least the ICAO Doc 4444 wake matrix categories the MVP includes.
5. Aircraft state is consistent across the radar display, the conflict detector, and the instruction validator at all times.
6. No aircraft enters a physically impossible state (below terrain, below stall, above service ceiling, or outside sector bounds) under any player action.
7. Session score updates in real time in response to scored events (landings, handoffs, separation losses, go-arounds).
8. The session can be reset without stale state from the previous run leaking into the next.

Each criterion above corresponds to a test-plan entry in the accompanying Test Plan document.

## Build priority order

The systems shall be built in the following order. Each system depends on the previous one being at least in a testable state before the next is started.

1. **Core types.** Enums, structs, delegates, and constants shared across every system. Written first because every subsequent system depends on this header set.
2. **Airspace Management System.** Owns aircraft state. Nothing meaningful can be built without a place to store aircraft state.
3. **Aircraft Behaviour System.** Executes movement. Depends on Airspace Management being able to accept state updates.
4. **Instruction Validator.** Stateless validation logic. Depends on Airspace Management being queryable for current aircraft state.
5. **Communication System (Comms Router).** Dispatch layer for player instructions. Depends on the validator and the behaviour system being callable.
6. **Conflict Detection System.** Read-only monitor. Depends on Airspace Management providing snapshots and on aircraft actually moving to produce meaningful conflict situations.
7. **Scoring System.** Session-scope logging and difficulty scaling. Depends on conflict, phase, and instruction events being fired.
8. **Aircraft Spawner.** Free-play traffic generator. Depends on Airspace Management accepting registrations and on Scoring providing the current difficulty.
9. **Simulation Controller.** Orchestrates the tick pipeline, binds delegates, owns UObject lifecycles for the UObject systems above. Formally comes last because it wires everything together.
10. **Minimal radar HUD and instruction UI.** Presentation layer. Only meaningful once the systems below it produce state worth rendering.

The order is deliberate. Building presentation before simulation state exists is a common failure mode; this project explicitly rejects it.

## Definition of done

The MVP is considered complete when:

- All ten build-priority items above compile, run, and produce their expected behaviour under free-play traffic.
- Every success criterion in Section 5 passes at least one exercise pass.
- Each test-plan entry corresponding to a success criterion passes.
- The player can complete a full session (start, sustained free-play, session-end score summary) without any hard failure that requires a restart.
- Documentation covers the shipped scope: at minimum a Systems Design document, a C++ scaffold, and a test plan reflect the delivered code.

Definition of done deliberately excludes performance benchmarks, packaging, and distribution. Those are considered part of a release preparation phase that follows MVP acceptance.

## Risks to MVP delivery

Risks identified during pre-production are tracked in `Docs/Planning/RiskRegister.md`. The subset that would most directly threaten MVP delivery:

- **R1: Ownership boundary drift.** If any system starts holding its own copy of aircraft state during implementation, the single-source-of-truth principle breaks and conflict detection becomes unreliable.
- **R2: Instruction validation gaps.** If the validator misses a class of physically impossible instructions, the behaviour system produces invalid states and the sim loses cognitive fidelity.
- **R3: Delegate binding brittleness.** If the controller misses a binding at session start, systems appear to work in isolation but events silently drop.
- **R4: Scope creep before core is stable.** Adding radar or federation or scenarios before the five core systems are proven risks producing a large surface area that never reaches a testable state.

Each risk carries a mitigation strategy in the Risk Register document.

## Post-MVP outlook

The following are considered natural next steps once MVP is stable. This section is deliberately short: it is the scope this document explicitly does not commit to, but it should be understood as the direction the project is expected to take.

- Networked instructor station with server-authoritative replication.
- Federation across DIS, DDS, RTI Connext, and HLA for interoperability with peer simulators.
- Placed radar sites with individual detection models and a coverage overlay.
- Electronic warfare (jamming, chaff, ghost tracks) integrated with the sensor layer.
- JSON-driven scenario library.
- Session recorder, replay, checkpoint, and After-Action Report.
- Simulink cascade autopilot and radar signal processor via Embedded Coder.
- Geospatial reconstruction of a real airfield on Cesium tiles.
- VR operator station.

All of the above were subsequently added to the shipped codebase and are documented in `Docs/Design/`. Their inclusion in the shipped scope does not retroactively expand the MVP boundary drawn here; the MVP was proven stable before any of them were started.

## References

Gregory, J. (2014) *Game Engine Architecture*, 2nd ed. CRC Press.

ICAO (2016) *Procedures for Air Navigation Services: Air Traffic Management*, Doc 4444, 16th ed. International Civil Aviation Organization.

Salen, K. and Zimmerman, E. (2003) *Rules of Play: Game Design Fundamentals*. MIT Press.
