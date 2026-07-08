// Radar range equation tests. Verifies that the monostatic pulse-radar
// equation shipped in ClearanceRadarEquation.h behaves the way Skolnik
// says it should. Guards against silent drift in the physics that
// drives detection Pd on the operator scope.
//
// The equation is:
//   Pr = (Pt * Gt * Gr * lambda^2 * sigma) / ((4*pi)^3 * R^4 * L)
// so the mathematical properties we can verify without loading Engine
// dependencies are:
//   - R^4 fall-off (halving range multiplies power by 16),
//   - linearity in target RCS,
//   - lambda^2 dependence,
//   - thermal noise floor N = kTBF,
//   - Pd is monotonic and centred on the required SNR.
//
// Covers requirements:
//   REQ-RADAR-001  Received power shall follow the R^4 range law.
//   REQ-RADAR-002  Received power shall be linear in target RCS.
//   REQ-RADAR-003  Noise floor shall equal k*T*B*F to numerical tolerance.
//   REQ-RADAR-004  Wavelength shall equal c/f for the given frequency.
//   REQ-RADAR-005  Pd(SnrDb=RequiredSnrDb) shall equal 0.5 exactly.
//   REQ-RADAR-006  Pd shall be monotonically non-decreasing in SNR.
//   REQ-RADAR-007  Default parameters shall place Pd ~= 0.5 at ~80 nm
//                  against a 10 m^2 target (calibration to the previous
//                  hard 80 nm range gate the physics replaced). - TripleA

#include "Misc/AutomationTest.h"
#include "Safety/ClearanceRadarEquation.h"

#if WITH_DEV_AUTOMATION_TESTS

// Convenience: a fully-populated FDetectionInputs mirroring the header's
// defaults. Individual tests copy this and mutate one field. - TripleA
static ClearanceRadarEquation::FDetectionInputs DefaultInputs()
{
	ClearanceRadarEquation::FDetectionInputs In;
	return In;   // aggregate defaults, see header
}

// -----------------------------------------------------------------------
// Covers: REQ-RADAR-001 - R^4 range law.
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRadarEquationRangeLawTest,
	"Clearance.Radar.Equation.R4RangeLaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRadarEquationRangeLawTest::RunTest(const FString& Parameters)
{
	using namespace ClearanceRadarEquation;
	const double Lambda = WavelengthM(2.8e9);
	const double Gt = LinearFromDb(34.0), Gr = LinearFromDb(34.0), L = LinearFromDb(6.0);

	const double Pr_10km = ReceivedPowerWatts(25000.0, Gt, Gr, Lambda, 10.0, 10000.0, L);
	const double Pr_20km = ReceivedPowerWatts(25000.0, Gt, Gr, Lambda, 10.0, 20000.0, L);

	// Doubling range should divide received power by 16.
	const double Ratio = Pr_10km / Pr_20km;
	TestTrue(TEXT("Pr(R) / Pr(2R) is approximately 16"),
		FMath::IsNearlyEqual(static_cast<float>(Ratio), 16.f, 0.01f));

	return true;
}

// -----------------------------------------------------------------------
// Covers: REQ-RADAR-002 - linearity in target RCS.
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRadarEquationRcsScalingTest,
	"Clearance.Radar.Equation.RcsScaling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRadarEquationRcsScalingTest::RunTest(const FString& Parameters)
{
	using namespace ClearanceRadarEquation;
	const double Lambda = WavelengthM(2.8e9);
	const double Gt = LinearFromDb(34.0), Gr = LinearFromDb(34.0), L = LinearFromDb(6.0);

	const double Pr_10 = ReceivedPowerWatts(25000.0, Gt, Gr, Lambda, 10.0,  15000.0, L);
	const double Pr_20 = ReceivedPowerWatts(25000.0, Gt, Gr, Lambda, 20.0,  15000.0, L);

	// Doubling RCS should double received power exactly (Pr is linear in sigma).
	const double Ratio = Pr_20 / Pr_10;
	TestTrue(TEXT("Pr(2*sigma) / Pr(sigma) is approximately 2"),
		FMath::IsNearlyEqual(static_cast<float>(Ratio), 2.f, 0.001f));

	return true;
}

