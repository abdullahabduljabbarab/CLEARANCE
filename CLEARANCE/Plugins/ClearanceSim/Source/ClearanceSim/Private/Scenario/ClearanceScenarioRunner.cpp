#include "Scenario/ClearanceScenarioRunner.h"
#include "Simulation/ClearanceSimulationController.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Core/CLEARANCETypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Comms/ClearanceVoiceOutput.h"

// On-screen + log together so something is visible whether the player has the
// Output Log open or just the live HUD. Each scenario message gets a fresh slot
// so it stacks rather than overwriting. - TripleA
static void ScenarioSay(const FString& Msg, const FColor& Col = FColor(180, 230, 255))
{
	UE_LOG(LogTemp, Display, TEXT("[Scenario] %s"), *Msg);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 8.f, Col, FString::Printf(TEXT("SCENARIO  %s"), *Msg));
	}
}

// ---------------------------------------------------------------------------
// JSON helpers - keep parsing tolerant: missing fields fall back to sensible
// defaults so scenario authors don't have to specify every field. - TripleA
// ---------------------------------------------------------------------------

namespace
{
	float GetNum(const TSharedPtr<FJsonObject>& O, const FString& Key, float Default)
	{
		if (!O.IsValid()) { return Default; }
		double V = 0.0;
		if (O->TryGetNumberField(Key, V)) { return static_cast<float>(V); }
		return Default;
	}

	int32 GetInt(const TSharedPtr<FJsonObject>& O, const FString& Key, int32 Default)
	{
		if (!O.IsValid()) { return Default; }
		int32 V = 0;
		if (O->TryGetNumberField(Key, V)) { return V; }
		return Default;
	}

	FString GetStr(const TSharedPtr<FJsonObject>& O, const FString& Key, const FString& Default = FString())
	{
		if (!O.IsValid()) { return Default; }
		FString V;
		if (O->TryGetStringField(Key, V)) { return V; }
		return Default;
	}

	bool GetBool(const TSharedPtr<FJsonObject>& O, const FString& Key, bool Default)
	{
		if (!O.IsValid()) { return Default; }
		bool V = false;
		if (O->TryGetBoolField(Key, V)) { return V; }
		return Default;
	}

	EScenarioActionType ParseActionType(const FString& S)
	{
		if (S.Equals(TEXT("spawn"),               ESearchCase::IgnoreCase)) { return EScenarioActionType::Spawn; }
		if (S.Equals(TEXT("declareEmergency"),    ESearchCase::IgnoreCase)) { return EScenarioActionType::DeclareEmergency; }
		if (S.Equals(TEXT("setWind"),             ESearchCase::IgnoreCase)) { return EScenarioActionType::SetWind; }
		if (S.Equals(TEXT("activateJammer"),      ESearchCase::IgnoreCase)) { return EScenarioActionType::ActivateJammer; }
		if (S.Equals(TEXT("dropChaff"),           ESearchCase::IgnoreCase)) { return EScenarioActionType::DropChaff; }
		if (S.Equals(TEXT("injectMessage"),       ESearchCase::IgnoreCase)) { return EScenarioActionType::InjectMessage; }
		if (S.Equals(TEXT("hostileDeclare"),      ESearchCase::IgnoreCase)) { return EScenarioActionType::HostileDeclare; }
		if (S.Equals(TEXT("scrambleIntercept"),   ESearchCase::IgnoreCase)) { return EScenarioActionType::ScrambleIntercept; }
		if (S.Equals(TEXT("setFlag"),             ESearchCase::IgnoreCase)) { return EScenarioActionType::SetFlag; }
		if (S.Equals(TEXT("logIncident"),         ESearchCase::IgnoreCase)) { return EScenarioActionType::LogIncident; }
		if (S.Equals(TEXT("finishScenario"),      ESearchCase::IgnoreCase)) { return EScenarioActionType::FinishScenario; }
		if (S.Equals(TEXT("pursue"),              ESearchCase::IgnoreCase)) { return EScenarioActionType::Pursue; }
		if (S.Equals(TEXT("breakOff"),            ESearchCase::IgnoreCase)) { return EScenarioActionType::BreakOff; }
		return EScenarioActionType::None;
	}

