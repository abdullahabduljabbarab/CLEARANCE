Gameplay Systems Design

**Project: CLEARANCE**

Abdullah Ameed Abduljabbar

Systems Designer

Date: 30th April 2026

# **Project Overview**

This document outlines the core gameplay systems for a first-person Air Traffic Control simulation (CLEARANCE) centred on player-driven airspace management, aircraft sequencing, conflict detection, and cognitive-fidelity training mechanics.

### **The Design emphasises:**

*   _Systemic interactions between the Airspace Management, Aircraft Behaviour, Communication, Conflict Detection, and Scoring systems_
*   _A simulation that produces genuine cognitive and emotional responses consistent with real ATC decision-making under pressure_
*   _Physics-based aircraft behaviour including realistic flight dynamics, wake turbulence separation, and atmospheric effects on operational decisions_

### **The Core Gameplay Loop:**

1.  _Aircraft enters the sector at the radar boundary_
2.  _Player reads data tag (callsign, altitude, speed, intention)_
3.  _Player issues instructions via the Communication System (heading, altitude, speed, clearance)_
4.  _Aircraft Behaviour System executes instructions gradually and realistically_
5.  _Conflict Detection System continuously monitors all aircraft for separation violations_
6.  _Player resolves conflicts through heading, altitude, or speed changes, accounting for wake turbulence separation and active runway selection based on wind conditions_
7.  _Aircraft lands, departs, or transits out of sector_
8.  _loop repeats with increasing traffic density_

_Supporting Systems enable and regulate this core loop (Flowchart1)._

This system flow shows how player progression moves from instruction issuance through aircraft response to conflict resolution and outcome assessment, with conflict detection acting as a continuous safety layer over the player’s sector management decisions.

This project adopts a systems-driven design approach, where gameplay emerges from the interaction of multiple interconnected systems rather than isolated mechanics.

A system is described as “_a group of interacting, interrelated, or interdependent elements forming a complex whole.” (Salen & Zimmerman, 2003, p.57)_

This perspective underpins the architecture of CLEARANCE, where Airspace Management, Aircraft Behaviour, Communication, Conflict Detection, and Scoring systems operate as interdependent components within a unified simulation loop.

# **Systems List**

**System**

**Purpose**

**Dependencies**

Airspace Management System

Owns and tracks all aircraft state as the single source of truth

Aircraft Behaviour System, Conflict Detection System, Communication System

Aircraft Behaviour System

Controls how aircraft move, respond to instructions, and execute flight paths

Airspace Management System

Communication System

Validates and routes player-issued instructions to aircraft

Airspace Management System, Aircraft Behaviour System

Conflict Detection System

Monitors all aircraft for separation violations and generates warnings

Airspace Management System

Scoring & Assessment System

Tracks performance, logs incidents, and scales difficulty

Airspace Management System, Conflict Detection System

# **Systems**

### **AIR SPACE MANAGEMENT SYSTEM**

1.  **Description:**

The Airspace Management System is responsible for storing, updating, and providing access to the state of every aircraft operating within the controlled sector. It receives position and intent data when aircraft enter the sector, accepts state updates from the Aircraft Behaviour System as instructions are executed, and responds to read requests from the Conflict Detection System, Communication System, and Scoring System. The system will display aircraft data to the radar screen in real time. The sector will begin each session with zero active aircraft.

1.  **System Purpose:**

The Airspace Management System will centralise all aircraft state data for the simulation, allowing the Aircraft Behaviour System, Conflict Detection System, Communication System, and Scoring System to communicate through a single authoritative source rather than managing aircraft state independently.

1.  **Design Rationale:**

A centralised authority will be established to prevent inconsistent aircraft state across systems, avoiding independent modifications that could cause:

*   _Conflicting position data between the radar display and the Conflict Detection System_
*   _Instructions being issued to aircraft with stale or incorrect state_
*   _Desynchronisation between what the player sees and what the simulation is calculating_

A centralised authority will introduce:

*   _A single source of truth for all aircraft position, altitude, speed, heading, and intent_
*   _Validated state updates before any system modifies aircraft data_
*   _Allows conflict calculations without directly mutating aircraft state_
*   _Reduction in edge cases where two systems write conflicting values simultaneously_
*   _Enables session replay and post-session review since all state changes route through a single auditable system, supporting training debrief and performance analysis_

**Alternatives Considered:**

**Approach**

**Description**

**Pros**

**Cons**

Distributed Aircraft State Handling

Each system (Conflict Detection, Communication, Scoring) maintains its own copy of aircraft position and state independently

*   Faster local read operations within each system
*   Reduced dependency on a central system
*   Simpler early prototyping of individual systems
*   Lower upfront architectural planning
*   Fewer inter-system calls during initial implementation

*   Risk of aircraft position desynchronisation between systems
*   Conflict Detection could calculate separation using outdated position data
*   Radar display could show position that differs from what Conflict Detection is using
*   No single source of truth for aircraft state
*   Validation logic for instruction compliance becomes fragmented across systems
*   Harder to maintain as simulation complexity and aircraft count increases

Direct Variable Access (Shared State)

Multiple systems directly read and write to shared aircraft state variables without validation control

*   Minimal abstractions
*   Fast to prototype
*   Easy to modify aircraft state from any system during early development
*   Fewer coordination steps

*   No validation control any system can overwrite aircraft state
*   High risk of two systems writing conflicting values simultaneously
*   Breaks system ownership boundaries
*   Aircraft could receive conflicting heading instructions from two systems at once
*   Harder to debug when aircraft behave unexpectedly
*   Weakens architectural clarity and ownership

Centralised Airspace Authority _(Chosen)_

Single system owns all aircraft state. All other systems query or update aircraft state through this system only

*   High consistency across all interacting systems
*   Provides a single source of truth for all aircraft position, altitude, speed, heading, and intent
*   Strong validation control, instructions are checked against real aircraft state before execution
*   Conflict Detection always reads current, authoritative position data
*   Simplified debugging, all state changes route through one system

*   More complex initial setup
*   Introduces a central dependency for all aircraft operations
*   Faults in the Airspace Management System affect all dependent systems
*   Requires stronger architectural planning
*   Introduces request overhead if poorly structured

**Evaluation Criteria:**

*   _Consistency of aircraft state across all interacting systems at all times_
*   _Degree of validation and control over aircraft state changes before execution_
*   _Resistance to conflicting writes from multiple systems simultaneously_
*   _Ease of debugging when aircraft behave unexpectedly mid-session_
*   _Supports scalability as aircraft count increases at higher difficulty levels_

**Conclusion:**

I chose the centralised approach because it gave me the strongest control over validation, consistency, and debugging across all five systems. With the Conflict Detection, Communication, and Scoring systems all depending on aircraft state being accurate, I needed one system that owned the airspace completely rather than letting multiple systems modify it independently.

**Justification Used:**

*   _If the systems don't agree on the position or altitude of an aircraft, the simulation breaks. Conflict Detection could miss a separation violation because it read stale data_
*   _I needed instruction validation, conflict monitoring, and scoring to always operate on the same reliable aircraft state no matter which system triggered the read_

_“Game mechanics are the core of what a game truly is. They are the interactions and relationships that remain when all of the aesthetics, technology, and story are stripped away.” (Schell, 2015, p.158)_

_This backed up my decision to treat the Airspace Management System as a protected core mechanic. If aircraft state becomes inconsistent, the player's instructions stop having predictable outcomes, the simulation loses fidelity. By centralising all aircraft state under one authoritative system I kept the simulation's feedback reliable and trustworthy._

_Mini Airways (Truong, 2023) reinforced this for me. Its entire loop depends on aircraft moving predictably in response to player instructions. That only works if every system reading aircraft position is reading the same data. I applied the same thinking to the Airspace Management System by keeping all aircraft state under one authoritative system rather than letting multiple systems queries or modify it independently._

1.  **System Inputs / System Outputs**

**Inputs**

**Outputs**

Aircraft entry data from the sector boundary (callsign, altitude, speed, heading, intention)

Updated aircraft state stored and made available to all dependent systems

State update requests from the Aircraft Behaviour System (position tick, altitude change, speed change, heading change)

Current aircraft state in response to read requests from Conflict Detection, Communication, and Scoring systems

Instruction confirmation from the Communication System (validated instruction ready to apply)

Radar display data, live aircraft positions, altitudes, speeds, and callsigns

Requests for current aircraft state from dependent systems

Player-facing feedback for aircraft entering, exiting, or changing state within the sector

Aircraft exit notifications when an aircraft leaves the sector boundary

Confirmed success / failure responses for state update requests

Sector wind direction and speed (configurable per session)

Active runway selection based on wind direction and crosswind component calculation

1.  **System Boundaries:**

**Responsible For**

**Not Responsible For**

Storing and updating all aircraft state (position, altitude, speed, heading, callsign, intention)

Rendering the radar display visually (Radar/UI System)

Providing a single authoritative source of aircraft state to all dependent systems

Calculating whether a separation violation has occurred (Conflict Detection System)

Registering aircraft when they enter the sector

Deciding what instruction to issue to an aircraft (Communication System)

