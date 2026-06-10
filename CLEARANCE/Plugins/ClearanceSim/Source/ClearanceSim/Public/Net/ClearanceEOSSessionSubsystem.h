// Thin wrapper around the Online Subsystem session interface so the main menu
// Blueprint doesn't have to know about EOS-specific types. Three buttons:
// Host (creates a named session), Join (finds by code, joins, ClientTravel),
// Leave (destroys + returns to menu). The session "code" is just a short
// human-readable string the host picks; we store it as a session setting and
// search on it. Auth is anonymous Connect so players don't need an Epic
// account. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "ClearanceEOSSessionSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FClearanceEOSStatus, bool, bSuccess, const FString&, Message);

UCLASS()
class CLEARANCESIM_API UClearanceEOSSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Sign the local player into EOS Connect anonymously. Idempotent. Until
	// this succeeds, host/find/join will fail. Fires OnLoginComplete when
	// done. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Clearance|EOS")
	void Login();

	// Host advertises itself with the given SessionCode (e.g. "ATC-4729"). When
	// found, opens MapName as a listen server. Public, internet-visible session;
	// EOS handles NAT punchthrough. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Clearance|EOS")
	void HostSession(const FString& SessionCode, const FString& MapName);

	// Search EOS for a session matching this code, then join it and ClientTravel
	// to whatever map the host is on. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Clearance|EOS")
	void JoinSession(const FString& SessionCode);

	// Tear down whatever session we're in (hosted or joined).
	UFUNCTION(BlueprintCallable, Category = "Clearance|EOS")
	void LeaveSession();

	UPROPERTY(BlueprintAssignable, Category = "Clearance|EOS")
	FClearanceEOSStatus OnLoginComplete;

	UPROPERTY(BlueprintAssignable, Category = "Clearance|EOS")
	FClearanceEOSStatus OnHostComplete;

	UPROPERTY(BlueprintAssignable, Category = "Clearance|EOS")
	FClearanceEOSStatus OnJoinComplete;

	UPROPERTY(BlueprintAssignable, Category = "Clearance|EOS")
	FClearanceEOSStatus OnLeaveComplete;

	UFUNCTION(BlueprintCallable, Category = "Clearance|EOS")
	bool IsLoggedIn() const { return bLoggedIn; }

	UFUNCTION(BlueprintCallable, Category = "Clearance|EOS")
	FString GetActiveSessionCode() const { return PendingSessionCode; }

private:
	IOnlineSessionPtr GetSessions() const;

	void HandleLoginComplete(int32 LocalUserNum, bool bWasSuccessful, const FUniqueNetId& UserId, const FString& Error);
	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);

	// Default name UE picks if you don't override - keep it standard so
	// any existing online-subsystem helpers find it. - TripleA
	static const FName SessionName;

	// "ATC_CODE" - custom session attribute we use as the join key. - TripleA
	static const FName SessionCodeAttribute;

	FDelegateHandle LoginCompleteHandle;
	FDelegateHandle CreateCompleteHandle;
	FDelegateHandle FindCompleteHandle;
	FDelegateHandle JoinCompleteHandle;
	FDelegateHandle DestroyCompleteHandle;

	TSharedPtr<FOnlineSessionSearch> Search;
	FString PendingSessionCode;
	FString PendingHostMap;
	bool bLoggedIn = false;
};
