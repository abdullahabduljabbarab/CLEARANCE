**CLEARANCE**
**Test and Validation Plan**
**Project:** CLEARANCE
**Type:** First-Person Air Traffic Control Simulation
**Engine:** Unreal Engine 5
**Purpose:** Define how the CLEARANCE MVP will be tested and validated before and during implementation.

**1\. Purpose**
This document defines the test and validation plan for CLEARANCE, a first-person Air Traffic Control simulation built around the interaction of the Airspace Management, Aircraft Behaviour, Communication, Conflict Detection, and Scoring systems. Its purpose is to ensure that each system works correctly in isolation, that the full gameplay loop functions as intended, and that the architecture remains consistent with the single-source-of-truth model established in the design and scaffold documents.
The plan is designed to validate not only whether the simulation functions, but whether it functions **safely, predictably, and according to its defined system boundaries**.

**2\. Test objectives**

**Objective**

**Description**

**1**

**Verify that each core system fulfils its intended responsibility**

**2**

**Verify that communication goes through the correct authoritative pathways**

**3**

**Verify that invalid input and edge cases are handled safely without corrupting state**

**4**

**Verify that the full simulation loop is playable end‑to‑end**

**5**

**Verify that ownership, tick order, and delegate‑based communication remain intact**

**6**

**Verify that physics constraints (performance categories, wake separation, runway selection) operate correctly within the simulation**

The test plan has five core objectives:
• Verify that each core system fulfils its intended responsibility \[1\]\[2\].
• Verify that all system communication routes through the correct authoritative pathways \[1\]\[2\].
• Verify that invalid input, edge cases, and failure states are handled safely without corrupting simulation state \[1\]\[2\].
• Verify that the full simulation loop is playable end-to-end \[1\]\[2\].
• Verify that the project’s technical architecture remains intact during implementation, especially the ownership, tick order, and delegate-based communication model \[2\].
**3\. Test strategy**
Testing will be carried out at three levels:
Test Level
Purpose
Unit testing
Verify isolated logic such as instruction validation, conflict calculation, scoring changes, and movement calculations \[2\]
Integration testing
Verify correct interaction between systems such as Communication → Behaviour → Airspace or Conflict Detection → Scoring \[1\]\[2\]
Scenario / simulation testing
Verify the full gameplay loop under live session conditions \[1\]\[2\]
The testing process will begin with isolated system validation and then move into combined simulation testing so that errors can be identified at the lowest possible level before they affect the wider project \[1\]\[2\].
**4\. Validation principles**
The following principles define what counts as a valid test outcome:
• A system passes validation only if it behaves correctly under both normal and edge-case conditions \[1\]\[2\].
• No test should permit direct bypass of the architecture, especially direct aircraft-state mutation outside the Airspace Manager \[1\]\[2\].
• Read-only systems such as Conflict Detection must never modify aircraft state directly \[2\].
• The Communication System must never execute movement directly and must always validate before routing \[2\].
• Aircraft movement must always be gradual and tick-driven rather than instant \[1\]\[2\].
• A failed test should produce a clear reason for failure, not an ambiguous incorrect state.
**5\. Test environment**
Testing will be performed in an Unreal Engine 5 development build using the C++ scaffold classes defined in the core systems document \[2\]. A dedicated test level should include:
• AClearanceSimulationController \[2\]
• AClearanceAirspaceManager \[2\]
• AClearanceAircraftSpawner \[2\]
• Controller-owned UObjects for Behaviour, Validator, Comms Router, Conflict Detector, and Scoring \[2\]
• A minimal Blueprint UI layer for issuing instructions and viewing radar data \[2\]
Debug logging should be enabled for:
• aircraft registration and deregistration,
• instruction issue and result,
• state updates,
• conflict detection,
• go-around triggers,
• score changes,
• and difficulty adjustments \[2\].
**6\. Airspace Management System tests**
**Target class:**AClearanceAirspaceManager \[2\]
**Purpose:** Validate that the Airspace Manager acts as the single authoritative owner of aircraft state \[1\]\[2\].
**6.1 Registration tests**
**Test 1: Aircraft State Registration**
When an aircraft enters the sector, the Airspace Management System must correctly register it with callsign, position, altitude, speed, heading, and intention/phase data and make that state immediately available to dependent systems \[1\]\[2\].
**Pass criteria:**
• RegisterAircraft returns success for valid aircraft \[2\].
• IsCallsignRegistered returns true \[2\].
• GetAircraftState returns the same state that was registered \[2\].
• OnAircraftRegistered is broadcast once \[2\].
**Test 2: Duplicate Registration Rejection**
Attempting to register an already-registered callsign should fail safely and not overwrite the existing aircraft state \[1\]\[2\].
**6.2 State update tests**
**Test 3: Valid State Update**
A valid state update from Aircraft Behaviour must be accepted and committed as the new authoritative state \[1\]\[2\].
**Pass criteria:**
• RequestStateUpdate returns true \[2\].
• Stored aircraft data is updated correctly.
• OnAircraftStateUpdated is broadcast \[2\].
**Test 4: Invalid Callsign Update Rejection**
If Aircraft Behaviour sends a state update for an unregistered aircraft, the request must be rejected and no state must be created \[1\].
**Test 5: Out-of-Sequence Update Rejection**
If an update older than the currently stored state is received, it must be rejected so that stale data never overwrites newer state \[1\].
**6.3 Boundary and failure tests**
**Test 6: Concurrent Read Stability**
Multiple systems reading state at the same time must not cause corruption or inconsistent returned values \[1\].
**Test 7: Zero-Aircraft Query**
If Airspace Management is queried before any aircraft enter the sector, it should return an empty list safely without errors \[1\].
**Test 8: Safe Failure on Reference Loss**
If a dependent system attempts to query a missing Airspace Manager reference, the request must fail safely and no state should be modified \[1\].

