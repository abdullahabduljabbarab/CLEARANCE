// UClearanceInstructionValidator tests. The Validator is a pure stateless
// gatekeeper - takes an FAircraftInstruction + FAircraftState and returns
// EInstructionResult::Accepted or a Rejected_* reason. Perfect unit-test
// target because it owns nothing and touches no world state. - TripleA
//
// Covers requirements:
//   REQ-COMMS-001  A valid feasible instruction on a valid state shall
//                  return Accepted.
//   REQ-COMMS-002  An instruction on an aircraft with bIsValid=false shall
//                  return Rejected_InvalidCallsign.
//   REQ-COMMS-003  A go-around (bIsGoAround) shall bypass envelope checks
//                  and return Accepted regardless of state.
//   REQ-COMMS-004  Any instruction to an aircraft in Exiting phase shall
//                  return Rejected_AircraftExited.
//   REQ-COMMS-005  AltitudeChange above the aircraft category's service
//                  ceiling shall return Rejected_PhysicallyImpossible.
//   REQ-COMMS-006  AltitudeChange below zero shall return
//                  Rejected_PhysicallyImpossible.
//   REQ-COMMS-007  SpeedChange below the aircraft category's minimum
//                  operating speed shall return Rejected_PhysicallyImpossible.
//   REQ-COMMS-008  SpeedChange above the aircraft category's max operating
//                  speed shall return Rejected_PhysicallyImpossible.
//   REQ-COMMS-009  NaN / infinite target values shall be rejected as
//                  Rejected_PhysicallyImpossible.
//   REQ-COMMS-010  Military airframes shall use the fighter performance
//                  envelope (Vmax ~1050 kts, ceiling 50000 ft) so an
//                  instruction that's rejected on a Heavy is accepted on
//                  a fighter.

#include "Misc/AutomationTest.h"
#include "Comms/ClearanceInstructionValidator.h"
#include "Core/CLEARANCETypes.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Baseline valid state: Medium (737-800) at cruise, ready to accept most
	// clearances within its envelope. Every test starts here + mutates the
	// one field it cares about. - TripleA
	FAircraftState MakeBaselineState()
	{
		FAircraftState S;
		S.bIsValid      = true;
		S.Callsign      = FName("DLH101");
		S.FlightPhase   = EFlightPhase::Enroute;
		S.WakeCategory  = EWakeCategory::Medium;
		S.bIsMilitary   = false;
		S.Altitude      = 30000.f;
		S.Speed         = 280.f;
		S.Heading       = 90.f;
		return S;
	}

	FAircraftInstruction MakeInstruction(EInstructionType Type, float TargetValue)
	{
		FAircraftInstruction I;
		I.TargetCallsign = FName("DLH101");
		I.Type           = Type;
		I.TargetValue    = TargetValue;
		I.bIsGoAround    = false;
		return I;
	}
}

// Covers: REQ-COMMS-001 - baseline sanity: a normal instruction accepts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceValidatorAcceptedBaselineTest,
	"Clearance.Comms.Validator.AcceptedBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceValidatorAcceptedBaselineTest::RunTest(const FString& Parameters)
{
	UClearanceInstructionValidator* V = NewObject<UClearanceInstructionValidator>();
	const FAircraftState S = MakeBaselineState();

	TestEqual(TEXT("Sensible heading change accepted"),
		int32(V->ValidateInstruction(MakeInstruction(EInstructionType::HeadingChange, 210.f), S)),
		int32(EInstructionResult::Accepted));
	TestEqual(TEXT("Sensible altitude change accepted"),
		int32(V->ValidateInstruction(MakeInstruction(EInstructionType::AltitudeChange, 25000.f), S)),
		int32(EInstructionResult::Accepted));
	TestEqual(TEXT("Sensible speed change accepted"),
		int32(V->ValidateInstruction(MakeInstruction(EInstructionType::SpeedChange, 260.f), S)),
		int32(EInstructionResult::Accepted));

	return true;
}

