#include "Aircraft/ClearanceAircraftBehaviour.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Core/ClearanceConstants.h"
#include "Engine/Engine.h"

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

	// Stamp the performance envelope onto the state from its category, so anything
	// reading the state sees the right limits without a second lookup.
	FAircraftState State = Manager->GetAircraftState(Callsign);
	if (!State.bIsValid)
	{
		return;
	}

	const ClearanceConstants::FCategoryPerformance Perf = ClearanceConstants::GetCategoryPerformance(State.WakeCategory);
	State.ServiceCeiling = Perf.ServiceCeilingFt;
	State.MinOperatingSpeed = Perf.MinOperatingSpeedKts;
	State.MaxOperatingSpeed = Perf.MaxOperatingSpeedKts;
	State.MaxClimbRate = Perf.MaxClimbRateFtMin;

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
	ActiveTurnDirection = 0;
	bExpediting = false;
	bApproachCaptured = false;
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

	// Once wheels are down on the runway, fly the ground roll instead of the airborne
	// model: hold the centreline, stay on the deck, and brake to a stop. Braking goes
	// below flying minimums, so the speed is set directly here, not via StepSpeed. The
	// controller pulls it off once it's slowed to taxi speed. - TripleA
	const bool bGroundRoll = (State.FlightPhase == EFlightPhase::Landing) && (State.Altitude <= 100.f);

	if (bGroundRoll)
	{
		const float FAC = (Env.ActiveRunwayHeading >= 0.f) ? Env.ActiveRunwayHeading : State.Heading;
		State.TargetHeading = FAC;
		State.TargetAltitude = 0.f;
		State.Altitude = 0.f;
		State.ClimbRate = 0.f;

		// Wheel braking to a stop, at a rate set by the aircraft's category - a Heavy
		// rolls out far longer than a Light. - TripleA
		const float BrakeKtsPerSec = ClearanceConstants::GetCategoryPerformance(State.WakeCategory).GroundBrakingKtsPerSec;
		State.TargetSpeed = 0.f;
		State.Speed = FMath::Max(0.f, State.Speed - BrakeKtsPerSec * DeltaTime);

		StepHeading(State, DeltaTime); // keep it tracking straight down the runway

		// Roll forward along the heading only - no wind drift on the ground, otherwise
		// a stopped aircraft would slide sideways with the wind.
		const float HeadingRad = FMath::DegreesToRadians(State.Heading);
		const FVector2D Roll(
			State.Speed * KnotsToNmPerSec * FMath::Sin(HeadingRad),
			State.Speed * KnotsToNmPerSec * FMath::Cos(HeadingRad));
		State.Velocity = FVector(Roll.X, Roll.Y, 0.f);
		State.Position += State.Velocity * DeltaTime;
	}
	else
	{
		// Cleared for approach, but the ILS only TAKES OVER once the aircraft is
		// established on the localiser - until then it flies the controller's vectors,
		// so positioning it for the intercept is the player's job. - TripleA
		if (State.FlightPhase == EFlightPhase::Approach || State.FlightPhase == EFlightPhase::Landing)
		{
			if (!bApproachCaptured && IsEstablishedOnApproach(State, Env))
			{
				bApproachCaptured = true;
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Green, FString::Printf(TEXT("%s established on the approach"), *State.Callsign.ToString()));
				}
			}

			// Go around ONLY if it never got established and has run past the threshold -
			// never lined up. Once established it commits to the landing: it flies the
			// glideslope down and touches down (long, if it came in high) rather than
			// bailing out at the last moment. - TripleA
			if (!bApproachCaptured && HasMissedApproach(State, Env))
			{
				bGoingAround = true;
				State.FlightPhase = EFlightPhase::GoAround;
				State.TargetAltitude = FMath::Min(State.Altitude + GoAroundClimbFt, State.ServiceCeiling);
				State.TargetSpeed = FMath::Clamp(State.TargetSpeed, State.MinOperatingSpeed, State.MaxOperatingSpeed);
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, FString::Printf(TEXT("%s unable - not established, going around, request vectors"), *State.Callsign.ToString()));
				}
			}
			else if (bApproachCaptured)
			{
				RunApproachGuidance(State, Env);
			}
		}

		StepHeading(State, DeltaTime);
		StepAltitude(State, DeltaTime);
		StepSpeed(State, DeltaTime);
		StepPosition(State, Env, DeltaTime);
	}

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