6.4 Environmental state tests

Test 35: Wind Condition Update

When sector wind conditions change, the Airspace Manager must update the stored environment state and broadcast OnRunwayChanged if the new wind requires a different active runway \[2\].

Test 36: Active Runway Selection

The Airspace Manager must select the active runway from available runways based on minimising crosswind component for the current wind direction \[2\].

Pass criteria:

Runway selection produces lowest crosswind component option \[2\]

Selection updates when wind direction changes significantly \[2\]

Test 37: Runway Selection Hysteresis

When wind direction sits near a runway selection boundary, the active runway must not oscillate between options each tick \[2\].
**7\. Aircraft Behaviour System tests**
**Target class:**UClearanceAircraftBehaviour \[2\]
**Purpose:** Validate that Aircraft Behaviour is the sole executor of aircraft movement and that all movement is gradual and realistic \[1\]\[2\].
**7.1 Core movement tests**
**Test 9: Heading Change Execution**
A heading instruction must cause the aircraft to turn gradually at the configured turn rate until the target heading is reached \[2\].
**Pass criteria:**
• Heading changes incrementally, not instantly \[2\].
• Target heading is reached within tolerance.
• Updated heading is pushed to Airspace Management each tick \[1\]\[2\].
**Test 10: Altitude Change Execution**
An altitude instruction must cause the aircraft to climb or descend at the configured vertical rate and level off correctly at the target altitude \[2\].
**Test 11: Speed Change Execution**
A speed instruction must cause gradual acceleration or deceleration until target speed is reached \[2\].
**7.2 Integrity and edge-case tests**
**Test 12: Invalid Movement Prevention**
Aircraft must never enter physically impossible values such as negative altitude or speed below safe thresholds \[1\]\[2\].
**Test 13: Simultaneous Aircraft Independence**
Multiple aircraft executing instructions at the same time must not interfere with each other’s calculations \[1\]\[2\].
**Test 14: Duplicate Go-Around Protection**
If a go-around is already in progress, a duplicate go-around trigger must be ignored \[1\].
**Test 15: Exited Aircraft Instruction Halt**
If an aircraft exits the sector mid-manoeuvre, movement execution must stop and no orphaned updates should continue \[1\].

