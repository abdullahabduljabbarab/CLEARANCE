#include "ClearanceMainMenuPC.h"

#include "Blueprint/UserWidget.h"

AClearanceMainMenuPC::AClearanceMainMenuPC()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void AClearanceMainMenuPC::BeginPlay()
{
	Super::BeginPlay();

	// UI-only input: the menu owns every click, no game input pipes through
	// to the level (which has nothing to receive it anyway).
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	bShowMouseCursor = true;

	if (MenuWidgetClass)
	{
		MenuWidgetInstance = CreateWidget<UUserWidget>(this, MenuWidgetClass);
		if (MenuWidgetInstance)
		{
			MenuWidgetInstance->AddToViewport();
		}
	}

	// Custom software cursor. Only takes effect while EMouseCursor::Default
	// is the active cursor - anything that sets a different cursor
	// (crosshair, hand) reverts to the OS default until reset. Fine for
	// a menu that never changes cursor mode. - TripleA
	if (CursorWidgetClass)
	{
		UUserWidget* CursorWidget = CreateWidget<UUserWidget>(this, CursorWidgetClass);
		if (CursorWidget)
		{
			SetMouseCursorWidget(EMouseCursor::Default, CursorWidget);
		}
	}
}
