#include "Aircraft/ClearanceAircraftBehaviour.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Core/ClearanceConstants.h"

namespace
{
	// knots -> nautical miles per second, so Position integrates in nm
	constexpr float KnotsToNmPerSec = 1.f / 3600.f;
}

void UClearanceAircraftBehaviour::Initialise(AClearanceAirspaceManager* InManager, FName InCallsign)
{
	Manager = InManager;
	Callsign = InCallsign;

	if (!Manager)
	{
		return;
	}

	// Stamp the performance envelope onto the state from its category, so the
	// rest of the sim (and the Validator) can read limits straight off the state.
	FAircraftState State = Manager->GetAircraftState(Callsign);
	if (!State.bIsValid)
	{
		return;
	}

	float Ceiling, MinSpeed, MaxSpeed, ClimbRate, BankLimit;
	GetCategoryLimits(State.WakeCategory, Ceiling, MinSpeed, MaxSpeed, ClimbRate, BankLimit);
	State.ServiceCeiling = Ceiling;
	State.MinOperatingSpeed = MinSpeed;
	State.MaxOperatingSpeed = MaxSpeed;
	State.MaxClimbRate = ClimbRate;

	// targets default to current so a freshly-spawned aircraft just holds - TripleA
	State.TargetHeading = State.Heading;
	State.TargetAltitude = State.Altitude;
	State.TargetSpeed = State.Speed;

	Manager->RequestStateUpdate(State);
}

void UClearanceAircraftBehaviour::QueueInstruction(const FAircraftInstruction& Instruction)
{
	Pending.Add(Instruction);
}

bool UClearanceAircraftBehaviour::HasActiveInstruction() const
{
	if (!Manager)
	{
		return false;
	}
	const FAircraftState State = Manager->GetAircraftState(Callsign);
	return State.bIsValid && State.bHasActiveInstruction;
}

void UClearanceAircraftBehaviour::ClearInstructions()
{
	Pending.Reset();
	bGoingAround = false;
}

void UClearanceAircraftBehaviour::ExecuteGoAround()
{
	if (!Manager)
	{
		return;
	}
	FAircraftState State = Manager->GetAircraftState(Callsign);
	if (!State.bIsValid)
	{
		return;
	}

	bGoingAround = true;
	Pending.Reset(); // a go-around overrides whatever was queued
	State.FlightPhase = EFlightPhase::GoAround;
	State.TargetAltitude = FMath::Min(State.Altitude + GoAroundClimbFt, State.ServiceCeiling);
	State.TargetSpeed = FMath::Clamp(State.TargetSpeed, State.MinOperatingSpeed, State.MaxOperatingSpeed);
	Manager->RequestStateUpdate(State);
}

void UClearanceAircraftBehaviour::UpdateMovement(float DeltaTime)
{
	if (!Manager || DeltaTime <= 0.f)
	{
		return;
	}

	FAircraftState State = Manager->GetAircraftState(Callsign);
	if (!State.bIsValid)
	{
		return;
	}

	for (const FAircraftInstruction& Instruction : Pending)
	{
		ApplyInstruction(Instruction, State);
	}
	Pending.Reset();

	const FSectorEnvironment Env = Manager->GetCurrentEnvironment();

	StepHeading(State, DeltaTime);
	StepAltitude(State, DeltaTime);
	StepSpeed(State, DeltaTime);
	StepPosition(State, Env, DeltaTime);

	const bool bHeadingDone = FMath::Abs(FMath::FindDeltaAngleDegrees(State.Heading, State.TargetHeading)) <= HeadingToleranceDeg;
	const bool bAltitudeDone = FMath::Abs(State.Altitude - State.TargetAltitude) <= AltitudeToleranceFt;
	const bool bSpeedDone = FMath::Abs(State.Speed - State.TargetSpeed) <= SpeedToleranceKnots;
	State.bHasActiveInstruction = !(bHeadingDone && bAltitudeDone && bSpeedDone);

	if (bGoingAround && bAltitudeDone)
	{
		bGoingAround = false;
		State.FlightPhase = EFlightPhase::Enroute;
	}

	Manager->RequestStateUpdate(State);
}

