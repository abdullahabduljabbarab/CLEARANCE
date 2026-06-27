// Networked instructor/operator PlayerController. Each connected player owns
// their own instance, so it's the right object to host client-to-server RPCs
// for the instructor station injects. The PC just forwards to the authoritative
// AClearanceSimulationController on the server. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceOperatorPC.generated.h"

class UUserWidget;
class UClearanceInstructorPanel;

UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceOperatorPC : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	// Soft class so we don't force-load UMG content from C++. Default points
	// at /Game/UI/WBP_InstructorPanel. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor",
		meta = (MetaClass = "/Script/ClearanceSim.ClearanceInstructorPanel"))
	TSoftClassPtr<UClearanceInstructorPanel> InstructorPanelClass =
		TSoftClassPtr<UClearanceInstructorPanel>(FSoftObjectPath(TEXT("/Game/UI/WBP_InstructorPanel.WBP_InstructorPanel_C")));

private:
	UPROPERTY(Transient)
	TObjectPtr<UClearanceInstructorPanel> InstructorPanel;

public:
	// All RPCs reliable + validated. Each is a thin forwarder - find the local
	// AClearanceSimulationController on the server and call its method. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectEmergency(FName Callsign, EEmergencyType Kind);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectClearEmergency(FName Callsign);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectClassify(FName Callsign, EThreatClass NewClass);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectScramble(FName BanditCallsign);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectSetWind(float DirectionDeg, float SpeedKts);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectSpawn();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectClearTraffic();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectLoadScenario(const FString& ScenarioName);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectStopScenario();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectSetPaused(bool bNewPaused);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectJamming(FName Callsign, bool bOn);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectChaff(FName Callsign);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectSetTimeScale(float Scale);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectResetScenario();

	// Replay control: client UI fires these so the SERVER's controller flips
	// into replay mode and poses the world to the recording. The server then
	// replicates the airspace back so the client sees the scrubbed traffic. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectEnterReplay();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectResumeLive();

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectSeekReplay(float TimeSeconds);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectSetReplayPaused(bool bInPaused);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectSetReplaySpeed(float Multiplier);

	// Instructor clicks "Export AAR" on the panel - server snapshots the
	// session into a Markdown report under <ProjectSavedDir>/Reports/. The
	// report path is announced back to all peers via PushNotification +
	// transcript so the instructor sees where the file landed. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|AAR")
	void Server_InjectExportAAR();

	// Session checkpoint controls - save the live world state under a name,
	// reload it later to reset for a trainee retry. Defence training rigs
	// use this for multi-attempt scenario rehearsals. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|Checkpoints")
	void Server_InjectSaveCheckpoint(FName Name);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|Checkpoints")
	void Server_InjectLoadCheckpoint(FName Name);

	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor|Checkpoints")
	void Server_InjectDeleteCheckpoint(FName Name);

	// Push the operator's full control rotation to the SimController so the
	// instructor PIP can mirror it. Unreliable - we send it every ~30Hz so a
	// dropped packet just leaves the instructor one frame behind. - TripleA
	UFUNCTION(Server, Unreliable, WithValidation)
	void Server_PushOperatorView(FRotator NewRot, FVector NewLoc);

private:
	float ViewPushAccumSec = 0.f;
};
