**CLEARANCE**
**Requirements and MVP Definition**
**Project:** CLEARANCE
**Type:** First-person Air Traffic Control simulation
**Author:** Abdullah Ameed Abduljabbar
**Purpose:** Define the minimum viable version of CLEARANCE before full production programming begins.

**MVP goal**

Item

Details

MVP goal

Prove that CLEARANCE’s core simulation loop works end-to-end

Success condition

If the loop works reliably, the project has a valid simulation foundation

**Core simulation loop**

**Step**

**Action**

**1**

**Aircraft enters the sector**

**2**

**Player reads aircraft state**

**3**

**Player issues an instruction**

**4**

**Aircraft executes that instruction gradually**

**5**

**Airspace state updates correctly**

**6**

**Conflict Detection monitors separation**

**7**

**Incidents and outcomes are logged**

**8**

**Aircraft exits, lands, or remains under control**

**Core MVP requirements**

**3.1 Aircraft registration and authoritative state**

**Requirement area**

**Details**

**Registration**

**System must register aircraft with callsign, position, altitude, speed, heading, and flight phase**

**State ownership**

**Airspace Management System must be the single source of truth**

**Access rule**

**Other systems must query or request updates through Airspace Management rather than storing their own authoritative copies**

**Removal**

**Aircraft must be removable when they exit the sector, land, or the session resets**

**3.2 Aircraft movement and behaviour**

**Requirement area**

**Details**

**Heading change**

**Must be supported**

**Altitude change**

**Must be supported**

**Speed change**

**Must be supported**

**Movement style**

**State changes must occur over time using constrained movement rates**

**State update**

**Updated aircraft state must be committed back into Airspace Management each tick**

**Performance limits**

**Aircraft must respect service ceiling and minimum/maximum operating speed per type**

**Bank angle limit**

**Turn rates must be derived from bank angle limited by aircraft performance category**

**Climb rate limit**

**Climb and descent rates must be limited by aircraft performance category**

**3.3 Player instruction pipeline**

**Requirement area**

**Details**

**Targeting**

**Player must be able to target a specific aircraft by callsign**

**Minimum instruction set**

**Heading change, altitude change, speed change**

**Validation**

**Instructions must be validated against current aircraft state before execution**

**Failure handling**

**Invalid instructions must be rejected safely with a result state**

**Physics validation**

**Instructions must be rejected if they exceed aircraft service ceiling, fall below minimum operating speed, or exceed maximum operating speed**

**3.4 Conflict detection**

**Requirement area**

**Details**

**Monitoring**

**System must compare active aircraft positions and altitudes**

**Detection**

**Must identify horizontal and vertical separation breaches**

**Alerting**

**Must support advisory, warning, and critical alert levels**

**Output**

**Conflict events must be surfaced to the simulation and be available for scoring/logging**

**Wake turbulence separation**

**Must monitor following distance behind heavier aircraft and generate advisory when separation falls below wake category minimum**

**3.5 Session scoring and incident logging**

**Requirement area**

**Details**

**Incident logging**

**Must log separation loss, unresolved exit / missed handoff, go-around triggered, and successful landing or departure if implemented**

**Score tracking**

**Must track a session score or assessment value**

**Reviewability**

**Must expose a readable session log for later review**

**3.6 Session orchestration**

**Requirement area**

**Details**

**Session control**

**Must support session start**

**Runtime**

**Must support active tick-based simulation**

**Session state control**

**Must support pause/resume or at minimum stop/end**

**Orchestration**

**Session controller must manage subsystem update order reliably**

**MVP feature set**

**Included in MVP**

**Status**

**Airspace Management System**

**Included**

**Aircraft Behaviour System**

**Included**

**Communication / instruction routing**

**Included**

**Instruction validation**

**Included**

**Conflict Detection System**

**Included**

**Scoring / incident logging**

**Included**

**Aircraft spawning into sector**

**Included**

**Simulation controller / orchestration**

**Included**

**Basic radar-state readability**

**Included**

**Aircraft performance categories (Light, Medium, Heavy, Super)**

**Included**

**Service ceiling and speed envelope enforcement**

**Included**

**Bank angle limited turn rates**

**Included**

**Wake turbulence separation monitoring**

**Included**

**Active runway selection based on wind direction**

**Included**

**Out scope for MVP**

**Feature**

**Status**

**Full voice communication simulation**

**Out of scope**

**Real speech recognition or phraseology parsing**

**Out of scope**

**Full procedural SID/STAR realism**

**Out of scope**

**Full aerodynamic aircraft physics**

**Out of scope**

**Advanced weather systems**

**Out of scope**

**Complex airport ground control systems**

**Out of scope**

**Large-scale commercial UI polish**

**Out of scope**

**Tutorialisation, onboarding, or final UX presentation polish**

**Out of scope**

**Multiplayer or networked ATC**

**Out of scope**

**Full replay system beyond basic logging**

**Out of scope**

