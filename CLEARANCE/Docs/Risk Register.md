**CLEARANCE**

**Risk Register (MVP)**

This risk register identifies the primary technical, design, scope, and delivery risks for the CLEARANCE MVP. It is based on the project’s systems-driven architecture, where Airspace Management acts as the single source of truth, Aircraft Behaviour acts as the sole movement executor, Communication validates and routes instructions, Conflict Detection monitors separation, and Scoring tracks performance and difficulty.

**Rating scale:**

*   **Likelihood**: Low / Medium / High
*   **Impact:**Low / Medium / High
*   **Priority:** Judgement based on combined likelihood and impact

**Risk register**

ID

Risk

Likelihood

Impact

Priority

Mitigation / Response

R1

Airspace Manager becomes a single point of failure because all systems depend on its state authority

Medium

High

High

Keep the Airspace Manager narrow in responsibility, unit test registration/update/query behaviour first, prevent all direct external state writes, and add state sanity checks for invalid altitude, speed, and callsign cases.

R2

Aircraft state becomes desynchronised if any system bypasses the Airspace Manager and writes state directly, breaking the single-source-of-truth model

Medium

High

High

Enforce ownership boundaries in C++ only, expose read-only data to dependent systems, reject invalid updates, and test authority enforcement explicitly in the validation checklist.

R3

Aircraft Behaviour performance drops at higher aircraft counts because all active aircraft are updated every tick

Medium

High

High

Start with a conservative concurrent-aircraft cap, keep movement logic lightweight, profile update cost early, and reduce non-essential per-tick work before increasing traffic density.

R4

Tick-order bugs cause Conflict Detection or Scoring to read half-updated state instead of committed authoritative state

Medium

High

High

Keep the 1–9 simulation sequence centralised inside the Simulation Controller and test that Conflict Detection runs only after Behaviour updates have been committed back to Airspace Management.

R5

Conflict Detection becomes too complex too early, especially with projected conflicts and many aircraft pairs

Medium

Medium

Medium

Implement current-state separation detection first, then layer projected conflict checks afterward; validate thresholds with small deterministic scenarios before scaling traffic.

R6

Player instructions feel unresponsive or unrealistic if aircraft movement is too abrupt, too slow, or inconsistent with the intended ATC cognitive-fidelity experience

Medium

High

High

Tune heading, altitude, and speed response rates early; test gradual behaviour with simple scenarios; prioritise predictability and readability over realism beyond MVP needs.

R7

The simulation becomes visually unreadable or cognitively overwhelming as difficulty increases and more aircraft enter the sector

Medium

Medium

Medium

Ramp spawn rate conservatively, cap simultaneous aircraft, keep radar/data tags readable, and use Scoring-driven difficulty adjustment within strict bounds.

R8

Players do not understand why an instruction failed or why a penalty occurred, reducing trust in the simulation

Medium

High

High

Return explicit instruction results from the Communication System, surface advisory and incident messages clearly in UI, and log penalties through the Scoring system with clear incident reasons.

R9

Scoring and difficulty tuning feel unfair, making the simulation frustrating rather than challenging

Medium

Medium

Medium

Keep scoring rules simple at MVP stage, log incidents clearly, tune point values after scenario testing, and constrain spawn-rate adjustment between defined minimum and maximum values.

R10

Scope creep pushes the project beyond MVP into full ATC realism, advanced procedures, or excessive simulation depth

High

High

High

Freeze MVP scope around the defined gameplay loop: traffic entry, player instruction, aircraft response, conflict management, safe exit/landing, scoring, and difficulty; defer deep procedure simulation and advanced realism features.

R11

Time is lost polishing radar UI and presentation before the core simulation loop is stable

Medium

Medium

Medium

Build Airspace, Behaviour, Comms, Conflict, Scoring, Spawner, and Controller first; use a minimal debug-friendly UI until the end-to-end loop passes core tests.

R12

Important edge cases are missed, such as exited-aircraft instructions, duplicate go-arounds, failed state updates, empty-airspace reads, or maximum-aircraft-cap scenarios

Medium

High

High

Convert all documented edge cases and failure states into mandatory test rows and treat the checklist matrix as a release gate for MVP completion.

R13

Event delegates are not bound correctly, causing UI, scoring, or warnings to fail silently even when simulation logic works

Medium

Medium

Medium

Bind delegates in a single initialisation path in the Simulation Controller, add debug output for key broadcasts, and test OnAircraftRegistered, OnConflictDetected, OnInstructionResult, and OnScoreUpdated events directly.

R14

Debugging becomes difficult if aircraft behaviour appears wrong but the root cause is unclear across multiple interconnected systems

Medium

Medium

Medium

Use the centralised architecture for diagnosis, inspect Airspace state first, log instruction issue/results and incidents, and provide lightweight debug inspection for selected aircraft state.

R15

Reproducing bugs is difficult when traffic is generated dynamically and failure scenarios cannot be repeated consistently

Medium

Medium

Medium

Use deterministic test scenarios alongside procedural traffic, log timestamps and incident details, and keep spawn inputs reproducible wherever possible during testing.

R16

Aircraft performance categories (Light, Medium, Heavy, Super) are not correctly assigned at spawn, causing physics validation to apply wrong limits and producing unrealistic aircraft behaviour

Medium

High

High

Define performance categories as enums tied to aircraft type at spawn, validate category assignment during registration, and test that service ceiling, speed envelope, and bank angle limits apply correctly per category through deterministic test scenarios.

R17

Wake turbulence separation calculations produce false positives or miss real violations because the category-pair separation matrix is incorrectly tuned, undermining the player’s trust in the safety monitoring

Medium

High

High

Implement the wake separation matrix using ICAO Document 4444 reference values, test each category-pair combination explicitly with deterministic scenarios, and validate that advisories trigger at correct distances behind preceding aircraft.

R18

Active runway selection logic incorrectly chooses runway when wind direction is near a crosswind boundary, producing unstable runway changes that disrupt aircraft sequencing

Medium

Medium

Medium

Implement crosswind component calculation with a hysteresis buffer to prevent runway oscillation near boundaries, test runway selection across wind direction sweeps, and log runway change events for debug review.

**Highest-priority risks**

The three most important risks to manage during MVP implementation are

*   Airspace Manager becoming a single point of failure
*   Behaviour System performance and correctness under load
*   Scope creep beyond the defined ATC core loop
*   Aircraft performance category assignment correctness, since incorrect category assignment cascades into physics validation, wake separation calculation, and aircraft behaviour producing unrealistic simulation

These carry the greatest chance of either destabilising the full architecture or preventing the project from reaching a complete and testable MVP within the intended scope.

This risk register accompanies the systems design, technical architecture, test plan, and checklist matrix. The register only explains what could threaten delivery or system integrity, while the test plan and matrix provide the practical validation steps used to reduce those risks during implementation.