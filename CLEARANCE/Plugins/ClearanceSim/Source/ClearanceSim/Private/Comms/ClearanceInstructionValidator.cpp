#include "Comms/ClearanceInstructionValidator.h"
#include "Core/ClearanceConstants.h"

EInstructionResult UClearanceInstructionValidator::ValidateInstruction(const FAircraftInstruction& Instruction, const FAircraftState& State) const
{
	if (!State.bIsValid)
	{
		return EInstructionResult::Rejected_InvalidCallsign;
	}

	// System-issued go-arounds bypass feasibility - safety overrides the envelope.
	if (Instruction.bIsGoAround)
	{
		return EInstructionResult::Accepted;
	}

	if (State.FlightPhase == EFlightPhase::Exiting)
	{
		return EInstructionResult::Rejected_AircraftExited;
	}

	// Limits come from the category, not the (possibly stale) state fields, so the
	// Validator is the single authority on what's physically allowed. - TripleA
	const ClearanceConstants::FCategoryPerformance Perf = ClearanceConstants::GetCategoryPerformance(State.WakeCategory);

	switch (Instruction.Type)
	{
	case EInstructionType::AltitudeChange:
		if (!FMath::IsFinite(Instruction.TargetValue) || Instruction.TargetValue < 0.f || Instruction.TargetValue > Perf.ServiceCeilingFt)
		{
			return EInstructionResult::Rejected_PhysicallyImpossible;
		}
		break;

	case EInstructionType::SpeedChange:
		if (!FMath::IsFinite(Instruction.TargetValue) || Instruction.TargetValue < Perf.MinOperatingSpeedKts || Instruction.TargetValue > Perf.MaxOperatingSpeedKts)
		{
			return EInstructionResult::Rejected_PhysicallyImpossible;
		}
		break;

	case EInstructionType::HeadingChange:
		// Any heading is reachable; only a garbage value is impossible.
		if (!FMath::IsFinite(Instruction.TargetValue))
		{
			return EInstructionResult::Rejected_PhysicallyImpossible;
		}
		break;

	case EInstructionType::Hold:
	case EInstructionType::ApproachClearance:
	case EInstructionType::TakeoffClearance:
	case EInstructionType::ExitSector:
	default:
		// Phase-change clearances carry no envelope to check here. Wake-separation
		// on approach is the Conflict Detector's job (it sees the other traffic).
		break;
	}

	return EInstructionResult::Accepted;
}
