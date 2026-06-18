#include "Comms/ClearanceVoiceOutput.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Sound/SoundWaveProcedural.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Serialization/MemoryReader.h"

AClearanceVoiceOutput::AClearanceVoiceOutput()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AClearanceVoiceOutput::BeginPlay()
{
	Super::BeginPlay();

	ServerUrl = FString::Printf(TEXT("http://127.0.0.1:%d"), ServerPort);

	if (bAutoLaunchServer)
	{
		TryLaunchServer();
	}
}

void AClearanceVoiceOutput::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bLaunchedServer && ServerProcHandle.IsValid())
	{
		FPlatformProcess::TerminateProc(ServerProcHandle, true);
		FPlatformProcess::CloseProc(ServerProcHandle);
		bLaunchedServer = false;
	}
	Super::EndPlay(EndPlayReason);
}

bool AClearanceVoiceOutput::IsServerResponding() const
{
	// Cheap probe - just see if anything binds the port. - TripleA
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS) { return false; }
	FSocket* Probe = SS->CreateSocket(NAME_Stream, TEXT("TtsProbe"), false);
	if (!Probe) { return false; }
	TSharedRef<FInternetAddr> Addr = SS->CreateInternetAddr();
	bool bValid = false;
	Addr->SetIp(TEXT("127.0.0.1"), bValid);
	Addr->SetPort(ServerPort);
	const bool bUp = bValid && Probe->Connect(*Addr);
	Probe->Close();
	SS->DestroySocket(Probe);
	return bUp;
}

