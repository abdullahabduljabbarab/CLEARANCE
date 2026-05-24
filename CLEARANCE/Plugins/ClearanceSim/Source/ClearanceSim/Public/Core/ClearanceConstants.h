// CLEARANCE - Tuning constants, consolidated into one authoritative place.
// Resolves the pre-production gap where these values were scattered across the
// Test Plan / Scaffold / breakdown docs. Values are grouped by source:
//   [DOC]  = specified in a pre-production document
//   [TODO] = NOT specified in docs; real-world-grounded placeholder, confirm later
// See Docs/PRODUCTION_LOG.md changelog for the open tuning items.
#pragma once

#include "CoreMinimal.h"
#include "Core/CLEARANCETypes.h"

namespace ClearanceConstants
{
	// --- Horizontal/vertical separation thresholds [DOC: Conflict Detector] ---
	constexpr float AdvisoryHorizontalNm = 8.f;
	constexpr float WarningHorizontalNm  = 5.f;
	constexpr float CriticalHorizontalNm = 3.f;
	constexpr float VerticalMinimumFt    = 1000.f;

	// --- Wake turbulence separation matrix, nm [DOC: Conflict Detector] ---
	// Required separation of a FOLLOWING aircraft behind a heavier LEADING one.
	// Pulled from ICAO Doc 4444 category pairs - TripleA
	constexpr float WakeLightBehindHeavyNm  = 6.f;
	constexpr float WakeMediumBehindHeavyNm = 5.f;
	constexpr float WakeLightBehindMediumNm = 5.f;
	constexpr float WakeHeavyBehindHeavyNm  = 4.f;
	constexpr float WakeStandardMinimumNm   = 3.f;	// all other category pairs

	// --- Max climb rate per category, ft/min [DOC: Test Plan ranges, upper bound] ---
	constexpr float MaxClimbRateLight  = 1000.f;	// doc range 500-1000
	constexpr float MaxClimbRateMedium = 2500.f;	// doc range 1500-2500
	constexpr float MaxClimbRateHeavy  = 3000.f;	// doc range 2000-3000
	constexpr float MaxClimbRateSuper  = 2800.f;	// [TODO] not in docs

	// placeholders below until I get real reference figures - TripleA
	// --- Service ceiling per category, ft [TODO: not in docs, confirm] ---
	constexpr float ServiceCeilingLight  = 25000.f;
	constexpr float ServiceCeilingMedium = 41000.f;
	constexpr float ServiceCeilingHeavy  = 43000.f;
	constexpr float ServiceCeilingSuper  = 43000.f;

	// --- Operating speed envelope per category, knots [TODO: not in docs, confirm] ---
	constexpr float MinSpeedLight  = 60.f;	constexpr float MaxSpeedLight  = 250.f;
	constexpr float MinSpeedMedium = 120.f;	constexpr float MaxSpeedMedium = 350.f;
	constexpr float MinSpeedHeavy  = 140.f;	constexpr float MaxSpeedHeavy  = 360.f;
	constexpr float MinSpeedSuper  = 150.f;	constexpr float MaxSpeedSuper  = 350.f;

	// --- Bank angle limit per category, degrees [TODO: not in docs, confirm] ---
	constexpr float BankLimitLight  = 25.f;
	constexpr float BankLimitMedium = 25.f;
	constexpr float BankLimitHeavy  = 25.f;
	constexpr float BankLimitSuper  = 20.f;

	// --- Runway selection hysteresis buffer, degrees [DOC: Risk R18 / Test 37] ---
	// Prevents active-runway oscillation near a crosswind boundary.
	constexpr float RunwaySelectionHysteresisDeg = 10.f;	// [TODO] magnitude TBC
}
