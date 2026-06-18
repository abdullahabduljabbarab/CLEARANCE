in//

\============================================================================

CLEARANCE - Air Traffic Control Simulation

Core Systems Scaffold

Abdullah Ameed Abduljabbar

// ============================================================================

//

\============================================================================

1.  FILE SET UP

// ============================================================================

**Code Line**

**Meaning**

**Purpose**

#pragma once

Include this header only once

Prevents duplicate-definition issues in compilation

#include “CoreMinimal.h”

Load Unreal’s core types and macros

Needed for things like FName, FVector, and Unreal reflection macros

#include “GameFramework/Actor.h”

Load Unreal’s AActor base class

Required because some systems inherit from AActor

#include “CLEARANCE\_Scaffold.generated.h”

Include Unreal’s generated support code

Required for UCLASS, USTRUCT, UENUM, UPROPERTY, and UFUNCTION to work correctly

// ============================================================================

1.  ENUMS

// ============================================================================

Flight phase

**Value**

**Meaning**

**Used By**

Enroute

Aircraft is cruising through the sector

FAircraftState

Approach

Aircraft is lining up to land

FAircraftState

Landing

Aircraft is in the landing segment

FAircraftState

GoAround

Aircraft aborted landing and is climbing away

FAircraftState, conflict response

Departing

Aircraft is leaving after takeoff

FAircraftState

Exiting

Aircraft is leaving the sector

FAircraftState, exit logic

Instruction type

Value

Meaning

Used By

HeadingChange

Turn to a new heading.

FAircraftInstruction, Behaviour, Validator

AltitudeChange

Climb or descend to a target altitude.

FAircraftInstruction, Behaviour, Validator

SpeedChange

Change aircraft speed.

FAircraftInstruction, Behaviour, Validator

Hold

Enter a hold.

FAircraftInstruction

ApproachClearance

Cleared for approach.

FAircraftInstruction, Behaviour, Validator

TakeoffClearance

Cleared for takeoff.

FAircraftInstruction

ExitSector

Prepare to leave the sector.

FAircraftInstruction, session flow

Alert level

Value

Meaning

Used by

None

No safety issue.

FConflictEvent

Advisory

Early warning, monitor the situation.

Conflict Detection

Warning

Higher-risk separation issue.

Conflict Detection

Critical

Immediate danger level.

Conflict Detection, go-around triggers

Incident type

Value

Meaning

Used By

SeparationLoss

Aircraft got too close.

FIncidentRecord, Scoring

UnresolvedExit

Aircraft left badly or without proper resolution.

FIncidentRecord, Scoring

MissedHandoff

Handoff was not completed correctly.

FIncidentRecord, Scoring

GoAroundTriggered

Go-around had to happen.

FIncidentRecord, Scoring

LateInstruction

Player reacted too late.

FIncidentRecord, Scoring

SuccessfulLanding

Aircraft landed successfully.

FIncidentRecord, Scoring

SuccessfulDeparture

Aircraft departed successfully.

FIncidentRecord, Scoring

SuccessfulResolution

Conflict/problem was handled successfully.

FIncidentRecord, Scoring

Instruction result

Value

Meaning

Used by

Accepted

Instruction was allowed and can be executed.

Comms Router

Rejected\_InvalidCallsign

Callsign not found.

Comms Router / validation path

Rejected\_PhysicallyImpossible

Instruction cannot safely/physically be done.

Validator

Rejected\_AircraftExited

Aircraft has already left the sector.

Comms Router

Rejected\_ConflictAdvisory

Instruction rejected because of safety/conflict concerns.

Comms Router / conflict-aware checks

EWakeCategory

Value

Meaning

Used by

Light

Aircraft with max takeoff weight under 7,000kg

FAircraftState, Conflict Detector

Medium

Aircraft with max takeoff weight 7,000-136,000kg

FAircraftState, Conflict Detector

Heavy

Aircraft with max takeoff weight 136,000-560,000kg

FAircraftState, Conflict Detector

Super

Aircraft with max takeoff weight over 560,000kg (A380, AN-225)

FAircraftState, Conflict Detector

// ============================================================================

1.  CORE DATA STRUCTS

// ============================================================================

FAircraftState

Field

Type

Meaning

Callsign

FName

Unique aircraft identifier.

Position

FVector

Current aircraft position in the world/sector.

Altitude

float

Current altitude in feet.

Speed

float

Current speed in knots.

Heading

float

Current heading in degrees.

Velocity

FVector

Current movement vector.

FlightPhase

EFlightPhase

Current stage of flight.

ClimbRate

float

Vertical rate in feet per minute.

TargetAltitude

float

Altitude the aircraft is trying to reach.

TargetHeading

float

Heading the aircraft is trying to reach.

TargetSpeed

float

Speed the aircraft is trying to reach.

bHasActiveInstruction

bool

