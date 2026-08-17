#include "Comms/ClearanceVoiceInput.h"
#include "Comms/ClearancePhraseology.h"
#include "Simulation/ClearanceSimulationController.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "EngineUtils.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Components/InputComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

AClearanceVoiceInput::AClearanceVoiceInput()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AClearanceVoiceInput::BeginPlay()
{
	Super::BeginPlay();

	ServerUrl = FString::Printf(TEXT("http://127.0.0.1:%d/inference"), ServerPort);

	if (bAutoLaunchServer)
	{
		TryLaunchServer();
	}

	// Wire push-to-talk: hold the key to capture, release to send.
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		EnableInput(PC);
		if (InputComponent)
		{
			InputComponent->BindKey(PushToTalkKey, IE_Pressed, this, &AClearanceVoiceInput::StartListening);
			InputComponent->BindKey(PushToTalkKey, IE_Released, this, &AClearanceVoiceInput::StopListening);
			UE_LOG(LogTemp, Display, TEXT("[Voice] push-to-talk bound to %s"), *PushToTalkKey.ToString());
		}
	}
}

void AClearanceVoiceInput::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Capture.IsValid())
	{
		Capture->StopStream();
		Capture->CloseStream();
		Capture.Reset();
	}

	// Only shut down the server if WE launched it (leave a pre-existing dev server alone).
	if (bLaunchedServer && ServerProcHandle.IsValid())
	{
		FPlatformProcess::TerminateProc(ServerProcHandle, true);
		FPlatformProcess::CloseProc(ServerProcHandle);
		bLaunchedServer = false;
	}

	Super::EndPlay(EndPlayReason);
}

void AClearanceVoiceInput::TryLaunchServer()
{
	if (IsServerResponding(ServerPort))
	{
		UE_LOG(LogTemp, Display, TEXT("[Voice] whisper server already running on port %d - reusing it"), ServerPort);
		return;
	}

	FString Dir = ServerDirectory.IsEmpty()
		? FPaths::Combine(FPaths::ProjectDir(), TEXT("WhisperServer"))
		: ServerDirectory;
	// Absolute - the server runs with a different working dir and must be able to
	// find the model by full path, or it exits silently and nothing listens. - TripleA
	Dir = FPaths::ConvertRelativePathToFull(Dir);
	const FString Exe = FPaths::Combine(Dir, ServerExeName);
	const FString Model = FPaths::Combine(Dir, ModelFileName);

	if (!FPaths::FileExists(Exe) || !FPaths::FileExists(Model))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Voice] server/model not found in '%s' - voice won't work until they're there (or a server is already running)"), *Dir);
		return;
	}

	const FString Args = FString::Printf(TEXT("-m \"%s\" --host 127.0.0.1 --port %d"), *Model, ServerPort);
	ServerProcHandle = FPlatformProcess::CreateProc(*Exe, *Args, /*bLaunchDetached*/ false, /*bLaunchHidden*/ true, /*bLaunchReallyHidden*/ true, nullptr, 0, *Dir, nullptr);

	if (ServerProcHandle.IsValid())
	{
		bLaunchedServer = true;
		UE_LOG(LogTemp, Display, TEXT("[Voice] launched whisper server from '%s' (loading model, ready in a few seconds)"), *Dir);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Voice] failed to launch whisper server"));
	}
}

bool AClearanceVoiceInput::IsServerResponding(int32 Port) const
{
	ISocketSubsystem* SocketSub = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSub)
	{
		return false;
	}

	bool bAddrValid = false;
	const TSharedRef<FInternetAddr> Addr = SocketSub->CreateInternetAddr();
	Addr->SetIp(TEXT("127.0.0.1"), bAddrValid);
	Addr->SetPort(Port);
	if (!bAddrValid)
	{
		return false;
	}

	FSocket* Socket = SocketSub->CreateSocket(NAME_Stream, TEXT("WhisperPortCheck"), false);
	if (!Socket)
	{
		return false;
	}

	Socket->SetNonBlocking(true);
	Socket->Connect(*Addr);
	const bool bConnected = Socket->Wait(ESocketWaitConditions::WaitForWrite, FTimespan::FromMilliseconds(300));

	Socket->Close();
	SocketSub->DestroySocket(Socket);
	return bConnected;
}

