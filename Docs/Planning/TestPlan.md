# Test Plan

**Project:** CLEARANCE
**Author:** Abdullah Ameed Abduljabbar
**Role:** Systems Designer
**Status:** Pre-production planning. Superseded in part by shipped test evidence under `Docs/Verification/`.

## Table of contents

- [Purpose](#purpose)
- [Document status](#document-status)
- [Test strategy](#test-strategy)
- [Test levels](#test-levels)
- [Per-system test approach](#per-system-test-approach)
- [End-to-end scenarios](#end-to-end-scenarios)
- [Coverage matrix](#coverage-matrix)
- [Test cadence](#test-cadence)
- [Definition of done for MVP testing](#definition-of-done-for-mvp-testing)
- [Deferred testing](#deferred-testing)
- [References](#references)

## Purpose

This document defines the testing approach for CLEARANCE at MVP scope: which levels of test each system will receive, what a passing scenario looks like at the whole-system level, and how coverage is tracked against the requirements.

CLEARANCE is a portfolio demonstrator and training-simulation prototype. The plan captured here is written before the systems exist. Once test evidence is captured under `Docs/Verification/` the shipped verification documents take precedence.

## Document status

This is a **planning** document. Language is aspirational ("shall", "will") because at the time of writing the tests, evidence artefacts, and release gates referenced do not yet exist. Where reality has since diverged, the shipped Verification set (`Docs/Verification/Requirements.md`, `Docs/Verification/V_AND_V_PLAN.md`) supersedes.

## Test strategy

Testing is proportional to risk. CLEARANCE is not a Category A avionics box; it is a demonstrator. Rigour is targeted where it matters most: state ownership, safety-critical constants, wire-format compliance, and the ATC decision-making loop.

| Principle | Description |
|---|---|
| Traceability | Every requirement traces to at least one test (automation or manual). Every test tags the requirement it covers. |
| Proportionality | Depth of testing scales with risk. Wake matrix and DIS wire format get exhaustive automation; visual polish gets playtest. |
| Automation-first | Anything expressible as pure logic gets a unit test. Automation runs every commit; manual runs before every release. |
| Evidence capture | Manual procedures produce dated evidence artefacts stored under `Docs/Verification/Evidence/` on each run. |
| Fail visibly | Silent failures are unacceptable. A failing automation run blocks the release gate; a missing manual capture blocks the manual release checklist. |

## Test levels

Three tiers, each with a defined cost, framework, and confidence profile.

| Tier | Definition | Framework | Cost | When run |
|---|---|---|---|---|
| Unit (T1) | Tests pure helpers, static functions, `UObject`-based systems, or data-only logic without a live game map. Actor-owned behaviour that requires a spawned world is tested at T2. Runs sub-second. | UE `IMPLEMENT_SIMPLE_AUTOMATION_TEST` under `EAutomationTestFlags::EditorContext \| EngineFilter` | Very low | Every commit |
| Integration (T2) | Tests interactions between multiple subsystems using a spawned `UWorld` and a minimal actor harness. | UE `IMPLEMENT_COMPLEX_AUTOMATION_TEST` with map load and latent commands | Medium | Before every release |
| Manual (T3) | Requirements that need external tooling (Wireshark, RTI Admin Console) or a running runtime (`rtinode.exe`, a second CLEARANCE instance). Operator follows a written procedure and captures evidence. | Written procedure with pass criteria and evidence-capture format | High | Before every release; after touching the relevant subsystem |

The selection rule is:

| Requirement type | Tier |
|---|---|
| Pure logic (algorithm, constant, table lookup, wire serialisation) | T1 |
| Multi-subsystem interaction requiring an actor and a world | T2 |
| Requires external tooling, a running federate, or a peer runtime | T3 |

Default to the lowest tier that can cover the requirement. Only escalate when the lower tier genuinely cannot reach.

## Per-system test approach

Each of the five MVP systems gets its own test plan. Tests are named against the eventual `EIncidentType` and `EInstructionResult` enums so the requirement-to-test mapping is unambiguous.

### Airspace Management System

Because `AClearanceAirspaceManager` is an `AActor`, its tests require a spawned world and default to T2.

| Test | Tier | What it exercises |
|---|---|---|
| Aircraft registration returns success | T2 | `RegisterAircraft` accepts a valid `FAircraftSpawnData` and returns true |
| Duplicate registration rejected | T2 | Second registration of the same callsign returns false |
| State update through valid path accepted | T2 | `RequestStateUpdate` on a registered callsign commits and returns true |
| State update on unregistered callsign rejected | T2 | Same call on an unknown callsign returns false; state map unchanged |
| Get on unknown callsign returns invalid state | T2 | `GetAircraftState` returns `bIsValid = false` for unknown callsign |
| Same-frame read stability | T2 | Multiple reads during the same simulation frame return the last committed state; no partial state is exposed. |
| Wind change triggers runway recalculation | T2 | `UpdateWindConditions` above the crosswind threshold flips `ActiveRunwayHeading` |
| Runway change broadcast fires exactly once per flip | T2 | `OnRunwayChanged` fires once on flip, does not fire on identical wind ticks |
| Deregistration removes state and broadcasts | T2 | `DeregisterAircraft` clears the map entry and fires `OnAircraftDeregistered` |
| Multi-aircraft state persistence across ticks | T2 | Ten aircraft registered, state committed each tick, all still readable at tick N+50 |

### Aircraft Behaviour System

| Test | Tier | What it exercises |
|---|---|---|
| Heading change reaches target at correct turn rate | T1 | Aircraft commanded to new heading closes error under the category's turn rate; overshoot within tolerance |
| Altitude change reaches target at correct climb rate | T1 | Aircraft commanded to new altitude climbs at density-adjusted rate; target captured cleanly |
| Speed change reaches target at correct accel rate | T1 | Aircraft speed matches category acceleration constant |
| Position updates with wind drift | T1 | Aircraft on constant heading in constant wind drifts along the wind vector at the wind vector magnitude |
| Bank angle clamped by category | T1 | Aggressive heading command produces bank at or below `BankLimitDeg` for the category |
| Service ceiling not exceeded under climb | T1 | Altitude commanded above ceiling clamps at ceiling; climb rate zero at cap |
| Speed clamped to min operating speed | T1 | Speed command below `MinOperatingSpeed` clamps at min; aircraft does not stall |
| Go-around aborts approach and climbs | T1 | `ExecuteGoAround` sets `bGoingAround`, aircraft climbs `GoAroundClimbFt` above current altitude |
| Approach guidance captures localiser | T2 | Aircraft cleared for approach with correct geometry establishes on the localiser within capture corridor |
| Aircraft state consistent across many ticks | T2 | Aircraft flown for 100 ticks with no instruction remains within tolerance of initial values |

### Communication System

| Test | Tier | What it exercises |
|---|---|---|
| Valid heading instruction accepted | T1 | Heading within envelope on a registered callsign returns `Accepted` |
| Valid altitude instruction accepted | T1 | Altitude within envelope on a registered callsign returns `Accepted` |
| Invalid callsign rejected | T1 | Any instruction on an unregistered callsign returns `Rejected_InvalidCallsign` |
| Altitude above ceiling rejected | T1 | Altitude command above `ServiceCeiling` returns `Rejected_PhysicallyImpossible` |
| Altitude below zero rejected | T1 | Altitude command below 0 returns `Rejected_PhysicallyImpossible` |
| Speed below stall rejected | T1 | Speed command below `MinOperatingSpeed` returns `Rejected_PhysicallyImpossible` |
| Speed above VMO rejected | T1 | Speed command above `MaxOperatingSpeed` returns `Rejected_PhysicallyImpossible` |
| NaN target rejected | T1 | Any non-finite target returns `Rejected_PhysicallyImpossible` |
| Go-around bypasses envelope check | T1 | Instruction with `bIsGoAround = true` accepted even if it would otherwise fail envelope |
| Exiting aircraft rejects further instructions | T1 | Instruction to an aircraft in `Exiting` phase returns `Rejected_AircraftExited` |

### Conflict Detection System

| Test | Tier | What it exercises |
|---|---|---|
| Advisory fires at 8 nm horizontal | T1 | Aircraft pair at 7.9 nm horizontal, under 1000 ft vertical, `AlertFromSeparation` returns `Advisory` |
| Warning fires at 5 nm horizontal | T1 | Same test at 4.9 nm returns `Warning` |
| Critical fires at 3 nm horizontal | T1 | Same test at 2.9 nm returns `Critical` |
| No alert with vertical separation above 1000 ft | T1 | Aircraft pair at 1 nm horizontal but 1500 ft vertical returns `None` |
| Wake advisory Light-behind-Heavy at 6 nm | T1 | Light trailing Heavy at 5.9 nm returns wake advisory |
| Wake matrix monotonic | T1 | Advisory > Warning > Critical in nm, ordering invariant holds under randomised values |
| TCAS RA fires at Critical | T1 | Pair crossing Critical broadcasts `OnTCASResolutionAdvisory` exactly once |
| TCAS RA does not fire on already active pair | T1 | Same pair remaining Critical does not fire a second RA |
| Engagement suppression on GCI pair | T2 | Viper and Hostile in Critical range produce no civilian alerts |
| Trajectory projection fires Advisory early | T2 | Pair currently clear but converging fires Advisory within `ProjectionLookaheadSeconds` |

### Scoring System

| Test | Tier | What it exercises |
|---|---|---|
| Every `EIncidentType` maps to a point delta | T1 | Each enum value produces the expected score change under `LogIncident` |
| `LogIncident` appends one record per call | T1 | Ten calls produce ten records; no dedup, no drop |
| `ResetSession` clears log and counters | T1 | After reset, log empty, score zero, counters zero, spawn interval at base |
| Successful handoff scales difficulty | T1 | Each handoff shrinks spawn interval by `DifficultySecondsPerHandled` |
| Spawn interval clamped at minimum | T1 | Many handoffs cannot push interval below `MinSpawnIntervalSeconds` |
| Efficiency bounded [0, 100] | T1 | Efficiency stays in bounds regardless of the sequence of incidents |
| Session log replicates correctly | T2 | Client sees the same `IncidentLog` the server has produced within one replication cycle |

## End-to-end scenarios

Full-session tests exercise the systems together. Each scenario is a scripted sequence of player actions with expected outcomes.

| ID | Scenario | Tier | Setup | Player actions | Expected outcome |
|---|---|---|---|---|---|
| E1 | Baseline free-play | T3 | Manual playtest. Sector empty; free-play spawner on at default rate | Play for 3 minutes issuing routine instructions | At least one landing scored, no invalid state, no crash. Score positive. |
| E2 | Single conflict resolution | T3 | Manual playtest. Two aircraft on converging tracks at same altitude | Vector one aircraft off course | Advisory fires as expected. Player resolves before Critical. `SuccessfulResolution` incident logged. |
| E3 | Wake separation compliance | T3 | Manual playtest. Light aircraft behind Heavy on approach | Player leaves in trail | Wake advisory fires at correct distance. Player either accepts penalty or vectors following aircraft off. |
| E4 | Invalid instruction rejection | T3 | Manual playtest. Any aircraft | Player issues altitude above ceiling, speed below stall, and a NaN via console injection | All three rejected with reasons spoken to the operator. No aircraft state modified. |
| E5 | Session reset | T3 | Manual playtest. Any session with active traffic and score | Player invokes reset | Log, score, counters, and traffic all cleared. New session starts cleanly. |
| E6 | Sustained load | T3 | Manual playtest. Free-play with high difficulty | Play for 10 minutes without pausing | Performance remains within budget. No state drift. No stale replicated arrays. |

## Coverage matrix

Requirements are grouped by system. This matrix is filled in as tests are written.

| System | Requirement count (planned) | Covered by | Verified by |
|---|---|---|---|
| Airspace Management | ~12 | Airspace T2 above | Automation, end-to-end E1 |
| Aircraft Behaviour | ~10 | Behaviour T1 + T2 above | Automation, end-to-end E1, E2, E6 |
| Communication | ~10 | Comms T1 above | Automation, end-to-end E4 |
| Conflict Detection | ~10 | Conflict T1 + T2 above | Automation, end-to-end E2, E3 |
| Scoring | ~7 | Scoring T1 + T2 above | Automation, end-to-end E1, E5 |

The eventual shipped mapping will live in `Docs/Verification/Requirements.md`.

## Test cadence

When to run what.

| Trigger | T1 unit | T2 integration | T3 manual | End-to-end |
|---|---|---|---|---|
| Every source-file commit | Yes | | | |
| Touching a subsystem | Yes | Yes for that subsystem | Yes for that subsystem | |
| Before every release | Yes | Yes | Yes | Yes |
| Before recording a video | Yes | Yes | Yes | Yes |
| UE version upgrade | Yes | Yes | Yes | Yes |
| Discovering a regression | Yes | Yes for the affected subsystem | | Yes for the affected scenario |

## Definition of done for MVP testing

Testing is considered complete for MVP when every row below is satisfied.

| Gate | Condition |
|---|---|
| Requirement coverage | Every requirement in the five MVP systems has at least one appropriate covering test: T1 where pure logic is sufficient, T2 where actor or world interaction is required, or T3 where manual runtime evidence is required. |
| Integration coverage | Every multi-system interaction listed in the T2 rows above passes on a clean map |
| End-to-end coverage | Every scenario E1 through E6 has been played through at least once with a passing outcome |
| Manual capture | Where a manual procedure exists, at least one dated evidence artefact is captured for it |
| Regression policy | Any test that was previously green and is now red must be fixed before any new feature work begins |

## Deferred testing

Testing scope explicitly deferred beyond MVP. These are documented so the intent is captured, not so they become MVP commitments.

| Deferred item | Reason |
|---|---|
| Federation wire format compliance testing | Federation is post-MVP; wire tests come with the federation work |
| Sensor layer detection modelling tests | Sensor layer is post-MVP |
| Electronic warfare effect tests | EW is post-MVP |
| Session recorder round-trip tests | Recorder is post-MVP |
| Checkpoint save/load equality tests | Checkpoint is post-MVP |
| Simulink model-based subsystem integration tests | MBD is post-MVP |
| Performance load testing at high aircraft counts | Not a functional correctness concern for MVP; measured informally during playtest |
| Localisation and accessibility testing | Not the portfolio narrative |
| Security testing | Not applicable for a single-player demonstrator at MVP scope |

Many of these were later scoped into the shipped verification set once the corresponding systems were built. Their inclusion in later verification evidence does not retroactively expand the MVP testing boundary drawn here.

## References

Gregory, J. (2014) *Game Engine Architecture*, 2nd ed. CRC Press.

ICAO (2016) *Procedures for Air Navigation Services: Air Traffic Management*, Doc 4444, 16th ed. International Civil Aviation Organization.

Salen, K. and Zimmerman, E. (2003) *Rules of Play: Game Design Fundamentals*. MIT Press.
