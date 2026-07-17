# Risk Register

**Project:** CLEARANCE
**Author:** Abdullah Ameed Abduljabbar
**Role:** Systems Designer
**Status:** Pre-production planning. Superseded in part by shipped verification evidence under `Docs/` and by the Test Plan.

## Table of contents

- [Purpose](#purpose)
- [Document status](#document-status)
- [Assessment approach](#assessment-approach)
- [Risk categories](#risk-categories)
- [Architecture risks](#architecture-risks)
- [Simulation fidelity risks](#simulation-fidelity-risks)
- [Player-facing risks](#player-facing-risks)
- [Post-MVP integration risks](#post-mvp-integration-risks)
- [Production process risks](#production-process-risks)
- [Mitigation summary](#mitigation-summary)
- [Change log](#change-log)
- [References](#references)

## Purpose

This document catalogues risks identified during pre-production for CLEARANCE, ranks each by likelihood and impact, and records the mitigation strategy the project will use to keep the risk from materialising or to contain it if it does.

CLEARANCE is a portfolio demonstrator and training-simulation prototype. The register is written as a planning artefact before implementation begins. It uses aspirational language for mitigations because at the time of writing the systems, tests, and processes referenced do not yet exist.

## Document status

This is a **planning** document. Where reality has since diverged from the mitigations proposed here, the shipped design and verification documents (`Docs/Design/`, `Docs/REQUIREMENTS.md`, `Docs/V_AND_V_PLAN.md`) take precedence. The register is preserved to keep the design record honest: what was anticipated, what mitigations were planned, and where the eventual production build actually landed.

## Assessment approach

Every risk carries a likelihood, an impact, and a combined severity band. The bands are qualitative rather than numeric because CLEARANCE is a solo development effort and precise probability estimates would be false precision.

| Level | Likelihood | Impact |
|---|---|---|
| High (H) | Expected to occur without deliberate mitigation | Blocks MVP release or requires significant rework |
| Medium (M) | Plausible during the build if attention slips | Delays a milestone or degrades a system's usability |
| Low (L) | Unlikely under normal build discipline | Nuisance, degraded polish, or easily worked around |

Severity is derived from the pair:

| Impact \ Likelihood | Low | Medium | High |
|---|---|---|---|
| **High** | Medium | High | Critical |
| **Medium** | Low | Medium | High |
| **Low** | Low | Low | Medium |

Critical and High severity risks require an active mitigation before implementation begins. Medium risks require a documented mitigation. Low risks are noted but do not require dedicated tracking.

## Risk categories

Risks are grouped by category so mitigations of the same shape sit near each other:

| Category | Focus |
|---|---|
| Architecture | Ownership boundaries, delegate map, tick ordering, cross-system data flow |
| Simulation fidelity | Flight dynamics, safety analysis, envelope enforcement, wake matrix |
| Player-facing | Instruction feedback, radar accuracy, voice input, alert timing |
| Post-MVP integration | Networking, federation, sensor layer, model-based subsystems |
| Production process | Documentation drift, test coverage, save and replay integrity |

## Architecture risks

Risks tied to how the runtime systems are put together.

| ID | Risk | Likelihood | Impact | Severity |
|---|---|---|---|---|
| R1 | Ownership boundary drift | H | H | Critical |
| R2 | Instruction validation gaps | M | H | High |
| R3 | Delegate binding brittleness | M | M | Medium |
| R4 | Scope creep before core is stable | H | M | High |
| R5 | Tick order violation | L | H | Medium |

### R1: Ownership boundary drift

**Description.** A system other than the Airspace Manager starts holding its own copy of aircraft state during implementation, either as a "cache" for performance or as a convenience during a rush.

**Consequence.** Conflict detection reads stale data, radar renders positions that differ from what the validator sees, cognitive fidelity collapses. Debugging becomes very hard because different systems disagree about ground truth.

**Mitigation.** The Airspace Manager's aircraft map is private. Every dependent system reads through a getter and writes through the one sanctioned request path. No other class is permitted to store an authoritative copy. Enforced by class boundary and reviewed on every change to a system that touches aircraft state.

**Trigger signs.** A new `TMap<FName, FAircraftState>` appearing outside the Airspace Manager. Aircraft position appearing to "flicker" between two values in a single frame.

### R2: Instruction validation gaps

**Description.** The Instruction Validator misses a class of physically impossible instructions (for example, altitude above service ceiling, speed below stall, bank angle outside envelope) and the behaviour system executes them.

**Consequence.** Aircraft enter invalid states. The sim loses cognitive fidelity because the player can command physically impossible manoeuvres.

**Mitigation.** Validator is stateless. Every rejection reason is enumerated in `EInstructionResult`. Unit tests cover the envelope for every wake category and the military profile. Rejections produce a spoken refusal so the player hears the failure rather than seeing silent success.

**Trigger signs.** Aircraft accepting a heading command that produces bank angles beyond category limits. Aircraft climbing past service ceiling. Any test that fires a `Rejected_PhysicallyImpossible` false-negative.

### R3: Delegate binding brittleness

**Description.** The Simulation Controller misses a delegate binding at session start. Systems appear to work in isolation but events silently drop between them.

**Consequence.** Silent failures. Scoring never receives a conflict event; conflict detection never receives an aircraft-registered event; radar sites paint an empty sector.

**Mitigation.** Delegate bindings are done in one `BindDelegates` function called from `BeginPlay`. Every listener registration is a single line at the same indent; a missing entry is visually obvious on review. Automation tests exercise each event path end-to-end.

**Trigger signs.** A system that "does nothing" during play despite compiling and initialising cleanly.

### R4: Scope creep before core is stable

**Description.** Federation, radar sites, electronic warfare, or scenarios are started before the five core systems are proven, on the assumption that they "should be easy to add later".

**Consequence.** Large surface area with no testable core. MVP slips indefinitely. Refactoring the core becomes painful because a large surface area has already been built against a shaky foundation.

**Mitigation.** MVP boundary defined in `Docs/Planning/MVP.md` is respected. Any post-MVP work only begins after the five core systems pass their success criteria.

**Trigger signs.** Federation or sensor code appearing in the source tree before the Conflict Detector is testable. A backlog of "quick tweaks" queued against the core because a downstream system needs them.

### R5: Tick order violation

**Description.** A system is added that ticks independently rather than being called by the Simulation Controller in the fixed order.

**Consequence.** Race conditions between the new system and the rest of the tick pipeline. Downstream systems read stale data.

**Mitigation.** Only three classes are permitted to Tick as actors (Simulation Controller, Airspace Manager, Spawner). Every other system is called by the controller in the authoritative order. Enforced by the class ownership section of the Runtime Technical Architecture document.

**Trigger signs.** A new UObject appearing with its own `Tick` implementation. A stateful class in a subsystem folder with independent tick behaviour.

## Simulation fidelity risks

Risks tied to the sim producing behaviour a controller would find plausible.

| ID | Risk | Likelihood | Impact | Severity |
|---|---|---|---|---|
| R6 | Flight dynamics feel wrong | M | M | Medium |
| R7 | Wake matrix threshold errors | L | H | Medium |
| R8 | Envelope enforcement gaps | M | H | High |
| R9 | Conflict projection false positives | M | M | Medium |
| R10 | Wind drift not applied consistently | L | M | Low |

### R6: Flight dynamics feel wrong

**Description.** Aircraft turn too slowly or too quickly, climb rates feel unrealistic, or aircraft appear to fly like arcade tokens rather than real airframes.

**Consequence.** Cognitive fidelity collapses. The player stops treating the sim as an ATC exercise and starts treating it as a game.

**Mitigation.** Per-category performance envelopes drawn from representative real aircraft (Cessna 172S, 737-800, 777-300ER, A380-800). Turn rates derived from coordinated-turn kinematics. Climb rates density-adjusted via a simplified ISA lookup. Playtesting against reference recordings during MVP.

**Trigger signs.** A 737 turning like a fighter. A Heavy climbing at Light rates. Playtest feedback of "feels arcade-y".

### R7: Wake matrix threshold errors

**Description.** Wake separation constants transposed, mislabelled, or off by a factor of two.

**Consequence.** Wake advisories fire at the wrong distance. Player either sees false positives (loses trust in the system) or misses real violations (unsafe).

**Mitigation.** Wake matrix values pinned to a shared constants header (`ClearanceConstants.h`). Automation tests assert every pair-category threshold against the ICAO Doc 4444 §5.8 published values. Monotonic ordering invariants (Advisory > Warning > Critical) tested separately.

**Trigger signs.** A wake advisory firing between two same-category aircraft at 8 nm (standard minimum is 3 nm). A miss on a Light-behind-Heavy at 5 nm (matrix requires 6 nm).

### R8: Envelope enforcement gaps

**Description.** An aircraft is allowed to fly outside its physical envelope: below stall, above service ceiling, or in a state its category should not support.

**Consequence.** Aircraft enter physically impossible states. The sim loses cognitive fidelity and player instructions produce nonsense results.

**Mitigation.** Envelope enforcement lives in three places: validator rejects invalid instructions upfront, behaviour clamps state values on every step, Airspace Manager's `ClampStateValues` acts as a final safety net. Unit tests cover the envelope corners for every category.

**Trigger signs.** An aircraft reported at negative altitude, above published service ceiling, or below stall speed.

### R9: Conflict projection false positives

**Description.** Forward trajectory projection produces false-positive advisories because it does not account for the player's imminent action.

**Consequence.** Player sees an advisory alert firing for a conflict they are already resolving; alert fatigue sets in and real advisories get ignored.

**Mitigation.** Projection window kept short (60 seconds default) and tunable. Advisory alerts clear the tick after the projected conflict is no longer projected. Player-facing advisory text explains "projected in N seconds" so the player can distinguish present-tense violations from projections.

**Trigger signs.** Player reporting "the alert fires and then clears itself two seconds later" as a normal occurrence.

### R10: Wind drift not applied consistently

**Description.** Wind drift applied to some aircraft (say, on approach) but not others (say, cruising in level flight).

**Consequence.** Aircraft on the same heading drift at different ground tracks depending on flight phase, producing surprising radar rendering.

**Mitigation.** Wind drift is applied in one place (behaviour's position-step) that runs for every aircraft on every tick regardless of phase.

**Trigger signs.** Two aircraft on the same heading with different wake-symmetric ground tracks.

## Player-facing risks

Risks tied to how the sim presents itself to the operator.

| ID | Risk | Likelihood | Impact | Severity |
|---|---|---|---|---|
| R11 | Instruction rejection is opaque | H | M | High |
| R12 | Radar renders desynced from state | L | H | Medium |
| R13 | Voice input unreliability | H | M | High |
| R14 | Alert timing too late for reaction | M | H | High |
| R15 | Data block clutter under load | M | M | Medium |

### R11: Instruction rejection is opaque

**Description.** An instruction is rejected but the player does not learn why. The pilot voice does not read back, the HUD does not show a reason, the transcript does not log the refusal.

**Consequence.** Player retries the same failed instruction, thinks the sim is unresponsive, or generalises the wrong lesson from the rejection.

**Mitigation.** Every rejection produces a spoken refusal with the reason. Transcript logs the refusal role-coloured against the pilot. Instruction result event carries the rejection reason so downstream UI can react.

**Trigger signs.** Player saying "I told it to descend to five thousand and nothing happened."

### R12: Radar renders desynced from state

**Description.** The radar display shows an aircraft position that differs from what the Conflict Detector or Validator sees, even briefly.

**Consequence.** Player issues instructions against the visible position and the sim resolves against the actual position, producing counterintuitive behaviour.

**Mitigation.** Radar rendering reads through the Airspace Manager via `BlueprintCallable` getters. Radar never caches state. Read pass runs once per frame from the same snapshot every other system used.

**Trigger signs.** An aircraft symbol lagging its actual movement across the scope by more than one frame at 60 Hz.

### R13: Voice input unreliability

**Description.** Voice input fails to transcribe reliably, or fails on packaged builds where the TTS or STT server is not installed.

**Consequence.** Player falls back to console commands, losing the cognitive-fidelity value of a voice interface, or the packaged build ships with a broken feature.

**Mitigation.** Whisper is local and does not require internet. TTS server bundled as a standalone executable via PyInstaller. Text console entry always available as a fallback. Voice failures are transparent (no muted errors).

**Trigger signs.** Recognition rate below acceptable threshold on the developer's own microphone under quiet conditions. Packaged build with no pilot voices.

### R14: Alert timing too late for reaction

**Description.** Conflict alerts fire so close to the actual violation that the player cannot react in time.

**Consequence.** Player either learns to ignore alerts (they are "always late") or gets penalised for conflicts they could not physically resolve.

**Mitigation.** Advisory tier fires at 8 nm horizontal, well outside the Critical band. Trajectory projection can fire Advisory earlier when a pair is on a converging track. Thresholds tunable in `ClearanceConstants.h` for calibration during playtest.

**Trigger signs.** Advisory and Warning alerts firing within the same tick, giving the player no reaction window.

### R15: Data block clutter under load

**Description.** At high aircraft counts (20 or more) the scope's data blocks overlap, leader lines cross, and the picture becomes unreadable.

**Consequence.** Player cannot read the picture and makes decisions on incomplete information.

**Mitigation.** Data block auto-avoidance across 18 candidate slots per symbol. Declutter algorithm fans out symbols within 12 pixels of each other so labels stay paired. Range labels around the perimeter rather than on the scope itself.

**Trigger signs.** Playtest complaints of "I can't read the callsigns" at high traffic.

## Post-MVP integration risks

Risks tied to scope that is deliberately post-MVP. These are listed here so mitigations are planned before the work begins, not so the work is committed to as part of MVP.

| ID | Risk | Likelihood | Impact | Severity |
|---|---|---|---|---|
| R16 | Server-authority race conditions | M | H | High |
| R17 | Federation wire format compliance drift | M | H | High |
| R18 | Peer ownership conflicts | M | M | Medium |
| R19 | Model-based subsystem integration coupling | L | M | Low |
| R20 | Sensor layer decoupled from airspace ownership | L | H | Medium |

### R16: Server-authority race conditions

**Description.** With multiple peers, an RPC from one client races an RPC from another, and the resulting state depends on delivery order.

**Mitigation.** Simulation is server-authoritative. All mutations pass through server-side RPCs (`Server_Inject*`) on the operator player controller. Clients see the replicated result rather than the input. Deterministic tie-breaks (for example, in TCAS RA climber/descender selection when altitudes match) resolve any residual ambiguity.

### R17: Federation wire format compliance drift

**Description.** DIS PDU codecs drift from IEEE 1278.1 sizes or offsets and stop being parseable by peer simulators or by Wireshark's dissector.

**Mitigation.** Automation tests cover fixed sizes, offsets, padding, round-trip, and malformed rejection for every PDU type in scope. Wireshark manual verification procedure captured in `V_AND_V_PLAN.md` MP-01.

### R18: Peer ownership conflicts

**Description.** Two federates both attempt to modify the same aircraft, or a peer federate's aircraft is accidentally treated as local.

**Mitigation.** `bIsExternal = true` flag on all federated aircraft. Instruction validator rejects mutations on external aircraft. Site ID chips visible on scope so the operator sees the origin.

### R19: Model-based subsystem integration coupling

**Description.** Simulink autopilot or radar signal processor produces outputs the sim cannot consume cleanly, or requires a shared global that breaks per-instance ownership.

**Mitigation.** Reusable-function packaging in the Simulink codegen so every aircraft or radar site owns its own model instance. Wrapper struct owns the per-instance state. Analytic fallback path preserved for A/B testing.

### R20: Sensor layer decoupled from airspace ownership

**Description.** Radar sites drift into holding their own aircraft state rather than reading from the Airspace Manager, once the sensor layer is added.

**Mitigation.** Radar sites read through the Airspace Manager each scan cycle and produce `FRadarTrack` values as the only sensor-side state. Airspace state is never mutated from a radar site.

## Production process risks

Risks tied to how the project is developed rather than what it does.

| ID | Risk | Likelihood | Impact | Severity |
|---|---|---|---|---|
| R21 | Documentation drift from code | H | M | High |
| R22 | Test coverage gaps in critical paths | M | H | High |
| R23 | Save and replay data integrity | M | M | Medium |
| R24 | Solo development bus factor | H | L | Medium |
| R25 | Uncommitted work lost between sessions | L | H | Medium |

### R21: Documentation drift from code

**Description.** Systems Design, C++ Scaffold, and other reference docs drift from the shipped code as the codebase evolves.

**Mitigation.** Docs live with the code in the same repo. A "code wins" statement at the top of every design document reminds a reader that the code is authoritative when they disagree.

### R22: Test coverage gaps in critical paths

**Description.** A critical path (DIS wire format, wake matrix, TCAS RA) is not covered by automation tests and drifts silently.

**Mitigation.** Every requirement in `REQUIREMENTS.md` maps to at least one automation test. Coverage summary in the requirements doc flags any category with a low ratio.

### R23: Save and replay data integrity

**Description.** Session recorder captures partial state, checkpoint save omits a field, or replay poses the world to a subtly wrong snapshot.

**Mitigation.** Recorder snapshots and checkpoint payloads are `USTRUCT`s so field additions are compile-time visible. Automation tests round-trip a representative snapshot and assert field-for-field equality.

### R24: Solo development bus factor

**Description.** All architectural decisions and shipping knowledge sit with one developer.

**Mitigation.** Design decisions committed to documentation as they are made. Docs describe why (not just what). Git history preserves the sequence of choices.

### R25: Uncommitted work lost between sessions

**Description.** Local uncommitted changes lost to a disk failure or accidental deletion between sessions.

**Mitigation.** Work committed and pushed at natural breakpoints. Git remote hosted off-machine.

## Mitigation summary

Every risk in this register maps to at least one active mitigation. Mitigations fall into five categories:

| Category | Applied to | How it works |
|---|---|---|
| Class boundaries | R1, R5, R12, R20 | Enforce ownership and read/write direction through public API surface |
| Enumerated failure modes | R2, R11, R17 | Every rejection or failure has a named enum value with a defined UI or wire response |
| Automation tests | R2, R7, R8, R17, R22, R23 | Assert invariants that would otherwise drift silently |
| Playtest calibration | R6, R9, R14, R15 | Numeric thresholds tuned against reference behaviour during MVP |
| Fallback paths | R13, R19 | Text console fallback for voice; analytic fallback for Simulink |

Where a risk has more than one mitigation, both apply. The register is not exhaustive; new risks discovered during implementation are added with the next available ID.

## Change log

| Date | Change |
|---|---|
| Pre-production | Initial register drafted with R1 through R25 across five categories. |
| Post-shipping (retrospective note) | Register preserved as a planning artefact. Verification evidence for many risks is now captured in `Docs/REQUIREMENTS.md` and `Docs/V_AND_V_PLAN.md`. |

## References

Gregory, J. (2014) *Game Engine Architecture*, 2nd ed. CRC Press.

ICAO (2016) *Procedures for Air Navigation Services: Air Traffic Management*, Doc 4444, 16th ed. International Civil Aviation Organization.

IEEE (2012) *IEEE Standard for Distributed Interactive Simulation: Application Protocols* (IEEE Std 1278.1-2012). IEEE Standards Association.

Salen, K. and Zimmerman, E. (2003) *Rules of Play: Game Design Fundamentals*. MIT Press.