7.3 Physics validation tests

Test 16: Performance Category Assignment

Aircraft spawning with a defined wake category (Light, Medium, Heavy, Super) must have correct service ceiling, minimum operating speed, maximum operating speed, and maximum climb rate values assigned from the performance lookup \[2\].

Pass criteria:

Aircraft state contains correct WakeCategory value after spawn \[2\]

ServiceCeiling, MinOperatingSpeed, MaxOperatingSpeed, MaxClimbRate match category lookup values \[2\]

Test 17: Bank Angle Limited Turn Rate

A heading change must produce turn rate calculated from bank angle limit per performance category, not a fixed turn rate regardless of aircraft type \[2\].

Pass criteria:

Heavy aircraft turn at lower rate than Medium aircraft for same heading change \[2\]

Bank angle never exceeds category limit during turn execution \[2\]

Test 18: Service Ceiling Enforcement

An altitude instruction at or above the aircraft’s service ceiling must be rejected at the Communication System validation stage \[2\].

Pass criteria:

Instruction rejected with RejectedPhysicallyImpossible \[2\]

Aircraft maintains current altitude \[2\]

Test 19: Climb Rate Per Category

Climb rate during altitude changes must respect the maximum climb rate for the aircraft’s performance category \[2\].

Pass criteria:

Light aircraft climb at 500-1000 ft/min \[2\]

Medium aircraft climb at 1500-2500 ft/min \[2\]

Heavy aircraft climb at 2000-3000 ft/min \[2\]

Test 20: Speed Envelope Enforcement

Speed instructions must be rejected if below minimum operating speed or above maximum operating speed for the aircraft type \[2\].

Pass criteria:

Below-minimum speed instruction rejected with RejectedPhysicallyImpossible \[2\]

Above-maximum speed instruction rejected with RejectedPhysicallyImpossible \[2\]

Within-envelope speed instructions accepted normally \[2\]
**8\. Communication System tests**
**Target classes:**UClearanceCommsRouter, UClearanceInstructionValidator \[2\]
**Purpose:** Validate instruction input, validation, rejection, and routing \[1\]\[2\].
**8.1 Validation tests**
**Test 16: Valid Instruction Acceptance**
A valid instruction should be accepted, routed to the correct Aircraft Behaviour object, and reported as accepted through OnInstructionResult \[2\].
**Test 17: Invalid Callsign Rejection**
An instruction issued to an unregistered callsign must return RejectedInvalidCallsign \[2\].
**Test 18: Physically Impossible Instruction Rejection**
An instruction requesting an impossible altitude, heading, or speed must be rejected by the validator with RejectedPhysicallyImpossible \[2\].
**Test 19: Exited Aircraft Rejection**
An instruction issued after an aircraft has already exited the sector must return RejectedAircraftExited \[2\].
**8.2 Routing tests**
**Test 20: Behaviour Routing**
A valid instruction must be routed only to the matching aircraft behaviour object and to no other aircraft \[2\].
**Test 21: Advisory Broadcast**
When the Communication System receives an advisory conflict event, it must broadcast OnAdvisoryWarning correctly for the UI layer \[2\].
**Test 22: Go-Around Routing**
When OnGoAroundRequired is received, the Comms Router must route a go-around through the proper instruction pathway rather than directly mutating aircraft state \[2\].
**9\. Conflict Detection System tests**
**Target class:**UClearanceConflictDetector \[2\]
**Purpose:** Validate read-only separation monitoring, alert generation, and go-around triggers \[1\]\[2\].
**9.1 Detection tests**
**Test 23: No-Conflict Case**
Aircraft safely separated horizontally and vertically should produce no conflict event \[2\].
**Test 24: Advisory Threshold Detection**
Aircraft inside the advisory threshold but above critical thresholds must generate an advisory-level conflict \[2\].
**Test 25: Warning Threshold Detection**
Aircraft closer together must escalate to warning as defined by configured thresholds \[2\].
**Test 26: Critical Threshold Detection**
Aircraft inside the critical threshold must produce a critical conflict event \[2\].
**9.2 Projection and monitoring tests**
**Test 27: Projected Conflict Detection**
Aircraft that are not yet in conflict but will enter conflict within the configured look-ahead window must be detected by projected conflict logic \[2\].
**Test 28: Conflict Resolution Detection**
When a previously active conflict is resolved, OnConflictResolved must broadcast correctly \[2\].
**Test 29: Go-Around Trigger**
If a landing aircraft is involved in a critical conflict on approach, OnGoAroundRequired must broadcast for that aircraft \[2\].
**Test 30: Read-Only Guarantee**
Conflict Detection must never modify input aircraft state while calculating separation \[2\].

