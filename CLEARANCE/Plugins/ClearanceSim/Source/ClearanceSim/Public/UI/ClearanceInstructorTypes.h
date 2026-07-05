// Plain-data views the instructor UMG widget binds to. The widget polls the
// SimulationController each tick, builds these snapshots, then fires
// BlueprintImplementableEvents - the UMG only sees these structs, never the
// underlying actor pointers. Keeps the C++ <-> BP boundary clean. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceInstructorTypes.generated.h"

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FInstructorAircraftRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName Callsign;
	// Instructor view: ground truth - what this contact ACTUALLY is. Populated
	// from State.TrueAffiliation so the god-view scope sees the real disposition
	// (red diamond for hidden hostiles even when the operator still has them
	// as amber Unknown). Operator-facing classification lives in
	// OperatorClassification below for the future operator-view toggle. - TripleA
	UPROPERTY(BlueprintReadOnly) EThreatClass ThreatClass = EThreatClass::Unknown;
	// Operator's current classification (what the trainee believes). Used by
	// the operator-view toggle to render the degraded picture. - TripleA
	UPROPERTY(BlueprintReadOnly) EThreatClass OperatorClassification = EThreatClass::Unknown;
	UPROPERTY(BlueprintReadOnly) EFlightPhase FlightPhase = EFlightPhase::Enroute;
	UPROPERTY(BlueprintReadOnly) EEmergencyType ActiveEmergency = EEmergencyType::None;
	UPROPERTY(BlueprintReadOnly) EAlertLevel CurrentAlertLevel = EAlertLevel::None;

	// Minutes remaining on the emergency countdown - reused across FuelLow
	// (default 5 min via FuelEmergencyMinutes) AND GeneralMayday (default
	// 7 min via MaydayTimeoutMinutes). Hits zero -> aircraft crashes:
	// "Fuel exhaustion" for fuel, "Mayday situation deteriorated" for mayday.
	// Hijack and CommsFailure don't carry a timer (-1 in those cases and
	// when no emergency is active). Instructor UI shows "FUEL M:SS" or
	// "MAYDAY M:SS" per active type so the operator faces a live crash
	// countdown they have to work against. - TripleA
	UPROPERTY(BlueprintReadOnly) float EmergencyTimerMinutes = -1.f;

	UPROPERTY(BlueprintReadOnly) float Heading = 0.f;
	UPROPERTY(BlueprintReadOnly) float TargetHeading = 0.f;
	UPROPERTY(BlueprintReadOnly) float Altitude = 0.f;
	UPROPERTY(BlueprintReadOnly) float TargetAltitude = 0.f;
	UPROPERTY(BlueprintReadOnly) float Speed = 0.f;
	UPROPERTY(BlueprintReadOnly) float TargetSpeed = 0.f;
	UPROPERTY(BlueprintReadOnly) int32 SquawkCode = 0;

	UPROPERTY(BlueprintReadOnly) bool bJammingOn = false;
	UPROPERTY(BlueprintReadOnly) bool bUnderGCIControl = false;
	UPROPERTY(BlueprintReadOnly) bool bIsMilitary = false;
	UPROPERTY(BlueprintReadOnly) bool bIFFOperational = true;

	// nm, sector-relative. Use UClearanceInstructorPanel::ScopeNmToPixel
	// to project for the mini-scope. - TripleA
	UPROPERTY(BlueprintReadOnly) FVector2D PositionNm = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FInstructorScoreView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) int32 Total = 0;
	UPROPERTY(BlueprintReadOnly) float EfficiencyPct = 100.f;

	UPROPERTY(BlueprintReadOnly) int32 Landings = 0;
	UPROPERTY(BlueprintReadOnly) int32 Handoffs = 0;
	UPROPERTY(BlueprintReadOnly) int32 ResolvedConflicts = 0;
	UPROPERTY(BlueprintReadOnly) int32 Intercepts = 0;
	UPROPERTY(BlueprintReadOnly) int32 Emergencies = 0;

	UPROPERTY(BlueprintReadOnly) int32 GoArounds = 0;
	UPROPERTY(BlueprintReadOnly) int32 SepLoss = 0;
	UPROPERTY(BlueprintReadOnly) int32 WakeBusts = 0;
	UPROPERTY(BlueprintReadOnly) int32 TCAS = 0;
	UPROPERTY(BlueprintReadOnly) int32 Strayed = 0;
	UPROPERTY(BlueprintReadOnly) int32 MisID = 0;
	UPROPERTY(BlueprintReadOnly) int32 Violated = 0;
	UPROPERTY(BlueprintReadOnly) int32 Crashed = 0;
	UPROPERTY(BlueprintReadOnly) int32 Busted = 0;

	UPROPERTY(BlueprintReadOnly) float NextSpawnSec = 0.f;
};

