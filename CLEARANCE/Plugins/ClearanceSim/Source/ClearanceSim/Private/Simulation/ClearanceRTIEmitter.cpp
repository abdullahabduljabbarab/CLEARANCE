// Unreal-side adapter around ClearanceRTI. Every Emit* method converts
// the game module's Unreal-shape input into the IDL POD types the pure
// C++ publisher speaks, then delegates. All unit conversions (nm/ft/kts
// to metres/m/s) happen at this boundary - identical pattern to the DIS
// and DDS emitters, so all three wires always agree on what a metre is.
// - TripleA

#include "Simulation/ClearanceRTIEmitter.h"

#include "ClearanceRTI/ClearanceRTIPublisher.h"
// AirspaceTelemetryRTI.hpp comes in via ClearanceRTIPublisher.h already,
// but under UE's THIRD_PARTY_INCLUDES_START/END guards so this TU sees
// the RTI-generated types without inheriting Windows macros. - TripleA
#include "ClearanceDIS/ClearanceDISPDU.h"   // reuse the FNV-1a callsign hash

// Named namespace (not anonymous) - avoids C2084 collisions with the DIS +
// DDS emitters when unity-build merges the three TUs. See the same block in
// ClearanceDISEmitter.cpp for the full rationale. - TripleA
namespace ClearanceRTIEmitterHelpers
{
	// Same reserved entity for operator / ground-station traffic that the
	// DIS and DDS emitters use so all three wires filter air-side from
	// ground-side by the same number. - TripleA
	constexpr std::uint16_t kOperatorGroundStationEntity = 60000;

	inline std::uint16_t EntityFromCallsign(FName Callsign)
	{
		if (Callsign.IsNone()) { return 0; }
		const FString S = Callsign.ToString();
		const FTCHARToUTF8 Utf8(*S);
		return ClearanceDIS::HashCallsignToEntityNumber(std::string_view(Utf8.Get(), Utf8.Length()));
	}

	inline uint8 ForceIdFor(EThreatClass T)
	{
		switch (T)
		{
		case EThreatClass::Friendly: return 1;
		case EThreatClass::Hostile:  return 2;
		case EThreatClass::Neutral:  return 3;
		case EThreatClass::Unknown:
		default:                     return 0;
		}
	}

	inline std::string AsciiString(const FString& S)
	{
		const FTCHARToUTF8 Conv(*S);
		return std::string(Conv.Get(), Conv.Length());
	}

	inline ClearanceRTI::WireHeader MakeHeader(int32 ExerciseId, int32 SiteId, int32 ApplicationId, float SimTimeSeconds)
	{
		// RTI's Modern C++ generator emits POD-style public fields, unlike
		// Fast DDS which emits fluent setter methods. Direct assignment. - TripleA
		ClearanceRTI::WireHeader H;
		H.ExerciseId     = static_cast<uint8_t>(ExerciseId);
		H.SiteId         = static_cast<uint16_t>(SiteId);
		H.ApplicationId  = static_cast<uint16_t>(ApplicationId);
		H.SimTimeSeconds = static_cast<double>(SimTimeSeconds);
		return H;
	}
}

using namespace ClearanceRTIEmitterHelpers;

UClearanceRTIEmitter::UClearanceRTIEmitter() = default;

UClearanceRTIEmitter::~UClearanceRTIEmitter()
{
	// Manual delete since Publisher is a raw pointer (see header for why -
	// TUniquePtr with a forward-declared type doesn't survive the UHT-
	// generated destructor instantiation). Guarded so a partially-
	// constructed emitter also tears down safely. - TripleA
	delete Publisher;
	Publisher = nullptr;
}

