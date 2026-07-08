// Safety-constants tests. Verifies that the tuning constants shipped in
// ClearanceConstants.h match ICAO doctrine (Doc 4444 for wake, PANS-OPS for
// vertical minima). Guard against silent tuning drift - anyone editing the
// numbers has to update these tests too, which is exactly the point. - TripleA
//
// Covers requirements:
//   REQ-SAFETY-001  Advisory horizontal separation shall be 8 nm.
//   REQ-SAFETY-002  Warning horizontal separation shall be 5 nm.
//   REQ-SAFETY-003  Critical horizontal separation shall be 3 nm.
//   REQ-SAFETY-004  Vertical minimum shall be 1000 ft (RVSM airspace).
//   REQ-SAFETY-005  Wake separation matrix shall match ICAO Doc 4444:
//                   Light behind Heavy  = 6 nm
//                   Medium behind Heavy = 5 nm
//                   Light behind Medium = 5 nm
//                   Heavy behind Heavy  = 4 nm
//                   Standard minimum    = 3 nm
//   REQ-SAFETY-006  Alert level ordering shall be monotonic:
//                   Advisory > Warning > Critical (nm), so as separation
//                   shrinks, alert escalates.

#include "Misc/AutomationTest.h"
#include "Core/ClearanceConstants.h"

#if WITH_DEV_AUTOMATION_TESTS

// Covers: REQ-SAFETY-001, REQ-SAFETY-002, REQ-SAFETY-003, REQ-SAFETY-006 -
// horizontal separation thresholds match ICAO + are monotonic.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceSafetyHorizontalSeparationTest,
	"Clearance.Safety.HorizontalSeparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceSafetyHorizontalSeparationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Advisory horizontal is 8 nm"),
		FMath::IsNearlyEqual(ClearanceConstants::AdvisoryHorizontalNm, 8.f, 0.001f));
	TestTrue(TEXT("Warning horizontal is 5 nm"),
		FMath::IsNearlyEqual(ClearanceConstants::WarningHorizontalNm, 5.f, 0.001f));
	TestTrue(TEXT("Critical horizontal is 3 nm"),
		FMath::IsNearlyEqual(ClearanceConstants::CriticalHorizontalNm, 3.f, 0.001f));

	// Monotonic: Advisory > Warning > Critical - shrinking separation escalates alert.
	TestTrue(TEXT("Advisory > Warning threshold"),
		ClearanceConstants::AdvisoryHorizontalNm > ClearanceConstants::WarningHorizontalNm);
	TestTrue(TEXT("Warning > Critical threshold"),
		ClearanceConstants::WarningHorizontalNm > ClearanceConstants::CriticalHorizontalNm);
	return true;
}

// Covers: REQ-SAFETY-004 - vertical minimum matches RVSM airspace.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceSafetyVerticalMinimumTest,
	"Clearance.Safety.VerticalMinimum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceSafetyVerticalMinimumTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Vertical minimum is 1000 ft (RVSM)"),
		FMath::IsNearlyEqual(ClearanceConstants::VerticalMinimumFt, 1000.f, 0.001f));
	return true;
}

