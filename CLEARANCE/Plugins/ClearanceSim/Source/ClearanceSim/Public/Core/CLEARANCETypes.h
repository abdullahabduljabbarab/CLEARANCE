// CLEARANCE - Core simulation types (enums + structs).
// Source of truth for the data shared across all ATC simulation systems.
// Mirrors "C++ Scaffold - Clearance" pre-production doc. All types are
// BlueprintType so the presentation layer (radar/HUD widgets) can read them.
#pragma once

#include "CoreMinimal.h"
#include "CLEARANCETypes.generated.h"

// ============================================================================
// ENUMS
// ============================================================================

/** Stage of flight for an aircraft in the sector. */
UENUM(BlueprintType)
enum class EFlightPhase : uint8
{
	Enroute		UMETA(DisplayName = "Enroute"),		// Cruising through the sector
	Approach	UMETA(DisplayName = "Approach"),	// Lining up to land
	Landing		UMETA(DisplayName = "Landing"),		// In the landing segment
	GoAround	UMETA(DisplayName = "Go-Around"),	// Aborted landing, climbing away
	Departing	UMETA(DisplayName = "Departing"),	// Leaving after takeoff
	Exiting		UMETA(DisplayName = "Exiting")		// Leaving the sector
};

/** Kind of instruction the player (or system) issues to an aircraft. */
UENUM(BlueprintType)
enum class EInstructionType : uint8
{
	HeadingChange		UMETA(DisplayName = "Heading Change"),
	AltitudeChange		UMETA(DisplayName = "Altitude Change"),
	SpeedChange			UMETA(DisplayName = "Speed Change"),
	Hold				UMETA(DisplayName = "Hold"),
	ApproachClearance	UMETA(DisplayName = "Approach Clearance"),
	TakeoffClearance	UMETA(DisplayName = "Takeoff Clearance"),
	ExitSector			UMETA(DisplayName = "Exit Sector")
};

/** Severity of a conflict / safety event. */
UENUM(BlueprintType)
enum class EAlertLevel : uint8
{
	None		UMETA(DisplayName = "None"),		// No safety issue
	Advisory	UMETA(DisplayName = "Advisory"),	// Early warning, monitor
	Warning		UMETA(DisplayName = "Warning"),		// Higher-risk separation issue
	Critical	UMETA(DisplayName = "Critical")		// Immediate danger
};

/** NATO-style threat classification carried per aircraft. Used in GCI / air-defence
 *  mode to mark contacts as friend/foe/unknown. STANAG-style tagging. */
UENUM(BlueprintType)
enum class EThreatClass : uint8
{
	Friendly  UMETA(DisplayName = "Friendly"),
	Hostile   UMETA(DisplayName = "Hostile"),
	Unknown   UMETA(DisplayName = "Unknown"),
	Neutral   UMETA(DisplayName = "Neutral")
};

// ICAO emergency states. The squawk code is the universal IFF signal - civilian
// pilots set these and any controller / federation peer immediately sees what's
// wrong. None = normal operation. - TripleA
UENUM(BlueprintType)
enum class EEmergencyType : uint8
{
	None                UMETA(DisplayName = "None"),
	GeneralMayday       UMETA(DisplayName = "General Mayday (7700)"),
	CommsFailure        UMETA(DisplayName = "Comms Failure (7600)"),
	Hijack              UMETA(DisplayName = "Hijack (7500)"),
	FuelLow             UMETA(DisplayName = "Fuel Emergency")
};

