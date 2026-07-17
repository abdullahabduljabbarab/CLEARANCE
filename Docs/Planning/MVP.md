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

The MVP shall include the following.

### Systems

| System | Purpose |
|---|---|
| Airspace Management System | Single source of truth for every aircraft's callsign, position, altitude, speed, heading, phase, and wake category. Owns sector environmental state (wind, active runway). Broadcasts registration, deregistration, and state-update events. |
| Aircraft Behaviour System | Per-aircraft object executing gradual motion under simplified performance-constrained flight dynamics. Only class permitted to mutate aircraft state. |
| Communication System | Player instruction entry, per-instruction validation against current aircraft state, dispatch to the correct behaviour object. Feedback to the player on accept or reject. |
| Conflict Detection System | Read-only monitor of every aircraft pair each tick. Fires alerts at Advisory, Warning, and Critical severity levels. Wake turbulence separation included at MVP level. |
| Scoring System | Session-scope incident log, running score, difficulty scaling on the aircraft spawn rate. |

### Gameplay features

| Feature | Description |
|---|---|
| Free-play traffic | Aircraft entering the sector at a controllable spawn rate. |
| Player instructions | Heading, altitude, and speed change commands issued by the player. |
| Approach and landing | Approach clearance and a simulated landing sequence to touchdown. |
| Go-around | Automatic go-around when conflict detection fires on an approach pair. |
| Graded conflict alerts | Advisory, Warning, and Critical alerts surfaced on the radar HUD. |
| Session score | Running score during the session and a summary at session end. |

### Presentation

| Element | Description |
|---|---|
| Radar scope | Aircraft symbols, data blocks, range indicators, sector boundary. |
| HUD | Score display and current alert-level readout. |
| Instruction entry | Text input at minimum; simple voice input as an optional MVP path. |
| Session control | Session start, pause, and reset without stale state leakage. |

### Content

| Item | Description |
|---|---|
| Sector | One playable sector, either abstract or geospatial. Real-world airport reconstruction is not required for MVP. |
| Weather | Configurable wind direction and speed. Active runway derived from wind. |
| Traffic mix | Aircraft drawn from the four ICAO wake categories (Light, Medium, Heavy, Super). |

## Out of scope for MVP

The following are explicitly deferred beyond the MVP boundary. They may be added later, but the MVP release does not commit to them.

| Category | Deferred item | Reason for deferral |
|---|---|---|
| Networking | Networked instructor station | MVP is single-player; multi-peer replication is a large surface area unrelated to core loop |
| Networking | Server-authoritative replication across multiple peers | As above |
| Networking | Federated play with peer simulators | As above |
| Networking | DIS, DDS, RTI, HLA, or any other interoperability stack | As above |
| Sensor modelling | Placed radar sites with individual detection models | MVP uses a single omniscient scope; sensor separation is post-MVP |
| Sensor modelling | Radar range equation modelling | As above |
| Sensor modelling | Coverage overlays or sensor confidence rendering | As above |
| Sensor modelling | Model-based DSP integration (Simulink or otherwise) | As above |
| Electronic warfare | Jamming | Requires the sensor layer above to be meaningful |
| Electronic warfare | Chaff clouds and ghost tracks | As above |
| Electronic warfare | EW-aware scoring | As above |
| Scenario authoring | JSON-driven scenario library | MVP is free-play only; scripted scenarios are a training-tool layer above MVP |
| Scenario authoring | Scripted voice injects | As above |
| Scenario authoring | Trigger-based events | As above |
| Scenario authoring | Multi-attempt training scenarios with distinct ROE | As above |
| Advanced training | Session recorder and replay | Instructor workflow, not part of MVP core loop |
| Advanced training | Checkpoint save and load | As above |
| Advanced training | After-Action Report generation | As above |
| Advanced training | Instructor inject controls | As above |
| Advanced training | Multi-user grading and debrief workflow | As above |
| Environmental modelling | Geospatial reconstruction of real airfields (Cesium tiles) | MVP allows an abstract sector; real-world reconstruction is presentation polish |
| Environmental modelling | VR operator station | MVP targets a flat-screen radar HUD |
| Environmental modelling | Photogrammetric terrain | As above |
| Model-based subsystems | Simulink cascade autopilot | Independent research thread; core behaviour system already covers movement |
| Model-based subsystems | Simulink radar signal processor | Requires the sensor layer above to be meaningful |
| Model-based subsystems | Embedded Coder integration | As above |

These are documented in the Post-MVP outlook and were later added to the shipped scope as the project matured beyond MVP.

## Success criteria

The MVP release shall satisfy the following at a minimum. Each criterion maps to an entry in the accompanying Test Plan document.

