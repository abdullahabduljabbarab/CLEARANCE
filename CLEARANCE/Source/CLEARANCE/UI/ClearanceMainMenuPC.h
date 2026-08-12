// Menu-side player controller. Owns the two widget classes the menu
// system spawns: the actual menu, and the custom aircraft-shaped
// software cursor. Both are TSubclassOf so a BP subclass points them at
// WBP_MainMenu / WBP_AircraftCursor without a rebuild. - TripleA

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Templates/SubclassOf.h"
#include "ClearanceMainMenuPC.generated.h"

class UUserWidget;

UCLASS()
class CLEARANCE_API AClearanceMainMenuPC : public APlayerController
{
	GENERATED_BODY()

public:
	AClearanceMainMenuPC();

protected:
	virtual void BeginPlay() override;

	// Set on BP_ClearanceMainMenuGameMode's PC class default. Menu widget
	// is added to the viewport; cursor widget is registered as the
	// software mouse cursor for EMouseCursor::Default.
	UPROPERTY(EditDefaultsOnly, Category = "Menu")
	TSubclassOf<UUserWidget> MenuWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "Menu")
	TSubclassOf<UUserWidget> CursorWidgetClass;

private:
	UPROPERTY()
	TObjectPtr<UUserWidget> MenuWidgetInstance = nullptr;
};
