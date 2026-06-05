#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformProcess.h"
#include "ClearanceVoiceOutput.generated.h"

class USoundWaveProcedural;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPilotSpoke, FName, Callsign, const FString&, Text);

// Pilot/aircraft TTS output. Mirror of UClearanceVoiceInput - the player's
// commands go OUT via voice input, the pilot readback comes back IN via this.
// Hits a local Python TTS server (see TtsServer/tts_server.py) that wraps
// Piper. Voice tag chosen per-airline so Speedbird sounds British, Lufthansa
// German, etc. - TripleA
UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceVoiceOutput : public AActor
{
	GENERATED_BODY()

public:
	AClearanceVoiceOutput();

	// Speak a single line in the chosen voice. Pass an empty Voice to fall back
	// to the en_US default. Returns immediately - audio plays once the HTTP
	// response arrives.
	UFUNCTION(BlueprintCallable, Category = "Voice")
	void Speak(FName Callsign, const FString& Text, const FString& VoiceTag);

	// Same as Speak but pushes the audio through a panic FX chain (pitch up,
	// tremolo, heavy saturation, hotter noise). For final-moments cockpit lines.
	UFUNCTION(BlueprintCallable, Category = "Voice")
	void SpeakPanic(FName Callsign, const FString& Text, const FString& VoiceTag);

	// Assign (or return previously-assigned) voice for this callsign. The first
	// time a callsign asks, picks from the regional pool that fits the airline,
	// skipping any voice already in use by another active aircraft. Same callsign
	// always gets the same voice for its whole life.
	UFUNCTION(BlueprintCallable, Category = "Voice")
	FString PickVoiceForCallsign(FName Callsign);

	// Release a voice slot when an aircraft is gone. Idempotent.
	UFUNCTION(BlueprintCallable, Category = "Voice")
	void ReleaseVoiceForCallsign(FName Callsign);

	// Play a short burst of radio static. Used for 7600 (broken radio) and 7500
	// (pilot keyed mic but couldn't speak) - the audible "something's wrong" cue
	// when there's no voice line to send. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Voice")
	void PlayStatic(float DurationSeconds);

	UPROPERTY(BlueprintAssignable, Category = "Voice")
	FOnPilotSpoke OnPilotSpoke;

	// Auto-launch the bundled tts_server.py on start (and reuse one if it's
	// already responding on the port).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Server")
	bool bAutoLaunchServer = true;

	// Folder holding tts_server.py + voices/. Empty = <Project>/TtsServer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Server")
	FString ServerDirectory;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Server")
	FString ServerScriptName = TEXT("tts_server.py");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Server")
	int32 ServerPort = 8123;

	// Python interpreter to launch the server with. Empty = bare "python".
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Server")
	FString PythonExe = TEXT("python");

	// Master volume scalar applied to all spoken lines.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice")
	float Volume = 1.f;

	// Apply a radio-comms colour to the synthesised voice: bandpass to phone band
	// (~300Hz - 3kHz), light noise floor, soft saturation. Makes the clean TTS
	// sound like it's coming through the operator's headset rather than a studio
	// mic. Turn off for the pristine voice. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Radio FX")
	bool bRadioFX = true;

	// 0 = no static, 0.02-0.05 is a realistic carrier noise floor.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Radio FX", meta = (ClampMin = "0.0", ClampMax = "0.2"))
	float RadioNoiseAmount = 0.015f;

	// Low end of the bandpass (Hz). 300 is the classic narrowband-radio cutoff.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Radio FX")
	float RadioHighpassHz = 300.f;

	// High end of the bandpass (Hz). 3000 = telephone/UHF feel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voice|Radio FX")
	float RadioLowpassHz = 3000.f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FString ServerUrl;
	FProcHandle ServerProcHandle;
	bool bLaunchedServer = false;

	// Per-aircraft voice assignment so the same callsign always sounds the same
	// AND two active aircraft never share a voice. - TripleA
	TMap<FName, FString> CallsignToVoice;
	TSet<FString> VoicesInUse;

	// Real ATC frequencies are half-duplex - only one transmission at a time. All
	// Speak / SpeakPanic / PlayStatic calls enqueue here; the channel processes
	// one request at a time, plays it, then takes the next. - TripleA
	struct FPendingSpeak
	{
		FName Callsign;
		FString Text;
		FString VoiceTag;
		bool bPanic = false;
		bool bStatic = false;
		float StaticDuration = 0.f;
	};
	TArray<FPendingSpeak> SpeechQueue;
	bool bChannelBusy = false;
	FTimerHandle ChannelTimer;

	void EnqueueSpeech(FName Callsign, const FString& Text, const FString& VoiceTag, bool bPanic, bool bStatic, float StaticDuration);
	void DrainQueue();

	void TryLaunchServer();
	bool IsServerResponding() const;

	// Parse a 16-bit PCM WAV blob, return (sample rate, channels, PCM bytes).
	static bool ParseWav(const TArray<uint8>& Wav, int32& OutSampleRate, int32& OutChannels, TArray<uint8>& OutPCM);

	void SpeakInternal(FName Callsign, const FString& Text, const FString& VoiceTag, bool bPanic);
	void PlayStaticImmediate(float DurationSeconds);
	void PlayPCM(FName Callsign, const FString& Text, int32 SampleRate, int32 Channels, TArray<uint8>&& PCM, bool bPanic);
};
