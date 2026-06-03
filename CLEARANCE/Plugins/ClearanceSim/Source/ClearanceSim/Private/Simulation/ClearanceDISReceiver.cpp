#include "Simulation/ClearanceDISReceiver.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Common/UdpSocketBuilder.h"

uint16 UClearanceDISReceiver::ReadU16BE(const uint8* P)
{
	return (static_cast<uint16>(P[0]) << 8) | static_cast<uint16>(P[1]);
}

uint32 UClearanceDISReceiver::ReadU32BE(const uint8* P)
{
	return (static_cast<uint32>(P[0]) << 24) | (static_cast<uint32>(P[1]) << 16)
	     | (static_cast<uint32>(P[2]) << 8)  |  static_cast<uint32>(P[3]);
}

float UClearanceDISReceiver::ReadFloatBE(const uint8* P)
{
	const uint32 Bits = ReadU32BE(P);
	float V;
	FMemory::Memcpy(&V, &Bits, sizeof(V));
	return V;
}

double UClearanceDISReceiver::ReadDoubleBE(const uint8* P)
{
	uint64 Bits = 0;
	for (int32 i = 0; i < 8; ++i) { Bits = (Bits << 8) | P[i]; }
	double V;
	FMemory::Memcpy(&V, &Bits, sizeof(V));
	return V;
}

bool UClearanceDISReceiver::Start(int32 Port)
{
	Stop();

	Socket = FUdpSocketBuilder(TEXT("ClearanceDISRecv"))
		.AsReusable()
		.AsNonBlocking()
		.WithReceiveBufferSize(256 * 1024)
		.BoundToPort(Port)
		.Build();

	if (!Socket)
	{
		UE_LOG(LogTemp, Warning, TEXT("DIS receiver: failed to bind UDP port %d"), Port);
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("DIS receiver: listening on UDP port %d"), Port);
	return true;
}

void UClearanceDISReceiver::Stop()
{
	if (Socket)
	{
		ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
		Socket->Close();
		if (SS) { SS->DestroySocket(Socket); }
		Socket = nullptr;
	}
	LastSeenSeconds.Reset();
}

void UClearanceDISReceiver::Poll(AClearanceAirspaceManager* Manager, double WorldTimeSeconds)
{
	LastPacketsReceived = 0;
	if (!Socket || !Manager) { return; }

	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS) { return; }

	const float DeltaSeconds = (LastPollSeconds < 0.0)
		? 0.f
		: static_cast<float>(FMath::Max(0.0, WorldTimeSeconds - LastPollSeconds));
	LastPollSeconds = WorldTimeSeconds;

	uint8 Buf[2048];
	uint32 Pending = 0;
	int32 Drained = 0;
	constexpr int32 MaxPerPoll = 128; // backstop against a flood
	TSharedRef<FInternetAddr> From = SS->CreateInternetAddr();
	TSet<FName> FreshThisPoll;

	while (Drained < MaxPerPoll && Socket->HasPendingData(Pending) && Pending > 0)
	{
		int32 Read = 0;
		if (!Socket->RecvFrom(Buf, sizeof(Buf), Read, *From)) { break; }
		if (Read <= 0) { break; }
		++Drained;
		if (ParseAndInject(Buf, Read, Manager, WorldTimeSeconds, FreshThisPoll))
		{
			++LastPacketsReceived;
		}
	}

	// Glide everyone who didn't get fresh truth this tick forward on their last
	// known velocity. Keeps externals flying smoothly between sparse packets. - TripleA
	if (DeltaSeconds > 0.f)
	{
		DeadReckonStale(Manager, WorldTimeSeconds, DeltaSeconds, FreshThisPoll);
	}

	// Expire externals that haven't been refreshed for StaleTimeoutSeconds.
	TArray<FName> Drop;
	for (const auto& Pair : LastSeenSeconds)
	{
		if (WorldTimeSeconds - Pair.Value > static_cast<double>(StaleTimeoutSeconds))
		{
			Drop.Add(Pair.Key);
		}
	}
	for (const FName& Cs : Drop)
	{
		Manager->DeregisterAircraft(Cs);
		LastSeenSeconds.Remove(Cs);
	}
}

void UClearanceDISReceiver::DeadReckonStale(AClearanceAirspaceManager* Manager, double WorldTimeSeconds,
	float DeltaSeconds, const TSet<FName>& FreshThisPoll)
{
	for (const auto& Pair : LastSeenSeconds)
	{
		const FName Cs = Pair.Key;
		if (FreshThisPoll.Contains(Cs)) { continue; } // already at fresh truth this tick

		FAircraftState S = Manager->GetAircraftState(Cs);
		if (!S.bIsValid || !S.bIsExternal) { continue; }

		// DR algorithm 2: constant linear velocity. Velocity is in nm/s; altitude
		// drifts on the climb rate (ft/min -> ft/sec). Heading/bank stay put -
		// the next packet will refresh those if they've changed. - TripleA
		S.Position += S.Velocity * DeltaSeconds;
		S.Altitude += (S.ClimbRate / 60.f) * DeltaSeconds;
		Manager->RequestStateUpdate(S);
	}
}

