# Scenario Design

**Project:** CLEARANCE
**Author:** Abdullah Ameed Abduljabbar
**Role:** Systems Designer

## Table of contents

- [Purpose](#purpose)
- [How to read this document](#how-to-read-this-document)
- [Scenario progression](#scenario-progression)
- [Scenario template](#scenario-template)
- [Baltic Intercept](#baltic-intercept)
- [Hijack Response](#hijack-response)
- [Mass Divert](#mass-divert)
- [Mayday Engine Fire](#mayday-engine-fire)
- [NORDO Inbound](#nordo-inbound)
- [Cold War Probe](#cold-war-probe)
- [Mixed Ops](#mixed-ops)
- [Instructor variants (shared)](#instructor-variants-shared)
- [Assessment matrix](#assessment-matrix)
- [References](#references)

## Purpose

CLEARANCE ships seven authored scenarios. Each is a self-contained training exercise built around a single high-level ATC skill: identification, prioritisation, tempo, degraded-picture judgement, procedural discipline, spatial awareness, or multi-threat classification. The scenarios are designed to be run individually as focused drills, or in sequence as a graduated training block.

This document describes the design intent behind each scenario: what skill it trains, what an operator is expected to do, what "correct" and "failed" performance look like, which incident types the scoring layer will log, and what should be discussed during the after-action debrief.

CLEARANCE is a portfolio demonstrator and training-simulation prototype. This document describes the training and scoring design behind the shipped scenarios, not certified training material.

## How to read this document

Two audiences. An engineering reviewer can read this to see how the training layer sits on top of the systems architecture and how the incident-type enum maps to scenario-specific pass and fail states. A training-design reviewer can read this to see how each scenario is intentionally built to force a particular decision under time pressure, with predictable failure modes.

For the runtime architecture behind the Scenario Runner see the Runtime Technical Architecture document. For the systems the scenarios drive see the Systems Design document.

## Scenario progression

The scenarios are ordered here in the order they were authored during production, which also happens to be roughly the order they were used for internal test playthroughs. They are not intended to be attempted strictly in this order during training; each is a self-contained drill.

1. **Baltic Intercept.** Sensor fusion, identification, GCI doctrine.
2. **Hijack Response.** ICAO 7500 recognition, SHADOW escort, protected-zone defence.
3. **Mass Divert.** Sustained ATC tempo, runway closure, fuel-cascade avoidance.
4. **Mayday Engine Fire.** Single-aircraft priority handling under traffic pressure.
5. **NORDO Inbound.** Lost-comms procedure, indirect control, separation planning around uncontrollable traffic.
6. **Cold War Probe.** Multi-bandit classification, threat-versus-probe judgement.
7. **Mixed Ops.** Restricted-airspace planning, spatial awareness under sustained load.

Roughly, the progression climbs through single-thread emergencies, into multi-aircraft prioritisation, into decision-making with imperfect information, ending on the sustained cognitive-load exercises.

## Scenario template

Every scenario writeup below follows the same nine-section template so scenarios can be compared side by side.

- **Training objective.** The single high-level skill the scenario is designed to test.
- **Location and ROE.** Where the scenario is set and the rules of engagement the operator is expected to follow.
- **Initial conditions.** Traffic on scope at T+0, weather, runway state, wake mix.
- **Timeline.** Key scripted events at their scheduled times, drawn from the JSON `timedEvents` and `triggers`.
- **Injects.** Voice injects, emergencies, or EW actions the runner fires against the sector during play.
- **Expected operator actions.** The "correct" playthrough. What a competent operator would do at each point.
- **Failure conditions.** What ends the scenario as a controller failure and why.
- **Scoring hooks.** Which `EIncidentType` values the scoring layer logs during the scenario, positive and negative.
- **Debrief points.** What to discuss during the AAR review.

## Baltic Intercept

### Training objective

Identify, classify, and vector-intercept an unknown contact closing on a civilian airliner. The scenario tests the operator's ability to run distributed sensor fusion, apply GCI doctrine correctly, and avoid mis-identifying a hostile as neutral or a neutral as hostile.

### Location and ROE

Baltic Sea, Latvia FIR. Type 1 unknown intercept. Visual identification (VID) required before declaring hostile. Mis-identification is the single largest failure state.

### Initial conditions

One civilian aircraft on scope: SK238 (Scandinavian Airlines, Medium, FL330, 280 kt, IFF active, squawking 1234). Westbound through the Baltic corridor. Wind 260 at 18. Active runway 27.

### Timeline

- **T+25 s.** AWACS calls "new contact, east of the sector, in trail of Scandinavian, low altitude, no IFF, closing from behind."
- **T+30 s.** UNKNOWN01 spawns at (110, 8) at 8000 ft, 300 kt, IFF off, threat class Unknown, true affiliation Hostile.
- **T+32 s.** UNKNOWN01 begins pursuit of SK238 (lead-pursuit tracker under GCI control).
- **Trigger: distance SK238 to UNKNOWN01 below 25 nm.** AWACS calls "unknown closing on Scandinavian inside twenty five miles. Recommend immediate intercept."
- **Trigger: distance below 5 nm.** Scenario logs a critical doctrine failure. Intercept has failed.
- **Trigger: aircraft count zero.** Scenario ends.

### Injects

Voice-only. AWACS calls at T+25 s and again on the 25 nm alert. No EW, no emergencies. The whole scenario is about identification and vector work.

### Expected operator actions

1. Hear AWACS call; look east on scope for the unknown.
2. Interrogate UNKNOWN01 (`clearance.iff UNKNOWN01`). IFF returns "no response" because the transponder is off.
3. Classify UNKNOWN01 as Hostile after the intercept posture is confirmed (`clearance.classify UNKNOWN01 hostile`).
4. Scramble alert fighters (`clearance.scramble UNKNOWN01`).
5. Vector the alert flight onto an intercept course before UNKNOWN01 closes inside 5 nm of SK238.
6. Complete the intercept; UNKNOWN01 breaks off westbound.

### Failure conditions

- UNKNOWN01 closes inside 5 nm of SK238 before intercept is established. Scenario logs a critical failure incident.
- Operator declares SK238 hostile at any point. Catastrophic `MisidentifiedCivilian` incident (SK238 has active IFF and is civilian).
- Alert fighters scrambled on SK238 by mistake. Same failure state.

### Scoring hooks

| Event | `EIncidentType` |
|---|---|
| SK238 mis-declared hostile | `MisidentifiedCivilian` (catastrophic) |
| UNKNOWN01 broken off outbound after intercept | `SuccessfulIntercept` |
| Alert flight vectored back to base cleanly | `SuccessfulHandoff` |
| UNKNOWN01 reached SK238 pre-intercept | Scenario failure (custom log entry) |

### Debrief points

- Did the operator interrogate the contact before classifying?
- Did they recognise the "no IFF response" pattern as the key differentiator?
- Was the intercept vectored efficiently or was there a lead-pursuit overshoot?
- If the operator hesitated, what was the reason and what would tighten the OODA cycle next time?

## Hijack Response

### Training objective

Recognise a mid-flight 7500 squawk change, call a SHADOW escort without hostile declaration, and prevent the hijacked aircraft from reaching a protected zone while managing four other unrelated civilians in the sector.

### Location and ROE

Continental FIR. ICAO 7500 doctrine. SHADOW escort (military escort, no weapons release) is the correct response. Hostile declaration is a last resort; a civilian aircraft under duress cannot be classified hostile without consequence.

### Initial conditions

Five civilian aircraft on scope, all IFF active, all normal cruise altitudes and speeds. One of them will squawk 7500 during the scenario.

### Timeline

- **T+0.** Five civilians on scope, normal traffic pattern.
- **T+60 to 120 s (script-dependent).** One aircraft squawks 7500. Radio silence follows. Aircraft begins deviating toward a protected zone.
- **Trigger: aircraft enters protected zone.** Scenario logs a `ViolationZoneBreached` incident and ends.
- **Trigger: SHADOW escort attaches, aircraft lands or exits sector safely.** Scenario ends as `SuccessfulEmergencyHandling`.

### Injects

Voice-only 7500 squawk change. No AWACS calls. Operator has to notice the squawk change on scope without prompting.

### Expected operator actions

1. Notice the 7500 squawk change on the hijacked aircraft's data block.
2. Do NOT attempt to raise the hijacked aircraft on the radio; hijack doctrine keeps operator silent.
3. Scramble a SHADOW escort (`clearance.scramble <callsign>` with military ROE).
4. Vector the other four civilians clear of the hijacked aircraft's projected path.
5. Watch for the hijacked aircraft's deviation and predict which protected zone it is heading for.
6. If necessary, vector a friendly interceptor between the hijack and the zone.

### Failure conditions

- Hijacked aircraft enters the protected zone. `ViolationZoneBreached` catastrophic penalty.
- Operator declares the hijacked aircraft hostile. `MisidentifiedCivilian` (this is a civilian under duress, not a hostile).
- Separation loss between the hijack and any other traffic while operator is distracted.

### Scoring hooks

| Event | `EIncidentType` |
|---|---|
| SHADOW attached, hijack landed or exited safely | `SuccessfulEmergencyHandling` |
| Hijack reaches protected zone | `ViolationZoneBreached` (catastrophic) |
| Operator mis-declares hijack hostile | `MisidentifiedCivilian` (catastrophic) |
| Separation loss during confusion | `SeparationLoss` |

### Debrief points

- Did the operator spot the squawk change without an inject prompting them?
- Was the SHADOW called promptly, or was there hesitation?
- Was the other traffic managed cleanly during the emergency, or did separation slip?
- How would this play differently under the two-federate variant, with the hijack owned by the peer federate?

## Mass Divert

### Training objective

Sustained ATC tempo under a hard deadline. Six aircraft inbound to a runway that becomes unusable partway through the scenario. Operator has five minutes to divert every inbound clear of the sector before fuel reserves cascade into declared emergencies.

### Location and ROE

Main Sector. Civil ATC only, no GCI involvement. Use `clearance.exit <callsign>` to clear each aircraft out of the sector toward the alternate field. Aircraft still in the sector at T+5 minutes begin declaring fuel emergencies. Three or more fuel emergencies is defined as sector failure.

### Initial conditions

Six aircraft inbound, staggered stages of approach. Wind rolling in, active runway about to become unusable due to weather. Wake mix includes at least one Heavy so the operator has to think about wake sequence even during a diversion.

### Timeline

- **T+0.** Six aircraft on approach or vectored to final. Weather rolling in.
- **T+30 to 60 s.** MET voice call: severe weather closing the runway.
- **T+60 s.** Active runway becomes unusable. Approach clearances no longer valid; every aircraft must be diverted.
- **T+300 s (five minutes).** Aircraft still in the sector begin declaring FuelLow emergencies at random intervals. Each ticks a 5-minute countdown.
- **Trigger: three fuel emergencies simultaneously active.** Sector failure logged and scenario ends.
- **Trigger: aircraft count zero (all diverted successfully).** Scenario ends as clean divert.

### Injects

MET voice call before runway closure. TOWER voice call announcing the divert. Fuel-low voice calls from each aircraft as the countdown starts.

### Expected operator actions

1. Hear MET call; recognise the runway is about to close.
2. Prioritise the sequence: which aircraft closest to needing fuel reserves first.
3. Issue divert instructions rapidly (`clearance.exit <callsign>` per aircraft).
4. Manage wake separation while sequencing the divert (Heavy first or last).
5. Watch fuel timers; land or divert the FuelLow aircraft first if any declare.

### Failure conditions

- Three simultaneous fuel emergencies. Sector failure.
- Any aircraft crashes from fuel exhaustion. `AircraftCrashed` catastrophic penalty.
- Separation loss during the rushed diverts.

### Scoring hooks

| Event | `EIncidentType` |
|---|---|
| Each successful divert | `SuccessfulHandoff` |
| Fuel-low declared before divert | Ticking down toward `AircraftCrashed` |
| Aircraft crashed from fuel exhaustion | `AircraftCrashed` (catastrophic) |
| Separation loss during rush | `SeparationLoss` |

### Debrief points

- Was the operator prioritising correctly (nearest to fuel critical first), or attempting to divert in arrival order?
- How was wake separation managed during the rushed sequence?
- Did the operator recognise the MET call as the trigger and act, or wait for TOWER confirmation?
- Under what traffic volume does this scenario become unwinnable, and where is the operator's ceiling?

## Mayday Engine Fire

### Training objective

Single-aircraft emergency priority handling under traffic pressure. The operator has to clear a corridor for the emergency aircraft to reach the runway while keeping six other aircraft out of the way without disrupting their flight paths more than necessary.

### Location and ROE

Main Sector. ICAO Mayday doctrine. The emergency aircraft has absolute priority. All other traffic vectored clear. Aircraft will burn down to a fuel-style cascade if not landed within seven minutes.

### Initial conditions

Seven aircraft on scope. One of them (Speedbird 394, Heavy) will declare Mayday with engine fire.

### Timeline

- **T+30 s.** BAW394 declares Mayday with engine fire and smoke in the cockpit. Countdown timer starts at 7 minutes.
- **Trigger: BAW394 lands safely.** `SuccessfulEmergencyHandling` logged and scenario ends.
- **Trigger: BAW394 fuel countdown reaches zero.** Aircraft crashes. `AircraftCrashed` catastrophic penalty and scenario ends.

### Injects

Voiced Mayday declaration from BAW394. Escalating radio urgency as the countdown reaches critical points (5 min, 3 min, 1 min).

### Expected operator actions

1. Acknowledge the Mayday declaration.
2. Vector the other six aircraft away from the emergency corridor (`clearance.vector <callsign> <heading>`).
3. Give BAW394 a straight-in vector to the active runway with priority landing sequence.
4. Manage wake separation for any following traffic that has to slot back in after the Mayday lands.
5. Land BAW394 within the 7-minute window.

### Failure conditions

- BAW394 crashes from countdown expiration. `AircraftCrashed`.
- Separation loss between BAW394 and any other traffic during the emergency corridor cleanup.
- Any of the six non-emergency aircraft strays into a restricted zone due to hasty vectors.

### Scoring hooks

| Event | `EIncidentType` |
|---|---|
| BAW394 lands within the window | `SuccessfulEmergencyHandling` |
| BAW394 crashes | `AircraftCrashed` (catastrophic) |
| Separation loss during corridor cleanup | `SeparationLoss` |
| Restricted zone bust from hasty vector | `RestrictedAirspaceBust` |

### Debrief points

- Was the corridor cleared efficiently, or did the operator overreact and stall other traffic?
- Did the operator use vectors or full re-route clearances for the non-emergency traffic?
- Under time pressure, did the operator remember wake separation on the follower behind the Mayday?
- How would the operator handle two simultaneous Maydays in the same sector?

## NORDO Inbound

### Training objective

Manage separation around aircraft the operator cannot control. Two aircraft go comms-failure simultaneously and fly the published lost-comms procedure on autopilot. The operator has to keep every other aircraft in the sector clear of the two NORDO tracks.

### Location and ROE

Main Sector. ICAO 7600 doctrine. NORDO traffic has right of way during lost-comms recovery. The operator can talk to and vector the four non-NORDO aircraft; the two NORDOs ignore every command. Three separation events ends the scenario as a controller failure.

### Initial conditions

Six aircraft on scope. Two will simultaneously go NORDO (squawk 7600, radios dead, transponders set) at a scripted time. Both will begin flying the published lost-comms procedure: turn for the active runway, descend to 3000, fly the ILS as if cleared.

### Timeline

- **T+45 s.** Two aircraft squawk 7600 simultaneously. Radio calls stop. Aircraft begin turning inbound to the active runway.
- **Ongoing.** Both NORDOs descend to 3000 ft and fly the ILS regardless of operator input.
- **Trigger: three separation events (any pair).** Scenario ends as controller failure.
- **Trigger: both NORDOs land or exit sector, all other traffic still safe.** Scenario ends as clean handling.

### Injects

Voice-silent. The NORDOs never speak. The operator has to notice the squawk change on the data blocks and read the intent from the aircraft's turn.

### Expected operator actions

1. Notice both squawk changes on the scope.
2. Predict both NORDO tracks (both will fly toward the active runway threshold).
3. Vector the four non-NORDO aircraft clear of both predicted tracks.
4. Manage wake separation among the four non-NORDO aircraft while they are being rerouted.
5. Let both NORDOs fly their procedure to a clean landing.

### Failure conditions

- Three separation events between any pair of aircraft during the scenario. Controller failure.
- Any aircraft (NORDO or non-NORDO) crashes during the scenario.
- Restricted zone bust from a hasty vector on one of the four controllable aircraft.

### Scoring hooks

| Event | `EIncidentType` |
|---|---|
| Both NORDOs land safely with no separation events | `SuccessfulEmergencyHandling` x 2 |
| Separation event between any pair | `SeparationLoss` |
| Attempted instruction to a NORDO | `Rejected_NoResponse` (silent refusal, no penalty on first attempt, small penalty on repeated attempts) |
| Restricted zone bust during rerouting | `RestrictedAirspaceBust` |

### Debrief points

- Did the operator try to talk to the NORDOs at first? How quickly did they recognise the silence pattern?
- Was the prediction of the NORDO tracks correct?
- Were the four controllable aircraft rerouted efficiently, or did the operator stall them unnecessarily?
- What is the largest number of controllable aircraft the operator can juggle around NORDO traffic?

## Cold War Probe

### Training objective

Multi-bandit GCI. Three unknown contacts inbound from three different bearings, all IFF off. One civilian transit is mixed in. The operator has to identify which contacts are real threats and which are recon probes that will turn outbound at the ADIZ line. Mis-classifying a probe as hostile is a large point loss; missing the real attacker is catastrophic.

### Location and ROE

Northern ADIZ. Standing GCI doctrine. Interrogate each contact. Real threats press through and target protected airspace; probes turn outbound at the ADIZ line. Each contact must be classified individually before scrambling. Multiple correct intercepts wins.

### Initial conditions

Four aircraft on scope by T+60 s: one civilian transit, three unknowns from three bearings. Wake mix is mostly fighter-class.

### Timeline

- **T+0.** Civilian transit on scope, transiting overhead.
- **T+15 to 45 s.** Three unknowns spawn from three bearings, all at fighter speed, all IFF off.
- **T+90 s (approx).** Two of the three unknowns turn outbound at the ADIZ line (these are the probes). One continues inbound toward protected airspace (this is the real threat).
- **Trigger: real threat enters protected zone.** `ViolationZoneBreached` catastrophic penalty and scenario ends.
- **Trigger: all threats classified and intercepted or turned back.** Scenario ends as clean multi-bandit intercept.

### Injects

GCI voice calls announcing each new unknown contact. AWACS calls confirming the probes' outbound turns as they happen.

### Expected operator actions

1. Interrogate each unknown as it appears (`clearance.iff <callsign>`). All return "no response".
2. Watch each contact's flight path for the ADIZ turn.
3. Do NOT scramble on any contact until its intent is confirmed (turn outbound = probe, continue inbound = threat).
4. Scramble on the real threat as soon as it commits.
5. Vector alert fighters onto lead-pursuit intercept.
6. Do NOT scramble on the civilian transit under any circumstances.

### Failure conditions

- Real threat reaches protected zone. `ViolationZoneBreached`.
- Operator scrambles on a probe. `MisidentifiedCivilian` if the operator declared it hostile; otherwise a wasted asset penalty.
- Operator declares the civilian transit hostile. `MisidentifiedCivilian` catastrophic.

### Scoring hooks

| Event | `EIncidentType` |
|---|---|
| Real threat intercepted and turned back | `SuccessfulIntercept` |
| Probes correctly identified and left alone | `SuccessfulResolution` |
| Real threat reaches protected zone | `ViolationZoneBreached` (catastrophic) |
| Operator scrambles on a probe or civilian | `MisidentifiedCivilian` (catastrophic) |

### Debrief points

- Did the operator wait for behavioural intent before classifying, or classify on IFF-off alone?
- Was the civilian transit correctly identified (active IFF, obvious transit pattern)?
- Were the probes' outbound turns spotted quickly, or did the operator scramble before they turned?
- How does the operator's decision cycle change under time pressure vs standing GCI doctrine?

## Mixed Ops

### Training objective

Controller spatial awareness under sustained load. Eight civilians transiting a sector with three or four active restricted areas (military training zones, P-areas, nuclear sites). No emergencies, no GCI. The operator has to plan vectors that route every civilian around every restricted zone.

### Location and ROE

Continental Sector with placed restricted zones. Civilians have active IFF. Any civilian entering a restricted area is an airspace bust (`RestrictedAirspaceBust`, -150 points). The vector verb is `vector <callsign> <heading>`.

### Initial conditions

Eight civilians on scope, various altitudes and speeds. Restricted zones placed and highlighted on the operator's scope.

### Timeline

- **T+0.** Eight civilians on scope, some direct-tracking toward restricted zones.
- **Ongoing.** Aircraft continue on their filed tracks unless vectored.
- **Trigger: aircraft enters a restricted zone.** `RestrictedAirspaceBust` logged, small penalty per incident.
- **Trigger: aircraft count zero (all handed off or exited).** Scenario ends.

### Injects

None. This scenario is pure operator planning under sustained load, no scripted interruptions.

### Expected operator actions

1. Read the scope: identify which civilians are heading toward restricted zones.
2. Prioritise the closest bust risks first.
3. Issue vectors to route each at-risk aircraft around the nearest zone edge.
4. Watch the wake corridor while sequencing vectors.
5. Hand off cleanly at the far edge of the sector.

### Failure conditions

- Multiple restricted zone busts. Each is a `RestrictedAirspaceBust` (-150). Cumulative penalty degrades the score.
- Separation loss during heavy vectoring. `SeparationLoss`.
- Missed handoffs (aircraft exiting sector without handoff). `UnresolvedExit`.

### Scoring hooks

| Event | `EIncidentType` |
|---|---|
| Each aircraft cleanly handed off | `SuccessfulHandoff` |
| Aircraft enters a restricted zone | `RestrictedAirspaceBust` |
| Separation loss during heavy vectoring | `SeparationLoss` |
| Aircraft exits without handoff | `UnresolvedExit` |

### Debrief points

- Which zones did the operator prioritise, and why?
- Was the vectoring efficient, or were aircraft flown well past the zone edge unnecessarily?
- How many aircraft can the operator route simultaneously before wake or separation slips?
- Under sustained load, how does the operator's scan pattern change?

## Instructor variants (shared)

Every scenario can be modified at inject time through the standard instructor RPC surface on `AClearanceOperatorPC`. These are the levers an instructor typically adjusts:

- **Max Aircraft slider (`Server_InjectSetMaxAircraft`).** Ceilings the concurrent traffic count. Lower for beginner passes, raise for stress tests.
- **Time scale (`Server_InjectSetTimeScale`).** 0.25x to 4x. Used to review a critical moment slowly or skip through a dead stretch. Left at 1x for assessment.
- **Emergency timer overrides (`Server_InjectEmergency` with `timerMinutes`).** Dial a 30-second panic or a 15-minute gentle exercise per emergency.
- **Wind and runway (`Server_InjectSetWind`).** Force a runway swap at any time, overriding the automatic wind-driven selection.
- **Aircraft injects (`Server_InjectSpawnAircraft`, `Server_InjectClearTraffic`).** Add unscripted contacts or clear the sector to reset live.
- **Classification injects (`Server_InjectClassifyAircraft`).** Force a truth-side reclassification of a contact without touching the operator's view. Useful for reproducing edge cases mid-run.
- **Federation start-stop (`Server_InjectStartDIS`, `Server_InjectStartDDS`, `Server_InjectHLAJoin`).** Toggle federated peers during a scenario to demonstrate cross-simulator behaviour.
- **Checkpoint save and load (`Server_InjectSaveCheckpoint`, `Server_InjectLoadCheckpoint`).** Snapshot before a critical moment; let trainees attempt in turn; load to reset.

Every inject is captured in the transcript and included in the AAR, so the debrief reflects both what the trainee did and what the instructor perturbed.

## Assessment matrix

Every scenario has a set of `EIncidentType` values that are the "must-not-happen" set: catastrophic incidents that fail the exercise regardless of anything else the operator did right.

| Scenario | Must-not-happen |
|---|---|
| Baltic Intercept | `MisidentifiedCivilian` (SK238), critical closure failure |
| Hijack Response | `ViolationZoneBreached`, `MisidentifiedCivilian` (hijack) |
| Mass Divert | `AircraftCrashed`, three simultaneous fuel emergencies |
| Mayday Engine Fire | `AircraftCrashed`, `SeparationLoss` during corridor |
| NORDO Inbound | Three separation events, `AircraftCrashed` |
| Cold War Probe | `ViolationZoneBreached`, `MisidentifiedCivilian` |
| Mixed Ops | Multiple `RestrictedAirspaceBust`, sustained `SeparationLoss` |

Positive scoring is not scenario-specific. Every scenario rewards `SuccessfulHandoff`, `SuccessfulLanding`, `SuccessfulResolution`, `SuccessfulIntercept`, and `SuccessfulEmergencyHandling` where they apply. Difficulty progression comes from the raw number of positives an operator can accumulate against the increasing incident volume.

## References

ICAO (2016) *Procedures for Air Navigation Services: Air Traffic Management*, Doc 4444, 16th ed. International Civil Aviation Organization.

NATO (2019) *Standardization Agreement 4193: Interoperability of Airborne Identification Friend or Foe (IFF) Systems*. NATO Standardization Office.

FAA (2022) *Aeronautical Information Manual*, Chapter 6 (Emergency Procedures). Federal Aviation Administration.
