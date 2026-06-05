#include "Comms/ClearancePhraseology.h"
#include "Simulation/ClearanceSimulationController.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Comms/ClearanceCommsRouter.h"
#include "EngineUtils.h"

namespace
{
	bool IsAllDigits(const FString& S)
	{
		if (S.IsEmpty()) return false;
		for (TCHAR C : S) { if (!FChar::IsDigit(C)) return false; }
		return true;
	}

	// Spoken digits, including the standard ATC variants (niner, tree, fife...).
	bool DigitWord(const FString& W, int32& Out)
	{
		static const TMap<FString, int32> Map = {
			{TEXT("zero"),0},{TEXT("oh"),0},{TEXT("one"),1},{TEXT("two"),2},
			{TEXT("three"),3},{TEXT("tree"),3},{TEXT("four"),4},{TEXT("fower"),4},
			{TEXT("five"),5},{TEXT("fife"),5},{TEXT("six"),6},{TEXT("seven"),7},
			{TEXT("eight"),8},{TEXT("nine"),9},{TEXT("niner"),9}
		};
		if (const int32* V = Map.Find(W)) { Out = *V; return true; }
		return false;
	}

	// Reads a run of number words/numerals (digit-by-digit, ATC style) starting at
	// Idx, with an optional hundred/thousand multiplier. "two five zero" -> 250,
	// "five thousand" -> 5000, "350" -> 350.
	bool ParseNumberRun(const TArray<FString>& Tokens, int32& Idx, int32& OutValue)
	{
		while (Idx < Tokens.Num() && (Tokens[Idx] == TEXT("to") || Tokens[Idx] == TEXT("and")))
		{
			++Idx;
		}

		FString Digits;
		int32 Multiplier = 1;
		int32 D;
		while (Idx < Tokens.Num())
		{
			const FString& T = Tokens[Idx];
			if (IsAllDigits(T)) { Digits += T; ++Idx; }
			else if (DigitWord(T, D)) { Digits.AppendChar(TCHAR('0' + D)); ++Idx; }
			else if (T == TEXT("thousand")) { Multiplier = 1000; ++Idx; break; }
			else if (T == TEXT("hundred")) { Multiplier = 100; ++Idx; break; }
			else break;
		}

		if (Digits.IsEmpty()) return false;
		OutValue = FCString::Atoi(*Digits) * Multiplier;
		return true;
	}

	FName ResolveCallsign(AClearanceSimulationController* Controller, const TArray<FString>& Tokens, int32& Idx)
	{
		static const TMap<FString, FString> Telephony = {
			{TEXT("speedbird"),TEXT("BAW")},{TEXT("lufthansa"),TEXT("DLH")},
			{TEXT("united"),TEXT("UAL")},{TEXT("american"),TEXT("AAL")},
			{TEXT("emirates"),TEXT("UAE")},{TEXT("airfrance"),TEXT("AFR")},
			// Unidentified contacts: spoken "unknown 001", "bogey 001", or "bandit 001"
			// all resolve to the UNK### track. - TripleA
			{TEXT("unknown"),TEXT("UNK")},{TEXT("bogey"),TEXT("UNK")},{TEXT("bandit"),TEXT("UNK")},
			// Friendly fighter flight - spoken "viper 01" resolves to VIPER01. - TripleA
			{TEXT("viper"),TEXT("VIPER")}
		};

		AClearanceAirspaceManager* AM = Controller->GetAirspaceManager();
		if (!AM)
		{
			return NAME_None;
		}

		FString Prefix;
		FString NumStr;

		if (Idx < Tokens.Num())
		{
			const FString W = Tokens[Idx];
			if (const FString* P = Telephony.Find(W)) { Prefix = *P; ++Idx; }
			else if (W == TEXT("air") && Idx + 1 < Tokens.Num() && Tokens[Idx + 1] == TEXT("france")) { Prefix = TEXT("AFR"); Idx += 2; }
			else
			{
				// A joined token like "baw101" (ICAO read straight off the tag):
				// split it into letters (airline) and digits (flight number).
				FString Letters, Digits;
				for (TCHAR C : W)
				{
					if (FChar::IsAlpha(C)) { Letters.AppendChar(C); }
					else if (FChar::IsDigit(C)) { Digits.AppendChar(C); }
				}
				if (Letters.Len() >= 2)
				{
					Prefix = Letters.ToUpper();
					NumStr = Digits;
					++Idx;
				}
			}
		}

		// If the number wasn't already part of the callsign token, read it next.
		if (NumStr.IsEmpty())
		{
			int32 Number = 0;
			if (ParseNumberRun(Tokens, Idx, Number)) { NumStr = FString::FromInt(Number); }
		}

		if (Prefix.IsEmpty() && NumStr.IsEmpty())
		{
			return NAME_None;
		}

		const FString Candidate = Prefix + NumStr;

		// Exact match.
		for (const FAircraftState& S : AM->GetAllAircraftStates())
		{
			if (S.Callsign.ToString().Equals(Candidate, ESearchCase::IgnoreCase)) { return S.Callsign; }
		}
		// Prefix match + number contained (handles a mangled airline or number).
		for (const FAircraftState& S : AM->GetAllAircraftStates())
		{
			const FString CS = S.Callsign.ToString();
			const bool bPrefixOk = Prefix.IsEmpty() || CS.StartsWith(Prefix, ESearchCase::IgnoreCase);
			const bool bNumOk = NumStr.IsEmpty() || CS.Contains(NumStr);
			if (bPrefixOk && bNumOk) { return S.Callsign; }
		}
		// Number-only last resort.
		if (!NumStr.IsEmpty())
		{
			for (const FAircraftState& S : AM->GetAllAircraftStates())
			{
				if (S.Callsign.ToString().Contains(NumStr)) { return S.Callsign; }
			}
		}
		return NAME_None;
	}
}

