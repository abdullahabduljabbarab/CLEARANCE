**CLEARANCE**
**Technical Architecture**
**Project:** CLEARANCE
**Type:** First-Person Air Traffic Control Simulation
**Engine:** Unreal Engine 5
**Primary Implementation Language:** C++
**Purpose:** Define the implementation architecture for CLEARANCE before programming begins, including class ownership, tick order, data flow, C++/Blueprint boundaries, delegate communication, and object lifecycle.

**1\. Purpose**
This document defines the technical architecture for CLEARANCE as a C++-driven Unreal Engine 5 simulation project \[1\]\[2\]. It exists to formalise which runtime classes own state, which runtime classes read or transform that state, how the simulation updates each frame, how systems communicate, and how Unreal object lifecycles are managed during a session \[1\]\[2\].
This architecture is intended to prevent ambiguous responsibilities, duplicated authority, and ad hoc cross-system mutation before implementation begins \[1\]\[2\].

**2\. Architectural principles**
The technical architecture is built around the following principles:
**• Single source of truth:** all aircraft state is owned by one class only \[1\]\[2\].
**• Single movement executor:** aircraft motion is only executed by behaviour objects \[1\]\[2\].
**• Read-only analysis:** conflict detection operates on snapshots, not mutable aircraft state \[2\].
**• Validation before execution:** instructions must be checked before they can affect simulation state \[2\].
**• Controller-driven update order:** runtime sequencing is controlled centrally \[2\].
**• Strict presentation boundary:** simulation logic remains in C++, while Blueprint is used only for player-facing UI and presentation \[2\].
These principles are implemented through the class ownership model, tick pipeline, data flow rules, delegate map, and memory/lifecycle rules defined below.

*   Physics-based aircraft behaviour: aircraft motion respects performance category limits and atmospheric conditions through validated physics constraints \[1\]\[2\].

**3\. Class ownership model**
This section defines which classes own state and which classes read it.

**3.1 AClearanceAirspaceManager**
**Type:**AActor \[2\]
**Role:** authoritative aircraft state owner \[1\]\[2\]
**Owns:**all active aircraft state and sector environmental state including wind direction, wind speed, and active runway selection \[1\]\[2\]

**Reads:** incoming state update requests, aircraft registration data, read requests from dependent systems, wind condition updates \[1\]\[2\]

AClearanceAirspaceManager owns all aircraft state because the simulation requires one authoritative airspace registry containing callsign, position, altitude, speed, heading, and phase for every active aircraft \[1\]\[2\]. It is implemented as an AActor because it needs world presence and Tick participation as a persistent runtime simulation authority \[2\].

The Airspace Manager also owns sector environmental state because runway selection depends on wind direction and crosswind component calculations affecting all aircraft operating in the sector \[2\]. Environmental state is read by Aircraft Behaviour for wind drift calculations, by the Validator for approach feasibility checks, and by player-facing UI to display current conditions \[2\].

No other class is permitted to own an alternative authoritative aircraft state store \[1\].
**3.2 UClearanceAircraftBehaviour**
**Type:**UObject \[2\]
**Role:** per-aircraft movement executor \[1\]\[2\]
**Owns:** per-aircraft instruction execution state, queued instructions, transient movement state, physics state including current bank angle and performance category limits \[2\]

**Reads:** current aircraft state supplied from authoritative airspace state \[2\]
UClearanceAircraftBehaviour is a UObject per aircraft owned by the Simulation Controller because it does not need world presence, transform, collision, or Actor-level identity \[2\]. It only needs tick-driven updates called by the controller \[2\].

Aircraft Behaviour applies flight dynamics during instruction execution including turn rate calculation from bank angle limits, climb and descent rate enforcement per performance category, speed envelope checking against minimum and maximum operating speeds, and wind drift effects on ground track \[2\]. Physics calculations occur during the per-aircraft tick before state is committed back to the Airspace Manager \[2\].

It is not an airspace owner. Its job is to take validated instructions and gradually transform aircraft state, then push those changes back to the Airspace Manager \[1\]\[2\].
**3.3 UClearanceConflictDetector**
**Type:**UObject \[2\]
**Role:** pure conflict analysis logic \[2\]
**Owns:** active conflict tracking state, active wake turbulence advisory tracking \[2\]

**Reads:** read-only aircraft snapshots from Airspace Manager \[2\]
UClearanceConflictDetector is a UObject because it is pure logic operating on snapshots and does not need world presence \[2\].

