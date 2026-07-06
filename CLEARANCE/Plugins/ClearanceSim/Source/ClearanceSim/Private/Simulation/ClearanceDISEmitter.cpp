#include "Simulation/ClearanceDISEmitter.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Common/UdpSocketBuilder.h"

namespace
{
	// Crude category -> DIS Entity Type (Kind/Domain/Country/Cat/Subcat/Specific/Extra).
	// Kind=1 Platform, Domain=2 Air, Country=225 USA / 224 UK for variety. - TripleA
	struct FDISEntityType
	{
		uint8 Kind, Domain;
		uint16 Country;
		uint8 Category, Subcategory, Specific, Extra;
	};

	FDISEntityType EntityTypeFor(EWakeCategory C)
	{
		switch (C)
		{
		case EWakeCategory::Light:  return {1, 2, 225, 1,  8, 1, 0};  // light civil
		case EWakeCategory::Heavy:  return {1, 2, 225, 1, 22, 8, 0};  // heavy commercial
		case EWakeCategory::Super:  return {1, 2, 224, 1, 22, 12, 0}; // super (A380-class)
		case EWakeCategory::Medium:
		default:                    return {1, 2, 225, 1, 22, 5, 0};  // commercial narrow-body
		}
	}
}

void UClearanceDISEmitter::WriteU8(TArray<uint8>& B, uint8 V) { B.Add(V); }

void UClearanceDISEmitter::WriteU16BE(TArray<uint8>& B, uint16 V)
{
	B.Add((V >> 8) & 0xFF);
	B.Add(V & 0xFF);
}

void UClearanceDISEmitter::WriteU32BE(TArray<uint8>& B, uint32 V)
{
	B.Add((V >> 24) & 0xFF);
	B.Add((V >> 16) & 0xFF);
	B.Add((V >> 8) & 0xFF);
	B.Add(V & 0xFF);
}

void UClearanceDISEmitter::WriteFloatBE(TArray<uint8>& B, float V)
{
	uint32 Bits;
	FMemory::Memcpy(&Bits, &V, sizeof(Bits));
	WriteU32BE(B, Bits);
}

void UClearanceDISEmitter::WriteDoubleBE(TArray<uint8>& B, double V)
{
	uint64 Bits;
	FMemory::Memcpy(&Bits, &V, sizeof(Bits));
	for (int32 i = 7; i >= 0; --i) { B.Add((Bits >> (i * 8)) & 0xFF); }
}

uint8 UClearanceDISEmitter::ReadU8(const TArray<uint8>& B, int32& Cursor, bool& bOk)
{
	if (Cursor + 1 > B.Num()) { bOk = false; return 0; }
	return B[Cursor++];
}

uint16 UClearanceDISEmitter::ReadU16BE(const TArray<uint8>& B, int32& Cursor, bool& bOk)
{
	if (Cursor + 2 > B.Num()) { bOk = false; return 0; }
	const uint16 V = (uint16(B[Cursor]) << 8) | uint16(B[Cursor + 1]);
	Cursor += 2;
	return V;
}

uint32 UClearanceDISEmitter::ReadU32BE(const TArray<uint8>& B, int32& Cursor, bool& bOk)
{
	if (Cursor + 4 > B.Num()) { bOk = false; return 0; }
	const uint32 V = (uint32(B[Cursor]) << 24) | (uint32(B[Cursor + 1]) << 16)
	              | (uint32(B[Cursor + 2]) <<  8) |  uint32(B[Cursor + 3]);
	Cursor += 4;
	return V;
}

float UClearanceDISEmitter::ReadFloatBE(const TArray<uint8>& B, int32& Cursor, bool& bOk)
{
	const uint32 Bits = ReadU32BE(B, Cursor, bOk);
	float V = 0.f;
	FMemory::Memcpy(&V, &Bits, sizeof(V));
	return V;
}

bool UClearanceDISEmitter::Start(const FString& Host, int32 Port)
{
	Stop(); // clean any previous socket

	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS) { return false; }

	const bool bBroadcast = Host.IsEmpty() || Host.Equals(TEXT("broadcast"), ESearchCase::IgnoreCase);

	Socket = FUdpSocketBuilder(TEXT("ClearanceDIS"))
		.AsReusable()
		.WithBroadcast()
		.WithSendBufferSize(256 * 1024)
		.Build();
	if (!Socket) { return false; }

	TargetAddr = SS->CreateInternetAddr();
	if (bBroadcast)
	{
		TargetAddr->SetBroadcastAddress();
	}
	else
	{
		bool bValid = false;
		TargetAddr->SetIp(*Host, bValid);
		if (!bValid)
		{
			UE_LOG(LogTemp, Warning, TEXT("DIS: bad IP '%s'"), *Host);
			Stop();
			return false;
		}
	}
	TargetAddr->SetPort(Port);

	UE_LOG(LogTemp, Display, TEXT("DIS: emitting to %s:%d  (site=%d app=%d ex=%d)"),
		bBroadcast ? TEXT("broadcast") : *Host, Port, SiteId, ApplicationId, ExerciseId);
	return true;
}

void UClearanceDISEmitter::Stop()
{
	if (Socket)
	{
		ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		Socket->Close();
		if (SS) { SS->DestroySocket(Socket); }
		Socket = nullptr;
	}
	TargetAddr.Reset();
}