void UClearanceAircraftBehaviour::ApplyInstruction(const FAircraftInstruction& Instruction, FAircraftState& State) const
{
	switch (Instruction.Type)
	{
	case EInstructionType::HeadingChange:
		State.TargetHeading = FMath::Fmod(Instruction.TargetValue, 360.f);
		if (State.TargetHeading < 0.f) State.TargetHeading += 360.f;
		break;
	case EInstructionType::AltitudeChange:
		State.TargetAltitude = FMath::Clamp(Instruction.TargetValue, 0.f, State.ServiceCeiling);
		break;
	case EInstructionType::SpeedChange:
		State.TargetSpeed = FMath::Clamp(Instruction.TargetValue, State.MinOperatingSpeed, State.MaxOperatingSpeed);
		break;
	case EInstructionType::ApproachClearance:
		State.FlightPhase = EFlightPhase::Approach;
		break;
	case EInstructionType::TakeoffClearance:
		State.FlightPhase = EFlightPhase::Departing;
		break;
	case EInstructionType::ExitSector:
		State.FlightPhase = EFlightPhase::Exiting;
		break;
	case EInstructionType::Hold:
	default:
		break;
	}
}

void UClearanceAircraftBehaviour::StepHeading(FAircraftState& State, float DeltaTime) const
{
	const float Delta = FMath::FindDeltaAngleDegrees(State.Heading, State.TargetHeading);
	if (FMath::Abs(Delta) <= HeadingToleranceDeg)
	{
		State.Heading = State.TargetHeading;
		State.BankAngle = 0.f;
		return;
	}

	const float MaxStep = TurnRateDegPerSec(State) * DeltaTime;
	const float Step = FMath::Clamp(Delta, -MaxStep, MaxStep);

	State.Heading = FMath::Fmod(State.Heading + Step, 360.f);
	if (State.Heading < 0.f) State.Heading += 360.f;

	// sign of the bank just reflects which way we're turning, for display
	float Ceiling, MinSpeed, MaxSpeed, ClimbRate, BankLimit;
	GetCategoryLimits(State.WakeCategory, Ceiling, MinSpeed, MaxSpeed, ClimbRate, BankLimit);
	State.BankAngle = FMath::Sign(Delta) * BankLimit;
}

void UClearanceAircraftBehaviour::StepAltitude(FAircraftState& State, float DeltaTime) const
{
	const float Delta = State.TargetAltitude - State.Altitude;
	if (FMath::Abs(Delta) <= AltitudeToleranceFt)
	{
		State.Altitude = State.TargetAltitude;
		State.ClimbRate = 0.f;
		return;
	}

	const float RateFtPerMin = DensityAdjustedClimbRate(State); // descents use the same cap here
	const float MaxStep = (RateFtPerMin / 60.f) * DeltaTime;
	const float Step = FMath::Clamp(Delta, -MaxStep, MaxStep);

	State.Altitude = FMath::Clamp(State.Altitude + Step, 0.f, State.ServiceCeiling);
	State.ClimbRate = (DeltaTime > 0.f) ? (Step / DeltaTime) * 60.f : 0.f;
}

void UClearanceAircraftBehaviour::StepSpeed(FAircraftState& State, float DeltaTime) const
{
	const float Delta = State.TargetSpeed - State.Speed;
	if (FMath::Abs(Delta) <= SpeedToleranceKnots)
	{
		State.Speed = State.TargetSpeed;
		return;
	}

	const float MaxStep = AccelerationKnotsPerSec * DeltaTime;
	const float Step = FMath::Clamp(Delta, -MaxStep, MaxStep);
	State.Speed = FMath::Clamp(State.Speed + Step, State.MinOperatingSpeed, State.MaxOperatingSpeed);
}