Conflict Detection includes wake turbulence monitoring as a third dimension alongside distance separation and trajectory projection \[2\]. Wake calculations read aircraft wake category from authoritative state and apply the ICAO category-pair separation matrix to identify violations behind heavier aircraft \[2\].

It does not own aircraft state and does not tick independently as an Actor. It is called by the Simulation Controller during the controller tick \[2\].

**3.4 UClearanceInstructionValidator**
**Type:**UObject \[2\]
**Role:** validation logic \[2\]
**Owns:** no persistent state \[2\]
**Reads:** current aircraft state, proposed instruction, aircraft performance limits including service ceiling and operating speed envelope \[2\]

UClearanceInstructionValidator is a stateless UObject because it validates instructions without storing runtime simulation state \[2\]. It does not need Tick, world presence, or ownership of any persistent aircraft data \[2\].

The Validator enforces physics-based constraints during validation including service ceiling checks against requested altitude, operating speed envelope checks against requested speed, and bank angle feasibility checks against requested turn rates \[2\]. Invalid physics instructions are rejected with reasons before reaching the Aircraft Behaviour System \[2\].

**3.5 UClearanceScoring**
**Type:**UObject \[2\]
**Role:** session scoring and assessment \[2\]
**Owns:** session log, score, efficiency metrics, difficulty state \[2\]
**Reads:** events emitted by simulation systems \[2\]
UClearanceScoring is a UObject because it owns session data but does not need world presence \[2\]. It is a session-level logic object, not a world-space actor \[2\].
**3.6 AClearanceAircraftSpawner**
**Type:**AActor \[2\]
**Role:** aircraft entry and spawn control \[2\]
**Owns:** spawn timing, spawn configuration, sector-entry-related state \[2\]
**Reads:** difficulty updates, spawn settings, sector rules \[2\]
AClearanceAircraftSpawner is an AActor because it needs world position for sector entry points and may depend on world-space spawning logic \[2\]. It also participates in session timing through Actor Tick \[2\].
**3.7 AClearanceSimulationController**
**Type:**AActor \[2\]
**Role:** simulation orchestration \[2\]
**Owns:** subsystem references, session state, behaviour-object lifecycle map \[2\]
**Reads:** all major subsystem outputs and delegate notifications \[2\]
AClearanceSimulationController is an AActor because it orchestrates everything through Tick \[2\]. It does not replace system ownership boundaries, but it does control runtime sequencing, subsystem creation, delegate binding, and the lifecycle of UObject-based systems \[2\].
**4\. Tick architecture**
This section defines what ticks and in what order.
**4.1 Actor tick participants**
Only the following classes tick as Actors:
• AClearanceSimulationController \[2\]
• AClearanceAirspaceManager \[2\]
• AClearanceAircraftSpawner \[2\]
These are the only direct Actor Tick participants in the simulation layer \[2\].
**4.2 Controller-called systems**
Everything else is called by the Simulation Controller during its tick in dependency order:
• UClearanceAircraftBehaviour \[2\]
• UClearanceConflictDetector \[2\]
• UClearanceInstructionValidator \[2\]
• UClearanceScoring \[2\]
• UClearanceCommsRouter \[2\]
These classes do not independently tick as Actors \[2\].
**4.3 Authoritative tick pipeline**
The tick sequence defined in the scaffold comments becomes the authoritative tick pipeline for this architecture \[2\]:
1\. The Spawner checks whether a new aircraft should enter the sector \[2\].
2\. Current aircraft state is pulled from the Airspace Manager \[2\].
3\. All Aircraft Behaviour objects update heading, altitude, speed, approach, or go-around motion with flight dynamics applied including bank angle limits, climb rate per category, and wind drift effects \[2\].

4\. Updated aircraft states are committed back to the Airspace Manager \[2\].
5\. The Conflict Detector runs on the committed snapshot for distance separation, trajectory projection, and wake turbulence separation \[2\].
6\. Go-around triggers and advisories are routed through the Comms Router \[2\].
7\. Scoring logs incidents and updates session assessment state \[2\].
8\. Difficulty adjustment is checked and broadcast \[2\].
9\. UI is notified through delegates and Blueprint-facing reads \[2\].
This sequence is authoritative and must not be bypassed because later systems depend on the outputs of earlier systems \[2\].
**5\. Data flow**
This section defines which direction data moves between systems.
**5.1 Airspace Manager outward flow**
The Airspace Manager provides **read-only snapshots outward** \[1\]\[2\]. These snapshots are read by:
• the Communication System for instruction validation \[2\],
• the Conflict Detection System for safety analysis \[2\],
• the Scoring System for aircraft/event context \[1\]\[2\],
• the radar display and player-facing UI through BlueprintCallable access \[1\]\[2\].