/** Category of a logged incident / outcome (drives scoring). */
UENUM(BlueprintType)
enum class EIncidentType : uint8
{
	SeparationLoss			UMETA(DisplayName = "Separation Loss"),
	UnresolvedExit			UMETA(DisplayName = "Unresolved Exit"),
	MissedHandoff			UMETA(DisplayName = "Missed Handoff"),
	GoAroundTriggered		UMETA(DisplayName = "Go-Around Triggered"),
	LateInstruction			UMETA(DisplayName = "Late Instruction"),
	WakeEncounter			UMETA(DisplayName = "Wake Encounter"),
	TCASResolutionAdvisory	UMETA(DisplayName = "TCAS Resolution Advisory"),
	SuccessfulLanding		UMETA(DisplayName = "Successful Landing"),
	SuccessfulDeparture		UMETA(DisplayName = "Successful Departure"),
	SuccessfulResolution	UMETA(DisplayName = "Successful Resolution"),
	SuccessfulIntercept		UMETA(DisplayName = "Successful Intercept"),
	// Operator declared a confirmed civilian (IFF on, not military) as hostile.
	// The single biggest failure in air defence doctrine - Vincennes / KAL-007 /
	// PS752 territory. Catastrophic score penalty; further scrambles locked for
	// the rest of the session. - TripleA
	MisidentifiedCivilian	UMETA(DisplayName = "Misidentified Civilian"),
	// A declared-hostile aircraft reached a protected violation zone (downtown,
	// airbase, etc.). The mirror of MisidentifiedCivilian - the operator either
	// missed the intercept call, declared too late, or vectored too slowly. Same
	// catastrophic weight. - TripleA
	ViolationZoneBreached	UMETA(DisplayName = "Violation Zone Breached"),
	// Aircraft declared an emergency (7700 / 7600 / 7500 / fuel) and was landed or
	// exited the sector safely under the operator's care. Real ATC rewards calm
	// emergency handling - so do we. - TripleA
	SuccessfulEmergencyHandling UMETA(DisplayName = "Successful Emergency Handling"),
	// Aircraft destroyed - fuel exhaustion in an unhandled fuel emergency, or
	// any other catastrophic loss. - TripleA
	AircraftCrashed			UMETA(DisplayName = "Aircraft Crashed")
};

/** Result of submitting an instruction through the Communication System. */
UENUM(BlueprintType)
enum class EInstructionResult : uint8
{
	Accepted					UMETA(DisplayName = "Accepted"),
	Rejected_InvalidCallsign	UMETA(DisplayName = "Rejected - Invalid Callsign"),
	Rejected_PhysicallyImpossible	UMETA(DisplayName = "Rejected - Physically Impossible"),
	Rejected_AircraftExited		UMETA(DisplayName = "Rejected - Aircraft Exited"),
	Rejected_ConflictAdvisory	UMETA(DisplayName = "Rejected - Conflict Advisory"),
	// NORDO contact - target's IFF is off and we have no comms with it. The
	// instruction is silently ignored on the aircraft side; the controller hears
	// nothing back. This is the giveaway that lets a sharp operator spot a hostile
	// posing as unknown traffic. - TripleA
	Rejected_NoResponse			UMETA(DisplayName = "Rejected - No Response")
};

// Wake category drives the separation matrix later, so it has to be set per
// aircraft at spawn, not guessed at conflict time. - TripleA
UENUM(BlueprintType)
enum class EWakeCategory : uint8
{
	Light	UMETA(DisplayName = "Light"),	// MTOW under 7,000 kg
	Medium	UMETA(DisplayName = "Medium"),	// MTOW 7,000 - 136,000 kg
	Heavy	UMETA(DisplayName = "Heavy"),	// MTOW 136,000 - 560,000 kg
	Super	UMETA(DisplayName = "Super")	// MTOW over 560,000 kg (A380, AN-225)
};

// ============================================================================
// CORE DATA STRUCTS
// ============================================================================

