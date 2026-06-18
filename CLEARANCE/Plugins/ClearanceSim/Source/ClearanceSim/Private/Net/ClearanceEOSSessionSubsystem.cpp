#include "Net/ClearanceEOSSessionSubsystem.h"
#include "OnlineSubsystem.h"
#include "OnlineSessionSettings.h"
#include "Interfaces/OnlineIdentityInterface.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "TimerManager.h"

const FName UClearanceEOSSessionSubsystem::SessionName(TEXT("GameSession"));
const FName UClearanceEOSSessionSubsystem::SessionCodeAttribute(TEXT("ATC_CODE"));

void UClearanceEOSSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("[ClearanceEOS] Subsystem initialised"));
}

void UClearanceEOSSessionSubsystem::Deinitialize()
{
	if (IOnlineSubsystem* OSS = IOnlineSubsystem::Get())
	{
		if (IOnlineIdentityPtr Id = OSS->GetIdentityInterface())
		{
			if (LoginCompleteHandle.IsValid())
			{
				Id->ClearOnLoginCompleteDelegate_Handle(0, LoginCompleteHandle);
			}
		}
		if (IOnlineSessionPtr Sessions = GetSessions())
		{
			if (CreateCompleteHandle.IsValid())  { Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle); }
			if (FindCompleteHandle.IsValid())    { Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle); }
			if (JoinCompleteHandle.IsValid())    { Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle); }
			if (DestroyCompleteHandle.IsValid()) { Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle); }
		}
	}
	Super::Deinitialize();
}

IOnlineSessionPtr UClearanceEOSSessionSubsystem::GetSessions() const
{
	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	return OSS ? OSS->GetSessionInterface() : nullptr;
}

void UClearanceEOSSessionSubsystem::Login()
{
	if (bLoggedIn)
	{
		OnLoginComplete.Broadcast(true, TEXT("already logged in"));
		return;
	}

	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	if (!OSS)
	{
		OnLoginComplete.Broadcast(false, TEXT("no online subsystem - check DefaultEngine.ini"));
		return;
	}
	IOnlineIdentityPtr Id = OSS->GetIdentityInterface();
	if (!Id.IsValid())
	{
		OnLoginComplete.Broadcast(false, TEXT("no identity interface"));
		return;
	}

	LoginCompleteHandle = Id->AddOnLoginCompleteDelegate_Handle(0,
		FOnLoginCompleteDelegate::CreateUObject(this, &UClearanceEOSSessionSubsystem::HandleLoginComplete));

	// EAS AccountPortal flow - opens the system browser, player signs into
	// their Epic account, EOS bridges that into a Connect product user id.
	// First-time flow only - cached afterwards. - TripleA
	FOnlineAccountCredentials Creds;
	Creds.Type  = TEXT("AccountPortal");
	Creds.Id    = TEXT("");
	Creds.Token = TEXT("");
	Id->Login(0, Creds);
}

void UClearanceEOSSessionSubsystem::HandleLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error)
{
	bLoggedIn = bWasSuccessful;
	if (IOnlineSubsystem* OSS = IOnlineSubsystem::Get())
	{
		if (IOnlineIdentityPtr Id = OSS->GetIdentityInterface())
		{
			Id->ClearOnLoginCompleteDelegate_Handle(LocalUserNum, LoginCompleteHandle);
		}
	}
	const FString Msg = bWasSuccessful ? FString::Printf(TEXT("logged in as %s"), *UserId.ToString())
	                                   : FString::Printf(TEXT("login failed: %s"), *Error);
	UE_LOG(LogTemp, Log, TEXT("[ClearanceEOS] %s"), *Msg);
	OnLoginComplete.Broadcast(bWasSuccessful, Msg);
}

