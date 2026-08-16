#include "UI/ClearanceStripButton.h"

#include "Simulation/ClearanceVROperatorPawn.h"

void UClearanceStripButton::Init(AClearanceVROperatorPawn* InOwner, EStripButtonKind InKind)
{
	OwnerPawn = InOwner;
	Kind      = InKind;
	// Fire on press alone rather than press+release-on-same-button.
	// Fingertip pokes drift enough between trigger down and trigger up
	// that release-based clicks reliably miss. Touchscreen-tap semantics
	// also match real ATC strip conventions - a poke commits. - TripleA
	SetClickMethod(EButtonClickMethod::MouseDown);
	OnClicked.AddDynamic(this, &UClearanceStripButton::HandleClicked);
}

void UClearanceStripButton::HandleClicked()
{
	if (!OwnerPawn.IsValid() || BoundCallsign == NAME_None) { return; }

	switch (Kind)
	{
	case EStripButtonKind::Threat:
		OwnerPawn->StripCycleThreatClass(BoundCallsign);
		break;
	case EStripButtonKind::Interrogate:
		OwnerPawn->StripInterrogate(BoundCallsign);
		break;
	case EStripButtonKind::Ack:
		OwnerPawn->StripAckEmergency(BoundCallsign);
		break;
	}
}