9.3 Wake turbulence tests

Test 31: Wake Separation Detection

When a following aircraft is closer to a preceding heavier aircraft than the wake separation matrix allows, an advisory must be generated \[2\].

Pass criteria:

Light following Heavy under 6nm generates wake advisory \[2\]

Medium following Heavy under 5nm generates wake advisory \[2\]

Light following Medium under 5nm generates wake advisory \[2\]

Heavy following Heavy under 4nm generates wake advisory \[2\]

Test 32: Wake Advisory Clearance

When the following aircraft increases separation beyond the wake category minimum, the wake advisory must clear on the next monitoring cycle \[2\].

Test 33: Same Category No Wake Advisory

Same-weight aircraft pairs with sufficient distance separation must not generate wake advisories \[2\].

Test 34: Wake Read-Only Guarantee

Wake turbulence monitoring must operate as read-only on aircraft state, never modifying the aircraft pair being monitored \[2\].
**10\. Scoring and Assessment System tests**
**Target class:**UClearanceScoring \[2\]
**Purpose:** Validate scoring, incident logging, and difficulty adjustment \[1\]\[2\].
**10.1 Logging tests**
**Test 31: Incident Logging**
When a separation loss or unresolved exit occurs, an FIncidentRecord must be added to the session log \[2\].
**Test 32: Landing and Departure Logging**
Successful landing and departure events must update totals and score correctly \[2\].
**Test 33: Go-Around Logging**
When a go-around occurs, it must be logged and the correct penalty applied \[2\].
**10.2 Score and difficulty tests**
**Test 34: Score Recalculation**
Score must update correctly after each logged event using the configured reward and penalty values \[2\].
**Test 35: Difficulty Adjustment Broadcast**
When score or efficiency changes require spawn pacing to change, OnDifficultyAdjusted must broadcast the new spawn rate \[2\].
**Test 36: Reset Session**
ResetSession must clear incident logs, reset totals, reset score, and restore base difficulty values \[2\].
**11\. Aircraft Spawner tests**
**Target class:**AClearanceAircraftSpawner \[2\]
**Purpose:** Validate aircraft entry into the sector and difficulty-driven spawn behaviour \[2\].
**Test 37: Manual Spawn Success**
Valid FAircraftSpawnData must produce a successful aircraft registration when aircraft count is below the cap \[2\].
**Test 38: Max Aircraft Cap**
If the sector is already at max concurrent aircraft, further spawn attempts must fail safely and not create invalid aircraft state \[1\]\[2\].
**Test 39: Spawn Rate Update**
When Scoring broadcasts a difficulty change, the Spawner must update its current spawn rate correctly \[2\].
**Test 40: Auto-Spawn Timing**
With auto-spawn enabled, aircraft must spawn according to the active spawn interval and not faster than allowed \[2\].
**12\. Simulation Controller tests**
**Target class:**AClearanceSimulationController \[2\]
**Purpose:** Validate orchestration, tick order, and lifecycle control \[2\].
**12.1 Session control tests**
**Test 41: Start Session**
StartSession must initialise systems, bind delegates, and enable active simulation \[2\].
**Test 42: Pause and Resume Session**
PauseSession must halt simulation progression without corrupting current state, and ResumeSession must continue normally \[2\].
**Test 43: End Session**
EndSession must stop simulation, clear airspace, and allow scoring finalisation \[1\]\[2\].
**12.2 Tick pipeline tests**
**Test 44: Authoritative Tick Order**
The Simulation Controller must execute its update pipeline in the documented order:
1\. Spawner check,
2\. Pull airspace state,
3\. Update behaviours,
4\. Commit updates,
5\. Conflict check,
6\. Process go-arounds,
7\. Score logging,
8\. Difficulty update,
9\. UI notification \[2\].
A test log should confirm this order each frame in the development build.
**13\. End-to-end scenario tests**
These tests validate the complete gameplay loop.
**Scenario 1: Clean aircraft handling**
• Aircraft enters sector \[1\].
• Player issues valid instructions \[1\]\[2\].
• Aircraft responds gradually \[1\]\[2\].
• No conflict occurs.
• Aircraft exits or lands successfully.
• Score updates positively \[2\].
**Scenario 2: Missed conflict**
• Two aircraft enter on converging paths \[1\].
• Player fails to resolve separation.
• Conflict Detection escalates and records a conflict \[1\]\[2\].
• Scoring logs separation loss \[2\].
**Scenario 3: Go-around event**
• Aircraft on approach enters conflict state \[1\]\[2\].
• Conflict Detection triggers OnGoAroundRequired \[2\].
• Comms Router routes go-around \[2\].
• Aircraft Behaviour executes go-around \[2\].
• Scoring logs go-around \[2\].
**Scenario 4: Late instruction after exit**
• Aircraft exits the sector \[1\]\[2\].
• Player issues instruction after exit.
• Communication rejects it safely with no state corruption \[1\]\[2\].

