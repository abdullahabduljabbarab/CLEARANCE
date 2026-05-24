#include "Comms/ClearanceCommsRouter.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Comms/ClearanceInstructionValidator.h"
#include "Aircraft/ClearanceAircraftBehaviour.h"

void UClearanceCommsRouter::SetReferences(AClearanceAirspaceManager* InManager, UClearanceInstructionValidator* InValidator)
{
	Manager = InManager;
	Validator = InValidator;
}

void UClearanceCommsRouter::RegisterBehaviour(FName Callsign, UClearanceAircraftBehaviour* Behaviour)
{
	if (Behaviour)
	{
		Behaviours.Add(Callsign, Behaviour);
	}
}

void UClearanceCommsRouter::UnregisterBehaviour(FName Callsign)
{
	Behaviours.Remove(Callsign);
	LastInstructionTime.Remove(Callsign);
}

EInstructionResult UClearanceCommsRouter::IssueInstruction(const FAircraftInstruction& Instruction)
{
	const FName Callsign = Instruction.TargetCallsign;

	// Bad wiring or unknown aircraft both fail as "invalid callsign" - the player
	// asked for someone who isn't (validly) in the sector. - TripleA
	if (!Manager || !Validator || !Manager->IsCallsignRegistered(Callsign))
	{
		OnInstructionResult.Broadcast(Callsign, EInstructionResult::Rejected_InvalidCallsign);
		return EInstructionResult::Rejected_InvalidCallsign;
	}

	const FAircraftState State = Manager->GetAircraftState(Callsign);
	EInstructionResult Result = Validator->ValidateInstruction(Instruction, State);

	if (Result == EInstructionResult::Accepted)
	{
		if (UClearanceAircraftBehaviour* Behaviour = Behaviours.FindRef(Callsign))
		{
			Behaviour->QueueInstruction(Instruction);
			if (Manager->GetWorld())
			{
				LastInstructionTime.Add(Callsign, Manager->GetWorld()->GetTimeSeconds());
			}
		}
		else
		{
			// registered but no mover wired up - can't actually carry it out
			Result = EInstructionResult::Rejected_InvalidCallsign;
		}
	}

	OnInstructionResult.Broadcast(Callsign, Result);
	return Result;
}

void UClearanceCommsRouter::RouteGoAround(FName Callsign)
{
	if (UClearanceAircraftBehaviour* Behaviour = Behaviours.FindRef(Callsign))
	{
		Behaviour->ExecuteGoAround();
		OnAdvisoryWarning.Broadcast(FString::Printf(TEXT("Go-around: %s"), *Callsign.ToString()), EAlertLevel::Warning);
	}
}

void UClearanceCommsRouter::ReceiveAdvisory(const FString& Message, EAlertLevel Level)
{
	OnAdvisoryWarning.Broadcast(Message, Level);
}
