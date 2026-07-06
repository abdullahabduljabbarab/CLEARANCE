// Unreal-side adapter for the ClearanceDIS module. This file exists to bridge
// Unreal's FAircraftState / FVoiceCommsEvent / FRadioTransmitter structs to
// the pure-C++ POD types the wire format layer speaks, then push the resulting
// byte buffer over a UDP socket. All actual serialisation lives in the
// ClearanceDIS module - this file is transport + type conversion only. Refactor
// isolates the spec-conformant wire code from the render layer so the same
// module can back a headless federate, a Unity client, or an HLA bridge later.
// - TripleA

#include "Simulation/ClearanceDISEmitter.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Common/UdpSocketBuilder.h"

#include "ClearanceDIS/ClearanceDISPDU.h"

#include <string>
#include <vector>

namespace
{
	// Convert an Unreal callsign (FName) into a stable DIS entity number.
	// Delegates to the pure-C++ FNV-1a hash so the mapping is identical
	// across Unreal and non-Unreal builds. - TripleA
	inline std::uint16_t EntityFromCallsign(FName Callsign)
	{
		if (Callsign.IsNone()) { return 0; }
		const FString S = Callsign.ToString();
		const FTCHARToUTF8 Utf8(*S);
		const std::string_view View(Utf8.Get(), Utf8.Length());
		return ClearanceDIS::HashCallsignToEntityNumber(View);
	}

	// Aircraft-class -> DIS Entity Type (Annex A). Kept table-driven here on
	// the Unreal side; the DIS module doesn't need to know CLEARANCE's own
	// wake-vortex categories. - TripleA
	struct FEntityTypeSubfields
	{
		uint8 Kind, Domain;
		uint16 Country;
		uint8 Category, Subcategory, Specific, Extra;
	};
	FEntityTypeSubfields EntityTypeFor(EWakeCategory C)
	{
		switch (C)
		{
		case EWakeCategory::Light:  return {1, 2, 225, 1,  8, 1,  0};
		case EWakeCategory::Heavy:  return {1, 2, 225, 1, 22, 8,  0};
		case EWakeCategory::Super:  return {1, 2, 224, 1, 22, 12, 0};
		case EWakeCategory::Medium:
		default:                    return {1, 2, 225, 1, 22, 5,  0};
		}
	}

	// Map CLEARANCE's ThreatClass onto the DIS Force ID field per §7.3.4.6.
	uint8 ForceIdFor(EThreatClass T)
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

	// Copy a std::vector<uint8> into Unreal's TArray<uint8> for the UDP send.
	// One O(N) copy per PDU - negligible next to the socket call. - TripleA
	inline void CopyPodToTArray(const std::vector<std::uint8_t>& Src, TArray<uint8>& Dst)
	{
		Dst.SetNumUninitialized(Src.size());
		if (!Src.empty())
		{
			FMemory::Memcpy(Dst.GetData(), Src.data(), Src.size());
		}
	}

	// Every call site needs the same FWireParams built from the emitter's
	// site / app / exercise + the current sim time. - TripleA
	inline ClearanceDIS::FWireParams MakeParams(int32 SiteId, int32 ApplicationId, int32 ExerciseId, float SimTimeSeconds)
	{
		ClearanceDIS::FWireParams P;
		P.ExerciseId    = static_cast<std::uint8_t>(ExerciseId);
		P.SiteId        = static_cast<std::uint16_t>(SiteId);
		P.ApplicationId = static_cast<std::uint16_t>(ApplicationId);
		P.SimTimeSeconds = static_cast<double>(SimTimeSeconds);
		return P;
	}

	inline std::string ToAsciiString(const FString& S)
	{
		const FTCHARToUTF8 Conv(*S);
		return std::string(Conv.Get(), Conv.Length());
	}
}

