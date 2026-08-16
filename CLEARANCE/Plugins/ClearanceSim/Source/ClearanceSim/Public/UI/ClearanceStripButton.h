// UButton subclass used by the VR operator strip monitor. Carries the
// aircraft callsign + which strip action (Threat / Interrogate / Ack)
// the button represents, and wires its own OnClicked handler to the
// owning VR pawn's Strip* dispatch helpers. Existing purely so
// fingertip-touch clicks know which aircraft to act on - a plain
// UButton has no payload channel to attach that context. - TripleA

#pragma once

#include "CoreMinimal.h"
#include "Components/Button.h"
#include "ClearanceStripButton.generated.h"

class AClearanceVROperatorPawn;

UENUM()
enum class EStripButtonKind : uint8
{
	Threat,
	Interrogate,
	Ack
};

UCLASS()
class CLEARANCESIM_API UClearanceStripButton : public UButton
{
	GENERATED_BODY()

public:
	// Callsign the strip row this button belongs to is currently bound
	// to. Written by AClearanceVROperatorPawn::UpdateStripRow every tick
	// so pool row reuse still routes clicks to the correct aircraft. - TripleA
	UPROPERTY(Transient)
	FName BoundCallsign;

	UPROPERTY(Transient)
	EStripButtonKind Kind = EStripButtonKind::Threat;

	UPROPERTY()
	TWeakObjectPtr<AClearanceVROperatorPawn> OwnerPawn;

	// Called once at row build time - captures the pawn ref, the button
	// kind, and hooks OnClicked to HandleClicked below. - TripleA
	void Init(AClearanceVROperatorPawn* InOwner, EStripButtonKind InKind);

private:
	UFUNCTION()
	void HandleClicked();
};