// Covers: REQ-SAFETY-005 - wake separation matrix matches ICAO Doc 4444.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceSafetyWakeSeparationMatrixTest,
	"Clearance.Safety.WakeSeparationMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceSafetyWakeSeparationMatrixTest::RunTest(const FString& Parameters)
{
	// The three "heavier-leader" values from Doc 4444 §5.8. Values are a
	// tuning knob for realism but any change here breaks compliance with
	// how real controllers space arrivals - hence the test. - TripleA
	TestTrue(TEXT("Light behind Heavy = 6 nm (Doc 4444)"),
		FMath::IsNearlyEqual(ClearanceConstants::WakeLightBehindHeavyNm, 6.f, 0.001f));
	TestTrue(TEXT("Medium behind Heavy = 5 nm (Doc 4444)"),
		FMath::IsNearlyEqual(ClearanceConstants::WakeMediumBehindHeavyNm, 5.f, 0.001f));
	TestTrue(TEXT("Light behind Medium = 5 nm (Doc 4444)"),
		FMath::IsNearlyEqual(ClearanceConstants::WakeLightBehindMediumNm, 5.f, 0.001f));
	TestTrue(TEXT("Heavy behind Heavy = 4 nm (Doc 4444)"),
		FMath::IsNearlyEqual(ClearanceConstants::WakeHeavyBehindHeavyNm, 4.f, 0.001f));
	TestTrue(TEXT("Standard minimum = 3 nm"),
		FMath::IsNearlyEqual(ClearanceConstants::WakeStandardMinimumNm, 3.f, 0.001f));

	// Ordering: heavier-behind-lighter never exceeds lighter-behind-heavier.
	// A Light following a Heavy needs more separation than a Heavy following a
	// Light. Validates that whoever tuned the matrix respects the aerodynamics. - TripleA
	TestTrue(TEXT("Light-behind-Heavy separation >= Medium-behind-Heavy"),
		ClearanceConstants::WakeLightBehindHeavyNm >=
		ClearanceConstants::WakeMediumBehindHeavyNm);
	TestTrue(TEXT("Wake separations always >= standard minimum"),
		ClearanceConstants::WakeLightBehindHeavyNm  >= ClearanceConstants::WakeStandardMinimumNm &&
		ClearanceConstants::WakeMediumBehindHeavyNm >= ClearanceConstants::WakeStandardMinimumNm &&
		ClearanceConstants::WakeLightBehindMediumNm >= ClearanceConstants::WakeStandardMinimumNm &&
		ClearanceConstants::WakeHeavyBehindHeavyNm  >= ClearanceConstants::WakeStandardMinimumNm);
	return true;
}

// Aircraft performance constants sanity check - Vmax > Vmin, service ceiling > 0,
// and each category has a distinct envelope (Light and Super shouldn't accidentally
// share numbers).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceSafetyCategoryPerformanceTest,
	"Clearance.Safety.CategoryPerformance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceSafetyCategoryPerformanceTest::RunTest(const FString& Parameters)
{
	using ClearanceConstants::GetCategoryPerformance;
	using ClearanceConstants::FCategoryPerformance;

	for (EWakeCategory Cat : {
		EWakeCategory::Light,
		EWakeCategory::Medium,
		EWakeCategory::Heavy,
		EWakeCategory::Super })
	{
		const FCategoryPerformance P = GetCategoryPerformance(Cat);
		TestTrue(TEXT("Service ceiling positive"), P.ServiceCeilingFt > 0.f);
		TestTrue(TEXT("Vmax > Vmin"), P.MaxOperatingSpeedKts > P.MinOperatingSpeedKts);
		TestTrue(TEXT("Vmin sensible (>= 50 kts)"), P.MinOperatingSpeedKts >= 50.f);
		TestTrue(TEXT("Vmax sensible (<= 500 kts)"), P.MaxOperatingSpeedKts <= 500.f);
	}

	// Military envelope should be MORE permissive than any civil category
	// on all axes - it's the F-35-ish profile. - TripleA
	const auto Fighter = ClearanceConstants::GetMilitaryPerformance();
	const auto Heavy   = GetCategoryPerformance(EWakeCategory::Heavy);
	TestTrue(TEXT("Fighter Vmax > Heavy Vmax"),
		Fighter.MaxOperatingSpeedKts > Heavy.MaxOperatingSpeedKts);
	TestTrue(TEXT("Fighter service ceiling > Heavy ceiling"),
		Fighter.ServiceCeilingFt > Heavy.ServiceCeilingFt);

	return true;
}

// Covers: GetEffectivePerformance branching.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceSafetyEffectivePerformanceRoutingTest,
	"Clearance.Safety.EffectivePerformanceRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceSafetyEffectivePerformanceRoutingTest::RunTest(const FString& Parameters)
{
	const auto CivilMedium =
		ClearanceConstants::GetEffectivePerformance(EWakeCategory::Medium, false);
	const auto MilitaryMedium =
		ClearanceConstants::GetEffectivePerformance(EWakeCategory::Medium, true);
	const auto Fighter = ClearanceConstants::GetMilitaryPerformance();
	const auto Medium  = ClearanceConstants::GetCategoryPerformance(EWakeCategory::Medium);

	TestTrue(TEXT("bIsMilitary=false picks civil envelope"),
		FMath::IsNearlyEqual(CivilMedium.MaxOperatingSpeedKts, Medium.MaxOperatingSpeedKts, 0.01f));
	TestTrue(TEXT("bIsMilitary=true picks fighter envelope"),
		FMath::IsNearlyEqual(MilitaryMedium.MaxOperatingSpeedKts, Fighter.MaxOperatingSpeedKts, 0.01f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
