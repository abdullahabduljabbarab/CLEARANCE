// Unreal-side adapter around ClearanceDDS. Every Emit* method converts the
// game module's Unreal-shape input into the IDL POD types the pure C++
// publisher speaks, then delegates. All unit conversions (nm/ft/kts to
// metres/m/s) happen at this boundary - identical pattern to the DIS
// emitter, so the two sides always agree on what a metre is. - TripleA

#include "Simulation/ClearanceDDSEmitter.h"

#include "ClearanceDDS/ClearanceDDSPublisher.h"
#include "AirspaceTelemetry.hpp"
#include "ClearanceDIS/ClearanceDISPDU.h"   // reuse the FNV-1a callsign hash

// Named namespace (not anonymous) - avoids C2084 collisions with the DIS +
// RTI emitters when unity-build merges the three TUs. See the same block in
// ClearanceDISEmitter.cpp for the full rationale. - TripleA
namespace ClearanceDDSEmitterHelpers
{
	// Same reserved entity for operator / ground-station traffic that the
	// DIS emitter uses, so both wires filter air-side from ground-side by
	// the same number. Kept in sync deliberately. - TripleA
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

	inline ClearanceDDS::WireHeader MakeHeader(int32 ExerciseId, int32 SiteId, int32 ApplicationId, float SimTimeSeconds)
	{
		ClearanceDDS::WireHeader H;
		H.ExerciseId(static_cast<uint8_t>(ExerciseId));
		H.SiteId(static_cast<uint16_t>(SiteId));
		H.ApplicationId(static_cast<uint16_t>(ApplicationId));
		H.SimTimeSeconds(static_cast<double>(SimTimeSeconds));
		return H;
	}
}

// using directive removed - qualified calls below to avoid unity-build ambiguity - TripleA

UClearanceDDSEmitter::UClearanceDDSEmitter() = default;
UClearanceDDSEmitter::~UClearanceDDSEmitter() = default;

bool UClearanceDDSEmitter::Start(int32 DomainId)
{
	Publisher = TUniquePtr<ClearanceDDS::FClearancePublisher>(
		ClearanceDDS::FClearancePublisher::Create(static_cast<std::uint32_t>(DomainId)).release());
	UE_LOG(LogTemp, Display, TEXT("[DDS] Publisher on domain %d -> %s"),
		DomainId, Publisher.IsValid() ? TEXT("OK") : TEXT("FAILED"));
	return Publisher.IsValid();
}

void UClearanceDDSEmitter::Stop()
{
	Publisher.Reset();
}

bool UClearanceDDSEmitter::IsRunning() const
{
	return Publisher.IsValid();
}

int32 UClearanceDDSEmitter::GetTotalPublishedCount() const
{
	return Publisher.IsValid() ? static_cast<int32>(Publisher->GetTotalPublishedCount()) : 0;
}

void UClearanceDDSEmitter::EmitStates(const TArray<FAircraftState>& States, float SimTimeSeconds)
{
	if (!Publisher.IsValid()) { return; }

	// Diagnostic: log ONCE PER SECOND showing what THIS instance is publishing.
	// Time-gated so it doesn't flood. Two instances writing here will produce
	// two distinguishable log lines with their own SiteId. - TripleA
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
			TEXT("[DDS] EmitStates SiteId=%d NumStates=%d Sample=[%s] PublishedTotal=%llu"),
			SiteId, States.Num(), *Callsigns,
			(unsigned long long)Publisher->GetTotalPublishedCount());
	}

	for (const FAircraftState& S : States)
	{
		if (!S.bIsValid) { continue; }

		ClearanceDDS::AircraftState M;
		M.Header(ClearanceDDSEmitterHelpers::MakeHeader(ExerciseId, SiteId, ApplicationId, SimTimeSeconds));
		M.EntityNumber(ClearanceDDSEmitterHelpers::EntityFromCallsign(S.Callsign));
		M.Marking(ClearanceDDSEmitterHelpers::AsciiString(S.Callsign.ToString()));
		M.ForceId(ClearanceDDSEmitterHelpers::ForceIdFor(S.ThreatClass));
		M.EntityKind(1);
		M.EntityDomain(2);
		M.EntityCountry(224);
		M.EntityCategory(1);
		M.EntitySubcategory(0);
		M.EntitySpecific(0);
		M.EntityExtra(0);
		M.XMeters(double(S.Position.X) * 1852.0);
		M.YMeters(double(S.Position.Y) * 1852.0);
		M.ZMeters(double(S.Altitude)   * 0.3048);

		// ENU velocity: Vx = East = sin(H), Vy = North = cos(H). - TripleA
		const float HeadingRad = FMath::DegreesToRadians(S.Heading);
		const float SpeedMps   = S.Speed * 0.514444f;
		M.VxMps(SpeedMps * FMath::Sin(HeadingRad));
		M.VyMps(SpeedMps * FMath::Cos(HeadingRad));
		M.VzMps(static_cast<float>(S.ClimbRate * 0.00508));
		M.PsiRad(HeadingRad);
		M.ThetaRad(0.f);
		M.PhiRad(FMath::DegreesToRadians(S.BankAngle));

		// ATC extension fields - carry the ATC-specific state peers need to
		// render the full picture (threat class, emergency, squawk, phase). - TripleA
		M.TrueAffiliation(ClearanceDDSEmitterHelpers::ForceIdFor(S.TrueAffiliation));
		M.SquawkCode(static_cast<uint16_t>(S.SquawkCode));
		M.ActiveEmergency(static_cast<uint8_t>(S.ActiveEmergency));
		M.FlightPhase(static_cast<uint8_t>(S.FlightPhase));

		Publisher->PublishAircraftState(M);
	}
}