// Covers: REQ-COMMS-002 - invalid aircraft state rejects.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceValidatorRejectInvalidCallsignTest,
	"Clearance.Comms.Validator.RejectInvalidCallsign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceValidatorRejectInvalidCallsignTest::RunTest(const FString& Parameters)
{
	UClearanceInstructionValidator* V = NewObject<UClearanceInstructionValidator>();
	FAircraftState S = MakeBaselineState();
	S.bIsValid = false;

	TestEqual(TEXT("Invalid state rejected"),
		int32(V->ValidateInstruction(MakeInstruction(EInstructionType::HeadingChange, 90.f), S)),
		int32(EInstructionResult::Rejected_InvalidCallsign));
	return true;
}

// Covers: REQ-COMMS-003 - go-around bypasses feasibility checks.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceValidatorGoAroundBypassesTest,
	"Clearance.Comms.Validator.GoAroundBypasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceValidatorGoAroundBypassesTest::RunTest(const FString& Parameters)
{
	UClearanceInstructionValidator* V = NewObject<UClearanceInstructionValidator>();
	FAircraftState S = MakeBaselineState();
	// Even if aircraft is exiting (which would normally reject), a
	// system-issued go-around must still get through - safety overrides
	// the envelope. - TripleA
	S.FlightPhase = EFlightPhase::Exiting;

	FAircraftInstruction I = MakeInstruction(EInstructionType::AltitudeChange, 999999.f);
	I.bIsGoAround = true;

	TestEqual(TEXT("Go-around accepted even with impossible altitude + exiting phase"),
		int32(V->ValidateInstruction(I, S)),
		int32(EInstructionResult::Accepted));
	return true;
}

// Covers: REQ-COMMS-004 - aircraft in Exiting phase refuses further orders.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceValidatorRejectExitingTest,
	"Clearance.Comms.Validator.RejectExiting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceValidatorRejectExitingTest::RunTest(const FString& Parameters)
{
	UClearanceInstructionValidator* V = NewObject<UClearanceInstructionValidator>();
	FAircraftState S = MakeBaselineState();
	S.FlightPhase = EFlightPhase::Exiting;

	TestEqual(TEXT("Heading instruction to exiting aircraft rejected"),
		int32(V->ValidateInstruction(MakeInstruction(EInstructionType::HeadingChange, 90.f), S)),
		int32(EInstructionResult::Rejected_AircraftExited));
	return true;
}

// Covers: REQ-COMMS-005 - altitude above service ceiling refused.
// Medium (737-800) service ceiling = 41000 ft.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceValidatorRejectAltitudeAboveCeilingTest,
	"Clearance.Comms.Validator.RejectAltitudeAboveCeiling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceValidatorRejectAltitudeAboveCeilingTest::RunTest(const FString& Parameters)
{
	UClearanceInstructionValidator* V = NewObject<UClearanceInstructionValidator>();
	const FAircraftState S = MakeBaselineState();  // Medium, ceiling 41000

	TestEqual(TEXT("Altitude 45000 ft rejected on Medium (ceiling 41000)"),
		int32(V->ValidateInstruction(MakeInstruction(EInstructionType::AltitudeChange, 45000.f), S)),
		int32(EInstructionResult::Rejected_PhysicallyImpossible));
	return true;
}

// Covers: REQ-COMMS-006 - negative altitude refused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceValidatorRejectAltitudeNegativeTest,
	"Clearance.Comms.Validator.RejectAltitudeNegative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceValidatorRejectAltitudeNegativeTest::RunTest(const FString& Parameters)
{
	UClearanceInstructionValidator* V = NewObject<UClearanceInstructionValidator>();
	const FAircraftState S = MakeBaselineState();

	TestEqual(TEXT("Negative altitude rejected"),
		int32(V->ValidateInstruction(MakeInstruction(EInstructionType::AltitudeChange, -100.f), S)),
		int32(EInstructionResult::Rejected_PhysicallyImpossible));
	return true;
}

// Covers: REQ-COMMS-007 - speed below stall refused.
// Medium min operating speed = 150 kts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceValidatorRejectSpeedBelowMinTest,
	"Clearance.Comms.Validator.RejectSpeedBelowMin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceValidatorRejectSpeedBelowMinTest::RunTest(const FString& Parameters)
{
	UClearanceInstructionValidator* V = NewObject<UClearanceInstructionValidator>();
	const FAircraftState S = MakeBaselineState();  // Medium, Vmin 150

	TestEqual(TEXT("Speed 100 kts rejected on Medium (Vmin 150)"),
		int32(V->ValidateInstruction(MakeInstruction(EInstructionType::SpeedChange, 100.f), S)),
		int32(EInstructionResult::Rejected_PhysicallyImpossible));
	return true;
}