void UClearanceDISEmitter::EmitStates(const TArray<FAircraftState>& States, float SimTimeSeconds)
{
	LastPacketsSent = 0;
	if (!Socket || !TargetAddr.IsValid()) { return; }

	TArray<uint8> Buf;
	Buf.Reserve(160);

	for (const FAircraftState& S : States)
	{
		// Don't re-broadcast traffic somebody else sent us - that's how federation
		// loops form. They own the truth for their aircraft. - TripleA
		if (S.bIsExternal) { continue; }
		Buf.Reset();
		BuildEntityStatePDU(Buf, S, SimTimeSeconds);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}

void UClearanceDISEmitter::BuildEntityStatePDU(TArray<uint8>& Out, const FAircraftState& S, float SimTimeSeconds) const
{
	// Entity ID = stable per-aircraft from callsign hash, mapped into 1..65535.
	const uint16 EntityNumber = static_cast<uint16>((GetTypeHash(S.Callsign) % 65535) + 1);
	const FDISEntityType ET = EntityTypeFor(S.WakeCategory);

	// DIS timestamp: fraction of an hour past the top of the hour, as a 31-bit value,
	// with the LSB = 0 indicating "relative" (not UTC-synced). - TripleA
	const double SecondsInHour = FMath::Fmod(SimTimeSeconds, 3600.0);
	const uint32 DISTimestamp = static_cast<uint32>((SecondsInHour * (2147483648.0 / 3600.0))) & 0xFFFFFFFE;

	// Sim coords -> DIS world coords (meters). Our sim has X=East/Y=North in nm,
	// altitude in ft. We emit them as ECEF doubles directly - a 'flat earth' DIS
	// simulation, which is normal for non-geographic test environments.
	const double XMeters = static_cast<double>(S.Position.X) * 1852.0;
	const double YMeters = static_cast<double>(S.Position.Y) * 1852.0;
	const double ZMeters = static_cast<double>(S.Altitude) * 0.3048;

	// Velocity (m/s, world axes). Speed kt -> m/s = * 0.514444.
	const float HeadingRad = FMath::DegreesToRadians(S.Heading);
	const float SpeedMps = S.Speed * 0.514444f;
	const float Vx = SpeedMps * FMath::Sin(HeadingRad);            // east
	const float Vy = SpeedMps * FMath::Cos(HeadingRad);            // north
	const float Vz = static_cast<float>(S.ClimbRate * 0.00508);    // ft/min -> m/s

	// Orientation (DIS Euler radians: psi=yaw/heading, theta=pitch, phi=roll/bank).
	const float Psi = HeadingRad;
	const float Theta = 0.f;
	const float Phi = FMath::DegreesToRadians(S.BankAngle);

	const uint16 PduLength = 144;

	// ---- PDU Header (12 bytes) ----
	WriteU8(Out, 6);                              // Protocol version (6 = DIS 1995/1998)
	WriteU8(Out, static_cast<uint8>(ExerciseId)); // Exercise ID
	WriteU8(Out, 1);                              // PDU type 1 = Entity State
	WriteU8(Out, 1);                              // Protocol family 1 = Entity Info
	WriteU32BE(Out, DISTimestamp);
	WriteU16BE(Out, PduLength);
	WriteU16BE(Out, 0);                           // padding

	// ---- Entity ID (6 bytes) ----
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, EntityNumber);

	// ---- Force ID + #articulation params ----
	WriteU8(Out, 0); // Force ID: 0 = Other (civilian)
	WriteU8(Out, 0); // No articulation parameters

	// ---- Entity Type (8 bytes) ----
	WriteU8(Out, ET.Kind);
	WriteU8(Out, ET.Domain);
	WriteU16BE(Out, ET.Country);
	WriteU8(Out, ET.Category);
	WriteU8(Out, ET.Subcategory);
	WriteU8(Out, ET.Specific);
	WriteU8(Out, ET.Extra);

	// ---- Alternative Entity Type (8 bytes) - same as above for simulation use ----
	WriteU8(Out, ET.Kind);
	WriteU8(Out, ET.Domain);
	WriteU16BE(Out, ET.Country);
	WriteU8(Out, ET.Category);
	WriteU8(Out, ET.Subcategory);
	WriteU8(Out, ET.Specific);
	WriteU8(Out, ET.Extra);

	// ---- Entity Linear Velocity (12 bytes, float x/y/z m/s) ----
	WriteFloatBE(Out, Vx);
	WriteFloatBE(Out, Vy);
	WriteFloatBE(Out, Vz);

	// ---- Entity Location (24 bytes, double x/y/z meters) ----
	WriteDoubleBE(Out, XMeters);
	WriteDoubleBE(Out, YMeters);
	WriteDoubleBE(Out, ZMeters);

	// ---- Entity Orientation (12 bytes, float psi/theta/phi radians) ----
	WriteFloatBE(Out, Psi);
	WriteFloatBE(Out, Theta);
	WriteFloatBE(Out, Phi);

	// ---- Entity Appearance (4 bytes bitfield) ----
	WriteU32BE(Out, 0);

	// ---- Dead Reckoning Parameters (40 bytes) ----
	WriteU8(Out, 2); // algorithm 2 = DRM(F,P,W) - constant linear velocity
	for (int32 i = 0; i < 15; ++i) { WriteU8(Out, 0); } // OtherParameters
	// Linear acceleration (m/s^2) - leaving zero is fine for a stable feed.
	WriteFloatBE(Out, 0.f); WriteFloatBE(Out, 0.f); WriteFloatBE(Out, 0.f);
	// Angular velocity (rad/s) - same.
	WriteFloatBE(Out, 0.f); WriteFloatBE(Out, 0.f); WriteFloatBE(Out, 0.f);

	// ---- Marking (12 bytes: 1 char-set byte + 11 chars) ----
	WriteU8(Out, 1); // ASCII
	const FString CallStr = S.Callsign.ToString();
	const FTCHARToUTF8 Conv(*CallStr);
	const int32 Take = FMath::Min(11, Conv.Length());
	for (int32 i = 0; i < Take; ++i) { WriteU8(Out, static_cast<uint8>(Conv.Get()[i])); }
	for (int32 i = Take; i < 11; ++i) { WriteU8(Out, 0); }

	// ---- Capabilities (4 bytes bitfield) ----
	WriteU32BE(Out, 0);
}

// ============================================================================
// Emission PDU (Type 23) - IEEE 1278.1 section 7.6.2. Broadcasts each radar's
// emitter fingerprint (frequency, PRF, pulse width, ERP) plus a Track/Jam list
// of every aircraft it's currently painting. External ELINT receivers see the
// same picture the sim's own operator scope sees. - TripleA
// ============================================================================