Removing aircraft when they exit the sector or land

Executing the physical movement of aircraft (Aircraft Behaviour System)

Validating that state update requests are structurally valid before applying them

Scoring the player or scaling difficulty (Scoring & Assessment System)

Maintaining sector environmental state (wind direction and speed) used for runway selection decisions

(existing entries remain)

1.  **System Dependencies:**

**System/Component**

**Role/Interaction**

Game Instance / World Manager

Provides shared access to the Airspace Management System across all dependent systems

Aircraft Behaviour System

Sends position and state update requests each simulation tick as aircraft move

Communication System

Reads current aircraft state to validate player instructions before execution

Conflict Detection System

Reads all aircraft positions and projected trajectories to calculate separation

Scoring & Assessment System

Reads aircraft state and session events to track performance and scale difficulty

Radar Display / UI

Reads aircraft state each frame to render live positions, altitudes, and data tags on screen

These dependencies position the Airspace Management System as the authoritative data layer within the simulation loop, rather than an isolated state store. Every system that affects or monitors aircraft routes through it.

1.  **System Communication:**

**Source System**

**Trigger**

**Message / Call**

**Data Sent**

**Response**

**Outcome**

Aircraft Behaviour System

Simulation tick update

UpdateAircraftState

Callsign, new position, altitude, speed, heading

Confirmed / Rejected

Airspace Management System updates the aircraft's stored state and makes it available to all dependent systems

Communication System

Player issues an instruction

GetAircraftState()

Callsign of target aircraft

Current aircraft state data

Communication System validates whether the instruction is physically possible given the aircraft's current state before passing it to Aircraft Behaviour

Conflict Detection System

Continuous monitoring loop

GetAllAircraftStates()

None (read-only request)

Full list of current aircraft states

Conflict Detection calculates separation distances and trajectory projections using authoritative position data

Scoring & Assessment System

Aircraft lands, exits, or incident occurs

GetAircraftState() / GetSessionLog()

Callsign, event type

Aircraft state at time of event

Scoring system logs the event, updates performance score, and adjusts difficulty scaling

Radar Display / UI

Each render frame

GetAllAircraftStates()

None (read-only request)

Full list of current aircraft states

Radar renders live aircraft positions, altitudes, speeds, and callsign data tags on screen

Session Controller

Session ends or resets

ClearAllAircraft()

None

Confirmation

All aircraft state is cleared. Dependent systems receive empty state on next read. Radar display clears. Scoring finalises session score.

1.  **System Flow Diagrams:**

_Aircraft Registration Flow (Flowchart2)_

_State Update Flow (Flowchart3)_

_Aircraft Deregistration Flow (Flowchart4)_

1.  **Edge Cases & Failure States**

The following edge cases have been identified to ensure the system remains robust under concurrent operations, invalid inputs, and system desynchronisation scenarios.

**Scenario**

**Cause**

**System Response**

**Outcome**

State update received with invalid callsign

Aircraft Behaviour System sends an update for an aircraft not currently registered

Request ignored; Airspace Management only processes updates for registered aircraft

No invalid state stored; Aircraft Behaviour System receives rejection response

Simultaneous state update attempts on the same aircraft

Two systems attempt to write to the same aircraft state at the same time

Request temporarily queued until the current state update is complete; only one write applied at a time

State integrity preserved; both updates eventually processed in sequence

Out-of-sequence state update received

Aircraft position update arrives with an older timestamp than the currently stored state

Update rejected; Airspace Management only accepts state updates newer than the currently stored value

Stale data never overwrites newer state; aircraft maintains most recent confirmed position

Conflict Detection reads aircraft state during an active update operation

Read request arrives mid-write

Conflict Detection receives the last confirmed state; in-progress updates are not exposed until fully committed

Conflict Detection always operates on stable authoritative state; no partial reads

Aircraft exits sector without handoff

Aircraft reaches boundary waypoint with no handoff issued

Aircraft flagged as unresolved exit; Scoring System notified

Incident logged as missed handoff; aircraft removed from active state

Airspace queried before any aircraft are registered

Dependent system requests aircraft data at session start

Returns empty state list

Dependent systems handle zero-aircraft state gracefully without errors

State update fails mid-operation

Internal error or invalid data during write

Update fails safely; no partial state is stored

Aircraft retains its last confirmed state; system continues operating normally

Airspace Management reference unavailable

Dependent system attempts query when reference is null or invalid

Request fails safely; no aircraft state modified

Dependent system receives null or error response; handles gracefully

Concurrent read requests from multiple systems

Multiple systems read aircraft state at the same time

All reads served from the same authoritative state

No write conflicts occur during read-only operations; all reads return consistent data

Instruction confirmation arrives for an exited aircraft

Communication System sends instruction after aircraft has left sector

Instruction discarded; Airspace Management rejects operations on aircraft no longer registered

No invalid operation executed; Communication System receives rejection

Aircraft count exceeds maximum supported at peak difficulty

Spawner attempts to register aircraft when sector is at cap

Registration rejected; Airspace Management refuses entry and notifies Scoring System

Sector cap enforced; no invalid aircraft state created

1.  **System Success Criteria**

**#**

**Success Criteria**

**Expected Behaviour / Validation Outcome**

1

Aircraft State Registration

When an aircraft enters the sector, the Airspace Management System correctly registers it with all state data (callsign, position, altitude, speed, heading, intention) and makes it immediately available to all dependent systems

2

State Accuracy

The aircraft state stored in the Airspace Management System is always accurate and up to date when requested by the Conflict Detection, Communication, Scoring, or Radar Display systems

3

Radar Synchronisation

The radar display always shows the correct aircraft positions, altitudes, and callsigns immediately after each state update — no stale data rendered

4

Instruction Validation Support

The Communication System can request current aircraft state at any time and receive a correct, up-to-date response to validate player instructions against

5

Conflict Detection Accuracy

The Conflict Detection System always reads authoritative aircraft state — separation calculations are never based on outdated or incorrect position data

6

Invalid State Prevention

Aircraft state never enters a physically impossible value (negative altitude, speed below stall, position outside sector bounds) under any circumstance

7

Concurrent Read Stability

Multiple systems reading aircraft state simultaneously do not cause data corruption, race conditions, or inconsistent state values

8

Safe Failure on Unavailability

If the Airspace Management System reference is unavailable when a dependent system queries it, the request fails safely and no aircraft state is modified or corrupted

9

Authority Enforcement

No dependent system (Aircraft Behaviour, Conflict Detection, Communication, or Scoring) ever reads or writes aircraft state directly — all interactions must route through the Airspace Management System |

10

Session Persistence

Aircraft state remains consistent and correctly tracked throughout the full play session regardless of how many aircraft enter, exit, land, or are handed off

_This system specification positions the Airspace Management System as the sole authority over aircraft state, ensuring that all aircraft-related interactions pass through validated requests and remain consistent across all dependent systems._

### Aircraft Behaviour System

1.  **Description:**

_The Aircraft Behaviour System is responsible for controlling how every aircraft within the sector moves and responds to instructions. It receives validated instructions from the Communication System and executes them gradually and realistically — aircraft turn at appropriate bank angles, climb and descend at realistic vertical rates, and accelerate or decelerate over time rather than snapping instantly to new values. The system updates the Airspace Management System each simulation tick with the aircraft's new position, altitude, speed, and heading. The session will begin with all aircraft following their assigned flight plans until the player issues instructions._

1.  **System Purpose:**

_The Aircraft Behaviour System will act as the sole executor of aircraft movement within the simulation, allowing the Communication System, Airspace Management System, and Conflict Detection System to request or monitor aircraft motion without directly controlling aircraft movement themselves._

_This will enforce a clear separation of concerns, ensuring that movement logic, instruction execution, and flight path calculation remain isolated within a single authoritative system, reducing any likelihood of inconsistent aircraft positions across the simulation (Salen & Zimmerman, 2003; Gregory, 2014)._

1.  **Design Rationale:**

**Alternatives Considered:**

**Approach**

**Description**

**Pros**

**Cons**

State Machine Per Aircraft (Hardcoded Transitions)

Each aircraft operates as a fixed state machine with predefined transitions (e.g. Cruising → Descending → Landing) with no dynamic instruction handling

*   Simple to implement and debug
*   Predictable behaviour — easy to test each state transition
*   Low computational overhead
*   Clear visual debugging — aircraft are always in a known state

*   Rigid — cannot handle dynamic player instructions mid-flight
*   Cannot respond to conflict resolution instructions (heading changes, altitude adjustments on demand)
*   No simulation fidelity — aircraft snap between states rather than transitioning gradually
*   Poor fit for a system requiring real-time player control

Distributed Movement Ownership (Each System Controls Its Own Aircraft)

The Communication System, Conflict Detection System, and other systems directly apply movement changes to aircraft without routing through a central behaviour system

*   Faster localised operations within individual systems
*   Reduced dependency on a central system
*   Simpler initial implementation

*   Desynchronisation risk between systems — two systems could issue conflicting movement instructions simultaneously
*   Conflicting heading or altitude updates causing physically impossible aircraft states
*   Harder debugging and validation — movement logic spread across multiple systems
*   Violates the single source of truth architectural approach

