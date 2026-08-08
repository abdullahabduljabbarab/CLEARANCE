// CLEARANCE - Core simulation types (enums + structs).
// Source of truth for the data shared across all ATC simulation systems.
// Mirrors "C++ Scaffold - Clearance" pre-production doc. All types are
// BlueprintType so the presentation layer (radar/HUD widgets) can read them.
#pragma once

#include "CoreMinimal.h"
#include "CLEARANCETypes.generated.h"

// ============================================================================
// COORDINATE HELPERS
// ============================================================================

// CLEARANCE runs in Cesium for Unreal's local frame at the georeference origin,
// which is East-North-Up (ENU) - +X points East, +Y points North, +Z is Up.
// Compass bearings are measured clockwise from North (0 = N, 90 = E, 180 = S,
// 270 = W). All bearing-to-direction conversions go through these helpers so
// the sim math matches Cesium's real-world coordinate frame; aircraft flying
// heading 090 physically move East along +X, runways with LandingHeadingDeg=70
// render at 070 magnetic on the Cesium tiles, and every downstream system
// (radar, autopilot, federation output) stays coordinate-consistent. - TripleA
namespace ClearanceCoords
{
	// Direction unit vector in world XY for a compass bearing (degrees).
	// ENU: X = East = sin(bearing), Y = North = cos(bearing). - TripleA
	FORCEINLINE FVector BearingToDir(float BearingDeg)
	{
		const float Rad = FMath::DegreesToRadians(BearingDeg);
		return FVector(FMath::Sin(Rad), FMath::Cos(Rad), 0.f);
	}

	FORCEINLINE FVector2D BearingToDir2D(float BearingDeg)
	{
		const float Rad = FMath::DegreesToRadians(BearingDeg);
		return FVector2D(FMath::Sin(Rad), FMath::Cos(Rad));
	}

	// Compass bearing (degrees, [0, 360)) for a direction vector in ENU. - TripleA
	FORCEINLINE float DirToBearing(const FVector2D& Dir)
	{
		const float Deg = FMath::RadiansToDegrees(FMath::Atan2(Dir.X, Dir.Y));
		return FMath::Fmod(Deg + 360.f, 360.f);
	}
}

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
	ExitSector			UMETA(DisplayName = "Exit Sector"),
	DeclareTrackLost	UMETA(DisplayName = "Declare Track Lost")  // "no joy", EW disengage
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
	SuccessfulHandoff		UMETA(DisplayName = "Successful Handoff"),
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
	AircraftCrashed			UMETA(DisplayName = "Aircraft Crashed"),
	// Civilian aircraft entered a restricted area (military training zone, P-area,
	// nuclear site, etc.). Different from a ViolationZoneBreached - this is a
	// civilian screw-up the controller should have prevented, not a hostile reaching
	// a critical target. Smaller penalty. - TripleA
	RestrictedAirspaceBust	UMETA(DisplayName = "Restricted Airspace Bust")
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
	// Operator-facing classification: what the trainee has identified the contact
	// as. Starts at whatever the spawner / scenario sets (Unknown for bandits,
	// matches TrueAffiliation for civilians + own forces) and changes when the
	// operator calls Reclassify. Used by the operator scope, scoring mis-ID
	// checks, and the player's interrogation workflow. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GCI")
	EThreatClass ThreatClass = EThreatClass::Neutral;

	// Ground truth - what the contact ACTUALLY is. Set once at spawn and never
	// touched by Reclassify. Used by the instructor scope (god view) and any
	// behaviour that needs to know the real disposition (bandit AI, voice line
	// selection, scoring mis-ID detection). Civilians + own forces have
	// TrueAffiliation == ThreatClass; bandits start with ThreatClass=Unknown
	// and TrueAffiliation=Hostile so the instructor sees the truth while the
	// operator has to work it out. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GCI")
	EThreatClass TrueAffiliation = EThreatClass::Neutral;

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

	// Which federate owns this aircraft. 0 = local (this instance owns it);
	// >0 = the peer's Site ID (this instance received it via DIS/DDS ingest).
	// Drives the "OWN" / "SITE N" chip on the aircraft list row so the
	// operator can tell at a glance whose aircraft they're looking at. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Federation")
	int32 OwnerSiteId = 0;

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

	// Aircraft is flying a racetrack holding pattern. Behaviour drives the
	// turn-and-leg pattern from these fields. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hold")
	bool bInHold = false;

	// True = right-hand pattern (standard ICAO), false = non-standard left turns.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hold")
	bool bHoldRightTurns = true;

	// SessionTime when the current hold leg started, used to time the racetrack.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hold")
	float HoldLegStartSeconds = 0.f;

	// 0 = flying the outbound leg, 1 = first turn, 2 = inbound leg, 3 = second turn.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hold")
	int32 HoldLegPhase = 0;

	// The heading the aircraft was on when it entered the hold - the inbound leg
	// flies this heading, the outbound is the reciprocal.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hold")
	float HoldInboundHeading = 0.f;

	// Highest conflict alert this aircraft is involved in. Computed server-side
	// by UClearanceConflictDetector and stamped into the state each tick so it
	// rides the replication stream out to clients. The client doesn't run a
	// detector of its own - it just colours the debug overlay from this. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conflict")
	EAlertLevel CurrentAlertLevel = EAlertLevel::None;

	// Electronic warfare: when true, this aircraft is actively jamming. Each
	// radar that paints it produces a degraded track (low confidence, big
	// jitter) AND blankets neighbouring bearings from that radar with noise.
	// Pairs with the fusion layer - one jammed sensor doesn't lose the picture
	// when other sensors cover the same volume from a different angle. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW")
	bool bJammingOn = false;
};

