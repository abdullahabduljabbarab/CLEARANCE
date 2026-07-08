// Unreal-side adapter around ClearanceHLA. Every EmitStates call converts
// each FAircraftState to the engine-free AircraftStateWire POD and hands
// it to the pure-C++ FClearanceHLAFederate facade, which encodes attributes
// via the rti1516e encoding helpers and pushes them through the
// RTIambassador. All ECEF-metres / m/s conversions happen at this boundary,
// matching the DIS + DDS + RTI adapters' pattern - one authoritative set
// of units at the sim boundary. - TripleA

#include "Simulation/ClearanceHLAEmitter.h"

#include "ClearanceHLA/ClearanceHLAFederate.h"
#include "ClearanceDIS/ClearanceDISPDU.h"   // reuse the FNV-1a callsign hash

// Named namespace (not anonymous) - same rationale as
// ClearanceDISEmitterHelpers etc.: prevents C2084 collisions when unity-
// build merges these adapter TUs. - TripleA
namespace ClearanceHLAEmitterHelpers
{
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

	inline std::string ToStdString(const FString& S)
	{
		const FTCHARToUTF8 Conv(*S);
		return std::string(Conv.Get(), Conv.Length());
	}
}
// No file-scope `using namespace` here on purpose - the sibling emitter TUs
// (DIS/DDS/RTI) each have their own named-namespace helpers with the same
// function names, and unity-build concatenation of 4 such `using` directives
// makes every unqualified call ambiguous. HLA's helper calls below are fully
// qualified. - TripleA

UClearanceHLAEmitter::UClearanceHLAEmitter() = default;

UClearanceHLAEmitter::~UClearanceHLAEmitter()
{
	delete Federate;
	Federate = nullptr;
}

bool UClearanceHLAEmitter::Join(const FString& FederationName, const FString& FederateName, const FString& FomModulePath)
{
	delete Federate;

	auto Owned = ClearanceHLA::FClearanceHLAFederate::Join(
		ClearanceHLAEmitterHelpers::ToStdString(FederationName),
		ClearanceHLAEmitterHelpers::ToStdString(FederateName),
		ClearanceHLAEmitterHelpers::ToStdString(FomModulePath));

	Federate = Owned.release();

	if (Federate)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[HLA] Joined federation '%s' as '%s' with FOM '%s'"),
			*FederationName, *FederateName, *FomModulePath);
	}
	else
	{
		const std::string Err = ClearanceHLA::FClearanceHLAFederate::GetLastJoinError();
		UE_LOG(LogTemp, Error,
			TEXT("[HLA] Join FAILED on federation '%s': %s"),
			*FederationName, *FString(Err.c_str()));
	}
	return Federate != nullptr;
}

void UClearanceHLAEmitter::Resign()
{
	if (Federate)
	{
		Federate->Resign();
	}
	delete Federate;
	Federate = nullptr;
	UE_LOG(LogTemp, Display, TEXT("[HLA] Resigned"));
}

bool UClearanceHLAEmitter::IsJoined() const
{
	return Federate && Federate->IsJoined();
}

int32 UClearanceHLAEmitter::GetTotalUpdatesCount() const
{
	return Federate ? static_cast<int32>(Federate->GetTotalUpdatesCount()) : 0;
}

void UClearanceHLAEmitter::EmitStates(const TArray<FAircraftState>& States, float SimTimeSeconds)
{
	if (!IsJoined()) { return; }

	// Once-per-second diagnostic. Matches DIS/DDS/RTI adapters' log
	// shape so `tail -f` output stacks readably. - TripleA
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
			TEXT("[HLA] EmitStates SiteId=%d NumStates=%d Sample=[%s] UpdatesTotal=%llu"),
			SiteId, States.Num(), *Callsigns,
			(unsigned long long)Federate->GetTotalUpdatesCount());
	}

	for (const FAircraftState& S : States)
	{
		if (!S.bIsValid) { continue; }

		ClearanceHLA::AircraftStateWire W;
		W.Callsign        = ClearanceHLAEmitterHelpers::ToStdString(S.Callsign.ToString());
		W.EntityNumber    = ClearanceHLAEmitterHelpers::EntityFromCallsign(S.Callsign);
		W.SiteId          = static_cast<std::uint16_t>(SiteId);
		W.ApplicationId   = static_cast<std::uint16_t>(ApplicationId);
		W.ForceId         = ClearanceHLAEmitterHelpers::ForceIdFor(S.ThreatClass);
		W.TrueAffiliation = ClearanceHLAEmitterHelpers::ForceIdFor(S.TrueAffiliation);
		W.SquawkCode      = static_cast<std::uint16_t>(S.SquawkCode);
		W.ActiveEmergency = static_cast<std::uint8_t>(S.ActiveEmergency);
		W.FlightPhase     = static_cast<std::uint8_t>(S.FlightPhase);

		W.XMeters = double(S.Position.X) * 1852.0;
		W.YMeters = double(S.Position.Y) * 1852.0;
		W.ZMeters = double(S.Altitude)   * 0.3048;

		const float HeadingRad = FMath::DegreesToRadians(S.Heading);
		const float SpeedMps   = S.Speed * 0.514444f;
		W.VxMps    = SpeedMps * FMath::Sin(HeadingRad);
		W.VyMps    = SpeedMps * FMath::Cos(HeadingRad);
		W.VzMps    = static_cast<float>(S.ClimbRate * 0.00508);
		W.PsiRad   = HeadingRad;
		W.ThetaRad = 0.f;
		W.PhiRad   = FMath::DegreesToRadians(S.BankAngle);

		Federate->PublishAircraftState(W);
	}
}