bool UClearanceRTIEmitter::Start(int32 DomainId)
{
	delete Publisher;
	Publisher = ClearanceRTI::FClearanceRTIPublisher::Create(static_cast<std::uint32_t>(DomainId)).release();
	if (Publisher)
	{
		UE_LOG(LogTemp, Display, TEXT("[RTI] Publisher on domain %d -> OK"), DomainId);
	}
	else
	{
		const std::string Err = ClearanceRTI::FClearanceRTIPublisher::GetLastCreateError();
		UE_LOG(LogTemp, Error, TEXT("[RTI] Publisher on domain %d -> FAILED: %s"),
			DomainId, *FString(Err.c_str()));
	}
	return Publisher != nullptr;
}

void UClearanceRTIEmitter::Stop()
{
	delete Publisher;
	Publisher = nullptr;
}

bool UClearanceRTIEmitter::IsRunning() const
{
	return Publisher != nullptr;
}

int32 UClearanceRTIEmitter::GetTotalPublishedCount() const
{
	return Publisher ? static_cast<int32>(Publisher->GetTotalPublishedCount()) : 0;
}

void UClearanceRTIEmitter::EmitStates(const TArray<FAircraftState>& States, float SimTimeSeconds)
{
	if (!Publisher) { return; }

	// Once-per-second diagnostic so the log shows the RTI wire's activity
	// distinct from the DDS + DIS lines that already fire. Matches the
	// DDS emitter's log shape so tail -f side-by-side is readable. - TripleA
	static double LastEmitLogTime = 0.0;
	const double NowSec = FPlatformTime::Seconds();
	if (NowSec - LastEmitLogTime > 1.0)
	{
		LastEmitLogTime = NowSec;
		FString Callsigns;
		for (int32 i = 0; i < FMath::Min(3, States.Num()); ++i)
		{
			Callsigns += (i > 0 ? TEXT(",") : TEXT("")) + States[i].Callsign.ToString();
		}
		UE_LOG(LogTemp, Display,
			TEXT("[RTI] EmitStates SiteId=%d NumStates=%d Sample=[%s] PublishedTotal=%llu"),
			SiteId, States.Num(), *Callsigns,
			(unsigned long long)Publisher->GetTotalPublishedCount());
	}

	for (const FAircraftState& S : States)
	{
		if (!S.bIsValid) { continue; }

		ClearanceRTI::AircraftState M;
		M.Header            = MakeHeader(ExerciseId, SiteId, ApplicationId, SimTimeSeconds);
		M.EntityNumber      = EntityFromCallsign(S.Callsign);
		M.Marking           = AsciiString(S.Callsign.ToString());
		M.ForceId           = ForceIdFor(S.ThreatClass);
		M.EntityKind        = 1;
		M.EntityDomain      = 2;
		M.EntityCountry     = 225;
		M.EntityCategory    = 1;
		M.EntitySubcategory = 0;
		M.EntitySpecific    = 0;
		M.EntityExtra       = 0;
		M.XMeters           = double(S.Position.X) * 1852.0;
		M.YMeters           = double(S.Position.Y) * 1852.0;
		M.ZMeters           = double(S.Altitude)   * 0.3048;

		const float HeadingRad = FMath::DegreesToRadians(S.Heading);
		const float SpeedMps   = S.Speed * 0.514444f;
		M.VxMps    = SpeedMps * FMath::Sin(HeadingRad);
		M.VyMps    = SpeedMps * FMath::Cos(HeadingRad);
		M.VzMps    = static_cast<float>(S.ClimbRate * 0.00508);
		M.PsiRad   = HeadingRad;
		M.ThetaRad = 0.f;
		M.PhiRad   = FMath::DegreesToRadians(S.BankAngle);

		// ATC extension fields - carry the ATC-specific state peers need
		// to render the full picture (threat class, emergency, squawk,
		// phase). Same field set as DDS. - TripleA
		M.TrueAffiliation = ForceIdFor(S.TrueAffiliation);
		M.SquawkCode      = static_cast<uint16_t>(S.SquawkCode);
		M.ActiveEmergency = static_cast<uint8_t>(S.ActiveEmergency);
		M.FlightPhase     = static_cast<uint8_t>(S.FlightPhase);

		Publisher->PublishAircraftState(M);
	}
}