// One chaff cloud release. Sits in the world for a few seconds scattering
// returns; every radar in line of sight reports it as a ghost track with
// no transponder. Fusion can't eliminate it (every sensor sees it, that's
// the point) but the lack of Mode C + static position betray it. - TripleA
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FChaffCloud
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW")
	FVector PositionNm = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW")
	float AltitudeFt = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW")
	float DropSessionTime = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "EW")
	float LifetimeSec = 12.f;
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

	// Asphalt length in world units (UE cm), from the runway actor's mesh
	// bounds. Used by camera-feed overlays to draw a centerline that
	// matches the actual strip rather than a fixed-length guess. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runway")
	float LengthUnits = 0.f;

	// Asphalt width, same source/units as LengthUnits. Lets the camera-feed
	// overlay outline the strip as a rectangle instead of a single
	// centerline. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runway")
	float WidthUnits = 0.f;

	// Hard override for the two-digit ICAO designator. 0 = compute from HeadingDeg
	// through the Cesium mirror. 1..36 = force this exact number at this end;
	// the reciprocal end automatically shows (this + 18) mod 36. Set when the
	// heading-based formula doesn't produce the label you want on your specific
	// runway placement. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Runway")
	int32 DesignatorOverride = 0;
};

/** One row on the operator's emergency / priority board. Auto-populated from
 *  aircraft state on the airspace manager - the operator doesn't manually add
 *  entries, the sim watches for abnormal states and lists them here sorted by
 *  urgency (shortest timer first). Matches how a real modern ATC position's
 *  CAAD / Emergency Alert Board works. - TripleA */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FOperatorEmergencyEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Emergency")
	FName Callsign = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "Emergency")
	EEmergencyType Type = EEmergencyType::None;

	// Minutes of countdown remaining. -1.f = no countdown (Hijack / CommsFailure
	// have no fuel clock). Widgets should show "--:--" for negative values and
	// pulse red when < 1.f (60s).
	UPROPERTY(BlueprintReadOnly, Category = "Emergency")
	float TimerMinutesRemaining = -1.f;

	UPROPERTY(BlueprintReadOnly, Category = "Emergency")
	int32 SquawkCode = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Emergency")
	EThreatClass ThreatClass = EThreatClass::Unknown;

	// Free-form detail string from the aircraft state (e.g. "smoke in cockpit"
	// for GeneralMayday). Empty if the emergency type has no extra context.
	UPROPERTY(BlueprintReadOnly, Category = "Emergency")
	FString Detail;
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

	// What the radar saw on the LAST paint, before time-based fade. Lets
	// EW pin a degraded read (jammed = 0.25, chaff ghost = 0.35) without
	// the per-tick fade pass resetting it back to full. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	float PaintConfidence = 1.f;

	// Operator's classification of this track, populated server-side in
	// RefreshOperatorTracks from the truth aircraft's ThreatClass (NOT
	// TrueAffiliation - operator scope shows what the trainee believes,
	// not god view). Default Neutral: ATC doctrine treats an unclassified
	// radar contact as unclassified-not-threatening rather than actively
	// suspicious, so ghost tracks + failed lookups read green rather than
	// amber "unknown affiliation." Consumed by the operator paint chain
	// to tint the affiliation symbol. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Radar")
	EThreatClass ThreatClass = EThreatClass::Neutral;
};