void AClearanceVoiceOutput::TryLaunchServer()
{
	if (IsServerResponding())
	{
		UE_LOG(LogTemp, Display, TEXT("[VoiceOut] TTS server already up on port %d - reusing it"), ServerPort);
		return;
	}

	FString Dir = ServerDirectory.IsEmpty()
		? FPaths::Combine(FPaths::ProjectDir(), TEXT("TtsServer"))
		: ServerDirectory;
	Dir = FPaths::ConvertRelativePathToFull(Dir);
	const FString Script = FPaths::Combine(Dir, ServerScriptName);

	if (!FPaths::FileExists(Script))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[VoiceOut] tts_server.py not found in '%s' - voice readbacks won't speak until it's there"),
			*Dir);
		return;
	}

	const FString Args = FString::Printf(TEXT("\"%s\" --host 127.0.0.1 --port %d"), *Script, ServerPort);
	ServerProcHandle = FPlatformProcess::CreateProc(*PythonExe, *Args,
		/*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true,
		nullptr, 0, *Dir, nullptr);

	if (ServerProcHandle.IsValid())
	{
		bLaunchedServer = true;
		UE_LOG(LogTemp, Display, TEXT("[VoiceOut] launched TTS server '%s %s' from '%s'"), *PythonExe, *Args, *Dir);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[VoiceOut] failed to launch TTS server (is Python on PATH?)"));
	}
}

namespace
{
	// Per-region voice pools. Mix of male / female in each so two aircraft from
	// the same airline can sound noticeably different. Each aircraft reserves one
	// voice for its entire life; no two active aircraft share. - TripleA
	const TArray<FString>& BritishVoices()
	{
		static const TArray<FString> V = {
			TEXT("en-GB-RyanNeural"), TEXT("en-GB-SoniaNeural"),
			TEXT("en-GB-LibbyNeural"), TEXT("en-GB-ThomasNeural")
		};
		return V;
	}
	const TArray<FString>& USVoices()
	{
		static const TArray<FString> V = {
			TEXT("en-US-AndrewNeural"),      TEXT("en-US-AriaNeural"),
			TEXT("en-US-ChristopherNeural"), TEXT("en-US-BrianNeural"),
			TEXT("en-US-JennyNeural"),       TEXT("en-US-EmmaNeural"),
			TEXT("en-US-EricNeural"),        TEXT("en-US-RogerNeural"),
			TEXT("en-US-GuyNeural"),         TEXT("en-US-AvaNeural")
		};
		return V;
	}
	const TArray<FString>& IrishVoices()
	{
		static const TArray<FString> V = {
			TEXT("en-IE-ConnorNeural"), TEXT("en-IE-EmilyNeural")
		};
		return V;
	}
	const TArray<FString>& AustralianVoices()
	{
		static const TArray<FString> V = {
			TEXT("en-AU-NatashaNeural"), TEXT("en-AU-WilliamNeural")
		};
		return V;
	}
}

FString AClearanceVoiceOutput::PickVoiceForCallsign(FName Callsign)
{
	// Already assigned? Same voice forever. - TripleA
	if (const FString* Existing = CallsignToVoice.Find(Callsign))
	{
		return *Existing;
	}

	// Region preference per airline - falls through to other pools if the
	// preferred region is exhausted by other active aircraft.
	const FString CS = Callsign.ToString().ToUpper();
	TArray<const TArray<FString>*> Priority;
	if (CS.StartsWith(TEXT("BAW")) || CS.StartsWith(TEXT("UAE")))
	{
		Priority = { &BritishVoices(), &USVoices(), &AustralianVoices(), &IrishVoices() };
	}
	else if (CS.StartsWith(TEXT("AFR")))
	{
		Priority = { &IrishVoices(), &BritishVoices(), &USVoices(), &AustralianVoices() };
	}
	else
	{
		// DLH, UAL, AAL, VIPER, default - US-leaning.
		Priority = { &USVoices(), &BritishVoices(), &AustralianVoices(), &IrishVoices() };
	}

	for (const TArray<FString>* Pool : Priority)
	{
		for (const FString& V : *Pool)
		{
			if (!VoicesInUse.Contains(V))
			{
				CallsignToVoice.Add(Callsign, V);
				VoicesInUse.Add(V);
				return V;
			}
		}
	}

	// Everything's busy (very rare - all ~18 voices in use). Pick the first one
	// without reserving so it'll still cycle. - TripleA
	const FString Fallback = USVoices()[0];
	CallsignToVoice.Add(Callsign, Fallback);
	return Fallback;
}

void AClearanceVoiceOutput::ReleaseVoiceForCallsign(FName Callsign)
{
	if (const FString* V = CallsignToVoice.Find(Callsign))
	{
		VoicesInUse.Remove(*V);
		CallsignToVoice.Remove(Callsign);
	}
}

namespace
{
	static const TCHAR* DigitName(TCHAR D)
	{
		static const TCHAR* Names[10] = {
			TEXT("zero"), TEXT("one"), TEXT("two"), TEXT("three"), TEXT("four"),
			TEXT("five"), TEXT("six"), TEXT("seven"), TEXT("eight"), TEXT("nine")
		};
		return Names[D - TEXT('0')];
	}

	// Altitude in feet: real ATC says "eight thousand", "twelve thousand five
	// hundred" etc - not digit-by-digit. Flight levels and headings stay digit-
	// by-digit; this only fires after "altitude". - TripleA
	FString AltitudeFeetToWords(int32 N)
	{
		if (N <= 0) { return TEXT("zero"); }
		FString Out;
		const int32 Thousands  = N / 1000;
		const int32 Hundreds   = (N % 1000) / 100;
		const int32 Remainder  = N % 100;

		if (Thousands > 0)
		{
			// Natural English for the thousands part:
			//   1-9   -> "five thousand"
			//   10-19 -> "ten thousand", "twelve thousand"
			//   20-99 -> "twenty thousand", "twenty-five thousand"
			//   100+  -> digit-by-digit fallback (rare)
			// - TripleA
			static const TCHAR* Teens[] = {
				TEXT("ten"), TEXT("eleven"), TEXT("twelve"), TEXT("thirteen"),
				TEXT("fourteen"), TEXT("fifteen"), TEXT("sixteen"), TEXT("seventeen"),
				TEXT("eighteen"), TEXT("nineteen")
			};
			static const TCHAR* TensWord[] = {
				TEXT(""), TEXT(""), TEXT("twenty"), TEXT("thirty"), TEXT("forty"),
				TEXT("fifty"), TEXT("sixty"), TEXT("seventy"), TEXT("eighty"), TEXT("ninety")
			};
			if (Thousands < 10)
			{
				Out += DigitName(TEXT('0') + Thousands);
			}
			else if (Thousands < 20)
			{
				Out += Teens[Thousands - 10];
			}
			else if (Thousands < 100)
			{
				const int32 T = Thousands / 10;
				const int32 O = Thousands % 10;
				Out += TensWord[T];
				if (O > 0)
				{
					Out.AppendChar(TEXT(' '));
					Out += DigitName(TEXT('0') + O);
				}
			}
			else
			{
				const FString TStr = FString::FromInt(Thousands);
				for (TCHAR C : TStr) { Out += DigitName(C); Out.AppendChar(TEXT(' ')); }
				Out.TrimEndInline();
			}
			Out += TEXT(" thousand");
		}
		if (Hundreds > 0)
		{
			if (!Out.IsEmpty()) { Out.AppendChar(TEXT(' ')); }
			Out += DigitName(TEXT('0') + Hundreds);
			Out += TEXT(" hundred");
		}
		if (Remainder > 0)
		{
			if (!Out.IsEmpty()) { Out.AppendChar(TEXT(' ')); }
			// Sub-100 leftover (rare in ATC) - just spell its digits.
			const FString R = (Remainder < 10) ? FString::Printf(TEXT("0%d"), Remainder) : FString::FromInt(Remainder);
			for (TCHAR C : R) { Out += DigitName(C); Out.AppendChar(TEXT(' ')); }
			Out.TrimEndInline();
		}
		return Out;
	}

	// Spell numbers digit-by-digit the way ATC does ("two seven zero" not "two
	// hundred seventy"), and replace ICAO callsign prefixes with their spoken
	// telephony name so "BAW101" reads back as "speedbird one zero one". Both
	// are required for the TTS to sound like actual radio comms. - TripleA
	FString TTSize(FString In)
	{
		static const TMap<FString, FString> Reverse = {
			{TEXT("BAW"),     TEXT("speedbird")},
			{TEXT("DLH"),     TEXT("lufthansa")},
			{TEXT("UAL"),     TEXT("united")},
			{TEXT("AAL"),     TEXT("american")},
			{TEXT("UAE"),     TEXT("emirates")},
			{TEXT("AFR"),     TEXT("air france")},
			{TEXT("VIPER"),   TEXT("viper")},
			{TEXT("UNKNOWN"), TEXT("unknown")}
			// Don't add a "UNK" entry - the substitution loop runs ReplaceInline
			// without word boundaries, so "UNK" matches inside "unknown" produced
			// by the line above and corrupts it to "unknownnown". - TripleA
		};
		// Longest-key-first so "VIPER" beats a "V" prefix match.
		TArray<FString> Keys;
		Reverse.GetKeys(Keys);
		Keys.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });
		for (const FString& K : Keys)
		{
			In.ReplaceInline(*K, *Reverse[K], ESearchCase::IgnoreCase);
		}

		// Find "altitude <N>" runs and replace the number with thousands/hundreds
		// English, so "altitude 8000" speaks as "altitude eight thousand" instead of
		// the wrong "altitude eight zero zero zero". Flight levels and headings stay
		// digit-by-digit (the per-digit pass below handles them). - TripleA
		{
			int32 Pos = 0;
			while (Pos < In.Len())
			{
				const int32 Found = In.Find(TEXT("altitude"), ESearchCase::IgnoreCase, ESearchDir::FromStart, Pos);
				if (Found == INDEX_NONE) { break; }
				int32 NumStart = Found + 8; // past "altitude"
				while (NumStart < In.Len() && (FChar::IsWhitespace(In[NumStart]) || In[NumStart] == TEXT(',')))
				{
					++NumStart;
				}
				int32 NumEnd = NumStart;
				while (NumEnd < In.Len() && FChar::IsDigit(In[NumEnd])) { ++NumEnd; }
				if (NumEnd > NumStart)
				{
					const int32 N = FCString::Atoi(*In.Mid(NumStart, NumEnd - NumStart));
					const FString Words = AltitudeFeetToWords(N);
					In = In.Left(NumStart) + Words + In.Mid(NumEnd);
					Pos = NumStart + Words.Len();
				}
				else
				{
					Pos = NumStart + 1;
				}
			}
		}

		// Now split any remaining digit runs into spoken digits (FL250 -> "two five
		// zero", heading 270 -> "two seven zero").
		FString Out;
		Out.Reserve(In.Len() * 2);
		bool bInRun = false;
		for (int32 i = 0; i < In.Len(); ++i)
		{
			const TCHAR C = In[i];
			if (FChar::IsDigit(C))
			{
				if (!bInRun && Out.Len() > 0 && !FChar::IsWhitespace(Out[Out.Len() - 1]))
				{
					Out.AppendChar(TEXT(' '));
				}
				Out += DigitName(C);
				Out.AppendChar(TEXT(' '));
				bInRun = true;
			}
			else
			{
				Out.AppendChar(C);
				bInRun = false;
			}
		}
		return Out;
	}
}

void AClearanceVoiceOutput::SpeakPanic(FName Callsign, const FString& Text, const FString& VoiceTag)
{
	EnqueueSpeech(Callsign, Text, VoiceTag, /*bPanic*/ true, /*bStatic*/ false, 0.f);
}