void UClearanceDISEmitter::EmitEmissions(const TArray<FRadarEmissionSnapshot>& Radars, float SimTimeSeconds)
{
	if (!Socket || !TargetAddr.IsValid()) { return; }

	TArray<uint8> Buf;
	Buf.Reserve(256);

	for (const FRadarEmissionSnapshot& R : Radars)
	{
		if (!R.bEnabled) { continue; }
		Buf.Reset();
		BuildEmissionPDU(Buf, R, SimTimeSeconds);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}

void UClearanceDISEmitter::BuildEmissionPDU(TArray<uint8>& Out, const FRadarEmissionSnapshot& R, float SimTimeSeconds) const
{
	// Emitting entity ID - stable per-radar-site hash so external federates can
	// track "which radar site" across sessions. Site + App from our identity,
	// entity number from the site name hash mapped into 1..65535. - TripleA
	const uint16 EmittingEntity = static_cast<uint16>((GetTypeHash(R.SiteName) % 65535) + 1);

	// DIS timestamp: 31-bit fraction of an hour past top-of-hour, LSB=0 for relative
	const double SecondsInHour = FMath::Fmod(static_cast<double>(SimTimeSeconds), 3600.0);
	const uint32 DISTimestamp = static_cast<uint32>((SecondsInHour * (2147483648.0 / 3600.0))) & 0xFFFFFFFE;

	// Fixed sizes computed up front so PDU Length field is known before we serialise:
	//   Header (12) + Emission body (16) + EmitterSystem (20) + Beam (52) + 8*NTargets
	const int32 NTargets = FMath::Min(R.PaintedCallsigns.Num(), 255);
	const uint16 PduLength = static_cast<uint16>(12 + 16 + 20 + 52 + 8 * NTargets);

	// Emitter System Data Length is in 32-bit words including its own length byte.
	// System block = 20 bytes fixed + Beam block. Beam block = 52 fixed + 8*NTargets.
	const int32 SystemDataBytes = 20 + 52 + 8 * NTargets;
	const uint8 SystemDataLength = static_cast<uint8>(SystemDataBytes / 4);
	const int32 BeamDataBytes = 52 + 8 * NTargets;
	const uint8 BeamDataLength = static_cast<uint8>(BeamDataBytes / 4);

	// ---- PDU Header (12 bytes) - IEEE 1278.1 §7.2.2 ----
	WriteU8(Out, 6);                              // Protocol version (6 = DIS 1995/1998)
	WriteU8(Out, static_cast<uint8>(ExerciseId));
	WriteU8(Out, 23);                             // PDU type 23 = Emission
	WriteU8(Out, 6);                              // Protocol family 6 = Distributed Emission Regeneration
	WriteU32BE(Out, DISTimestamp);
	WriteU16BE(Out, PduLength);
	WriteU16BE(Out, 0);                           // padding

	// ---- Emission PDU Body (16 bytes) - §7.6.2 ----
	// Emitting Entity ID (6 bytes) = SimulationAddress + Entity number
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, EmittingEntity);

	// Event ID (6 bytes). Constant per radar for now - a real implementation
	// would bump this on state transitions.
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, EmittingEntity);              // Event number reuses entity for simplicity

	WriteU8(Out, 0);                              // State Update Indicator (0 = Heartbeat)
	WriteU8(Out, 1);                              // Number of Emitter Systems (this radar = 1)
	WriteU16BE(Out, 0);                           // padding

	// ---- Emitter System (20 bytes) - §7.6.2 table 7-30 ----
	WriteU8(Out, SystemDataLength);
	WriteU8(Out, 1);                              // Number of Beams = 1
	WriteU16BE(Out, 0);                           // padding

	// Emitter System block (4 bytes) = Name (2) + Function (1) + Number (1)
	WriteU16BE(Out, static_cast<uint16>(R.Signature.EmitterName));
	WriteU8(Out, static_cast<uint8>(R.Signature.EmitterFunction));
	WriteU8(Out, 1);                              // Emitter Number (this radar's slot on the entity)

	// Location relative to entity centre (12 bytes) - radar antenna XYZ in
	// entity coords. The entity IS the site so location is (0,0,0). - TripleA
	WriteFloatBE(Out, 0.f);
	WriteFloatBE(Out, 0.f);
	WriteFloatBE(Out, 0.f);

	// ---- Beam block (52 bytes fixed + track/jam) - §7.6.2 table 7-31 ----
	WriteU8(Out, BeamDataLength);
	WriteU8(Out, 1);                              // Beam ID Number
	WriteU16BE(Out, 0);                           // Beam Parameter Index

	// Fundamental Parameter Data (40 bytes) - table 7-32
	const float CenterFreq = 0.5f * (R.Signature.FrequencyLowHz + R.Signature.FrequencyHighHz);
	const float FreqRange  = FMath::Max(0.f, R.Signature.FrequencyHighHz - R.Signature.FrequencyLowHz);
	WriteFloatBE(Out, CenterFreq);                // Frequency (Hz)
	WriteFloatBE(Out, FreqRange);                 // Frequency Range (Hz)
	WriteFloatBE(Out, R.Signature.EffectiveRadiatedPowerDbm);
	WriteFloatBE(Out, R.Signature.PulseRepetitionFreqHz);
	WriteFloatBE(Out, R.Signature.PulseWidthMicrosec);
	WriteFloatBE(Out, FMath::DegreesToRadians(R.SweepAngleDeg));    // Beam Azimuth Center (rad)
	WriteFloatBE(Out, FMath::DegreesToRadians(360.f));              // Azimuth Sweep = full 360 for scanning radar
	WriteFloatBE(Out, 0.f);                                          // Beam Elevation Center
	WriteFloatBE(Out, FMath::DegreesToRadians(30.f));                // Elevation Sweep
	WriteFloatBE(Out, 0.f);                                          // Beam Sweep Sync

	WriteU8(Out, static_cast<uint8>(R.Signature.BeamFunction));
	WriteU8(Out, static_cast<uint8>(NTargets));                     // Number of Targets
	WriteU8(Out, 0);                                                // High Density Track/Jam (0 = flat list)
	WriteU8(Out, 1);                                                // Beam Status (bit 0: 1 = active)

	// Jamming Technique (4 bytes: Kind, Category, SubCategory, Specific)
	WriteU8(Out, 0);
	WriteU8(Out, 0);
	WriteU8(Out, 0);
	WriteU8(Out, 0);

	// ---- Track/Jam Data (8 bytes per target) - §7.6.2 table 7-33 ----
	for (int32 i = 0; i < NTargets; ++i)
	{
		const uint16 TargetEntity = static_cast<uint16>((GetTypeHash(R.PaintedCallsigns[i]) % 65535) + 1);
		WriteU16BE(Out, static_cast<uint16>(SiteId));
		WriteU16BE(Out, static_cast<uint16>(ApplicationId));
		WriteU16BE(Out, TargetEntity);
		WriteU8(Out, 1);                          // Emitter Number
		WriteU8(Out, 1);                          // Beam Number
	}
}