/** Radar emission signature - the IEEE 1278.1 fingerprint an ELINT receiver would
 * see for this radar. Everything here maps directly to Emission PDU (Type 23)
 * Fundamental Parameter Data fields. Real-world catalogues:
 *   Emitter Name (Annex A tab. 8): 3110 AN/APG-63, 3151 AN/APG-73,
 *     8790 ASR-9 (civil airport surveillance), 8791 ASR-9000, 8785 ASR-8
 *   Emitter Function (tab. 9): 3 Early-Warning/Surveillance, 5 Fire Control,
 *     6 Acquisition/Detection, 7 Tracker
 *   Beam Function (tab. 10): 2 Search, 3 Height Finder, 5 Tracking,
 *     6 Track-While-Scan
 * Bands: L 1-2 GHz, S 2-4 GHz, C 4-8 GHz, X 8-12 GHz, Ku 12-18 GHz.
 * - TripleA */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FEmissionSignature
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emission")
	float FrequencyLowHz = 9.5e9f;   // X-band search default

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emission")
	float FrequencyHighHz = 10.5e9f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emission")
	float PulseRepetitionFreqHz = 1000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emission")
	float PulseWidthMicrosec = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emission")
	float EffectiveRadiatedPowerDbm = 80.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emission")
	int32 EmitterName = 8790;   // ASR-9 civil airport surveillance (SISO-REF-010 UID 75)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emission", meta = (ClampMin = "0", ClampMax = "255"))
	int32 EmitterFunction = 22; // Air Traffic Control (SISO-REF-010 UID 76)

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emission", meta = (ClampMin = "0", ClampMax = "255"))
	int32 BeamFunction = 1;     // Search (SISO-REF-010 UID 78)
};

/** Per-tick snapshot handed to the DIS emitter for building an Emission PDU.
 * Assembled server-side by the SimulationController each tick from the active
 * radar sites. The emitter uses this to fill the PDU's per-emitter-system + per-
 * beam + per-target blocks without needing to reach back into the radar
 * objects. - TripleA */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FRadarEmissionSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName SiteName;
	UPROPERTY(BlueprintReadOnly) FVector2D SitePositionNm = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FEmissionSignature Signature;
	UPROPERTY(BlueprintReadOnly) float SweepAngleDeg = 0.f;
	UPROPERTY(BlueprintReadOnly) float RangeNm = 0.f;
	UPROPERTY(BlueprintReadOnly) TArray<FName> PaintedCallsigns;   // for beam Track/Jam block
	UPROPERTY(BlueprintReadOnly) bool bEnabled = false;
};

/** Snapshot of a weapons-fire event, handed to the DIS emitter for building a
 * Fire PDU (Type 2). Fired from SCRAMBLE / intercept launch, gun kills, and
 * any other point in the sim where a munition leaves an aircraft. - TripleA */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FWeaponsFireEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName FiringCallsign;    // aircraft that fired
	UPROPERTY(BlueprintReadOnly) FName TargetCallsign;    // intended target (NAME_None if untargeted)
	UPROPERTY(BlueprintReadOnly) FVector2D LocationNm = FVector2D::ZeroVector;   // world XY in sector nm
	UPROPERTY(BlueprintReadOnly) float AltitudeFt = 0.f;
	UPROPERTY(BlueprintReadOnly) float VelocityXKts = 0.f;     // launch velocity components
	UPROPERTY(BlueprintReadOnly) float VelocityYKts = 0.f;
	UPROPERTY(BlueprintReadOnly) float VelocityZKts = 0.f;

	// IEEE 1278.1 Munition Entity Type (Annex A):
	//   Kind 2 = Munition, Domain 2 = Air, Country 224 = UK,
	//   Category 1 = Guided (missile), 2 = Ballistic, 3 = Fixed
	// Combined with sensible defaults into a single lookup key for now.
	UPROPERTY(BlueprintReadOnly) uint8 MunitionKind = 1;       // 1 = missile, 2 = gun round, 3 = bomb
	UPROPERTY(BlueprintReadOnly) int32 WarheadKind = 1000;     // Annex A Warhead code, 1000 = HE
	UPROPERTY(BlueprintReadOnly) int32 FuseKind = 1000;        // Annex A Fuse code, 1000 = contact
	UPROPERTY(BlueprintReadOnly) int32 Quantity = 1;
	UPROPERTY(BlueprintReadOnly) int32 Rate = 0;               // rounds/min for cannon, 0 for missile
	UPROPERTY(BlueprintReadOnly) float RangeMeters = 0.f;      // effective range at launch
	UPROPERTY(BlueprintReadOnly) int32 EventNumber = 0;       // unique per launch, referenced by Detonation
};

