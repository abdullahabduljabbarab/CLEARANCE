#include "Airspace/ClearanceAirspaceManager.h"
#include "Core/ClearanceConstants.h"
#include "Net/UnrealNetwork.h"

AClearanceAirspaceManager::AClearanceAirspaceManager()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true; // shared state for every connected operator - no relevance culling
}

void AClearanceAirspaceManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AClearanceAirspaceManager, ReplicatedAircraft);
	DOREPLIFETIME(AClearanceAirspaceManager, SectorEnvironment);
	DOREPLIFETIME(AClearanceAirspaceManager, Runways);
	DOREPLIFETIME(AClearanceAirspaceManager, ChaffClouds);
}

void AClearanceAirspaceManager::DropChaff(const FVector& PositionNm, float AltitudeFt)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AirspaceMgr] BLOCKED DropChaff on CLIENT"));
		return;
	}
	FChaffCloud C;
	C.PositionNm = PositionNm;
	C.AltitudeFt = AltitudeFt;
	C.DropSessionTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	C.LifetimeSec = 12.f;
	ChaffClouds.Add(C);

	// Trim retired clouds so the array doesn't grow unbounded across a long
	// session - radars only see live ones anyway. - TripleA
	const float Now = C.DropSessionTime;
	ChaffClouds.RemoveAll([Now](const FChaffCloud& X)
	{
		return (Now - X.DropSessionTime) > X.LifetimeSec;
	});
}

TArray<FChaffCloud> AClearanceAirspaceManager::GetActiveChaffClouds() const
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	TArray<FChaffCloud> Out;
	Out.Reserve(ChaffClouds.Num());
	for (const FChaffCloud& C : ChaffClouds)
	{
		if ((Now - C.DropSessionTime) <= C.LifetimeSec)
		{
			Out.Add(C);
		}
	}
	return Out;
}

void AClearanceAirspaceManager::OnRep_ReplicatedAircraft()
{
	// Server is the authority - OnRep should never run server-side, but UE
	// sometimes fires it on the listen-server host in PIE multi-process setups.
	// Bail out so we don't mutate server-side AircraftStates from a stale view
	// of ReplicatedAircraft (which is what's making DLH101 re-appear after the
	// scenario clears it). - TripleA
	if (HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AirspaceMgr] OnRep fired on AUTHORITY - skipping (would have overwritten server state)"));
		return;
	}

	// SNAPSHOT the replicated array first - delegates fired below can re-enter
	// (HandleAircraftRegistered spawns actors which can trigger more replication
	// callbacks), and a ranged-for over ReplicatedAircraft would crash with
	// "Array has changed during ranged-for iteration!". - TripleA
	const TArray<FAircraftState> Snapshot = ReplicatedAircraft;

	FString Dbg;
	for (const FAircraftState& S : Snapshot)
	{
		Dbg += FString::Printf(TEXT(" %s"), *S.Callsign.ToString());
	}
	UE_LOG(LogTemp, Display, TEXT("[NET RepAircraft] count=%d%s"), Snapshot.Num(), *Dbg);

	TSet<FName> SeenNow;
	for (const FAircraftState& S : Snapshot)
	{
		if (S.Callsign == NAME_None) { continue; }
		SeenNow.Add(S.Callsign);
		const bool bWasPresent = AircraftStates.Contains(S.Callsign);
		AircraftStates.Add(S.Callsign, S);
		if (bWasPresent)
		{
			OnAircraftStateUpdated.Broadcast(S.Callsign);
		}
		else
		{
			OnAircraftRegistered.Broadcast(S.Callsign);
		}
	}

	TArray<FName> Gone;
	for (const TPair<FName, FAircraftState>& Pair : AircraftStates)
	{
		if (!SeenNow.Contains(Pair.Key)) { Gone.Add(Pair.Key); }
	}
	for (const FName& K : Gone)
	{
		AircraftStates.Remove(K);
		OnAircraftDeregistered.Broadcast(K);
	}
}

void AClearanceAirspaceManager::RebuildReplicatedArray()
{
	ReplicatedAircraft.Reset(AircraftStates.Num());
	for (const TPair<FName, FAircraftState>& Pair : AircraftStates)
	{
		ReplicatedAircraft.Add(Pair.Value);
	}
	// Trace with world NetMode so we can finally tell which actor (server vs
	// client local) is rebuilding. Two PIE windows have actors with the SAME
	// name in DIFFERENT worlds. - TripleA
	const TCHAR* RoleStr = TEXT("?");
	if (UWorld* W = GetWorld())
	{
		RoleStr = W->GetNetMode() == NM_Client ? TEXT("CLIENT-WORLD")
		        : W->GetNetMode() == NM_ListenServer ? TEXT("SERVER-WORLD")
		        : W->GetNetMode() == NM_DedicatedServer ? TEXT("DEDSRV")
		        : TEXT("STANDALONE");
	}
	FString Dump;
	for (const FAircraftState& S : ReplicatedAircraft) { Dump += FString::Printf(TEXT(" %s"), *S.Callsign.ToString()); }
	UE_LOG(LogTemp, Warning, TEXT("[REBUILD %s] %s[ac=%d]%s"), RoleStr, *GetName(), ReplicatedAircraft.Num(), *Dump);
}