/**
 * Authoritative state of a single aircraft. Owned by the Airspace Manager;
 * read by every other system; never mutated outside the Airspace Manager. - TripleA
 */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FAircraftState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	FName Callsign = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	float Altitude = 0.f;			// feet

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	float Speed = 0.f;				// knots

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	float Heading = 0.f;			// degrees

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	EFlightPhase FlightPhase = EFlightPhase::Enroute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	float ClimbRate = 0.f;			// feet per minute

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	float TargetAltitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	float TargetHeading = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Targets")
	float TargetSpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	bool bHasActiveInstruction = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	bool bIsValid = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aircraft")
	float TimeEnteredSector = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	EWakeCategory WakeCategory = EWakeCategory::Medium;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	float BankAngle = 0.f;			// degrees, limited by performance category

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	float ServiceCeiling = 0.f;		// feet

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	float MinOperatingSpeed = 0.f;	// knots

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	float MaxOperatingSpeed = 0.f;	// knots

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance")
	float MaxClimbRate = 0.f;		// feet per minute

	// --- GCI / air-defence tagging ----------------------------------------
	// What this contact actually IS (the truth). The operator's classification of a
	// track lives elsewhere; this is what an IFF interrogation would resolve to.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GCI")
	EThreatClass ThreatClass = EThreatClass::Friendly;

	// SSR / Mode A "squawk" code (octal, 0-7777). 1200 = VFR civilian default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GCI")
	int32 SquawkCode = 1200;

	// Does the IFF respond to interrogation? Hostile contacts often run silent.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GCI")
	bool bIFFOperational = true;

	// While true, civilian ATC can't issue instructions to this aircraft - it's
	// under air defence control (a classified hostile, or a fighter dispatched on
	// an intercept). ATC's safety nets (TCAS, conflict alerts, separation penalty)
	// are also suppressed for hostile-involved pairs. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GCI")
	bool bUnderGCIControl = false;

	// Military aircraft draw their visual from the Controller's FighterVariants pool
	// (so an F-35 mesh doesn't randomly appear on a civilian airliner). Independent of
	// threat class - a friendly military intercept aircraft and a hostile fighter both
	// get fighter mesh assignment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GCI")
	bool bIsMilitary = false;

	// True if this aircraft is fed in from an outside source (a DIS feed, a network
	// peer). The local Behaviour doesn't fly it, the local player can't command it,
	// and the local emitter doesn't re-broadcast it. Truth lives with whoever sent
	// it; we just mirror it onto our scope. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	bool bIsExternal = false;

	// Currently-declared emergency (or None for normal flight). Sets the squawk
	// code automatically when transitioning out of None - 7700/7600/7500. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emergency")
	EEmergencyType ActiveEmergency = EEmergencyType::None;

	// Real-world minutes of fuel remaining. Only meaningful while ActiveEmergency
	// is FuelLow - decremented each tick by the Controller. Aircraft crashes when
	// this hits zero. -1 = not tracked. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emergency")
	float FuelRemainingMinutes = -1.f;

	// Session-time seconds at which the emergency was declared - used for the
	// debug readout (how long has this been going on?) and AAR review. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emergency")
	float EmergencyDeclaredAtSeconds = 0.f;

	// Aircraft has run out of options and is physically falling. Behaviour stops
	// flying it; the Controller takes over and drives it to the ground each tick.
	// On ground impact the score / wreck site / lost-contact call fire. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emergency")
	bool bCrashing = false;

	// Free-text description of WHAT'S wrong - engine failure, smoke in cockpit,
	// bird strike, etc. Real MAYDAYs always include the cause; the operator
	// needs to know whether to clear them for fast approach (engine fire) or
	// slow controlled descent (medical emergency). - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emergency")
	FString EmergencyDetail;
};

/** A single instruction targeted at one aircraft. */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FAircraftInstruction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instruction")
	FName TargetCallsign = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instruction")
	EInstructionType Type = EInstructionType::HeadingChange;

	/** Numeric target: altitude (ft), speed (kts), or heading (deg) per Type. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instruction")
	float TargetValue = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instruction")
	float IssuedTime = 0.f;

	/** True when system-triggered (e.g. go-around) rather than player-issued. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instruction")
	bool bIsGoAround = false;

	/** Heading turns: -1 = turn left, +1 = turn right, 0 = shortest way. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instruction")
	int32 TurnDirection = 0;

	/** Altitude changes: expedite the climb/descent (faster rate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instruction")
	bool bExpedite = false;
};

/** A detected separation conflict between two aircraft. */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FConflictEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conflict")
	FName AircraftA = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conflict")
	FName AircraftB = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conflict")
	float HorizontalSeparationNm = 0.f;	// nautical miles

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conflict")
	float VerticalSeparationFt = 0.f;	// feet

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conflict")
	EAlertLevel AlertLevel = EAlertLevel::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conflict")
	float TimeOfDetection = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conflict")
	bool bRequiresGoAround = false;
};