void AClearanceVoiceInput::StartListening()
{
	if (bListening)
	{
		return;
	}

	{
		FScopeLock Lock(&SamplesLock);
		MonoSamples.Reset();
		DeviceSampleRate = 0;
	}

	Capture = MakeUnique<Audio::FAudioCapture>();

	Audio::FAudioCaptureDeviceParams Params; // default input device
	Audio::FOnAudioCaptureFunction OnCapture = [this](const void* InAudio, int32 NumFrames, int32 NumChannels, int32 InSampleRate, double /*StreamTime*/, bool /*bOverFlow*/)
	{
		OnAudioCaptured(static_cast<const float*>(InAudio), NumFrames, NumChannels, InSampleRate);
	};

	if (!Capture->OpenAudioCaptureStream(Params, MoveTemp(OnCapture), 1024))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Voice] Could not open mic capture stream"));
		Capture.Reset();
		return;
	}

	Capture->StartStream();
	bListening = true;
}

void AClearanceVoiceInput::StopListening()
{
	if (!bListening)
	{
		return;
	}
	bListening = false;

	if (Capture.IsValid())
	{
		Capture->StopStream();
		Capture->CloseStream();
		Capture.Reset();
	}

	SendForRecognition();
}

void AClearanceVoiceInput::OnAudioCaptured(const float* InAudio, int32 NumFrames, int32 NumChannels, int32 InSampleRate)
{
	if (!InAudio || NumFrames <= 0 || NumChannels <= 0)
	{
		return;
	}

	FScopeLock Lock(&SamplesLock);
	DeviceSampleRate = InSampleRate;
	MonoSamples.Reserve(MonoSamples.Num() + NumFrames);
	for (int32 Frame = 0; Frame < NumFrames; ++Frame)
	{
		float Sum = 0.f;
		for (int32 Ch = 0; Ch < NumChannels; ++Ch)
		{
			Sum += InAudio[Frame * NumChannels + Ch];
		}
		MonoSamples.Add(Sum / NumChannels);
	}
}

void AClearanceVoiceInput::SendForRecognition()
{
	TArray<float> Samples;
	int32 SrcRate;
	{
		FScopeLock Lock(&SamplesLock);
		Samples = MoveTemp(MonoSamples);
		MonoSamples.Reset();
		SrcRate = DeviceSampleRate;
	}

	if (Samples.Num() < 1600 || SrcRate <= 0) // < ~0.1s of audio: ignore
	{
		return;
	}

	const TArray<uint8> Wav = EncodeWav16kMono(Samples, SrcRate);

	// multipart/form-data: the WAV file + a request for plain-text output
	const FString Boundary = TEXT("----ClearanceBoundary") + FGuid::NewGuid().ToString();
	TArray<uint8> Body;
	auto AppendUtf8 = [&Body](const FString& Str)
	{
		FTCHARToUTF8 Conv(*Str);
		Body.Append(reinterpret_cast<const uint8*>(Conv.Get()), Conv.Length());
	};

	AppendUtf8(TEXT("--") + Boundary + TEXT("\r\n"));
	AppendUtf8(TEXT("Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"));
	AppendUtf8(TEXT("Content-Type: audio/wav\r\n\r\n"));
	Body.Append(Wav);
	AppendUtf8(TEXT("\r\n"));
	AppendUtf8(TEXT("--") + Boundary + TEXT("\r\n"));
	AppendUtf8(TEXT("Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"));
	AppendUtf8(TEXT("text\r\n"));
	AppendUtf8(TEXT("--") + Boundary + TEXT("--\r\n"));

	const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(ServerUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("multipart/form-data; boundary=") + Boundary);
	Request->SetContent(Body);

	TWeakObjectPtr<AClearanceVoiceInput> WeakThis(this);
	Request->OnProcessRequestComplete().BindLambda([WeakThis](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk)
	{
		if (!WeakThis.IsValid()) { return; }
		if (bOk && Response.IsValid())
		{
			WeakThis->OnRecognised(Response->GetContentAsString());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Voice] whisper server request failed (is it running on the URL?)"));
		}
	});
	Request->ProcessRequest();
}