void UClearanceEOSSessionSubsystem::HostSession(const FString& SessionCode, const FString& MapName)
{
	if (!bLoggedIn) { OnHostComplete.Broadcast(false, TEXT("login first")); return; }
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid()) { OnHostComplete.Broadcast(false, TEXT("no sessions iface")); return; }

	PendingSessionCode = SessionCode.ToUpper();
	PendingHostMap = MapName;

	FOnlineSessionSettings S;
	S.NumPublicConnections      = 8;     // operator + up to 7 instructors
	S.NumPrivateConnections     = 0;
	S.bShouldAdvertise          = true;
	S.bAllowJoinInProgress      = true;
	S.bIsLANMatch               = false;
	S.bUsesPresence             = false;
	S.bAllowInvites             = true;
	S.bAllowJoinViaPresence     = false;
	S.bUseLobbiesIfAvailable    = false;  // we disabled Lobbies in the EOS policy
	S.bUsesStats                = false;

	// Stash the session code as a queryable string attribute so JoinSession's
	// search can match on it. - TripleA
	S.Set(SessionCodeAttribute, PendingSessionCode,
		EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	CreateCompleteHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &UClearanceEOSSessionSubsystem::HandleCreateSessionComplete));

	Sessions->CreateSession(0, SessionName, S);
}

void UClearanceEOSSessionSubsystem::HandleCreateSessionComplete(FName InSessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateCompleteHandle);
	}
	if (!bWasSuccessful)
	{
		OnHostComplete.Broadcast(false, TEXT("CreateSession failed"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[ClearanceEOS] Hosted session '%s' code=%s -> opening %s"),
		*InSessionName.ToString(), *PendingSessionCode, *PendingHostMap);
	OnHostComplete.Broadcast(true, FString::Printf(TEXT("hosting %s"), *PendingSessionCode));

	if (UWorld* W = GetWorld())
	{
		const FString Cmd = PendingHostMap + TEXT("?listen");
		UGameplayStatics::OpenLevel(W, FName(*Cmd), true, FString());
	}
}

void UClearanceEOSSessionSubsystem::JoinSession(const FString& SessionCode)
{
	if (!bLoggedIn) { OnJoinComplete.Broadcast(false, TEXT("login first")); return; }
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid()) { OnJoinComplete.Broadcast(false, TEXT("no sessions iface")); return; }

	PendingSessionCode = SessionCode.ToUpper();

	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery     = false;
	Search->MaxSearchResults = 50;
	Search->QuerySettings.Set(SessionCodeAttribute, PendingSessionCode, EOnlineComparisonOp::Equals);

	FindCompleteHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &UClearanceEOSSessionSubsystem::HandleFindSessionsComplete));

	Sessions->FindSessions(0, Search.ToSharedRef());
}

void UClearanceEOSSessionSubsystem::HandleFindSessionsComplete(bool bWasSuccessful)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindCompleteHandle);
	}
	if (!bWasSuccessful || !Search.IsValid() || Search->SearchResults.Num() == 0)
	{
		OnJoinComplete.Broadcast(false, FString::Printf(TEXT("no session for code %s"), *PendingSessionCode));
		return;
	}

	JoinCompleteHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &UClearanceEOSSessionSubsystem::HandleJoinSessionComplete));

	Sessions->JoinSession(0, SessionName, Search->SearchResults[0]);
}

void UClearanceEOSSessionSubsystem::HandleJoinSessionComplete(FName InSessionName, EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (Sessions.IsValid())
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinCompleteHandle);
	}
	if (Result != EOnJoinSessionCompleteResult::Success)
	{
		OnJoinComplete.Broadcast(false, TEXT("JoinSession failed"));
		return;
	}

	// Pull the connect string from the joined session and ClientTravel to it. - TripleA
	FString ConnectStr;
	if (Sessions.IsValid() && Sessions->GetResolvedConnectString(InSessionName, ConnectStr))
	{
		if (APlayerController* PC = GetGameInstance() ? GetGameInstance()->GetFirstLocalPlayerController(GetWorld()) : nullptr)
		{
			UE_LOG(LogTemp, Log, TEXT("[ClearanceEOS] Joined - traveling to %s"), *ConnectStr);
			PC->ClientTravel(ConnectStr, ETravelType::TRAVEL_Absolute);
			OnJoinComplete.Broadcast(true, FString::Printf(TEXT("joined %s"), *PendingSessionCode));
			return;
		}
	}
	OnJoinComplete.Broadcast(false, TEXT("joined but couldn't resolve connect string"));
}

void UClearanceEOSSessionSubsystem::LeaveSession()
{
	IOnlineSessionPtr Sessions = GetSessions();
	if (!Sessions.IsValid()) { OnLeaveComplete.Broadcast(false, TEXT("no sessions iface")); return; }

	DestroyCompleteHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &UClearanceEOSSessionSubsystem::HandleDestroySessionComplete));

	Sessions->DestroySession(SessionName);
}

