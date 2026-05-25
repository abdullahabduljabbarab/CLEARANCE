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

	bool IsThreeAlpha(const FString& S)
	{
		if (S.Len() != 3) return false;
		for (TCHAR C : S) { if (!FChar::IsAlpha(C)) return false; }
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
			{TEXT("emirates"),TEXT("UAE")},{TEXT("airfrance"),TEXT("AFR")}
		};

		FString Prefix;
		if (Idx < Tokens.Num())
		{
			const FString W = Tokens[Idx];
			if (const FString* P = Telephony.Find(W)) { Prefix = *P; ++Idx; }
			else if (W == TEXT("air") && Idx + 1 < Tokens.Num() && Tokens[Idx + 1] == TEXT("france")) { Prefix = TEXT("AFR"); Idx += 2; }
			else if (IsThreeAlpha(W)) { Prefix = W.ToUpper(); ++Idx; } // raw ICAO e.g. "baw"
		}

		int32 Number = 0;
		const bool bHasNumber = ParseNumberRun(Tokens, Idx, Number);

		AClearanceAirspaceManager* AM = Controller->GetAirspaceManager();
		if (!AM || (Prefix.IsEmpty() && !bHasNumber))
		{
			return NAME_None;
		}

		// Exact reconstruction first (e.g. "speedbird one zero one" -> BAW101).
		if (!Prefix.IsEmpty() && bHasNumber)
		{
			const FName Exact(*(Prefix + FString::FromInt(Number)));
			if (AM->IsCallsignRegistered(Exact)) return Exact;
		}

		// Otherwise match a live aircraft by prefix and/or number.
		const FString NumStr = bHasNumber ? FString::FromInt(Number) : FString();
		for (const FAircraftState& S : AM->GetAllAircraftStates())
		{
			const FString CS = S.Callsign.ToString();
			const bool bPrefixOk = Prefix.IsEmpty() || CS.StartsWith(Prefix);
			const bool bNumOk = NumStr.IsEmpty() || CS.Contains(NumStr);
			if (bPrefixOk && bNumOk)
			{
				return S.Callsign;
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

	for (const FParsed& P : Parsed)
	{
		const EInstructionResult Result = Controller->PlayerIssueInstruction(P.Instruction);
		if (Result == EInstructionResult::Accepted) { Accepted.Add(P.Readback); }
		else { Unable.Add(P.Readback); }
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