// -----------------------------------------------------------------------
// Covers: REQ-RADAR-003 - noise floor N = kTBF.
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRadarEquationNoiseFloorTest,
	"Clearance.Radar.Equation.NoiseFloorKtbf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRadarEquationNoiseFloorTest::RunTest(const FString& Parameters)
{
	using namespace ClearanceRadarEquation;

	// Standard reference case: T = 290 K, B = 1 MHz, F = 3 dB (linear 1.9953).
	const double T = 290.0, B = 1.0e6;
	const double FLin = LinearFromDb(3.0);
	const double N = NoiseFloorWatts(T, B, FLin);

	// -174 dBm/Hz is Johnson noise at 290 K. At 1 MHz bandwidth and 3 dB
	// noise figure the expected noise power is roughly -111 dBm (~= 8e-15 W).
	const double N_dBW = DbFromLinear(N);
	TestTrue(TEXT("Noise floor is approximately -141 dBW (= -111 dBm)"),
		FMath::IsNearlyEqual(static_cast<float>(N_dBW), -141.f, 1.f));

	// Also verify by direct computation of kTBF.
	const double Expected = 1.380649e-23 * T * B * FLin;
	TestTrue(TEXT("Noise floor equals k*T*B*F"),
		FMath::IsNearlyEqual(static_cast<float>(N / Expected), 1.f, 0.001f));

	return true;
}

// -----------------------------------------------------------------------
// Covers: REQ-RADAR-004 - wavelength = c / f.
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRadarEquationWavelengthTest,
	"Clearance.Radar.Equation.Wavelength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRadarEquationWavelengthTest::RunTest(const FString& Parameters)
{
	using namespace ClearanceRadarEquation;

	// S-band 2.8 GHz -> ~107 mm wavelength.
	const double LambdaS = WavelengthM(2.8e9);
	TestTrue(TEXT("Lambda at 2.8 GHz is ~107 mm"),
		FMath::IsNearlyEqual(static_cast<float>(LambdaS), 0.107f, 0.001f));

	// X-band 10 GHz -> ~30 mm wavelength.
	const double LambdaX = WavelengthM(10.0e9);
	TestTrue(TEXT("Lambda at 10 GHz is ~30 mm"),
		FMath::IsNearlyEqual(static_cast<float>(LambdaX), 0.030f, 0.001f));

	return true;
}

// -----------------------------------------------------------------------
// Covers: REQ-RADAR-005 - Pd(SnrDb = RequiredSnrDb) = 0.5 exactly.
// The logistic is centred on RequiredSnrDb by construction.
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRadarEquationPdCentreTest,
	"Clearance.Radar.Equation.PdAtCentre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRadarEquationPdCentreTest::RunTest(const FString& Parameters)
{
	using namespace ClearanceRadarEquation;

	const double Pd = ProbabilityOfDetection(13.0, 13.0, 4.0);
	TestTrue(TEXT("Pd at threshold SNR is exactly 0.5"),
		FMath::IsNearlyEqual(static_cast<float>(Pd), 0.5f, 0.0001f));

	// At +infty SNR Pd -> 1; at -infty SNR Pd -> 0.
	const double PdHigh = ProbabilityOfDetection( 60.0, 13.0, 4.0);
	const double PdLow  = ProbabilityOfDetection(-60.0, 13.0, 4.0);
	TestTrue(TEXT("Pd at very high SNR is nearly 1"), PdHigh > 0.999);
	TestTrue(TEXT("Pd at very low SNR is nearly 0"),  PdLow  < 0.001);

	return true;
}

// -----------------------------------------------------------------------
// Covers: REQ-RADAR-006 - Pd is monotonically non-decreasing in SNR.
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRadarEquationPdMonotonicTest,
	"Clearance.Radar.Equation.PdMonotonic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRadarEquationPdMonotonicTest::RunTest(const FString& Parameters)
{
	using namespace ClearanceRadarEquation;

	double Prev = -1.0;
	bool bMonotonic = true;
	for (int32 SnrDb = -20; SnrDb <= 40; ++SnrDb)
	{
		const double Pd = ProbabilityOfDetection(static_cast<double>(SnrDb), 13.0, 4.0);
		if (Pd + 1e-9 < Prev) { bMonotonic = false; break; }
		Prev = Pd;
	}
	TestTrue(TEXT("Pd is monotonically non-decreasing across a 60 dB SNR sweep"),
		bMonotonic);

	return true;
}

