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
};
