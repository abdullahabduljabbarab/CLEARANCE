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

This is a **planning** document. Where reality has since diverged from the mitigations proposed here, the shipped design and verification documents (`Docs/Design/`, `Docs/Verification/Requirements.md`, `Docs/Verification/V_AND_V_PLAN.md`) take precedence. The register is preserved to keep the design record honest: what was anticipated, what mitigations were planned, and where the eventual production build actually landed.

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

| ID | Risk | L | I | Sev | Description | Consequence | Mitigation | Trigger signs |
|---|---|---|---|---|---|---|---|---|
| R1 | Ownership boundary drift | H | H | Critical | A system other than the Airspace Manager starts holding its own copy of aircraft state, as a "cache" or a convenience. | Conflict detection reads stale data, radar renders positions the validator does not see, cognitive fidelity collapses. Debugging becomes very hard. | Airspace Manager's aircraft map stays private. Every dependent system reads through a getter and writes through the one sanctioned request path. Reviewed on every change that touches aircraft state. | A new `TMap<FName, FAircraftState>` outside the Airspace Manager. Aircraft position flickering between two values in a frame. |
| R2 | Instruction validation gaps | M | H | High | The validator misses a class of physically impossible instructions (altitude above service ceiling, speed below stall, bank outside envelope) and the behaviour system executes them. | Aircraft enter invalid states; the sim loses cognitive fidelity because the player can command impossible manoeuvres. | Validator is stateless. Every rejection is a named `EInstructionResult` value. Unit tests cover the envelope for every wake category and the military profile. Rejections produce a spoken refusal. | Aircraft accepting a heading that produces bank beyond category limits. Aircraft climbing past service ceiling. Any `Rejected_PhysicallyImpossible` false-negative. |
| R3 | Delegate binding brittleness | M | M | Medium | The Simulation Controller misses a delegate binding at session start. Systems appear to work in isolation but events silently drop between them. | Silent failures. Scoring misses conflicts; conflict detection never sees registrations; radar sites paint an empty sector. | All bindings in one `BindDelegates` function called from `BeginPlay`. Every listener registered on a single line at the same indent so a missing entry is visually obvious. Automation tests exercise each event path end-to-end. | A system that "does nothing" during play despite compiling and initialising cleanly. |
| R4 | Scope creep before core is stable | H | M | High | Federation, radar sites, electronic warfare, or scenarios are started before the five core systems are proven, on the assumption they "should be easy to add later". | Large surface area with no testable core. MVP slips indefinitely. Refactoring the core becomes painful because a large surface area is built against a shaky foundation. | MVP boundary in `Docs/Planning/MVP.md` respected. Post-MVP work begins only after the five core systems pass their success criteria. | Federation or sensor code in the source tree before the Conflict Detector is testable. Backlog of "quick tweaks" queued against the core. |
| R5 | Tick order violation | L | H | Medium | A system is added that ticks independently rather than being called by the Simulation Controller in the fixed order. | Race conditions between the new system and the rest of the pipeline. Downstream systems read stale data. | Direct actor Tick participation restricted to the approved runtime list: Simulation Controller, Airspace Manager, Spawner, and RadarSite scan scheduling only. All simulation logic still runs through the Simulation Controller's authoritative order. | A new UObject with its own `Tick`. A stateful class in a subsystem folder with independent tick behaviour. |

## Simulation fidelity risks

Risks tied to the sim producing behaviour a controller would find plausible.