void UClearanceDDSEmitter::EmitEmissions(const TArray<FRadarEmissionSnapshot>& Radars, float SimTimeSeconds)
{
	if (!Publisher.IsValid()) { return; }
	for (const FRadarEmissionSnapshot& R : Radars)
	{
		if (!R.bEnabled) { continue; }

		ClearanceDDS::EmissionSnapshot M;
		M.Header(ClearanceDDSEmitterHelpers::MakeHeader(ExerciseId, SiteId, ApplicationId, SimTimeSeconds));
		M.EmittingEntity(ClearanceDDSEmitterHelpers::EntityFromCallsign(R.SiteName));
		M.PositionMetersX(double(R.SitePositionNm.X) * 1852.0);
		M.PositionMetersY(double(R.SitePositionNm.Y) * 1852.0);
		M.PositionMetersZ(0.0);
		M.EmitterName(static_cast<uint16_t>(R.Signature.EmitterName));
		M.EmitterFunction(R.Signature.EmitterFunction);
		M.FrequencyLowHz(R.Signature.FrequencyLowHz);
		M.FrequencyHighHz(R.Signature.FrequencyHighHz);
		M.EffectiveRadiatedPowerDbm(R.Signature.EffectiveRadiatedPowerDbm);
		M.PulseRepetitionFreqHz(R.Signature.PulseRepetitionFreqHz);
		M.PulseWidthMicrosec(R.Signature.PulseWidthMicrosec);
		M.BeamAzimuthRad(FMath::DegreesToRadians(R.SweepAngleDeg));
		M.BeamFunction(R.Signature.BeamFunction);

		std::vector<uint16_t> Painted;
		Painted.reserve(R.PaintedCallsigns.Num());
		for (const FName& C : R.PaintedCallsigns)
		{
			const std::uint16_t E = ClearanceDDSEmitterHelpers::EntityFromCallsign(C);
			if (E != 0) { Painted.push_back(E); }
		}
		M.PaintedEntityNumbers(std::move(Painted));

		Publisher->PublishEmissionSnapshot(M);
	}
}

void UClearanceDDSEmitter::EmitFireEvents(const TArray<FWeaponsFireEvent>& Events, float SimTimeSeconds)
{
	if (!Publisher.IsValid()) { return; }
	for (const FWeaponsFireEvent& E : Events)
	{
		const std::uint16_t Firer  = ClearanceDDSEmitterHelpers::EntityFromCallsign(E.FiringCallsign);
		const std::uint16_t Target = ClearanceDDSEmitterHelpers::EntityFromCallsign(E.TargetCallsign);

		ClearanceDDS::FireEvent M;
		M.Header(ClearanceDDSEmitterHelpers::MakeHeader(ExerciseId, SiteId, ApplicationId, SimTimeSeconds));
		M.FiringEntity(Firer);
		M.TargetEntity(Target);
		M.EventNumber(static_cast<uint16_t>(E.EventNumber & 0xFFFFu));
		M.MunitionEntity(E.MunitionEntityId != 0
			? static_cast<uint16_t>(E.MunitionEntityId & 0xFFFFu)
			: ClearanceDIS::DeriveMunitionEntityNumber(Firer, static_cast<std::uint32_t>(E.EventNumber)));
		M.XMeters(double(E.LocationNm.X) * 1852.0);
		M.YMeters(double(E.LocationNm.Y) * 1852.0);
		M.ZMeters(double(E.AltitudeFt)   * 0.3048);
		M.VxMps(E.VelocityXKts * 0.514444f);
		M.VyMps(E.VelocityYKts * 0.514444f);
		M.VzMps(E.VelocityZKts * 0.514444f);
		M.MunitionKind(E.MunitionKind);
		M.WarheadKind(static_cast<uint16_t>(E.WarheadKind));
		M.FuseKind(static_cast<uint16_t>(E.FuseKind));
		M.Quantity(static_cast<uint16_t>(E.Quantity));
		M.Rate(static_cast<uint16_t>(E.Rate));
		M.RangeMeters(E.RangeMeters);

		Publisher->PublishFireEvent(M);
	}
}

