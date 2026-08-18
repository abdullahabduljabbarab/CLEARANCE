#include "ClearanceMenuFunctionLibrary.h"

#include "AudioCapture.h"
#include "AudioCaptureDeviceInterface.h"
#include "Engine/Engine.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformProcess.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Misc/Paths.h"
#include "IHeadMountedDisplay.h"
#include "IHeadMountedDisplayModule.h"
#include "IXRTrackingSystem.h"
#include "Features/IModularFeatures.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "VoiceModule.h"

namespace
{
	constexpr const TCHAR* kMainSimLevel = TEXT("/Game/LVL_Warton.LVL_Warton");
	constexpr const TCHAR* kOptionsSection = TEXT("ClearanceMenu.Options");
}

bool UClearanceMenuFunctionLibrary::bPendingLaunchAsInstructor = false;

// Process-wide CVar mirror of bPendingLaunchAsInstructor. Cross-module
// readable by ClearanceSim plugin without a hard include on this class. - TripleA
static TAutoConsoleVariable<int32> CVarClearanceRoleInstructor(
	TEXT("clearance.role.instructor"),
	0,
	TEXT("1 = last menu button chose instructor role, 0 = operator (default)"),
	ECVF_Default);

namespace
{
	void SetPendingInstructorLatch(bool bAsInstructor)
	{
		UClearanceMenuFunctionLibrary::bPendingLaunchAsInstructor = bAsInstructor;
		CVarClearanceRoleInstructor->Set(bAsInstructor ? 1 : 0);
	}
}

bool UClearanceMenuFunctionLibrary::IsHMDConnected()
{
	// Fast path: XR is already initialized (someone already entered VR this
	// session and came back to the menu). Query the live device directly.
	if (GEngine && GEngine->XRSystem.IsValid())
	{
		IHeadMountedDisplay* HMD = GEngine->XRSystem->GetHMDDevice();
		if (HMD && HMD->IsHMDConnected())
		{
			UE_LOG(LogTemp, Warning, TEXT("[ClearanceMenu] IsHMDConnected: true (via live XRSystem)."));
			return true;
		}
	}

	// Fallback: enumerate loaded HMD modules and ask each whether their
	// backing runtime reports a connected headset. This path works BEFORE
	// XR is initialized - important for a menu that hasn't entered VR yet.
	// OculusXR / OpenXR / SteamVR each implement IHeadMountedDisplayModule
	// and their IsHMDConnected() talks to their respective runtime SDK
	// without spinning up the full XR system. - TripleA
	IModularFeatures& Features = IModularFeatures::Get();
	const FName HMDFeatureName = IHeadMountedDisplayModule::GetModularFeatureName();
	TArray<IHeadMountedDisplayModule*> HMDModules = Features.GetModularFeatureImplementations<IHeadMountedDisplayModule>(HMDFeatureName);
	for (IHeadMountedDisplayModule* HMDModule : HMDModules)
	{
		if (HMDModule && HMDModule->IsHMDConnected())
		{
			UE_LOG(LogTemp, Log,
				TEXT("[ClearanceMenu] IsHMDConnected: true (via HMD module '%s' without XRSystem init)."),
				*HMDModule->GetModuleKeyName());
			return true;
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[ClearanceMenu] IsHMDConnected: false. XRSystem.IsValid=%s, HMD modules found=%d."),
		(GEngine && GEngine->XRSystem.IsValid()) ? TEXT("true") : TEXT("false"),
		HMDModules.Num());
	return false;
}

bool UClearanceMenuFunctionLibrary::IsMicrophoneAvailable()
{
	// Two-step check. First: is the Voice module even available on this
	// platform. Second: does at least one audio capture device exist right
	// now. FVoiceModule alone reports platform capability, not device
	// presence - a Windows PC with no mic plugged in still reports "voice
	// capture supported." Combine with Audio::FAudioCapture device
	// enumeration to also verify at least one real input device. - TripleA
	if (!FVoiceModule::IsAvailable())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClearanceMenu] IsMicrophoneAvailable: false (Voice module unavailable)."));
		return false;
	}
	if (!FVoiceModule::Get().DoesPlatformSupportVoiceCapture())
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClearanceMenu] IsMicrophoneAvailable: false (platform doesn't support voice capture)."));
		return false;
	}

	Audio::FAudioCapture Capture;
	TArray<Audio::FCaptureDeviceInfo> Infos;
	Capture.GetCaptureDevicesAvailable(Infos);
	if (Infos.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ClearanceMenu] IsMicrophoneAvailable: false (0 capture devices)."));
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[ClearanceMenu] IsMicrophoneAvailable: true (%d capture devices, first: '%s')."),
		Infos.Num(), *Infos[0].DeviceName);
	return true;
}