| # | Criterion | What "pass" looks like |
|---|---|---|
| 1 | End-to-end playable loop | Player can start a session, spawn traffic, issue instructions, resolve at least one conflict, land at least one aircraft, and receive a session score. |
| 2 | Gradual motion | Aircraft respond to instructions on realistic timescales with no teleportation or instantaneous state changes. |
| 3 | Alert ladder | Conflict alerts fire at the configured horizontal thresholds (nominally 8, 5, 3 nautical miles for civil separation) and account for the vertical separation minimum where relevant. Escalation proceeds in order without skipping a level. |
| 4 | Wake separation | Wake turbulence separation is enforced using the wake-category rules defined for CLEARANCE, informed by ICAO Doc 4444 separation concepts. |
| 5 | Single source of truth | Aircraft state is consistent across the radar display, the conflict detector, and the instruction validator at all times. |
| 6 | Envelope safety | No aircraft enters a physically impossible or uncontrolled state under player action; planned exits and handoffs are handled through the intended sector-exit flow. |
| 7 | Live scoring | Session score updates in real time in response to scored events (landings, handoffs, separation losses, go-arounds). |
| 8 | Clean reset | Session can be reset without stale state from the previous run leaking into the next. |

## Build priority order

The systems shall be built in the following order. Each system depends on the previous one being at least in a testable state before the next is started.

| # | System | Depends on | Why this order |
|---|---|---|---|
| 1 | Core types (enums, structs, delegates, constants) | Nothing | Every subsequent system includes this header set |
| 2 | Airspace Management System | Core types | Owns aircraft state; nothing meaningful exists without it |
| 3 | Aircraft Behaviour System | Airspace Management | Executes movement; needs a place to commit state |
| 4 | Instruction Validator | Airspace Management | Stateless; needs current state to validate against |
| 5 | Communication System (Comms Router) | Validator, Behaviour | Dispatch layer that composes validator + behaviour |
| 6 | Conflict Detection System | Airspace Management, Behaviour | Reads committed snapshots; needs aircraft to actually move to produce conflicts |
| 7 | Scoring System | Conflict, Comms, Airspace | Consumes events fired by the systems above |
| 8 | Aircraft Spawner | Airspace, Scoring | Registers new aircraft; asks Scoring for current difficulty |
| 9 | Simulation Controller | All systems above | Wires the tick pipeline, binds delegates, owns UObject lifecycles |
| 10 | Minimal radar HUD and instruction UI | All systems above | Presentation layer; only meaningful once systems produce state |

The order is deliberate. Building presentation before simulation state exists is a common failure mode; this project explicitly rejects it.

## Definition of done

The MVP is considered complete when every row below is satisfied.

| Gate | Condition |
|---|---|
| Build | All ten build-priority items compile and run under free-play traffic. |
| Behaviour | Every success criterion above passes at least one exercise pass. |
| Test coverage | Each test-plan entry corresponding to a success criterion passes. |
| Full loop | Player can complete a full session (start, sustained free-play, session-end score summary) without a hard failure that requires a restart. |
| Documentation | Systems Design, C++ scaffold, and Test Plan documents reflect the delivered code at minimum. |

Definition of done deliberately excludes performance benchmarks, packaging, and distribution. Those are considered part of a release preparation phase that follows MVP acceptance.

## Risks to MVP delivery

Risks identified during pre-production are tracked in `Docs/Planning/RiskRegister.md`. The subset that would most directly threaten MVP delivery is summarised below.

| ID | Risk | Impact if it materialises |
|---|---|---|
| R1 | Ownership boundary drift | Any system holding its own copy of aircraft state breaks the single-source-of-truth principle and makes conflict detection unreliable. |
| R2 | Instruction validation gaps | Missed classes of physically impossible instructions produce invalid states in the behaviour system and the sim loses cognitive fidelity. |
| R3 | Delegate binding brittleness | A missed binding at session start makes systems appear to work in isolation while events silently drop between them. |
| R4 | Scope creep before core is stable | Adding radar, federation, or scenarios before the five core systems are proven produces a large surface area that never reaches a testable state. |

Each risk carries a mitigation strategy in the Risk Register document.

## Post-MVP outlook

The following are considered natural next steps once MVP is stable. This section is deliberately short: it is the scope this document explicitly does not commit to, but it should be understood as the direction the project is expected to take.

| Post-MVP direction | Purpose |
|---|---|
| Networked instructor station with server-authoritative replication | Adds a second operator role and multi-peer training workflow |
| Federation across DIS, DDS, RTI Connext, and HLA | Interoperability with peer simulators |
| Placed radar sites with individual detection models and a coverage overlay | Sensor layer separation between what the operator sees and what the god view knows |
| Electronic warfare (jamming, chaff, ghost tracks) | Degraded-picture decision-making drills |
| JSON-driven scenario library | Scripted training exercises with fixed rules of engagement |
| Session recorder, replay, checkpoint, After-Action Report | Instructor and trainee review workflow |
| Simulink cascade autopilot and radar signal processor via Embedded Coder | Model-based-design integration research |
| Geospatial reconstruction of a real airfield on Cesium tiles | Photogrammetric environment for immersion |
| VR operator station | First-person diegetic operator experience |

Many of the above were subsequently added to the shipped codebase and are documented in `Docs/Design/`. Their inclusion in the later shipped scope does not retroactively expand the MVP boundary drawn here; the MVP represented the original stable core before post-MVP expansion.

## References

Gregory, J. (2014) *Game Engine Architecture*, 2nd ed. CRC Press.

ICAO (2016) *Procedures for Air Navigation Services: Air Traffic Management*, Doc 4444, 16th ed. International Civil Aviation Organization.

Salen, K. and Zimmerman, E. (2003) *Rules of Play: Game Design Fundamentals*. MIT Press.