void UClearanceDDSEmitter::EmitDetonationEvents(const TArray<FWeaponsDetonationEvent>& Events, float SimTimeSeconds)
{
	if (!Publisher.IsValid()) { return; }
	for (const FWeaponsDetonationEvent& E : Events)
	{
		const std::uint16_t Firer  = ClearanceDDSEmitterHelpers::EntityFromCallsign(E.FiringCallsign);
		const std::uint16_t Target = ClearanceDDSEmitterHelpers::EntityFromCallsign(E.TargetCallsign);

		ClearanceDDS::DetonationEvent M;
		M.Header(ClearanceDDSEmitterHelpers::MakeHeader(ExerciseId, SiteId, ApplicationId, SimTimeSeconds));
		M.FiringEntity(Firer);
		M.TargetEntity(Target);
		M.EventNumber(static_cast<uint16_t>(E.EventNumber & 0xFFFFu));
		M.MunitionEntity(E.MunitionEntityId != 0
			? static_cast<uint16_t>(E.MunitionEntityId & 0xFFFFu)
			: ClearanceDIS::DeriveMunitionEntityNumber(Firer, static_cast<std::uint32_t>(E.EventNumber)));
		M.XMeters(double(E.LocationNm.X) * 1852.0);
		M.YMeters(double(E.LocationNm.Y) * 1852.0);
		M.ZMeters(double(E.AltitudeFt)   * 0.3048);
		M.VxMps(E.VelocityXKts * 0.514444f);
		M.VyMps(E.VelocityYKts * 0.514444f);
		M.VzMps(E.VelocityZKts * 0.514444f);
		M.MunitionKind(E.MunitionKind);
		M.WarheadKind(static_cast<uint16_t>(E.WarheadKind));
		M.FuseKind(static_cast<uint16_t>(E.FuseKind));
		M.Quantity(static_cast<uint16_t>(E.Quantity));
		M.Rate(static_cast<uint16_t>(E.Rate));
		M.DetonationResult(E.DetonationResult);

		Publisher->PublishDetonationEvent(M);
	}
}

void UClearanceDDSEmitter::EmitVoiceEvents(const TArray<FVoiceCommsEvent>& Events, float SimTimeSeconds)
{
	if (!Publisher.IsValid()) { return; }
	for (const FVoiceCommsEvent& E : Events)
	{
		ClearanceDDS::SignalEvent M;
		M.Header(ClearanceDDSEmitterHelpers::MakeHeader(ExerciseId, SiteId, ApplicationId, SimTimeSeconds));
		M.OwnerEntity(E.SpeakerCallsign.IsNone()
			? ClearanceDDSEmitterHelpers::kOperatorGroundStationEntity
			: ClearanceDDSEmitterHelpers::EntityFromCallsign(E.SpeakerCallsign));
		M.RadioId(static_cast<uint16_t>(E.RadioId));

		const FTCHARToUTF8 Utf8(*E.Transcript);
		std::vector<uint8_t> Payload(reinterpret_cast<const uint8_t*>(Utf8.Get()),
			reinterpret_cast<const uint8_t*>(Utf8.Get()) + Utf8.Length());
		M.Data(std::move(Payload));

		Publisher->PublishSignalEvent(M);
	}
}

void UClearanceDDSEmitter::EmitTransmitters(const TArray<FRadioTransmitter>& Transmitters, float SimTimeSeconds)
{
	if (!Publisher.IsValid()) { return; }
	for (const FRadioTransmitter& R : Transmitters)
	{
		ClearanceDDS::TransmitterState M;
		M.Header(ClearanceDDSEmitterHelpers::MakeHeader(ExerciseId, SiteId, ApplicationId, SimTimeSeconds));
		M.OwnerEntity(R.OwnerCallsign.IsNone()
			? ClearanceDDSEmitterHelpers::kOperatorGroundStationEntity
			: ClearanceDDSEmitterHelpers::EntityFromCallsign(R.OwnerCallsign));
		M.RadioId(static_cast<uint16_t>(R.RadioId));
		M.FrequencyHz(static_cast<uint64_t>(R.FrequencyHz));
		M.BandwidthHz(R.BandwidthHz);
		M.PowerDbm(R.PowerDbm);
		M.TransmitState(R.TransmitState);
		M.AntennaXMeters(R.AntennaWorldMeters.X);
		M.AntennaYMeters(R.AntennaWorldMeters.Y);
		M.AntennaZMeters(R.AntennaWorldMeters.Z);

		Publisher->PublishTransmitterState(M);
	}
}
