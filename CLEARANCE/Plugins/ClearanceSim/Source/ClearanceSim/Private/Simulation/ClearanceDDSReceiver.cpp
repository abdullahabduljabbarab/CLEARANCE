// Unreal-side DDS ingest. Owns a FClearanceSubscriber that subscribes to
// clearance/aircraft/state (and the five other topics for participant
// completeness; only AircraftState is ingested today). Received samples get
// queued on the transport thread and drained on the game thread from the
// SimulationController tick. - TripleA

#include "Simulation/ClearanceDDSReceiver.h"

#include "Airspace/ClearanceAirspaceManager.h"

namespace
{
	// Map the DDS Force ID enum back onto CLEARANCE's ThreatClass.
	// Matches the emitter's ForceIdFor() direction: 0=other/unknown,
	// 1=friendly, 2=hostile, 3=neutral. - TripleA
	EThreatClass ThreatClassFromForceId(uint8 ForceId)
	{
		switch (ForceId)
		{
		case 1: return EThreatClass::Friendly;
		case 2: return EThreatClass::Hostile;
		case 3: return EThreatClass::Neutral;
		default: return EThreatClass::Unknown;
		}
	}

	// Wake category from the DIS entity subcategory the emitter sends.
	// Emitter maps Light->8, Heavy->22, Super->22 (country 224), Medium->22
	// so we look at subcategory + country to disambiguate. Best-effort. - TripleA
	EWakeCategory WakeFromEntityType(uint8 Subcategory, uint16 Country)
	{
		if (Subcategory == 8)  { return EWakeCategory::Light; }
		if (Subcategory == 22 && Country == 224) { return EWakeCategory::Super; }
		if (Subcategory == 22) { return EWakeCategory::Heavy; }
		return EWakeCategory::Medium;
	}
}

UClearanceDDSReceiver::UClearanceDDSReceiver() = default;
UClearanceDDSReceiver::~UClearanceDDSReceiver() = default;

bool UClearanceDDSReceiver::Start(int32 DomainId)
{
	Stop();

	// Handler runs on the Fast DDS transport thread. Never touch UObjects
	// here - just enqueue the POD sample and let TickDrain do the work on
	// the game thread. - TripleA
	ClearanceDDS::FSubscriberHandlers Handlers;
	Handlers.OnAircraftState = [this](const ClearanceDDS::AircraftState& S)
	{
		FScopeLock Lock(&QueueMutex);
		PendingAircraft.Add(S);
	};
	// Other topic handlers deliberately left null - the subscriber creates
	// their DataReaders anyway, samples are dropped on the transport thread.
	// Cheap and matches DIS receiver scope (Entity State only ingest). - TripleA

	std::unique_ptr<ClearanceDDS::FClearanceSubscriber> Raw =
		ClearanceDDS::FClearanceSubscriber::Create(
			static_cast<std::uint32_t>(DomainId), std::move(Handlers));
	Subscriber = TUniquePtr<ClearanceDDS::FClearanceSubscriber>(Raw.release());

	UE_LOG(LogTemp, Display, TEXT("[DDS] Receiver on domain %d -> %s"),
		DomainId, Subscriber.IsValid() ? TEXT("OK") : TEXT("FAILED"));
	return Subscriber.IsValid();
}

void UClearanceDDSReceiver::Stop()
{
	Subscriber.Reset();
	{
		FScopeLock Lock(&QueueMutex);
		PendingAircraft.Reset();
	}
	LastSeenSeconds.Reset();
	UE_LOG(LogTemp, Display, TEXT("[DDS] Receiver stopped"));
}

bool UClearanceDDSReceiver::IsRunning() const
{
	return Subscriber.IsValid();
}

int32 UClearanceDDSReceiver::GetTotalIngestedCount() const
{
	return IngestedCount;
}