Whether an instruction is currently active.

bIsValid

bool

Whether the state is valid for use.

TimeEnteredSector

float

Time when the aircraft entered the sector.

WakeCategory

EWakeCategory

Wake turbulence category based on max takeoff weight.

BankAngle

float

Current bank angle in degrees, limited by performance category

ServiceCeiling

Float

Maximum operational altitude for this aircraft type in feet

MinOperatingSpeed

Float

Minimum safe speed above stall in knots.

MaxOperatingSpeed

Float

Maximum operational speed in knots.

MaxClimbRate

Float

Maximum climb rate for performance category in feet per minute.

FAircraftInstruction

Field

Type

Meaning

TargetCallsign

FName

Which aircraft the instruction is for.

Type

EInstructionType

What kind of instruction it is.

TargetValue

float

The numeric target, such as altitude, speed, or heading.

IssuedTime

float

When the instruction was issued.

bIsGoAround

bool

Whether this instruction was system-triggered rather than player-issued.

FConflictEvent

Field

Type

Meaning

AircraftA

FName

First aircraft in the conflict.

AircraftB

FName

Second aircraft in the conflict.

HorizontalSeparationNm

float

Horizontal separation in nautical miles.

VerticalSeparationFt

float

Vertical separation in feet.

AlertLevel

EAlertLevel

Severity of the event.

TimeOfDetection

float

When the conflict was detected.

bRequiresGoAround

bool

Whether the situation requires a go-around.

FIncidentRecord

Field

Type

Meaning

Type

EIncidentType

Incident category.

AircraftA

FName

First aircraft involved.

AircraftB

FName

Second aircraft involved if relevant.

TimeStamp

float

When the incident occurred.

Details

FString

Notes about the incident.

FAircraftSpawnData

Field

Type

Meaning

Callsign

FName

Aircraft identifier.

EntryPosition

FVector

Spawn position in the sector.

EntryAltitude

float

Starting altitude.

EntrySpeed

float

Starting speed.

EntryHeading

float

Starting heading.

InitialPhase

EFlightPhase

Starting flight phase.

FSectorEnvironment

Field

Type

Meaning

WindDirection

Float

Wind direction in degrees

WindSpeed

Float

Wind speed in knots

ActiveRunwayHeading

Float

Currently active runway based on wind direction

AvailableRunways

TArray(float)

All runway headings available for selection in the sector

// ============================================================================

1.  DELEGATES - Event-Driven Communication Between Systems

// ============================================================================

**Delegate**

**Broadcast by**

**Meaning**

FOnAircraftRegistered

Airspace Manager

A new aircraft was added to the sector.

FOnAircraftDeregistered

Airspace Manager

An aircraft was removed from the sector.

FOnAircraftStateUpdated

Airspace Manager

Aircraft state changed.

FOnConflictDetected

Conflict Detector

A conflict has been found.

FOnConflictResolved

Conflict Detector

A previously active conflict has been resolved.

FOnGoAroundRequired

Conflict Detector

An aircraft should perform a go-around.

FOnInstructionResult

Comms Router

An instruction was accepted or rejected.

FOnAdvisoryWarning

Comms Router

A warning/advisory should be shown to the player.

FOnScoreUpdated

Scoring

Score changed.

FOnDifficultyAdjusted

Scoring

Spawn rate / difficulty changed.

FOnWakeTurbulenceAdvisory

Conflict Detector

Wake separation has fallen below minimum for category combination.

FOnRunwayChanged

Airspace Manager

Active runway has changed due to wind shift.

// ============================================================================

1.  SYSTEMS SUMARRY

// ============================================================================

System

Main job

Key inputs

Key outputs

Important rule

AClearanceAirspaceManager

Store authoritative aircraft state.

Registrations, state updates

Aircraft queries, update broadcasts

Single source of truth: manages active runway based on wind conditions

UClearanceAircraftBehaviour

Execute movement gradually.

Validated instructions, current aircraft state

Updated aircraft state

Sole movement executor, applies flight dynamics including wind drift and atmospheric density effects

UClearanceCommsRouter

Accept, validate, and route instructions.

Player commands, system go-around requests

Instruction results, routed commands

Does not move aircraft directly

UClearanceInstructionValidator

Check if instructions are plausible.

Current state, requested instruction

Pass/fail with reason

Reject impossible commands

UClearanceConflictDetector

Monitor for current and projected conflicts.

Read-only aircraft snapshots

Conflict events, go-around requests, wake turbulence advisories

Read-only safety monitor

UClearanceScoring

Log outcomes, calculate score, scale difficulty.

Incidents and gameplay events

Score updates, spawn-rate changes

Tracks performance, not aircraft state

AClearanceAircraftSpawner

Spawn aircraft into the sector.

Spawn config and difficulty/spawn rate

New aircraft registrations

Controls entry only, not behaviour