	EScenarioConditionType ParseConditionType(const FString& S)
	{
		if (S.Equals(TEXT("atTime"),              ESearchCase::IgnoreCase)) { return EScenarioConditionType::AtTime; }
		if (S.Equals(TEXT("aircraftInArea"),      ESearchCase::IgnoreCase)) { return EScenarioConditionType::AircraftInArea; }
		if (S.Equals(TEXT("aircraftAtAltitude"),  ESearchCase::IgnoreCase)) { return EScenarioConditionType::AircraftAtAltitude; }
		if (S.Equals(TEXT("distanceBetween"),     ESearchCase::IgnoreCase)) { return EScenarioConditionType::DistanceBetween; }
		if (S.Equals(TEXT("playerIssued"),        ESearchCase::IgnoreCase)) { return EScenarioConditionType::PlayerIssued; }
		if (S.Equals(TEXT("scoreReached"),        ESearchCase::IgnoreCase)) { return EScenarioConditionType::ScoreReached; }
		if (S.Equals(TEXT("flagSet"),             ESearchCase::IgnoreCase)) { return EScenarioConditionType::FlagSet; }
		if (S.Equals(TEXT("aircraftCount"),       ESearchCase::IgnoreCase)) { return EScenarioConditionType::AircraftCount; }
		return EScenarioConditionType::None;
	}

	EThreatClass ParseThreat(const FString& S)
	{
		if (S.Equals(TEXT("Hostile"),  ESearchCase::IgnoreCase)) { return EThreatClass::Hostile; }
		if (S.Equals(TEXT("Unknown"),  ESearchCase::IgnoreCase)) { return EThreatClass::Unknown; }
		if (S.Equals(TEXT("Neutral"),  ESearchCase::IgnoreCase)) { return EThreatClass::Neutral; }
		return EThreatClass::Friendly;
	}

	EEmergencyType ParseEmergency(const FString& S)
	{
		if (S.Equals(TEXT("Hijack"),       ESearchCase::IgnoreCase) || S == TEXT("7500")) { return EEmergencyType::Hijack; }
		if (S.Equals(TEXT("CommsFailure"), ESearchCase::IgnoreCase) || S == TEXT("7600")) { return EEmergencyType::CommsFailure; }
		if (S.Equals(TEXT("FuelLow"),      ESearchCase::IgnoreCase) || S.Equals(TEXT("Fuel"), ESearchCase::IgnoreCase)) { return EEmergencyType::FuelLow; }
		if (S.Equals(TEXT("Mayday"),       ESearchCase::IgnoreCase) || S == TEXT("7700")) { return EEmergencyType::GeneralMayday; }
		return EEmergencyType::None;
	}

	void ReadParams(const TSharedPtr<FJsonObject>& O, TMap<FName, FString>& OutParams)
	{
		if (!O.IsValid()) { return; }
		const TSharedPtr<FJsonObject>* P = nullptr;
		if (!O->TryGetObjectField(TEXT("params"), P) || !P || !P->IsValid()) { return; }
		for (const auto& KV : (*P)->Values)
		{
			if (!KV.Value.IsValid()) { continue; }
			FString V;
			if (KV.Value->Type == EJson::String)  { V = KV.Value->AsString(); }
			else if (KV.Value->Type == EJson::Number) { V = FString::SanitizeFloat(KV.Value->AsNumber()); }
			else if (KV.Value->Type == EJson::Boolean) { V = KV.Value->AsBool() ? TEXT("true") : TEXT("false"); }
			else { V = KV.Value->AsString(); }
			OutParams.Add(FName(*KV.Key), V);
		}
	}
}

// ---------------------------------------------------------------------------
// Setup + load
// ---------------------------------------------------------------------------

void UClearanceScenarioRunner::SetReferences(AClearanceSimulationController* InController, AClearanceAirspaceManager* InManager)
{
	Controller = InController;
	Manager    = InManager;
}