FString UClearanceMenuFunctionLibrary::GetLocalIPv4()
{
	ISocketSubsystem* SS = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SS)
	{
		return TEXT("127.0.0.1");
	}

	TArray<TSharedPtr<FInternetAddr>> Adapters;
	if (SS->GetLocalAdapterAddresses(Adapters))
	{
		for (const TSharedPtr<FInternetAddr>& Addr : Adapters)
		{
			if (!Addr.IsValid())
			{
				continue;
			}
			uint32 Raw = 0;
			Addr->GetIp(Raw);
			const FIPv4Address IPv4(Raw);
			// Skip loopback (127.x) and link-local (169.254.x). Anything
			// else on a private LAN class (192.168 / 10.x / 172.16-31)
			// is what we want to display to the peer joining us. - TripleA
			const uint8 A = (Raw >> 24) & 0xFF;
			const uint8 B = (Raw >> 16) & 0xFF;
			if (A == 127) { continue; }
			if (A == 169 && B == 254) { continue; }
			return IPv4.ToString();
		}
	}

	// Fallback: whatever the socket subsystem thinks the local host is.
	bool bCanBind = false;
	TSharedRef<FInternetAddr> Local = SS->GetLocalHostAddr(*GLog, bCanBind);
	if (Local->IsValid())
	{
		uint32 Raw = 0;
		Local->GetIp(Raw);
		return FIPv4Address(Raw).ToString();
	}
	return TEXT("127.0.0.1");
}

namespace
{
	// EnableHMD(true) starts the XR runtime session so the operator's VR
	// pawn actually renders through the headset. UE only auto-enables VR
	// when the app is launched with `-vr`; switching levels in-app doesn't
	// touch HMD state. Instructor sessions get EnableHMD(false) explicitly
	// so a player who was previously in VR and came back to the menu ends
	// up on a proper flat-desktop instructor panel view, not a frozen
	// stereo frame. Both calls are safe no-ops when no HMD is connected. - TripleA
	void SetHMDForSession(bool bWantHMD)
	{
		if (bWantHMD)
		{
			UHeadMountedDisplayFunctionLibrary::EnableHMD(true);
		}
		else
		{
			UHeadMountedDisplayFunctionLibrary::EnableHMD(false);
		}
	}
}

void UClearanceMenuFunctionLibrary::OpenInstructorSessionSolo(UObject* WorldContextObject)
{
	if (!WorldContextObject) { return; }
	SetPendingInstructorLatch(true);
	SetHMDForSession(false);
	UGameplayStatics::OpenLevel(
		WorldContextObject,
		FName(kMainSimLevel),
		/*bAbsolute=*/true,
		TEXT("role=instructor"));
}

void UClearanceMenuFunctionLibrary::OpenOperatorSessionSolo(UObject* WorldContextObject)
{
	if (!WorldContextObject) { return; }
	SetPendingInstructorLatch(false);
	SetHMDForSession(true);
	UGameplayStatics::OpenLevel(
		WorldContextObject,
		FName(kMainSimLevel),
		/*bAbsolute=*/true,
		TEXT("role=operator?autonomous=1"));
}