// ============================================================================
// Fire PDU (Type 2) - IEEE 1278.1 §7.3.3. "Aircraft A just launched a munition."
// ============================================================================

void UClearanceDISEmitter::EmitFireEvents(const TArray<FWeaponsFireEvent>& Events, float SimTimeSeconds)
{
	if (!Socket || !TargetAddr.IsValid()) { return; }

	TArray<uint8> Buf;
	Buf.Reserve(96);

	for (const FWeaponsFireEvent& E : Events)
	{
		Buf.Reset();
		BuildFirePDU(Buf, E, SimTimeSeconds);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}

void UClearanceDISEmitter::BuildFirePDU(TArray<uint8>& Out, const FWeaponsFireEvent& E, float SimTimeSeconds) const
{
	const uint16 FiringEntity = static_cast<uint16>((GetTypeHash(E.FiringCallsign) % 65535) + 1);
	const uint16 TargetEntity = E.TargetCallsign.IsNone()
		? 0
		: static_cast<uint16>((GetTypeHash(E.TargetCallsign) % 65535) + 1);
	// Munition entity number derived from the FiringEntity + EventNumber -
	// unique per launch, referenced by the Detonation PDU that closes the
	// event. - TripleA
	const uint16 MunitionEntity = static_cast<uint16>(((FiringEntity * 65521u) ^ E.EventNumber) % 65535 + 1);

	const double SecondsInHour = FMath::Fmod(static_cast<double>(SimTimeSeconds), 3600.0);
	const uint32 DISTimestamp = static_cast<uint32>((SecondsInHour * (2147483648.0 / 3600.0))) & 0xFFFFFFFE;

	// Fixed PDU length - Fire PDU is 96 bytes total per §7.3.3 table 7-3.
	const uint16 PduLength = 96;

	// ---- PDU Header (12 bytes) ----
	WriteU8(Out, 6);                              // Protocol version
	WriteU8(Out, static_cast<uint8>(ExerciseId));
	WriteU8(Out, 2);                              // PDU type 2 = Fire
	WriteU8(Out, 2);                              // Protocol family 2 = Warfare
	WriteU32BE(Out, DISTimestamp);
	WriteU16BE(Out, PduLength);
	WriteU16BE(Out, 0);                           // padding

	// ---- Firing Entity ID (6 bytes) ----
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, FiringEntity);

	// ---- Target Entity ID (6 bytes) ----
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, TargetEntity);

	// ---- Munition Entity ID (6 bytes) ----
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, MunitionEntity);

	// ---- Event ID (6 bytes) ----
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, static_cast<uint16>(E.EventNumber & 0xFFFFu));

	// ---- Fire Mission Index (4 bytes) - 0 for single shots ----
	WriteU32BE(Out, 0);

	// ---- Location in World Coordinates (24 bytes) - 3 x double ECEF metres ----
	const double XMeters = static_cast<double>(E.LocationNm.X) * 1852.0;
	const double YMeters = static_cast<double>(E.LocationNm.Y) * 1852.0;
	const double ZMeters = static_cast<double>(E.AltitudeFt) * 0.3048;
	WriteDoubleBE(Out, XMeters);
	WriteDoubleBE(Out, YMeters);
	WriteDoubleBE(Out, ZMeters);

	// ---- Burst Descriptor (16 bytes) - table 7-4 ----
	// Munition Entity Type = Kind (1) + Domain (1) + Country (2) + Category (1) + SubCat (1) + Specific (1) + Extra (1) = 8 bytes
	WriteU8(Out, 2);                              // Kind 2 = Munition
	WriteU8(Out, 2);                              // Domain 2 = Air
	WriteU16BE(Out, 225);                         // Country 225 = United States
	WriteU8(Out, E.MunitionKind);                 // Category (1 = guided missile, etc.)
	WriteU8(Out, 0);                              // SubCategory
	WriteU8(Out, 0);                              // Specific
	WriteU8(Out, 0);                              // Extra
	WriteU16BE(Out, static_cast<uint16>(E.WarheadKind));
	WriteU16BE(Out, static_cast<uint16>(E.FuseKind));
	WriteU16BE(Out, static_cast<uint16>(E.Quantity));
	WriteU16BE(Out, static_cast<uint16>(E.Rate));

	// ---- Velocity (12 bytes) - 3 x float m/s ----
	WriteFloatBE(Out, E.VelocityXKts * 0.514444f);
	WriteFloatBE(Out, E.VelocityYKts * 0.514444f);
	WriteFloatBE(Out, E.VelocityZKts * 0.514444f);

	// ---- Range (4 bytes) - float metres ----
	WriteFloatBE(Out, E.RangeMeters);
}