bool UClearanceScenarioRunner::LoadFromFile(const FString& AbsolutePath, FString& OutError)
{
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *AbsolutePath))
	{
		OutError = FString::Printf(TEXT("failed to read %s"), *AbsolutePath);
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("invalid JSON in %s"), *AbsolutePath);
		return false;
	}

	Scenario = FClearanceScenario();
	Flags.Reset();

	// metadata
	const TSharedPtr<FJsonObject>* MetaObj = nullptr;
	if (Root->TryGetObjectField(TEXT("metadata"), MetaObj) && MetaObj && MetaObj->IsValid())
	{
		Scenario.Metadata.Id         = FName(*GetStr(*MetaObj, TEXT("id"), TEXT("scenario")));
		Scenario.Metadata.Name       = GetStr(*MetaObj, TEXT("name"));
		Scenario.Metadata.Brief      = GetStr(*MetaObj, TEXT("brief"));
		Scenario.Metadata.Location   = GetStr(*MetaObj, TEXT("location"));
		Scenario.Metadata.ROE        = GetStr(*MetaObj, TEXT("roe"));
		Scenario.Metadata.Difficulty = GetStr(*MetaObj, TEXT("difficulty"));
		Scenario.Metadata.bKeepLevelZones = GetBool(*MetaObj, TEXT("keepLevelZones"), false);
	}

	// environment
	const TSharedPtr<FJsonObject>* EnvObj = nullptr;
	if (Root->TryGetObjectField(TEXT("environment"), EnvObj) && EnvObj && EnvObj->IsValid())
	{
		Scenario.Environment.WindDirectionDeg       = GetNum(*EnvObj, TEXT("windDirectionDeg"), 270.f);
		Scenario.Environment.WindSpeedKts           = GetNum(*EnvObj, TEXT("windSpeedKts"), 10.f);
		Scenario.Environment.ActiveRunwayHeadingDeg = GetNum(*EnvObj, TEXT("activeRunwayHeadingDeg"), 270.f);
	}

	// initial spawns
	const TArray<TSharedPtr<FJsonValue>>* SpawnArr = nullptr;
	if (Root->TryGetArrayField(TEXT("initialSpawns"), SpawnArr) && SpawnArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *SpawnArr)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) { continue; }

			FScenarioSpawn Sp;
			Sp.Callsign   = FName(*GetStr(O, TEXT("callsign"), TEXT("XXXX")));
			Sp.Type       = FName(*GetStr(O, TEXT("type"), TEXT("MEDIUM")));
			Sp.HeadingDeg = GetNum(O, TEXT("headingDeg"), 0.f);
			Sp.SpeedKts   = GetNum(O, TEXT("speedKts"), 250.f);
			Sp.AltitudeFt = GetNum(O, TEXT("altitudeFt"), 15000.f);
			Sp.Squawk     = GetInt(O, TEXT("squawk"), 1200);
			Sp.Threat     = ParseThreat(GetStr(O, TEXT("threat"), TEXT("Friendly")));
			Sp.bIFFOn     = GetBool(O, TEXT("iff"), true);

			const TSharedPtr<FJsonObject>* PosObj = nullptr;
			if (O->TryGetObjectField(TEXT("positionNm"), PosObj) && PosObj && PosObj->IsValid())
			{
				Sp.PositionNm = FVector(
					GetNum(*PosObj, TEXT("x"), 0.f),
					GetNum(*PosObj, TEXT("y"), 0.f),
					Sp.AltitudeFt);
			}
			Scenario.InitialSpawns.Add(Sp);
		}
	}

	// timed events
	const TArray<TSharedPtr<FJsonValue>>* EvArr = nullptr;
	if (Root->TryGetArrayField(TEXT("timedEvents"), EvArr) && EvArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *EvArr)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) { continue; }

			FScenarioTimedEvent Ev;
			Ev.AtSec = GetNum(O, TEXT("atSec"), 0.f);

			const TSharedPtr<FJsonObject>* ActObj = nullptr;
			if (O->TryGetObjectField(TEXT("action"), ActObj) && ActObj && ActObj->IsValid())
			{
				Ev.Action.Type = ParseActionType(GetStr(*ActObj, TEXT("type")));
				ReadParams(*ActObj, Ev.Action.Params);
			}
			Scenario.TimedEvents.Add(Ev);
		}
	}

	// triggers
	const TArray<TSharedPtr<FJsonValue>>* TrigArr = nullptr;
	if (Root->TryGetArrayField(TEXT("triggers"), TrigArr) && TrigArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *TrigArr)
		{
			const TSharedPtr<FJsonObject> O = V.IsValid() ? V->AsObject() : nullptr;
			if (!O.IsValid()) { continue; }

			FScenarioTrigger Tr;
			Tr.Id    = FName(*GetStr(O, TEXT("id"), TEXT("trigger")));
			Tr.bOnce = GetBool(O, TEXT("once"), true);

			const TSharedPtr<FJsonObject>* WhenObj = nullptr;
			if (O->TryGetObjectField(TEXT("when"), WhenObj) && WhenObj && WhenObj->IsValid())
			{
				Tr.When.Type = ParseConditionType(GetStr(*WhenObj, TEXT("type")));
				ReadParams(*WhenObj, Tr.When.Params);
			}

			const TSharedPtr<FJsonObject>* ThenObj = nullptr;
			if (O->TryGetObjectField(TEXT("then"), ThenObj) && ThenObj && ThenObj->IsValid())
			{
				Tr.Then.Type = ParseActionType(GetStr(*ThenObj, TEXT("type")));
				ReadParams(*ThenObj, Tr.Then.Params);
			}
			Scenario.Triggers.Add(Tr);
		}
	}

	return true;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void UClearanceScenarioRunner::Start()
{
	if (Scenario.Metadata.Id == NAME_None || !Controller || !Manager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Scenario] Start refused - no scenario loaded or refs missing"));
		return;
	}
	bRunning      = true;
	ElapsedSec    = 0.f;
	FiredEvents   = 0;
	FiredTriggers = 0;
	Flags.Reset();
	for (FScenarioTimedEvent& E : Scenario.TimedEvents) { E.bFired = false; }
	for (FScenarioTrigger& T : Scenario.Triggers)       { T.bFired = false; }

	// Lock the spawner via the independent scenario-lock flag so we don't trample
	// the user's bAutoSpawn free-play preference. Suspend level-placed restricted +
	// violation zone checks so scenario aircraft aren't penalised for hitting
	// geometry the JSON didn't author. - TripleA
	if (Controller)
	{
		Controller->SetSpawnerScenarioLocked(true);
		Controller->bZoneChecksSuspended = !Scenario.Metadata.bKeepLevelZones;
	}

	ApplyEnvironment();
	FireInitialSpawns();

	ScenarioSay(FString::Printf(TEXT("STARTED: %s"), *Scenario.Metadata.Name), FColor(120, 255, 160));
}

