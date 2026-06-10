// Networked instructor/operator PlayerController. Each connected player owns
// their own instance, so it's the right object to host client-to-server RPCs
// for the instructor station injects. The PC just forwards to the authoritative
// AClearanceSimulationController on the server. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/CLEARANCETypes.h"
#include "ClearanceOperatorPC.generated.h"

UCLASS(Blueprintable)
class CLEARANCESIM_API AClearanceOperatorPC : public APlayerController
{
	GENERATED_BODY()

public:
	// All RPCs reliable + validated. Each is a thin forwarder - find the local
	// AClearanceSimulationController on the server and call its method. - TripleA
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "Instructor")
	void Server_InjectEmergency(FName Callsign, EEmergencyType Kind);

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
};