bool UClearanceDISEmitter::ParseFirePDU(const TArray<uint8>& In, FWeaponsFireEvent& Out,
	int32& OutFiringEntity, int32& OutTargetEntity, int32& OutMunitionEntity)
{
	int32 Cursor = 0;
	bool bOk = true;

	// Header
	const uint8 ProtoVersion = ReadU8(In, Cursor, bOk);
	(void)ReadU8(In, Cursor, bOk);                // Exercise
	const uint8 PduType      = ReadU8(In, Cursor, bOk);
	const uint8 ProtoFamily  = ReadU8(In, Cursor, bOk);
	(void)ReadU32BE(In, Cursor, bOk);             // Timestamp
	const uint16 PduLength   = ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);             // padding
	if (!bOk || ProtoVersion == 0 || PduType != 2 || ProtoFamily != 2) { return false; }
	if (PduLength != static_cast<uint16>(In.Num())) { return false; }

	// Firing Entity
	(void)ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);
	OutFiringEntity = ReadU16BE(In, Cursor, bOk);

	// Target Entity
	(void)ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);
	OutTargetEntity = ReadU16BE(In, Cursor, bOk);

	// Munition Entity
	(void)ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);
	OutMunitionEntity = ReadU16BE(In, Cursor, bOk);

	// Event ID
	(void)ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);
	Out.EventNumber = ReadU16BE(In, Cursor, bOk);

	// Fire Mission Index
	(void)ReadU32BE(In, Cursor, bOk);

	// Location (3 doubles). We already have ReadDoubleBE-less code path, do inline.
	auto ReadDoubleBE = [&](int32& Cur, bool& Ok)
	{
		if (Cur + 8 > In.Num()) { Ok = false; return 0.0; }
		uint64 Bits = 0;
		for (int32 i = 0; i < 8; ++i) { Bits = (Bits << 8) | uint64(In[Cur + i]); }
		Cur += 8;
		double V = 0.0;
		FMemory::Memcpy(&V, &Bits, sizeof(V));
		return V;
	};
	const double XMeters = ReadDoubleBE(Cursor, bOk);
	const double YMeters = ReadDoubleBE(Cursor, bOk);
	const double ZMeters = ReadDoubleBE(Cursor, bOk);
	Out.LocationNm = FVector2D(static_cast<float>(XMeters / 1852.0), static_cast<float>(YMeters / 1852.0));
	Out.AltitudeFt = static_cast<float>(ZMeters / 0.3048);

	// Burst Descriptor
	(void)ReadU8(In, Cursor, bOk);                // Kind
	(void)ReadU8(In, Cursor, bOk);                // Domain
	(void)ReadU16BE(In, Cursor, bOk);             // Country
	Out.MunitionKind = ReadU8(In, Cursor, bOk);
	(void)ReadU8(In, Cursor, bOk);                // SubCategory
	(void)ReadU8(In, Cursor, bOk);                // Specific
	(void)ReadU8(In, Cursor, bOk);                // Extra
	Out.WarheadKind = ReadU16BE(In, Cursor, bOk);
	Out.FuseKind    = ReadU16BE(In, Cursor, bOk);
	Out.Quantity    = ReadU16BE(In, Cursor, bOk);
	Out.Rate        = ReadU16BE(In, Cursor, bOk);

	// Velocity
	Out.VelocityXKts = ReadFloatBE(In, Cursor, bOk) / 0.514444f;
	Out.VelocityYKts = ReadFloatBE(In, Cursor, bOk) / 0.514444f;
	Out.VelocityZKts = ReadFloatBE(In, Cursor, bOk) / 0.514444f;

	// Range
	Out.RangeMeters = ReadFloatBE(In, Cursor, bOk);

	return bOk;
}

// ============================================================================
// Detonation PDU (Type 3) - IEEE 1278.1 §7.3.4. "The munition from event N
// just impacted / missed / dud." Pairs with Fire PDU by EventNumber.
// ============================================================================

void UClearanceDISEmitter::EmitDetonationEvents(const TArray<FWeaponsDetonationEvent>& Events, float SimTimeSeconds)
{
	if (!Socket || !TargetAddr.IsValid()) { return; }

	TArray<uint8> Buf;
	Buf.Reserve(112);

	for (const FWeaponsDetonationEvent& E : Events)
	{
		Buf.Reset();
		BuildDetonationPDU(Buf, E, SimTimeSeconds);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}

void UClearanceDISEmitter::BuildDetonationPDU(TArray<uint8>& Out, const FWeaponsDetonationEvent& E, float SimTimeSeconds) const
{
	const uint16 FiringEntity = static_cast<uint16>((GetTypeHash(E.FiringCallsign) % 65535) + 1);
	const uint16 TargetEntity = E.TargetCallsign.IsNone()
		? 0
		: static_cast<uint16>((GetTypeHash(E.TargetCallsign) % 65535) + 1);
	const uint16 MunitionEntity = static_cast<uint16>(((FiringEntity * 65521u) ^ E.EventNumber) % 65535 + 1);

	const double SecondsInHour = FMath::Fmod(static_cast<double>(SimTimeSeconds), 3600.0);
	const uint32 DISTimestamp = static_cast<uint32>((SecondsInHour * (2147483648.0 / 3600.0))) & 0xFFFFFFFE;

	// Fixed PDU length - Detonation PDU is 104 bytes total per §7.3.4 table 7-5.
	const uint16 PduLength = 104;

	// ---- PDU Header (12 bytes) ----
	WriteU8(Out, 6);
	WriteU8(Out, static_cast<uint8>(ExerciseId));
	WriteU8(Out, 3);                              // PDU type 3 = Detonation
	WriteU8(Out, 2);                              // Protocol family 2 = Warfare
	WriteU32BE(Out, DISTimestamp);
	WriteU16BE(Out, PduLength);
	WriteU16BE(Out, 0);

	// ---- Firing / Target / Munition Entity IDs + Event ID (24 bytes) ----
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, FiringEntity);
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, TargetEntity);
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, MunitionEntity);
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, static_cast<uint16>(E.EventNumber & 0xFFFFu));

	// ---- Velocity (12 bytes) - detonation-time impact velocity ----
	WriteFloatBE(Out, E.VelocityXKts * 0.514444f);
	WriteFloatBE(Out, E.VelocityYKts * 0.514444f);
	WriteFloatBE(Out, E.VelocityZKts * 0.514444f);

	// ---- World Location (24 bytes) - 3 x double ECEF metres ----
	const double XMeters = static_cast<double>(E.LocationNm.X) * 1852.0;
	const double YMeters = static_cast<double>(E.LocationNm.Y) * 1852.0;
	const double ZMeters = static_cast<double>(E.AltitudeFt) * 0.3048;
	WriteDoubleBE(Out, XMeters);
	WriteDoubleBE(Out, YMeters);
	WriteDoubleBE(Out, ZMeters);

	// ---- Burst Descriptor (16 bytes) ----
	WriteU8(Out, 2);                              // Kind = Munition
	WriteU8(Out, 2);                              // Domain = Air
	WriteU16BE(Out, 225);                         // Country = US
	WriteU8(Out, E.MunitionKind);
	WriteU8(Out, 0);
	WriteU8(Out, 0);
	WriteU8(Out, 0);
	WriteU16BE(Out, static_cast<uint16>(E.WarheadKind));
	WriteU16BE(Out, static_cast<uint16>(E.FuseKind));
	WriteU16BE(Out, static_cast<uint16>(E.Quantity));
	WriteU16BE(Out, static_cast<uint16>(E.Rate));

	// ---- Location in Entity Coordinates (12 bytes) - relative XYZ ----
	WriteFloatBE(Out, 0.f);
	WriteFloatBE(Out, 0.f);
	WriteFloatBE(Out, 0.f);

	// ---- Detonation Result (1) + Number of Articulation Parameters (1) +
	// Padding (2) - §7.3.4 table 7-5. Params count = 0 so no variable block. ----
	WriteU8(Out, E.DetonationResult);
	WriteU8(Out, 0);                              // Number of Articulation Parameters
	WriteU16BE(Out, 0);                           // Pad
}

