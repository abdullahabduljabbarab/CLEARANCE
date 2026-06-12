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
	UPROPERTY(BlueprintReadOnly) int32 Departures = 0;
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