void UClearanceMenuFunctionLibrary::OpenCombinedSessionSolo(UObject* WorldContextObject)
{
	if (!WorldContextObject) { return; }

	// Combined = 2 humans on 1 PC. Same architecture as HOST LAN + JOIN LAN
	// with two physical machines, except the "other PC" is 127.0.0.1 and
	// the second process launches automatically. UE local multiplayer via
	// UGameplayStatics::CreatePlayer doesn't give the two players separate
	// windows in a packaged build (they share one viewport, split-screen,
	// which VR takes exclusive control over), so we use two OS-level
	// processes instead - the same way PIE with "Number of Players: 2"
	// actually works under the hood. - TripleA

	// This process becomes the VR operator, hosting on localhost.
	SetPendingInstructorLatch(false);
	SetHMDForSession(true);
	UGameplayStatics::OpenLevel(
		WorldContextObject,
		FName(kMainSimLevel),
		/*bAbsolute=*/true,
		TEXT("listen?role=operator"));

	// Skip the shell-launch when running inside the editor - it would spawn
	// another instance of the editor.exe which is huge, slow, and not what
	// the tester wants. For in-editor combined-mode testing, use PIE's
	// "Number of Players: 2 / Play as Listen Server" instead. In a
	// packaged shipping build this branch fires and spawns the instructor
	// client alongside the VR host. - TripleA
	if (GIsEditor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CombinedSession] Skipping second-process launch: running in editor. "
			     "Combined session only spawns the instructor client in a packaged build. "
			     "For editor testing use PIE > Number of Players: 2 > Play as Listen Server."));
		return;
	}

	const FString ExePath = FPlatformProcess::ExecutablePath();
	// Args: the URL is the first positional argument, then any engine
	// flags. `-windowed -resx=1600 -resy=900` keeps the instructor window
	// non-fullscreen so the VR mirror can also be visible on the desktop
	// if the user wants. - TripleA
	const FString Args = TEXT("127.0.0.1?role=instructor -windowed -resx=1600 -resy=900");

	uint32 SpawnedPID = 0;
	FProcHandle Proc = FPlatformProcess::CreateProc(
		*ExePath,
		*Args,
		/*bLaunchDetached=*/true,
		/*bLaunchHidden=*/false,
		/*bLaunchReallyHidden=*/false,
		&SpawnedPID,
		/*PriorityModifier=*/0,
		/*OptionalWorkingDirectory=*/nullptr,
		/*PipeWriteChild=*/nullptr,
		/*PipeReadChild=*/nullptr);

	if (Proc.IsValid())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[CombinedSession] Launched second process PID=%u as instructor client "
			     "connecting to 127.0.0.1. Exe: %s"),
			SpawnedPID, *ExePath);
		FPlatformProcess::CloseProc(Proc);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombinedSession] Failed to launch second process at '%s'. "
			     "Instructor client will not appear."), *ExePath);
	}
}

void UClearanceMenuFunctionLibrary::HostLANSession(UObject* WorldContextObject)
{
	if (!WorldContextObject) { return; }
	// Host = operator per CLEARANCE's network convention: whoever runs the
	// listen server has the VR pawn locally (best latency for the immersive
	// side). Joining instructor peers connect via JoinLANSession below. See
	// AClearanceOperatorPC::BeginPlay for the role dispatch. - TripleA
	SetPendingInstructorLatch(false);
	SetHMDForSession(true);
	UGameplayStatics::OpenLevel(
		WorldContextObject,
		FName(kMainSimLevel),
		/*bAbsolute=*/true,
		TEXT("listen?role=operator"));
}

void UClearanceMenuFunctionLibrary::JoinLANSession(APlayerController* PlayerController, const FString& HostIP)
{
	if (!PlayerController) { return; }
	const FString Trimmed = HostIP.TrimStartAndEnd();
	if (Trimmed.IsEmpty()) { return; }
	// Client = instructor per CLEARANCE's network convention: remote peers
	// join to observe / inject events on the operator's authoritative sim.
	// Appending `?role=instructor` carries the role through the connect URL
	// so the server sees it in InitNewPlayer and the client sees it in the
	// resulting World URL on the far side. - TripleA
	SetPendingInstructorLatch(true);
	SetHMDForSession(false);
	PlayerController->ClientTravel(Trimmed + TEXT("?role=instructor"), ETravelType::TRAVEL_Absolute);
}

void UClearanceMenuFunctionLibrary::QuitToDesktop(APlayerController* PlayerController)
{
	UKismetSystemLibrary::QuitGame(
		PlayerController ? PlayerController->GetWorld() : nullptr,
		PlayerController,
		EQuitPreference::Quit,
		/*bIgnorePlatformRestrictions=*/false);
}