FString UClearancePhraseology::Interpret(AClearanceSimulationController* Controller, const FString& Transmission)
{
	if (!Controller)
	{
		return TEXT("(no simulation controller)");
	}

	FString Lower = Transmission.ToLower();
	Lower.ReplaceInline(TEXT(","), TEXT(" "));
	Lower.ReplaceInline(TEXT("."), TEXT(" "));

	TArray<FString> Tokens;
	Lower.ParseIntoArray(Tokens, TEXT(" "), true);
	if (Tokens.Num() == 0)
	{
		return TEXT("(empty transmission)");
	}

	// NATO GCI brevity (military intercept doctrine, distinct from ICAO civil
	// phraseology). These commands lead with the verb, not the callsign, and
	// run through the GCI methods on the controller rather than the civilian
	// instruction pipeline. - TripleA
	//   INTERROGATE <callsign>            -> IFF interrogation
	//   DECLARE <callsign> HOSTILE        -> classify hostile
	//   DECLARE <callsign> FRIENDLY       -> classify friendly
	//   SHOW <callsign> HOSTILE/FRIENDLY  -> alt spelling for DECLARE
	//   SCRAMBLE BANDIT <callsign>        -> 3-ship boundary launch + auto-vector
	//   ALERT FLIGHT SCRAMBLE BANDIT <cs> -> same
	{
		int32 GCI = 0;
		// Eat optional leading filler ("alpha flight ...", "alert flight ...", "alert five ...")
		while (GCI < Tokens.Num() && (Tokens[GCI] == TEXT("alpha") || Tokens[GCI] == TEXT("alert") ||
		       Tokens[GCI] == TEXT("flight") || Tokens[GCI] == TEXT("five") || Tokens[GCI] == TEXT("the")))
		{
			++GCI;
		}
		if (GCI < Tokens.Num())
		{
			const FString Verb = Tokens[GCI];
			int32 After = GCI + 1;

			if (Verb == TEXT("interrogate"))
			{
				const FName Cs = ResolveCallsign(Controller, Tokens, After);
				if (Cs.IsNone()) { return TEXT("INTERROGATE - say again target"); }
				EThreatClass C; int32 Sq;
				const bool bOk = Controller->InterrogateIFF(Cs, C, Sq);
				if (!bOk) { return FString::Printf(TEXT("%s, NO RESPONSE"), *Cs.ToString()); }
				const TCHAR* CL = (C == EThreatClass::Friendly) ? TEXT("FRIENDLY") :
				                  (C == EThreatClass::Hostile)  ? TEXT("HOSTILE")  :
				                  (C == EThreatClass::Neutral)  ? TEXT("NEUTRAL")  : TEXT("UNKNOWN");
				return FString::Printf(TEXT("%s, %s, SQUAWK %04d"), *Cs.ToString(), CL, Sq);
			}
			if (Verb == TEXT("declare") || Verb == TEXT("show"))
			{
				const FName Cs = ResolveCallsign(Controller, Tokens, After);
				if (Cs.IsNone()) { return FString::Printf(TEXT("%s - say again target"), *Verb.ToUpper()); }
				EThreatClass NewC = EThreatClass::Unknown;
				if (After < Tokens.Num())
				{
					const FString& Cls = Tokens[After];
					if      (Cls == TEXT("hostile"))  { NewC = EThreatClass::Hostile;  }
					else if (Cls == TEXT("friendly")) { NewC = EThreatClass::Friendly; }
					else if (Cls == TEXT("neutral"))  { NewC = EThreatClass::Neutral;  }
					else if (Cls == TEXT("unknown"))  { NewC = EThreatClass::Unknown;  }
					else { return FString::Printf(TEXT("%s - say again classification"), *Cs.ToString()); }
				}
				else { return FString::Printf(TEXT("%s - say again classification"), *Cs.ToString()); }
				Controller->ClassifyAircraft(Cs, NewC);
				const TCHAR* CL = (NewC == EThreatClass::Friendly) ? TEXT("FRIENDLY") :
				                  (NewC == EThreatClass::Hostile)  ? TEXT("HOSTILE")  :
				                  (NewC == EThreatClass::Neutral)  ? TEXT("NEUTRAL")  : TEXT("UNKNOWN");
				return FString::Printf(TEXT("%s SHOWING %s"), *Cs.ToString(), CL);
			}
			if (Verb == TEXT("scramble"))
			{
				// "scramble bandit <cs>" or "scramble <cs>"
				if (After < Tokens.Num() && (Tokens[After] == TEXT("bandit") || Tokens[After] == TEXT("bogey"))) { ++After; }
				const FName Cs = ResolveCallsign(Controller, Tokens, After);
				if (Cs.IsNone()) { return TEXT("SCRAMBLE - say again bandit"); }
				const int32 N = Controller->ScrambleInterceptors(Cs);
				if (N <= 0) { return FString::Printf(TEXT("SCRAMBLE %s - unable"), *Cs.ToString()); }
				return FString::Printf(TEXT("ALPHA FLIGHT SCRAMBLED, %d-SHIP INBOUND %s"), N, *Cs.ToString());
			}
		}
	}

	int32 Idx = 0;
	const FName Callsign = ResolveCallsign(Controller, Tokens, Idx);
	if (Callsign.IsNone())
	{
		return TEXT("Station calling, say again your callsign");
	}

	struct FParsed { FAircraftInstruction Instruction; FString Readback; };
	TArray<FParsed> Parsed;

	const bool bExpedite = Lower.Contains(TEXT("expedite")); // applies to altitude changes
	bool bGoAround = false;

	auto Make = [&](EInstructionType Type, float TargetValue, const FString& Readback, int32 TurnDir, bool bExp)
	{
		FParsed P;
		P.Instruction.TargetCallsign = Callsign;
		P.Instruction.Type = Type;
		P.Instruction.TargetValue = TargetValue;
		P.Instruction.TurnDirection = TurnDir;
		P.Instruction.bExpedite = bExp;
		P.Readback = Readback;
		Parsed.Add(P);
	};
	auto DirWord = [](int32 Dir) -> FString { return Dir < 0 ? TEXT("left ") : (Dir > 0 ? TEXT("right ") : TEXT("")); };

	while (Idx < Tokens.Num())
	{
		const FString T = Tokens[Idx];
		int32 Value = 0;
		int32 D;

		if (T == TEXT("go") && Idx + 1 < Tokens.Num() && Tokens[Idx + 1] == TEXT("around"))
		{
			bGoAround = true; Idx += 2;
		}
		else if (T == TEXT("turn"))
		{
			++Idx;
			int32 Dir = 0;
			if (Idx < Tokens.Num() && Tokens[Idx] == TEXT("left")) { Dir = -1; ++Idx; }
			else if (Idx < Tokens.Num() && Tokens[Idx] == TEXT("right")) { Dir = 1; ++Idx; }

			if (Idx < Tokens.Num() && Tokens[Idx] == TEXT("heading"))
			{
				++Idx;
				if (ParseNumberRun(Tokens, Idx, Value)) { Make(EInstructionType::HeadingChange, (float)Value, FString::Printf(TEXT("%sheading %03d"), *DirWord(Dir), Value), Dir, false); }
			}
			else if (ParseNumberRun(Tokens, Idx, Value)) // relative: "turn left 30 [degrees]"
			{
				float Cur = 0.f;
				if (AClearanceAirspaceManager* AM = Controller->GetAirspaceManager()) { Cur = AM->GetAircraftState(Callsign).Heading; }
				float Target = (Dir < 0) ? Cur - (float)Value : Cur + (float)Value;
				Target = FMath::Fmod(Target + 360.f, 360.f);
				Make(EInstructionType::HeadingChange, Target, FString::Printf(TEXT("%s%d degrees"), *DirWord(Dir), Value), Dir, false);
				if (Idx < Tokens.Num() && Tokens[Idx] == TEXT("degrees")) { ++Idx; }
			}
		}
		else if (T == TEXT("heading"))
		{
			++Idx;
			if (ParseNumberRun(Tokens, Idx, Value)) { Make(EInstructionType::HeadingChange, (float)Value, FString::Printf(TEXT("heading %03d"), Value), 0, false); }
		}
		else if (T == TEXT("flight") && Idx + 1 < Tokens.Num() && Tokens[Idx + 1] == TEXT("level"))
		{
			Idx += 2;
			if (ParseNumberRun(Tokens, Idx, Value)) { Make(EInstructionType::AltitudeChange, (float)Value * 100.f, FString::Printf(TEXT("flight level %d"), Value), 0, bExpedite); }
		}
		else if (T == TEXT("altitude"))
		{
			++Idx;
			if (ParseNumberRun(Tokens, Idx, Value)) { Make(EInstructionType::AltitudeChange, (float)Value, FString::Printf(TEXT("altitude %d"), Value), 0, bExpedite); }
		}
		else if (T == TEXT("speed"))
		{
			++Idx;
			if (ParseNumberRun(Tokens, Idx, Value)) { Make(EInstructionType::SpeedChange, (float)Value, FString::Printf(TEXT("speed %d"), Value), 0, false); }
		}
		else if (T == TEXT("descend") || T == TEXT("climb"))
		{
			// "descend 8000" with no "altitude"/"flight level" keyword -> plain altitude.
			++Idx;
			int32 Peek = Idx;
			while (Peek < Tokens.Num() && (Tokens[Peek] == TEXT("to") || Tokens[Peek] == TEXT("and"))) { ++Peek; }
			if (Peek < Tokens.Num() && (IsAllDigits(Tokens[Peek]) || DigitWord(Tokens[Peek], D)))
			{
				Idx = Peek;
				if (ParseNumberRun(Tokens, Idx, Value)) { Make(EInstructionType::AltitudeChange, (float)Value, FString::Printf(TEXT("altitude %d"), Value), 0, bExpedite); }
			}
		}
		else if (T == TEXT("approach") || T == TEXT("ils") || T == TEXT("land"))
		{
			++Idx;
			Make(EInstructionType::ApproachClearance, 0.f, TEXT("cleared approach"), 0, false);
		}
		else if (T == TEXT("takeoff"))
		{
			++Idx;
			Make(EInstructionType::TakeoffClearance, 0.f, TEXT("cleared for takeoff"), 0, false);
		}
		else if (T == TEXT("contact") || T == TEXT("leave"))
		{
			Make(EInstructionType::ExitSector, 0.f, TEXT("leaving the sector"), 0, false);
			break; // the rest is the facility/frequency - don't parse it as commands
		}
		else
		{
			++Idx; // flavour word: reduce / increase / maintain / fly / cleared / for / the...
		}
	}

	if (Parsed.Num() == 0 && !bGoAround)
	{
		return FString::Printf(TEXT("%s, say again"), *Callsign.ToString());
	}

	// Issue everything, read back the accepted parts, flag any the aircraft can't take.
	TArray<FString> Accepted;
	TArray<FString> Unable;

	if (bGoAround)
	{
		if (UClearanceCommsRouter* Router = Controller->GetCommsRouter()) { Router->RouteGoAround(Callsign); }
		Accepted.Add(TEXT("going around"));
	}

	bool bNoResponse = false;
	for (const FParsed& P : Parsed)
	{
		const EInstructionResult Result = Controller->PlayerIssueInstruction(P.Instruction);
		if (Result == EInstructionResult::Accepted) { Accepted.Add(P.Readback); }
		else if (Result == EInstructionResult::Rejected_NoResponse) { bNoResponse = true; }
		else { Unable.Add(P.Readback); }
	}

	if (bNoResponse && Accepted.Num() == 0 && Unable.Num() == 0)
	{
		return FString::Printf(TEXT("%s, NO RESPONSE"), *Callsign.ToString());
	}

	FString Readback = Callsign.ToString();
	if (Accepted.Num() > 0) { Readback += TEXT(", ") + FString::Join(Accepted, TEXT(", ")); }
	if (Unable.Num() > 0) { Readback += TEXT(" -- UNABLE ") + FString::Join(Unable, TEXT(", ")); }
	return Readback;
}

// clearance.say <free text>  e.g.  clearance.say speedbird 101 descend flight level 100 turn right heading 270 speed 210
static FAutoConsoleCommandWithWorldAndArgs GClearanceSayCmd(
	TEXT("clearance.say"),
	TEXT("clearance.say <transmission> - parse a spoken-style ATC clearance and issue it"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() == 0) { return; }
		const FString Transmission = FString::Join(Args, TEXT(" "));
		for (TActorIterator<AClearanceSimulationController> It(World); It; ++It)
		{
			const FString Readback = UClearancePhraseology::Interpret(*It, Transmission);
			UE_LOG(LogTemp, Display, TEXT("[ATC] \"%s\"  ->  %s"), *Transmission, *Readback);
			if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Emerald, FString::Printf(TEXT("ATC: %s"), *Readback)); }
			return;
		}
	}));