void AClearanceAirspaceManager::BeginPlay()
{
	Super::BeginPlay();

	// Server initialises the environment; clients receive it via replication.
	// Without this gate the client would overwrite the replicated wind/runway
	// with its defaults (0/0) on join and pick the wrong active strip. - TripleA
	if (!HasAuthority()) { return; }

	SectorEnvironment.WindDirection = DefaultWindDirection;
	SectorEnvironment.WindSpeed = DefaultWindSpeed;
	SectorEnvironment.ActiveRunwayHeading = -1.f; // forces the first pick below

	RecalculateActiveRunway();
}

bool AClearanceAirspaceManager::RegisterAircraft(const FAircraftState& NewAircraft)
{
	// Authoritative mutation - clients only ever receive state via OnRep_ReplicatedAircraft.
	// If something on the client tries to call in here (rogue console, replicated delegate,
	// behaviour spawned client-side), we want to know about it AND refuse to mutate. - TripleA
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AirspaceMgr] BLOCKED RegisterAircraft on CLIENT for callsign %s"), *NewAircraft.Callsign.ToString());
		return false;
	}

	if (!ValidateState(NewAircraft))
	{
		return false;
	}

	if (AircraftStates.Contains(NewAircraft.Callsign))
	{
		return false; // already in the sector - don't clobber the live state
	}

	if (AircraftStates.Num() >= MaxAircraftCount)
	{
		return false;
	}

	FAircraftState State = NewAircraft;
	ClampStateValues(State);
	State.bIsValid = true;
	State.TimeEnteredSector = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	AircraftStates.Add(State.Callsign, State);
	RebuildReplicatedArray();
	UE_LOG(LogTemp, Warning, TEXT("[REGISTER] %s (count now %d)"), *State.Callsign.ToString(), AircraftStates.Num());
	OnAircraftRegistered.Broadcast(State.Callsign);
	return true;
}

bool AClearanceAirspaceManager::DeregisterAircraft(FName Callsign)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AirspaceMgr] BLOCKED DeregisterAircraft on CLIENT for callsign %s"), *Callsign.ToString());
		return false;
	}

	if (AircraftStates.Remove(Callsign) > 0)
	{
		RebuildReplicatedArray();
		UE_LOG(LogTemp, Warning, TEXT("[DEREGISTER] %s (count now %d)"), *Callsign.ToString(), AircraftStates.Num());
		OnAircraftDeregistered.Broadcast(Callsign);
		return true;
	}
	return false;
}

bool AClearanceAirspaceManager::RequestStateUpdate(const FAircraftState& UpdatedState)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AirspaceMgr] BLOCKED RequestStateUpdate on CLIENT for callsign %s"), *UpdatedState.Callsign.ToString());
		return false;
	}

	FAircraftState* Existing = AircraftStates.Find(UpdatedState.Callsign);
	if (!Existing)
	{
		return false; // nothing to update - reject rather than silently create one - TripleA
	}

	if (!ValidateState(UpdatedState))
	{
		return false;
	}

	FAircraftState State = UpdatedState;
	ClampStateValues(State);
	State.bIsValid = true;
	State.TimeEnteredSector = Existing->TimeEnteredSector; // registration-time fact, not the Behaviour's to change

	*Existing = State;
	RebuildReplicatedArray();
	OnAircraftStateUpdated.Broadcast(State.Callsign);
	return true;
}

FAircraftState AClearanceAirspaceManager::GetAircraftState(FName Callsign) const
{
	if (const FAircraftState* Found = AircraftStates.Find(Callsign))
	{
		return *Found;
	}
	return FAircraftState(); // bIsValid stays false
}

TArray<FAircraftState> AClearanceAirspaceManager::GetAllAircraftStates() const
{
	TArray<FAircraftState> Out;
	AircraftStates.GenerateValueArray(Out);
	return Out;
}

int32 AClearanceAirspaceManager::GetAircraftCount() const
{
	return AircraftStates.Num();
}

bool AClearanceAirspaceManager::IsCallsignRegistered(FName Callsign) const
{
	return AircraftStates.Contains(Callsign);
}

void AClearanceAirspaceManager::ClearAllAircraft()
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Warning, TEXT("[AirspaceMgr] BLOCKED ClearAllAircraft on CLIENT"));
		return;
	}

	TArray<FName> Callsigns;
	AircraftStates.GenerateKeyArray(Callsigns);

	AircraftStates.Empty();
	RebuildReplicatedArray();

	for (const FName& Callsign : Callsigns)
	{
		OnAircraftDeregistered.Broadcast(Callsign);
	}
}

FSectorEnvironment AClearanceAirspaceManager::GetCurrentEnvironment() const
{
	return SectorEnvironment;
}