Direct Position Mutation (Instant Teleportation)

Other systems directly modify aircraft position and altitude values instantly without gradual transitions

*   Minimal abstraction and faster implementation
*   Direct access reduces communication overhead

*   Enables invalid states and exploit potential
*   No simulation fidelity — aircraft teleport rather than fly
*   Breaks system boundaries
*   Destroys cognitive fidelity — the simulation no longer feels like real ATC

Physics-Based Simulation (Full Flight Dynamics)

Each aircraft simulates full aerodynamic behaviour including thrust, drag, lift, bank angle physics, and stall modelling

*   Maximum simulation realism
*   Authentic aircraft response to all instructions

*   Unnecessary complexity for a portfolio ATC simulation
*   Increased processing and implementation cost
*   No functional benefit for the gameplay loop — cognitive fidelity is achieved through decision pressure, not aerodynamic accuracy
*   Risk of scope creep delaying core system delivery

Gradual Instruction Execution via Centralised Behaviour System _(Chosen)_

A single Aircraft Behaviour System owns all aircraft movement. Instructions are executed gradually over time at realistic rates. Position updates are sent to the Airspace Management System each tick

*   Realistic aircraft movement preserving simulation fidelity
*   Single source of truth for all aircraft motion
*   Clean instruction pipeline — Communication System sends, Aircraft Behaviour System executes
*   Scalable to any number of simultaneous aircraft

*   More complex than direct mutation
*   Requires careful tick-rate management to avoid performance issues at higher aircraft counts
*   Central dependency — faults affect all aircraft simultaneously

These alternatives were evaluated against criteria including simulation fidelity, instruction responsiveness, validation control, conflict prevention, and scalability within a multi-system architecture.

**Approach Chosen:**

**Approach**

**Description**

**Pros**

**Cons**

Gradual Instruction Execution via Centralised Behaviour System _(Chosen)_

A single Aircraft Behaviour System owns all aircraft movement. Instructions are executed gradually at realistic rates. Position updates are sent to the Airspace Management System each tick

Prevents multiple systems from controlling aircraft movement independently. Realistic gradual transitions preserve simulation fidelity. Clean instruction pipeline. Scalable to any number of simultaneous aircraft

Introduces a single point of failure. Requires careful tick-rate management. Central dependency means faults affect all aircraft simultaneously

I chose the centralised gradual behaviour approach because it gave me the strongest balance between simulation fidelity and architectural clarity. With the Conflict Detection and Communication systems both depending on aircraft moving predictably and responsively, I needed one system that owned all aircraft movement completely rather than having multiple systems apply conflicting motion changes independently.

**Justification Used:**

*   _If aircraft don't move gradually and realistically, the simulation loses cognitive fidelity — the player's instructions stop feeling like real ATC and start feeling like a menu system_
*   _I needed heading changes, altitude adjustments, and speed changes to always produce consistent, predictable aircraft motion regardless of which system triggered the instruction_

_"A simulation is a set of things that affect one another within an environment to form a larger pattern that is different from any of the individual parts."_ (Salen & Zimmerman, 2003, p.57)

This quote supported my decision to treat the Aircraft Behaviour System as an authoritative movement executor rather than a passive instruction receiver. Because aircraft movement directly affects conflict detection, radar accuracy, and instruction validation, I needed movement changes to propagate reliably across every dependent system. A centralised behaviour-oriented approach gave me stronger consistency, prevented conflicting motion commands, and supported better simulation performance (Gregory, 2014).

This approach also lined up with how aircraft movement works in reference games like Mini Airways (Truong, 2023) and Global ATC Simulator (Laminar Research, 2024), where aircraft respond to instructions with gradual, realistic transitions rather than instant repositioning. Gradual movement made a stronger fit for CLEARANCE's cognitive fidelity design goals, because the simulation's ability to produce genuine ATC decision pressure depends entirely on aircraft behaving the way a controller expects them to behave (Salen & Zimmerman, 2003).

**Flight Dynamics Implementation**

Aircraft motion within the Aircraft Behaviour System is driven by simplified flight dynamics rather than waypoint interpolation. This produces aircraft behaviour that responds correctly to physical parameters and atmospheric conditions, supporting the simulation’s cognitive fidelity goal.

The system implements the following physics elements:

Physics Element

Implementation

Effect on Simulation

Turn rate calculation

Standard rate turn formula. Turn radius equals velocity squared divided by gravitational acceleration times tangent of bank angle. Turn rate is velocity divided by turn radius. Bank angle limited by aircraft performance category — standard rate turns at 25 degrees for transport category aircraft

Aircraft turn at realistic rates that scale with airspeed; faster aircraft require larger turn radius; player must account for turn anticipation when sequencing aircraft

Climb and descent rates

Limited by aircraft performance category. Light aircraft climb at 500-1000 ft/min. Medium aircraft climb at 1500-2500 ft/min. Heavy aircraft climb at 2000-3000 ft/min. Service ceiling limits maximum altitude per aircraft type

Altitude changes take realistic time to complete; player must issue altitude instructions with sufficient lead time; heavy aircraft cannot match light aircraft climb performance

Speed envelope enforcement

Each aircraft has a minimum operating speed (above stall) and maximum operating speed. Speed instructions outside this envelope are rejected at validation. Speed changes occur gradually at realistic acceleration and deceleration rates

Player cannot issue physically impossible speed instructions; speed adjustments take time to execute; speed differential between aircraft types becomes a sequencing factor

Atmospheric density variation

International Standard Atmosphere lookup. Air density decreases with altitude following ISA. Implementation uses an altitude-to-density lookup function rather than computing the atmospheric model from first principles

Engine performance reduces at high altitudes; climb rates progressively decrease as aircraft approach service ceiling; realistic altitude limits emerge from physics rather than arbitrary caps

Wind effects on aircraft motion

Aircraft ground track differs from heading based on wind direction and speed. Aircraft on approach must account for crosswind components

Aircraft do not fly exact compass headings over the ground; player must account for wind drift when issuing vectors; strong crosswinds affect runway selection and approach feasibility

Implementation references Stengel (2004) Flight Dynamics and Anderson (2016) Introduction to Flight. Simplified equations are used rather than full computational fluid dynamics because the simulation requires real-time performance across multiple simultaneous aircraft and ATC perspective rather than pilot perspective.

**Evaluation Summary:**

I chose the centralised behaviour approach because it gave me the strongest combination of simulation fidelity, instruction consistency, and scalability across my interdependent systems. Compared with state machine or direct position mutation approaches, it eliminated desynchronisation risk and ensured that all aircraft movement changes went through a single validated execution pathway. The extra architectural complexity was worth it because instruction execution, conflict monitoring, and radar accuracy all needed aircraft to behave reliably across the wider simulation loop.

1.  **System Inputs / System Outputs**

**Inputs**

**Outputs**

Validated instructions from the Communication System (heading change, altitude change, speed change, approach clearance, takeoff clearance)

Updated aircraft state sent to the Airspace Management System each simulation tick (position, altitude, speed, heading)

Flight plan data for aircraft entering the sector from the Airspace Management System

Confirmation to the Communication System that an instruction has been accepted and is being executed

Go-around trigger from the Conflict Detection System when a landing must be aborted

Rejection response to the Communication System if an instruction cannot be physically executed

Sector exit notifications when an aircraft reaches a boundary waypoint

Sector exit notification sent to the Airspace Management System when an aircraft leaves the sector

Landing clearance confirmation from the Communication System

Go-around execution confirmation sent to the Conflict Detection System when triggered

1.  **System Boundaries:**

The Aircraft Behaviour System will act as the sole executor of aircraft motion. All movement changes must pass through this system; no external system is permitted to directly alter aircraft position, altitude, speed, or heading values.

**Responsible For**

**Not Responsible For**

Executing all aircraft movement — heading changes, altitude changes, speed changes — gradually over time at realistic rates

UI layout or radar visual rendering (Radar/UI System)

Following assigned flight plans until the player issues an instruction via the Communication System

Validating whether a player instruction is permissible — that is the Communication System's responsibility

Sending updated aircraft position, altitude, speed, and heading to the Airspace Management System each simulation tick

Calculating whether a separation violation has occurred (Conflict Detection System)

Executing approach paths and landing sequences when approach clearance is received

Scoring the player or logging incidents (Scoring & Assessment System)

Executing go-arounds when triggered by the Conflict Detection System

Deciding which instruction to issue to which aircraft — that is the player's decision via the Communication System

The Aircraft Behaviour System acts as the sole executor of aircraft motion. All movement changes must pass through this system; no external system is permitted to directly alter aircraft position, altitude, speed, or heading values.

**State Ownership Principle:**

The Aircraft Behaviour System is the sole executor of aircraft motion within the simulation. All movement operations are routed through validated instruction pipelines, and no write operations are permitted directly from outside this system.

1.  **System Dependencies:**

**System/Component**

**Role/Interaction**

Airspace Management System

Provides flight plan data when aircraft enter the sector; receives updated aircraft state (position, altitude, speed, heading) each simulation tick

Communication System