*   The Aircraft Behaviour System for sector environmental conditions affecting wind drift and active runway \[2\]

**5.2 Aircraft Behaviour inward flow**
Aircraft Behaviour pushes **state updates inward** to the Airspace Manager \[1\]\[2\]. It receives instructions, executes gradual motion, and submits new aircraft state back to the central authority \[2\].
**5.3 Communication flow**
The Communication System:
• reads current aircraft state from the Airspace Manager \[2\],
• validates instructions against that current state \[2\],
• routes valid instructions to Aircraft Behaviour \[2\].
It does not directly mutate aircraft state \[2\].
**5.4 Conflict Detection flow**
Conflict Detection:
• reads aircraft state from the Airspace Manager \[2\],
• evaluates distance separation, projected conflicts, and wake turbulence separation based on aircraft category pairs \[2\]

• broadcasts alerts and go-around requirements through delegates \[2\].
It does not write back into airspace state directly \[2\].
**5.5 Scoring flow**
Scoring receives events from:
• Conflict Detection \[2\],
• Airspace-related outcomes such as aircraft registration/deregistration or exit/landing context routed through system events \[1\]\[2\].
It logs and evaluates the session, but it does not directly change aircraft state \[2\].
**5.6 No-bypass rule**
No system bypasses the defined flow \[1\]\[2\]. Specifically:
• no UI may mutate simulation state directly,
• no Conflict Detection logic may directly reposition aircraft,
• no scoring logic may alter aircraft state,
• no behaviour object may become a replacement source of truth,
• no dependent system may store its own authoritative copy of aircraft state \[1\]\[2\].
**6\. Blueprint versus C++ boundary**
This section defines the implementation boundary between systems code and presentation code.
**6.1 C++ simulation layer**
All simulation logic stays in C++ \[2\]. That includes:
• airspace state ownership,
• aircraft movement execution,
• instruction validation,
• instruction routing,
• conflict detection,
• scoring,
• spawning,
• simulation orchestration \[2\].
There is to be **no simulation logic in Blueprint**.
**6.2 Blueprint presentation layer**
Blueprint handles player-facing UI only \[2\]. That includes:
• radar display,
• aircraft data tags,
• alert display,
• player input widgets,
• menu/HUD presentation,
• any other purely visual presentation logic \[2\].
Radar display reads from C++ through BlueprintCallable functions \[2\]. Instruction input from the player enters through Blueprint calling BlueprintCallable methods on the Comms Router or Simulation Controller \[2\].
**6.3 Clean boundary rule**
The boundary is deliberately clean:
**• Blueprint is the presentation layer.**
**• C++ is the simulation layer.**
There is no simulation logic in Blueprint and no UI logic in C++ \[2\].
**7\. Delegate map**
This section turns the scaffold delegates into a formal communication map \[2\].
**7.1 Airspace Manager delegates**
Delegate
Broadcast by
Listened to by
Purpose
OnAircraftRegistered
Airspace Manager \[2\]
Simulation Controller \[2\]
Create/register the per-aircraft behaviour object
OnAircraftDeregistered
Airspace Manager \[2\]
Simulation Controller \[2\]
Remove and destroy the per-aircraft behaviour object
OnAircraftStateUpdated
Airspace Manager \[2\]
UI/debug listeners, optional controller listeners \[2\]
Surface committed aircraft state updates

OnRunwayChanged

Airspace Manager \[2\]

Spawner, UI \[2\]

Notify systems that active runway has changed due to wind shift
**7.2 Conflict Detection delegates**
Delegate
Broadcast by
Listened to by
Purpose
OnConflictDetected
Conflict Detector \[2\]
Comms Router, Scoring \[2\]
Trigger advisories and log conflict events
OnConflictResolved
Conflict Detector \[2\]
Scoring, optional UI listeners \[2\]
Log a successful conflict resolution
OnGoAroundRequired
Conflict Detector \[2\]
Comms Router \[2\]
Route a go-around through the defined command path

OnWakeTurbulenceAdvisory

Conflict Detector \[2\]

Comms Router, Scoring \[2\]