bool UClearanceDISEmitter::ParseDetonationPDU(const TArray<uint8>& In, FWeaponsDetonationEvent& Out,
	int32& OutFiringEntity, int32& OutTargetEntity, int32& OutMunitionEntity)
{
	int32 Cursor = 0;
	bool bOk = true;

	// Header
	const uint8 ProtoVersion = ReadU8(In, Cursor, bOk);
	(void)ReadU8(In, Cursor, bOk);
	const uint8 PduType      = ReadU8(In, Cursor, bOk);
	const uint8 ProtoFamily  = ReadU8(In, Cursor, bOk);
	(void)ReadU32BE(In, Cursor, bOk);
	const uint16 PduLength   = ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);
	if (!bOk || ProtoVersion == 0 || PduType != 3 || ProtoFamily != 2) { return false; }
	if (PduLength != static_cast<uint16>(In.Num())) { return false; }

	// Firing / Target / Munition Entity IDs + Event ID
	(void)ReadU16BE(In, Cursor, bOk); (void)ReadU16BE(In, Cursor, bOk);
	OutFiringEntity = ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk); (void)ReadU16BE(In, Cursor, bOk);
	OutTargetEntity = ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk); (void)ReadU16BE(In, Cursor, bOk);
	OutMunitionEntity = ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk); (void)ReadU16BE(In, Cursor, bOk);
	Out.EventNumber = ReadU16BE(In, Cursor, bOk);

	// Velocity
	Out.VelocityXKts = ReadFloatBE(In, Cursor, bOk) / 0.514444f;
	Out.VelocityYKts = ReadFloatBE(In, Cursor, bOk) / 0.514444f;
	Out.VelocityZKts = ReadFloatBE(In, Cursor, bOk) / 0.514444f;

	// World Location
	auto ReadDoubleBE = [&](int32& Cur, bool& Ok)
	{
		if (Cur + 8 > In.Num()) { Ok = false; return 0.0; }
		uint64 Bits = 0;
		for (int32 i = 0; i < 8; ++i) { Bits = (Bits << 8) | uint64(In[Cur + i]); }
		Cur += 8;
		double V = 0.0;
		FMemory::Memcpy(&V, &Bits, sizeof(V));
		return V;
	};
	const double XMeters = ReadDoubleBE(Cursor, bOk);
	const double YMeters = ReadDoubleBE(Cursor, bOk);
	const double ZMeters = ReadDoubleBE(Cursor, bOk);
	Out.LocationNm = FVector2D(static_cast<float>(XMeters / 1852.0), static_cast<float>(YMeters / 1852.0));
	Out.AltitudeFt = static_cast<float>(ZMeters / 0.3048);

	// Burst Descriptor
	(void)ReadU8(In, Cursor, bOk);                // Kind
	(void)ReadU8(In, Cursor, bOk);                // Domain
	(void)ReadU16BE(In, Cursor, bOk);             // Country
	Out.MunitionKind = ReadU8(In, Cursor, bOk);
	(void)ReadU8(In, Cursor, bOk);                // SubCategory
	(void)ReadU8(In, Cursor, bOk);                // Specific
	(void)ReadU8(In, Cursor, bOk);                // Extra
	Out.WarheadKind = ReadU16BE(In, Cursor, bOk);
	Out.FuseKind    = ReadU16BE(In, Cursor, bOk);
	Out.Quantity    = ReadU16BE(In, Cursor, bOk);
	Out.Rate        = ReadU16BE(In, Cursor, bOk);

	// Location in Entity Coordinates (skip)
	(void)ReadFloatBE(In, Cursor, bOk);
	(void)ReadFloatBE(In, Cursor, bOk);
	(void)ReadFloatBE(In, Cursor, bOk);

	// Detonation Result (1) + Number of Articulation Parameters (1) + Pad (2)
	Out.DetonationResult = ReadU8(In, Cursor, bOk);
	(void)ReadU8(In, Cursor, bOk);      // NumArticulationParameters - always 0 in our emitter
	(void)ReadU16BE(In, Cursor, bOk);   // Pad

	return bOk;
}

