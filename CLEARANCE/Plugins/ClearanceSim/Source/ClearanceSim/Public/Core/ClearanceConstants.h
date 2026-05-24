#pragma once

#include "CoreMinimal.h"
#include "Core/CLEARANCETypes.h"

// All the sim's tuning lives here so nothing has to guess. Separation figures are
// ICAO; the per-category performance comes from representative real aircraft.
namespace ClearanceConstants
{
	// Horizontal / vertical separation thresholds
	constexpr float AdvisoryHorizontalNm = 8.f;
	constexpr float WarningHorizontalNm  = 5.f;
	constexpr float CriticalHorizontalNm = 3.f;
	constexpr float VerticalMinimumFt    = 1000.f;

	// Wake separation matrix (nm): a following aircraft behind a heavier leader.
	// ICAO Doc 4444 category pairs - TripleA
	constexpr float WakeLightBehindHeavyNm  = 6.f;
	constexpr float WakeMediumBehindHeavyNm = 5.f;
	constexpr float WakeLightBehindMediumNm = 5.f;
	constexpr float WakeHeavyBehindHeavyNm  = 4.f;
	constexpr float WakeStandardMinimumNm   = 3.f;

	// How much less crosswind (kts) a runway must have before we switch to it -
	// the dead-band that stops the active runway flipping on a marginal wind.
	constexpr float RunwaySwitchDeadbandKts = 2.f;

	// Per-category performance from representative real aircraft:
	//   Light  - Cessna 172S        Medium - Boeing 737-800
	//   Heavy  - Boeing 777-300ER    Super  - Airbus A380-800
	// MinOperatingSpeed is ~1.3x stall (Vref) - the slowest ATC would actually
	// clear - not the raw stall speed. Super climbs slower than Heavy because it's
	// far heavier despite the bigger airframe. - TripleA
	struct FCategoryPerformance
	{
		float ServiceCeilingFt;
		float MinOperatingSpeedKts;
		float MaxOperatingSpeedKts;
		float MaxClimbRateFtMin;
		float MaxDescentRateFtMin;
		float BankLimitDeg;
		float AccelKtsPerSec;
		float DecelKtsPerSec;
		float CrosswindLimitKts;
	};

	inline FCategoryPerformance GetCategoryPerformance(EWakeCategory Category)
	{
		switch (Category)
		{
		case EWakeCategory::Light:   // Cessna 172S
			return { 14000.f,  65.f, 163.f,  730.f, 1500.f, 30.f, 2.0f, 1.00f, 15.f };
		case EWakeCategory::Heavy:   // Boeing 777-300ER
			return { 43100.f, 170.f, 350.f, 2500.f, 3500.f, 25.f, 1.0f, 0.50f, 40.f };
		case EWakeCategory::Super:   // Airbus A380-800
			return { 43000.f, 155.f, 340.f, 1500.f, 3000.f, 25.f, 0.8f, 0.40f, 40.f };
		case EWakeCategory::Medium:  // Boeing 737-800
		default:
			return { 41000.f, 150.f, 340.f, 1800.f, 3000.f, 25.f, 1.5f, 0.75f, 37.f };
		}
	}
}