AClearanceSimulationController

Run the simulation in the right order.

References to all core systems

Full session orchestration

Coordinates dependency order

// ============================================================================

1.  PER-SYSTEMS BREAKDOWN

// ============================================================================

**Airspace Manager**

Section

Contents

Public functions

Register, deregister, query one aircraft, query all aircraft, count aircraft, check callsign, request state update, clear all aircraft.

Public settings

Max aircraft count, safe altitude and speed limits.

Events

Registered, deregistered, state updated.

Private data

AircraftStates map.

Private helpers

ValidateState, ClampStateValues.

Environment functions

Get current environment, update wind conditions, get active runway, recalculate active runway on wind change.

Environment settings

Default wind direction, default wind speed, available runway headings, crosswind component limit.

Environment events

Runway changed.

Environment data

SectorEnvironment struct.

**Aircraft Behaviour**

**Section**

**Contents**

Public functions

Initialise, update each tick, queue instruction, check queue, clear instructions, execute go-around, status queries.

Private data

Pending instruction list, current instruction, execution flag.

Motion functions

Heading change, altitude change, speed change, position update, approach path, go-around.

Tuning values

Turn rate, climb rate, descent rate, acceleration.

Helpers

Shortest turn direction, target reached check, queue advance.

Physics functions

Apply wind drift to ground track, calculate density-adjusted climb rate, enforce bank angle limit, check service ceiling.

Physics tuning values

Bank angle limit per category, service ceiling per category, ISA density lookup table, max climb rate per category.

Physics helpers

ISA density at altitude, wind drift calculation, ground track from heading and wind.

**Comms Router**

**Section**

**Contents**

Public functions

Issue instruction, route go-around, receive advisory, set references.

Events

Instruction result, advisory warning.

Private data

References to Airspace Manager, Validator, Behaviour Map.

Control logic

Last instruction time per aircraft, minimum interval between instructions.

**Instruction Validator**

**Section**

**Contents**

Public function

Validate instruction against current state.

Private validation rules

Heading, altitude, speed, approach checks.

Constraints

Minimum/maximum altitude, minimum/maximum speed, stall speed.

Physics validation rules

Service ceiling check, minimum operating speed check, maximum operating speed check, wake separation check on approach.

Physics constraints

Service ceiling per aircraft type, min operating speed per aircraft type, max operating speed per aircraft type, wake separation matrix.

**Conflict Detector**

**Section**

**Contents**

Public functions

Detect current conflicts, detect projected conflicts, query alert level, remove departed aircraft from monitoring.

Events

Conflict detected, conflict resolved, go-around required.

Thresholds

Advisory 8nm, Warning 5nm, Critical 3nm, vertical minimum 1000ft.

Private data

Active conflict map.

Helpers

Conflict test, alert calculation, projected position, horizontal/vertical separation, key generation, go-around rule.

Wake functions

Detect wake turbulence violations, calculate required wake separation by category pair, monitor following distance behind heavier aircraft.

Wake events

Wake turbulence advisory.

Wake thresholds

Light following Heavy 6nm, Medium following Heavy 5nm, Light following Medium 5nm, Heavy following Heavy 4nm, standard 3nm minimum otherwise.

Wake helpers

Wake category lookup, required separation calculation, following aircraft detection.

**Scoring**

**Section**

**Contents**

Public functions

Log incidents, landings, departures, go-arounds, instructions; query score and efficiency; reset session; get log.

Events

Score updated, difficulty adjusted.

Session data

Incident log, current score, totals for landings/departures/go-arounds/instructions/handled aircraft.

Scoring values

Points for landing/departure and penalties for losses and failures.

Difficulty values

Base, min, max, and current spawn rate plus scaling factor.

Helpers

Recalculate score, adjust difficulty, calculate efficiency.

**Aircraft Spawner**

**Section**

**Contents**

Public functions

Tick, set references, spawn aircraft, set spawn rate, enable/disable auto-spawn.

Public settings

Spawn pool, max concurrent aircraft.

Private data

References to Airspace Manager and Scoring, spawn timer, current spawn rate, auto-spawn toggle, callsign counter.

Helpers

Generate callsign, generate random spawn data, random entry point/altitude/speed/heading.

**Simulation Controller**

**Section**

**Contents**

Public functions

BeginPlay, Tick, start/pause/resume/end session, check session active, player issue instruction, get Airspace Manager, get Scoring.

Core references

Airspace Manager, Spawner, Conflict Detector, Comms Router, Validator, Scoring, Behaviour Map.

Session state

bSessionActive, SessionTime.

Simulation functions

Step simulation, update behaviours, run conflict check, process go-arounds, check exits, update difficulty.

Setup functions

Initialise systems, bind delegates.

Event handlers

Aircraft registered, aircraft deregistered, conflict detected, go-around required, difficulty adjusted.