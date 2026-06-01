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