/** A logged incident / outcome for scoring and session review. */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FIncidentRecord
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incident")
	EIncidentType Type = EIncidentType::SeparationLoss;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incident")
	FName AircraftA = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incident")
	FName AircraftB = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incident")
	float TimeStamp = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Incident")
	FString Details;
};

/** Initial data used to spawn / register an aircraft into the sector. */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FAircraftSpawnData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FName Callsign = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	FVector EntryPosition = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	float EntryAltitude = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	float EntrySpeed = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	float EntryHeading = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	EFlightPhase InitialPhase = EFlightPhase::Enroute;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	EWakeCategory WakeCategory = EWakeCategory::Medium;
};

/** One runway: where its threshold is (sim nm, X=East/Y=North) and the heading
 *  flown to land on it. Built from placed runway actors. */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FRunwayInfo
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runway")
	FVector2D ThresholdNm = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runway")
	float HeadingDeg = 270.f;
};

/** What the RADAR believes about one aircraft. Distinct from the truth held in the
 *  Airspace Manager: position is the last paint (with sensor noise), heading/speed
 *  are estimated, callsign is only present if the transponder return was good. */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FRadarTrack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	FName TruthCallsign = NAME_None;       // internal, always set - used to match to truth

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	FName DisplayCallsign = NAME_None;     // shown to player; empty if no secondary return

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	FVector Position = FVector::ZeroVector; // nm (X=East, Y=North), with sensor jitter

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	float Altitude = 0.f;                   // ft - precise if secondary, estimated if primary only

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	float Heading = 0.f;                    // deg, estimated from successive paints

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	float Speed = 0.f;                      // kt, estimated

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	bool bHasSecondary = false;             // true if the transponder (SSR) responded

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	float LastPaintTime = 0.f;              // session-time, for fading old tracks

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	float Confidence = 0.f;                 // 0..1, fades with time since last paint
};

/** One snapshot of the whole sector at a moment in time, captured by the recorder. */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FRecordedSnapshot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording")
	float TimeStamp = 0.f; // session-relative seconds

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording")
	TArray<FAircraftState> States;
};

/** A point-in-time event (instruction, conflict, score change, etc) on the timeline. */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FRecordedEvent
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording")
	float TimeStamp = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Recording")
	FString Description;
};

/** Sector-wide environmental state owned by the Airspace Manager. */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FSectorEnvironment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float WindDirection = 0.f;			// degrees

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float WindSpeed = 0.f;				// knots

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	float ActiveRunwayHeading = 0.f;	// degrees, of the selected runway

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	FVector ActiveRunwayThreshold = FVector::ZeroVector; // nm (X=East, Y=North) of selected runway

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	TArray<float> AvailableRunways;		// selectable runway headings (deg), for display
};

// ============================================================================
// DELEGATES - event-driven communication between systems (BlueprintAssignable)
// ============================================================================

// Airspace Manager
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAircraftRegistered, FName, Callsign);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAircraftDeregistered, FName, Callsign);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAircraftStateUpdated, FName, Callsign);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRunwayChanged, float, NewRunwayHeading);

// Conflict Detector
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConflictDetected, FConflictEvent, Conflict);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConflictResolved, FConflictEvent, Conflict);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGoAroundRequired, FName, Callsign);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWakeTurbulenceAdvisory, FName, FollowingCallsign, FName, LeadingCallsign, float, RequiredSeparationNm);
// Coordinated TCAS RA: higher aircraft climbs, lower descends, to the given target
// altitudes. Last-resort split when separation has been lost. - TripleA
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnTCASResolutionAdvisory, FName, ClimberCallsign, FName, DescenderCallsign, float, ClimberTargetAltitudeFt, float, DescenderTargetAltitudeFt);

// Comms Router
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInstructionResult, FName, Callsign, EInstructionResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAdvisoryWarning, FString, Message, EAlertLevel, Level);

// Scoring
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnScoreUpdated, int32, NewScore);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDifficultyAdjusted, float, NewSpawnRate);