bool UClearanceDISEmitter::Start(const FString& Host, int32 Port)
{
	Stop();

	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS) { return false; }

	Socket = FUdpSocketBuilder(TEXT("ClearanceDISEmitter"))
		.AsReusable()
		.WithBroadcast()
		.WithSendBufferSize(64 * 1024)
		.Build();
	if (!Socket) { return false; }

	TargetAddr = SS->CreateInternetAddr();
	bool bAddrOk = false;
	TargetAddr->SetIp(*Host, bAddrOk);
	if (!bAddrOk)
	{
		Socket->Close();
		SS->DestroySocket(Socket);
		Socket = nullptr;
		return false;
	}
	TargetAddr->SetPort(Port);

	LastPacketsSent = 0;
	return true;
}

void UClearanceDISEmitter::Stop()
{
	if (Socket)
	{
		Socket->Close();
		if (ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM))
		{
			SS->DestroySocket(Socket);
		}
		Socket = nullptr;
	}
	TargetAddr.Reset();
}

void UClearanceDISEmitter::EmitStates(const TArray<FAircraftState>& States, float SimTimeSeconds)
{
	if (!Socket || !TargetAddr.IsValid()) { return; }
	const ClearanceDIS::FWireParams Params = MakeParams(SiteId, ApplicationId, ExerciseId, SimTimeSeconds);

	TArray<uint8> Buf;
	for (const FAircraftState& S : States)
	{
		if (!S.bIsValid) { continue; }

		ClearanceDIS::FEntityState PodS;
		PodS.EntityNumber = EntityFromCallsign(S.Callsign);
		PodS.ForceId      = ForceIdFor(S.ThreatClass);

		const FEntityTypeSubfields ET = EntityTypeFor(S.WakeCategory);
		PodS.EntityKind        = ET.Kind;
		PodS.EntityDomain      = ET.Domain;
		PodS.EntityCountry     = ET.Country;
		PodS.EntityCategory    = ET.Category;
		PodS.EntitySubcategory = ET.Subcategory;
		PodS.EntitySpecific    = ET.Specific;
		PodS.EntityExtra       = ET.Extra;

		PodS.XMeters = double(S.Position.X) * 1852.0;
		PodS.YMeters = double(S.Position.Y) * 1852.0;
		PodS.ZMeters = double(S.Altitude)   * 0.3048;

		const float HeadingRad = FMath::DegreesToRadians(S.Heading);
		const float SpeedMps   = S.Speed * 0.514444f;
		PodS.VxMps    = SpeedMps * FMath::Sin(HeadingRad);
		PodS.VyMps    = SpeedMps * FMath::Cos(HeadingRad);
		PodS.VzMps    = static_cast<float>(S.ClimbRate * 0.00508);
		PodS.PsiRad   = HeadingRad;
		PodS.ThetaRad = 0.f;
		PodS.PhiRad   = FMath::DegreesToRadians(S.BankAngle);

		PodS.Marking = ToAsciiString(S.Callsign.ToString());

		CopyPodToTArray(ClearanceDIS::BuildEntityStatePDU(PodS, Params), Buf);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}

void UClearanceDISEmitter::EmitEmissions(const TArray<FRadarEmissionSnapshot>& Radars, float SimTimeSeconds)
{
	if (!Socket || !TargetAddr.IsValid()) { return; }
	const ClearanceDIS::FWireParams Params = MakeParams(SiteId, ApplicationId, ExerciseId, SimTimeSeconds);

	TArray<uint8> Buf;
	for (const FRadarEmissionSnapshot& R : Radars)
	{
		if (!R.bEnabled) { continue; }

		ClearanceDIS::FEmissionSnapshot Pod;
		Pod.EmittingEntity  = EntityFromCallsign(R.SiteName);
		Pod.PositionMetersX = double(R.SitePositionNm.X) * 1852.0;
		Pod.PositionMetersY = double(R.SitePositionNm.Y) * 1852.0;
		Pod.PositionMetersZ = 0.0;

		Pod.EmitterName             = static_cast<std::uint16_t>(R.Signature.EmitterName);
		Pod.EmitterFunction         = R.Signature.EmitterFunction;
		Pod.FrequencyLowHz          = R.Signature.FrequencyLowHz;
		Pod.FrequencyHighHz         = R.Signature.FrequencyHighHz;
		Pod.EffectiveRadiatedPowerDbm = R.Signature.EffectiveRadiatedPowerDbm;
		Pod.PulseRepetitionFreqHz   = R.Signature.PulseRepetitionFreqHz;
		Pod.PulseWidthMicrosec      = R.Signature.PulseWidthMicrosec;
		Pod.BeamAzimuthRad          = FMath::DegreesToRadians(R.SweepAngleDeg);
		Pod.BeamFunction            = R.Signature.BeamFunction;

		Pod.PaintedEntityNumbers.reserve(R.PaintedCallsigns.Num());
		for (const FName& C : R.PaintedCallsigns)
		{
			const std::uint16_t E = EntityFromCallsign(C);
			if (E != 0) { Pod.PaintedEntityNumbers.push_back(E); }
		}

		CopyPodToTArray(ClearanceDIS::BuildEmissionPDU(Pod, Params), Buf);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}

void UClearanceDISEmitter::EmitFireEvents(const TArray<FWeaponsFireEvent>& Events, float SimTimeSeconds)
{
	if (!Socket || !TargetAddr.IsValid()) { return; }
	const ClearanceDIS::FWireParams Params = MakeParams(SiteId, ApplicationId, ExerciseId, SimTimeSeconds);

	TArray<uint8> Buf;
	for (const FWeaponsFireEvent& E : Events)
	{
		ClearanceDIS::FFireEvent Pod;
		Pod.FiringEntity   = EntityFromCallsign(E.FiringCallsign);
		Pod.TargetEntity   = EntityFromCallsign(E.TargetCallsign);
		Pod.EventNumber    = static_cast<std::uint16_t>(E.EventNumber & 0xFFFFu);
		Pod.MunitionEntity = ClearanceDIS::DeriveMunitionEntityNumber(Pod.FiringEntity, static_cast<std::uint32_t>(E.EventNumber));

		Pod.XMeters = double(E.LocationNm.X) * 1852.0;
		Pod.YMeters = double(E.LocationNm.Y) * 1852.0;
		Pod.ZMeters = double(E.AltitudeFt)   * 0.3048;
		Pod.VxMps   = E.VelocityXKts * 0.514444f;
		Pod.VyMps   = E.VelocityYKts * 0.514444f;
		Pod.VzMps   = E.VelocityZKts * 0.514444f;

		Pod.MunitionKind = E.MunitionKind;
		Pod.WarheadKind  = static_cast<std::uint16_t>(E.WarheadKind);
		Pod.FuseKind     = static_cast<std::uint16_t>(E.FuseKind);
		Pod.Quantity     = static_cast<std::uint16_t>(E.Quantity);
		Pod.Rate         = static_cast<std::uint16_t>(E.Rate);
		Pod.RangeMeters  = E.RangeMeters;

		CopyPodToTArray(ClearanceDIS::BuildFirePDU(Pod, Params), Buf);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}

void UClearanceDISEmitter::EmitDetonationEvents(const TArray<FWeaponsDetonationEvent>& Events, float SimTimeSeconds)
{
	if (!Socket || !TargetAddr.IsValid()) { return; }
	const ClearanceDIS::FWireParams Params = MakeParams(SiteId, ApplicationId, ExerciseId, SimTimeSeconds);

	TArray<uint8> Buf;
	for (const FWeaponsDetonationEvent& E : Events)
	{
		ClearanceDIS::FDetonationEvent Pod;
		Pod.FiringEntity   = EntityFromCallsign(E.FiringCallsign);
		Pod.TargetEntity   = EntityFromCallsign(E.TargetCallsign);
		Pod.EventNumber    = static_cast<std::uint16_t>(E.EventNumber & 0xFFFFu);
		Pod.MunitionEntity = ClearanceDIS::DeriveMunitionEntityNumber(Pod.FiringEntity, static_cast<std::uint32_t>(E.EventNumber));

		Pod.XMeters = double(E.LocationNm.X) * 1852.0;
		Pod.YMeters = double(E.LocationNm.Y) * 1852.0;
		Pod.ZMeters = double(E.AltitudeFt)   * 0.3048;
		Pod.VxMps   = E.VelocityXKts * 0.514444f;
		Pod.VyMps   = E.VelocityYKts * 0.514444f;
		Pod.VzMps   = E.VelocityZKts * 0.514444f;

		Pod.MunitionKind = E.MunitionKind;
		Pod.WarheadKind  = static_cast<std::uint16_t>(E.WarheadKind);
		Pod.FuseKind     = static_cast<std::uint16_t>(E.FuseKind);
		Pod.Quantity     = static_cast<std::uint16_t>(E.Quantity);
		Pod.Rate         = static_cast<std::uint16_t>(E.Rate);
		Pod.DetonationResult = E.DetonationResult;

		CopyPodToTArray(ClearanceDIS::BuildDetonationPDU(Pod, Params), Buf);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}

void UClearanceDISEmitter::EmitVoiceEvents(const TArray<FVoiceCommsEvent>& Events, float SimTimeSeconds)
{
	if (!Socket || !TargetAddr.IsValid()) { return; }
	const ClearanceDIS::FWireParams Params = MakeParams(SiteId, ApplicationId, ExerciseId, SimTimeSeconds);

	TArray<uint8> Buf;
	for (const FVoiceCommsEvent& E : Events)
	{
		ClearanceDIS::FSignalEvent Pod;
		Pod.OwnerEntity = E.SpeakerCallsign.IsNone()
			? ClearanceDIS::kOperatorGroundStationEntity
			: EntityFromCallsign(E.SpeakerCallsign);
		Pod.RadioId = static_cast<std::uint16_t>(E.RadioId);

		const FTCHARToUTF8 Utf8(*E.Transcript);
		Pod.Data.assign(reinterpret_cast<const std::uint8_t*>(Utf8.Get()),
		                reinterpret_cast<const std::uint8_t*>(Utf8.Get()) + Utf8.Length());

		CopyPodToTArray(ClearanceDIS::BuildSignalPDU(Pod, Params), Buf);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}

void UClearanceDISEmitter::EmitTransmitters(const TArray<FRadioTransmitter>& Transmitters, float SimTimeSeconds)
{
	if (!Socket || !TargetAddr.IsValid()) { return; }
	const ClearanceDIS::FWireParams Params = MakeParams(SiteId, ApplicationId, ExerciseId, SimTimeSeconds);

	TArray<uint8> Buf;
	for (const FRadioTransmitter& R : Transmitters)
	{
		ClearanceDIS::FTransmitterState Pod;
		Pod.OwnerEntity = R.OwnerCallsign.IsNone()
			? ClearanceDIS::kOperatorGroundStationEntity
			: EntityFromCallsign(R.OwnerCallsign);
		Pod.RadioId       = static_cast<std::uint16_t>(R.RadioId);
		Pod.FrequencyHz   = static_cast<std::uint64_t>(R.FrequencyHz);
		Pod.BandwidthHz   = R.BandwidthHz;
		Pod.PowerDbm      = R.PowerDbm;
		Pod.TransmitState = R.TransmitState;
		Pod.AntennaXMeters = R.AntennaWorldMeters.X;
		Pod.AntennaYMeters = R.AntennaWorldMeters.Y;
		Pod.AntennaZMeters = R.AntennaWorldMeters.Z;

		CopyPodToTArray(ClearanceDIS::BuildTransmitterPDU(Pod, Params), Buf);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}