// Covers: REQ-COMMS-008 - speed above Vmax refused.
// Medium max operating speed = 340 kts.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceValidatorRejectSpeedAboveMaxTest,
	"Clearance.Comms.Validator.RejectSpeedAboveMax",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceValidatorRejectSpeedAboveMaxTest::RunTest(const FString& Parameters)
{
	UClearanceInstructionValidator* V = NewObject<UClearanceInstructionValidator>();
	const FAircraftState S = MakeBaselineState();  // Medium, Vmax 340

	TestEqual(TEXT("Speed 500 kts rejected on Medium (Vmax 340)"),
		int32(V->ValidateInstruction(MakeInstruction(EInstructionType::SpeedChange, 500.f), S)),
		int32(EInstructionResult::Rejected_PhysicallyImpossible));
	return true;
}

// Covers: REQ-COMMS-009 - NaN + infinite target values refused.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceValidatorRejectNonFiniteTest,
	"Clearance.Comms.Validator.RejectNonFinite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceValidatorRejectNonFiniteTest::RunTest(const FString& Parameters)
{
	UClearanceInstructionValidator* V = NewObject<UClearanceInstructionValidator>();
	const FAircraftState S = MakeBaselineState();

	TestEqual(TEXT("NaN heading rejected"),
		int32(V->ValidateInstruction(
			MakeInstruction(EInstructionType::HeadingChange, std::numeric_limits<float>::quiet_NaN()), S)),
		int32(EInstructionResult::Rejected_PhysicallyImpossible));
	TestEqual(TEXT("Infinite altitude rejected"),
		int32(V->ValidateInstruction(
			MakeInstruction(EInstructionType::AltitudeChange, std::numeric_limits<float>::infinity()), S)),
		int32(EInstructionResult::Rejected_PhysicallyImpossible));
	TestEqual(TEXT("NaN speed rejected"),
		int32(V->ValidateInstruction(
			MakeInstruction(EInstructionType::SpeedChange, std::numeric_limits<float>::quiet_NaN()), S)),
		int32(EInstructionResult::Rejected_PhysicallyImpossible));
	return true;
}

// Covers: REQ-COMMS-010 - military envelope allows instructions rejected on
// airliner categories. Same command, different acceptance outcome.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceValidatorMilitaryEnvelopeExpandsTest,
	"Clearance.Comms.Validator.MilitaryEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceValidatorMilitaryEnvelopeExpandsTest::RunTest(const FString& Parameters)
{
	UClearanceInstructionValidator* V = NewObject<UClearanceInstructionValidator>();

	// Same aircraft, same instruction - toggle bIsMilitary and verify the
	// acceptance boundary shifts. - TripleA
	FAircraftState Civil = MakeBaselineState();
	Civil.bIsMilitary = false;

	FAircraftState Military = MakeBaselineState();
	Military.bIsMilitary = true;

	// Speed 800 kts: rejected on Medium (Vmax 340), accepted on fighter (Vmax ~1050)
	const FAircraftInstruction FastCmd = MakeInstruction(EInstructionType::SpeedChange, 800.f);
	TestEqual(TEXT("800 kts rejected on Medium civil"),
		int32(V->ValidateInstruction(FastCmd, Civil)),
		int32(EInstructionResult::Rejected_PhysicallyImpossible));
	TestEqual(TEXT("800 kts accepted on military fighter"),
		int32(V->ValidateInstruction(FastCmd, Military)),
		int32(EInstructionResult::Accepted));

	// Altitude 48000 ft: rejected on Medium (ceiling 41000), accepted on fighter (ceiling 50000)
	const FAircraftInstruction HighCmd = MakeInstruction(EInstructionType::AltitudeChange, 48000.f);
	TestEqual(TEXT("48000 ft rejected on Medium civil"),
		int32(V->ValidateInstruction(HighCmd, Civil)),
		int32(EInstructionResult::Rejected_PhysicallyImpossible));
	TestEqual(TEXT("48000 ft accepted on military fighter"),
		int32(V->ValidateInstruction(HighCmd, Military)),
		int32(EInstructionResult::Accepted));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
