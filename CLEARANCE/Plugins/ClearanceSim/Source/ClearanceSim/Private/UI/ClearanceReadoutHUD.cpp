#include "UI/ClearanceReadoutHUD.h"
#include "Simulation/ClearanceSimulationController.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"

void AClearanceReadoutHUD::DrawHUD()
{
    Super::DrawHUD();

    if (!Canvas) { return; }

    AClearanceSimulationController* Ctrl = FindController();
    if (!Ctrl) { return; }

    UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
    if (!Font) { return; }

    const float X = 16.f;
    float Y = 16.f;
    const FLinearColor DiagCol(0.86f, 0.86f, 1.0f);
    const FLinearColor MainCol = FLinearColor::White;

    if (!Ctrl->CurrentDiagLine.IsEmpty())
    {
        FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Ctrl->CurrentDiagLine), Font, DiagCol);
        Item.EnableShadow(FLinearColor::Black);
        Canvas->DrawItem(Item);
        Y += 18.f;
    }

    if (!Ctrl->CurrentReadout.IsEmpty())
    {
        // Multi-line readout. Canvas auto-wraps on \n via FCanvasTextItem. - TripleA
        FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Ctrl->CurrentReadout), Font, MainCol);
        Item.EnableShadow(FLinearColor::Black);
        Canvas->DrawItem(Item);
    }

    // Notification stack on the right side. Newest at top of the strip; each
    // entry fades over its LifetimeSec then drops off. Server pushes via the
    // replicated RepNotifications, we just paint. - TripleA
    const float NowServer = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
    float NotifY = Canvas->ClipY * 0.3f;
    const float NotifX = Canvas->ClipX - 380.f;
    for (int32 i = Ctrl->RepNotifications.Num() - 1; i >= 0; --i)
    {
        const FClearanceNotification& N = Ctrl->RepNotifications[i];
        const float Age = NowServer - N.ServerTimeAdded;
        if (Age < 0.f || Age > N.LifetimeSec) { continue; }
        const float Fade = 1.f - FMath::Clamp(Age / N.LifetimeSec, 0.f, 1.f);
        FLinearColor Col = FLinearColor(N.Colour);
        Col.A = Fade;
        FCanvasTextItem Item(FVector2D(NotifX, NotifY), FText::FromString(N.Text), Font, Col);
        Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, Fade * 0.8f));
        Canvas->DrawItem(Item);
        NotifY += 18.f;
        if (NotifY > Canvas->ClipY * 0.85f) { break; }
    }

    // Role badge top-right: SERVER (listen-host) vs INSTRUCTOR (client peer).
    // Helpful in PIE multi-window when both desktops look identical. - TripleA
    {
        const APlayerController* PC = GetOwningPlayerController();
        const bool bIsServer = PC && PC->GetLocalRole() == ROLE_Authority;
        const FString BadgeText = bIsServer ? TEXT("SERVER") : TEXT("INSTRUCTOR");
        const FLinearColor BadgeCol = bIsServer
            ? FLinearColor(0.3f, 1.0f, 0.5f, 0.9f)   // bright green
            : FLinearColor(0.6f, 0.85f, 1.0f, 0.9f); // cool blue

        const float BadgeX = Canvas->ClipX - 220.f;
        const float BadgeY = 16.f;
        FCanvasTextItem Item(FVector2D(BadgeX, BadgeY), FText::FromString(BadgeText), Font, BadgeCol);
        Item.Scale = FVector2D(1.6f, 1.6f);
        Item.EnableShadow(FLinearColor::Black);
        Canvas->DrawItem(Item);

        // Connection status underneath the badge.
        FString ConnText = TEXT("STANDALONE");
        FLinearColor ConnCol(0.8f, 0.8f, 0.8f, 0.9f);
        if (PC)
        {
            const ENetMode NM = PC->GetNetMode();
            if (NM == NM_ListenServer)
            {
                ConnText = TEXT("LISTEN HOST");
                ConnCol  = FLinearColor(0.4f, 1.f, 0.5f, 0.9f);
            }
            else if (NM == NM_Client)
            {
                ConnText = PC->GetNetConnection() ? TEXT("CONNECTED") : TEXT("DISCONNECTED");
                ConnCol  = PC->GetNetConnection() ? FLinearColor(0.5f, 1.f, 0.6f, 0.9f) : FLinearColor(1.f, 0.4f, 0.4f, 0.9f);
            }
        }
        FCanvasTextItem Conn(FVector2D(BadgeX, BadgeY + 24.f), FText::FromString(ConnText), Font, ConnCol);
        Conn.EnableShadow(FLinearColor::Black);
        Canvas->DrawItem(Conn);
    }
}

AClearanceSimulationController* AClearanceReadoutHUD::FindController()
{
    if (CachedController && IsValid(CachedController)) { return CachedController; }

    for (TActorIterator<AClearanceSimulationController> It(GetWorld()); It; ++It)
    {
        CachedController = *It;
        return CachedController;
    }
    return nullptr;
}