bool UClearanceDISEmitter::ParseEmissionPDU(const TArray<uint8>& In, FRadarEmissionSnapshot& Out,
	int32& OutEmittingSite, int32& OutEmittingApp, int32& OutEmittingEntity,
	TArray<int32>& OutTargetEntityNumbers)
{
	int32 Cursor = 0;
	bool bOk = true;
	OutTargetEntityNumbers.Reset();

	// Header
	const uint8 ProtoVersion = ReadU8(In, Cursor, bOk);
	(void)ReadU8(In, Cursor, bOk);                // Exercise
	const uint8 PduType      = ReadU8(In, Cursor, bOk);
	const uint8 ProtoFamily  = ReadU8(In, Cursor, bOk);
	(void)ReadU32BE(In, Cursor, bOk);             // Timestamp
	const uint16 PduLength   = ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);             // padding
	if (!bOk || ProtoVersion == 0 || PduType != 23 || ProtoFamily != 6) { return false; }
	if (PduLength != static_cast<uint16>(In.Num())) { return false; }

	// Emission body
	OutEmittingSite    = ReadU16BE(In, Cursor, bOk);
	OutEmittingApp     = ReadU16BE(In, Cursor, bOk);
	OutEmittingEntity  = ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);             // Event site
	(void)ReadU16BE(In, Cursor, bOk);             // Event app
	(void)ReadU16BE(In, Cursor, bOk);             // Event number
	(void)ReadU8(In, Cursor, bOk);                // State Update Indicator
	const uint8 NSystems = ReadU8(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);             // padding
	if (!bOk || NSystems == 0) { return false; }

	// Emitter System (only parse the first for MVP)
	(void)ReadU8(In, Cursor, bOk);                // System Data Length
	const uint8 NBeams = ReadU8(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);             // padding
	Out.Signature.EmitterName     = ReadU16BE(In, Cursor, bOk);
	Out.Signature.EmitterFunction = ReadU8(In, Cursor, bOk);
	(void)ReadU8(In, Cursor, bOk);                // Emitter Number
	(void)ReadFloatBE(In, Cursor, bOk);           // Location X
	(void)ReadFloatBE(In, Cursor, bOk);           // Location Y
	(void)ReadFloatBE(In, Cursor, bOk);           // Location Z
	if (!bOk || NBeams == 0) { return false; }

	// Beam
	(void)ReadU8(In, Cursor, bOk);                // Beam Data Length
	(void)ReadU8(In, Cursor, bOk);                // Beam ID
	(void)ReadU16BE(In, Cursor, bOk);             // Beam Parameter Index

	const float CenterFreq   = ReadFloatBE(In, Cursor, bOk);
	const float FreqRange    = ReadFloatBE(In, Cursor, bOk);
	Out.Signature.FrequencyLowHz  = CenterFreq - 0.5f * FreqRange;
	Out.Signature.FrequencyHighHz = CenterFreq + 0.5f * FreqRange;
	Out.Signature.EffectiveRadiatedPowerDbm = ReadFloatBE(In, Cursor, bOk);
	Out.Signature.PulseRepetitionFreqHz     = ReadFloatBE(In, Cursor, bOk);
	Out.Signature.PulseWidthMicrosec        = ReadFloatBE(In, Cursor, bOk);
	const float BeamAzCenter = ReadFloatBE(In, Cursor, bOk);
	Out.SweepAngleDeg = FMath::RadiansToDegrees(BeamAzCenter);
	(void)ReadFloatBE(In, Cursor, bOk);           // Azimuth Sweep
	(void)ReadFloatBE(In, Cursor, bOk);           // Elevation Center
	(void)ReadFloatBE(In, Cursor, bOk);           // Elevation Sweep
	(void)ReadFloatBE(In, Cursor, bOk);           // Beam Sweep Sync
	Out.Signature.BeamFunction = ReadU8(In, Cursor, bOk);
	const uint8 NTargets = ReadU8(In, Cursor, bOk);
	(void)ReadU8(In, Cursor, bOk);                // High Density Track/Jam
	(void)ReadU8(In, Cursor, bOk);                // Beam Status
	(void)ReadU32BE(In, Cursor, bOk);             // Jamming Technique

	// Track/Jam data
	OutTargetEntityNumbers.Reserve(NTargets);
	for (uint8 i = 0; i < NTargets; ++i)
	{
		(void)ReadU16BE(In, Cursor, bOk);         // Target site
		(void)ReadU16BE(In, Cursor, bOk);         // Target app
		const uint16 TargetEntity = ReadU16BE(In, Cursor, bOk);
		(void)ReadU8(In, Cursor, bOk);            // Emitter Number
		(void)ReadU8(In, Cursor, bOk);            // Beam Number
		OutTargetEntityNumbers.Add(TargetEntity);
	}

	Out.bEnabled = bOk;
	return bOk;
}

// ============================================================================
// Signal PDU (Type 26) - IEEE 1278.1 §7.7.3. "Here is a chunk of radio data
// from radio N on entity E." Carries the actual voice / data payload for a
// radio that has previously been announced by a Transmitter PDU (Type 25).
// CLEARANCE ships the Signal PDU alone with the transcript as raw-binary
// payload; adding Transmitter later is a natural next step. - TripleA
// ============================================================================

namespace
{
	// Cap so a chatty operator can't blow past a comfortable UDP payload.
	// 200 ASCII bytes leaves headroom for the fixed 32-byte header + padding
	// under the standard 1500-byte Ethernet MTU. - TripleA
	constexpr int32 kSignalTranscriptMaxBytes = 200;

	// Fixed entity number for operator / ground-station comms (SpeakerCallsign
	// = NAME_None). Picked deliberately outside the aircraft hash space so it
	// won't collide with a real callsign hash. - TripleA
	constexpr uint16 kOperatorGroundStationEntity = 60000;
}

void UClearanceDISEmitter::EmitVoiceEvents(const TArray<FVoiceCommsEvent>& Events, float SimTimeSeconds)
{
	if (!Socket || !TargetAddr.IsValid()) { return; }

	TArray<uint8> Buf;
	Buf.Reserve(256);

	for (const FVoiceCommsEvent& E : Events)
	{
		Buf.Reset();
		BuildSignalPDU(Buf, E, SimTimeSeconds);
		int32 Sent = 0;
		if (Socket->SendTo(Buf.GetData(), Buf.Num(), Sent, *TargetAddr) && Sent > 0)
		{
			++LastPacketsSent;
		}
	}
}