Scenario 5: Wake turbulence sequencing

Heavy aircraft enters sector on approach \[1\]

Medium aircraft enters sector behind Heavy \[1\]

Player vectors Medium too close behind Heavy \[2\]

Wake turbulence advisory triggers \[2\]

Player adjusts Medium aircraft separation \[2\]

Advisory clears once separation restored \[2\]

Scenario 6: Runway change mid-session

Aircraft active in sector under initial runway selection \[1\]

Wind direction shifts requiring different active runway \[2\]

OnRunwayChanged broadcasts \[2\]

Spawner uses new runway for subsequent aircraft \[2\]

Active aircraft complete current operations on previous runway clearance \[2\]
**14\. Edge-case and failure-state validation**
The following failure scenarios must be explicitly tested because they appear in the systems design document:
• Invalid callsign in a state update \[1\].
• Simultaneous update attempts on one aircraft \[1\].
• Out-of-sequence state updates \[1\].
• Conflict Detection reading during an update \[1\].
• Airspace queried before any aircraft are active \[1\].
• State update failure mid-operation \[1\].
• Unavailable Airspace reference \[1\].
• Instruction sent to aircraft no longer registered \[1\].
• Peak aircraft count exceeded \[1\].
• Tick-rate spike causing delayed state update \[1\].
• Duplicate go-around trigger \[1\].
• Physically impossible movement state \[1\]\[2\].

Aircraft spawning with invalid or missing wake category assignment

Wake advisory generated for aircraft pair that exits sector before resolution

Wind direction shift during active aircraft approach causing runway change

Performance category enforcement preventing physically impossible instructions
Each of these must fail safely, preserve valid aircraft state, and avoid producing partial or corrupt data \[1\]\[2\].
**15\. Pass criteria**
The CLEARANCE MVP passes validation when all of the following are true:
• Aircraft can be registered, updated, queried, and removed reliably through Airspace Management \[1\]\[2\].
• Aircraft Behaviour executes heading, altitude, speed, and go-around logic gradually and correctly \[1\]\[2\].
• Communication validates and routes instructions without directly executing movement \[2\].
• Conflict Detection reads authoritative state and produces correct alerts and triggers \[1\]\[2\].
• Scoring logs incidents and updates difficulty correctly \[2\].
• Spawner obeys aircraft caps and difficulty-driven spawn rate \[1\]\[2\].
• Simulation Controller runs the correct tick order every frame \[2\].
• No system bypasses the architecture or breaks ownership boundaries \[1\]\[2\].
• The full gameplay loop is playable from sector entry to session outcome \[1\]\[2\].