| ID | Risk | L | I | Sev | Description | Consequence | Mitigation | Trigger signs |
|---|---|---|---|---|---|---|---|---|
| R6 | Flight dynamics feel wrong | M | M | Medium | Aircraft turn too slowly or too quickly, climb rates feel unrealistic, or aircraft appear to fly like arcade tokens. | Cognitive fidelity collapses. Player treats the sim as a game rather than an ATC exercise. | Per-category envelopes drawn from representative real aircraft (172S, 737-800, 777-300ER, A380-800). Turn rates from coordinated-turn kinematics. Climb rates density-adjusted via simplified ISA. Playtest against reference recordings during MVP. | A 737 turning like a fighter. A Heavy climbing at Light rates. "Feels arcade-y" feedback. |
| R7 | Wake matrix threshold errors | L | H | Medium | Wake separation constants transposed, mislabelled, or off by a factor of two. | Wake advisories fire at the wrong distance. Player sees false positives (loses trust) or misses real violations (unsafe). | Wake matrix values pinned to `ClearanceConstants.h`. Automation tests assert every pair-category threshold against the published wake matrix values. Monotonic ordering invariants tested separately. | Wake advisory between same-category aircraft at 8 nm (minimum is 3 nm). Miss on Light-behind-Heavy at 5 nm (matrix requires 6 nm). |
| R8 | Envelope enforcement gaps | M | H | High | An aircraft is allowed to fly outside its physical envelope: below stall, above service ceiling, or in a category-incompatible state. | Aircraft enter physically impossible states; player instructions produce nonsense results. | Enforcement in three places: validator rejects upfront, behaviour clamps on every step, Airspace Manager's `ClampStateValues` as final safety net. Unit tests cover the envelope corners for every category. | Aircraft reported at negative altitude, above service ceiling, or below stall speed. |
| R9 | Conflict projection false positives | M | M | Medium | Forward trajectory projection produces false advisories because it does not account for the player's imminent action. | Alert fatigue sets in; real advisories get ignored because the player has learned to distrust the projection. | Projection window kept short (60 s default) and tunable. Advisories clear the tick after the projected conflict is no longer projected. Advisory text explains "projected in N seconds". | Player reports "alert fires and clears itself two seconds later" as normal behaviour. |
| R10 | Wind drift not applied consistently | L | M | Low | Wind drift applied to some aircraft (say, on approach) but not others (say, cruising level). | Aircraft on the same heading drift at different ground tracks depending on phase, producing surprising radar rendering. | Wind drift is applied in one place (behaviour's position-step) that runs for every aircraft every tick regardless of phase. | Two aircraft on the same heading with divergent ground tracks under identical wind. |

## Player-facing risks

Risks tied to how the sim presents itself to the operator.

| ID | Risk | L | I | Sev | Description | Consequence | Mitigation | Trigger signs |
|---|---|---|---|---|---|---|---|---|
| R11 | Instruction rejection is opaque | H | M | High | An instruction is rejected but the player does not learn why. No pilot readback, no HUD reason, no transcript entry. | Player retries the same failed instruction, thinks the sim is unresponsive, or generalises the wrong lesson from the rejection. | Every rejection produces a spoken refusal with the reason. Transcript logs the refusal role-coloured against the pilot. Instruction result event carries the reason for downstream UI to react to. | "I told it to descend to five thousand and nothing happened." |
| R12 | Radar renders desynced from state | L | H | Medium | The radar display shows an aircraft position that differs from what the Conflict Detector or Validator sees, even briefly. | Player issues instructions against the visible position, sim resolves against the actual position, behaviour reads as counterintuitive. | Radar reads through the Airspace Manager via `BlueprintCallable` getters. No caching in the presentation layer. Read pass runs once per frame from the same snapshot every other system used. | An aircraft symbol lagging its actual movement across the scope by more than one frame at 60 Hz. |
| R13 | Voice input unreliability | H | M | High | Voice input fails to transcribe reliably, or fails on packaged builds where the TTS or STT server is not present. | Player falls back to console commands (cognitive-fidelity loss) or the packaged build ships with a broken feature. | Voice input remains optional for MVP, with text console entry always available as the reliable fallback. Any local STT or TTS support must fail visibly rather than silently. | Recognition rate below acceptable threshold on the developer's own microphone in quiet conditions. Packaged build with a voice feature that fails silently. |
| R14 | Alert timing too late for reaction | M | H | High | Conflict alerts fire so close to the actual violation that the player cannot react in time. | Player learns to ignore alerts ("always late") or gets penalised for conflicts they could not physically resolve. | Advisory tier fires at 8 nm horizontal, well outside Critical. Trajectory projection can fire Advisory earlier on converging tracks. Thresholds tunable in `ClearanceConstants.h` for calibration during playtest. | Advisory and Warning firing within the same tick, giving the player no reaction window. |
| R15 | Data block clutter under load | M | M | Medium | At high aircraft counts (twenty or more) data blocks overlap, leader lines cross, and the picture becomes unreadable. | Player cannot read the picture and makes decisions on incomplete information. | Data block auto-avoidance across 18 candidate slots per symbol. Declutter fans out symbols within 12 pixels so labels stay paired. Range labels around the perimeter rather than on the scope. | Playtest complaints of "I can't read the callsigns" at high traffic. |

## Post-MVP integration risks

Risks tied to scope that is deliberately post-MVP. Mitigations are planned before the work begins.

| ID | Risk | L | I | Sev | Description | Consequence | Mitigation | Trigger signs |
|---|---|---|---|---|---|---|---|---|
| R16 | Server-authority race conditions | M | H | High | With multiple peers, an RPC from one client races an RPC from another, and the resulting state depends on delivery order. | Non-deterministic state; peers see divergent aircraft positions. | Server-authoritative. All mutations through `Server_Inject*` RPCs on the operator player controller. Clients see the replicated result. Deterministic tie-breaks (TCAS RA climber/descender when altitudes match) resolve residual ambiguity. | Peers disagreeing about the outcome of a simultaneous action. |
| R17 | Federation wire format compliance drift | M | H | High | DIS PDU codecs drift from IEEE 1278.1 sizes or offsets and stop being parseable by peer simulators or by Wireshark's dissector. | Federation with peer sims silently breaks; portfolio interoperability claim fails. | Automation tests cover fixed sizes, offsets, padding, round-trip, and malformed rejection for every PDU type in scope. Wireshark manual verification procedure `Docs/Verification/V_AND_V_PLAN.md` MP-01. | Wireshark flagging "Malformed Packet" on any CLEARANCE-emitted PDU. |
| R18 | Peer ownership conflicts | M | M | Medium | Two federates both attempt to modify the same aircraft, or a peer federate's aircraft is treated as local. | Peer aircraft mutated on the wrong federate; ownership rules violated. | `bIsExternal = true` on all federated aircraft. Validator rejects mutations on external aircraft. Site ID chips visible on scope so the operator sees the origin. | Instruction to a peer-owned aircraft is accepted rather than rejected. |
| R19 | Model-based subsystem integration coupling | L | M | Low | Simulink autopilot or radar signal processor produces outputs the sim cannot consume, or needs a shared global that breaks per-instance ownership. | Model-based subsystem cannot be swapped in; A/B testing impossible. | Reusable-function packaging in the Simulink codegen so every aircraft or radar site owns its own model instance. Wrapper struct owns per-instance state. Analytic fallback path preserved. | Any shared global in the generated C. |
| R20 | Sensor layer decoupled from airspace ownership | L | H | Medium | Radar sites drift into holding their own aircraft state rather than reading from the Airspace Manager. | Sensor picture disagrees with authoritative state; conflict analysis becomes unreliable. | Radar sites read through the Airspace Manager each scan and produce `FRadarTrack` as the only sensor-side state. Airspace state is never mutated from a radar site. | Radar site with a `TMap<FName, FAircraftState>` field. |

## Production process risks

Risks tied to how the project is developed rather than what it does.

| ID | Risk | L | I | Sev | Description | Consequence | Mitigation | Trigger signs |
|---|---|---|---|---|---|---|---|---|
| R21 | Documentation drift from code | H | M | High | Systems Design, C++ Scaffold, and other reference docs drift from the shipped code as the codebase evolves. | Docs mislead readers. Portfolio claims stop matching the reality of the repo. | Docs live with the code in the same repo. A "code wins" statement at the top of every design document reminds the reader that the code is authoritative on disagreement. | A doc claim that a grep of the source falsifies in under a minute. |
| R22 | Test coverage gaps in critical paths | M | H | High | A critical path (DIS wire format, wake matrix, TCAS RA) is not covered by automation tests or a documented manual procedure and drifts silently. | Regression ships without being noticed until a manual playtest catches it. | Every requirement in `Docs/Verification/Requirements.md` maps to either an automation test or a named manual verification procedure. Coverage summary flags any category with a low ratio. | A requirement with neither a covering test nor a manual procedure in the traceability table. |
| R23 | Save and replay data integrity | M | M | Medium | Session recorder captures partial state, checkpoint save omits a field, or replay poses the world to a subtly wrong snapshot. | Instructor workflow becomes unreliable; trainee re-attempts see stale state from a previous run. | Recorder snapshots and checkpoint payloads are `USTRUCT`s so field additions are compile-time visible. Automation tests round-trip a representative snapshot and assert field-for-field equality. | Replay showing an aircraft in a position it did not occupy at that timestamp. |
| R24 | Solo development bus factor | H | L | Medium | All architectural decisions and shipping knowledge sit with one developer. | Project becomes hard to hand over. Rediscovery cost is high after time away. | Design decisions committed to documentation as they are made. Docs describe why, not just what. Git history preserves the sequence of choices. | A design decision made but not documented anywhere. |
| R25 | Uncommitted work lost between sessions | L | H | Medium | Local uncommitted changes lost to disk failure or accidental deletion between sessions. | Rework required. Worst case, entire feature has to be rebuilt from memory. | Work committed and pushed at natural breakpoints. Git remote hosted off-machine. | A session ending with more than a couple of hours of uncommitted work. |

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
| Post-shipping (retrospective note) | Register preserved as a planning artefact. Verification evidence for many risks is now captured in `Docs/Verification/Requirements.md` and `Docs/Verification/V_AND_V_PLAN.md`. |

## References

Gregory, J. (2014) *Game Engine Architecture*, 2nd ed. CRC Press.

ICAO (2016) *Procedures for Air Navigation Services: Air Traffic Management*, Doc 4444, 16th ed. International Civil Aviation Organization.

IEEE (2012) *IEEE Standard for Distributed Interactive Simulation: Application Protocols* (IEEE Std 1278.1-2012). IEEE Standards Association.

Salen, K. and Zimmerman, E. (2003) *Rules of Play: Game Design Fundamentals*. MIT Press.