void UClearanceScenarioRunner::Stop()
{
	if (bRunning)
	{
		ScenarioSay(FString::Printf(TEXT("STOPPED: %s after %.0fs (%d/%d events, %d/%d triggers)"),
			*Scenario.Metadata.Name, ElapsedSec,
			FiredEvents, Scenario.TimedEvents.Num(),
			FiredTriggers, Scenario.Triggers.Num()), FColor(255, 180, 120));
	}
	bRunning = false;
	ActivePursuits.Reset();
	// Release the scenario lock + zone checks so free-play resumes after the
	// scenario ends. The user's bAutoSpawn preference was never touched. - TripleA
	if (Controller)
	{
		Controller->SetSpawnerScenarioLocked(false);
		Controller->bZoneChecksSuspended = false;
	}
}

void UClearanceScenarioRunner::Tick(float SimDeltaSeconds)
{
	if (!bRunning || SimDeltaSeconds <= 0.f) { return; }
	ElapsedSec += SimDeltaSeconds;
	EvaluateTimedEvents();
	EvaluateTriggers();
	UpdatePursuits();
}

void UClearanceScenarioRunner::UpdatePursuits()
{
	if (!Manager || ActivePursuits.Num() == 0) { return; }

	TArray<FName> ToRemove;
	for (const TPair<FName, FName>& P : ActivePursuits)
	{
		const FAircraftState Hunter = Manager->GetAircraftState(P.Key);
		const FAircraftState Target = Manager->GetAircraftState(P.Value);
		if (!Hunter.bIsValid || !Target.bIsValid)
		{
			ToRemove.Add(P.Key);
			continue;
		}

		// Disengage automatically when the hunter has been intercepted and the
		// Controller flips it into Exiting phase. Stops the scenario from fighting
		// the join-up outbound vector every tick. - TripleA
		if (Hunter.FlightPhase == EFlightPhase::Exiting)
		{
			ToRemove.Add(P.Key);
			ScenarioSay(FString::Printf(TEXT("%s intercepted - pursuit ended"),
				*P.Key.ToString()), FColor(180, 255, 180));
			continue;
		}

		// Vector from hunter to target in plan view (XY plane). Steer the hunter onto
		// that bearing and match the target's altitude band - the Behaviour layer does
		// the bank-limited turn naturally. - TripleA
		const FVector2D Delta(Target.Position.X - Hunter.Position.X,
		                      Target.Position.Y - Hunter.Position.Y);
		float BearingDeg = FMath::RadiansToDegrees(FMath::Atan2(Delta.X, Delta.Y));
		if (BearingDeg < 0.f) { BearingDeg += 360.f; }

		FAircraftState H = Hunter;
		H.TargetHeading  = BearingDeg;
		H.TargetAltitude = Target.Altitude;
		H.TargetSpeed    = FMath::Max(H.TargetSpeed, Target.Speed + 20.f); // subtle overtake - interceptors should win the race
		Manager->RequestStateUpdate(H);
	}
	for (const FName& K : ToRemove) { ActivePursuits.Remove(K); }
}