**Highly advanced projected trajectory prediction beyond a simple first implementation**

**Out of scope**

**Full aerodynamic physics (lift, drag, angle of attack, stall modelling)**

**Out of scope**

**Wind drift affecting ground track**

**Out of scope (post-MVP enhancement)**

**Atmospheric density variation with altitude**

**Out of scope (post-MVP enhancement)**

**Engine thermodynamics and fuel consumption**

**Out of scope**

**Crosswind landing physics**

**Out of scope**

**Player actions required in MVP**

**Player action**

**Required**

**Observe active aircraft in the sector**

**Yes**

**Identify aircraft by callsign and current state**

**Yes**

**Select an aircraft**

**Yes**

**Issue a heading instruction**

**Yes**

**Issue an altitude instruction**

**Yes**

**Issue a speed instruction**

**Yes**

**Receive feedback on accepted or rejected instruction**

**Yes**

**Continue issuing instructions while multiple aircraft are active**

**Yes**

**Simulation outcomes required in MVP**

**Outcome**

**Required**

**Aircraft enters sector successfully**

**Yes**

**Aircraft updates correctly over time**

**Yes**

**Aircraft responds gradually to valid player input**

**Yes**

**Invalid input is rejected safely**

**Yes**

**Aircraft can approach or exit sector state if implemented**

**Yes**

**Conflicts can be detected between aircraft**

**Yes**

**Conflict or incident data is logged**

**Yes**

**Session scoring updates based on simulation outcomes**

**Yes**

**Aircraft respect performance envelope limits**

**Yes**

**Wake turbulence advisories generate correctly**

**Yes**

**Active runway changes when wind direction shifts**

**Yes**

**Technical requirements**

**Technical constraint**

**Details**

**Core implementation language**

**Simulation logic should be primarily in C++ rather than Blueprint-only logic**

**Conflict monitoring**

**Airspace Management System must remain the only authoritative owner of aircraft state**

**Movement authority**

**Aircraft movement must only be executed through the Aircraft Behaviour System**

**Conflict monitoring**

**Conflict Detection must operate as a read-only monitoring system over authoritative state**

**Instruction gatekeeping**

**Communication System must validate instructions before execution**

**System communication**

**Subsystems should communicate through clean references and delegates rather than ad hoc direct mutation where possible**

**MVP success criteria**

**#**

**Success criterion**

**1**

**Aircraft can spawn/register into the sector successfully**

**2**

**Aircraft state is stored and queried correctly from a central authority**

**3**

**Player can issue heading, altitude, and speed instructions to aircraft**

**4**

**Valid instructions result in gradual movement changes over time**

**5**

**Invalid instructions are rejected safely and clearly**

**6**

**Multiple aircraft can exist simultaneously without corrupting state ownership**

**7**

**Conflict Detection can identify separation problems using the same authoritative state seen by the rest of the simulation**

**8**

**Incidents and key outcomes are logged to the scoring/assessment system**

**9**

**Session can be started, progressed, and ended through a stable orchestration layer**

**10**

**Simulation is demonstrably playable as a coherent ATC decision loop rather than isolated technical prototypes**

**11**

**Aircraft cannot be instructed beyond their performance envelope (service ceiling, speed limits)**

**12**

**Wake turbulence separation is monitored and advisories trigger correctly behind heavier aircraft**

**13**

**Active runway selection responds correctly to changes in wind direction**

**Build priority**

**Priority**

**System / task**

**1**

**Core data structures and enums**

**2**

**Airspace Management System**

**3**

**Aircraft Behaviour System**

**4**

**Instruction Validator**

**5**

**Communication Router**

**6**

**Conflict Detection**

**7**

**Scoring / incident logging**

**8**

**Aircraft Spawner**

**9**

**Simulation Controller**

**10**

**Minimal player-facing UI / debug interface**

**Non-MVP future expansion**

**Future expansion**

**Phase**

**Projected conflict look-ahead improvements**

**Post-MVP**

**Approach clearance and landing sequence expansion**

**Post-MVP**

**Go-around logic refinement**

**Post-MVP**

**Better radar visualisation and ATC UI**

**Post-MVP**

**Replay/debrief improvements using session logs**

**Post-MVP**

**More realistic traffic patterns, sector rules, and difficulty progression**

**Post-MVP**

**Wind drift effects on aircraft ground track**

**Post-MVP**

**Atmospheric density variation reducing climb performance at altitude**

**Post-MVP**

**Full aerodynamic physics for advanced training scenarios**

**Post-MVP**

**Crosswind component limits affecting approach feasibility**

**Post-MVP**

**Definition of done**

**Done condition**

**Required state**

**Real-time management**

**Player can manage multiple aircraft in real time**

**Aircraft behaviour**

**Aircraft respond gradually and predictably**

**Separation model**

**Separation can be lost or maintained**

**Assessment**

**Incidents are logged and scored**

**End-to-end stability**

**Full loop operates in one stable session build**