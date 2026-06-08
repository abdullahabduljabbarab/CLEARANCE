// Scenario authoring types - the JSON-roundtrip schema that defines a training
// mission: initial state, scheduled injects, conditional triggers, actions. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceScenarioTypes.generated.h"

// One discriminator per supported verb in the scenario language. Keep the enum
// names short and lowercase-friendly - they appear verbatim in JSON. - TripleA
UENUM(BlueprintType)
enum class EScenarioActionType : uint8
{
	None                UMETA(DisplayName = "None"),
	Spawn               UMETA(DisplayName = "Spawn Aircraft"),
	DeclareEmergency    UMETA(DisplayName = "Declare Emergency"),
	SetWind             UMETA(DisplayName = "Set Wind"),
	ActivateJammer      UMETA(DisplayName = "Activate Jammer"),
	DropChaff           UMETA(DisplayName = "Drop Chaff"),
	InjectMessage       UMETA(DisplayName = "Inject Message"),
	HostileDeclare      UMETA(DisplayName = "Hostile Declare"),
	ScrambleIntercept   UMETA(DisplayName = "Scramble Intercept"),
	SetFlag             UMETA(DisplayName = "Set Flag"),
	LogIncident         UMETA(DisplayName = "Log Incident"),
	FinishScenario      UMETA(DisplayName = "Finish Scenario"),
	Pursue              UMETA(DisplayName = "Pursue Target"),
	BreakOff            UMETA(DisplayName = "Break Off / Flee")
};

UENUM(BlueprintType)
enum class EScenarioConditionType : uint8
{
	None                UMETA(DisplayName = "None"),
	AtTime              UMETA(DisplayName = "At Time"),
	AircraftInArea      UMETA(DisplayName = "Aircraft In Area"),
	AircraftAtAltitude  UMETA(DisplayName = "Aircraft At Altitude"),
	DistanceBetween     UMETA(DisplayName = "Distance Between"),
	PlayerIssued        UMETA(DisplayName = "Player Issued Instruction"),
	ScoreReached        UMETA(DisplayName = "Score Reached"),
	FlagSet             UMETA(DisplayName = "Flag Set"),
	AircraftCount       UMETA(DisplayName = "Aircraft Count")
};

// Discriminated payload. Params are name -> string so JSON parsing stays trivial.
// Each verb pulls the params it cares about by key. - TripleA
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FScenarioAction
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	EScenarioActionType Type = EScenarioActionType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	TMap<FName, FString> Params;
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FScenarioCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	EScenarioConditionType Type = EScenarioConditionType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	TMap<FName, FString> Params;
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FScenarioTimedEvent
{
	GENERATED_BODY()

	// Seconds since scenario start (sim time, not real time).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	float AtSec = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FScenarioAction Action;

	// Set to true once fired so it doesn't fire again.
	UPROPERTY()
	bool bFired = false;
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FScenarioTrigger
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FName Id = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FScenarioCondition When;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FScenarioAction Then;

	// If true, the trigger arms once, fires once, then disarms. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	bool bOnce = true;

	UPROPERTY()
	bool bFired = false;
};

// Initial aircraft state at scenario start. Mirrors FAircraftState's important
// fields without forcing JSON authors to supply everything. - TripleA
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FScenarioSpawn
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FName Callsign = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FName Type = TEXT("MEDIUM");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FVector PositionNm = FVector::ZeroVector; // X east, Y north, Z altitude in ft

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	float HeadingDeg = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	float SpeedKts = 250.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	float AltitudeFt = 15000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	int32 Squawk = 1200;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	EThreatClass Threat = EThreatClass::Friendly;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	bool bIFFOn = true;
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FScenarioEnvironment
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	float WindDirectionDeg = 270.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	float WindSpeedKts = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	float ActiveRunwayHeadingDeg = 270.f;
};

USTRUCT(BlueprintType)
struct CLEARANCESIM_API FScenarioMetadata
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FName Id = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FString Brief;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FString Location;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FString ROE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FString Difficulty;
};

// Top-level container - one JSON file maps to one of these. - TripleA
USTRUCT(BlueprintType)
struct CLEARANCESIM_API FClearanceScenario
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FScenarioMetadata Metadata;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	FScenarioEnvironment Environment;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	TArray<FScenarioSpawn> InitialSpawns;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	TArray<FScenarioTimedEvent> TimedEvents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scenario")
	TArray<FScenarioTrigger> Triggers;
};