Sends validated player instructions (heading change, altitude change, speed change, approach clearance, takeoff clearance) for execution

Conflict Detection System

Triggers go-around commands when a landing must be aborted due to a separation violation on the approach path

Game World / Simulation Clock

Provides the tick rate that drives gradual aircraft movement calculations each frame

These dependencies ensure that aircraft movement is directly constrained and driven by validated inputs from authoritative systems, forming a controlled pipeline from instruction receipt to physical aircraft motion.

1.  **System Communication:**

**Source System**

**Trigger**

**Message/Call**

**Data Sent**

**Response**

**Outcome**

Communication System

Player issues a heading change instruction

ExecuteHeadingChange

Callsign, target heading

Accepted / Rejected

Aircraft Behaviour System begins gradually turning the aircraft toward the new heading at a realistic bank rate; updated heading values sent to Airspace Management each tick

Communication System

Player issues an altitude change instruction

ExecuteAltitudeChange

Callsign, target altitude

Accepted / Rejected

Aircraft begins climbing or descending at a realistic vertical rate; updated altitude values sent to Airspace Management each tick

Communication System

Player issues approach clearance

ExecuteApproachClearance()

Callsign, runway identifier

Accepted / Rejected

Aircraft begins following the approach path toward the runway, descending gradually; Airspace Management updated each tick

Conflict Detection System

Separation violation detected on approach path

ExecuteGoAround()

Callsign identifier and quantity

Confirmed

Aircraft aborts landing, climbs away from the runway at a realistic rate, and re-enters the circuit; Airspace Management updated; Conflict Detection notified

Simulation Clock

Each tick

UpdateAllAircraftPositions

None (internal tick)

Updated state pushed to Airspace Management

All active aircraft positions, altitudes, speeds, and headings are recalculated and committed to the Airspace Management System

This communication structure ensures that all aircraft movement changes originate from validated system instructions, allowing the Aircraft Behaviour System to act as a consistent executor between instruction receipt, movement calculation, and Airspace Management state updates, while maintaining simulation integrity across the gameplay loop.

1.  **System Flow Diagrams**

_Heading Change Execution Flow (Flowchart5)_

_Altitude Change Execution Flow (Flowchart6)_

_Go-Around Execution Flow (Flowchart7)_

1.  **Edge Cases & Failure States**

**Movement Integrity Guarantees:**

*   _The system will enforce strict movement integrity rules to prevent invalid or inconsistent aircraft states:_
*   _Aircraft altitude can never fall below terrain elevation or minimum safe altitude_
*   _All instructions are validated prior to execution to prevent physically impossible manoeuvres, with safeguards to minimise partial or inconsistent movement outcomes_
*   _Aircraft state remains consistent across all dependent systems through tick-driven position updates_

**Scenario**

**Cause**

**System Response**

**Outcome**

Instruction received for an aircraft that has already exited the sector

Communication System sends instruction after aircraft departure

Instruction is rejected. Aircraft Behaviour System only processes instructions for currently registered aircraft

No invalid movement executed; Communication System receives rejection response

Two conflicting instructions received simultaneously (e.g. climb and descend)

Race condition between player instructions or system triggers

The second instruction is queued until the first is complete; only one active instruction per axis at a time

Only valid sequential instruction attempts succeed

Go-around triggered while aircraft is already executing a go-around

Conflict Detection fires multiple alerts for the same aircraft

Duplicate go-around command is ignored. System validates that a go-around is not already in progress before executing

Aircraft continues existing go-around uninterrupted

Instruction execution interrupted by aircraft exiting the sector mid-manoeuvre

Aircraft reaches boundary waypoint while turning or climbing

Instruction execution halts; aircraft is removed from Airspace Management; no further state updates are sent

No orphaned movement calculations; Airspace Management correctly reflects aircraft removal

Aircraft reaches physically impossible state (negative altitude, speed below stall)

Edge case in gradual movement calculation

System clamps values to minimum safe thresholds and flags the state for review

Aircraft remains in a valid state; no simulation crash

Approach clearance issued but runway is occupied

Communication System grants clearance without checking runway state

Aircraft Behaviour System triggers go-around upon detecting runway occupancy on approach

Landing aborted safely; Scoring System notified of the incident

Tick-rate spike causes position update delay

Performance issue during high aircraft count

Aircraft holds last confirmed position until next successful tick update

No invalid intermediate states pushed to Airspace Management

Aircraft cannot achieve requested climb rate at high altitude

Air density at altitude reduces engine performance below requested climb rate

System reduces climb rate to maximum sustainable for current altitude and aircraft type

Aircraft climbs at reduced rate; Communication System notified that target altitude will take longer to reach

Aircraft requested to exceed service ceiling

Player issues altitude instruction beyond aircraft performance envelope

Instruction rejected at Communication System validation against aircraft service ceiling

Player receives rejection feedback with reason; aircraft maintains current cleared altitude

Strong crosswind during approach

Wind component perpendicular to runway exceeds aircraft crosswind limit

Aircraft Behaviour System triggers go-around; Communication System notified

Landing aborted safely; player must reassign aircraft or wait for wind change

Speed instruction below minimum operating speed

Player issues speed instruction below stall speed for aircraft type

Instruction rejected at Communication System validation

Player receives rejection feedback; aircraft maintains current speed

1.  **System Success Criteria**

**#**

**Criteria**

**Expected Behaviour / Validation Outcome**

1

Heading Change Execution

When a heading change instruction is received, the aircraft turns gradually at a realistic bank rate and reaches the target heading correctly

2

Altitude Change Execution

When an altitude change instruction is received, the aircraft climbs or descends at a realistic vertical rate and levels off at the target altitude

3

Speed Change Execution

When a speed change instruction is received, the aircraft accelerates or decelerates gradually to the target speed

4

Approach and Landing

When approach clearance is received, the aircraft follows the approach path correctly and lands without error

5

Go-Around Execution

When a go-around is triggered, the aircraft immediately climbs away from the runway at a realistic rate and re-enters the circuit

6

Tick-Rate State Updates

Aircraft position, altitude, speed, and heading are updated in the Airspace Management System every simulation tick with no stale data

7

Invalid State Prevention

Aircraft altitude, speed, and heading never enter physically impossible values under any instruction or edge case scenario

8

Simultaneous Aircraft Independence

Multiple aircraft executing instructions simultaneously do not interfere with each other's movement calculations

The Aircraft Behaviour System acts as the sole movement execution layer for the simulation, handling instruction execution, flight path management, and tick-rate state updates.

**Core Movement Operations (Summary):**

*   _Heading execution — Receives validated heading change instructions from the Communication System and turns the aircraft gradually at a realistic bank rate_
*   _Altitude execution — Receives validated altitude change instructions and climbs or descends the aircraft at a realistic vertical rate_
*   _Approach and landing — Follows the approach path to touchdown when approach clearance is received; executes go-arounds when triggered by Conflict Detection_
*   _Tick-rate updates — Recalculates and commits all aircraft positions, altitudes, speeds, and headings to the Airspace Management System every simulation tick_

### Communication System

1.  **Description**

The Communication System is the transaction layer between the player and the simulation. It receives player-issued instructions through the radar interface, validates each instruction against the current aircraft state retrieved from the Airspace Management System, and routes confirmed instructions to the Aircraft Behaviour System for execution. The system will provide feedback to the player on instruction success or rejection. The session will begin with no active instructions in the queue.

1.  **System Purpose**

_The Communication System will provide the player with a controlled interface for issuing ATC instructions to aircraft, while keeping instruction validation, movement execution, and state management responsibilities separated across the Airspace Management, Aircraft Behaviour, and Conflict Detection systems._

_This system will act as the interaction layer between player input, current aircraft state, and movement execution rather than owning aircraft state or executing movement directly._

1.  **Design Rationale**

**Alternatives Considered**

**Approach**

**Description**

**Pros**

**Cons**

Direct Instruction Execution (No Validation Layer)

Player instructions are sent directly to the Aircraft Behaviour System without passing through a validation step

*   Very fast to implement
*   Minimal interaction flow
*   Low UI complexity
*   Reduced dependency on a central validation layer
*   Immediate player feedback loop

*   No validation control — physically impossible instructions could be executed
*   Aircraft could receive conflicting heading and altitude changes simultaneously
*   No feedback to the player when an instruction is invalid
*   Breaks system boundary separation — player input directly controls movement
*   Removes the ATC authenticity of instruction confirmation and rejection

Broadcast Instruction System (All Aircraft Receive All Instructions)

Player instructions are broadcast to all active aircraft simultaneously rather than targeting a specific callsign

*   Simple implementation — no aircraft selection logic required
*   Fast to prototype

*   No simulation fidelity — real ATC instructions are always callsign-specific
*   Creates immediate conflict scenarios by changing all aircraft simultaneously
*   Impossible to manage individual aircraft separation
*   Fundamentally incompatible with the conflict detection and scoring architecture

Validated Instruction Pipeline via Communication System _(Chosen)_

Player selects an aircraft by callsign, selects an instruction type, and the Communication System validates the instruction against current aircraft state before routing it to the Aircraft Behaviour System