Aircraft respect performance category limits including service ceiling, speed envelope, and climb rate \[2\]

Wake turbulence separation is monitored and advisories trigger correctly for category-pair combinations \[2\]

Active runway selection responds to wind direction changes with hysteresis preventing oscillation \[2\]
**16\. Definition of done**
The test plan is complete when:
• every core system has isolated tests,
• all integration paths have been validated,
• all listed failure states have been exercised,
• and at least one full session scenario has been run successfully from start to finish \[1\]\[2\].
The project is ready to move forward once the MVP passes both system-level and end-to-end validation.

\## Test Checklist Matrix (Compact)

This compact checklist matrix is intended to accompany the full Test and Validation Plan. It provides a practical implementation-tracking sheet for the MVP systems and scenarios defined in the CLEARANCE design and scaffold documents \[1\]\[2\].

| ID | System | Done? |

| A1 | Airspace: Register valid aircraft | | A2 | Airspace: Reject duplicate callsign | | A3 | Airspace: Deregister aircraft cleanly | | A4 | Airspace: Accept valid state update | | A5 | Airspace: Reject invalid callsign update | | A6 | Airspace: Clamp invalid altitude/speed | |

A7 Airspace: Update wind conditions correctly

A8 Airspace: Select active runway from wind

A9 Airspace: Apply runway selection hysteresis

| B1 | Behaviour: Execute heading change gradually | | B2 | Behaviour: Execute altitude change gradually | | B3 | Behaviour: Execute speed change gradually | | B4 | Behaviour: Queue/reject conflicting axis instructions | | B5 | Behaviour: Execute go-around correctly | | B6 | Behaviour: Stop updates after sector exit | |

B7 | Behaviour: Apply bank angle limit per category | |

B8 Behaviour: Respect climb rate per category

B9 Behaviour: Reject altitude above service ceiling

B10 Behaviour: Reject speed outside operating envelope

| C1 | Comms: Accept valid instruction | | C2 | Comms: Reject unknown callsign | | C3 | Comms: Reject physically impossible instruction | | C4 | Comms: Reject exited aircraft instruction | | C5 | Comms: Show advisory warning | | C6 | Comms: Route go-around without direct mutation | |

C7 Comms: Reject service ceiling violation

C8 Comms: Reject speed envelope violation

| D1 | Conflict: Detect advisory alert | | D2 | Conflict: Detect warning alert | | D3 | Conflict: Detect critical alert | | D4 | Conflict: Detect projected conflict | | D5 | Conflict: Clear resolved conflict | | D6 | Conflict: Preserve read-only monitoring | |

D7 Conflict: Detect wake turbulence advisory

D8 Conflict: Clear wake advisory on separation increase

D9 Conflict: Apply correct wake matrix by category pair

| E1 | Scoring: Log separation incident correctly | | E2 | Scoring: Award landing points once | | E3 | Scoring: Award departure points once | | E4 | Scoring: Apply go-around penalty | | E5 | Scoring: Update efficiency rating correctly | | E6 | Scoring: Adjust difficulty within spawn bounds | | E7 | Scoring: Reset session state cleanly | | F1 | Spawner: Spawn under aircraft cap | | F2 | Spawner: Reject spawn at cap | | F3 | Spawner: Respect spawn interval | | F4 | Spawner: Respond to difficulty changes | |

F5 Spawner: Assign correct wake category

| G1 | Controller: Start session correctly | | G2 | Controller: Pause and resume cleanly | | G3 | Controller: End session and clear state | | G4 | Controller: Preserve tick/update order | | S1 | Scenario: Single-aircraft clean flow | | S2 | Scenario: Separation violation flow | | S3 | Scenario: Go-around flow | | S4 | Scenario: Late instruction after exit | |

S5 Scenario: Wake turbulence sequencing

S6 Scenario: Runway change mid-session