void UClearanceAircraftBehaviour::ApplyInstruction(const FAircraftInstruction& Instruction, FAircraftState& State)
{
	switch (Instruction.Type)
	{
	case EInstructionType::HeadingChange:
		State.TargetHeading = FMath::Fmod(Instruction.TargetValue, 360.f);
		if (State.TargetHeading < 0.f) State.TargetHeading += 360.f;
		ActiveTurnDirection = Instruction.TurnDirection; // -1/0/+1
		break;
	case EInstructionType::AltitudeChange:
		State.TargetAltitude = FMath::Clamp(Instruction.TargetValue, 0.f, State.ServiceCeiling);
		bExpediting = Instruction.bExpedite;
		break;
	case EInstructionType::SpeedChange:
		State.TargetSpeed = FMath::Clamp(Instruction.TargetValue, State.MinOperatingSpeed, State.MaxOperatingSpeed);
		break;
	case EInstructionType::ApproachClearance:
		State.FlightPhase = EFlightPhase::Approach;
		bApproachCaptured = false; // must re-establish on the localiser
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

void UClearanceAircraftBehaviour::StepHeading(FAircraftState& State, float DeltaTime)
{
	const float Delta = FMath::FindDeltaAngleDegrees(State.Heading, State.TargetHeading);
	if (FMath::Abs(Delta) <= HeadingToleranceDeg)
	{
		State.Heading = State.TargetHeading;
		State.BankAngle = 0.f;
		ActiveTurnDirection = 0;
		return;
	}

	const float MaxStep = TurnRateDegPerSec(State) * DeltaTime;
	const float BankLimit = ClearanceConstants::GetCategoryPerformance(State.WakeCategory).BankLimitDeg;

	if (ActiveTurnDirection != 0)
	{
		// Forced turn direction (the controller said "turn left/right"): go that
		// way even if it's the long way round, until we reach the target. - TripleA
		const float Remaining = (ActiveTurnDirection > 0)
			? FMath::Fmod(State.TargetHeading - State.Heading + 360.f, 360.f)  // clockwise / right
			: FMath::Fmod(State.Heading - State.TargetHeading + 360.f, 360.f); // anticlockwise / left

		if (Remaining <= MaxStep)
		{
			State.Heading = State.TargetHeading;
			State.BankAngle = 0.f;
			ActiveTurnDirection = 0;
			return;
		}

		State.Heading = FMath::Fmod(State.Heading + MaxStep * ActiveTurnDirection + 360.f, 360.f);
		State.BankAngle = ActiveTurnDirection * BankLimit;
		return;
	}

	const float Step = FMath::Clamp(Delta, -MaxStep, MaxStep);
	State.Heading = FMath::Fmod(State.Heading + Step, 360.f);
	if (State.Heading < 0.f) State.Heading += 360.f;
	State.BankAngle = FMath::Sign(Delta) * BankLimit; // sign just reflects turn direction, for display
}

void UClearanceAircraftBehaviour::StepAltitude(FAircraftState& State, float DeltaTime)
{
	const float Delta = State.TargetAltitude - State.Altitude;
	if (FMath::Abs(Delta) <= AltitudeToleranceFt)
	{
		// Within tolerance we snap to the target - but when the target is itself moving
		// (a glideslope dropping under us), zeroing ClimbRate is wrong: it hides the real
		// descent and breaks anything reading vertical speed. Report the closing rate. - TripleA
		State.ClimbRate = (DeltaTime > 0.f) ? (Delta / DeltaTime) * 60.f : 0.f;
		State.Altitude = State.TargetAltitude;
		bExpediting = false;
		return;
	}

	// Climbs are thrust-limited (and fall off with altitude); descents are
	// airframe/comfort limited, so they use a separate flat rate. - TripleA
	float RateFtPerMin;
	if (Delta > 0.f)
	{
		RateFtPerMin = DensityAdjustedClimbRate(State);
	}
	else
	{
		RateFtPerMin = ClearanceConstants::GetCategoryPerformance(State.WakeCategory).MaxDescentRateFtMin;
	}

	if (bExpediting)
	{
		RateFtPerMin *= 1.5f; // "expedite" - push the rate up
	}

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

	// Aircraft slow down more reluctantly than they speed up (less drag than thrust).
	const ClearanceConstants::FCategoryPerformance Perf = ClearanceConstants::GetCategoryPerformance(State.WakeCategory);
	const float RateKtsPerSec = (Delta > 0.f) ? Perf.AccelKtsPerSec : Perf.DecelKtsPerSec;

	const float MaxStep = RateKtsPerSec * DeltaTime;
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

bool UClearanceAircraftBehaviour::IsEstablishedOnApproach(const FAircraftState& State, const FSectorEnvironment& Env) const
{
	const float FAC = (Env.ActiveRunwayHeading >= 0.f) ? Env.ActiveRunwayHeading : 270.f;
	const FVector2D Threshold(Env.ActiveRunwayThreshold.X, Env.ActiveRunwayThreshold.Y);
	const FVector2D Rel = FVector2D(State.Position.X, State.Position.Y) - Threshold;
	const float DistNm = Rel.Size();

	const float FacRad = FMath::DegreesToRadians(FAC);
	const FVector2D Inbound(FMath::Sin(FacRad), FMath::Cos(FacRad));
	const FVector2D RightOfCourse(Inbound.Y, -Inbound.X);

	const float CrossTrackNm = FMath::Abs(FVector2D::DotProduct(Rel, RightOfCourse));
	const float AlongTrack = FVector2D::DotProduct(Rel, Inbound); // < 0 = on the approach side (before threshold)
	const float HeadingErr = FMath::Abs(FMath::FindDeltaAngleDegrees(State.Heading, FAC));

	// Glideslope height at this distance - must be at or just below it (small margin) so
	// it intercepts the glidepath from BELOW and rides it down, rather than capturing
	// from way overhead and diving onto the runway. - TripleA
	const float GlideAlt = FMath::Max(0.f, -AlongTrack) * 318.f; // ~318 ft per nm (3 deg)

	// Capture corridor is a CONE: narrow at the runway, widening with distance (like a
	// real localiser beam), so it must be precisely lined up close in but is forgiving
	// far out. Matches the funnel drawn on the debug overlay. - TripleA
	const float CorridorHalf = FMath::Clamp(-AlongTrack * 0.1f, 0.3f, ClearanceConstants::ApproachCorridorHalfWidthNm);

	return CrossTrackNm <= CorridorHalf                        // inside the capture cone
		&& HeadingErr <= 40.f                                  // intercepting at a sane angle, not crossing it
		&& AlongTrack < 0.f                                    // positioned to fly toward the runway, not away
		&& DistNm <= ClearanceConstants::ApproachCorridorLengthNm // within intercept range (matches the drawn centreline)
		&& State.Altitude <= GlideAlt + 300.f;                 // at/below the glidepath - intercept from below, no dive
}

bool UClearanceAircraftBehaviour::HasMissedApproach(const FAircraftState& State, const FSectorEnvironment& Env) const
{
	const float FAC = (Env.ActiveRunwayHeading >= 0.f) ? Env.ActiveRunwayHeading : 270.f;
	const FVector2D Threshold(Env.ActiveRunwayThreshold.X, Env.ActiveRunwayThreshold.Y);
	const FVector2D Rel = FVector2D(State.Position.X, State.Position.Y) - Threshold;

	const float FacRad = FMath::DegreesToRadians(FAC);
	const FVector2D Inbound(FMath::Sin(FacRad), FMath::Cos(FacRad));
	const float AlongTrack = FVector2D::DotProduct(Rel, Inbound);

	// Past the threshold (out the departure end) and STILL airborne = it overflew the
	// runway without landing - either never lined up, or cleared too late and couldn't
	// get down. A landed aircraft rolling through is below the height gate. The slack
	// stops it tripping right as it crosses while settling on. - TripleA
	return AlongTrack > 1.f && State.Altitude > 300.f;
}

void UClearanceAircraftBehaviour::RunApproachGuidance(FAircraftState& State, const FSectorEnvironment& Env)
{
	// Aim at the SELECTED runway's threshold (X=East, Y=North, in nm).
	const float FAC = (Env.ActiveRunwayHeading >= 0.f) ? Env.ActiveRunwayHeading : 270.f;
	const FVector2D Threshold(Env.ActiveRunwayThreshold.X, Env.ActiveRunwayThreshold.Y);
	const FVector2D Rel = FVector2D(State.Position.X, State.Position.Y) - Threshold; // aircraft relative to threshold

	const float FacRad = FMath::DegreesToRadians(FAC);
	const FVector2D Inbound(FMath::Sin(FacRad), FMath::Cos(FacRad));   // direction flown to land
	const FVector2D RightOfCourse(Inbound.Y, -Inbound.X);
	const float CrossTrackNm = FVector2D::DotProduct(Rel, RightOfCourse);
	const float AlongTrack = FVector2D::DotProduct(Rel, Inbound); // < 0 before the threshold, 0 at it

	// Localiser: steer back toward the extended centreline; fly the course on it. - TripleA
	const float Correction = FMath::Clamp(-CrossTrackNm * 3.f, -30.f, 30.f);
	State.TargetHeading = FMath::Fmod(FAC + Correction + 360.f, 360.f);
	ActiveTurnDirection = 0;

	// Aim point sits a little way INTO the runway (the real touchdown zone is ~300m
	// past the threshold), not right on the lip. The 3-degree glideslope (~318 ft/nm)
	// is measured to that aim point, so height hits 0 there. - TripleA
	const float AimAlong = AlongTrack - TouchdownZoneOffsetNm; // < 0 before the aim point, 0 at it
	const float DistToAimNm = FMath::Max(0.f, -AimAlong);
	const float GlideAlt = DistToAimNm * 318.f;
	State.TargetAltitude = FMath::Min(State.TargetAltitude, GlideAlt);

	// Settle onto a final-approach speed.
	State.TargetSpeed = State.MinOperatingSpeed + 10.f;

	// Touchdown once it has reached the aim point (into the runway) and is low, so it
	// plants in the touchdown zone, not on the edge. If it came in high it carries past
	// and lands long - but never short of the aim point. - TripleA
	if (AimAlong >= 0.f && State.Altitude <= 100.f)
	{
		State.FlightPhase = EFlightPhase::Landing;
	}
}

float UClearanceAircraftBehaviour::TurnRateDegPerSec(const FAircraftState& State) const
{
	const float SpeedMps = State.Speed * 0.514444f;
	if (SpeedMps < 1.f)
	{
		return 0.f; // basically stationary, no meaningful turn
	}

	const float BankLimit = ClearanceConstants::GetCategoryPerformance(State.WakeCategory).BankLimitDeg;
	const float Omega = (9.81f * FMath::Tan(FMath::DegreesToRadians(BankLimit))) / SpeedMps;
	return FMath::RadiansToDegrees(Omega);
}

float UClearanceAircraftBehaviour::DensityAdjustedClimbRate(const FAircraftState& State) const
{
	const float Base = (State.MaxClimbRate > 0.f)
		? State.MaxClimbRate
		: ClearanceConstants::GetCategoryPerformance(State.WakeCategory).MaxClimbRateFtMin;
	return Base * ISADensityRatio(State.Altitude);
}

float UClearanceAircraftBehaviour::ISADensityRatio(float AltitudeFt)
{
	// Simplified ISA troposphere density ratio; enough to make climb fall off with
	// altitude without modelling full atmospherics (post-MVP).
	const float Alt = FMath::Max(0.f, AltitudeFt);
	const float Ratio = FMath::Pow(1.f - 6.875e-6f * Alt, 4.2561f);
	return FMath::Clamp(Ratio, 0.1f, 1.f);
}