*   Full validation control before any instruction is executed
*   Player receives meaningful feedback on success or rejection
*   Clean separation between player input, validation logic, and movement execution
*   Authentic to real ATC instruction procedure — callsign, instruction, confirmation
*   Supports conflict detection integration — invalid instructions near separation limits can be flagged

*   More complex than direct execution
*   Adds a validation step to every player instruction
*   Requires Airspace Management System to be available for every instruction check

These alternatives were evaluated against criteria including instruction clarity, system separation, validation control, feedback quality, and scalability within a multi-system architecture.

**Justification Used:**

_“A data-driven architecture is what differentiates a game engine … When a game contains hard-coded logic or game rules or employs special-case code to render specific types of game objects, it becomes difficult or impossible to reuse that software to make a different game.”_ (Gregory, 2014, p. 11)

This quote supported my decision to treat the Communication System as a controlled validation layer rather than a simple instruction passthrough. Every player interaction in CLEARANCE — heading changes, altitude adjustments, approach clearances, handoffs — is part of a simulation loop that links player intent to aircraft movement, and I needed those instructions to be validated against authoritative state before being executed. Routing everything through the Communication System gave me cleaner instruction handling, stronger consistency between systems, and a pipeline that could scale as session complexity increased.

Mini Airways (Truong, 2023) was also a useful reference here because player instructions in that game are treated as deliberate, callsign-specific interactions rather than invisible background state changes. That supported my decision to keep the Communication System as a dedicated interaction layer responsible for instruction selection, validation, and confirmation, while leaving state authority to the Airspace Management System and movement authority to the Aircraft Behaviour System.

**Evaluation Summary:**

I chose the validated instruction pipeline approach because it gives me the strongest combination of instruction clarity, system separation, and scalability across the simulation pipeline. Compared with direct execution or broadcast handling approaches, this eliminates desynchronisation risk and ensures that player instructions remain validated and clearly communicated between the player, the Airspace Management System, and the Aircraft Behaviour System. The validation layer is justified because instructions need to be predictable, verified, and meaningful within the wider simulation loop.

1.  **System Inputs / System Outputs**

**Inputs**

**Outputs**

Player instruction input (aircraft selected, instruction type selected, instruction parameters entered)

Validated instruction sent to the Aircraft Behaviour System for execution

Current aircraft state from the Airspace Management System (used to validate the instruction)

Rejection response with feedback returned to the player if the instruction is invalid

Conflict Detection System advisory flag (warns Communication System if an instruction would worsen a developing conflict)

Confirmation feedback to the player that the instruction has been accepted and is being executed

Go-around trigger from Conflict Detection System

Go-around instruction sent to the Aircraft Behaviour System

Instruction confirmation response from the Aircraft Behaviour System

Player-facing feedback messages displayed on the radar HUD

1.  **System Dependencies:**

**System / Component**

**Role / Interaction**

Airspace Management System

Provides current aircraft state for instruction validation before execution

Aircraft Behaviour System

Receives validated instructions and executes them as gradual aircraft movement

Conflict Detection System

Sends advisory flags when a player instruction could worsen a developing separation issue; triggers go-around commands

Player HUD / UI

Displays instruction input interface, feedback messages, and confirmation responses to the player

These dependencies position the Communication System as a controlled instruction validation interface within the wider simulation loop rather than as a standalone input handler or movement controller.

1.  **System Assumptions:**

**Assumption**

**Implication for the Communication System**

The Airspace Management System always returns a valid and current aircraft state when queried

The Communication System will rely on external state validation rather than managing or caching aircraft state itself

Aircraft state data is accurate and reflects the aircraft's real current position, altitude, speed, and heading

The Communication System can safely validate instructions against state data without requiring additional verification

The Aircraft Behaviour System is available and responsive when a validated instruction is sent

The Communication System will hand off validated instructions without needing to track long-term instruction execution status

Player instructions are only initiated through deliberate player input through the radar interface

The Communication System will remain a controlled player-facing validation layer with reduced risk of spurious or automated instruction injection

1.  **Data and Ownership Model:**

**Data / State**

**Owned By**

**Used By / Purpose**

Player instruction input (selected aircraft, instruction type, parameters)

Communication System (temporarily, during validation)

Used to form the validated instruction request sent to Aircraft Behaviour System

Current aircraft state (position, altitude, speed, heading, intention)

Airspace Management System

Read by the Communication System to validate instruction feasibility before execution

Instruction validation result (accepted / rejected)

Communication System

Returned to the player as feedback; if accepted, triggers instruction dispatch to Aircraft Behaviour System

Conflict advisory flag

Conflict Detection System

Read by Communication System to warn the player before a potentially dangerous instruction is confirmed

Instruction execution confirmation

Aircraft Behaviour System

Read by Communication System to confirm the instruction is being executed; relayed to player as feedback

1.  **System Communication:**

**Source System**

**Trigger**

**Message / Call**

**Data Sent**

**Response**

**Outcome**

Player / Radar UI

Player selects an aircraft on the radar and chooses an instruction type

RequestInstructionValidation()

Callsign, instruction type, instruction parameters (target heading / altitude / speed)

Validation pending

Communication System queries the Airspace Management System for the current aircraft state

Airspace Management System

Communication System requests current aircraft state for validation

GetAircraftState()

Callsign

Current aircraft state returned

Communication System receives position, altitude, speed, heading, and intention data to validate the instruction against

Communication System

Instruction validated successfully

SendInstruction()

Callsign, instruction type, parameters

Accepted / Rejected by Aircraft Behaviour System

If accepted: Aircraft Behaviour System begins executing the instruction. Player receives confirmation feedback on HUD. If rejected: player receives rejection feedback with reason

Conflict Detection System

Separation advisory generated while player is forming an instruction

SendAdvisoryFlag()

Callsign, advisory level

Advisory received

Communication System displays advisory warning to player on HUD before instruction confirmation — player can modify or cancel the instruction

Conflict Detection System

Critical separation violation — go-around required

TriggerGoAround()

Callsign of aircraft on approach

Confirmed

Communication System routes the go-around command directly to the Aircraft Behaviour System; player HUD displays go-around alert

This communication structure ensures that the Communication System remains a controlled validation intermediary between player instruction input, aircraft state data, and movement execution while maintaining system separation across the simulation loop.

1.  **Flow Chart Diagrams**

Instruction Validation Flow _(Flowchart8)_

Advisory Warning Flow _(Flowchart9)_

Go-Around Flow _(Flowchart10)_

1.  **Edge Cases & Failure States**

**Scenario**

**Cause**

**System Response**

**Outcome**

Player issues instruction to an aircraft that has already exited the sector

Instruction submitted after aircraft departure

Communication System rejects the instruction — only active registered aircraft can receive instructions

No invalid instruction executed; player receives rejection feedback

Player issues a heading change that would immediately cause a separation violation

Instruction conflicts with existing traffic

Conflict Detection advisory flag received before confirmation — Communication System displays warning on HUD

Player is warned; can modify or cancel the instruction before it executes

Airspace Management System unavailable when Communication System requests aircraft state

System dependency failure

Instruction validation fails safely — Communication System returns an error state and no instruction is dispatched

No instruction executed; no aircraft state modified

Player issues an altitude change below minimum safe altitude

Invalid parameter entered

Instruction rejected during validation — minimum safe altitude constraint enforced

Player receives rejection feedback with the reason; aircraft maintains current altitude

Two instructions issued to the same aircraft in rapid succession

Player input speed exceeds validation processing

Second instruction queued until first validation and dispatch is complete

Only sequential validated instructions reach the Aircraft Behaviour System

Go-around triggered for an aircraft not currently on approach

Conflict Detection fires erroneous alert

Communication System validates that the aircraft is in an approach state before routing the go-around command

Invalid go-around command is discarded; no erroneous movement executed

Player cancels instruction mid-validation

Manual cancellation input

Validation process is abandoned safely — no instruction dispatched to Aircraft Behaviour System

No aircraft state modified; system returns to idle instruction state

1.  **System Success Criteria**

**#**

**Criteria**

**Expected Behaviour / Validation Outcome**

1

Instruction Input

Player can select an aircraft on the radar, choose an instruction type, and enter parameters without errors

2

State Query

Communication System successfully retrieves current aircraft state from the Airspace Management System before every validation attempt

3

Valid Instruction Dispatch

When an instruction passes validation, it is correctly dispatched to the Aircraft Behaviour System and the player receives confirmation feedback on the HUD

4

Invalid Instruction Rejection

When an instruction fails validation (physically impossible, unsafe altitude, conflicting with aircraft state), the instruction is rejected and the player receives clear feedback explaining why

5

Advisory Display

When the Conflict Detection System sends an advisory flag during instruction formation, the advisory is correctly displayed on the player HUD before confirmation

6

Go-Around Routing

When a go-around is triggered by the Conflict Detection System, the command is correctly routed to the Aircraft Behaviour System and the player HUD displays the go-around alert

7

Cancelled Instruction Safety

When a player cancels an instruction mid-validation, no instruction is dispatched to the Aircraft Behaviour System and no aircraft state is modified

8

Exited Aircraft Rejection

