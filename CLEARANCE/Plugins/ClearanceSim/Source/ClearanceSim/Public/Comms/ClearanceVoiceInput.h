#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InputCoreTypes.h"
#include "AudioCapture.h"
#include "HAL/PlatformProcess.h"
#include "ClearanceVoiceInput.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVoiceTranscript, const FString&, Transcript);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVoiceReadback, const FString&, Readback);

// Push-to-talk voice input: captures the mic while "listening", sends the audio
// to a local whisper.cpp server for transcription, then feeds the recognised
// text into the phraseology parser. Drop one in the level; Neo's UI calls
// StartListening/StopListening and binds OnTranscript/OnReadback. - TripleA
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceVoiceInput : public AActor
{
	GENERATED_BODY()

public:
	AClearanceVoiceInput();

	// Push-to-talk down / up.
	UFUNCTION(BlueprintCallable, Category = "Voice")
	void StartListening();

	UFUNCTION(BlueprintCallable, Category = "Voice")
	void StopListening();

	UFUNCTION(BlueprintCallable, Category = "Voice")
	bool IsListening() const { return bListening; }

	// Fires with the raw recognised text, then with the ATC readback.
	UPROPERTY(BlueprintAssignable, Category = "Voice")
	FOnVoiceTranscript OnTranscript;

	UPROPERTY(BlueprintAssignable, Category = "Voice")
	FOnVoiceReadback OnReadback;

	// Hold this key to transmit (real push-to-talk), release to send.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
	FKey PushToTalkKey = EKeys::LeftAlt;

	// Auto-launch a bundled whisper.cpp server on start (and reuse one if it's
	// already running), so the game is self-contained for the player.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Server")
	bool bAutoLaunchServer = true;

	// Folder holding whisper-server.exe + the model. Empty = <Project>/WhisperServer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Server")
	FString ServerDirectory;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Server")
	FString ServerExeName = TEXT("whisper-server.exe");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Server")
	FString ModelFileName = TEXT("ggml-small.en.bin");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Server")
	int32 ServerPort = 8080;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Built from ServerPort at BeginPlay.
	FString ServerUrl;

	FProcHandle ServerProcHandle;
	bool bLaunchedServer = false;

	void TryLaunchServer();
	bool IsServerResponding(int32 Port) const;

	TUniquePtr<Audio::FAudioCapture> Capture;

	// Accumulated mono samples at the device sample rate (filled on audio thread).
	TArray<float> MonoSamples;
	FCriticalSection SamplesLock;
	int32 DeviceSampleRate = 0;

	bool bListening = false;

	void OnAudioCaptured(const float* InAudio, int32 NumFrames, int32 NumChannels, int32 InSampleRate);
	void SendForRecognition();
	void OnRecognised(const FString& Text);

	static TArray<uint8> EncodeWav16kMono(const TArray<float>& Samples, int32 SrcSampleRate);
};