void AClearanceVoiceInput::OnRecognised(const FString& Text)
{
	FString Clean = Text.TrimStartAndEnd();
	if (Clean.IsEmpty())
	{
		return;
	}

	OnTranscript.Broadcast(Clean);

	for (TActorIterator<AClearanceSimulationController> It(GetWorld()); It; ++It)
	{
		const FString Readback = UClearancePhraseology::Interpret(*It, Clean);
		OnReadback.Broadcast(Readback);
		return;
	}
}

// Console push-to-talk for testing before a real UI exists:
//   clearance.listen.start  (hold) -> speak -> clearance.listen.stop
static AClearanceVoiceInput* GetOrSpawnVoiceInput(UWorld* World)
{
	if (!World) { return nullptr; }
	for (TActorIterator<AClearanceVoiceInput> It(World); It; ++It) { return *It; }
	return World->SpawnActor<AClearanceVoiceInput>();
}

static FAutoConsoleCommandWithWorld GClearanceListenStart(
	TEXT("clearance.listen.start"),
	TEXT("Start capturing the mic (speak, then run clearance.listen.stop)"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (AClearanceVoiceInput* V = GetOrSpawnVoiceInput(World))
		{
			V->StartListening();
		}
	}));

static FAutoConsoleCommandWithWorld GClearanceListenStop(
	TEXT("clearance.listen.stop"),
	TEXT("Stop the mic and send the audio for recognition"),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		if (!World) { return; }
		for (TActorIterator<AClearanceVoiceInput> It(World); It; ++It) { It->StopListening(); return; }
	}));

TArray<uint8> AClearanceVoiceInput::EncodeWav16kMono(const TArray<float>& Samples, int32 SrcSampleRate)
{
	constexpr int32 OutRate = 16000;

	// Resample to 16 kHz (linear interpolation - fine for speech).
	TArray<float> Resampled;
	if (SrcSampleRate == OutRate)
	{
		Resampled = Samples;
	}
	else
	{
		const int64 OutCount = (int64)Samples.Num() * OutRate / SrcSampleRate;
		Resampled.Reserve(OutCount);
		for (int64 i = 0; i < OutCount; ++i)
		{
			const double SrcPos = (double)i * SrcSampleRate / OutRate;
			const int32 i0 = (int32)SrcPos;
			const int32 i1 = FMath::Min(i0 + 1, Samples.Num() - 1);
			const float Frac = (float)(SrcPos - i0);
			Resampled.Add(FMath::Lerp(Samples[i0], Samples[i1], Frac));
		}
	}

	// 16-bit PCM
	const int32 NumSamples = Resampled.Num();
	const int32 DataSize = NumSamples * sizeof(int16);

	TArray<uint8> Wav;
	Wav.Reserve(44 + DataSize);

	auto PushU32 = [&Wav](uint32 V) { Wav.Append(reinterpret_cast<const uint8*>(&V), 4); };
	auto PushU16 = [&Wav](uint16 V) { Wav.Append(reinterpret_cast<const uint8*>(&V), 2); };
	auto PushTag = [&Wav](const char* T) { Wav.Append(reinterpret_cast<const uint8*>(T), 4); };

	PushTag("RIFF");  PushU32(36 + DataSize);  PushTag("WAVE");
	PushTag("fmt ");  PushU32(16);  PushU16(1); PushU16(1); // PCM, mono
	PushU32(OutRate); PushU32(OutRate * 2);     PushU16(2); PushU16(16); // byterate, blockalign, bits
	PushTag("data");  PushU32(DataSize);

	for (float S : Resampled)
	{
		const int16 V = (int16)(FMath::Clamp(S, -1.f, 1.f) * 32767.f);
		PushU16((uint16)V);
	}

	return Wav;
}