Instructions issued to aircraft that have already left the sector are rejected and no movement is executed

9

Sequential Instruction Handling

Rapid successive instructions to the same aircraft are processed sequentially — no race conditions or conflicting commands reach the Aircraft Behaviour System

10

System Unavailability Safety

If the Airspace Management System is unavailable during a state query, validation fails safely and no instruction is dispatched

11

System Separation Integrity

The Communication System does not directly modify aircraft state, execute movement, or access Airspace Management data outside of validated instruction requests

**Core Communication Responsibilities (Summary):**

*   _Receives and processes player instruction input from the radar interface_
*   _Queries current aircraft state from the Airspace Management System for instruction validation_
*   _Validates instructions against aircraft state before dispatching to the Aircraft Behaviour System_
*   _Provides player-facing feedback for valid and invalid instruction attempts on the HUD_
*   _Routes confirmed instructions through the authoritative Aircraft Behaviour System pipeline_
*   _Receives and displays conflict advisory warnings from the Conflict Detection System_
*   _Routes go-around commands from the Conflict Detection System to the Aircraft Behaviour System_

### Conflict Detection System

1.  **Description**

The Conflict Detection System is responsible for continuously monitoring all aircraft within the sector for separation violations. It reads all aircraft state data from the Airspace Management System each monitoring cycle, calculates the distance and projected trajectories between every active aircraft pair, and generates warnings at three severity levels — Advisory, Warning, and Critical. It sends advisory flags to the Communication System to warn the player during instruction formation, and triggers go-around commands when a landing aircraft is at critical separation risk. The session will begin with no active alerts.

1.  **System Purpose**

To act as the safety monitoring authority for all aircraft within the controlled sector, ensuring that separation violations are detected early, communicated to the player through graded alerts, and escalated to automatic intervention when required — without directly modifying aircraft state or executing movement itself.

1.  **Design Rationale**

_I considered: continuous proximity monitoring, trajectory projection monitoring, and a hybrid approach combining both immediate distance checks with short-term trajectory prediction._

_I followed established simulation design practice where safety systems are assessed based on detection reliability, response time, and integration with authoritative state systems (Salen and Zimmerman, 2004)._

Alternatives Considered

Approach

Description

Pros

Cons

Proximity Distance Only (No Trajectory Projection)

Monitor the distance between all active aircraft pairs each cycle. Generate a warning only when distance falls below the minimum separation threshold

*   Simple to implement — calculate distance between all aircraft pairs each cycle
*   Low computational overhead
*   Easy to debug — clear pass/fail per aircraft pair
*   Predictable detection behaviour

*   Reacts only when aircraft are already dangerously close — no advance warning
*   No ability to warn the player about converging aircraft before separation is violated
*   Misses high-speed conflict scenarios where aircraft close distance rapidly between checks
*   Not authentic to real ATC conflict detection which uses trajectory prediction

Trajectory Projection Only (No Proximity Check)

Project each aircraft's future position based on current heading, speed, and altitude. Generate warnings if projected positions will violate separation within a defined time window

*   Provides early warning before aircraft are dangerously close
*   Authentic to real ATC conflict detection methodology
*   Supports advisory-level warnings giving the player time to respond

*   More complex to implement — requires trajectory calculation per aircraft per cycle
*   Projection accuracy degrades when players issue rapid heading changes
*   Could generate false positives if aircraft are projected to conflict but the player intervenes before the violation occurs
*   Higher computational cost at peak aircraft counts

Hybrid Proximity and Trajectory Monitoring _(Chosen)_

Combine continuous distance monitoring with short-term trajectory projection. Distance checks catch immediate violations. Trajectory projection generates early advisory warnings before violations occur

*   Three-level warning system (Advisory, Warning, Critical) maps naturally to this hybrid approach
*   Early warnings give the player time to respond before separation is violated
*   Immediate distance checks catch rapid closure that projection may miss
*   Authentic to real ATC safety monitoring methodology
*   Supports Communication System advisory flag integration

*   More complex than either approach alone
*   Requires careful tuning of projection window duration to avoid excessive false positives
*   Higher computational cost — runs every monitoring cycle across all aircraft pairs

These alternatives were evaluated against criteria including detection reliability, advance warning time, player response opportunity, computational feasibility, and scalability within a multi-aircraft environment.

**Justification Used:**

_“A data-driven architecture is what differentiates a game engine … When a game contains hard-coded logic or game rules or employs special-case code to render specific types of game objects, it becomes difficult or impossible to reuse that software to make a different game.”_ (Gregory, 2014, p. 11)

This quote supported my decision to treat the Conflict Detection System as a dedicated safety monitoring layer that reads from the Airspace Management System without modifying it. Every aircraft pair in CLEARANCE needs to be monitored continuously — I needed separation calculations to always operate on authoritative state data. Routing conflict detection through a read-only monitoring cycle gave me reliable, consistent safety data without coupling the detection logic to movement or instruction systems.

Global ATC Simulator (Laminar Research, 2024) was also a useful reference here because its conflict alerting system uses trajectory-based prediction to give controllers advance notice of separation issues. That supported my decision to implement trajectory projection alongside proximity distance monitoring, rather than relying on distance alone — giving the player meaningful warning time before a violation occurs.

**Evaluation Summary:**

I chose the hybrid proximity and trajectory monitoring approach because it gives me the strongest combination of detection reliability, advance warning capability, and scalability across the simulation pipeline. Compared with distance-only or trajectory-only approaches, this eliminates the risk of late or missed alerts and ensures that the player always has meaningful time to respond before a separation violation occurs. The three-level warning system is justified because it maps directly to how real ATC conflict alerting works — advisory, warning, critical — producing genuine cognitive fidelity in the player's response.

**Wake Turbulence Monitoring**

Wake turbulence separation is monitored as a third dimension alongside distance separation and trajectory projection. Aircraft are categorised by ICAO wake turbulence category (Light, Medium, Heavy, Super) based on maximum takeoff weight. Following aircraft must maintain minimum separation behind preceding aircraft based on the category combination — for example, Light following Heavy requires 6 nautical miles minimum separation, Medium following Heavy requires 5 nautical miles.

The system tracks each aircraft’s wake category from the aircraft state data and calculates wake separation requirements between any pair of aircraft on similar approach paths or following routes. When separation falls below the required minimum for the category combination, an advisory is sent to the Communication System.

Implementation follows ICAO Document 4444 wake turbulence separation standards. Simplified to focus on time and distance behind preceding aircraft rather than full vortex modelling because the ATC perspective requires the player to maintain separation rather than experience wake effects directly.

1.  **System Boundaries**

The Conflict Detection System is the sole safety monitoring authority for all aircraft separation within the sector. It reads aircraft state but never modifies it.

This follows common simulation architecture principles where a single authoritative system monitors safety state and prevents undetected violations through continuous independent checking (Gregory, 2014).

**Responsible For**

**Not Responsible For**

Continuously monitoring all active aircraft pairs for separation distance violations

Modifying aircraft state, position, altitude, speed, or heading (handled by Aircraft Behaviour System)

Calculating short-term trajectory projections to predict future separation violations

Issuing player instructions or validating player input (handled by Communication System)

Generating three-level separation alerts (Advisory, Warning, Critical) and sending them to the Communication System

Scoring calculations or difficulty scaling (handled by Scoring & Assessment System)

Triggering go-around commands via the Communication System when a landing aircraft reaches critical separation risk

UI rendering of alert indicators (handled by Radar/UI System)

Monitoring runway occupancy to detect runway incursion risk

Aircraft movement execution (handled by Aircraft Behaviour System)

Logging separation incidents to the Scoring & Assessment System

Persistent save storage or session file writing (handled by session management)

Monitoring wake turbulence separation between aircraft based on wake category and time-distance behind preceding aircraft

(existing entries remain)

The system ensures that all separation monitoring is read-only, continuous, and independent — the Conflict Detection System never modifies aircraft state and never issues instructions directly to aircraft.

1.  **Inputs / Outputs**

**Inputs**

**Outputs**

All active aircraft state data from the Airspace Management System (position, altitude, speed, heading, intention) — read each monitoring cycle

Advisory flag sent to the Communication System when two aircraft are converging but still safely separated

Monitoring cycle trigger from the simulation clock

Warning alert sent to the Communication System and Radar HUD when separation is decreasing toward minimum

Runway occupancy state from the Airspace Management System

Critical alert sent to the Communication System and Radar HUD when separation violation is imminent

Aircraft exit notifications from the Airspace Management System (to remove departed aircraft from monitoring)

Go-around command routed through the Communication System when a landing aircraft reaches critical separation risk

Separation incident log sent to the Scoring & Assessment System when a violation occurs

Wake turbulence categories from aircraft state (Light, Medium, Heavy, Super)

Wake turbulence advisory sent to Communication System when following aircraft is too close behind heavier aircraft

1.  **Data Model**

**Data / State**

**Owned By**

**Used By / Purpose**

All active aircraft positions, altitudes, speeds, and headings

Airspace Management System

Read by Conflict Detection each monitoring cycle to calculate separation distances and trajectories

Current alert level per aircraft pair (None, Advisory, Warning, Critical)