// ---------------------------------------------------------------------------
// Setup steps
// ---------------------------------------------------------------------------

void UClearanceScenarioRunner::ApplyEnvironment()
{
	if (Controller)
	{
		Controller->SetWind(Scenario.Environment.WindDirectionDeg, Scenario.Environment.WindSpeedKts);
	}
}

void UClearanceScenarioRunner::FireInitialSpawns()
{
	if (!Manager) { return; }
	for (const FScenarioSpawn& Sp : Scenario.InitialSpawns)
	{
		FAircraftState A;
		A.Callsign        = Sp.Callsign;
		A.Position        = FVector(Sp.PositionNm.X, Sp.PositionNm.Y, Sp.AltitudeFt);
		A.Heading         = Sp.HeadingDeg;
		A.Speed           = Sp.SpeedKts;
		A.TargetHeading   = Sp.HeadingDeg;
		A.TargetSpeed     = Sp.SpeedKts;
		A.TargetAltitude  = Sp.AltitudeFt;
		A.Altitude        = Sp.AltitudeFt;
		A.SquawkCode      = Sp.Squawk;
		A.ThreatClass     = Sp.Threat;
		A.bIFFOperational = Sp.bIFFOn;
		A.FlightPhase     = EFlightPhase::Enroute;
		A.bIsValid        = true;
		Manager->RegisterAircraft(A);

		ScenarioSay(FString::Printf(TEXT("spawned %s at (%.0f, %.0f) hdg %.0f alt %.0f"),
			*Sp.Callsign.ToString(), Sp.PositionNm.X, Sp.PositionNm.Y, Sp.HeadingDeg, Sp.AltitudeFt),
			FColor(180, 255, 180));
	}
}

// ---------------------------------------------------------------------------
// Per-tick evaluation
// ---------------------------------------------------------------------------

void UClearanceScenarioRunner::EvaluateTimedEvents()
{
	for (FScenarioTimedEvent& E : Scenario.TimedEvents)
	{
		if (E.bFired) { continue; }
		if (ElapsedSec >= E.AtSec)
		{
			ScenarioSay(FString::Printf(TEXT("T+%.0fs - event fires (action %d)"),
				E.AtSec, (int32)E.Action.Type), FColor(180, 220, 255));
			ExecuteAction(E.Action);
			E.bFired = true;
			++FiredEvents;
		}
	}
}

void UClearanceScenarioRunner::EvaluateTriggers()
{
	for (FScenarioTrigger& T : Scenario.Triggers)
	{
		if (T.bFired && T.bOnce) { continue; }
		if (EvaluateCondition(T.When))
		{
			ScenarioSay(FString::Printf(TEXT("trigger '%s' fires (action %d)"),
				*T.Id.ToString(), (int32)T.Then.Type), FColor(220, 180, 255));
			ExecuteAction(T.Then);
			T.bFired = true;
			++FiredTriggers;
		}
	}
}

