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

	// How far out the approach centreline runs (nm). The drawn corridor and the
	// distance an aircraft is allowed to capture from both use this, so they can't
	// drift apart - extend the localiser, extend where you can line up. - TripleA
	constexpr float ApproachCorridorLengthNm = 80.f;

	// Half-width of the localiser capture corridor (nm) - how far off the centreline
	// the aircraft can be and still get established (matches the drawn grey lines).
	constexpr float ApproachCorridorHalfWidthNm = 3.f;

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
		// Wheel-braking deceleration on the landing roll (kts/sec). Heavier aircraft
		// shed speed more slowly, so they roll out far longer than a Light. - TripleA
		float GroundBrakingKtsPerSec;
	};

	inline FCategoryPerformance GetCategoryPerformance(EWakeCategory Category)
	{
		switch (Category)
		{
		case EWakeCategory::Light:   // Cessna 172S
			return { 14000.f,  65.f, 163.f,  730.f, 1500.f, 30.f, 2.0f, 1.00f, 15.f, 2.2f };
		case EWakeCategory::Heavy:   // Boeing 777-300ER
			return { 43100.f, 170.f, 350.f, 2500.f, 3500.f, 25.f, 1.0f, 0.50f, 40.f, 1.0f };
		case EWakeCategory::Super:   // Airbus A380-800
			return { 43000.f, 155.f, 340.f, 1500.f, 3000.f, 25.f, 0.8f, 0.40f, 40.f, 0.85f };
		case EWakeCategory::Medium:  // Boeing 737-800
		default:
			return { 41000.f, 150.f, 340.f, 1800.f, 3000.f, 25.f, 1.5f, 0.75f, 37.f, 1.2f };
		}
	}

	// Military fighter envelope - F-35-ish. Far faster, far higher bank limit, much
	// quicker climb / accel than an airliner. Wake category still applies for the
	// separation matrix, but the actual flight envelope comes from here when the
	// aircraft is flagged bIsMilitary. - TripleA
	inline FCategoryPerformance GetMilitaryPerformance()
	{
		// F-35-ish. Vmax ~Mach 1.6 (~1050 kts), service ceiling 50k, sustained 80deg
		// bank in a hard turn, ~10 kts/sec accel through the transonic. - TripleA
		//        ceil    Vmin   Vmax    climb    desc    bank   accel  decel  xwind  brake
		return { 50000.f, 180.f, 1050.f, 12000.f, 15000.f, 80.f, 10.0f, 6.00f, 45.f, 3.0f };
	}

	// Picks the right envelope for the airframe. Military gets the fighter profile;
	// everyone else gets their wake-category profile.
	inline FCategoryPerformance GetEffectivePerformance(EWakeCategory Category, bool bIsMilitary)
	{
		return bIsMilitary ? GetMilitaryPerformance() : GetCategoryPerformance(Category);
	}
}