Conflict Detection System

Used to determine which alert tier to send to Communication System and Radar HUD

Trajectory projection data (calculated short-term future positions per aircraft)

Conflict Detection System (temporary, recalculated each cycle)

Used internally to predict separation violations before they occur

Runway occupancy state

Airspace Management System

Read by Conflict Detection to monitor runway incursion risk during approach sequences

Separation incident log

Scoring & Assessment System

Written to by Conflict Detection when a separation violation is confirmed

1.  **System Dependencies**

The Conflict Detection System depends on the following external systems:

**System / Component**

**Role / Interaction**

Airspace Management System

Primary data source — provides all aircraft state (position, altitude, speed, heading) each monitoring cycle for separation calculations

Simulation Clock

Provides the monitoring cycle trigger that drives continuous separation checks each tick

Communication System

Receives advisory flags and go-around commands from Conflict Detection for routing to the player and Aircraft Behaviour System

Scoring & Assessment System

Receives separation incident logs when a violation is confirmed

Radar / HUD System

Displays alert indicators (Advisory, Warning, Critical) to the player based on Conflict Detection output

1.  **System Communication**

**Source System**

**Trigger**

**Message / Call**

**Data Sent**

**Response**

**Outcome**

Simulation Clock

Each monitoring cycle

RunSeparationCheck()

None (internal trigger)

All aircraft states read from Airspace Management

Conflict Detection calculates separation distances and trajectory projections for all active aircraft pairs

Conflict Detection System

Advisory threshold crossed between two aircraft

SendAdvisoryFlag() to Communication System

Callsigns of both aircraft, current separation distance, alert level

Advisory received

Communication System displays advisory warning on player HUD; player can act before violation occurs

Conflict Detection System

Warning threshold crossed

SendWarningAlert() to Communication System and Radar HUD

Callsigns, separation distance, alert level

Warning received

Player HUD escalates alert display; player must act urgently

Conflict Detection System

Critical separation violation detected on approach path

TriggerGoAround() to Communication System

Callsign of landing aircraft

Go-around routed to Aircraft Behaviour System

Aircraft executes go-around; player HUD displays critical alert; Scoring System notified

Conflict Detection System

Separation violation confirmed (aircraft breached minimum separation)

LogSeparationIncident() to Scoring & Assessment System

Callsigns of both aircraft, separation distance at violation, timestamp

Incident logged

Scoring System records the violation, updates performance score, and adjusts difficulty scaling

This communication structure ensures that conflict detection operates continuously and independently, propagating separation alerts and incident logs consistently across the wider simulation loop without modifying aircraft state directly.

1.  **Flow Diagrams**

Separation Check Flow _(Flowchart12)_

Separation Violation Log Flow (Flowchart13)

Go-Around Flow (Flowchart14)

1.  **Edge Cases**

**Scenario**

**Cause**

**System Response**

**Outcome**

Aircraft exits the sector while a separation alert is active

Aircraft departs before the conflict resolves

Conflict Detection removes the departed aircraft from the monitoring set; alert is cleared

No orphaned alerts; remaining aircraft continue to be monitored normally

Two aircraft trigger simultaneous critical alerts

Multiple conflict pairs breach critical threshold at the same time

Each conflict pair is handled independently; go-around issued for any aircraft on approach; all alerts sent to Communication System

All critical situations are handled without one masking the other

Trajectory projection generates a false positive (aircraft projected to conflict but player intervenes)

Player issues heading change between projection and violation

Alert clears on the next monitoring cycle once recalculated trajectories show safe separation

No permanent false alert state; system recalculates every cycle

Monitoring cycle delayed by performance spike

High aircraft count causes processing delay

System uses last confirmed aircraft state until the next successful cycle completes

No invalid intermediate state; detection resumes on the next successful tick

Go-around triggered for an aircraft that has already landed

Race condition between landing confirmation and conflict alert

Go-around command is discarded — Aircraft Behaviour System validates that the aircraft is still airborne before executing

No erroneous go-around executed; aircraft remains in landed state

Aircraft breaches minimum separation but immediately recovers

Player issues correction instruction before the violation is fully logged

Violation is still logged to the Scoring System — separation was breached regardless of subsequent recovery

Accurate incident record maintained; player is scored on the violation

Wake turbulence advisory triggered behind heavy aircraft on approach

Light or Medium aircraft sequenced too close behind Heavy aircraft on same approach path

Advisory sent to Communication System; player must vector following aircraft for additional separation

Player issues correction; advisory clears once separation is restored

1.  **System Success Criteria**

These criteria are defined to evaluate both system correctness and player-facing clarity, ensuring the Conflict Detection System performs reliably while remaining responsive and meaningful to the player.

**#**

**Criteria**

**Expected Behaviour / Validation Outcome**

**1**

Separation Detection Accuracy

When two aircraft breach the minimum separation distance, the violation is detected and logged within the same monitoring cycle it occurs

**2**

Advisory Warning Timing

Advisory alerts are generated before aircraft breach minimum separation — giving the player meaningful time to respond

**3**

Three-Level Alert Escalation

Alert levels escalate correctly from Advisory to Warning to Critical as aircraft continue to converge — no level is skipped

**4**

Go-Around Triggering

When a landing aircraft reaches critical separation risk on approach, the go-around command is correctly triggered and routed to the Communication System

**5**

False Positive Clearance

When a developing conflict is resolved by player instruction, the alert clears on the next monitoring cycle after recalculation confirms safe separation

**6**

Read-Only State Access

The Conflict Detection System never modifies aircraft state, position, altitude, speed, or heading — all reads are non-destructive

**7**

Concurrent Pair Monitoring

Multiple simultaneous conflict pairs are monitored and alerted independently — one conflict does not suppress or delay alerts for another

**8**

Incident Logging

Every confirmed separation violation is correctly logged to the Scoring & Assessment System with callsigns, separation distance, and timestamp

**9**

Departed Aircraft Removal

When an aircraft exits the sector, it is immediately removed from the monitoring set and no further alerts are generated for it

**10**

Performance Stability

Separation calculations across all active aircraft pairs complete within the monitoring cycle without causing frame drops or simulation hitches

**Core Conflict Detection Responsibilities (Summary):**

*   _Continuously monitors all active aircraft pairs for separation distance violations each simulation tick_
*   _Projects short-term trajectories for all aircraft to predict violations before they occur_
*   _Generates three-level separation alerts (Advisory, Warning, Critical) and sends them to the Communication System and Radar HUD_
*   _Triggers go-around commands via the Communication System when a landing aircraft reaches critical separation risk_
*   _Monitors runway occupancy state to detect runway incursion risk_
*   _Logs confirmed separation violations to the Scoring & Assessment System with callsigns, distance, and timestamp_
*   _Never modifies aircraft state directly — all monitoring is read-only_

### Scoring and Assessment System

1.  **Description**

_The Scoring & Assessment System is responsible for tracking player performance throughout each simulation session. It receives separation incident logs from the Conflict Detection System, monitors successful landings, departures, and handoffs via the Airspace Management System, and tracks instruction efficiency through the Communication System. The system calculates a live performance score, logs all incidents with timestamps, and adjusts session difficulty by scaling the rate at which new aircraft enter the sector based on player competence. The session will begin with a clean score state and zero incidents._

1.  **System Purpose**

_The Scoring & Assessment System will centralise all performance measurement for the simulation, allowing the Conflict Detection, Airspace Management, and Communication systems to report events through a single assessment layer rather than managing performance tracking independently._

1.  **Design Rationale**

_I considered: distributed scoring (each system tracks its own performance metrics independently), event-driven centralised scoring (a single system receives event reports from all other systems), and real-time HUD-only scoring (no persistent score, just live feedback)._

_I followed established simulation design practice where performance measurement is separated from gameplay execution systems (Salen and Zimmerman, 2004)._

Alternatives Considered

Approach

Description

Pros

Cons

Distributed Scoring (Each System Tracks Its Own Metrics)

Conflict Detection, Airspace Management, and Communication each maintain their own performance counters independently

Faster local operations within each system. Reduced dependency on a central system. Simpler initial implementation

No single source of truth for player performance. Metrics could contradict each other across systems. Harder to calculate a unified session score. Difficulty scaling requires reading from multiple systems simultaneously

Real-Time HUD-Only Scoring (No Persistent Score)

Performance is shown to the player live but never stored, calculated, or used for difficulty scaling

Minimal implementation. No additional system required

No difficulty scaling — session never adapts to player competence. No incident log for post-session review. No portfolio evidence of assessment system design

Event-Driven Centralised Scoring _(Chosen)_

A single Scoring & Assessment System receives event reports from all other systems. Calculates a unified performance score, logs all incidents, and adjusts difficulty based on player competence

Single source of truth for all performance data. Unified score calculation across all event types. Supports difficulty scaling from one authoritative system. Clean separation from gameplay execution systems

More complex than distributed tracking. Introduces a central dependency for all performance reporting. Requires all systems to correctly report events

These alternatives were evaluated against criteria including consistency of performance data, ability to calculate a unified session score, support for difficulty scaling, and ease of debugging when scores behave unexpectedly.