// One section of the in-station instructor manual - authored as terse
// operator-oriented reference text. Title is the display heading; Anchor
// is the stable key BP uses to select it from the TOC; Body is plain
// text with `\n\n` paragraph breaks and simple inline markers:
//   **bold**   -> BP decorator swaps to bold weight
//   `code`     -> BP decorator swaps to monospace + cyan-accent tint
//   [ACCENT]   -> UPPERCASE-BRACKETED tokens colored cyan-primary (button names)
// - TripleA
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FManualSection
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Anchor;
	UPROPERTY(BlueprintReadOnly) FString Title;
	UPROPERTY(BlueprintReadOnly) FString Body;
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FInstructorScenarioView
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) bool bRunning = false;
	UPROPERTY(BlueprintReadOnly) FString Name;
	UPROPERTY(BlueprintReadOnly) float ElapsedSec = 0.f;
	UPROPERTY(BlueprintReadOnly) int32 FiredEvents = 0;
	UPROPERTY(BlueprintReadOnly) int32 TotalEvents = 0;
	UPROPERTY(BlueprintReadOnly) int32 FiredTriggers = 0;
	UPROPERTY(BlueprintReadOnly) int32 TotalTriggers = 0;
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FInstructorZoneMarker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName Name;
	UPROPERTY(BlueprintReadOnly) FVector2D PositionNm = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float RadiusNm = 0.f;
	// true = protected (hostile-must-not-reach), red on scope
	// false = restricted (civilian-must-avoid), amber on scope
	UPROPERTY(BlueprintReadOnly) bool bIsProtected = false;
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FInstructorChaffMarker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FVector2D PositionNm = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) float AltitudeFt = 0.f;
	UPROPERTY(BlueprintReadOnly) float AgeFrac = 0.f;   // 0 = fresh, 1 = expired
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FInstructorWaypointMarker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName Name;
	UPROPERTY(BlueprintReadOnly) FVector2D PositionNm = FVector2D::ZeroVector;
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FInstructorAirwaySegment
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FVector2D StartNm = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector2D EndNm   = FVector2D::ZeroVector;
};

// One floating label entry for the camera-feed HUD overlay. Computed by
// projecting the aircraft's world position through the PIP capture's view
// frustum. UMG positions a TextBlock at ScreenUV * ImageSize each tick.
// Only aircraft inside the frustum are returned; off-screen ones are
// dropped so the UMG doesn't need to bother with culling. - TripleA
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FInstructorCameraLabel
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName Callsign;
	// Top-left origin, 0..1 across the PIP image. UMG multiplies by the
	// image's render size to get pixel coords. - TripleA
	UPROPERTY(BlueprintReadOnly) FVector2D ScreenUV = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) EThreatClass ThreatClass = EThreatClass::Unknown;
	UPROPERTY(BlueprintReadOnly) int32 FlightLevel = 0;
	// Compass heading in degrees (0..360, 0 = North). - TripleA
	UPROPERTY(BlueprintReadOnly) int32 HeadingDeg = 0;
	// Ground speed in knots. - TripleA
	UPROPERTY(BlueprintReadOnly) int32 SpeedKts = 0;
};

// Projected line segment for the camera-feed HUD overlay. Used for runway
// centerlines, approach corridors, glide slopes, sector boundaries -
// anything that's a world-space line painted as a 2D screen-space stroke
// over the camera feed. - TripleA
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FInstructorCameraLine
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FVector2D StartUV = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FVector2D EndUV   = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly) float Thickness = 2.f;
};

// Projected text label for the camera-feed HUD overlay. Used for runway
// designators (36R / 18L / ...) painted at the threshold ends and
// anything else where a string needs to sit on a world position. - TripleA
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FInstructorCameraText
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString Text;
	UPROPERTY(BlueprintReadOnly) FVector2D ScreenUV = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly) FLinearColor Color = FLinearColor::White;
	UPROPERTY(BlueprintReadOnly) int32 FontSize = 14;
};