/** Snapshot of a weapons-detonation event, handed to the DIS emitter for
 * building a Detonation PDU (Type 3). Fired when the earlier Fire event
 * resolves - hit, miss, or timeout. The EventNumber links back to the
 * originating Fire PDU so a federation observer can pair the two. - TripleA */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FWeaponsDetonationEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName FiringCallsign;     // aircraft that originally fired
	UPROPERTY(BlueprintReadOnly) FName TargetCallsign;     // aircraft that was hit (NAME_None if miss)
	UPROPERTY(BlueprintReadOnly) FVector2D LocationNm = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float AltitudeFt = 0.f;
	UPROPERTY(BlueprintReadOnly) float VelocityXKts = 0.f;
	UPROPERTY(BlueprintReadOnly) float VelocityYKts = 0.f;
	UPROPERTY(BlueprintReadOnly) float VelocityZKts = 0.f;

	UPROPERTY(BlueprintReadOnly) uint8 MunitionKind = 1;
	UPROPERTY(BlueprintReadOnly) int32 WarheadKind = 1000;
	UPROPERTY(BlueprintReadOnly) int32 FuseKind = 1000;
	UPROPERTY(BlueprintReadOnly) int32 Quantity = 1;
	UPROPERTY(BlueprintReadOnly) int32 Rate = 0;

	// IEEE 1278.1 Detonation Result codes (table 7-4):
	//   0 = Other, 1 = Entity Impact, 2 = Entity Proximate Detonation,
	//   3 = Ground Impact, 4 = Ground Proximate Detonation, 5 = Detonation,
	//   6 = None (dud/no detonation)
	UPROPERTY(BlueprintReadOnly) uint8 DetonationResult = 1;
	UPROPERTY(BlueprintReadOnly) int32 EventNumber = 0;   // matches the originating Fire PDU
};

/** Snapshot of a single radio transmission, handed to the DIS emitter for
 * building a Signal PDU (Type 26). Every voice / phraseology line the sim
 * produces - operator command, aircraft readback, controller inject - flows
 * through here so a federation observer can see the whole comms picture as
 * live traffic. Payload is opaque raw-binary per §7.7.3, which lets us carry
 * the ASCII transcript directly without a codec dependency. - TripleA */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FVoiceCommsEvent
{
	GENERATED_BODY()

	// Radio owner. NAME_None => operator / ground station (uses a fixed entity
	// number so downstream federates can still identify the source uniquely).
	UPROPERTY(BlueprintReadOnly) FName SpeakerCallsign;

	// The words on the wire (ASCII, trimmed). Length is capped in the emitter
	// so an oversize line can't blow the UDP MTU.
	UPROPERTY(BlueprintReadOnly) FString Transcript;

	// Which radio on the entity is transmitting. Most aircraft only ever use
	// radio 1; a full ATC federate might separate tower / approach / ground.
	UPROPERTY(BlueprintReadOnly) int32 RadioId = 1;

	// Carrier frequency in Hz. Defaulted to 121.500 MHz (international ATC
	// guard channel) since it's a plausible common frequency for the
	// federation to receive on.
	UPROPERTY(BlueprintReadOnly) int64 FrequencyHz = 121500000;
};

/** Snapshot of a single radio's current state, handed to the DIS emitter for
 * building a Transmitter PDU (Type 25). Announces "I have a radio at this
 * position on this frequency, currently in this transmit state". Federation
 * observers use this to discover who's on the air and what to tune to
 * before hearing the actual traffic on the paired Signal PDU. §7.7.2. - TripleA */
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FRadioTransmitter
{
	GENERATED_BODY()

	// Radio owner. NAME_None => operator / ground station.
	UPROPERTY(BlueprintReadOnly) FName OwnerCallsign;

	// Which radio on the entity (1 = primary).
	UPROPERTY(BlueprintReadOnly) int32 RadioId = 1;

	// Carrier frequency in Hz. 121.500 MHz = international ATC guard channel.
	UPROPERTY(BlueprintReadOnly) int64 FrequencyHz = 121500000;

	// Channel bandwidth in Hz. 25 kHz = VHF AM airband spacing.
	UPROPERTY(BlueprintReadOnly) float BandwidthHz = 25000.f;

	// Transmit power in decibel-milliwatts (dBm). 40 dBm = 10 W typical for
	// tower / handheld radios; airliner VHF is ~35-45 dBm.
	UPROPERTY(BlueprintReadOnly) float PowerDbm = 40.f;

	// Transmit state per §7.7.2.6: 0 = off, 1 = on-not-transmitting,
	// 2 = on-transmitting. Set to 2 for one tick when a voice line fires
	// for this radio, otherwise 1 (on-idle heartbeat).
	UPROPERTY(BlueprintReadOnly) uint8 TransmitState = 1;

	// World position of the antenna, ECEF metres. Aircraft radios report the
	// aircraft position; ground stations report their fixed location.
	UPROPERTY(BlueprintReadOnly) FVector AntennaWorldMeters = FVector::ZeroVector;
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
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnInstructionResult, FName, Callsign, FAircraftInstruction, Instruction, EInstructionResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAdvisoryWarning, FString, Message, EAlertLevel, Level);