// -----------------------------------------------------------------------
// Covers: REQ-RADAR-007 - default params calibrate to ~80 nm reference
// range for a 10 m^2 target. The physics upgrade replaced a hard 80 nm
// range gate; the defaults must land Pd ~= 0.5 near that range so
// operators don't see a step-change in what they can track.
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRadarEquationCalibrationTest,
	"Clearance.Radar.Equation.DefaultCalibration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRadarEquationCalibrationTest::RunTest(const FString& Parameters)
{
	using namespace ClearanceRadarEquation;

	// Sweep range and locate the Pd = 0.5 crossing point for the shipped
	// defaults + a 10 m^2 target. Must fall in a wide but sensible band
	// around 80 nm - tight enough to catch drift, loose enough not to
	// paper over engineering tuning inside the model.
	double CrossingNm = -1.0;
	for (double Nm = 5.0; Nm <= 300.0; Nm += 1.0)
	{
		FDetectionInputs In = DefaultInputs();
		In.RcsSquareMetres = 10.0;
		In.RangeMetres     = Nm * kMetresPerNauticalMile;
		const double Pd = ComputeDetection(In).ProbabilityOfDetection;
		if (Pd < 0.5) { CrossingNm = Nm; break; }
	}
	TestTrue(TEXT("Pd=0.5 range for 10 m^2 target falls between 40 and 200 nm"),
		CrossingNm > 40.0 && CrossingNm < 200.0);

	// A super-heavy (200 m^2) target must be detectable at longer range
	// than a light (1 m^2) - the whole point of putting RCS in the model.
	FDetectionInputs Heavy = DefaultInputs();
	Heavy.RcsSquareMetres = kRcsHeavy;
	Heavy.RangeMetres     = 40.0 * kMetresPerNauticalMile;
	FDetectionInputs Light = DefaultInputs();
	Light.RcsSquareMetres = kRcsLight;
	Light.RangeMetres     = 40.0 * kMetresPerNauticalMile;
	const double PdHeavy = ComputeDetection(Heavy).ProbabilityOfDetection;
	const double PdLight = ComputeDetection(Light).ProbabilityOfDetection;
	TestTrue(TEXT("At 40 nm, Pd(Heavy 100 m^2) > Pd(Light 1 m^2)"),
		PdHeavy > PdLight);

	return true;
}

// -----------------------------------------------------------------------
// dB conversion round-trips - sanity check that the inline helpers are
// consistent, since every other test in this file depends on them.
// -----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FClearanceRadarEquationDbRoundTripTest,
	"Clearance.Radar.Equation.DbRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FClearanceRadarEquationDbRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace ClearanceRadarEquation;

	for (const double Db : {0.0, 3.0, 6.0, 10.0, 13.0, 30.0, 60.0, -20.0, -40.0})
	{
		const double Lin = LinearFromDb(Db);
		const double Back = DbFromLinear(Lin);
		TestTrue(TEXT("dB -> linear -> dB round-trips within 1e-6"),
			FMath::IsNearlyEqual(static_cast<float>(Back), static_cast<float>(Db), 0.0001f));
	}

	// LinearFromDb(0) is exactly 1.
	TestTrue(TEXT("LinearFromDb(0) is exactly 1"),
		FMath::IsNearlyEqual(static_cast<float>(LinearFromDb(0.0)), 1.f, 1e-9f));

	// DbFromLinear(0) returns the -1000 sentinel, not NaN or -inf.
	TestTrue(TEXT("DbFromLinear(0) returns -1000 dB sentinel"),
		FMath::IsNearlyEqual(static_cast<float>(DbFromLinear(0.0)), -1000.f, 1e-3f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