void AClearanceVoiceOutput::Speak(FName Callsign, const FString& Text, const FString& VoiceTag)
{
	EnqueueSpeech(Callsign, Text, VoiceTag, /*bPanic*/ false, /*bStatic*/ false, 0.f);
}

void AClearanceVoiceOutput::EnqueueSpeech(FName Callsign, const FString& Text, const FString& VoiceTag, bool bPanic, bool bStatic, float StaticDuration)
{
	if (bMuted) { return; }
	if (!bStatic && Text.TrimStartAndEnd().IsEmpty()) { return; }
	if ( bStatic && StaticDuration <= 0.f) { return; }
	FPendingSpeak P;
	P.Callsign = Callsign;
	P.Text = Text;
	P.VoiceTag = VoiceTag;
	P.bPanic = bPanic;
	P.bStatic = bStatic;
	P.StaticDuration = StaticDuration;
	SpeechQueue.Add(P);
	DrainQueue();
}

void AClearanceVoiceOutput::DrainQueue()
{
	if (bChannelBusy || SpeechQueue.Num() == 0) { return; }
	FPendingSpeak Req = SpeechQueue[0];
	SpeechQueue.RemoveAt(0);
	bChannelBusy = true;

	if (Req.bStatic)
	{
		// Static burst is generated locally - no HTTP. Reserve the channel for
		// the duration of the burst then drain the next entry. - TripleA
		PlayStaticImmediate(Req.StaticDuration);
		if (UWorld* W = GetWorld())
		{
			TWeakObjectPtr<AClearanceVoiceOutput> Weak(this);
			W->GetTimerManager().SetTimer(ChannelTimer, FTimerDelegate::CreateLambda(
				[Weak]() { if (Weak.IsValid()) { Weak->bChannelBusy = false; Weak->DrainQueue(); } }),
				FMath::Max(0.1f, Req.StaticDuration + 0.2f), false);
		}
		else { bChannelBusy = false; }
		return;
	}

	SpeakInternal(Req.Callsign, Req.Text, Req.VoiceTag, Req.bPanic);
}