void UClearanceMenuFunctionLibrary::ExitToMainMenu(UObject* WorldContextObject)
{
	if (!WorldContextObject) { return; }
	// Disable HMD before opening the flat menu level so a returning VR
	// operator lands on a proper desktop menu, not a stereo one. Safe
	// no-op if HMD wasn't active. - TripleA
	SetHMDForSession(false);
	UGameplayStatics::OpenLevel(
		WorldContextObject,
		FName(TEXT("/Game/LVL_MainMenu.LVL_MainMenu")),
		/*bAbsolute=*/true);
}

// ---------------------------------------------------------------------
// Options persistence + audio device enumeration
// ---------------------------------------------------------------------

TArray<FString> UClearanceMenuFunctionLibrary::GetInputDeviceNames()
{
	TArray<FString> Names;

	// Query the platform audio capture layer for currently connected input
	// devices. FAudioCapture wraps whichever platform backend is active
	// (WASAPI on Windows) and returns a flat list of FCaptureDeviceInfo. If
	// the platform has no devices at all (rare - virtual "default" always
	// exists on Windows) the list will just be empty. - TripleA
	Audio::FAudioCapture Capture;
	TArray<Audio::FCaptureDeviceInfo> Infos;
	Capture.GetCaptureDevicesAvailable(Infos);
	Names.Reserve(Infos.Num());
	for (const Audio::FCaptureDeviceInfo& Info : Infos)
	{
		Names.Add(Info.DeviceName);
	}
	return Names;
}

int32 UClearanceMenuFunctionLibrary::LoadSelectedInputDeviceIndex()
{
	int32 Idx = 0;
	GConfig->GetInt(kOptionsSection, TEXT("InputDeviceIndex"), Idx, GGameUserSettingsIni);
	return FMath::Max(0, Idx);
}

void UClearanceMenuFunctionLibrary::SaveSelectedInputDeviceIndex(int32 DeviceIndex)
{
	GConfig->SetInt(kOptionsSection, TEXT("InputDeviceIndex"), FMath::Max(0, DeviceIndex), GGameUserSettingsIni);
	GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
}

float UClearanceMenuFunctionLibrary::LoadMasterVolume()
{
	float V = 0.8f;
	GConfig->GetFloat(kOptionsSection, TEXT("MasterVolume"), V, GGameUserSettingsIni);
	return FMath::Clamp(V, 0.f, 1.f);
}

void UClearanceMenuFunctionLibrary::SaveMasterVolume(float Volume0To1)
{
	const float Clamped = FMath::Clamp(Volume0To1, 0.f, 1.f);
	GConfig->SetFloat(kOptionsSection, TEXT("MasterVolume"), Clamped, GGameUserSettingsIni);
	GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
}

FKey UClearanceMenuFunctionLibrary::LoadPushToTalkKey()
{
	// Default: Left Alt. Matches the standard PTT convention on real ATC and
	// combat sim rigs; big physical key, not confused with typing keys. - TripleA
	FString KeyName;
	GConfig->GetString(kOptionsSection, TEXT("PushToTalkKey"), KeyName, GGameUserSettingsIni);
	if (KeyName.IsEmpty())
	{
		return EKeys::LeftAlt;
	}
	FKey K(*KeyName);
	return K.IsValid() ? K : EKeys::LeftAlt;
}

void UClearanceMenuFunctionLibrary::SavePushToTalkKey(FKey Key)
{
	const FKey ToSave = Key.IsValid() ? Key : EKeys::LeftAlt;
	GConfig->SetString(kOptionsSection, TEXT("PushToTalkKey"), *ToSave.GetFName().ToString(), GGameUserSettingsIni);
	GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
}

bool UClearanceMenuFunctionLibrary::LoadStartInVR()
{
	// Default true because OPERATOR SESSION and HOST LAN both hard-gate on
	// HMD anyway - honouring "start in VR" on first launch matches the
	// expectation of a player who plugged in their headset. - TripleA
	bool bValue = true;
	GConfig->GetBool(kOptionsSection, TEXT("StartInVR"), bValue, GGameUserSettingsIni);
	return bValue;
}