void UClearanceDDSReceiver::TickDrain(AClearanceAirspaceManager* Manager, float WorldTimeSeconds)
{
	if (!Manager || !Subscriber.IsValid()) { return; }

	TArray<ClearanceDDS::AircraftState> Batch;
	{
		FScopeLock Lock(&QueueMutex);
		if (PendingAircraft.Num() == 0) { return; }
		Batch = MoveTemp(PendingAircraft);
		PendingAircraft.Reset();
	}

	for (const ClearanceDDS::AircraftState& S : Batch)
	{
		// Loopback filter - if this sample came from us, skip. Same identity
		// check the DIS receiver uses so a single sim publishing on both
		// wires doesn't ingest itself. - TripleA
		const int32 SrcSite = static_cast<int32>(S.Header().SiteId());
		const int32 SrcApp  = static_cast<int32>(S.Header().ApplicationId());
		if (SrcSite == LocalSiteId && SrcApp == LocalApplicationId) { continue; }

		// Convert DDS-native units (ECEF metres, m/s, radians) back to
		// CLEARANCE-native (nm, ft, kts, degrees). - TripleA
		const FString MarkingStr = FString(UTF8_TO_TCHAR(S.Marking().c_str())).TrimStartAndEnd();
		if (MarkingStr.IsEmpty()) { continue; }
		const FName Callsign(*MarkingStr);

		FAircraftState State;
		State.bIsValid  = true;
		State.Callsign  = Callsign;
		State.Position  = FVector(static_cast<float>(S.XMeters() / 1852.0),
		                          static_cast<float>(S.YMeters() / 1852.0), 0.f);
		State.Altitude  = static_cast<float>(S.ZMeters() / 0.3048);

		float HeadingDeg = FMath::Fmod(FMath::RadiansToDegrees(S.PsiRad()), 360.f);
		if (HeadingDeg < 0.f) { HeadingDeg += 360.f; }
		State.Heading        = HeadingDeg;
		State.TargetHeading  = HeadingDeg;
		State.TargetAltitude = State.Altitude;
		State.BankAngle      = FMath::RadiansToDegrees(S.PhiRad());

		const float SpeedMps = FMath::Sqrt(S.VxMps() * S.VxMps() + S.VyMps() * S.VyMps());
		State.Speed       = SpeedMps * 1.94384f;                     // m/s -> kt
		State.TargetSpeed = State.Speed;
		State.ClimbRate   = S.VzMps() / 0.00508f;                    // m/s -> ft/min
		State.Velocity    = FVector(S.VxMps() / 1852.f, S.VyMps() / 1852.f, 0.f);

		State.WakeCategory = WakeFromEntityType(S.EntitySubcategory(), S.EntityCountry());
		State.ThreatClass  = ThreatClassFromForceId(S.ForceId());
		State.TrueAffiliation = ThreatClassFromForceId(S.TrueAffiliation());
		State.SquawkCode      = static_cast<int32>(S.SquawkCode());
		State.ActiveEmergency = static_cast<EEmergencyType>(S.ActiveEmergency());
		State.FlightPhase     = static_cast<EFlightPhase>(S.FlightPhase());
		State.bIsExternal  = true;                                   // peer-owned, non-commandable
		State.OwnerSiteId  = static_cast<int32>(S.Header().SiteId()); // "SITE N" chip source
		State.bIsMilitary  = (S.ForceId() == 1 || S.ForceId() == 2);

		// Skip if we already own this callsign locally (bIsExternal == false).
		// Two federates running the same scenario (e.g. both Baltic Intercept)
		// spawn overlapping callsigns; without this guard, peer samples would
		// overwrite our own aircraft's operator-authored state (threat class,
		// injected emergencies) with the peer's default state - a classic
		// federation ownership violation. Only ingest if the callsign isn't
		// present, or if we're already tracking it as external. - TripleA
		const FAircraftState Existing = Manager->GetAircraftState(Callsign);
		if (Existing.bIsValid && !Existing.bIsExternal)
		{
			// We own this locally - ignore the peer's copy. Our operator's
			// classifications and emergencies stay authoritative. - TripleA
			continue;
		}

		if (!LastSeenSeconds.Contains(Callsign))
		{
			Manager->RegisterAircraft(State);
		}
		else
		{
			Manager->RequestStateUpdate(State);
		}
		LastSeenSeconds.Add(Callsign, WorldTimeSeconds);
		++IngestedCount;
	}
}