void UClearanceAircraftBehaviour::StepPosition(FAircraftState& State, const FSectorEnvironment& Env, float DeltaTime) const
{
	// Heading is a compass bearing (0 = North, 90 = East), so X is East and Y is
	// North. Wind blows FROM Env.WindDirection, i.e. toward that bearing + 180. - TripleA
	const float HeadingRad = FMath::DegreesToRadians(State.Heading);
	const FVector2D Air(
		State.Speed * KnotsToNmPerSec * FMath::Sin(HeadingRad),
		State.Speed * KnotsToNmPerSec * FMath::Cos(HeadingRad));

	const float WindToRad = FMath::DegreesToRadians(Env.WindDirection + 180.f);
	const FVector2D Wind(
		Env.WindSpeed * KnotsToNmPerSec * FMath::Sin(WindToRad),
		Env.WindSpeed * KnotsToNmPerSec * FMath::Cos(WindToRad));

	const FVector2D Ground = Air + Wind;

	State.Velocity = FVector(Ground.X, Ground.Y, 0.f);
	State.Position += State.Velocity * DeltaTime;
}

float UClearanceAircraftBehaviour::TurnRateDegPerSec(const FAircraftState& State) const
{
	const float SpeedMps = State.Speed * 0.514444f;
	if (SpeedMps < 1.f)
	{
		return 0.f; // basically stationary, no meaningful turn
	}

	float Ceiling, MinSpeed, MaxSpeed, ClimbRate, BankLimit;
	GetCategoryLimits(State.WakeCategory, Ceiling, MinSpeed, MaxSpeed, ClimbRate, BankLimit);

	const float Omega = (9.81f * FMath::Tan(FMath::DegreesToRadians(BankLimit))) / SpeedMps;
	return FMath::RadiansToDegrees(Omega);
}

float UClearanceAircraftBehaviour::DensityAdjustedClimbRate(const FAircraftState& State) const
{
	const float Base = (State.MaxClimbRate > 0.f) ? State.MaxClimbRate : ClearanceConstants::MaxClimbRateMedium;
	return Base * ISADensityRatio(State.Altitude);
}

float UClearanceAircraftBehaviour::ISADensityRatio(float AltitudeFt)
{
	// Simplified ISA troposphere density ratio; good enough to make climb fall
	// off with altitude without modelling full atmospherics (post-MVP).
	const float Alt = FMath::Max(0.f, AltitudeFt);
	const float Ratio = FMath::Pow(1.f - 6.875e-6f * Alt, 4.2561f);
	return FMath::Clamp(Ratio, 0.1f, 1.f);
}

void UClearanceAircraftBehaviour::GetCategoryLimits(EWakeCategory Category, float& OutCeiling, float& OutMinSpeed, float& OutMaxSpeed, float& OutClimbRate, float& OutBankLimitDeg)
{
	using namespace ClearanceConstants;
	switch (Category)
	{
	case EWakeCategory::Light:
		OutCeiling = ServiceCeilingLight; OutMinSpeed = MinSpeedLight; OutMaxSpeed = MaxSpeedLight;
		OutClimbRate = MaxClimbRateLight; OutBankLimitDeg = BankLimitLight; break;
	case EWakeCategory::Heavy:
		OutCeiling = ServiceCeilingHeavy; OutMinSpeed = MinSpeedHeavy; OutMaxSpeed = MaxSpeedHeavy;
		OutClimbRate = MaxClimbRateHeavy; OutBankLimitDeg = BankLimitHeavy; break;
	case EWakeCategory::Super:
		OutCeiling = ServiceCeilingSuper; OutMinSpeed = MinSpeedSuper; OutMaxSpeed = MaxSpeedSuper;
		OutClimbRate = MaxClimbRateSuper; OutBankLimitDeg = BankLimitSuper; break;
	case EWakeCategory::Medium:
	default:
		OutCeiling = ServiceCeilingMedium; OutMinSpeed = MinSpeedMedium; OutMaxSpeed = MaxSpeedMedium;
		OutClimbRate = MaxClimbRateMedium; OutBankLimitDeg = BankLimitMedium; break;
	}
}