void UClearanceEOSSessionSubsystem::HandleDestroySessionComplete(FName InSessionName, bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessions())
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyCompleteHandle);
	}
	PendingSessionCode.Reset();
	OnLeaveComplete.Broadcast(bWasSuccessful, bWasSuccessful ? TEXT("left session") : TEXT("DestroySession failed"));
}

// ---------------------------------------------------------------------------
// Console commands so the multiplayer flow can be driven without a UMG main
// menu existing yet. Same Blueprint API, exercised from in-game console. - TripleA
// ---------------------------------------------------------------------------
namespace
{
	UClearanceEOSSessionSubsystem* FindEOS(UWorld* World)
	{
		if (!World) { return nullptr; }
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UClearanceEOSSessionSubsystem>();
		}
		return nullptr;
	}

	void Toast(const FString& Msg, FColor C = FColor::Cyan)
	{
		if (GEngine) { GEngine->AddOnScreenDebugMessage(-1, 5.f, C, Msg); }
		UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GClearanceEOSLoginCmd(
	TEXT("clearance.eos.login"),
	TEXT("clearance.eos.login - sign in to EOS Connect anonymously (do this once before host/join)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UClearanceEOSSessionSubsystem* EOS = FindEOS(World);
		if (!EOS) { Toast(TEXT("eos: no subsystem - is the GameInstance up?"), FColor::Red); return; }
		Toast(TEXT("eos: logging in..."));
		EOS->Login();
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceEOSHostCmd(
	TEXT("clearance.eos.host"),
	TEXT("clearance.eos.host <code> [map] - host a session under <code>, default map = current"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UClearanceEOSSessionSubsystem* EOS = FindEOS(World);
		if (!EOS) { Toast(TEXT("eos: no subsystem"), FColor::Red); return; }
		if (Args.Num() < 1)
		{
			Toast(TEXT("usage: clearance.eos.host <code> [map]"), FColor::Yellow);
			return;
		}
		const FString Code = Args[0];
		FString Map;
		if (Args.Num() >= 2)
		{
			Map = Args[1];
		}
		else if (World)
		{
			// Default: open the current level as a listen server.
			Map = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
		}
		Toast(FString::Printf(TEXT("eos: hosting '%s' on %s"), *Code, *Map));
		EOS->HostSession(Code, Map);
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceEOSJoinCmd(
	TEXT("clearance.eos.join"),
	TEXT("clearance.eos.join <code> - search EOS for session <code> and join it"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UClearanceEOSSessionSubsystem* EOS = FindEOS(World);
		if (!EOS) { Toast(TEXT("eos: no subsystem"), FColor::Red); return; }
		if (Args.Num() < 1)
		{
			Toast(TEXT("usage: clearance.eos.join <code>"), FColor::Yellow);
			return;
		}
		Toast(FString::Printf(TEXT("eos: searching for '%s'..."), *Args[0]));
		EOS->JoinSession(Args[0]);
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceEOSLeaveCmd(
	TEXT("clearance.eos.leave"),
	TEXT("clearance.eos.leave - destroy the local session (host or client)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UClearanceEOSSessionSubsystem* EOS = FindEOS(World);
		if (!EOS) { Toast(TEXT("eos: no subsystem"), FColor::Red); return; }
		Toast(TEXT("eos: leaving..."));
		EOS->LeaveSession();
	}));

static FAutoConsoleCommandWithWorldAndArgs GClearanceEOSStatusCmd(
	TEXT("clearance.eos.status"),
	TEXT("clearance.eos.status - dump current login + session state"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic([](const TArray<FString>& Args, UWorld* World)
	{
		UClearanceEOSSessionSubsystem* EOS = FindEOS(World);
		if (!EOS) { Toast(TEXT("eos: no subsystem"), FColor::Red); return; }
		const FString Msg = FString::Printf(TEXT("eos: loggedIn=%d  code='%s'"),
			EOS->IsLoggedIn() ? 1 : 0,
			*EOS->GetActiveSessionCode());
		Toast(Msg);
	}));
