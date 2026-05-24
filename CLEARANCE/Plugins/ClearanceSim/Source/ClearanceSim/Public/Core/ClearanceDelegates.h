// CLEARANCE - Event-driven communication between systems.
// Dynamic multicast delegates so the presentation layer (Blueprint UI) can
// bind to them via BlueprintAssignable. Systems broadcast; UI and other
// systems listen. See "Technical Implementation Scaffold" delegate map.
#pragma once

#include "CoreMinimal.h"
#include "Core/CLEARANCETypes.h"

// --- Airspace Manager ---
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAircraftRegistered, FName, Callsign);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAircraftDeregistered, FName, Callsign);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAircraftStateUpdated, FName, Callsign);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRunwayChanged, float, NewRunwayHeading);

// --- Conflict Detector ---
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConflictDetected, FConflictEvent, Conflict);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnConflictResolved, FConflictEvent, Conflict);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGoAroundRequired, FName, Callsign);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnWakeTurbulenceAdvisory, FName, FollowingCallsign, FName, LeadingCallsign, float, RequiredSeparationNm);

// --- Comms Router ---
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInstructionResult, FName, Callsign, EInstructionResult, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAdvisoryWarning, FString, Message, EAlertLevel, Level);

// --- Scoring ---
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnScoreUpdated, int32, NewScore);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDifficultyAdjusted, float, NewSpawnRate);