void UClearanceMenuFunctionLibrary::SaveStartInVR(bool bStartInVR)
{
	GConfig->SetBool(kOptionsSection, TEXT("StartInVR"), bStartInVR, GGameUserSettingsIni);
	GConfig->Flush(/*bRead=*/false, GGameUserSettingsIni);
}

// ---------------------------------------------------------------------
// PTT key capture. Registers a global Slate input pre-processor that
// intercepts the next key down event before any widget sees it, fires
// the BP delegate, unregisters itself. Keeps state entirely in C++ -
// the widget only calls two BlueprintCallable statics. - TripleA
// ---------------------------------------------------------------------

namespace ClearanceMenuFuncLib_Private
{
	class FPTTKeyCaptureProcessor
		: public IInputProcessor
		, public TSharedFromThis<FPTTKeyCaptureProcessor>
	{
	public:
		FClearancePTTKeyCapturedDelegate Callback;

		virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
		{
			FKey Captured = InKeyEvent.GetKey();
			// Escape = user cancelled the rebind; report Invalid so the BP
			// handler can distinguish "cancel" from "key chosen". - TripleA
			if (Captured == EKeys::Escape)
			{
				Captured = EKeys::Invalid;
			}
			if (Callback.IsBound())
			{
				Callback.Execute(Captured);
			}
			// Unregister ourselves after firing exactly once.
			UnregisterFromSlate();
			// Return true = consume the event so the "chosen" key doesn't
			// leak into whatever widget currently has focus. - TripleA
			return true;
		}

		void UnregisterFromSlate()
		{
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication::Get().UnregisterInputPreProcessor(SharedThis(this));
			}
		}
	};

	// Only one PTT capture may be armed at a time. If BeginPTTKeyCapture is
	// called while one exists, the older one is silently cancelled first.
	static TSharedPtr<FPTTKeyCaptureProcessor> GCurrentCapture;
}

void UClearanceMenuFunctionLibrary::BeginPTTKeyCapture(const FClearancePTTKeyCapturedDelegate& OnCaptured)
{
	using namespace ClearanceMenuFuncLib_Private;

	if (!FSlateApplication::IsInitialized())
	{
		return;
	}

	if (GCurrentCapture.IsValid())
	{
		GCurrentCapture->UnregisterFromSlate();
		GCurrentCapture.Reset();
	}

	GCurrentCapture = MakeShared<FPTTKeyCaptureProcessor>();
	GCurrentCapture->Callback = OnCaptured;
	FSlateApplication::Get().RegisterInputPreProcessor(GCurrentCapture);
}

void UClearanceMenuFunctionLibrary::CancelPTTKeyCapture()
{
	using namespace ClearanceMenuFuncLib_Private;

	if (GCurrentCapture.IsValid())
	{
		GCurrentCapture->UnregisterFromSlate();
		GCurrentCapture.Reset();
	}
}

void UClearanceMenuFunctionLibrary::BeginPTTKeyCaptureByName(UObject* Target, FName FunctionName)
{
	if (!Target)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ClearanceMenu] BeginPTTKeyCaptureByName: Target is null."));
		return;
	}
	if (FunctionName.IsNone())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ClearanceMenu] BeginPTTKeyCaptureByName: FunctionName is None."));
		return;
	}

	// FindFunction only walks the object's own UClass hierarchy, so this
	// works on any UUserWidget subclass without extra plumbing. If the
	// name doesn't resolve or the signature doesn't match, BindUFunction
	// will still succeed silently and the delegate execute would crash at
	// call time - so we verify resolution up front and log for the BP
	// author's benefit. - TripleA
	if (!Target->FindFunction(FunctionName))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ClearanceMenu] BeginPTTKeyCaptureByName: %s has no UFUNCTION '%s'. "
			     "Function must be BlueprintCallable / BlueprintImplementableEvent and "
			     "take a single FKey parameter."),
			*GetNameSafe(Target), *FunctionName.ToString());
		return;
	}

	FClearancePTTKeyCapturedDelegate Delegate;
	Delegate.BindUFunction(Target, FunctionName);
	BeginPTTKeyCapture(Delegate);
}
