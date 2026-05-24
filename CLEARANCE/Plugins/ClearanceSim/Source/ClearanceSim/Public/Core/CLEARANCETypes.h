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

/** Category of a logged incident / outcome (drives scoring). */
UENUM(BlueprintType)
enum class EIncidentType : uint8
{
	SeparationLoss			UMETA(DisplayName = "Separation Loss"),
	UnresolvedExit			UMETA(DisplayName = "Unresolved Exit"),
	MissedHandoff			UMETA(DisplayName = "Missed Handoff"),
	GoAroundTriggered		UMETA(DisplayName = "Go-Around Triggered"),
	LateInstruction			UMETA(DisplayName = "Late Instruction"),
	SuccessfulLanding		UMETA(DisplayName = "Successful Landing"),
	SuccessfulDeparture		UMETA(DisplayName = "Successful Departure"),
	SuccessfulResolution	UMETA(DisplayName = "Successful Resolution")
};

/** Result of submitting an instruction through the Communication System. */
UENUM(BlueprintType)
enum class EInstructionResult : uint8
{
	Accepted					UMETA(DisplayName = "Accepted"),
	Rejected_InvalidCallsign	UMETA(DisplayName = "Rejected - Invalid Callsign"),
	Rejected_PhysicallyImpossible	UMETA(DisplayName = "Rejected - Physically Impossible"),
	Rejected_AircraftExited		UMETA(DisplayName = "Rejected - Aircraft Exited"),
	Rejected_ConflictAdvisory	UMETA(DisplayName = "Rejected - Conflict Advisory")
};

/** Wake turbulence category, derived from max takeoff weight (ICAO). */
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
 * read by every other system; never mutated outside the Airspace Manager.
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
	float ActiveRunwayHeading = 0.f;	// degrees, chosen from AvailableRunways

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Environment")
	TArray<float> AvailableRunways;		// all selectable runway headings (deg)
};