void AClearanceAirspaceManager::InitialiseEnvironment(float WindDir, float WindSpeed)
{
	SectorEnvironment.WindDirection = FMath::Fmod(WindDir, 360.f);
	if (SectorEnvironment.WindDirection < 0.f)
	{
		SectorEnvironment.WindDirection += 360.f;
	}
	SectorEnvironment.WindSpeed = FMath::Max(0.f, WindSpeed);
	SectorEnvironment.ActiveRunwayHeading = -1.f; // force a fresh pick
	RecalculateActiveRunway();
}

void AClearanceAirspaceManager::SetRunways(const TArray<FRunwayInfo>& InRunways)
{
	Runways = InRunways;

	// Mirror the headings for display/Blueprint.
	SectorEnvironment.AvailableRunways.Reset();
	for (const FRunwayInfo& R : Runways)
	{
		SectorEnvironment.AvailableRunways.Add(R.HeadingDeg);
	}

	SectorEnvironment.ActiveRunwayHeading = -1.f; // re-pick with the new runways
	RecalculateActiveRunway();
}

void AClearanceAirspaceManager::UpdateWindConditions(float NewWindDirection, float NewWindSpeed)
{
	SectorEnvironment.WindDirection = FMath::Fmod(NewWindDirection, 360.f);
	if (SectorEnvironment.WindDirection < 0.f)
	{
		SectorEnvironment.WindDirection += 360.f;
	}
	SectorEnvironment.WindSpeed = FMath::Max(0.f, NewWindSpeed);

	RecalculateActiveRunway();
}

float AClearanceAirspaceManager::GetActiveRunway() const
{
	return SectorEnvironment.ActiveRunwayHeading;
}

bool AClearanceAirspaceManager::ValidateState(const FAircraftState& State) const
{
	if (State.Callsign.IsNone())
	{
		return false;
	}
	if (!FMath::IsFinite(State.Altitude) || !FMath::IsFinite(State.Speed) || !FMath::IsFinite(State.Heading))
	{
		return false;
	}
	return true;
}

void AClearanceAirspaceManager::ClampStateValues(FAircraftState& State) const
{
	State.Altitude = FMath::Clamp(State.Altitude, MinSafeAltitude, MaxSafeAltitude);
	State.Speed = FMath::Clamp(State.Speed, MinSafeSpeed, MaxSafeSpeed);

	State.Heading = FMath::Fmod(State.Heading, 360.f);
	if (State.Heading < 0.f)
	{
		State.Heading += 360.f;
	}
}

void AClearanceAirspaceManager::RecalculateActiveRunway()
{
	// Use the configured runways, or fall back to a default 09/27 strip at the
	// sector centre so selection still runs with none placed. - TripleA
	TArray<FRunwayInfo> Effective = Runways;
	if (Effective.Num() == 0)
	{
		FRunwayInfo A; A.ThresholdNm = FVector2D::ZeroVector; A.HeadingDeg = 90.f;  Effective.Add(A);
		FRunwayInfo B; B.ThresholdNm = FVector2D::ZeroVector; B.HeadingDeg = 270.f; Effective.Add(B);
	}

	// We land INTO wind. Score each runway by crosswind (want it low) minus headwind
	// (want it high): crosswind alone can't separate the two ends of one strip - they
	// share it exactly - so the headwind term is what makes the into-wind end win and
	// the downwind end lose. Lower score = better. - TripleA
	const float Wind = SectorEnvironment.WindDirection;
	auto RunwayCost = [&](float RunwayHeading)
	{
		const float Delta = FMath::DegreesToRadians(Wind - RunwayHeading);
		const float Crosswind = SectorEnvironment.WindSpeed * FMath::Abs(FMath::Sin(Delta));
		const float Headwind  = SectorEnvironment.WindSpeed * FMath::Cos(Delta);
		return Crosswind - Headwind;
	};

	int32 BestIdx = 0;
	float BestCost = RunwayCost(Effective[0].HeadingDeg);
	for (int32 i = 1; i < Effective.Num(); ++i)
	{
		const float C = RunwayCost(Effective[i].HeadingDeg);
		if (C < BestCost) { BestCost = C; BestIdx = i; }
	}
	const FRunwayInfo& Best = Effective[BestIdx];

	// Dead-band: don't switch away from the current runway for a marginal gain.
	const bool bHasCurrent = SectorEnvironment.ActiveRunwayHeading >= 0.f;
	if (bHasCurrent && !FMath::IsNearlyEqual(Best.HeadingDeg, SectorEnvironment.ActiveRunwayHeading))
	{
		const float CurrentCost = RunwayCost(SectorEnvironment.ActiveRunwayHeading);
		if (CurrentCost - BestCost < ClearanceConstants::RunwaySwitchDeadbandKts)
		{
			return;
		}
	}

	const bool bChanged = !FMath::IsNearlyEqual(Best.HeadingDeg, SectorEnvironment.ActiveRunwayHeading);
	SectorEnvironment.ActiveRunwayHeading = Best.HeadingDeg;
	SectorEnvironment.ActiveRunwayThreshold = FVector(Best.ThresholdNm.X, Best.ThresholdNm.Y, 0.f);
	if (bChanged)
	{
		OnRunwayChanged.Broadcast(Best.HeadingDeg);
	}
}