// One line in the ATC comms transcript - operator command, pilot readback /
// refusal, or system advisory. Persisted on the Controller and replicated to
// the panel for the Performance > Transcript sub-tab. - TripleA
UENUM(BlueprintType)
enum class EClearanceCommsRole : uint8
{
	Operator	UMETA(DisplayName = "Operator"),   // The trainee - ATC on frequency
	Pilot		UMETA(DisplayName = "Pilot"),      // Aircraft transmissions
	System		UMETA(DisplayName = "System"),     // Auto sim events - squawk changes, wake-turbulence advisories

	// Instructor-scripted injects (emergency declared, threat reclassified,
	// scramble ordered, checkpoint saved etc). Distinct from System so the
	// transcript reader can tell "instructor caused this" from "sim ticked
	// and produced this". - TripleA
	Instructor	UMETA(DisplayName = "Instructor"),

	// External / support facilities that are neither the trainee nor the
	// pilots. Each renders in its own color so the transcript scans as a
	// real multi-facility comms log. - TripleA
	Tower		UMETA(DisplayName = "Tower"),      // Airport tower
	Acc		    UMETA(DisplayName = "ACC"),        // Area Control (enroute)
	Awacs		UMETA(DisplayName = "AWACS"),      // Airborne early warning
	Gci		    UMETA(DisplayName = "GCI"),        // Ground Control Intercept
	Atis		UMETA(DisplayName = "ATIS"),       // Automated terminal info
	Met		    UMETA(DisplayName = "MET")         // Meteorological
};

// Radio channel the operator's PTT transmits on. Aircraft only hear voice
// commands when their AssignedFrequency matches the operator's active
// channel. Real ATC channel discipline: outer-sector traffic on Approach,
// close-in traffic on Tower, emergency traffic on Emergency, universal
// distress on Guard. Handoff instructions ("BAW123 contact Tower on 118.5")
// reassign the addressed aircraft's frequency so the receiving controller
// takes over cleanly. - TripleA
UENUM(BlueprintType)
enum class ECommsFrequency : uint8
{
	None		UMETA(DisplayName = "None"),          // No channel selected (mic effectively cold)
	Tower		UMETA(DisplayName = "Tower"),         // Close-in: final approach, runway, taxi
	Approach	UMETA(DisplayName = "Approach"),      // Inbound: sector edge to ~10nm final, vectoring + spacing
	Emergency	UMETA(DisplayName = "Emergency"),     // Dedicated emergency coordination channel
	Guard		UMETA(DisplayName = "Guard")          // 121.5 MHz universal distress, always monitored
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FCommsTranscriptEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) float TimeSec = 0.f;
	UPROPERTY(BlueprintReadOnly) EClearanceCommsRole Role = EClearanceCommsRole::Operator;
	// Aircraft involved in the exchange (the same for both operator and pilot
	// entries about the same instruction). Use Speaker for the UI label. - TripleA
	UPROPERTY(BlueprintReadOnly) FName Callsign = NAME_None;
	// Pre-computed display label for the speaker column: "ATC" for operator
	// transmissions, the aircraft callsign for pilot transmissions, "SYS" for
	// emergency / classification / advisory lines. Saves the UI from branching
	// on Role to derive this. - TripleA
	UPROPERTY(BlueprintReadOnly) FString Speaker;
	UPROPERTY(BlueprintReadOnly) FString Text;
};

// Lightweight info about a saved checkpoint - what the instructor panel
// dropdown needs to render the row. The full payload (aircraft states,
// scoring log etc.) stays server-side; only this summary replicates. - TripleA
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FClearanceCheckpointInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName Name = NAME_None;
	UPROPERTY(BlueprintReadOnly) float SessionTimeAtSave = 0.f;
	UPROPERTY(BlueprintReadOnly) int32 AircraftCount = 0;
	UPROPERTY(BlueprintReadOnly) int32 ScoreAtSave = 0;
};

// Scoring
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnScoreUpdated, int32, NewScore);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDifficultyAdjusted, float, NewSpawnRate);