void AClearanceVoiceOutput::SpeakInternal(FName Callsign, const FString& Text, const FString& VoiceTag, bool bPanic)
{
	const FString Trim = TTSize(Text.TrimStartAndEnd());
	if (Trim.IsEmpty()) { return; }

	const FString Voice = VoiceTag.IsEmpty() ? PickVoiceForCallsign(Callsign) : VoiceTag;

	// JSON body. The text gets escaped via FJsonObject would be tidier, but for the
	// short readbacks ATC produces a hand-built body is fine and avoids the dep.
	FString Body;
	{
		FString Escaped = Trim;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Escaped.ReplaceInline(TEXT("\n"), TEXT(" "));
		Escaped.ReplaceInline(TEXT("\r"), TEXT(" "));
		Body = FString::Printf(TEXT("{\"text\":\"%s\",\"voice\":\"%s\"}"), *Escaped, *Voice);
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ServerUrl + TEXT("/speak"));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetContentAsString(Body);

	TWeakObjectPtr<AClearanceVoiceOutput> WeakThis(this);
	const FName CapturedCallsign = Callsign;
	const FString CapturedText = Trim;
	const bool CapturedPanic = bPanic;

	Request->OnProcessRequestComplete().BindLambda(
		[WeakThis, CapturedCallsign, CapturedText, CapturedPanic]
		(FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
		{
			if (!WeakThis.IsValid()) { return; }
			if (!bOk || !Response.IsValid() || Response->GetResponseCode() != 200)
			{
				UE_LOG(LogTemp, Warning, TEXT("[VoiceOut] TTS request failed (%d)"),
					Response.IsValid() ? Response->GetResponseCode() : -1);
				// Free the channel so a failure doesn't stall the whole queue.
				WeakThis->bChannelBusy = false;
				WeakThis->DrainQueue();
				return;
			}

			TArray<uint8> Wav = Response->GetContent();
			int32 SampleRate = 0, Channels = 0;
			TArray<uint8> PCM;
			if (!AClearanceVoiceOutput::ParseWav(Wav, SampleRate, Channels, PCM))
			{
				UE_LOG(LogTemp, Warning, TEXT("[VoiceOut] response wasn't a parseable PCM WAV"));
				WeakThis->bChannelBusy = false;
				WeakThis->DrainQueue();
				return;
			}
			WeakThis->PlayPCM(CapturedCallsign, CapturedText, SampleRate, Channels, MoveTemp(PCM), CapturedPanic);
		});
	Request->ProcessRequest();
}

bool AClearanceVoiceOutput::ParseWav(const TArray<uint8>& Wav, int32& OutSampleRate, int32& OutChannels, TArray<uint8>& OutPCM)
{
	// Minimal canonical WAV parse: RIFF/WAVE header, fmt chunk (PCM 16-bit
	// expected), data chunk. Skip any other chunks in between. - TripleA
	if (Wav.Num() < 44) { return false; }
	const uint8* P = Wav.GetData();
	if (FMemory::Memcmp(P, "RIFF", 4) != 0 || FMemory::Memcmp(P + 8, "WAVE", 4) != 0) { return false; }

	int32 i = 12;
	uint16 FmtCode = 0;
	uint16 NumChan = 0;
	uint32 SampleRate = 0;
	uint16 BitsPerSample = 0;
	const uint8* PcmStart = nullptr;
	int32 PcmSize = 0;

	while (i + 8 <= Wav.Num())
	{
		const char* Id = reinterpret_cast<const char*>(P + i);
		const uint32 Size = static_cast<uint32>(P[i + 4]) | (static_cast<uint32>(P[i + 5]) << 8)
			| (static_cast<uint32>(P[i + 6]) << 16) | (static_cast<uint32>(P[i + 7]) << 24);
		i += 8;
		if (i + static_cast<int32>(Size) > Wav.Num()) { return false; }

		if (FMemory::Memcmp(Id, "fmt ", 4) == 0)
		{
			if (Size < 16) { return false; }
			FmtCode      = static_cast<uint16>(P[i] | (P[i + 1] << 8));
			NumChan      = static_cast<uint16>(P[i + 2] | (P[i + 3] << 8));
			SampleRate   = static_cast<uint32>(P[i + 4]) | (static_cast<uint32>(P[i + 5]) << 8)
			             | (static_cast<uint32>(P[i + 6]) << 16) | (static_cast<uint32>(P[i + 7]) << 24);
			BitsPerSample= static_cast<uint16>(P[i + 14] | (P[i + 15] << 8));
		}
		else if (FMemory::Memcmp(Id, "data", 4) == 0)
		{
			PcmStart = P + i;
			PcmSize = static_cast<int32>(Size);
		}
		i += Size + (Size & 1u);  // chunks are word-aligned
	}

	if (FmtCode != 1 || BitsPerSample != 16 || !PcmStart || PcmSize <= 0) { return false; }

	OutSampleRate = static_cast<int32>(SampleRate);
	OutChannels   = static_cast<int32>(NumChan);
	OutPCM.SetNumUninitialized(PcmSize);
	FMemory::Memcpy(OutPCM.GetData(), PcmStart, PcmSize);
	return true;
}

namespace
{
	// Cheap-and-cheerful radio colour: 1-pole highpass + 1-pole lowpass to band-
	// limit the speech to a typical narrowband-radio band, a touch of white noise
	// for carrier floor, and soft atan saturation so it pumps a little. - TripleA
	void ApplyRadioFX(TArray<uint8>& PCM, int32 SampleRate,
		float HighpassHz, float LowpassHz, float NoiseAmount)
	{
		if (PCM.Num() < 4 || SampleRate <= 0) { return; }
		int16* S = reinterpret_cast<int16*>(PCM.GetData());
		const int32 N = PCM.Num() / 2;
		const float Sr = static_cast<float>(SampleRate);

		const float HpA = Sr / (2.f * PI * FMath::Max(1.f, HighpassHz) + Sr);
		const float LpA = (2.f * PI * FMath::Max(1.f, LowpassHz))
			/ (2.f * PI * FMath::Max(1.f, LowpassHz) + Sr);

		float HpPrevIn = 0.f, HpPrevOut = 0.f, LpPrev = 0.f;
		for (int32 i = 0; i < N; ++i)
		{
			float X = S[i] / 32768.f;

			// Highpass: kills the low rumble that doesn't fit on a radio.
			const float Hp = HpA * (HpPrevOut + X - HpPrevIn);
			HpPrevIn = X;
			HpPrevOut = Hp;

			// Lowpass: kills the airy treble.
			LpPrev = LpA * Hp + (1.f - LpA) * LpPrev;
			float Y = LpPrev;

			// Carrier noise floor.
			Y += (FMath::FRand() - 0.5f) * NoiseAmount;

			// Soft atan saturation to add bite without harsh clipping.
			Y = FMath::Atan(Y * 2.2f) / (PI * 0.5f);

			// Make-up gain - the bandpass + saturation drops perceived loudness.
			Y *= 1.4f;
			Y = FMath::Clamp(Y, -1.f, 1.f);

			S[i] = static_cast<int16>(Y * 32767.f);
		}
	}
}

void AClearanceVoiceOutput::PlayStatic(float DurationSeconds)
{
	// Public entry: enqueue. The actual generation+playback happens in
	// PlayStaticImmediate when the channel pops this off the queue. - TripleA
	EnqueueSpeech(NAME_None, FString(), FString(), /*bPanic*/ false, /*bStatic*/ true, DurationSeconds);
}

void AClearanceVoiceOutput::StopGPWS(FName Callsign)
{
	if (TWeakObjectPtr<UAudioComponent>* Found = GPWSComponents.Find(Callsign))
	{
		if (UAudioComponent* Comp = Found->Get())
		{
			Comp->Stop();
		}
		GPWSComponents.Remove(Callsign);
	}
}

void AClearanceVoiceOutput::PlayGPWS(FName Callsign)
{
	// Routes through the half-duplex queue the same way pilot lines do, so the
	// alarm doesn't step on other aircraft's transmissions. It takes its slot in
	// the queue and plays after whatever came before, in the GPWS voice. - TripleA
	if (GPWSText.IsEmpty()) { return; }
	EnqueueSpeech(Callsign, GPWSText, GPWSVoiceTag, /*bPanic*/ false, /*bStatic*/ false, 0.f);
}

void AClearanceVoiceOutput::PlayStaticImmediate(float DurationSeconds)
{
	if (DurationSeconds <= 0.f) { return; }
	constexpr int32 SampleRate = 22050;
	const int32 NumSamples = FMath::CeilToInt(DurationSeconds * SampleRate);
	TArray<uint8> PCM;
	PCM.SetNumUninitialized(NumSamples * 2);
	int16* S = reinterpret_cast<int16*>(PCM.GetData());

	// White noise. The bandpass + saturation in the radio FX path shapes it
	// into something that sounds like an open carrier with no modulation. - TripleA
	const float Amp = 0.55f * 32767.f;
	const int32 RampSamples = FMath::Min(NumSamples / 8, 2200); // ~100ms fade
	for (int32 i = 0; i < NumSamples; ++i)
	{
		float Env = 1.f;
		if (i < RampSamples)                       { Env = static_cast<float>(i) / RampSamples; }
		else if (i > NumSamples - RampSamples)     { Env = static_cast<float>(NumSamples - i) / RampSamples; }
		const float W = (FMath::FRand() * 2.f - 1.f) * Amp * Env;
		S[i] = static_cast<int16>(FMath::Clamp(W, -32767.f, 32767.f));
	}

	PlayPCM(NAME_None, TEXT("(static)"), SampleRate, 1, MoveTemp(PCM), /*bPanic*/ false);
}

namespace
{
	// Panic chain: deep tremolo (shaky breathing), micro-dropouts (voice catching
	// on sobs), hard saturation (yelling into mic), boosted gain. - TripleA
	void ApplyPanicFX(TArray<uint8>& PCM, int32 SampleRate)
	{
		if (PCM.Num() < 4 || SampleRate <= 0) { return; }
		int16* S = reinterpret_cast<int16*>(PCM.GetData());
		const int32 N = PCM.Num() / 2;

		const float TremoloHz       = 8.5f;   // faster shaking
		const float TremoloDepth    = 0.55f;  // deeper modulation
		const float BreakIntervalS  = 1.7f;   // catch/sob every ~1.7 sec
		const float BreakDurationS  = 0.08f;  // 80ms catch
		const float SubVibratoHz    = 4.f;    // slow pitch-feel wobble via gain
		const float SubVibratoDepth = 0.12f;

		for (int32 i = 0; i < N; ++i)
		{
			const float T = static_cast<float>(i) / static_cast<float>(SampleRate);

			// Main shake.
			const float Tremolo  = 1.f - TremoloDepth    * (0.5f + 0.5f * FMath::Sin(2.f * PI * TremoloHz * T));
			// Slow secondary wobble layered on top.
			const float Vibrato  = 1.f - SubVibratoDepth * (0.5f + 0.5f * FMath::Sin(2.f * PI * SubVibratoHz * T + 1.3f));

			// Voice catch / micro-dropout - fades the signal nearly to zero for a
			// brief moment, like the pilot's breath snags. - TripleA
			const float BreakPhase = FMath::Fmod(T, BreakIntervalS);
			float BreakEnv = 1.f;
			if (BreakPhase < BreakDurationS)
			{
				const float U = BreakPhase / BreakDurationS;     // 0..1
				BreakEnv = 0.15f + 0.85f * FMath::Abs(U - 0.5f) * 2.f; // V-shape - quiet at the catch
			}

			float X = (S[i] / 32768.f) * 2.1f * Tremolo * Vibrato * BreakEnv;
			X = FMath::Atan(X * 3.6f) / (PI * 0.5f);       // harder saturation
			X = FMath::Clamp(X, -1.f, 1.f);
			S[i] = static_cast<int16>(X * 32767.f);
		}
	}
}

void AClearanceVoiceOutput::PlayPCM(FName Callsign, const FString& Text, int32 SampleRate, int32 Channels, TArray<uint8>&& PCM, bool bPanic)
{
	if (bPanic)
	{
		// Tremolo + saturation + boosted gain do the panic work. No pitch shift -
		// Edge voices already have natural emotional range, and the old +15%
		// sample-rate trick made the pilot sound like a chipmunk. - TripleA
		ApplyPanicFX(PCM, SampleRate);
	}
	if (bRadioFX)
	{
		const float Noise = bPanic ? FMath::Max(RadioNoiseAmount * 2.5f, 0.04f) : RadioNoiseAmount;
		ApplyRadioFX(PCM, SampleRate, RadioHighpassHz, RadioLowpassHz, Noise);
	}

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(SampleRate);
	Wave->NumChannels = Channels;
	Wave->SampleByteSize = 2;     // 16-bit
	Wave->Duration = INDEFINITELY_LOOPING_DURATION;
	Wave->SoundGroup = SOUNDGROUP_Voice;
	Wave->bLooping = false;
	Wave->bProcedural = true;
	Wave->bCanProcessAsync = true;
	Wave->QueueAudio(PCM.GetData(), PCM.Num());

	if (UAudioComponent* Comp = UGameplayStatics::CreateSound2D(GetWorld(), Wave, Volume, /*PitchMultiplier*/ 1.f, /*StartTime*/ 0.f, /*ConcurrencySettings*/ nullptr, /*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ true))
	{
		Comp->Play();
	}

	OnPilotSpoke.Broadcast(Callsign, Text);
	UE_LOG(LogTemp, Display, TEXT("[VoiceOut] %s spoke: \"%s\""), *Callsign.ToString(), *Text);

	// Reserve the half-duplex channel for the duration of the audio plus a small
	// gap. When the timer fires the channel opens for the next transmission. - TripleA
	const int32 NumSamples = PCM.Num() / 2; // 16-bit
	const float Duration = (Channels > 0 && SampleRate > 0)
		? static_cast<float>(NumSamples) / FMath::Max(1, Channels) / static_cast<float>(SampleRate)
		: 1.5f;
	if (UWorld* W = GetWorld())
	{
		TWeakObjectPtr<AClearanceVoiceOutput> Weak(this);
		W->GetTimerManager().SetTimer(ChannelTimer, FTimerDelegate::CreateLambda(
			[Weak]() { if (Weak.IsValid()) { Weak->bChannelBusy = false; Weak->DrainQueue(); } }),
			FMath::Max(0.3f, Duration + 0.3f), false);
	}
	else { bChannelBusy = false; }
}