// ---------------------------------------------------------------------------
// Condition predicates
// ---------------------------------------------------------------------------

bool UClearanceScenarioRunner::EvaluateCondition(const FScenarioCondition& Cond) const
{
	switch (Cond.Type)
	{
	case EScenarioConditionType::AtTime:
	{
		const FString* S = Cond.Params.Find(TEXT("seconds"));
		const float Target = S ? FCString::Atof(**S) : 0.f;
		return ElapsedSec >= Target;
	}
	case EScenarioConditionType::AircraftInArea:
	{
		if (!Manager) { return false; }
		const FString* Cs   = Cond.Params.Find(TEXT("callsign"));
		const FString* Cx   = Cond.Params.Find(TEXT("centreX"));
		const FString* Cy   = Cond.Params.Find(TEXT("centreY"));
		const FString* Rad  = Cond.Params.Find(TEXT("radiusNm"));
		if (!Cs || !Cx || !Cy || !Rad) { return false; }
		const FAircraftState S = Manager->GetAircraftState(FName(**Cs));
		if (!S.bIsValid) { return false; }
		const float DX = S.Position.X - FCString::Atof(**Cx);
		const float DY = S.Position.Y - FCString::Atof(**Cy);
		const float R  = FCString::Atof(**Rad);
		return (DX*DX + DY*DY) <= (R*R);
	}
	case EScenarioConditionType::AircraftAtAltitude:
	{
		if (!Manager) { return false; }
		const FString* Cs   = Cond.Params.Find(TEXT("callsign"));
		const FString* Op   = Cond.Params.Find(TEXT("op"));     // "lt" | "gt" | "eq"
		const FString* AltS = Cond.Params.Find(TEXT("altitudeFt"));
		if (!Cs || !AltS) { return false; }
		const FAircraftState S = Manager->GetAircraftState(FName(**Cs));
		if (!S.bIsValid) { return false; }
		const float Alt = FCString::Atof(**AltS);
		const FString OpS = Op ? **Op : FString(TEXT("lt"));
		if (OpS == TEXT("lt")) { return S.Altitude < Alt; }
		if (OpS == TEXT("gt")) { return S.Altitude > Alt; }
		return FMath::IsNearlyEqual(S.Altitude, Alt, 50.f);
	}
	case EScenarioConditionType::DistanceBetween:
	{
		if (!Manager) { return false; }
		const FString* A = Cond.Params.Find(TEXT("a"));
		const FString* B = Cond.Params.Find(TEXT("b"));
		const FString* L = Cond.Params.Find(TEXT("lessThanNm"));
		if (!A || !B || !L) { return false; }
		const FAircraftState SA = Manager->GetAircraftState(FName(**A));
		const FAircraftState SB = Manager->GetAircraftState(FName(**B));
		if (!SA.bIsValid || !SB.bIsValid) { return false; }
		const float DX = SA.Position.X - SB.Position.X;
		const float DY = SA.Position.Y - SB.Position.Y;
		const float Lim = FCString::Atof(**L);
		return (DX*DX + DY*DY) <= (Lim*Lim);
	}
	case EScenarioConditionType::FlagSet:
	{
		const FString* F = Cond.Params.Find(TEXT("flag"));
		return F ? Flags.Contains(FName(**F)) : false;
	}
	case EScenarioConditionType::AircraftCount:
	{
		if (!Manager) { return false; }
		const FString* Op = Cond.Params.Find(TEXT("op"));
		const FString* N  = Cond.Params.Find(TEXT("count"));
		if (!N) { return false; }
		const int32 Want = FCString::Atoi(**N);
		const int32 Have = Manager->GetAircraftCount();
		const FString OpS = Op ? **Op : FString(TEXT("gte"));
		if (OpS == TEXT("gte")) { return Have >= Want; }
		if (OpS == TEXT("lte")) { return Have <= Want; }
		if (OpS == TEXT("eq"))  { return Have == Want; }
		return false;
	}
	default:
		return false;
	}
}

// ---------------------------------------------------------------------------
// Action verbs
// ---------------------------------------------------------------------------