**Evaluation Criteria:**

*   _Consistency of performance data across all reporting systems_
*   _Ability to calculate a unified session score from multiple event types_
*   _Support for difficulty scaling based on real-time performance data_
*   _Ease of debugging when scores behave unexpectedly_

**Justification Used:**

_“A data-driven architecture is what differentiates a game engine … When a game contains hard-coded logic or game rules or employs special-case code to render specific types of game objects, it becomes difficult or impossible to reuse that software to make a different game.”_ (Gregory, 2014, p. 11)

_This quote supported my decision to treat the Scoring & Assessment System as a centralised event receiver rather than a distributed tracker. Every gameplay event in CLEARANCE — separation violations, successful landings, missed handoffs — needs to contribute to one consistent performance score. Routing all scoring through a single system gave me reliable, consistent performance data without coupling scoring logic to execution systems._

_Mini Airways (Truong, 2023) reinforced this — its scoring system responds directly to player decisions and scales difficulty based on performance. I applied the same thinking to CLEARANCE, using confirmed simulation events from authoritative systems as the sole source of truth for all performance measurement._

**Evaluation Summary:**

I chose the event-driven centralised scoring approach because it gives me the strongest combination of performance consistency, difficulty scaling capability, and scalability across the simulation pipeline. Compared with distributed or HUD-only approaches, this eliminates the risk of contradictory metrics across systems and ensures that the player always receives accurate, meaningful performance feedback. The event-driven approach is justified because it maps directly to how simulation training assessment works — every confirmed event produces a scored outcome.

1.  **System Boundaries**

The Scoring & Assessment System is the sole authority over player performance data within the session. It receives event reports but never modifies aircraft state or issues instructions.

This follows common simulation architecture principles where a single authoritative system measures performance and prevents contradictory scoring across systems (Gregory, 2014).

**Responsible For**

**Not Responsible For**

Receiving and logging all separation incident reports from the Conflict Detection System

Detecting separation violations (Conflict Detection System)

Receiving and logging successful landing, departure, and handoff events from the Airspace Management System

Rendering the score display or HUD elements (Radar/UI System)

Calculating and maintaining the player's live performance score throughout the session

Modifying aircraft state, issuing instructions, or executing movement (other systems)

Adjusting session difficulty by scaling aircraft spawn rate based on player performance

Managing aircraft registration or sector state (Airspace Management System)

Generating the post-session performance summary

Persistent save storage or session file writing (handled by session management)

The Scoring & Assessment System acts as the sole authority over player performance data — the system never modifies aircraft state, issues instructions, or reads aircraft position directly. All data arrives via event reports from authoritative systems.

1.  **Inputs / Outputs**

**Inputs**

**Outputs**

Separation incident log from the Conflict Detection System (callsigns, distance, timestamp)

Updated live performance score sent to the Radar HUD

Successful landing confirmation from the Airspace Management System (callsign, timestamp)

Difficulty scaling output — adjusted aircraft spawn rate sent to the sector spawn controller

Successful departure confirmation from the Airspace Management System (callsign, timestamp)

Session incident log — full record of all scored events with timestamps

Missed handoff notification from the Airspace Management System (callsign, timestamp)

Post-session performance summary displayed to the player at session end

Instruction efficiency data from the Communication System (instructions issued vs minimum necessary)

Difficulty level indicator sent to the Radar HUD

1.  **Data Model**

**Data / State**

**Owned By**

**Used By / Purpose**

Separation incident records (callsigns, distance, timestamp)

Scoring & Assessment System

Logged when reported by Conflict Detection; used to calculate score deductions and populate session summary

Successful event records (landings, departures, handoffs)

Scoring & Assessment System

Logged when reported by Airspace Management; used to calculate score awards

Current live performance score

Scoring & Assessment System

Sent to Radar HUD each time it updates; used for difficulty scaling calculations

Difficulty scaling factor

Scoring & Assessment System

Sent to sector spawn controller to adjust aircraft entry rate based on current performance

All active aircraft state data

Airspace Management System

Never read directly by Scoring System — all data arrives via event reports

1.  **System Dependencies**

The Scoring & Assessment System depends on the following external systems:

**System / Component**

**Role / Interaction**

Conflict Detection System

Sends confirmed separation violations, go-around triggers, and alert level flags for incident logging and score deduction

Airspace Management System

Provides aircraft state data (position, altitude, speed, callsign) used to validate handoffs, landings, and departures for scoring

Communication System

Sends confirmed player instruction events (valid clearances issued) used to track command count and efficiency rating

Aircraft Behaviour System

Confirms successful execution of manoeuvres (landing, departure, go-around) used to award performance points

1.  **System Communication**

**Source System**

**Trigger**

**Message / Call**

**Data Sent**

**Response**

**Outcome**

Conflict Detection System

Separation violation confirmed

LogViolation()

Callsign pair, distance, alert level, timestamp

Incident recorded

Score deducted; incident added to session log

Conflict Detection System

Go-around triggered

LogGoAround()

Callsign, reason (conflict/runway incursion)

Go-around logged

Counted against player efficiency rating

Communication System

Valid clearance issued by player

LogInstruction()

Callsign, instruction type, timestamp

Instruction recorded

Added to total command count for efficiency calculation

Airspace Management System

Aircraft lands successfully

LogLanding()

Callsign, runway, timestamp

Landing recorded

Score awarded; aircraft removed from active session

Airspace Management System

Aircraft departs successfully

LogDeparture()

Callsign, destination, timestamp

Departure recorded

Score awarded; aircraft removed from active session

Scoring & Assessment System

Score threshold crossed

AdjustDifficulty()

Current score, incident count, efficiency rating

Spawn rate updated

Aircraft spawn rate scaled up or down based on player performance

This communication structure ensures that scoring and assessment operates continuously and independently, logging incidents and performance events consistently without modifying aircraft state or simulation flow directly.

1.  **Flow Diagrams**

Score Event Log Flow _(Flowchart15)_

Difficulty Adjustment Flow (Flowchart16)

Session Summary Flow (Flowchart17)

1.  **Edge Cases**

**Scenario**

**Cause**

**System Response**

**Outcome**

Violation logged but aircraft has already exited the sector

Aircraft leaves before log write completes

System records the incident using last known state at time of flag

Incident preserved in session log; score deducted correctly

Multiple violations triggered in the same tick

Rapid conflict escalation between several aircraft pairs

Each violation logged independently with its own timestamp and callsign pair

All incidents recorded; no events dropped or merged

Score update causes difficulty adjustment mid-session

Player performance crosses spawn-rate threshold

Difficulty scaling applied on next spawn cycle only; current aircraft unaffected

Smooth difficulty transition without disrupting live traffic

Session ends while a violation is still being processed

Player quits or session timeout occurs mid-log

Partial log entry discarded; only fully confirmed events written to session summary

Session summary reflects only validated, complete events

Player issues a correct instruction after a violation is already logged

Correction arrives too late

Instruction logged as valid command; violation record unchanged

Efficiency score updated; violation remains on record

Duplicate landing event received

Race condition between Aircraft Behaviour and Airspace Management confirmation

System checks callsign against already-landed list; duplicate discarded

Score awarded once only; no double-counting

1.  **System Success Criteria**

These criteria are defined to evaluate both system correctness and player-facing clarity, ensuring the Scoring & Assessment System performs reliably while remaining accurate and meaningful to the player.

**#**

**Criteria**

**Expected Behaviour / Validation Outcome**

**1**

Violation Logging Accuracy

When a separation violation is confirmed by Conflict Detection, the incident is logged within the same simulation tick with the correct callsign pair, distance, and timestamp

**2**

Landing and Departure Scoring

When an aircraft successfully lands or departs, a score event is recorded and the correct point value applied to the session total without duplication

**3**

Efficiency Rating Calculation

The efficiency rating updates correctly after each player instruction, reflecting the ratio of valid commands issued to total aircraft handled

**4**

Difficulty Scaling Trigger

When the player's score crosses a defined threshold, the aircraft spawn rate adjusts on the next spawn cycle without affecting aircraft already active in the session

**5**

Go-Around Penalty Logging

When a go-around is triggered, it is logged against the correct callsign and counted in the session's incident total

**6**

Session Summary Accuracy

At session end, the summary reflects only fully confirmed events — no partial or duplicate entries appear in the final score breakdown

**7**

Score Persistence

The running score total is maintained correctly across the full session, with each event incrementing or decrementing the score without overwriting or resetting previous values

**Core Scoring & Assessment Responsibilities**

*   Receives confirmed separation violation flags from the Conflict Detection System and logs each incident with callsign pair, distance, alert level, and timestamp
*   Receives landing and departure confirmation events from the Airspace Management System and awards the correct score value for each
*   Receives valid player instruction events from the Communication System and tracks total command count for efficiency rating calculation
*   Calculates and maintains a running session score, updating in real time as events are received
*   Adjusts aircraft spawn rate difficulty based on player performance score, applying scaling on the next spawn cycle only
*   Logs go-around events against the responsible callsign and counts them in the session incident total
*   Generates a session summary at end of play reflecting only fully confirmed, validated events — no partial or duplicate entries