bool UClearanceDISReceiver::ParseAndInject(const uint8* Data, int32 Length, AClearanceAirspaceManager* Manager,
	double WorldTimeSeconds, TSet<FName>& OutFreshThisPoll)
{
	if (Length < 144) { return false; }

	// Header
	const uint8 ProtocolVersion = Data[0];
	const uint8 PduType         = Data[2];
	if (ProtocolVersion != 6 || PduType != 1) { return false; } // DIS v6 Entity State only

	// Entity ID at offset 12
	const uint16 SrcSite   = ReadU16BE(Data + 12);
	const uint16 SrcApp    = ReadU16BE(Data + 14);
	const uint16 SrcEntity = ReadU16BE(Data + 16);

	// Skip our own broadcast loopback.
	if (static_cast<int32>(SrcSite) == LocalSiteId && static_cast<int32>(SrcApp) == LocalApplicationId)
	{
		return false;
	}

	// Force ID (offset 18) -> threat class
	const uint8 ForceId = Data[18];
	EThreatClass ThreatClass;
	switch (ForceId)
	{
	case 1:  ThreatClass = EThreatClass::Friendly; break;
	case 2:  ThreatClass = EThreatClass::Hostile;  break;
	case 3:  ThreatClass = EThreatClass::Neutral;  break;
	default: ThreatClass = EThreatClass::Unknown;  break;
	}

	// Rough wake-category guess from Entity Type (offset 20). The emitter half
	// of this sim writes sub=8 for Light, sub=22 for airliners with specific
	// codes 5/8/12 = Medium/Heavy/Super. Anything else falls back to Medium so
	// the conflict matrix still has a category to work with. - TripleA
	const uint8 ETSub      = Data[25];
	const uint8 ETSpecific = Data[26];
	EWakeCategory WakeCategory = EWakeCategory::Medium;
	if (ETSub == 8)
	{
		WakeCategory = EWakeCategory::Light;
	}
	else if (ETSub == 22)
	{
		if (ETSpecific == 8)        { WakeCategory = EWakeCategory::Heavy; }
		else if (ETSpecific == 12)  { WakeCategory = EWakeCategory::Super; }
		else                        { WakeCategory = EWakeCategory::Medium; }
	}

	// Linear velocity (offset 36, 3 floats, m/s)
	const float Vx = ReadFloatBE(Data + 36);
	const float Vy = ReadFloatBE(Data + 40);
	const float Vz = ReadFloatBE(Data + 44);

	// Location (offset 48, 3 doubles, meters)
	const double XMeters = ReadDoubleBE(Data + 48);
	const double YMeters = ReadDoubleBE(Data + 56);
	const double ZMeters = ReadDoubleBE(Data + 64);

	// Orientation (offset 72, 3 floats, radians: psi/theta/phi)
	const float Psi = ReadFloatBE(Data + 72);
	// const float Theta = ReadFloatBE(Data + 76);
	const float Phi = ReadFloatBE(Data + 80);

	// Marking (offset 128: charset byte + 11 chars)
	char MarkingChars[12] = {};
	for (int32 i = 0; i < 11; ++i) { MarkingChars[i] = static_cast<char>(Data[129 + i]); }
	MarkingChars[11] = '\0';
	FString CallsignStr = FString(ANSI_TO_TCHAR(MarkingChars)).TrimStartAndEnd();
	if (CallsignStr.IsEmpty())
	{
		CallsignStr = FString::Printf(TEXT("EXT-%u-%u-%u"), SrcSite, SrcApp, SrcEntity);
	}
	const FName Callsign(*CallsignStr);

	// Build a state in the local sim's units (nm, ft, kt, deg).
	FAircraftState State;
	State.bIsValid = true;
	State.Callsign = Callsign;
	State.Position = FVector(static_cast<float>(XMeters / 1852.0), static_cast<float>(YMeters / 1852.0), 0.f);
	State.Altitude = static_cast<float>(ZMeters / 0.3048);

	float HeadingDeg = FMath::Fmod(FMath::RadiansToDegrees(Psi), 360.f);
	if (HeadingDeg < 0.f) { HeadingDeg += 360.f; }
	State.Heading = HeadingDeg;
	State.TargetHeading = HeadingDeg;
	State.TargetAltitude = State.Altitude;
	State.BankAngle = FMath::RadiansToDegrees(Phi);

	const float SpeedMps = FMath::Sqrt(Vx * Vx + Vy * Vy);
	State.Speed = SpeedMps * 1.94384f;
	State.TargetSpeed = State.Speed;
	State.ClimbRate = Vz / 0.00508f;                              // m/s -> ft/min
	State.Velocity = FVector(Vx / 1852.f, Vy / 1852.f, 0.f);      // ground velocity in nm/s

	State.WakeCategory = WakeCategory;
	State.ThreatClass  = ThreatClass;
	State.FlightPhase  = EFlightPhase::Enroute;
	State.bIsExternal  = true;
	State.bIsMilitary  = (ForceId == 1 || ForceId == 2);

	const bool bExisting = LastSeenSeconds.Contains(Callsign);
	if (!bExisting)
	{
		Manager->RegisterAircraft(State);
	}
	else
	{
		Manager->RequestStateUpdate(State);
	}
	LastSeenSeconds.Add(Callsign, WorldTimeSeconds);
	OutFreshThisPoll.Add(Callsign);
	return true;
}
