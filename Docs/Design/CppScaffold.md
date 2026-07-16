# C++ Scaffold

**Project:** CLEARANCE
**Author:** Abdullah Ameed Abduljabbar
**Role:** Systems Designer

## Table of contents

- [Purpose](#purpose)
- [How to read this document](#how-to-read-this-document)
- [File setup](#file-setup)
- [Enums](#enums)
- [Core data structs](#core-data-structs)
- [Delegates](#delegates)
- [Systems summary](#systems-summary)
- [Per-system class breakdown](#per-system-class-breakdown)
- [Tuning constants](#tuning-constants)
- [Model-based subsystem hooks](#model-based-subsystem-hooks)
- [References](#references)

## Purpose

Class-level companion to the Systems Design document. Describes the enums, structs, delegates, and per-system class breakdown as they exist in the shipped codebase. Class names, headers, and interfaces are current at the time of writing; anywhere the code has drifted, the code wins and this document trails.

CLEARANCE is a portfolio demonstrator and training-simulation prototype. This scaffold documents the classes that carry the simulation, not the certification artefacts an operational programme would require.

## How to read this document

This document is not a tutorial and not a full API reference. It is a class-level map of the shipped CLEARANCE simulation layer: the shared data types, event delegates, major system classes, ownership boundaries, and tuning constants. For implementation details, the code is authoritative.

## File setup

Simulation headers follow a consistent include pattern, with each header including the relevant Unreal base type it derives from. The plugin module is `ClearanceSim`; the API macro is `CLEARANCESIM_API`.

| Line | Meaning | Purpose |
|---|---|---|
| `#pragma once` | Include this header only once | Prevents duplicate-definition issues in compilation |
| `#include "CoreMinimal.h"` | Load Unreal's core types and macros | Needed for `FName`, `FVector`, and the reflection macros |
| `#include "GameFramework/Actor.h"` | Load `AActor` | Required for the `AClearance*` actor classes |
| `#include "UObject/NoExportTypes.h"` | Load `UObject` | Required for `UClearance*` UObject classes (behaviour, validator, comms router, scoring, conflict detector) |
| `#include "Core/CLEARANCETypes.h"` | Load the shared enums, structs, and delegates | Every other simulation header depends on this |
| `#include "<HeaderName>.generated.h"` | Unreal-generated reflection code | Required for `UCLASS`, `USTRUCT`, `UENUM`, `UPROPERTY`, and `UFUNCTION` |

The shared types live under `Plugins/ClearanceSim/Source/ClearanceSim/Public/Core/`. Simulation systems live under `Public/Airspace/`, `Public/Aircraft/`, `Public/Comms/`, `Public/Safety/`, `Public/Scoring/`, `Public/Simulation/`, `Public/Scenario/`, and `Public/UI/`.

## Enums

Defined in `Public/Core/CLEARANCETypes.h`. Every enum is `UENUM(BlueprintType)` so the presentation layer can read them.

### EFlightPhase

Stage of flight for an aircraft in the sector.

| Value | Meaning | Used by |
|---|---|---|
| `Enroute` | Cruising through the sector | `FAircraftState`, spawner default |
| `Approach` | Lining up to land | `FAircraftState`, approach guidance in behaviour |
| `Landing` | In the landing segment | `FAircraftState`, ground braking in behaviour |
| `GoAround` | Aborted landing, climbing away | `FAircraftState`, TCAS RA go-around conversion |
| `Departing` | Leaving after takeoff | `FAircraftState`, spawner departure profile |
| `Exiting` | Leaving the sector | `FAircraftState`, controller `CheckExits` |

### EInstructionType

Kind of instruction the player or system issues to an aircraft.

| Value | Meaning | Used by |
|---|---|---|
| `HeadingChange` | Turn to a new heading | Phraseology parser, validator, behaviour |
| `AltitudeChange` | Climb or descend | Phraseology parser, validator, behaviour, TCAS RA handler |
| `SpeedChange` | Change speed | Phraseology parser, validator, behaviour |
| `Hold` | Enter a hold | Phraseology parser, behaviour |
| `ApproachClearance` | Cleared for approach | Phraseology parser, validator, behaviour approach mode |
| `TakeoffClearance` | Cleared for takeoff | Phraseology parser, behaviour departure mode |
| `ExitSector` | Prepare to leave | Phraseology parser, controller handoff |
| `DeclareTrackLost` | "No joy" / EW disengage | Phraseology parser, scoring EW-aware handler |

### EAlertLevel

Severity of a conflict or safety event.

| Value | Meaning | Used by |
|---|---|---|
| `None` | No safety issue | `FConflictEvent`, per-aircraft alert stamping |
| `Advisory` | Early warning, monitor | Conflict Detector at 8 nm horizontal |
| `Warning` | Higher-risk separation issue | Conflict Detector at 5 nm horizontal |
| `Critical` | Immediate danger, TCAS RA fires | Conflict Detector at 3 nm horizontal |

### EThreatClass

NATO-style threat classification carried per aircraft. Used in GCI and air-defence flows.

| Value | Meaning | Used by |
|---|---|---|
| `Friendly` | Verified friendly (transponder + IFF confirmed) | Symbol tint, engagement suppression, scoring |
| `Hostile` | Confirmed hostile | Engagement suppression, intercept scoring |
| `Unknown` | Unknown pending classification | Rejection path for control instructions, GCI flow |
| `Neutral` | Neutral (civilian, no IFF response required) | Default civilian traffic |

Every aircraft carries two threat fields: `ThreatClass` (what the operator sees) and `TrueAffiliation` (god-view truth). Instructor reclassification only updates one of the two depending on the flow.

### EEmergencyType

ICAO emergency states. The transponder squawk code is the universal signal any controller or federation peer picks up.

| Value | Squawk | Meaning | Used by |
|---|---|---|---|
| `None` | ordinary | Normal operation | Default |
| `GeneralMayday` | 7700 | General emergency | Emergency broadcast script, countdown handler |
| `CommsFailure` | 7600 | NORDO, lost radio | Rejects incoming instructions with `Rejected_NoResponse` |
| `Hijack` | 7500 | Hijack in progress | Shadow-fighter scramble trigger, radio silence |
| `FuelLow` | ordinary | Fuel emergency | Countdown handler, escalating urgency broadcasts |

### EIncidentType

Category of logged incident driving the scoring policy.

| Value | Meaning | Reward or penalty |
|---|---|---|
| `SeparationLoss` | Aircraft breached horizontal or vertical minima | Penalty |
| `UnresolvedExit` | Aircraft left without handoff | Penalty |
| `MissedHandoff` | Handoff missed | Penalty |
| `GoAroundTriggered` | Landing aborted | Penalty |
| `LateInstruction` | Operator reacted after threshold | Penalty |
| `WakeEncounter` | Follower flew into wake corridor | Penalty |
| `TCASResolutionAdvisory` | RA fired | Logged, no score change (TCAS resolved it) |
| `SuccessfulLanding` | Aircraft landed cleanly | Reward |
| `SuccessfulHandoff` | Aircraft handed off cleanly | Reward |
| `SuccessfulResolution` | Conflict cleared by operator instruction | Reward |
| `SuccessfulIntercept` | GCI intercept completed | Reward |
| `MisidentifiedCivilian` | Confirmed civilian declared hostile | Catastrophic penalty; further scrambles locked for the session |
| `ViolationZoneBreached` | Hostile reached a protected zone | Catastrophic penalty |
| `SuccessfulEmergencyHandling` | 7500 / 7600 / 7700 / fuel handled safely | Reward |
| `AircraftCrashed` | Aircraft destroyed | Catastrophic penalty |
| `RestrictedAirspaceBust` | Civilian entered a restricted area | Small penalty |

### EInstructionResult

Result of submitting an instruction through the Communication System.

| Value | Meaning |
|---|---|
| `Accepted` | Allowed, dispatched to behaviour |
| `Rejected_InvalidCallsign` | Callsign not registered |
| `Rejected_PhysicallyImpossible` | Outside envelope, service ceiling, or operating speed |
| `Rejected_AircraftExited` | Aircraft has already left the sector |
| `Rejected_ConflictAdvisory` | Would immediately create or worsen a conflict |
| `Rejected_NoResponse` | NORDO or IFF-off contact, silent refusal |

### EWakeCategory

Wake turbulence category driving the separation matrix. Set at spawn from aircraft type.

| Value | MTOW band |
|---|---|
| `Light` | Under 7,000 kg |
| `Medium` | 7,000 to 136,000 kg |
| `Heavy` | 136,000 to 560,000 kg |
| `Super` | Over 560,000 kg (A380, AN-225) |

### EClearanceCommsRole

Role of a transcript entry. Drives colour and speaker attribution on the transcript widget.

| Value | Meaning |
|---|---|
| `Operator` | Player transmission |
| `Pilot` | Aircraft readback or refusal |
| `System` | Simulation event, no callsign attribution |
| `Instructor` | Instructor inject (colour-coded to distinguish from system-tick events) |
| `Tower`, `Acc`, `Awacs`, `Gci`, `Atis`, `Met` | Facility voice injects, each with a distinct voice and colour |

## Core data structs

Defined in `Public/Core/CLEARANCETypes.h`. Every struct is `USTRUCT(BlueprintType)`.

### FAircraftState

Authoritative state of a single aircraft. Owned by the Airspace Manager; read by every other system; never mutated outside the Airspace Manager.

| Field | Type | Meaning |
|---|---|---|
| `Callsign` | `FName` | Unique aircraft identifier |
| `Position` | `FVector` | Position in the sector, ENU frame, nautical miles |
| `Altitude` | `float` | Current altitude in feet |
| `Speed` | `float` | Current speed in knots |
| `Heading` | `float` | Current heading in degrees |
| `Velocity` | `FVector` | Movement vector, nm per second |
| `FlightPhase` | `EFlightPhase` | Current stage of flight |
| `ClimbRate` | `float` | Vertical rate in feet per minute |
| `TargetAltitude` | `float` | Altitude the aircraft is chasing |
| `TargetHeading` | `float` | Heading the aircraft is chasing |
| `TargetSpeed` | `float` | Speed the aircraft is chasing |
| `bHasActiveInstruction` | `bool` | True while any axis is out of tolerance |
| `bIsValid` | `bool` | Whether the state is safe to read |
| `TimeEnteredSector` | `float` | Session time when the aircraft registered |
| `WakeCategory` | `EWakeCategory` | Wake band, drives the ICAO separation matrix |
| `BankAngle` | `float` | Current bank in degrees, limited by category |
| `ServiceCeiling` | `float` | Max operational altitude in feet |
| `MinOperatingSpeed` | `float` | Minimum safe speed in knots |
| `MaxOperatingSpeed` | `float` | Max operational speed in knots |
| `MaxClimbRate` | `float` | Max climb for the aircraft type in fpm |
| `ThreatClass` | `EThreatClass` | Operator-visible affiliation |
| `TrueAffiliation` | `EThreatClass` | God-view affiliation, may differ from `ThreatClass` on unclassified contacts |
| `ActiveEmergency` | `EEmergencyType` | Current emergency lifecycle marker |
| `FuelRemainingMinutes` | `float` | Countdown for FuelLow and GeneralMayday emergencies |
| `CurrentAlertLevel` | `EAlertLevel` | Highest active alert on the aircraft, stamped each tick from the conflict detector |
| `bJammingOn` | `bool` | Aircraft's own noise jammer active |
| `bIsMilitary` | `bool` | True for military airframes, switches performance envelope to fighter profile |
| `bIsExternal` | `bool` | True for aircraft owned by a federated peer sim |
| `bAutopilotEngaged` | `bool` | If true, behaviour uses the Simulink cascade autopilot instead of the analytic steppers |

### FChaffCloud

An active chaff cloud. Ghost tracks paint against this in the sensor layer.

| Field | Type | Meaning |
|---|---|---|
| `OriginCallsign` | `FName` | Aircraft that dropped the cloud |
| `PositionNm` | `FVector` | Drop position in sector-relative NM |
| `DropTime` | `float` | Session time of drop |
| `LifetimeSeconds` | `float` | Total cloud lifetime (default 12 s) |

### FAircraftInstruction

Payload from the phraseology parser through the Comms Router into the behaviour queue.

| Field | Type | Meaning |
|---|---|---|
| `TargetCallsign` | `FName` | Which aircraft the instruction targets |
| `Type` | `EInstructionType` | What kind of instruction |
| `TargetValue` | `float` | Altitude, speed, or heading target |
| `IssuedTime` | `float` | Session time at issue |
| `bIsGoAround` | `bool` | System-triggered rather than player-issued (bypasses envelope check) |
| `bExpedite` | `bool` | Push the rate up (TCAS RA sets this) |
| `TurnDirection` | `int32` | -1 left, +1 right, 0 shortest way |

### FConflictEvent

Broadcast by the Conflict Detector for every detected or escalated pair.

| Field | Type | Meaning |
|---|---|---|
| `AircraftA` | `FName` | First aircraft in the conflict |
| `AircraftB` | `FName` | Second aircraft |
| `HorizontalSeparationNm` | `float` | Horizontal separation at detection |
| `VerticalSeparationFt` | `float` | Vertical separation at detection |
| `AlertLevel` | `EAlertLevel` | Severity at detection |
| `TimeOfDetection` | `float` | Session time |
| `bRequiresGoAround` | `bool` | Whether the geometry requires a go-around |

### FIncidentRecord

Scored event appended to the session log.

| Field | Type | Meaning |
|---|---|---|
| `Type` | `EIncidentType` | Category |
| `AircraftA` | `FName` | Primary aircraft involved |
| `AircraftB` | `FName` | Second aircraft if the event is a pair |
| `TimeStamp` | `float` | Wall-clock session time |
| `Details` | `FString` | Free-text notes shown in the AAR |

### FAircraftSpawnData

Spawner and scenario payload for `RegisterAircraft`.

| Field | Type | Meaning |
|---|---|---|
| `Callsign` | `FName` | Identifier |
| `EntryPosition` | `FVector` | Spawn position in sector-relative NM |
| `EntryAltitude` | `float` | Starting altitude |
| `EntrySpeed` | `float` | Starting speed |
| `EntryHeading` | `float` | Starting heading |
| `InitialPhase` | `EFlightPhase` | Starting phase |
| `WakeCategory` | `EWakeCategory` | Category at spawn |
| `bIsMilitary` | `bool` | Fighter envelope on spawn |
| `ThreatClass` | `EThreatClass` | Initial classification |
| `TrueAffiliation` | `EThreatClass` | God-view classification |

### FRunwayInfo

Registered runway threshold. Populated by `AClearanceRunway` actors at BeginPlay.

| Field | Type | Meaning |
|---|---|---|
| `ThresholdNm` | `FVector2D` | Sector-relative NM position of the primary threshold |
| `HeadingDeg` | `float` | Landing direction in degrees |
| `LengthUnits` | `float` | Asphalt length in world units |
| `WidthUnits` | `float` | Asphalt width in world units |
| `DesignatorOverride` | `int32` | Forces the ICAO designator (1 to 36). Zero uses the heading-based fallback. |

### FOperatorEmergencyEntry

Sorted emergency row for the instructor emergency panel.

| Field | Type | Meaning |
|---|---|---|
| `Callsign` | `FName` | Aircraft in emergency |
| `Type` | `EEmergencyType` | Emergency kind |
| `TimerMinutesRemaining` | `float` | Countdown for FuelLow and GeneralMayday. -1 for timerless emergencies. |
| `SquawkCode` | `int32` | Transponder code (7500, 7600, 7700, or 0 for FuelLow) |
| `ThreatClass` | `EThreatClass` | Operator's current classification |
| `Detail` | `FString` | Instructor-provided or emergency-script-provided extra text |

### FRadarTrack

What one radar site currently believes about one aircraft. Distinct from the truth in the Airspace Manager.

| Field | Type | Meaning |
|---|---|---|
| `TruthCallsign` | `FName` | Internal, always set. Used to match against truth. |
| `DisplayCallsign` | `FName` | Shown to the operator; empty for primary-only paints |
| `Position` | `FVector` | Last paint position in NM, with sensor jitter |
| `Altitude` | `float` | Feet; precise with secondary, estimated primary-only |
| `Heading` | `float` | Estimated from successive paints |
| `Speed` | `float` | Estimated |
| `bHasSecondary` | `bool` | True if the transponder responded |
| `LastPaintTime` | `float` | For fading old tracks |
| `Confidence` | `float` | 0..1, fades with time since last paint |
| `PaintConfidence` | `float` | Last-paint confidence before time fade. EW pins this. |

### FEmissionSignature, FRadarEmissionSnapshot

DIS Emission PDU inputs. `FEmissionSignature` carries emitter name, function, frequency band, ERP, PRF, pulse width, and beam azimuth; fields map to the DIS Fundamental Parameter Data carried by the Emission PDU. `FRadarEmissionSnapshot` bundles the signature with the currently painted entities into a Track/Jam list.

### FWeaponsFireEvent, FWeaponsDetonationEvent

DIS Fire and Detonation PDU inputs. Used for engagement and intercept event modelling, such as simulated fire and intercept-resolution events during GCI scenarios.

### FVoiceCommsEvent, FRadioTransmitter

DIS Signal and Transmitter PDU inputs. Voice PTT samples with an operator ground-station Entity ID.

### FRecordedSnapshot, FRecordedEvent

Session Recorder payloads. Snapshot captures every `FAircraftState` at a timestamp; event captures a scored event with the same timestamp for `GetEventsInRange`.

### FSectorEnvironment

Sector-wide environmental state owned by the Airspace Manager.

| Field | Type | Meaning |
|---|---|---|
| `WindDirection` | `float` | Degrees |
| `WindSpeed` | `float` | Knots |
| `ActiveRunwayHeading` | `float` | Currently selected runway |
| `ActiveRunwayThreshold` | `FVector` | Sector-NM position of the selected runway threshold |
| `AvailableRunways` | `TArray<float>` | All runway headings available for wind-based selection |

### FCommsTranscriptEntry

Transcript row on the Simulation Controller.

| Field | Type | Meaning |
|---|---|---|
| `TimeSec` | `float` | Session time |
| `Role` | `EClearanceCommsRole` | Speaker role |
| `Callsign` | `FName` | Aircraft or facility identifier |
| `Speaker` | `FString` | Pre-computed display speaker (`ATC`, callsign, `TWR`, `SYS`) so the widget binds one pin |
| `Text` | `FString` | Transmission body |

### FClearanceCheckpointInfo

Metadata for a saved checkpoint (name, session time, aircraft count, score at save). Replicates to clients for the dropdown UI; the full payload stays server-side.

## Delegates

Defined at the bottom of `Public/Core/CLEARANCETypes.h`. Delegates are declared as dynamic multicast where Blueprint-facing subscription is needed; they act as the event map between systems, with the Simulation Controller binding every listener at session start.

| Delegate | Broadcast by | Meaning |
|---|---|---|
| `FOnAircraftRegistered` | Airspace Manager | New aircraft entered the sector |
| `FOnAircraftDeregistered` | Airspace Manager | Aircraft removed from the sector |
| `FOnAircraftStateUpdated` | Airspace Manager | State changed on an aircraft |
| `FOnRunwayChanged` | Airspace Manager | Active runway swapped due to wind shift |
| `FOnConflictDetected` | Conflict Detector | Pair crossed a separation threshold |
| `FOnConflictResolved` | Conflict Detector | Previously active conflict cleared |
| `FOnGoAroundRequired` | Conflict Detector | Aircraft on approach must abort |
| `FOnWakeTurbulenceAdvisory` | Conflict Detector | Follower entered a leader's wake corridor |
| `FOnTCASResolutionAdvisory` | Conflict Detector | Critical separation, coordinated vertical split fires |
| `FOnInstructionResult` | Comms Router | Instruction accepted or rejected |
| `FOnAdvisoryWarning` | Comms Router | Advisory shown to the operator |
| `FOnScoreUpdated` | Scoring | Score changed |
| `FOnDifficultyAdjusted` | Scoring | Spawn interval scaled |

## Systems summary

| Class | Type | Main job | Key inputs | Key outputs | Rule |
|---|---|---|---|---|---|
| `AClearanceAirspaceManager` | AActor | Store authoritative aircraft state and sector environment | Registrations, state updates, wind updates | Aircraft queries, update broadcasts, runway changes | Single source of truth |
| `UClearanceAircraftBehaviour` | UObject | Execute movement gradually | Validated instructions, current state | Updated state committed each tick | Sole movement executor |
| `UClearanceCommsRouter` | UObject | Accept, validate, and route instructions | Player transmissions, system go-arounds | Instruction results, routed commands, transcript entries | Never moves aircraft directly |
| `UClearanceInstructionValidator` | UObject | Check if an instruction is feasible | Current state, requested instruction | Pass or fail with reason | Stateless; rejects impossible commands |
| `UClearanceConflictDetector` | UObject | Monitor separation, wake, and RA conditions | Read-only aircraft snapshots | Conflict events, go-around requirements, TCAS RA broadcasts | Read-only safety monitor |
| `UClearanceScoring` | UObject | Log outcomes, calculate score, scale difficulty | Incidents and gameplay events | Score updates, spawn-rate changes | Tracks performance, not aircraft state |
| `AClearanceAircraftSpawner` | AActor | Introduce aircraft into the sector | Spawn config, difficulty | New aircraft registrations | Controls entry only, not behaviour |
| `AClearanceSimulationController` | AActor | Orchestrate the tick pipeline and own UObject lifecycles | References to every system | Full session orchestration, replicated arrays, RPCs | Coordinates dependency order |
| `UClearanceScenarioRunner` | UObject | Execute JSON scenarios | Scenario file, session time | Spawn actions, voice injects, weather changes | Suspends the free-play spawner |
| `AClearanceRadarSite` (+ owned `UClearanceRadar`) | AActor + UObject | Paint tracks against airspace state | Snapshot, EW state | `FRadarTrack` map per site | Analytic or Simulink DSP path |
| `UClearanceSessionRecorder` | UObject | Capture snapshots and events per tick | Airspace snapshots, scoring events | `FRecordedSnapshot` and `FRecordedEvent` arrays | Ring-buffered, seekable |
| `UClearanceInstructorPanel` | UUserWidget | Presentation for the instructor station | BlueprintCallable getters | Slate paint, RPC dispatch | Never mutates sim state |

## Per-system class breakdown

### AClearanceAirspaceManager

| Section | Contents |
|---|---|
| Public functions | `RegisterAircraft`, `DeregisterAircraft`, `RequestStateUpdate`, `GetAircraftState`, `GetAllAircraftStates`, `GetCurrentEnvironment`, `UpdateWindConditions`, `RecalculateActiveRunway`, `GetActiveRunway`, `GetAllRunways`, `IsCallsignRegistered`, `ClearAllAircraft` |
| Public settings | `MaxAircraftCount`, `MinSafeAltitude`, `MaxSafeAltitude`, `MinSafeSpeed`, `MaxSafeSpeed`, `DefaultWindDirection`, `DefaultWindSpeed`, `AvailableRunwayHeadings`, crosswind limit |
| Events | `OnAircraftRegistered`, `OnAircraftDeregistered`, `OnAircraftStateUpdated`, `OnRunwayChanged` |
| Private data | `TMap<FName, FAircraftState> AircraftStates`, `TArray<FAircraftState> ReplicatedAircraft` (mirror for clients), `FSectorEnvironment SectorEnvironment`, `TArray<FRunwayInfo> Runways`, `TArray<FChaffCloud> ChaffClouds` |
| Private helpers | `ValidateState`, `ClampStateValues`, `RebuildReplicatedArray`, `RecalculateActiveRunway` |

### UClearanceAircraftBehaviour

| Section | Contents |
|---|---|
| Public functions | `Initialise`, `UpdateMovement`, `QueueInstruction`, `HasActiveInstruction`, `ClearInstructions`, `ExecuteGoAround`, `SetAutopilotEngaged`, `IsAutopilotEngaged` |
| Motion internals | `ApplyInstruction`, `StepHeading`, `StepAltitude`, `StepSpeed`, `StepPosition`, `StepWithAutopilot`, `RunApproachGuidance`, `IsEstablishedOnApproach`, `HasMissedApproach` |
| Tuning values | `HeadingToleranceDeg`, `AltitudeToleranceFt`, `SpeedToleranceKnots`, `GoAroundClimbFt` |
| Private data | `Pending` (instruction queue), `bGoingAround`, `bExpediting`, `bApproachCaptured`, `bAutopilotEngaged`, `ActiveTurnDirection` |
| Physics helpers | `TurnRateDegPerSec`, `DensityAdjustedClimbRate`, `ISADensityRatio` |
| Autopilot bridge | `AutopilotWrapper` (Simulink instance), captured-state gate, elevator and aileron deadband |

### UClearanceCommsRouter

| Section | Contents |
|---|---|
| Public functions | `IssueInstruction`, `RouteGoAround`, `SendAdvisoryWarning`, `SetReferences`, `SetLastInstructionTime`, `GetLastInstructionTime` |
| Events | `OnInstructionResult`, `OnAdvisoryWarning` |
| Private data | References to `AClearanceAirspaceManager`, `UClearanceInstructionValidator`, `BehaviourMap` |
| Control logic | Per-callsign last-instruction time, minimum interval enforcement |

### UClearanceInstructionValidator

| Section | Contents |
|---|---|
| Public function | `Validate(instruction, state)` returns an `EInstructionResult` |
| Private rules | Envelope checks, service ceiling, min and max operating speed, bank angle feasibility, approach geometry, wake separation on final |
| Constants | Referenced from `Public/Core/ClearanceConstants.h` |

### UClearanceConflictDetector

| Section | Contents |
|---|---|
| Public functions | `DetectConflicts`, `GetAlertLevelFor`, `RemoveAircraft`, `IsInWakeTurbulence`, `GetWakeIntensity` |
| Events | `OnConflictDetected`, `OnConflictResolved`, `OnGoAroundRequired`, `OnWakeTurbulenceAdvisory`, `OnTCASResolutionAdvisory` |
| Public settings | `ProjectionLookaheadSeconds` (default 60 s) |
| Private data | `Manager` reference, `TMap<FString, FConflictEvent> ActiveConflicts`, `TSet<FString> ActiveWakeAdvisories`, `TSet<FString> ActiveTCAS`, `TMap<FName, float> WakeAffected` |
| Static helpers | `HorizontalSeparationNm`, `VerticalSeparationFt`, `AlertFromSeparation`, `PairKey`, `FollowerIsBehindLeader`, `RequiredWakeSeparationNm` |
| Engagement suppression | `bThreatInvolved`, `bBothUnderGCI`, `bShadowPair` gate the civilian alert ladder for GCI, viper, and shadow pairs |

### UClearanceScoring

| Section | Contents |
|---|---|
| Public functions | `LogIncident`, `LogInstruction`, `RegisterHandled`, `ResetSession`, `RestoreFromCheckpoint`, `GetSessionLog`, `GetScore`, `GetEfficiency`, `GetSpawnInterval` |
| Events | `OnScoreUpdated`, `OnDifficultyAdjusted` |
| Session data | `IncidentLog`, `Score`, `TotalLandings`, `TotalHandoffs`, `TotalGoArounds`, `TotalInstructions`, `TotalHandled` |
| Scoring values | `PointsLanding`, `PointsHandoff`, `PointsResolution`, `PenaltySeparationLoss`, `PenaltyGoAround`, `PenaltyMisID`, `PenaltyBust`, `PenaltyCrash`, and others |
| Difficulty values | `BaseSpawnIntervalSeconds`, `MinSpawnIntervalSeconds`, `MaxSpawnIntervalSeconds`, `DifficultySecondsPerHandled` |

### AClearanceAircraftSpawner

| Section | Contents |
|---|---|
| Public functions | `TickSpawning`, `SpawnAircraft`, `SetSpawnRate`, `SetReferences`, `SetAutoSpawnEnabled` |
| Public settings | Callsign pool, `MaxConcurrentAircraft`, per-category weighting |
| Private data | References to Airspace Manager and Scoring, spawn timer, current spawn interval, callsign counter |
| Helpers | `GenerateCallsign`, sample entry point, sample altitude, sample speed, sample heading |

### AClearanceSimulationController

| Section | Contents |
|---|---|
| Public functions | `BeginPlay`, `Tick`, `StartSession`, `PauseSession`, `ResumeSession`, `EndSession`, `PlayerIssueInstruction`, `SetAircraftAutopilotEngaged`, `ExportAARReport`, `SaveCheckpoint`, `LoadCheckpoint`, `DeleteCheckpoint` |
| Core references | Airspace Manager, Spawner, Conflict Detector, Comms Router, Validator, Scoring, Scenario Runner, Behaviour Map, Session Recorder, DIS emitter, DDS emitter, RTI emitter, HLA broker |
| Session state | `bSessionActive`, `SessionTime`, `SimulationTimeScale`, `bReplayMode`, `bReplayPaused`, `ReplayTime`, `ReplaySpeed`, `ReplayDuration` |
| Replicated | `ReplicatedAircraft`, `RepScoringLog`, `RepOperatorTracks`, `RepNotifications`, `RepCheckpoints`, `Transcript`, `SectorEnvironment`, `Runways`, `ChaffClouds` |
| Simulation steps | `StepSimulation`, `UpdateBehaviours`, `RunConflictCheck`, `ProcessGoArounds`, `CheckExits`, `UpdateDifficulty`, `TickGCIIntercepts`, `TickBanditEW`, `TickCrashingAircraft`, `UpdateVisuals` |
| Event handlers | `HandleAircraftRegistered`, `HandleAircraftDeregistered`, `HandleConflictDetected`, `HandleConflictResolved`, `HandleGoAroundRequired`, `HandleWakeAdvisory`, `HandleTCASResolutionAdvisory`, `HandleDifficultyAdjusted` |

### UClearanceScenarioRunner

Loads a JSON scenario file, evaluates timed actions each tick, applies them via the same Simulation Controller RPCs the instructor uses. Actions: `spawnAircraft`, `setHeading`, `setAltitude`, `setSpeed`, `declareHostile`, `declareFriendly`, `declareUnknown`, `activateJammer`, `dropChaff`, `injectEmergency`, `broadcastFacility`, `scrambleFighters`, `changeWind`, `forceRunway`, `forceConflict`, `forceIncident`.

### AClearanceRadarSite and UClearanceRadar

`AClearanceRadarSite` is the placed world actor: it carries the antenna position, orientation, range, and per-site enable flag. It owns a `UClearanceRadar` UObject that runs the detection logic and stores the resulting tracks. When the Simulink DSP path is engaged, that UObject also owns an `FRadarWrapper` around the generated model.

| Section | Contents |
|---|---|
| Public functions | `PaintTracks`, `GetTracks`, `SetRangeNm`, `SetEnabled`, `ConfigureAnalytic`, `ConfigureSimulinkDSP` |
| Public settings | `RangeNm`, `AntennaPosition`, `ScanIntervalSec`, `RadarEquationInputs`, `bUseSimulinkDSP` |
| Private data | `Tracks` (`TMap<FName, FRadarTrack>`), analytic path RCS lookup, Simulink `FRadarWrapper` |
| Analytic path | Skolnik monostatic radar range equation, receiver noise floor, logistic Pd curve, per-aircraft RCS |
| Simulink path | I/Q cube synthesis, `radar_step`, CFAR detection matching |

### UClearanceSessionRecorder

| Section | Contents |
|---|---|
| Public functions | `StartRecording`, `StopRecording`, `CaptureSnapshot`, `LogEvent`, `FindSnapshotAt`, `GetEventsInRange`, `GetDurationSeconds`, `ClearRecording`, `IsRecording` |
| Private data | `Snapshots` (`TArray<FRecordedSnapshot>`), `Events` (`TArray<FRecordedEvent>`), ring cap |

### UClearanceInstructorPanel

Presentation-side base class. Owns `BlueprintCallable` getters that the UMG widget binds to. Runs `NativePaint` for the scope and camera overlay layers. Never writes to simulation state.

## Tuning constants

Defined in `Public/Core/ClearanceConstants.h`. All numeric thresholds live here so any change is a one-file edit.

| Constant | Value | Purpose |
|---|---|---|
| `AdvisoryHorizontalNm` | 8.0 | Conflict alert Advisory threshold |
| `WarningHorizontalNm` | 5.0 | Conflict alert Warning threshold |
| `CriticalHorizontalNm` | 3.0 | Critical threshold; a simulated TCAS-style RA fires at Critical when vertical separation is also below the RVSM minimum |
| `VerticalMinimumFt` | 1000.0 | RVSM vertical separation minimum |
| `WakeLightBehindHeavyNm` | 6.0 | Wake matrix, Light following Heavy |
| `WakeMediumBehindHeavyNm` | 5.0 | Wake matrix |
| `WakeLightBehindMediumNm` | 5.0 | Wake matrix |
| `WakeHeavyBehindHeavyNm` | 4.0 | Wake matrix |
| `WakeStandardMinimumNm` | 3.0 | Baseline wake minimum |
| `ApproachCorridorLengthNm` | 80.0 | Extended centerline drawn from threshold |
| `ApproachCorridorHalfWidthNm` | 3.0 | Localiser capture corridor half-width at the far end |

### Per-category performance

`GetCategoryPerformance(EWakeCategory)` returns an `FCategoryPerformance` with `ServiceCeilingFt`, `MinOperatingSpeedKts`, `MaxOperatingSpeedKts`, `MaxClimbRateFtMin`, `MaxDescentRateFtMin`, `BankLimitDeg`, `AccelKtsPerSec`, `DecelKtsPerSec`, `CrosswindLimitKts`, `GroundBrakingKtsPerSec`.

| Category | Reference aircraft | Vmin | Vmax | Ceiling | Bank | Climb |
|---|---|---|---|---|---|---|
| Light | Cessna 172S | 65 | 163 | 14,000 | 30 | 730 |
| Medium | Boeing 737-800 | 150 | 340 | 41,000 | 25 | 1,800 |
| Heavy | Boeing 777-300ER | 170 | 350 | 43,100 | 25 | 2,500 |
| Super | Airbus A380-800 | 155 | 340 | 43,000 | 25 | 1,500 |

`GetMilitaryPerformance()` returns the fighter envelope used when `bIsMilitary = true`:

| Vmin | Vmax | Ceiling | Bank | Climb | Accel |
|---|---|---|---|---|---|
| 180 | 1,050 | 50,000 | 80 | 12,000 | 10 |

## Model-based subsystem hooks

Two Simulink models integrate into the sim through separate plugin modules alongside `ClearanceSim`.

### ClearanceAutopilotMBD

Wraps the generated C from a cascade PID autopilot Simulink model. The Unreal wrapper accepts operational targets (target heading, altitude, airspeed) alongside current aircraft state, computes the outer-loop bank, pitch, and speed commands in C++, and then feeds the generated Simulink inner-loop model. Interface:

- `FClearanceAutopilotInputs` bundles target heading, altitude, and airspeed plus current heading, altitude, airspeed, bank, pitch approximation, and vertical speed.
- `FClearanceAutopilotOutputs` returns aileron and elevator (radians) and throttle (0..1) from the inner loop.
- `FAutopilotWrapper::Step(inputs)` runs one `autopilot_step` call against the model's per-instance runtime state.

`UClearanceAircraftBehaviour::StepWithAutopilot` builds the inputs, calls `Step`, and integrates aileron and elevator as rate commands.

### ClearanceRadarMBD

Wraps the generated C from a pulsed-radar signal processor (LFM waveform, MVDR beamforming, matched-filter pulse compression, range-Doppler, CFAR detection). Interface:

- `FRadarInputs` carries per-pulse I/Q samples.
- `FRadarOutputs` returns CFAR detections (range, Doppler, amplitude).
- `FRadarWrapper::Step(inputs)` runs one `radar_step` call.

`UClearanceRadar::PaintTracks` synthesises the I/Q cube from live airspace state per site, calls `Step`, and matches CFAR detections back to source aircraft by range.

Both wrappers use reusable-function packaging so every aircraft and every radar site owns its own model instance with no shared globals.

## References

Anderson, J. D. (2016) *Introduction to Flight*, 8th ed. McGraw-Hill Education.

Gregory, J. (2014) *Game Engine Architecture*, 2nd ed. CRC Press.

IEEE (2012) *IEEE Standard for Distributed Interactive Simulation: Application Protocols* (IEEE Std 1278.1-2012). IEEE Standards Association.

ICAO (2016) *Procedures for Air Navigation Services: Air Traffic Management*, Doc 4444, 16th ed. International Civil Aviation Organization.

Skolnik, M. (2001) *Introduction to Radar Systems*, 3rd ed. McGraw-Hill.

Stengel, R. F. (2004) *Flight Dynamics*. Princeton University Press.