void UClearanceDISEmitter::BuildSignalPDU(TArray<uint8>& Out, const FVoiceCommsEvent& E, float SimTimeSeconds) const
{
	// Speaker entity number. Operator (empty callsign) gets a fixed reserved
	// entity so downstream receivers can filter ground-station chatter apart
	// from aircraft chatter. - TripleA
	const uint16 SpeakerEntity = E.SpeakerCallsign.IsNone()
		? kOperatorGroundStationEntity
		: static_cast<uint16>((GetTypeHash(E.SpeakerCallsign) % 65535) + 1);

	// ASCII payload (trim + clamp). Truncation is a soft failure - the
	// receiver still gets the first N characters, which is enough for
	// federation logging. - TripleA
	FTCHARToUTF8 Utf8(*E.Transcript);
	const int32 SrcLen = FMath::Min<int32>(Utf8.Length(), kSignalTranscriptMaxBytes);
	const uint8* SrcData = reinterpret_cast<const uint8*>(Utf8.Get());

	// Data field is padded to a 32-bit boundary per §7.7.3.9.
	const int32 PadTo4 = (SrcLen + 3) & ~3;

	// Fixed body = 20 bytes (6 entity + 2 radio + 2 encoding + 2 TDL + 4 rate
	// + 2 length + 2 samples). Header = 12. Add padded data. - TripleA
	const uint16 PduLength = static_cast<uint16>(12 + 20 + PadTo4);

	const double SecondsInHour = FMath::Fmod(static_cast<double>(SimTimeSeconds), 3600.0);
	const uint32 DISTimestamp = static_cast<uint32>((SecondsInHour * (2147483648.0 / 3600.0))) & 0xFFFFFFFE;

	// ---- PDU Header (12 bytes) ----
	WriteU8(Out, 6);                              // Protocol version
	WriteU8(Out, static_cast<uint8>(ExerciseId));
	WriteU8(Out, 26);                             // PDU type 26 = Signal
	WriteU8(Out, 4);                              // Protocol family 4 = Radio Communications
	WriteU32BE(Out, DISTimestamp);
	WriteU16BE(Out, PduLength);
	WriteU16BE(Out, 0);                           // padding

	// ---- Radio Reference ID (6 bytes) - the emitting radio's entity ----
	WriteU16BE(Out, static_cast<uint16>(SiteId));
	WriteU16BE(Out, static_cast<uint16>(ApplicationId));
	WriteU16BE(Out, SpeakerEntity);

	// ---- Radio ID (2 bytes) - which radio on that entity ----
	WriteU16BE(Out, static_cast<uint16>(E.RadioId));

	// ---- Encoding Scheme (2 bytes) - top 2 bits = Encoding Class,
	// bottom 14 bits = Encoding Type. Class 1 = Raw Binary Data, Type 0.
	// So the packed value is 0x4000. §7.7.3.5 table 7-19. - TripleA
	const uint16 EncodingScheme = (uint16(1) << 14) | uint16(0);
	WriteU16BE(Out, EncodingScheme);

	// ---- TDL Type (2 bytes) - 0 = Other. Only meaningful for Tactical Data
	// Link protocol traffic, which voice-over-DIS isn't. §7.7.3.6. - TripleA
	WriteU16BE(Out, 0);

	// ---- Sample Rate (4 bytes) - 0 for raw binary per §7.7.3.7. ----
	WriteU32BE(Out, 0);

	// ---- Data Length in BITS (2 bytes) - §7.7.3.8. ----
	WriteU16BE(Out, static_cast<uint16>(SrcLen * 8));

	// ---- Samples (2 bytes) - 0 for raw binary per §7.7.3.9. ----
	WriteU16BE(Out, 0);

	// ---- Data (variable, padded to 32-bit boundary) ----
	for (int32 i = 0; i < SrcLen; ++i) { Out.Add(SrcData[i]); }
	for (int32 i = SrcLen; i < PadTo4; ++i) { Out.Add(0); }
}

bool UClearanceDISEmitter::ParseSignalPDU(const TArray<uint8>& In, FVoiceCommsEvent& Out,
	int32& OutSpeakerEntity)
{
	int32 Cursor = 0;
	bool bOk = true;

	// Header
	const uint8 ProtoVersion = ReadU8(In, Cursor, bOk);
	(void)ReadU8(In, Cursor, bOk);                // Exercise
	const uint8 PduType      = ReadU8(In, Cursor, bOk);
	const uint8 ProtoFamily  = ReadU8(In, Cursor, bOk);
	(void)ReadU32BE(In, Cursor, bOk);             // Timestamp
	const uint16 PduLength   = ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);             // padding
	if (!bOk || ProtoVersion == 0 || PduType != 26 || ProtoFamily != 4) { return false; }
	if (PduLength != static_cast<uint16>(In.Num())) { return false; }

	// Radio Reference ID
	(void)ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);
	OutSpeakerEntity = ReadU16BE(In, Cursor, bOk);

	// Radio ID
	Out.RadioId = ReadU16BE(In, Cursor, bOk);

	// Encoding Scheme + TDL Type
	(void)ReadU16BE(In, Cursor, bOk);             // encoding scheme
	(void)ReadU16BE(In, Cursor, bOk);             // TDL type

	// Sample Rate
	(void)ReadU32BE(In, Cursor, bOk);

	// Data Length (in bits) + Samples
	const uint16 DataLenBits = ReadU16BE(In, Cursor, bOk);
	(void)ReadU16BE(In, Cursor, bOk);             // samples

	// Data
	const int32 DataLenBytes = static_cast<int32>(DataLenBits) / 8;
	if (!bOk || Cursor + DataLenBytes > In.Num()) { return false; }

	FString AsciiOut;
	AsciiOut.Reserve(DataLenBytes);
	for (int32 i = 0; i < DataLenBytes; ++i)
	{
		AsciiOut.AppendChar(static_cast<TCHAR>(In[Cursor + i]));
	}
	Out.Transcript = MoveTemp(AsciiOut);

	return bOk;
}