void UClearanceScenarioRunner::ExecuteAction(const FScenarioAction& Act)
{
	if (!Controller || !Manager) { return; }

	switch (Act.Type)
	{
	case EScenarioActionType::Spawn:
	{
		FScenarioSpawn Sp;
		const FString* Cs = Act.Params.Find(TEXT("callsign"));
		Sp.Callsign   = Cs ? FName(**Cs) : FName(TEXT("XXXX"));
		const FString* Type = Act.Params.Find(TEXT("type"));
		Sp.Type       = Type ? FName(**Type) : FName(TEXT("MEDIUM"));
		const FString* Px = Act.Params.Find(TEXT("x"));
		const FString* Py = Act.Params.Find(TEXT("y"));
		Sp.PositionNm = FVector(
			Px ? FCString::Atof(**Px) : 0.f,
			Py ? FCString::Atof(**Py) : 0.f,
			0.f);
		const FString* Hdg = Act.Params.Find(TEXT("headingDeg"));
		Sp.HeadingDeg = Hdg ? FCString::Atof(**Hdg) : 0.f;
		const FString* Spd = Act.Params.Find(TEXT("speedKts"));
		Sp.SpeedKts   = Spd ? FCString::Atof(**Spd) : 250.f;
		const FString* Alt = Act.Params.Find(TEXT("altitudeFt"));
		Sp.AltitudeFt = Alt ? FCString::Atof(**Alt) : 15000.f;
		const FString* Sq  = Act.Params.Find(TEXT("squawk"));
		Sp.Squawk     = Sq ? FCString::Atoi(**Sq) : 1200;
		const FString* Th  = Act.Params.Find(TEXT("threat"));
		Sp.Threat     = Th ? ParseThreat(*Th) : EThreatClass::Friendly;
		const FString* If_ = Act.Params.Find(TEXT("iff"));
		Sp.bIFFOn     = If_ ? (If_->Equals(TEXT("true"), ESearchCase::IgnoreCase) || *If_ == TEXT("1")) : true;

		FAircraftState A;
		A.Callsign        = Sp.Callsign;
		A.Position        = FVector(Sp.PositionNm.X, Sp.PositionNm.Y, Sp.AltitudeFt);
		A.Heading         = Sp.HeadingDeg;
		A.Speed           = Sp.SpeedKts;
		A.TargetHeading   = Sp.HeadingDeg;
		A.TargetSpeed     = Sp.SpeedKts;
		A.TargetAltitude  = Sp.AltitudeFt;
		A.Altitude        = Sp.AltitudeFt;
		A.SquawkCode      = Sp.Squawk;
		A.ThreatClass     = Sp.Threat;
		A.bIFFOperational = Sp.bIFFOn;
		A.FlightPhase     = EFlightPhase::Enroute;
		A.bIsValid        = true;
		Manager->RegisterAircraft(A);
		ScenarioSay(FString::Printf(TEXT("spawned %s at (%.0f, %.0f) hdg %.0f alt %.0f"),
			*Sp.Callsign.ToString(), Sp.PositionNm.X, Sp.PositionNm.Y, Sp.HeadingDeg, Sp.AltitudeFt),
			FColor(180, 255, 180));
		break;
	}
	case EScenarioActionType::DeclareEmergency:
	{
		const FString* Cs = Act.Params.Find(TEXT("callsign"));
		const FString* Em = Act.Params.Find(TEXT("kind"));
		if (!Cs || !Em) { break; }
		Controller->DeclareEmergencyOn(FName(**Cs), ParseEmergency(*Em));
		break;
	}
	case EScenarioActionType::SetWind:
	{
		const FString* D = Act.Params.Find(TEXT("directionDeg"));
		const FString* S = Act.Params.Find(TEXT("speedKts"));
		const float Dir = D ? FCString::Atof(**D) : 270.f;
		const float Spd = S ? FCString::Atof(**S) : 10.f;
		Controller->SetWind(Dir, Spd);
		break;
	}
	case EScenarioActionType::HostileDeclare:
	{
		const FString* Cs = Act.Params.Find(TEXT("callsign"));
		if (Cs) { Controller->ClassifyAircraft(FName(**Cs), EThreatClass::Hostile); }
		break;
	}
	case EScenarioActionType::ScrambleIntercept:
	{
		const FString* Cs = Act.Params.Find(TEXT("bandit"));
		if (Cs) { Controller->ScrambleInterceptors(FName(**Cs)); }
		break;
	}
	case EScenarioActionType::SetFlag:
	{
		const FString* F = Act.Params.Find(TEXT("flag"));
		if (F) { Flags.Add(FName(**F)); }
		break;
	}
	case EScenarioActionType::InjectMessage:
	{
		const FString* M = Act.Params.Find(TEXT("text"));
		if (!M) { break; }
		ScenarioSay(*M);

		// Route through the Controller's NetMulticast so every connected peer
		// hears AWACS / scripted broadcasts, not just whichever machine the
		// server's iterator landed on. Default callsign "AWACS" + a controller-
		// style voice; the scenario author can override via params. - TripleA
		const FString* CsParam    = Act.Params.Find(TEXT("callsign"));
		const FString* VoiceParam = Act.Params.Find(TEXT("voice"));
		const FName    SpeakerCs  = CsParam ? FName(**CsParam) : FName(TEXT("AWACS"));
		const FString  VoiceTag   = VoiceParam ? *VoiceParam : FString(TEXT("en-US-EricNeural"));
		if (Controller)
		{
			Controller->Multicast_PlayTTS(SpeakerCs, *M, VoiceTag, /*bPanic*/ false);
		}
		break;
	}
	case EScenarioActionType::LogIncident:
	{
		const FString* M = Act.Params.Find(TEXT("note"));
		ScenarioSay(FString::Printf(TEXT("INCIDENT - %s"), M ? **M : TEXT("(no note)")), FColor(255, 80, 80));
		break;
	}
	case EScenarioActionType::FinishScenario:
	{
		Stop();
		break;
	}
	case EScenarioActionType::Pursue:
	{
		const FString* Hunter = Act.Params.Find(TEXT("hunter"));
		const FString* Target = Act.Params.Find(TEXT("target"));
		if (Hunter && Target)
		{
			ActivePursuits.Add(FName(**Hunter), FName(**Target));
			ScenarioSay(FString::Printf(TEXT("pursuit armed: %s -> %s"),
				**Hunter, **Target), FColor(255, 140, 80));
		}
		break;
	}
	case EScenarioActionType::BreakOff:
	{
		// Bandit "intercepted, breaking off" - drop any active pursuit, point the
		// nose at the closest sector exit, climb hard, run for max speed. Reads as
		// a classic intercept outcome: contact disengages without a kill. - TripleA
		const FString* Cs = Act.Params.Find(TEXT("callsign"));
		if (!Cs) { break; }
		const FName CsName(**Cs);
		ActivePursuits.Remove(CsName);
		FAircraftState S = Manager->GetAircraftState(CsName);
		if (!S.bIsValid) { break; }

		// Bearing from origin to aircraft -> push outbound on that bearing so they
		// leave the sector by the shortest path. - TripleA
		const FVector2D Here(S.Position.X, S.Position.Y);
		float OutBearing = 0.f;
		if (!Here.IsNearlyZero())
		{
			OutBearing = FMath::RadiansToDegrees(FMath::Atan2(Here.X, Here.Y));
			if (OutBearing < 0.f) { OutBearing += 360.f; }
		}
		S.TargetHeading  = OutBearing;
		S.TargetAltitude = FMath::Max(S.Altitude, 35000.f);
		S.TargetSpeed    = FMath::Max(S.TargetSpeed, S.MaxOperatingSpeed > 0.f ? S.MaxOperatingSpeed : 500.f);
		Manager->RequestStateUpdate(S);
		ScenarioSay(FString::Printf(TEXT("%s breaking off - egressing on %03.0f"),
			*CsName.ToString(), OutBearing), FColor(255, 200, 100));
		break;
	}
	case EScenarioActionType::ActivateJammer:
	case EScenarioActionType::DropChaff:
		// EW actions are placeholders until the EW system lands. Author them now so
		// scenarios can be written; wire them up when jamming exists. - TripleA
		ScenarioSay(FString::Printf(TEXT("EW action %d (pending EW system)"), (int32)Act.Type),
			FColor(220, 200, 80));
		break;
	default:
		break;
	}
}
