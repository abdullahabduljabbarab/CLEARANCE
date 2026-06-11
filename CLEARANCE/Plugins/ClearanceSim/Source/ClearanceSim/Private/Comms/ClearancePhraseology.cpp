#include "Comms/ClearancePhraseology.h"
#include "Comms/ClearanceVoiceOutput.h"
#include "Simulation/ClearanceSimulationController.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Comms/ClearanceCommsRouter.h"
#include "EngineUtils.h"

namespace
{
	// Push a line to every peer's VoiceOutput so the pilot/aircraft (or alert
	// flight, for GCI) actually speaks the readback on each client, not just on
	// whichever machine the server's iterator happened to land on. Routes through
	// the Controller's NetMulticast so all participants hear the same exchange. - TripleA
	void SpeakOut(AClearanceSimulationController* Controller, FName Callsign, const FString& Text)
	{
		if (!Controller || Text.IsEmpty()) { return; }
		Controller->Multicast_PlayTTS(Callsign, Text, FString(), /*bPanic*/ false);
	}

	// System / controller voice for messages that aren't tied to a specific
	// aircraft - "Station calling, say again", "SCRAMBLE - say again bandit",
	// etc. Fixed voice (Eric) so the operator learns to recognise "this is the
	// system asking me to repeat" vs "a specific pilot replied". - TripleA
	void SpeakAsController(AClearanceSimulationController* Controller, const FString& Text)
	{
		if (!Controller || Text.IsEmpty()) { return; }
		Controller->Multicast_PlayTTS(NAME_None, Text, TEXT("en-US-EricNeural"), /*bPanic*/ false);
	}

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
				if (Cs.IsNone())
				{
					const FString R = TEXT("INTERROGATE - say again target");
					SpeakAsController(Controller, R);
					return R;
				}
				EThreatClass C; int32 Sq;
				const bool bOk = Controller->InterrogateIFF(Cs, C, Sq);
				if (!bOk)
				{
					const FString R = FString::Printf(TEXT("%s, NO RESPONSE"), *Cs.ToString());
					return R;  // NORDO - silent on purpose
				}
				const TCHAR* CL = (C == EThreatClass::Friendly) ? TEXT("FRIENDLY") :
				                  (C == EThreatClass::Hostile)  ? TEXT("HOSTILE")  :
				                  (C == EThreatClass::Neutral)  ? TEXT("NEUTRAL")  : TEXT("UNKNOWN");
				const FString R = FString::Printf(TEXT("%s, %s, SQUAWK %04d"), *Cs.ToString(), CL, Sq);
				SpeakOut(Controller, Cs, R);
				return R;
			}
			if (Verb == TEXT("declare") || Verb == TEXT("show"))
			{
				const FName Cs = ResolveCallsign(Controller, Tokens, After);
				if (Cs.IsNone())
				{
					const FString R = FString::Printf(TEXT("%s - say again target"), *Verb.ToUpper());
					SpeakAsController(Controller, R);
					return R;
				}
				EThreatClass NewC = EThreatClass::Unknown;
				if (After < Tokens.Num())
				{
					const FString& Cls = Tokens[After];
					if      (Cls == TEXT("hostile"))  { NewC = EThreatClass::Hostile;  }
					else if (Cls == TEXT("friendly")) { NewC = EThreatClass::Friendly; }
					else if (Cls == TEXT("neutral"))  { NewC = EThreatClass::Neutral;  }
					else if (Cls == TEXT("unknown"))  { NewC = EThreatClass::Unknown;  }
					else
					{
						const FString R = FString::Printf(TEXT("%s - say again classification"), *Cs.ToString());
						SpeakAsController(Controller, R);
						return R;
					}
				}
				else
				{
					const FString R = FString::Printf(TEXT("%s - say again classification"), *Cs.ToString());
					SpeakAsController(Controller, R);
					return R;
				}
				Controller->ClassifyAircraft(Cs, NewC);
				const TCHAR* CL = (NewC == EThreatClass::Friendly) ? TEXT("FRIENDLY") :
				                  (NewC == EThreatClass::Hostile)  ? TEXT("HOSTILE")  :
				                  (NewC == EThreatClass::Neutral)  ? TEXT("NEUTRAL")  : TEXT("UNKNOWN");
				const FString R = FString::Printf(TEXT("%s SHOWING %s"), *Cs.ToString(), CL);
				SpeakOut(Controller, Cs, R);
				return R;
			}
			if (Verb == TEXT("shadow") || Verb == TEXT("intercept"))
			{
				// "shadow <cs>", "intercept <cs>", "intercept visual <cs>" - all
				// launch a tailing escort on a 7500 / 7600 aircraft.
				if (Verb == TEXT("intercept") && After < Tokens.Num() && Tokens[After] == TEXT("visual"))
				{
					++After;
				}
				const FName Cs = ResolveCallsign(Controller, Tokens, After);
				if (Cs.IsNone())
				{
					const FString R = TEXT("SHADOW - say again target");
					SpeakAsController(Controller, R);
					return R;
				}
				const int32 N = Controller->ShadowEscort(Cs);
				if (N <= 0)
				{
					const FString R = FString::Printf(TEXT("SHADOW %s - unable, not 7500"), *Cs.ToString());
					SpeakAsController(Controller, R);
					return R;
				}
				const FString R = FString::Printf(TEXT("ALPHA FLIGHT SHADOWING, %d-SHIP TAILING %s"), N, *Cs.ToString());
				SpeakOut(Controller, FName(TEXT("VIPER01")), R);
				return R;
			}
			if (Verb == TEXT("scramble"))
			{
				// "scramble bandit <cs>" or "scramble <cs>"
				if (After < Tokens.Num() && (Tokens[After] == TEXT("bandit") || Tokens[After] == TEXT("bogey"))) { ++After; }
				const FName Cs = ResolveCallsign(Controller, Tokens, After);
				if (Cs.IsNone())
				{
					const FString R = TEXT("SCRAMBLE - say again bandit");
					SpeakAsController(Controller, R);
					return R;
				}
				const int32 N = Controller->ScrambleInterceptors(Cs);
				if (N <= 0)
				{
					const FString R = FString::Printf(TEXT("SCRAMBLE %s - unable"), *Cs.ToString());
					SpeakAsController(Controller, R);
					return R;
				}
				const FString R = FString::Printf(TEXT("ALPHA FLIGHT SCRAMBLED, %d-SHIP INBOUND %s"), N, *Cs.ToString());
				SpeakOut(Controller, FName(TEXT("VIPER01")), R);
				return R;
			}
		}
	}

	int32 Idx = 0;
	const FName Callsign = ResolveCallsign(Controller, Tokens, Idx);
	if (Callsign.IsNone())
	{
		const FString R = TEXT("Station calling, say again your callsign");
		SpeakAsController(Controller, R);
		return R;
	}

	struct FParsed { FAircraftInstruction Instruction; FString Readback; };
	TArray<FParsed> Parsed;

	const bool bExpedite = Lower.Contains(TEXT("expedite")); // applies to altitude changes
	bool bGoAround = false;
	bool bApproachIssued = false;   // "cleared ILS approach" has two keywords, only count once
	bool bTakeoffIssued  = false;
	FString HoldReadback;            // injected into Accepted after the parse loop
	bool bExitIssued     = false;

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

			// Pilot readback: "turning left heading two seven zero" - include verb. - TripleA
			const TCHAR* TurningWord = (Dir < 0) ? TEXT("turning left ") :
			                           (Dir > 0) ? TEXT("turning right ") : TEXT("turning ");

			if (Idx < Tokens.Num() && Tokens[Idx] == TEXT("heading"))
			{
				++Idx;
				if (ParseNumberRun(Tokens, Idx, Value)) { Make(EInstructionType::HeadingChange, (float)Value, FString::Printf(TEXT("%sheading %03d"), TurningWord, Value), Dir, false); }
			}
			else if (ParseNumberRun(Tokens, Idx, Value)) // relative: "turn left 30 [degrees]"
			{
				float Cur = 0.f;
				if (AClearanceAirspaceManager* AM = Controller->GetAirspaceManager()) { Cur = AM->GetAircraftState(Callsign).Heading; }
				float Target = (Dir < 0) ? Cur - (float)Value : Cur + (float)Value;
				Target = FMath::Fmod(Target + 360.f, 360.f);
				Make(EInstructionType::HeadingChange, Target, FString::Printf(TEXT("%s%d degrees"), TurningWord, Value), Dir, false);
				if (Idx < Tokens.Num() && Tokens[Idx] == TEXT("degrees")) { ++Idx; }
			}
		}
		else if (T == TEXT("heading"))
		{
			++Idx;
			if (ParseNumberRun(Tokens, Idx, Value))
			{
				// No turn verb said by the controller; the pilot readback infers the
				// direction from the shortest turn ("turning right" vs "turning left")
				// so it still sounds like radio comms. - TripleA
				float Cur = 0.f;
				if (AClearanceAirspaceManager* AM = Controller->GetAirspaceManager()) { Cur = AM->GetAircraftState(Callsign).Heading; }
				const float Delta = FMath::FindDeltaAngleDegrees(Cur, (float)Value);
				const TCHAR* TurningWord = (Delta < -1.f) ? TEXT("turning left ") :
				                           (Delta >  1.f) ? TEXT("turning right ") : TEXT("");
				const int32 InferredDir = (Delta < 0.f) ? -1 : (Delta > 0.f ? 1 : 0);
				Make(EInstructionType::HeadingChange, (float)Value, FString::Printf(TEXT("%sheading %03d"), TurningWord, Value), InferredDir, false);
			}
		}
		else if (T == TEXT("flight") && Idx + 1 < Tokens.Num() && Tokens[Idx + 1] == TEXT("level"))
		{
			Idx += 2;
			if (ParseNumberRun(Tokens, Idx, Value))
			{
				// Pilot readback includes the verb - "descending to flight level 250"
				// or "climbing to flight level 250" depending on current altitude. - TripleA
				float CurAlt = 0.f;
				if (AClearanceAirspaceManager* AM = Controller->GetAirspaceManager()) { CurAlt = AM->GetAircraftState(Callsign).Altitude; }
				const float Target = (float)Value * 100.f;
				const TCHAR* Verb = (CurAlt > Target + 50.f) ? TEXT("descending to") :
				                    (CurAlt < Target - 50.f) ? TEXT("climbing to")   : TEXT("maintaining");
				Make(EInstructionType::AltitudeChange, Target, FString::Printf(TEXT("%s flight level %d"), Verb, Value), 0, bExpedite);
			}
		}
		else if (T == TEXT("altitude"))
		{
			++Idx;
			if (ParseNumberRun(Tokens, Idx, Value))
			{
				float CurAlt = 0.f;
				if (AClearanceAirspaceManager* AM = Controller->GetAirspaceManager()) { CurAlt = AM->GetAircraftState(Callsign).Altitude; }
				const float Target = (float)Value;
				const TCHAR* Verb = (CurAlt > Target + 50.f) ? TEXT("descending to") :
				                    (CurAlt < Target - 50.f) ? TEXT("climbing to")   : TEXT("maintaining");
				Make(EInstructionType::AltitudeChange, Target, FString::Printf(TEXT("%s altitude %d"), Verb, Value), 0, bExpedite);
			}
		}
		else if (T == TEXT("speed"))
		{
			++Idx;
			if (ParseNumberRun(Tokens, Idx, Value))
			{
				// Pilot readback uses "reducing to" / "increasing to" / "maintaining"
				// based on whether the new speed is lower / higher / matches. - TripleA
				float CurSpd = 0.f;
				if (AClearanceAirspaceManager* AM = Controller->GetAirspaceManager()) { CurSpd = AM->GetAircraftState(Callsign).Speed; }
				const float Target = (float)Value;
				const TCHAR* Verb = (CurSpd > Target + 5.f) ? TEXT("reducing to") :
				                    (CurSpd < Target - 5.f) ? TEXT("increasing to") : TEXT("maintaining");
				Make(EInstructionType::SpeedChange, Target, FString::Printf(TEXT("%s speed %d"), Verb, Value), 0, false);
			}
		}
		else if (T == TEXT("descend") || T == TEXT("climb"))
		{
			// "descend 8000" / "climb 8000" - the verb the operator used is the readback verb.
			const bool bDescend = (T == TEXT("descend"));
			++Idx;
			int32 Peek = Idx;
			while (Peek < Tokens.Num() && (Tokens[Peek] == TEXT("to") || Tokens[Peek] == TEXT("and"))) { ++Peek; }
			if (Peek < Tokens.Num() && (IsAllDigits(Tokens[Peek]) || DigitWord(Tokens[Peek], D)))
			{
				Idx = Peek;
				if (ParseNumberRun(Tokens, Idx, Value))
				{
					const TCHAR* Verb = bDescend ? TEXT("descending to") : TEXT("climbing to");
					Make(EInstructionType::AltitudeChange, (float)Value, FString::Printf(TEXT("%s altitude %d"), Verb, Value), 0, bExpedite);
				}
			}
		}
		else if (T == TEXT("approach") || T == TEXT("ils") || T == TEXT("land"))
		{
			++Idx;
			if (!bApproachIssued)
			{
				Make(EInstructionType::ApproachClearance, 0.f, TEXT("cleared the approach"), 0, false);
				bApproachIssued = true;
			}
		}
		else if (T == TEXT("takeoff"))
		{
			++Idx;
			if (!bTakeoffIssued)
			{
				Make(EInstructionType::TakeoffClearance, 0.f, TEXT("cleared for takeoff"), 0, false);
				bTakeoffIssued = true;
			}
		}
		else if (T == TEXT("contact") || T == TEXT("leave") || T == TEXT("exit") || T == TEXT("divert"))
		{
			if (!bExitIssued)
			{
				Make(EInstructionType::ExitSector, 0.f, TEXT("leaving the sector"), 0, false);
				bExitIssued = true;
			}
			break; // the rest is the facility/frequency - don't parse it as commands
		}
		else if (T == TEXT("lost") || T == TEXT("no") || T == TEXT("noJoy") || T == TEXT("nojoy"))
		{
			// Operator declares a track lost - "<callsign> lost contact",
			// "<callsign> no joy" (fighter pilot brevity for "I can't see the
			// target"), "<callsign> lost track". Eats the optional follow-up
			// noun ("contact"/"track"/"joy") so the readback is clean. - TripleA
			++Idx;
			if (Idx < Tokens.Num() && (Tokens[Idx] == TEXT("contact") || Tokens[Idx] == TEXT("track") || Tokens[Idx] == TEXT("joy")))
			{
				++Idx;
			}
			Make(EInstructionType::DeclareTrackLost, 0.f, TEXT("track lost"), 0, false);
			break;
		}
		else if (T == TEXT("hold"))
		{
			// "hold", "hold left", "hold right" - enter a racetrack at the current
			// altitude and heading. Player issues a heading / approach / etc. to
			// exit. Bypasses the normal instruction pipeline because hold isn't a
			// per-axis target - it's a flight mode. - TripleA
			++Idx;
			bool bRightTurns = true;
			if (Idx < Tokens.Num() && Tokens[Idx] == TEXT("left"))  { bRightTurns = false; ++Idx; }
			else if (Idx < Tokens.Num() && Tokens[Idx] == TEXT("right")) { bRightTurns = true; ++Idx; }
			// Eat "turns" / "pattern" filler
			while (Idx < Tokens.Num() && (Tokens[Idx] == TEXT("turns") || Tokens[Idx] == TEXT("pattern")
				|| Tokens[Idx] == TEXT("present") || Tokens[Idx] == TEXT("position"))) { ++Idx; }

			if (AClearanceAirspaceManager* AM = Controller->GetAirspaceManager())
			{
				FAircraftState S = AM->GetAircraftState(Callsign);
				if (S.bIsValid)
				{
					S.bInHold = true;
					S.bHoldRightTurns = bRightTurns;
					S.HoldInboundHeading = S.Heading;
					S.HoldLegStartSeconds = 0.f;
					AM->RequestStateUpdate(S);
				}
			}
			// State is already in hold from the direct RequestStateUpdate above -
			// inject the readback into Accepted after the parse loop completes.
			HoldReadback = FString::Printf(TEXT("holding present position, %s turns"),
				bRightTurns ? TEXT("right") : TEXT("left"));
		}
		else
		{
			++Idx; // flavour word: reduce / increase / maintain / fly / cleared / for / the...
		}
	}

	if (Parsed.Num() == 0 && !bGoAround && HoldReadback.IsEmpty())
	{
		const FString R = FString::Printf(TEXT("%s, say again"), *Callsign.ToString());
		SpeakOut(Controller, Callsign, R);
		return R;
	}

	// Issue everything, read back the accepted parts, flag any the aircraft can't take.
	TArray<FString> Accepted;
	TArray<FString> Unable;

	if (bGoAround)
	{
		if (UClearanceCommsRouter* Router = Controller->GetCommsRouter()) { Router->RouteGoAround(Callsign); }
		Accepted.Add(TEXT("going around"));
	}

	if (!HoldReadback.IsEmpty())
	{
		Accepted.Add(HoldReadback);
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
	SpeakOut(Controller, Callsign, Readback);
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