Trigger wake separation advisory and log event
**7.3 Communication delegates**
Delegate
Broadcast by
Listened to by
Purpose
OnInstructionResult
Comms Router \[2\]
UI, optional controller listeners \[2\]
Show accepted/rejected instruction feedback
OnAdvisoryWarning
Comms Router \[2\]
UI \[2\]
Show player-facing advisories
**7.4 Scoring delegates**
Delegate
Broadcast by
Listened to by
Purpose
OnScoreUpdated
Scoring \[2\]
UI \[2\]
Update score display
OnDifficultyAdjusted
Scoring \[2\]
Spawner \[2\]
Adjust spawn pacing and difficulty
This full delegate map should be documented and fixed before implementation expands beyond the core MVP \[2\].
**8\. Memory and lifecycle**
This section defines how objects are created and destroyed.
**8.1 Actors**
Actors are spawned by the world \[2\]. This includes:
• AClearanceSimulationController \[2\]
• AClearanceAirspaceManager \[2\]
• AClearanceAircraftSpawner \[2\]
These persist at world/session scope.
**8.2 UObjects**
UObjects are created with NewObject and owned by the Simulation Controller \[2\]. This includes:
• UClearanceAircraftBehaviour \[2\]
• UClearanceConflictDetector \[2\]
• UClearanceInstructionValidator \[2\]
• UClearanceScoring \[2\]
• UClearanceCommsRouter \[2\]
**8.3 Aircraft Behaviour lifecycle**
Aircraft Behaviour instances are created when aircraft register and destroyed when aircraft deregister \[2\].
The lifecycle is:
1\. Airspace Manager registers an aircraft and broadcasts OnAircraftRegistered \[2\].
2\. Simulation Controller receives the event \[2\].
3\. The controller creates a UClearanceAircraftBehaviour with NewObject \[2\].
4\. The object is stored in the controller’s TMap<FName, UClearanceAircraftBehaviour\*> BehaviourMap \[2\].
5\. When the aircraft deregisters, Airspace Manager broadcasts OnAircraftDeregistered \[2\].
6\. The controller removes the behaviour object from the map \[2\].
7\. Once no longer referenced, the UObject may be cleaned up normally by Unreal object lifetime rules.
The TMap in the Simulation Controller manages this lifecycle \[2\].
**8.4 Session lifecycle**
At session start:
• world actors exist or are spawned,
• controller-owned UObjects are created,
• delegates are bound,
• simulation state is initialised \[2\].
At session end or reset:
• simulation activity halts,
• the Airspace Manager clears airspace state \[2\],
• the controller clears BehaviourMap \[2\],
• Conflict Detection clears active conflict tracking \[2\],
• Scoring resets session data \[2\],
• Spawner timers and difficulty state reset \[2\].

•Environmental state resets to default wind conditions and active runway \[2\]
This ensures no stale behaviours, conflicts, or scoring state survive into the next run.
**9\. Suggested source structure**
A source layout matching the architecture is:
Source/CLEARANCE/

├── Core/

│ ├── CLEARANCETypes.h

│ ├── ClearanceDelegates.h

│ └── ClearanceConstants.h

├── Airspace/

│ ├── ClearanceAirspaceManager.h

│ ├── ClearanceAirspaceManager.cpp

│ └── ClearanceSectorEnvironment.h

├── Aircraft/

│ ├── ClearanceAircraftBehaviour.h

│ ├── ClearanceAircraftBehaviour.cpp

│ ├── ClearanceAircraftSpawner.h

│ ├── ClearanceAircraftSpawner.cpp

│ ├── ClearanceFlightDynamics.h

│ └── ClearanceFlightDynamics.cpp

├── Comms/

│ ├── ClearanceCommsRouter.h

│ ├── ClearanceCommsRouter.cpp

│ ├── ClearanceInstructionValidator.h

│ └── ClearanceInstructionValidator.cpp

├── Safety/

│ ├── ClearanceConflictDetector.h

│ ├── ClearanceConflictDetector.cpp

│ └── ClearanceWakeSeparation.h

├── Scoring/

│ ├── ClearanceScoring.h

│ └── ClearanceScoring.cpp

├── Simulation/

│ ├── ClearanceSimulationController.h

│ └── ClearanceSimulationController.cpp

└── UI/

├── Radar widgets

└── Blueprint presentation layer

**10\. Definition of readiness**
The technical architecture is ready for implementation when:
• class ownership is fixed,
• Actor tick participation is fixed,
• controller-called dependency order is fixed,
• data flow is fixed,
• Blueprint/C++ boundaries are fixed,
• the full delegate map is fixed,
• and lifecycle rules are fixed \[2\].

• physics constraints including performance categories, service ceilings, operating speed envelopes, wake separation matrix, and environmental modelling are fixed \[2\]