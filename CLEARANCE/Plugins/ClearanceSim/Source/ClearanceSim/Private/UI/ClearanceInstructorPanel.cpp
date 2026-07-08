#include "UI/ClearanceInstructorPanel.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Airspace/ClearanceViolationZone.h"
#include "Airspace/ClearanceRestrictedArea.h"
#include "Airspace/ClearanceWaypoint.h"
#include "Safety/ClearanceRadarSite.h"
#include "Safety/ClearanceRadar.h"
#include "Simulation/ClearanceDISEmitter.h"
#include "Simulation/ClearanceDISReceiver.h"
#include "Simulation/ClearanceDDSEmitter.h"
#include "Simulation/ClearanceDDSReceiver.h"
#include "Scenario/ClearanceScenarioRunner.h"
#include "Components/ScrollBox.h"
#include "Components/Image.h"
#include "Components/Slider.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"

UClearanceInstructorPanel::UClearanceInstructorPanel(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Default the row widget class so the panel works out of the box - no
	// editor-side property wiring needed in the UMG subclass. - TripleA
	static ConstructorHelpers::FClassFinder<UUserWidget> RowBP(TEXT("/Game/UI/WBP_InstructorAircraftRow"));
	if (RowBP.Succeeded())
	{
		AircraftRowClass = RowBP.Class;
	}
}

void UClearanceInstructorPanel::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshLocalRefs();
}

// --- Overview mouse handling ----------------------------------------------
//
// Drag + zoom + double-click reset live in C++ because the UMG paint-event /
// mouse-event overrides have to bind to the parent class's NativeOn... pair,
// which the MCP editor tool can't reliably author from the Widget Blueprint
// side. C++ here forwards into AddOverviewPan / AddOverviewZoom /
// ResetOverviewView on the controller; the controller owns the actual pan +
// zoom state so it persists across view switches. - TripleA

bool UClearanceInstructorPanel::IsOverviewActiveForInput() const
{
	if (!bShowCameraView || !CachedController) { return false; }
	return CachedController->GetInstructorPipView() == EClearanceCameraView::Overview;
}

FReply UClearanceInstructorPanel::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && IsOverviewActiveForInput())
	{
		bOverviewDragging = true;
		OverviewDragLastScreenPos = InMouseEvent.GetScreenSpacePosition();
		SetCursor(EMouseCursor::GrabHandClosed);
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UClearanceInstructorPanel::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bOverviewDragging && Img_CameraFeed && CachedController)
	{
		const FVector2D Current = InMouseEvent.GetScreenSpacePosition();
		const FVector2D ImageSize = Img_CameraFeed->GetCachedGeometry().GetLocalSize();
		if (ImageSize.X > KINDA_SMALL_NUMBER && ImageSize.Y > KINDA_SMALL_NUMBER)
		{
			// No negation - drag direction = camera direction, so dragging
			// right pans the view right. Feels like steering the camera
			// rather than dragging the world. - TripleA
			const FVector2D Delta = (Current - OverviewDragLastScreenPos) / ImageSize;
			CachedController->AddOverviewPan(Delta);
			OverviewDragLastScreenPos = Current;
		}
	}
	return Super::NativeOnMouseMove(InGeometry, InMouseEvent);
}

FReply UClearanceInstructorPanel::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bOverviewDragging)
	{
		bOverviewDragging = false;
		SetCursor(EMouseCursor::Default);
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
}

FReply UClearanceInstructorPanel::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (IsOverviewActiveForInput())
	{
		CachedController->AddOverviewZoom(InMouseEvent.GetWheelDelta() * 0.1f);
		return FReply::Handled();
	}
	return Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
}

FReply UClearanceInstructorPanel::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && IsOverviewActiveForInput())
	{
		CachedController->ResetOverviewView();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
}

int32 UClearanceInstructorPanel::NativePaint(const FPaintArgs& Args,
	const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 Result = Super::NativePaint(Args, AllottedGeometry, MyCullingRect,
		OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	// Master gate first - when the PERFORMANCE tab (or any other non-scope tab)
	// is active, neither paint event should fire. Child-widget collapse on the
	// scope/camera VBox isn't enough because these paints run from the panel
	// root, not from those children. - TripleA
	if (bShowScopeOrCamera)
	{
		// Skip the scope paint entirely when the panel is in camera-feed mode -
		// otherwise the vectors would render on top of the camera Image widget
		// (NativePaint runs after child widgets so its LayerId is higher) and
		// the user would see a scope overlay on the camera feed. The Image
		// widget itself is shown/hidden by UMG; this just stops C++ from
		// drawing into the same area. - TripleA
		if (!bShowCameraView)
		{
			// Build a paint context and surface it to BP so the scope can paint into
			// it. const_cast is the standard pattern - BlueprintImplementableEvent
			// dispatch is non-const but the paint elements list it writes to is
			// already the const& we got passed. - TripleA
			FPaintContext Context(AllottedGeometry, MyCullingRect, OutDrawElements,
				LayerId, InWidgetStyle, bParentEnabled);
			const_cast<UClearanceInstructorPanel*>(this)->BP_PaintScope(Context, AllottedGeometry.GetLocalSize());
		}
		else
		{
			// Camera-view paint pass for HUD overlays (runway centerlines,
			// approach corridors). Use Result + 1 so we sit ABOVE every child
			// widget that just painted - using the original LayerId puts us
			// underneath Img_CameraFeed (the image draws at a higher layer than
			// the panel root) and the lines get hidden behind the feed. - TripleA
			FPaintContext Context(AllottedGeometry, MyCullingRect, OutDrawElements,
				Result + 1, InWidgetStyle, bParentEnabled);
			const_cast<UClearanceInstructorPanel*>(this)->BP_PaintCameraOverlay(Context, AllottedGeometry.GetLocalSize());
		}
	}

	// Replay scrub-bar seam ticks. One vertical line over the slider track
	// for each "Go Live" boundary, anchored on the slider's own paint geometry
	// so it lines up with the track regardless of where the slider sits in
	// the widget tree. No-op when the slider isn't bound or we're not in
	// replay. - TripleA
	if (CachedController && CachedController->IsInReplay() && Slider_Scrub)
	{
		const float Duration = CachedController->GetReplayDuration();
		const TArray<float>& Seams = CachedController->GetReplaySegmentSeams();
		if (Duration > KINDA_SMALL_NUMBER && Seams.Num() > 0)
		{
			const FGeometry& SliderGeo = Slider_Scrub->GetPaintSpaceGeometry();
			const FVector2D SliderSize = SliderGeo.GetLocalSize();
			if (SliderSize.X > 0.f && SliderSize.Y > 0.f)
			{
				const FSlateRect UnboundedCull(-1e7f, -1e7f, 1e7f, 1e7f);
				FPaintContext SeamContext(SliderGeo, UnboundedCull, OutDrawElements,
					Result + 2, InWidgetStyle, bParentEnabled);
				const FLinearColor SeamColor(1.f, 0.78f, 0.2f, 0.85f); // amber
				for (float Seam : Seams)
				{
					const float X = (Seam / Duration) * SliderSize.X;
					UWidgetBlueprintLibrary::DrawLine(SeamContext,
						FVector2D(X, 0.f), FVector2D(X, SliderSize.Y),
						SeamColor, true, 2.f);
				}
			}
		}
	}

	return Result;
}

void UClearanceInstructorPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Federation activity rate sampler - runs on the raw frame tick (not the
	// throttled 5Hz refresh below) so the /s value in the panel is meaningful
	// even while the rest of the UI is coasting. Snapshots cumulative counts
	// once per second and stores the delta. - TripleA
	RateSampleAccumSec += InDeltaTime;
	if (RateSampleAccumSec >= 1.f)
	{
		const int32 CurDISEmit = GetDISPacketsSent();
		const int32 CurDISRecv = GetDISPacketsReceived();
		const int32 CurDDSEmit = GetDDSPacketsSent();
		const int32 CurDDSRecv = GetDDSPacketsReceived();
		const int32 CurRTIEmit = GetRTIPacketsSent();
		const int32 CurHLAUpd  = GetHLAUpdatesSent();

		auto ComputeRate = [Elapsed = RateSampleAccumSec](int32 Cur, int32 Last)
		{
			return FMath::Max(0, FMath::RoundToInt((Cur - Last) / Elapsed));
		};

		DISEmitRatePerSec   = ComputeRate(CurDISEmit, LastDISEmitSample);
		DISRecvRatePerSec   = ComputeRate(CurDISRecv, LastDISRecvSample);
		DDSEmitRatePerSec   = ComputeRate(CurDDSEmit, LastDDSEmitSample);
		DDSRecvRatePerSec   = ComputeRate(CurDDSRecv, LastDDSRecvSample);
		RTIEmitRatePerSec   = ComputeRate(CurRTIEmit, LastRTIEmitSample);
		HLAUpdateRatePerSec = ComputeRate(CurHLAUpd,  LastHLAUpdateSample);

		LastDISEmitSample   = CurDISEmit;
		LastDISRecvSample   = CurDISRecv;
		LastDDSEmitSample   = CurDDSEmit;
		LastDDSRecvSample   = CurDDSRecv;
		LastRTIEmitSample   = CurRTIEmit;
		LastHLAUpdateSample = CurHLAUpd;

		RateSampleAccumSec = 0.f;
	}

	TimeSinceRefreshSec += InDeltaTime;
	if (TimeSinceRefreshSec < RefreshIntervalSec) { return; }
	TimeSinceRefreshSec = 0.f;

	// Re-resolve refs - the controller can replicate in after we constructed,
	// and the PC class can swap during seamless travel. Cheap to look up. - TripleA
	if (!CachedController || !CachedOperatorPC) { RefreshLocalRefs(); }
	if (!CachedController) { return; }

	{
		TArray<FInstructorAircraftRow> Rows;
		BuildAircraftRows(Rows);

		// Auto-fit the scope range to whatever's farthest from the sector centre
		// across aircraft, zones, and runways - so scenarios with fixed-position
		// overlays at large radii (e.g. a protected zone far out) still render
		// inside the outer ring. Floored to keep the scope from collapsing on
		// an empty sector. - TripleA
		if (bAutoFitScopeRange)
		{
			float MaxDistNm = MinAutoFitRangeNm;
			for (const FInstructorAircraftRow& R : Rows)
			{
				MaxDistNm = FMath::Max(MaxDistNm, R.PositionNm.Size());
			}
			for (const FInstructorZoneMarker& Z : GetZoneMarkers())
			{
				// Include the zone's radius so the whole circle fits inside the
				// outer ring, not just its centre. - TripleA
				MaxDistNm = FMath::Max(MaxDistNm, Z.PositionNm.Size() + Z.RadiusNm);
			}
			for (const FRunwayInfo& Rwy : GetRunwayMarkers())
			{
				MaxDistNm = FMath::Max(MaxDistNm, Rwy.ThresholdNm.Size());
			}
			// The sector boundary is the hard ceiling - nothing outside it is real
			// airspace, so the scope shouldn't zoom out past it. Anything reporting
			// a position beyond ExitRadius is either stale or a unit error, and
			// without this clamp it drags the range markers off into nonsense
			// values (e.g. a runway threshold accidentally in km pulls the scope
			// 4x larger than the sector). - TripleA
			if (CachedController->ExitRadiusNm > 0.f)
			{
				MaxDistNm = FMath::Min(MaxDistNm, CachedController->ExitRadiusNm);
			}
			ScopeRangeNm = MaxDistNm * FMath::Max(1.f, AutoFitMarginFactor);
		}

		if (AircraftListChanged(Rows, LastAircraft))
		{
			LastAircraft = Rows;
			OnAircraftListChanged(Rows);
		}
	}
	{
		FInstructorScoreView View;
		BuildScoreView(View);
		if (ScoreChanged(View, LastScore))
		{
			LastScore = View;
			OnScoreChanged(View);
		}
	}
	{
		FInstructorScenarioView View;
		BuildScenarioView(View);
		if (ScenarioChanged(View, LastScenario))
		{
			LastScenario = View;
			OnScenarioStateChanged(View);
		}
	}
	{
		TArray<FClearanceNotification> Notes;
		BuildNotifications(Notes);
		if (NotificationsChanged(Notes, LastNotifications))
		{
			LastNotifications = Notes;
			OnNotificationsChanged(Notes);
		}
	}
}

void UClearanceInstructorPanel::RefreshLocalRefs()
{
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AClearanceSimulationController> It(W); It; ++It)
		{
			CachedController = *It;
			break;
		}
		if (APlayerController* PC = GetOwningPlayer())
		{
			CachedOperatorPC = Cast<AClearanceOperatorPC>(PC);
		}
	}
}

// --- View builders --------------------------------------------------------

bool UClearanceInstructorPanel::BuildAircraftRows(TArray<FInstructorAircraftRow>& Out) const
{
	Out.Reset();
	if (!CachedController || !CachedController->GetAirspaceManager()) { return false; }
	const TArray<FAircraftState> States = CachedController->GetAirspaceManager()->GetAllAircraftStates();
	Out.Reserve(States.Num());
	for (const FAircraftState& S : States)
	{
		FInstructorAircraftRow R;
		R.Callsign                = S.Callsign;
		// Instructor scope reads truth - the god view should see a disguised
		// hostile as Hostile, not as Unknown. Hijacked civilians get promoted
		// to Hostile in BOTH views because the airframe is now compromised
		// (7500 squawk) - it reads as a threat even though it's fundamentally
		// a civilian aircraft. Underlying TrueAffiliation stays Neutral; this
		// is purely the view interpretation. - TripleA
		const bool bHijackActive = (S.ActiveEmergency == EEmergencyType::Hijack);
		R.ThreatClass             = bHijackActive ? EThreatClass::Hostile : S.TrueAffiliation;
		R.OperatorClassification  = bHijackActive ? EThreatClass::Hostile : S.ThreatClass;
		R.FlightPhase             = S.FlightPhase;
		R.ActiveEmergency    = S.ActiveEmergency;
		R.CurrentAlertLevel  = S.CurrentAlertLevel;
		// FuelRemainingMinutes on FAircraftState is a misnomer - it's the
		// shared countdown for both FuelLow AND GeneralMayday. Expose it
		// only when one of those two types is active. - TripleA
		R.EmergencyTimerMinutes = (S.ActiveEmergency == EEmergencyType::FuelLow
		                        || S.ActiveEmergency == EEmergencyType::GeneralMayday)
			? S.FuelRemainingMinutes : -1.f;
		R.Heading            = S.Heading;
		R.TargetHeading      = S.TargetHeading;
		R.Altitude           = S.Altitude;
		R.TargetAltitude     = S.TargetAltitude;
		R.Speed              = S.Speed;
		R.TargetSpeed        = S.TargetSpeed;
		R.SquawkCode         = S.SquawkCode;
		R.bJammingOn         = S.bJammingOn;
		R.bUnderGCIControl   = S.bUnderGCIControl;
		R.bIsMilitary        = S.bIsMilitary;
		R.bIFFOperational    = S.bIFFOperational;
		R.bIsExternal        = S.bIsExternal;
		R.OwnerSiteId        = S.OwnerSiteId;
		R.PositionNm         = FVector2D(S.Position.X, S.Position.Y);
		Out.Add(R);
	}
	return true;
}

void UClearanceInstructorPanel::BuildScoreView(FInstructorScoreView& Out) const
{
	if (!CachedController) { return; }
	Out.Total              = CachedController->RepScoreTotal;
	Out.EfficiencyPct      = CachedController->RepScoreEfficiencyPct;
	Out.Landings           = CachedController->RepScoreLandings;
	Out.Handoffs           = CachedController->RepScoreHandoffs;
	Out.ResolvedConflicts  = CachedController->RepScoreResolved;
	Out.Intercepts         = CachedController->RepScoreIntercepts;
	Out.Emergencies        = CachedController->RepScoreEmergencies;
	Out.GoArounds          = CachedController->RepScoreGoArounds;
	Out.SepLoss            = CachedController->RepScoreSepLoss;
	Out.WakeBusts          = CachedController->RepScoreWake;
	Out.TCAS               = CachedController->RepScoreTCAS;
	Out.Strayed            = CachedController->RepScoreStrayed;
	Out.MisID              = CachedController->RepScoreMisID;
	Out.Violated           = CachedController->RepScoreViolated;
	Out.Crashed            = CachedController->RepScoreCrashed;
	Out.Busted             = CachedController->RepScoreBusted;
	Out.NextSpawnSec       = CachedController->RepScoreNextSpawnSec;
}

void UClearanceInstructorPanel::BuildScenarioView(FInstructorScenarioView& Out) const
{
	if (!CachedController) { return; }
	Out.bRunning       = CachedController->bRepScenarioRunning;
	Out.Name           = CachedController->RepScenarioName;
	Out.ElapsedSec     = CachedController->RepScenarioElapsedSec;
	Out.FiredEvents    = CachedController->RepScenarioFiredEvents;
	Out.TotalEvents    = CachedController->RepScenarioTotalEvents;
	Out.FiredTriggers  = CachedController->RepScenarioFiredTriggers;
	Out.TotalTriggers  = CachedController->RepScenarioTotalTriggers;
}

void UClearanceInstructorPanel::BuildNotifications(TArray<FClearanceNotification>& Out) const
{
	Out.Reset();
	if (!CachedController) { return; }
	const TArray<FClearanceNotification>& All = CachedController->RepNotifications;
	const int32 Take = FMath::Min(All.Num(), MaxNotifications);
	Out.Reserve(Take);
	for (int32 i = All.Num() - Take; i < All.Num(); ++i)
	{
		Out.Add(All[i]);
	}
}

// --- Public read views (pull-style) ---------------------------------------

TArray<FInstructorAircraftRow> UClearanceInstructorPanel::GetAircraftRows() const
{
	TArray<FInstructorAircraftRow> Out;
	BuildAircraftRows(Out);
	return Out;
}

// --- Picture-in-picture camera feed ---------------------------------------
// Thin wrappers around the controller's PIP API. The toggle also flips the
// SceneCapture on/off so we're not paying for a second render pass while the
// instructor's looking at the scope. - TripleA

void UClearanceInstructorPanel::ToggleScopeCameraView()
{
	bShowCameraView = !bShowCameraView;
	if (!CachedController) { RefreshLocalRefs(); }
	if (CachedController)
	{
		CachedController->SetInstructorPipEnabled(bShowCameraView);
		if (bShowCameraView)
		{
			// Default to Overview each time the instructor opens the camera
			// feed so they always land on a sensible sector-wide shot rather
			// than wherever the last mode left things. - TripleA
			CachedController->SetInstructorPipView(EClearanceCameraView::Overview);
		}
	}
}

UTextureRenderTarget2D* UClearanceInstructorPanel::GetInstructorPipRT() const
{
	return CachedController ? CachedController->GetInstructorPipRT() : nullptr;
}

void UClearanceInstructorPanel::SetInstructorPipView(EClearanceCameraView View)
{
	if (!CachedController) { return; }
	CachedController->SetInstructorPipView(View);
}

void UClearanceInstructorPanel::CycleInstructorPipView()
{
	if (!CachedController) { return; }
	CachedController->CycleInstructorPipView();
}

EClearanceCameraView UClearanceInstructorPanel::GetInstructorPipView() const
{
	return CachedController ? CachedController->GetInstructorPipView() : EClearanceCameraView::Tower;
}

void UClearanceInstructorPanel::ApplyTowerYawDelta(float DeltaDeg)
{
	if (!CachedController) { RefreshLocalRefs(); }
	if (CachedController)
	{
		CachedController->ApplyTowerYawDelta(DeltaDeg);
	}
}

TArray<FString> UClearanceInstructorPanel::GetApproachRunwayLabels() const
{
	return CachedController ? CachedController->GetApproachRunwayLabels() : TArray<FString>();
}

void UClearanceInstructorPanel::SetInstructorPipApproachRunway(int32 Index)
{
	if (!CachedController) { RefreshLocalRefs(); }
	UE_LOG(LogTemp, Warning, TEXT("[PIP] Panel SetApproachRunway: idx=%d controller=%p"),
		Index, CachedController.Get());
	if (!CachedController) { return; }
	CachedController->SetInstructorPipApproachRunway(Index);
}

int32 UClearanceInstructorPanel::GetInstructorPipApproachRunwayIndex() const
{
	return CachedController ? CachedController->GetInstructorPipApproachRunwayIndex() : 0;
}

void UClearanceInstructorPanel::PickApproachRunwayByLabel(const FString& Label)
{
	if (!CachedController) { RefreshLocalRefs(); }
	if (!CachedController) { return; }
	CachedController->PickApproachRunwayByLabel(Label);
}

void UClearanceInstructorPanel::CycleChaseAngleNext()
{
	if (!CachedController) { RefreshLocalRefs(); }
	if (CachedController) { CachedController->CycleInstructorPipFollowAngleNext(); }
}

void UClearanceInstructorPanel::CycleChaseAnglePrev()
{
	if (!CachedController) { RefreshLocalRefs(); }
	if (CachedController) { CachedController->CycleInstructorPipFollowAnglePrev(); }
}

EClearanceFollowAngle UClearanceInstructorPanel::GetChaseAngle() const
{
	return CachedController ? CachedController->GetInstructorPipFollowAngle() : EClearanceFollowAngle::Chase;
}

TArray<FInstructorCameraLabel> UClearanceInstructorPanel::GetCameraLabels() const
{
	return CachedController ? CachedController->GetCameraLabels() : TArray<FInstructorCameraLabel>();
}

TArray<FInstructorCameraLine> UClearanceInstructorPanel::GetCameraOverlayLines() const
{
	return CachedController ? CachedController->GetCameraOverlayLines() : TArray<FInstructorCameraLine>();
}

TArray<FInstructorCameraText> UClearanceInstructorPanel::GetCameraOverlayText() const
{
	return CachedController ? CachedController->GetCameraOverlayText() : TArray<FInstructorCameraText>();
}

void UClearanceInstructorPanel::SetSelectedCallsign(FName NewSelection)
{
	const bool bChanged = (NewSelection != SelectedCallsign);
	SelectedCallsign = NewSelection;
	// Selection IS the chase target. Whenever the instructor picks a different
	// aircraft, the Chase view follows along automatically - no extra clicks
	// required. Empty selection means no chase target, the Follow case in
	// UpdateInstructorPip silently no-ops which freezes the camera on the last
	// good frame. - TripleA
	if (!CachedController) { RefreshLocalRefs(); }
	if (CachedController)
	{
		CachedController->SetInstructorPipFollowCallsign(NewSelection);
		// Reset the chase sub-angle to the default behind-and-above each time
		// the selection moves to a new aircraft. Cycling angles is meant for
		// the contact you're currently watching - keeping a previous "Top"
		// view on a freshly selected aircraft is just stale state. - TripleA
		if (bChanged)
		{
			CachedController->SetInstructorPipFollowAngle(EClearanceFollowAngle::Chase);
		}
	}
}

void UClearanceInstructorPanel::DrawCameraOverlayLines(FPaintContext& Context, UImage* CameraImage)
{
	if (!CameraImage) { return; }

	// Paint-space geometry is the image's geometry for THIS paint pass.
	// GetCachedGeometry() returns whatever was last painted - in dual-
	// viewport / multi-pass setups (which is what this widget tree turned
	// out to be), the cached value can be from a sibling render pass that
	// has nothing to do with the visible one. - TripleA
	const FGeometry& ImageGeo = CameraImage->GetPaintSpaceGeometry();
	const FVector2D ImageSize = ImageGeo.GetLocalSize();
	if (ImageSize.X <= 0.f || ImageSize.Y <= 0.f) { return; }

	const FVector2D PanelAbs = Context.AllottedGeometry.GetAbsolutePosition();
	const FVector2D ImageAbs = ImageGeo.GetAbsolutePosition();
	const float Scale = FMath::Max(KINDA_SMALL_NUMBER, Context.AllottedGeometry.Scale);
	const FVector2D ImageOriginInPanel = (ImageAbs - PanelAbs) / Scale;

	const TArray<FInstructorCameraLine> Lines = GetCameraOverlayLines();
	for (const FInstructorCameraLine& Line : Lines)
	{
		const FVector2D Start = ImageOriginInPanel + Line.StartUV * ImageSize;
		const FVector2D End   = ImageOriginInPanel + Line.EndUV   * ImageSize;
		UWidgetBlueprintLibrary::DrawLine(Context, Start, End, Line.Color, true, Line.Thickness);
	}
}

void UClearanceInstructorPanel::DrawCameraFeedBorder(FPaintContext& Context, UImage* CameraImage)
{
	if (!CameraImage) { return; }

	const FGeometry& ImageGeo = CameraImage->GetPaintSpaceGeometry();
	const FVector2D ImageSize = ImageGeo.GetLocalSize();
	if (ImageSize.X <= 0.f || ImageSize.Y <= 0.f) { return; }

	const FVector2D PanelAbs = Context.AllottedGeometry.GetAbsolutePosition();
	const FVector2D ImageAbs = ImageGeo.GetAbsolutePosition();
	const float Scale = FMath::Max(KINDA_SMALL_NUMBER, Context.AllottedGeometry.Scale);
	const FVector2D Origin = (ImageAbs - PanelAbs) / Scale;
	const FVector2D Far(Origin.X + ImageSize.X, Origin.Y + ImageSize.Y);

	const FLinearColor Cyan(0.20f, 0.85f, 1.00f, 0.6f);
	constexpr float Thick = 1.f;
	UWidgetBlueprintLibrary::DrawLine(Context, FVector2D(Origin.X, Origin.Y), FVector2D(Far.X,    Origin.Y), Cyan, true, Thick);
	UWidgetBlueprintLibrary::DrawLine(Context, FVector2D(Far.X,    Origin.Y), FVector2D(Far.X,    Far.Y),    Cyan, true, Thick);
	UWidgetBlueprintLibrary::DrawLine(Context, FVector2D(Far.X,    Far.Y),    FVector2D(Origin.X, Far.Y),    Cyan, true, Thick);
	UWidgetBlueprintLibrary::DrawLine(Context, FVector2D(Origin.X, Far.Y),    FVector2D(Origin.X, Origin.Y), Cyan, true, Thick);
}

void UClearanceInstructorPanel::DrawCameraOverlayText(FPaintContext& Context, UImage* CameraImage)
{
	if (!CameraImage) { return; }

	const FGeometry& ImageGeo = CameraImage->GetPaintSpaceGeometry();
	const FVector2D ImageSize = ImageGeo.GetLocalSize();
	if (ImageSize.X <= 0.f || ImageSize.Y <= 0.f) { return; }

	const FVector2D PanelAbs = Context.AllottedGeometry.GetAbsolutePosition();
	const FVector2D ImageAbs = ImageGeo.GetAbsolutePosition();
	const float Scale = FMath::Max(KINDA_SMALL_NUMBER, Context.AllottedGeometry.Scale);
	const FVector2D ImageOriginInPanel = (ImageAbs - PanelAbs) / Scale;

	const TArray<FInstructorCameraText> Items = GetCameraOverlayText();
	const FLinearColor ShadowColor(0.f, 0.f, 0.f, 0.85f);
	for (const FInstructorCameraText& Item : Items)
	{
		// Rough character-width offset so the text reads centred on the
		// projected threshold instead of left-justified to it. ~8 px per
		// char of the default text style is a usable estimate. - TripleA
		const FVector2D Centre = ImageOriginInPanel + Item.ScreenUV * ImageSize;
		const FVector2D Offset(Item.Text.Len() * 7.f, Item.FontSize * 0.5f);
		const FVector2D Pos = Centre - Offset;
		// Cheap drop-shadow: same string painted 1 px down-right in black
		// first. Keeps the cyan legible whether it lands over sky, runway
		// or terrain. - TripleA
		UWidgetBlueprintLibrary::DrawText(Context, Item.Text, Pos + FVector2D(2.f, 2.f), ShadowColor);
		UWidgetBlueprintLibrary::DrawText(Context, Item.Text, Pos, Item.Color);
	}
}

void UClearanceInstructorPanel::RebindCameraFeedBrush(UImage* TargetImage)
{
	if (!TargetImage) { return; }
	if (!CachedController) { RefreshLocalRefs(); }
	if (!CachedController) { return; }
	UTextureRenderTarget2D* RT = GetInstructorPipRT();
	if (!RT) { return; }

	FSlateBrush Brush = TargetImage->GetBrush();
	Brush.SetResourceObject(RT);
	Brush.SetImageSize(FVector2D(RT->SizeX, RT->SizeY));
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	TargetImage->SetBrush(Brush);
}

FInstructorScoreView UClearanceInstructorPanel::GetScoreView() const
{
	FInstructorScoreView Out;
	BuildScoreView(Out);
	return Out;
}

FInstructorScenarioView UClearanceInstructorPanel::GetScenarioView() const
{
	FInstructorScenarioView Out;
	BuildScenarioView(Out);
	return Out;
}

TArray<FClearanceNotification> UClearanceInstructorPanel::GetRecentNotifications() const
{
	TArray<FClearanceNotification> Out;
	BuildNotifications(Out);
	return Out;
}

bool UClearanceInstructorPanel::IsDISEmitting() const
{
	return CachedController && CachedController->bRepDISEmitting;
}

bool UClearanceInstructorPanel::IsDISReceiving() const
{
	return CachedController && CachedController->bRepDISReceiving;
}

// Federation counter reads go via the controller's replicated Rep* fields
// rather than dereferencing the (server-only, unreplicated) emitter/receiver
// subobject pointers. Client-side proxies never see those subobjects, so
// touching them directly would silently return 0. - TripleA
int32 UClearanceInstructorPanel::GetDISPacketsSent() const
{
	return CachedController ? CachedController->RepDISPacketsSent : 0;
}

int32 UClearanceInstructorPanel::GetDISPacketsReceived() const
{
	return CachedController ? CachedController->RepDISPacketsReceived : 0;
}

bool UClearanceInstructorPanel::IsDDSEmitting() const
{
	return CachedController && CachedController->bRepDDSEmitting;
}

bool UClearanceInstructorPanel::IsDDSReceiving() const
{
	return CachedController && CachedController->bRepDDSReceiving;
}

int32 UClearanceInstructorPanel::GetDDSPacketsSent() const
{
	return CachedController ? CachedController->RepDDSPacketsSent : 0;
}

int32 UClearanceInstructorPanel::GetDDSPacketsReceived() const
{
	return CachedController ? CachedController->RepDDSPacketsReceived : 0;
}

bool UClearanceInstructorPanel::IsRTIEmitting() const
{
	return CachedController && CachedController->bRepRTIEmitting;
}

int32 UClearanceInstructorPanel::GetRTIPacketsSent() const
{
	return CachedController ? CachedController->RepRTIPacketsSent : 0;
}

bool UClearanceInstructorPanel::IsHLAJoined() const
{
	return CachedController && CachedController->bRepHLAJoined;
}

int32 UClearanceInstructorPanel::GetHLAUpdatesSent() const
{
	return CachedController ? CachedController->RepHLAUpdatesSent : 0;
}

namespace
{
	// Convert the manual's simple markdown-ish markers to RichTextBlock tags:
	//   **bold**    -> <Bold>bold</>
	//   `code`      -> <Code>code</>
	//   [UPPERCASE] -> <Accent>UPPERCASE</>  (only when the whole bracketed
	//                  token is A-Z; lower-case [x] is left alone so it can
	//                  be used as a literal placeholder)
	// The style keys must exist in the RichTextBlock's DataTable
	// (Default / Bold / Code / Accent) or they render as plain text. - TripleA
	FString TransformManualMarkup(FString In)
	{
		// **bold** - non-greedy pair match. Simple state machine finds the
		// left ** and the next ** on the same string.
		while (true)
		{
			const int32 L = In.Find(TEXT("**"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 0);
			if (L == INDEX_NONE) { break; }
			const int32 R = In.Find(TEXT("**"), ESearchCase::CaseSensitive, ESearchDir::FromStart, L + 2);
			if (R == INDEX_NONE) { break; }
			const FString Inner = In.Mid(L + 2, R - L - 2);
			In = In.Left(L) + TEXT("<Bold>") + Inner + TEXT("</>") + In.Mid(R + 2);
		}

		// `code` - single backticks
		while (true)
		{
			const int32 L = In.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 0);
			if (L == INDEX_NONE) { break; }
			const int32 R = In.Find(TEXT("`"), ESearchCase::CaseSensitive, ESearchDir::FromStart, L + 1);
			if (R == INDEX_NONE) { break; }
			const FString Inner = In.Mid(L + 1, R - L - 1);
			In = In.Left(L) + TEXT("<Code>") + Inner + TEXT("</>") + In.Mid(R + 1);
		}

		// Semantic color markers - matched with paired 2-char sentinels chosen
		// to be unambiguous vs prose. Order: green, yellow, red.
		//   @@text@@ -> <Green>text</>   (positive / safe / confirmation)
		//   !!text!! -> <Yellow>text</>  (caution / warning)
		//   ~~text~~ -> <Red>text</>     (destructive / critical)
		// - TripleA
		auto ReplaceInlineTag = [](FString& S, const TCHAR* Marker, const TCHAR* OpenTag)
		{
			const int32 Ml = FCString::Strlen(Marker);
			while (true)
			{
				const int32 L = S.Find(Marker, ESearchCase::CaseSensitive, ESearchDir::FromStart, 0);
				if (L == INDEX_NONE) { break; }
				const int32 R = S.Find(Marker, ESearchCase::CaseSensitive, ESearchDir::FromStart, L + Ml);
				if (R == INDEX_NONE) { break; }
				const FString Inner = S.Mid(L + Ml, R - L - Ml);
				S = S.Left(L) + OpenTag + Inner + TEXT("</>") + S.Mid(R + Ml);
			}
		};
		ReplaceInlineTag(In, TEXT("@@"), TEXT("<Green>"));
		ReplaceInlineTag(In, TEXT("!!"), TEXT("<Yellow>"));
		ReplaceInlineTag(In, TEXT("~~"), TEXT("<Red>"));

		// [UPPERCASE] - fully-uppercase bracketed tokens tinted as accent
		// (button names like [SCRAMBLE], [RESET SCENARIO]). Space + digits
		// permitted; lower-case letters disqualify the match so BP-side
		// literals stay literal.
		FString Out;
		Out.Reserve(In.Len() + 32);
		int32 i = 0;
		const int32 N = In.Len();
		while (i < N)
		{
			const TCHAR c = In[i];
			if (c == TEXT('['))
			{
				int32 j = i + 1;
				bool bAllUpper = j < N;
				while (j < N && In[j] != TEXT(']'))
				{
					const TCHAR d = In[j];
					const bool bOk = (d >= TEXT('A') && d <= TEXT('Z'))
					              || (d >= TEXT('0') && d <= TEXT('9'))
					              || d == TEXT(' ')
					              || d == TEXT('+')
					              || d == TEXT('/')
					              || d == TEXT('-');
					if (!bOk) { bAllUpper = false; break; }
					++j;
				}
				if (bAllUpper && j < N && In[j] == TEXT(']') && (j - i) > 1)
				{
					const FString Inner = In.Mid(i + 1, j - i - 1);
					Out += TEXT("<Accent>");
					Out += Inner;
					Out += TEXT("</>");
					i = j + 1;
					continue;
				}
			}
			Out.AppendChar(c);
			++i;
		}
		return Out;
	}
}

TArray<FManualSection> UClearanceInstructorPanel::GetManualSections() const
{
	auto Make = [](const TCHAR* Anchor, const TCHAR* Title, const TCHAR* Body)
	{
		FManualSection S;
		S.Anchor = Anchor;
		S.Title  = Title;
		S.Body   = TransformManualMarkup(FString(Body));
		return S;
	};

	TArray<FManualSection> Sections;

	Sections.Add(Make(TEXT("overview"), TEXT("Overview"),
		TEXT("The instructor station is a single-window control surface with three primary tabs plus this **MANUAL** reference.\n\n"
		"[SCOPE] shows god-view radar with every aircraft visible regardless of what the trainee sees - the instructor's picture of ground truth. Also shows the trainee scope [OPERATOR]\n\n"
		"[CAMERAS] gives live 3D observation of aircraft or airport across five modes: Overview / Tower / Chase / Approach / Operator.\n\n"
		"[AAR] is the real-time scoring dashboard, incident log, comms transcript, and After Action Report export.\n\n"
		"The right-hand column persists across all tabs. It holds the inject controls: **EMERGENCY** / **THREAT CLASS** / **EW** (jamming / chaff / scramble) / **SECTOR CONTROLS** (scenarios / traffic) / **WIND** and the destructive-action buttons ~~SCRAMBLE~~, ~~CLEAR ALL~~, ~~RESET SCENARIO~~.\n\n"
		"The top row also persists: session controls (!!PAUSE!! / @@SAVE@@ / [LOAD] / ~~DEL~~ checkpoint / speed) and bottom row replay scrub for reviewing past events without disrupting the live session.")));

	Sections.Add(Make(TEXT("session"), TEXT("Session Lifecycle"),
		TEXT("**Starting a fresh session**\n\n"
		"1. Verify the trainee's operator station is connected. Aircraft list on the top-left populates once the trainee spawns into the tower.\n"
		"2. Confirm the scenario dropdown shows the intended exercise (default `baltic_intercept`).\n"
		"3. Set initial wind if the scenario requires non-default conditions. [APPLY WIND] commits.\n"
		"4. [LOAD] the scenario to start from t=0. Otherwise leave the session in freeplay and inject manually.\n\n"
		"**Pause and speed**\n\n"
		"Pause via the !!PAUSE!! button in the SESSION row (top, amber-striped). Speed controls (`0.25x` through `4x`) time-dilate the live simulation - compress quiet stretches, slow down dense conflicts for teaching moments. Pause and speed apply to the LIVE simulation. Replay playback uses its own controls and does not affect the live session.\n\n"
		"**Ending a session**\n\n"
		"1. !!STOP!! the scenario if one is running.\n"
		"2. Optional ~~CLEAR ALL~~ removes all live traffic without ending the session - useful between back-to-back exercises.\n"
		"3. Switch to AAR tab, click @@EXPORT AAR@@. Report is written to !!<ProjectSavedDir>/Reports/Session_YYYYMMDD_HHMMSS.md!!. The absolute path is announced in the event log.\n"
		"4. ~~RESET SCENARIO~~ (sector controls, red) wipes the entire session state - scores, transcript, checkpoints, live traffic. **Irreversible**. Use only between complete training days.")));

	Sections.Add(Make(TEXT("truth-scope"), TEXT("Truth Scope"),
		TEXT("The truth scope shows the airspace as it really is - every aircraft, their true classification (friendly / hostile / neutral / unknown), their actual position and vector, without any operator-side degradation.\n\n"
		"**Reading the scope**\n\n"
		"- North reference: 000 at top, 090 right, 180 bottom, 270 left. 36 minor tick marks around the perimeter (every 10 deg), 12 major labels every 30 deg.\n"
		"- Range rings at approximately 33% / 66% / 100% of scope range, labelled in nautical miles.\n"
		"- Sector features: airway centerlines (thin green), restricted zones (dashed red rings), protected zones (dashed red), runways (thick strips + extended centerline + designator), waypoints (green squares + 5-letter name).\n\n"
		"**Aircraft symbology (MIL-STD-2525 affiliation)**\n\n"
		"- Rectangle - Friendly (cyan)\n"
		"- Diamond - Hostile (red)\n"
		"- Hexagon - Unknown (amber)\n"
		"- Square - Neutral (green)\n\n"
		"Each symbol has a data block (callsign + FL + heading), a leader line to the block, and a bearing vector along the current heading. Clustered aircraft auto-fan into a small circle - the \"Datablock Stagger\" convention from real STARS displays.\n\n"
		"**Instructor overlays** (buttons under the scope)\n\n"
		"- [FULL DATA BLOCKS] expands compact 2-line blocks into full 4-line ATC-style blocks.\n"
		"- [TRUTH] / [OPERATOR] toggle switches between ground-truth view and the fused radar picture the trainee sees. In OPERATOR mode, EW-jammed tracks fade proportional to confidence, chaff ghost contacts appear, radar gaps show as missing tracks.\n"
		"- [COVERAGE] overlays radial-gradient discs per radar site showing coverage areas. Overlapping discs indicate multi-sensor fusion. Absence of green = coverage gap.")));

	Sections.Add(Make(TEXT("camera"), TEXT("Camera View"),
		TEXT("Switches from radar to 3D observation. Five sub-modes plus persistent HUD overlays.\n\n"
		"**OVERVIEW** - fixed elevated view of the entire sector. Wide-area situational awareness.\n\n"
		"**TOWER** - first-person from the airport control tower. Pan left/right with the chevron overlays on the feed edges. Simulates the visual picture a tower controller has.\n\n"
		"**CHASE** - trailing camera behind the currently-selected aircraft. Chevrons cycle different camera angles on the same aircraft.\n\n"
		"**APPROACH** - camera at an approach corridor end, looking along the approach path back toward the runway. The runway picker row appears below the feed - [RWY 36R] / [RWY 36L] / [RWY 18L] / [RWY 18R]. The currently-selected approach lights up cyan.\n\n"
		"**OPERATOR** - POV from the selected aircraft, as if looking through the pilot's or radar-operator's eyes.\n\n"
		"**Persistent HUD overlays** across all modes:\n\n"
		"- Runway centerline projections\n"
		"- Approach corridor lines\n"
		"- Sector boundary\n"
		"- Zone boundaries\n"
		"- Aircraft callsign + altitude tags on visible traffic\n"
		"- 1px cyan frame around the feed itself")));

	Sections.Add(Make(TEXT("injects"), TEXT("Injects"),
		TEXT("Right-hand column. All injects require an aircraft target selected (except APPLY WIND, LOAD scenario, and CLEAR ALL which are global).\n\n"
		"**EMERGENCY**\n\n"
		"Dropdown selects the type. [INJECT] applies.\n\n"
		"- **Mayday (7700)** - general distress. Countdown default 7 minutes.\n"
		"- **Comms Failure (7600)** - lost radio. No countdown; aircraft auto-flies lost-comms procedure.\n"
		"- **Hijack (7500)** - hostile takeover. No countdown; needs SCRAMBLE authorization.\n"
		"- **Fuel Emergency** - fuel-critical. Countdown default 5 minutes.\n\n"
		"Fuel and Mayday carry crash countdowns. If the aircraft is not brought to safe landing before expiry, it crashes. Timer countdowns display on the aircraft row as `FUEL M:SS` or `MAYDAY M:SS` with color thresholds - red under 1 min, amber under 3 min.\n\n"
		"The **timer input** next to the dropdown lets you override the default duration (0.5 to 30 min). Short timers for high-tempo panic training, long timers for gentle-training observation exercises. !!CLEAR EMER!! resolves the current emergency - squawk returns to 1200, timer stops.\n\n"
		"**THREAT CLASS**\n\n"
		"Instructor-side reclassification. [RECLASSIFY] commits. **Only affects the TRUE affiliation** (truth scope) - does NOT change the operator's view. The operator must independently classify via radar picture, IFF interrogation, and voice challenges. Instructor can flip a hidden hostile mid-session to test operator identification.\n\n"
		"**EW - Electronic Warfare**\n\n"
		"- !!JAM ON!! - selected aircraft begins EW jamming. Degrades radar confidence on all tracks in its jamming radius. Operator scope tracks fade.\n"
		"- @@JAM OFF@@ - cancels jamming.\n"
		"- !!DROP CHAFF!! - releases chaff cloud. Radar paints ghost contacts around the aircraft's true position.\n\n"
		"~~SCRAMBLE~~ (destructive)\n\n"
		"Launches interceptors against the selected aircraft. Announcement: `SCRAMBLE: N interceptors on <CS>` in the event log (amber).\n\n"
		"**SECTOR CONTROLS**\n\n"
		"- Scenario dropdown + [LOAD] - starts the selected scenario.\n"
		"- !!STOP!! - halts the running scenario. Aircraft remain.\n"
		"- ~~CLEAR ALL~~ - removes ALL aircraft immediately.\n"
		"- [SPAWN +1] - adds one random aircraft.\n"
		"- **MAX AIRCRAFT** slider - tune the traffic-density cap live (range 8-40). Existing aircraft aren't yanked, only new spawns are gated.\n\n"
		"**WIND**\n\n"
		"DIR slider (0-360 deg) + SPD slider (0-200 kt). [APPLY WIND] commits. Wind affects: aircraft ground track (crab on approach), active runway selection (crosswind component), fuel burn calculations. Force runway changes, degrade approach performance, introduce wind-shear-like conditions.")));

	Sections.Add(Make(TEXT("scenarios"), TEXT("Scenarios"),
		TEXT("Scenarios are JSON files defining timed spawn events, injects, and scoring rules. Located at `Plugins/ClearanceSim/Scenarios/`.\n\n"
		"**Loading**\n\n"
		"1. Select scenario name from the SECTOR CONTROLS dropdown.\n"
		"2. Click [LOAD]. Sim clears existing traffic, resets the scenario timer, begins ticking scenario events.\n"
		"3. Event log announces `SCENARIO LOADED: <name>` in cyan.\n\n"
		"**Stopping**\n\n"
		"!!STOP!! halts new events but aircraft remain. Event log announces `SCENARIO STOPPED`.\n\n"
		"**Baseline scenarios shipped**\n\n"
		"- `baltic_intercept` - hostile inbound from east, requires SCRAMBLE authorization.\n"
		"- `hijack_response` - commercial traffic squawks 7500 mid-flight.\n"
		"- `mass_divert` - multiple emergencies simultaneously; tests prioritization.\n"
		"- `mayday_engine_fire` - single high-priority emergency, tests procedural clearances.\n"
		"- `nordo_inbound` - comms failure aircraft approaches from outside the sector.\n"
		"- `cold_war_probe` - unidentified track probes the sector edge, tests interrogation.\n"
		"- `mixed_ops` - mixed civilian and military traffic, tests general competence.")));

	Sections.Add(Make(TEXT("checkpoints"), TEXT("Checkpoints"),
		TEXT("SESSION row (bottom). Multi-attempt rehearsal workflow.\n\n"
		"**Workflow**\n\n"
		"1. Advance session to the state you want to rehearse from.\n"
		"2. Type a name into the checkpoint text field.\n"
		"3. @@SAVE@@. Checkpoint snapshots all aircraft states, wind, session time, current score, full scoring log.\n"
		"4. Trainee attempts the scenario from this state.\n"
		"5. If the attempt fails or completes, [LOAD], select the checkpoint from the dropdown, hit LOAD.\n"
		"6. Aircraft return to their exact positions at save time. Score resets to save-time value. Wind restores.\n"
		"7. ~~DEL~~ permanently removes the selected checkpoint.\n\n"
		"**What's preserved across a LOAD**\n\n"
		"- Every aircraft position, altitude, heading, speed, phase, emergency, and IFF state.\n"
		"- Wind direction and speed.\n"
		"- Session time counter.\n"
		"- Total score and per-category incident log.\n"
		"- Difficulty tuning (spawn intervals).\n\n"
		"**What's NOT touched on LOAD**\n\n"
		"- **Transcript** - the full voice/text comms log from ALL attempts is preserved. Reviewing AAR after multiple attempts shows the trainee's full progression.\n"
		"- **Recorder** - the DVR-style session recorder continues capturing through checkpoint loads. Replay scrub can navigate across load boundaries.\n\n"
		"**Recommended usage**\n\n"
		"- Save `trainee_start` at the beginning of each training block.\n"
		"- Save `pre_intercept` immediately before the critical decision point in a scenario.\n"
		"- Save `post_conflict` after a successful resolution as a reference state for follow-up exercises.\n"
		"- Delete outdated checkpoints between training days to keep the list manageable.")));

	Sections.Add(Make(TEXT("performance"), TEXT("Performance & AAR"),
		TEXT("Real-time debrief screen. Four sections.\n\n"
		"**SCORE hero**\n\n"
		"Large SCORE value (white when >= 0, red when < 0). EFFICIENCY % colored by threshold - green >= 90%, amber 70-89%, red < 70%.\n\n"
		"**OPERATIONS card** (green-bordered) - positive metrics\n\n"
		"Landings / Handoffs / Resolved conflicts / Intercepts / Emergencies successfully handled.\n\n"
		"**INCIDENTS card** (red-bordered) - penalty metrics\n\n"
		"Go-Arounds / Sep Loss / Wake Busts / TCAS RA / Strayed (amber tint when > 0) then Mis-ID / Violated / Crashed / Busted (red tint when > 0). Rows dim grey when zero.\n\n"
		"**SESSION SUMMARY** - Duration / Scenario / Aircraft count / Wind.\n\n"
		"@@EXPORT AAR@@ writes a full Markdown AAR to `<ProjectSavedDir>/Reports/Session_YYYYMMDD_HHMMSS.md`. Report structure: header (timestamp, session duration, scenario, wind), summary (score, efficiency, ops totals, incident counts), chronological timeline table, critical incidents drilldown with 60-second comms window, score breakdown, full transcript.\n\n"
		"**TRANSCRIPT sub-tab**\n\n"
		"Live scroll of voice and text comms. Each entry: timestamp `MM:SS` + role stripe + role tag + speaker callsign + text content. Roles are color-coded - PILOT cyan, OPERATOR green, SYS grey, INSTR magenta, TWR/ACC/AWACS/GCI amber-family, ATIS/MET muted cyan.\n\n"
		"**Filter dropdown** at the top lets you multi-select which roles to show. Click a role to toggle it in the filter set. Click **All** to select or deselect everything. Useful presets by selecting multiple: Voice Only (PILOT + OPERATOR + facilities), Instructor Actions Only (INSTR), Facility Broadcasts (TWR + ACC + AWACS + GCI + ATIS + MET).")));

	Sections.Add(Make(TEXT("federation"), TEXT("Federation Interoperability"),
		TEXT("CLEARANCE speaks IEEE 1278 Distributed Interactive Simulation, the "
		"defence-industry standard for federating multiple training simulators "
		"across a network. When enabled, aircraft state emits as Entity State "
		"PDUs over UDP with correct Big-Endian byte order per the DIS spec; "
		"external simulators on the same federation see CLEARANCE aircraft as "
		"live entities and can inject their own tracks back into the sim.\n\n"
		"**Why it matters**\n\n"
		"Real defence training rigs at CAE / L3 / Lockheed / BAE run in federated "
		"exercises. A sortie may span an aircraft simulator, a radar operator "
		"station, an AWACS console, a GCI position, and a ground threat model - "
		"each running on separate hardware and connected over DIS or HLA. "
		"CLEARANCE plugs into that federation as an ATC / air-defence station, "
		"contributing its picture and consuming external tracks.\n\n"
		"**Emitter**\n\n"
		"- `UClearanceDISEmitter` broadcasts Entity State PDUs for every "
		"aircraft in the sector\n"
		"- Standard IEEE 1278.1 packet format: header + entity ID + force ID + "
		"entity type + location + orientation + velocity + appearance\n"
		"- Emits at the tick rate (default ~5 Hz per DIS convention); packet "
		"count per second shown in the AAR footer `[DIS N/s]` tag\n"
		"- Ground truth is emitted, not the operator's degraded picture, so "
		"external observers see the sector as it really is\n\n"
		"**Receiver**\n\n"
		"- `UClearanceDISReceiver` listens on a UDP port for incoming Entity "
		"State PDUs from other federation members\n"
		"- Received entities land in the AirspaceManager as first-class "
		"aircraft with a synthetic callsign, correct affiliation, and full "
		"position / velocity state\n"
		"- Operator and instructor scopes render them exactly like sim-native "
		"aircraft - the trainee cannot tell whether a track came from the local "
		"scenario or the external federation\n\n"
		"**Enabling from the instructor UI**\n\n"
		"DIS controls live in the DIS FEDERATION section of the right-hand "
		"panel:\n\n"
		"- **EMIT** row - host + port input, then [START EMIT] / !!STOP EMIT!! "
		"button. Multicast `224.0.0.1:3000` is the classic default for local "
		"exercises; unicast to a specific federation broker also works.\n"
		"- **RECV** row - port input, then [START RECV] / !!STOP RECV!! button. "
		"Listens on the given UDP port and lands external entities in the "
		"AirspaceManager.\n"
		"- **Status** row - two indicator lights (grey = idle, green = active) "
		"and live packet-per-second counters for each direction. Federation "
		"health at a glance.\n\n"
		"When both sides are running the AAR header line shows "
		"`[DIS OUT: N/s | DIS IN: N/s]` for post-session review.\n\n"
		"**Site-level exercise use**\n\n"
		"For a live training rig, the recommended flow is: cold-start the "
		"federation on a dedicated multicast address, bring each participating "
		"station online in sequence, start receivers before emitters so the "
		"initial states aren't missed, verify the packet counter climbs, and "
		"then start the scenario. Post-session, the DIS packet totals appear "
		"in the exported AAR alongside operations and incidents.\n\n"
		"**Interop status**\n\n"
		"Current build ships six IEEE 1278.1 PDU types:\n\n"
		"- **Entity State PDU (Type 1, §7.3.4)** - one per aircraft per tick, "
		"broadcasting position / velocity / orientation / affiliation.\n"
		"- **Fire PDU (Type 2, §7.3.3)** - fires on each SCRAMBLE launch, "
		"broadcasting firing entity, target entity, munition entity, launch "
		"position + velocity, and a burst descriptor (munition kind / warhead "
		"/ fuse / quantity). Event ID uniquely identifies each launch.\n"
		"- **Detonation PDU (Type 3, §7.3.4)** - fires on intercept "
		"resolution, broadcasting the impact location + detonation result "
		"code. Pairs with the originating Fire PDU by matching Event ID so "
		"external federates can close the fire-effect loop.\n"
		"- **Emission PDU (Type 23, §7.6.2)** - one per active radar per tick, "
		"broadcasting emitter fingerprint (frequency, PRF, pulse width, ERP) "
		"and a Track/Jam list of every aircraft the radar is currently "
		"painting. Turns each radar into a first-class federation citizen "
		"visible to any DIS ELINT receiver.\n"
		"- **Transmitter PDU (Type 25, §7.7.2)** - heartbeat per active radio "
		"per tick, announcing frequency (121.5 MHz ATC guard), bandwidth, "
		"modulation (VHF AM), power, and transmit state (off / on-idle / "
		"on-transmitting). Federation receivers use this to discover who is "
		"on the air and what to tune to before the Signal PDU delivers the "
		"actual traffic.\n"
		"- **Signal PDU (Type 26, §7.7.3)** - fires on each transcribed radio "
		"transmission (operator commands, pilot readbacks, facility "
		"broadcasts). Radio Communications family, raw-binary payload carrying "
		"the ASCII transcript so an external federate can log the whole voice "
		"picture without a codec dependency.\n\n"
		"### DDS wire (OMG Data Distribution Service via eProsima Fast DDS)\n\n"
		"Alongside DIS, CLEARANCE publishes the same six data primitives as "
		"OMG DDS topics under the `clearance/*` namespace. DDS is the middleware "
		"next-generation defence platforms actually run on - Tempest / FCAS "
		"combat cloud, FACE-compliant avionics, autonomous wingmen. Same aircraft "
		"state, fire event, detonation event, radar emission, transmitter "
		"heartbeat, and voice signal on both wires - a defence integrator sees "
		"both legacy DIS and next-generation DDS federation in one project.\n\n"
		"### Federation ownership\n\n"
		"Each running CLEARANCE instance is a **federate** with a unique **Site "
		"ID**. When two federates share a domain, each publishes its own "
		"aircraft and ingests peer aircraft as **external tracks** (display-only, "
		"non-commandable). The aircraft list shows an @@OWN@@ chip for locally-"
		"owned aircraft and a `SITE N` chip for peer-owned ones. Only the "
		"owning federate can RECLASSIFY, INJECT emergencies, or SCRAMBLE against "
		"its own aircraft - matches real-world ATC handoff doctrine where each "
		"facility has authority over aircraft in its sector.\n\n"
		"### 2-federate setup (DIS + DDS)\n\n"
		"Open two independent CLEARANCE processes (**Play Standalone** ×2, "
		"NOT two client windows of the same editor). Then in each:\n\n"
		"**Instance A console:**\n"
		"- `clearance.dis.site 1` (identity for both wires)\n"
		"- `clearance.dis.start` (broadcast DIS PDUs)\n"
		"- `clearance.dis.listen 3000` (ingest peer's DIS)\n"
		"- `clearance.dds.start` (publish DDS topics on domain 0)\n"
		"- `clearance.dds.listen` (subscribe to peer's DDS topics)\n\n"
		"**Instance B console (DIFFERENT site ID):**\n"
		"- `clearance.dis.site 2`\n"
		"- Same DIS/DDS start + listen sequence.\n\n"
		"Both scopes now show each other's aircraft as external tracks. The "
		"@@OWN@@ / `SITE N` chips make ownership obvious. Kill either instance "
		"and its aircraft age out of the peer's scope after a few seconds.\n\n"
		"### Instructor Station buttons (`DIS FEDERATION` + `DDS FEDERATION`)\n\n"
		"Both panel sections have `EMIT` / `STOP` buttons for the publish side "
		"and `RECV` / `STOP` buttons for the ingest side. DIS takes a HOST + "
		"PORT (default 127.0.0.1 : 3000); DDS takes a DOMAIN ID (default 0). "
		"Set the same values across two federates so they discover each other.\n\n"
		"### HLA / IEEE 1516\n\n"
		"Architecture + RPR-FOM 2.0 mapping documented in "
		"`Docs/HLA_INTEGRATION.md`. Concrete FOM module ships at "
		"`Plugins/ClearanceSim/FOM/ClearanceRPR-FOM.xml` - extends the SISO "
		"RPR-FOM 2.0 base with an `ATCManagedAircraft` subclass carrying "
		"SquawkCode / FlightPhase / ActiveClearance / ATCFacility. Runtime "
		"integration deferred pending RTI vendor selection - see the doc for "
		"the OpenRTI / Portico decision rationale.")));

	Sections.Add(Make(TEXT("reference"), TEXT("Button Reference"),
		TEXT("**Right panel**\n\n"
		"- [INJECT] - Apply selected emergency type to selected aircraft\n"
		"- !!CLEAR EMER!! - Cancel active emergency on selected aircraft\n"
		"- [RECLASSIFY] - Set true affiliation of selected aircraft\n"
		"- !!JAM ON!! - Selected aircraft begins EW jamming\n"
		"- @@JAM OFF@@ - Cancel EW jamming\n"
		"- !!DROP CHAFF!! - Selected aircraft releases chaff cloud\n"
		"- ~~SCRAMBLE~~ - Launch interceptors on selected aircraft\n"
		"- [LOAD] scenario - Load selected scenario, clear existing traffic, start\n"
		"- !!STOP!! - Halt running scenario (aircraft remain)\n"
		"- ~~CLEAR ALL~~ - Remove all aircraft from sector\n"
		"- [SPAWN +1] - Add one random aircraft\n"
		"- [APPLY WIND] - Commit wind direction and speed\n\n"
		"**Sector controls**\n\n"
		"- ~~RESET SCENARIO~~ - Full session wipe. Irreversible.\n\n"
		"**SESSION row (top)**\n\n"
		"- !!PAUSE!! - Pause the live simulation\n"
		"- @@SAVE@@ - Save checkpoint with typed name\n"
		"- [LOAD] - Restore selected checkpoint\n"
		"- ~~DEL~~ - Delete selected checkpoint\n"
		"- `0.25x` / `0.5x` / `1x` / `2x` / `4x` - Live time dilation\n\n"
		"**REPLAY row (bottom)**\n\n"
		"- [PLAY] - Start replay playback\n"
		"- `0.25x` / `0.5x` / `1x` / `2x` / `4x` - Replay playback speed\n"
		"- ~~GO LIVE~~ - Exit replay, return to live session\n\n"
		"**Performance tab**\n\n"
		"- @@EXPORT AAR@@ - Write Markdown AAR to disk")));

	return Sections;
}

// --- Inject forwarders ----------------------------------------------------

void UClearanceInstructorPanel::InjectEmergency(FName Callsign, EEmergencyType Kind, float TimerMinutes)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectEmergency(Callsign, Kind, TimerMinutes); }
}
void UClearanceInstructorPanel::InjectClearEmergency(FName Callsign)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectClearEmergency(Callsign); }
}
void UClearanceInstructorPanel::InjectClassify(FName Callsign, EThreatClass NewClass)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectClassify(Callsign, NewClass); }
}
void UClearanceInstructorPanel::InjectScramble(FName BanditCallsign)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectScramble(BanditCallsign); }
}
void UClearanceInstructorPanel::InjectJamming(FName Callsign, bool bOn)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectJamming(Callsign, bOn); }
}
void UClearanceInstructorPanel::InjectChaff(FName Callsign)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectChaff(Callsign); }
}
void UClearanceInstructorPanel::InjectSpawn()
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectSpawn(); }
}
void UClearanceInstructorPanel::InjectClearTraffic()
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectClearTraffic(); }
}
void UClearanceInstructorPanel::InjectSetWind(float DirectionDeg, float SpeedKts)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectSetWind(DirectionDeg, SpeedKts); }
}
void UClearanceInstructorPanel::InjectLoadScenario(const FString& ScenarioName)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectLoadScenario(ScenarioName); }
}
void UClearanceInstructorPanel::InjectStopScenario()
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectStopScenario(); }
}
void UClearanceInstructorPanel::InjectResetScenario()
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectResetScenario(); }
}
void UClearanceInstructorPanel::InjectSetPaused(bool bNewPaused)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectSetPaused(bNewPaused); }
}
void UClearanceInstructorPanel::InjectSetTimeScale(float Scale)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectSetTimeScale(Scale); }
}

// --- Aircraft list population ---------------------------------------------

namespace
{
	// ProcessEvent payloads have to match the BP function's parameter layout
	// exactly. SetRowData takes one FInstructorAircraftRow by value. - TripleA
	struct FSetRowDataParam
	{
		FInstructorAircraftRow Row;
	};

	struct FSetSelectedParam
	{
		bool bSelected = false;
	};
}

namespace
{
	void PushRowData(UUserWidget* RowWidget, const FInstructorAircraftRow& Row)
	{
		if (!RowWidget) { return; }
		if (UFunction* Fn = RowWidget->FindFunction(TEXT("SetRowData")))
		{
			FSetRowDataParam Param;
			Param.Row = Row;
			RowWidget->ProcessEvent(Fn, &Param);
		}
	}

	void PushRowSelected(UUserWidget* RowWidget, bool bSelected)
	{
		if (!RowWidget) { return; }
		if (UFunction* Fn = RowWidget->FindFunction(TEXT("SetSelected")))
		{
			FSetSelectedParam Param;
			Param.bSelected = bSelected;
			RowWidget->ProcessEvent(Fn, &Param);
		}
	}
}

void UClearanceInstructorPanel::PopulateAircraftScrollBox(UScrollBox* ScrollBox, const TArray<FInstructorAircraftRow>& Rows)
{
	if (!ScrollBox) { return; }

	if (!AircraftRowClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InstructorPanel] PopulateAircraftScrollBox: AircraftRowClass not set."));
		return;
	}

	// Minimal-diff reflow. Walk both lists in order looking for the first
	// index where CurrentRowCallsigns disagrees with Rows. Everything before
	// that stays untouched (in-place data push only) - Slate widgets keep
	// their existing layout, no flash. Everything after gets rebuilt.
	// ClearChildren has to be avoided entirely because it invalidates the
	// panel's Slate tree even when the same widget instances get re-added,
	// which is what was still causing the visible blink. - TripleA
	int32 InSync = 0;
	while (InSync < Rows.Num()
	    && InSync < CurrentRowCallsigns.Num()
	    && InSync < ScrollBox->GetChildrenCount()
	    && CurrentRowCallsigns[InSync] == Rows[InSync].Callsign)
	{
		++InSync;
	}

	// Data-push the in-sync prefix. No structural change to the panel.
	for (int32 i = 0; i < InSync; ++i)
	{
		UUserWidget* RowWidget = Cast<UUserWidget>(ScrollBox->GetChildAt(i));
		PushRowData(RowWidget, Rows[i]);
		PushRowSelected(RowWidget, Rows[i].Callsign == SelectedCallsign && SelectedCallsign != NAME_None);
	}

	// Trim the mismatched suffix from the panel. Widget objects survive via
	// AircraftRowByCallsign strong refs. - TripleA
	while (ScrollBox->GetChildrenCount() > InSync)
	{
		ScrollBox->RemoveChildAt(ScrollBox->GetChildrenCount() - 1);
	}

	// Append the new suffix, reusing widget instances when the callsign is
	// familiar.
	for (int32 i = InSync; i < Rows.Num(); ++i)
	{
		const FInstructorAircraftRow& Row = Rows[i];
		UUserWidget* RowWidget = nullptr;
		if (TObjectPtr<UUserWidget>* Existing = AircraftRowByCallsign.Find(Row.Callsign))
		{
			RowWidget = Existing->Get();
		}
		if (!RowWidget)
		{
			RowWidget = CreateWidget<UUserWidget>(this, AircraftRowClass);
			if (!RowWidget) { continue; }
			AircraftRowByCallsign.Add(Row.Callsign, RowWidget);
		}
		PushRowData(RowWidget, Row);
		PushRowSelected(RowWidget, Row.Callsign == SelectedCallsign && SelectedCallsign != NAME_None);
		ScrollBox->AddChild(RowWidget);
	}

	// Sync bookkeeping.
	CurrentRowCallsigns.Reset(Rows.Num());
	for (const FInstructorAircraftRow& Row : Rows)
	{
		CurrentRowCallsigns.Add(Row.Callsign);
	}

	// Drop widgets whose callsigns left the sim.
	TSet<FName> LiveCallsigns;
	LiveCallsigns.Reserve(Rows.Num());
	for (const FInstructorAircraftRow& Row : Rows) { LiveCallsigns.Add(Row.Callsign); }
	for (auto It = AircraftRowByCallsign.CreateIterator(); It; ++It)
	{
		if (!LiveCallsigns.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

namespace
{
	void PushEventRowData(UUserWidget* RowWidget, const FClearanceNotification& Entry)
	{
		if (!RowWidget) { return; }
		UFunction* Fn = RowWidget->FindFunction(TEXT("SetNotification"));
		if (!Fn) { return; }
		// Pass the FClearanceNotification directly. ProcessEvent copies the
		// param frame into the function's own stack based on the property
		// layout declared on the UFunction, not by matching our struct's
		// member names - so passing the value itself is safest.
		// Cast away const because ProcessEvent's signature takes void*. - TripleA
		FClearanceNotification LocalCopy = Entry;
		RowWidget->ProcessEvent(Fn, &LocalCopy);
	}
}

void UClearanceInstructorPanel::PopulateEventLogScrollBox(UScrollBox* ScrollBox,
	TSubclassOf<UUserWidget> RowClass,
	const TArray<FClearanceNotification>& Entries)
{
	if (!ScrollBox || !RowClass) { return; }

	auto ToMs = [](float SecondsFloat)
	{
		return FMath::RoundToInt(SecondsFloat * 1000.f);
	};

	// Server's RepNotifications ring buffer caps at 12 and trims front by
	// RemoveAt(0) when it overflows. So every new notification past the cap
	// shifts every existing entry left by one. Detect the front-trim by
	// looking for the first NEW entry's time inside CurrentEventTimesMs -
	// its index there is how many entries were trimmed. Remove that many
	// widgets from the FRONT of the ScrollBox before the prefix-diff runs,
	// so what SHOULD be the prefix actually aligns. - TripleA
	int32 FrontTrim = 0;
	if (Entries.Num() > 0 && CurrentEventTimesMs.Num() > 0)
	{
		const int32 FirstNewMs = ToMs(Entries[0].ServerTimeAdded);
		for (int32 i = 0; i < CurrentEventTimesMs.Num(); ++i)
		{
			if (CurrentEventTimesMs[i] == FirstNewMs) { FrontTrim = i; break; }
		}
	}
	// If nothing matched, the sim reset - fall through and let the prefix
	// diff below rebuild everything.
	if (FrontTrim > 0)
	{
		const int32 TrimCount = FMath::Min(FrontTrim, ScrollBox->GetChildrenCount());
		for (int32 k = 0; k < TrimCount; ++k)
		{
			ScrollBox->RemoveChildAt(0);
		}
		CurrentEventTimesMs.RemoveAt(0, FMath::Min(FrontTrim, CurrentEventTimesMs.Num()));
	}

	// Prefix-diff on the (now-aligned) arrays. - TripleA
	int32 InSync = 0;
	while (InSync < Entries.Num()
	    && InSync < CurrentEventTimesMs.Num()
	    && InSync < ScrollBox->GetChildrenCount()
	    && CurrentEventTimesMs[InSync] == ToMs(Entries[InSync].ServerTimeAdded))
	{
		++InSync;
	}

	for (int32 i = 0; i < InSync; ++i)
	{
		UUserWidget* RowWidget = Cast<UUserWidget>(ScrollBox->GetChildAt(i));
		PushEventRowData(RowWidget, Entries[i]);
	}

	while (ScrollBox->GetChildrenCount() > InSync)
	{
		ScrollBox->RemoveChildAt(ScrollBox->GetChildrenCount() - 1);
	}

	for (int32 i = InSync; i < Entries.Num(); ++i)
	{
		const FClearanceNotification& Entry = Entries[i];
		const int32 KeyMs = ToMs(Entry.ServerTimeAdded);
		UUserWidget* RowWidget = nullptr;
		if (TObjectPtr<UUserWidget>* Existing = EventRowByTimeMs.Find(KeyMs))
		{
			RowWidget = Existing->Get();
		}
		if (!RowWidget)
		{
			RowWidget = CreateWidget<UUserWidget>(this, RowClass);
			if (!RowWidget) { continue; }
			EventRowByTimeMs.Add(KeyMs, RowWidget);
		}
		PushEventRowData(RowWidget, Entry);
		ScrollBox->AddChild(RowWidget);
	}

	CurrentEventTimesMs.Reset(Entries.Num());
	for (const FClearanceNotification& Entry : Entries)
	{
		CurrentEventTimesMs.Add(ToMs(Entry.ServerTimeAdded));
	}

	TSet<int32> LiveKeys;
	LiveKeys.Reserve(Entries.Num());
	for (const FClearanceNotification& Entry : Entries) { LiveKeys.Add(ToMs(Entry.ServerTimeAdded)); }
	for (auto It = EventRowByTimeMs.CreateIterator(); It; ++It)
	{
		if (!LiveKeys.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

// --- Mini-scope projection ------------------------------------------------

FVector2D UClearanceInstructorPanel::ScopeNmToPixel(FVector2D PositionNm, FVector2D ScopeCentre, float ScopePixelRadius) const
{
	// ScopeCentre is the sector origin (0,0) in pixel coords. +X (east) is
	// pixel +X (right), +Y (north) is pixel -Y (up — screen Y is inverted).
	// ScopePixelRadius defines how many pixels = ScopeRangeNm at the outer
	// ring boundary. - TripleA
	const float Range = FMath::Max(1.f, ScopeRangeNm);
	const float Px = ScopeCentre.X + (PositionNm.X / Range) * ScopePixelRadius;
	const float Py = ScopeCentre.Y - (PositionNm.Y / Range) * ScopePixelRadius;
	return FVector2D(Px, Py);
}

namespace
{
	FLinearColor ColourForThreat(EThreatClass T)
	{
		switch (T)
		{
		case EThreatClass::Friendly: return FLinearColor(0.31f, 0.78f, 1.00f);
		case EThreatClass::Hostile:  return FLinearColor(1.00f, 0.24f, 0.24f);
		case EThreatClass::Unknown:  return FLinearColor(1.00f, 0.70f, 0.24f);
		case EThreatClass::Neutral:  return FLinearColor(0.31f, 1.00f, 0.47f);
		default:                     return FLinearColor::White;
		}
	}

	FLinearColor ColourForAlert(EAlertLevel L)
	{
		switch (L)
		{
		case EAlertLevel::Critical: return FLinearColor(1.00f, 0.24f, 0.24f);
		case EAlertLevel::Warning:  return FLinearColor(1.00f, 0.55f, 0.10f);
		case EAlertLevel::Advisory: return FLinearColor(1.00f, 0.85f, 0.20f);
		default:                    return FLinearColor::Transparent;
		}
	}

	void PaintLine(FPaintContext& Ctx, FVector2D A, FVector2D B, const FLinearColor& Tint, float Thickness = 1.5f)
	{
		UWidgetBlueprintLibrary::DrawLine(Ctx, A, B, Tint, true);
		// UWidgetBlueprintLibrary::DrawLine ignores thickness in some UE
		// versions; double-up to fake heavier strokes if needed. - TripleA
		if (Thickness > 1.6f)
		{
			UWidgetBlueprintLibrary::DrawLine(Ctx, A + FVector2D(0.5f, 0.f), B + FVector2D(0.5f, 0.f), Tint, true);
		}
	}

	void PaintPolyline(FPaintContext& Ctx, const TArray<FVector2D>& Points, const FLinearColor& Tint)
	{
		UWidgetBlueprintLibrary::DrawLines(Ctx, Points, Tint, true);
	}

	void PaintCircle(FPaintContext& Ctx, FVector2D Centre, float Radius, int32 Segments, const FLinearColor& Tint)
	{
		if (Segments < 4) { Segments = 4; }
		TArray<FVector2D> Pts;
		Pts.Reserve((Segments + 1) * 2);
		for (int32 i = 0; i < Segments; ++i)
		{
			const float A0 = (static_cast<float>(i)     / Segments) * 2.f * PI;
			const float A1 = (static_cast<float>(i + 1) / Segments) * 2.f * PI;
			Pts.Add(Centre + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Radius);
			Pts.Add(Centre + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Radius);
		}
		PaintPolyline(Ctx, Pts, Tint);
	}
}

void UClearanceInstructorPanel::DrawAffiliationSymbol(
	FPaintContext& Context, FVector2D Centre, EThreatClass Threat,
	bool bIsMilitary, float HeadingDeg, EAlertLevel Alert, float HalfSizePx, float Alpha)
{
	const float A = FMath::Clamp(Alpha, 0.f, 1.f);
	FLinearColor Frame = (Alert == EAlertLevel::Critical) ? ColourForAlert(Alert) : ColourForThreat(Threat);
	Frame.A *= A;
	const float H = FMath::Max(2.f, HalfSizePx);

	TArray<FVector2D> Frags;

	auto Add = [&](FVector2D A, FVector2D B) { Frags.Add(A); Frags.Add(B); };

	switch (Threat)
	{
	case EThreatClass::Friendly:
	{
		// Rectangle, wider than tall.
		const FVector2D W(H, 0);
		const FVector2D T(0, H * 0.65f);
		Add(Centre - W + T, Centre + W + T);
		Add(Centre + W + T, Centre + W - T);
		Add(Centre + W - T, Centre - W - T);
		Add(Centre - W - T, Centre - W + T);
		break;
	}
	case EThreatClass::Hostile:
	{
		// Diamond - screen Y inverted so "N" is -Y in pixels.
		const FVector2D N(0, -H);
		const FVector2D E(H, 0);
		const FVector2D S(0, H);
		const FVector2D Wv(-H, 0);
		Add(N, E); Add(E, S); Add(S, Wv); Add(Wv, N);
		for (FVector2D& P : Frags) { P += Centre; }
		break;
	}
	case EThreatClass::Unknown:
	{
		// Octagon (quatrefoil approximation).
		const float K = H * 0.4f;
		const FVector2D Pts[] = {
			FVector2D( H,  K),  FVector2D( K,  H),
			FVector2D(-K,  H),  FVector2D(-H,  K),
			FVector2D(-H, -K),  FVector2D(-K, -H),
			FVector2D( K, -H),  FVector2D( H, -K),
		};
		for (int32 i = 0; i < 8; ++i) { Add(Pts[i], Pts[(i + 1) % 8]); }
		for (FVector2D& P : Frags) { P += Centre; }
		break;
	}
	case EThreatClass::Neutral:
	{
		const FVector2D W(H * 0.85f, 0);
		const FVector2D T(0, H * 0.85f);
		Add(Centre - W + T, Centre + W + T);
		Add(Centre + W + T, Centre + W - T);
		Add(Centre + W - T, Centre - W - T);
		Add(Centre - W - T, Centre - W + T);
		break;
	}
	default: break;
	}

	// Symbol shape only. NOTE: don't append the bearing vector to Frags -
	// PaintPolyline draws a CONNECTED polyline, which would render an extra
	// segment from the last symbol vertex to (Centre) before continuing to
	// the bearing tip. That stray diagonal was visible inside the box as a
	// "mystery third line". Bearing vector is drawn separately below with
	// its own PaintLine so the symbol polyline ends cleanly. - TripleA
	PaintPolyline(Context, Frags, Frame);

	// Bearing vector. Screen Y inverted: north (0deg) = -Y.
	const float Rad = FMath::DegreesToRadians(HeadingDeg);
	const FVector2D BearingTip = Centre + FVector2D(FMath::Sin(Rad), -FMath::Cos(Rad)) * H * 1.6f;
	PaintLine(Context, Centre, BearingTip, Frame);

	if (Alert != EAlertLevel::None)
	{
		FLinearColor AlertRing = ColourForAlert(Alert);
		AlertRing.A *= A;
		PaintCircle(Context, Centre, H * 1.4f, 24, AlertRing);
	}

	if (bIsMilitary)
	{
		const float Below = H * 1.15f;
		const float Sz = H * 0.22f;
		PaintLine(Context, Centre + FVector2D(-Sz, Below), Centre + FVector2D(Sz, Below), Frame);
		PaintLine(Context, Centre + FVector2D(0, Below - Sz), Centre + FVector2D(0, Below + Sz), Frame);
	}
}

void UClearanceInstructorPanel::DrawScopeBoundary(FPaintContext& Context, FVector2D ScopeCentre, float ScopePixelRadius)
{
	const float Outer = FMath::Max(8.f, ScopePixelRadius);

	const FLinearColor Border    = FLinearColor(0.20f, 0.24f, 0.31f, 0.85f);
	const FLinearColor Faint     = FLinearColor(0.20f, 0.24f, 0.31f, 0.45f);
	const FLinearColor MinorTick = FLinearColor(0.31f, 0.78f, 1.00f, 0.40f);
	const FLinearColor MajorTick = FLinearColor(0.31f, 0.78f, 1.00f, 0.85f);
	const FLinearColor RoseLabel = FLinearColor(0.55f, 0.88f, 1.00f, 0.85f);

	// Three range rings (25 / 50 / 75% of outer) + outer boundary.
	PaintCircle(Context, ScopeCentre, Outer * 0.25f, 32, Faint);
	PaintCircle(Context, ScopeCentre, Outer * 0.50f, 48, Faint);
	PaintCircle(Context, ScopeCentre, Outer * 0.75f, 56, Faint);
	PaintCircle(Context, ScopeCentre, Outer,          64, Border);

	// Standard ATC heading rose: 36 ticks at every 10deg around the perimeter,
	// every third tick (12 total at every 30deg) gets a longer mark + a
	// "000" / "030" / "060" / ... 3-digit label sitting just outside it. Screen
	// Y is inverted so 0deg (north) points along -Y; labels rotate with the
	// scope but are kept upright for readability. - TripleA
	constexpr int32 NumTicks = 36; // 360 / 10
	for (int32 i = 0; i < NumTicks; ++i)
	{
		const float DegA = static_cast<float>(i) * (360.f / static_cast<float>(NumTicks));
		const float RadA = FMath::DegreesToRadians(DegA);
		const FVector2D Dir(FMath::Sin(RadA), -FMath::Cos(RadA));

		const bool bMajor = ((i % 3) == 0);
		const float Inner    = bMajor ? Outer - 4.f : Outer - 1.f;
		const float TickEnd  = bMajor ? Outer + 6.f : Outer + 3.f;

		PaintLine(Context,
			ScopeCentre + Dir * Inner,
			ScopeCentre + Dir * TickEnd,
			bMajor ? MajorTick : MinorTick);

		if (bMajor)
		{
			// 3-digit heading label sitting just outside the major tick. Pre-
			// shift the text anchor by half its approximate pixel size so the
			// label visually centres on the tick instead of hanging off it.
			// 7px/char * 3 chars = ~21px wide, ~12px tall at default font. - TripleA
			const int32 DegInt = FMath::RoundToInt(DegA);
			const FString LabelText = FString::Printf(TEXT("%03d"), DegInt);
			const FVector2D Anchor = ScopeCentre + Dir * (Outer + 14.f);
			UWidgetBlueprintLibrary::DrawText(Context, LabelText,
				Anchor + FVector2D(-10.f, -6.f), RoseLabel);
		}
	}

	// Cardinal letters (N / E / S / W) sit a little further out than the
	// numeric labels, brighter + bigger-looking via full alpha. Gives the
	// scope an instant orientation read at a glance without having to parse
	// the numeric headings. Real ATC scopes pair the numeric rose with
	// these for exactly that reason. - TripleA
	const FLinearColor CardinalTint = FLinearColor(0.85f, 1.00f, 1.00f, 1.0f);
	static const TCHAR* CardinalGlyphs[4] = { TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W") };
	static const float  CardinalDeg[4]    = { 0.f, 90.f, 180.f, 270.f };
	for (int32 c = 0; c < 4; ++c)
	{
		const float RadC = FMath::DegreesToRadians(CardinalDeg[c]);
		const FVector2D Dir(FMath::Sin(RadC), -FMath::Cos(RadC));
		const FVector2D Anchor = ScopeCentre + Dir * (Outer + 32.f);
		UWidgetBlueprintLibrary::DrawText(Context, CardinalGlyphs[c],
			Anchor + FVector2D(-3.f, -7.f), CardinalTint);
	}
}

void UClearanceInstructorPanel::DrawScopeChaffCloud(FPaintContext& Context, FVector2D Centre, float AgeFrac)
{
	AgeFrac = FMath::Clamp(AgeFrac, 0.f, 1.f);
	const float Radius = 12.f - 8.f * AgeFrac;
	const float Alpha  = 1.f - AgeFrac * 0.7f;
	const FLinearColor Tint(1.0f, 0.86f - AgeFrac * 0.30f, 0.24f, Alpha);
	PaintCircle(Context, Centre, FMath::Max(2.f, Radius), 16, Tint);
}

void UClearanceInstructorPanel::DrawZoneMarker(FPaintContext& Context, FVector2D ScopeCentre,
	float ScopePixelRadius, const FInstructorZoneMarker& Zone)
{
	// Route position through ScopeNmToPixel so there's one projection path
	// shared with aircraft / chaff / runway markers - keeps everything
	// proportional through zoom changes. Radius uses the same nm->px scale
	// factor (no floor - let small zones shrink to invisible at wide zoom). - TripleA
	const FVector2D Centre = ScopeNmToPixel(Zone.PositionNm, ScopeCentre, ScopePixelRadius);
	const float Rad = (Zone.RadiusNm / FMath::Max(1.f, ScopeRangeNm)) * ScopePixelRadius;
	if (Rad < 2.f) { return; }

	const FLinearColor Tint = Zone.bIsProtected
		? FLinearColor(1.00f, 0.24f, 0.24f, 0.65f)   // protected = red
		: FLinearColor(1.00f, 0.70f, 0.24f, 0.55f);  // restricted = amber

	// Dashed ring - draw alternating arc segments around the circumference.
	// Cheaper than a real dash pattern + reads as "restricted" at a glance.
	constexpr int32 DashSegments = 24;
	TArray<FVector2D> Pts;
	Pts.Reserve(DashSegments * 2);
	for (int32 i = 0; i < DashSegments; ++i)
	{
		if ((i % 2) != 0) { continue; }  // every other segment - the gaps
		const float A0 = (static_cast<float>(i)     / DashSegments) * 2.f * PI;
		const float A1 = (static_cast<float>(i + 1) / DashSegments) * 2.f * PI;
		Pts.Add(Centre + FVector2D(FMath::Cos(A0), FMath::Sin(A0)) * Rad);
		Pts.Add(Centre + FVector2D(FMath::Cos(A1), FMath::Sin(A1)) * Rad);
	}
	PaintPolyline(Context, Pts, Tint);

	// Zone name label sitting just above the top of the ring. Approximate
	// pre-centre by character count since the default font is ~7px/char. The
	// label is rendered at full alpha so it reads cleanly over the dashed
	// ring colour. - TripleA
	if (!Zone.Name.IsNone())
	{
		const FString NameStr = Zone.Name.ToString();
		const float NameWidthPx = static_cast<float>(NameStr.Len()) * 7.f;
		FLinearColor LabelTint = Tint;
		LabelTint.A = 1.f;
		UWidgetBlueprintLibrary::DrawText(Context, NameStr,
			Centre + FVector2D(-NameWidthPx * 0.5f, -Rad - 14.f), LabelTint);
	}
}

void UClearanceInstructorPanel::DrawRunwayMarker(FPaintContext& Context, FVector2D ScopeCentre,
	float ScopePixelRadius, const FRunwayInfo& Runway)
{
	// Same projection as aircraft so the runway sits at the right place
	// relative to traffic as the scope zooms. - TripleA
	const FVector2D Threshold = ScopeNmToPixel(Runway.ThresholdNm, ScopeCentre, ScopePixelRadius);

	const float Rad = FMath::DegreesToRadians(Runway.HeadingDeg);
	const FVector2D Along(FMath::Sin(Rad), -FMath::Cos(Rad));   // screen Y inverted
	const FVector2D Cross(-Along.Y, Along.X);

	const FLinearColor StripTint     (0.95f, 0.95f, 0.95f, 0.95f);
	const FLinearColor CentreTint    (0.85f, 0.85f, 0.85f, 0.55f);
	const FLinearColor DesignatorTint(0.95f, 0.95f, 0.95f, 1.00f);

	// Runway strip - thick line from threshold along landing direction for
	// the full asphalt length. Length is on the runway actor in UE units,
	// convert to scope pixels via WorldUnitsPerNm + the scope's pixel/nm
	// ratio. Falls back to a small fixed length so single-point runways
	// (or zoomed-way-out scopes) still show something. - TripleA
	const float PixelsPerNm = ScopePixelRadius / FMath::Max(1.f, ScopeRangeNm);
	const float UnitsPerNm  = (CachedController ? FMath::Max(1.f, CachedController->WorldUnitsPerNm) : 1000.f);
	const float StripLenPx  = FMath::Max(10.f, (Runway.LengthUnits / UnitsPerNm) * PixelsPerNm);
	PaintLine(Context, Threshold, Threshold + Along * StripLenPx, StripTint, 3.0f);

	// Threshold edge tick - perpendicular bar marking where the touchdown
	// zone starts. Slightly wider than the strip for visibility.
	const float ThreshHalf = FMath::Max(4.f, StripLenPx * 0.06f);
	PaintLine(Context, Threshold - Cross * ThreshHalf, Threshold + Cross * ThreshHalf, StripTint, 2.0f);

	// Extended centerline - dashed line going OUT from the threshold in the
	// approach direction (opposite the landing direction). 20nm worth of
	// pixels by default; pilots line up on this radial so the instructor
	// can see where inbound traffic should be sequencing. - TripleA
	const float CentreLineLenPx = 20.f * PixelsPerNm;
	constexpr int32 DashCount = 10;
	for (int32 i = 0; i < DashCount; ++i)
	{
		if ((i % 2) != 0) { continue; }
		const float F0 = static_cast<float>(i)     / static_cast<float>(DashCount);
		const float F1 = static_cast<float>(i + 1) / static_cast<float>(DashCount);
		PaintLine(Context,
			Threshold - Along * (F0 * CentreLineLenPx),
			Threshold - Along * (F1 * CentreLineLenPx),
			CentreTint, 1.5f);
	}

	// Two-digit designator (heading / 10, rounded; 0 -> 36 per ICAO
	// convention). L / R / C suffixes aren't on FRunwayInfo - parallel
	// siblings can be told apart by their threshold positions on the scope
	// itself. - TripleA
	int32 Des = FMath::RoundToInt(Runway.HeadingDeg / 10.f);
	if (Des <= 0)  { Des = 36; }
	if (Des > 36)  { Des = Des % 36; if (Des == 0) { Des = 36; } }
	const FString DesText = FString::Printf(TEXT("%02d"), Des);
	UWidgetBlueprintLibrary::DrawText(Context, DesText,
		Threshold + Cross * (ThreshHalf + 4.f) + FVector2D(-7.f, -6.f), DesignatorTint);
}

void UClearanceInstructorPanel::DrawWaypointMarker(FPaintContext& Context, FVector2D ScopeCentre,
	float ScopePixelRadius, const FInstructorWaypointMarker& Waypoint)
{
	const FVector2D P = ScopeNmToPixel(Waypoint.PositionNm, ScopeCentre, ScopePixelRadius);
	// Triangle marker much more subtle than the label so the navigation grid
	// reads as background reference rather than competing with aircraft. - TripleA
	const FLinearColor MarkerTint(0.65f, 0.78f, 1.00f, 0.40f);
	const FLinearColor LabelTint (0.65f, 0.78f, 1.00f, 0.55f);

	const FVector2D Top   = P + FVector2D(0.f, -5.f);
	const FVector2D BL    = P + FVector2D(-4.5f, 3.f);
	const FVector2D BR    = P + FVector2D( 4.5f, 3.f);
	PaintLine(Context, Top, BL, MarkerTint);
	PaintLine(Context, BL,  BR, MarkerTint);
	PaintLine(Context, BR,  Top, MarkerTint);

	UWidgetBlueprintLibrary::DrawText(Context, Waypoint.Name.ToString(),
		P + FVector2D(7.f, -2.f), LabelTint);
}

void UClearanceInstructorPanel::DrawAirwaySegment(FPaintContext& Context, FVector2D ScopeCentre,
	float ScopePixelRadius, const FInstructorAirwaySegment& Airway)
{
	const FVector2D A = ScopeNmToPixel(Airway.StartNm, ScopeCentre, ScopePixelRadius);
	const FVector2D B = ScopeNmToPixel(Airway.EndNm,   ScopeCentre, ScopePixelRadius);
	const FLinearColor Tint(0.36f, 0.46f, 0.62f, 0.55f);
	PaintLine(Context, A, B, Tint);
}

void UClearanceInstructorPanel::DrawRangeLabels(FPaintContext& Context, FVector2D ScopeCentre,
	float ScopePixelRadius)
{
	const float Range = FMath::Max(1.f, ScopeRangeNm);
	const FLinearColor Tint(0.55f, 0.72f, 0.95f, 0.65f);

	// Label each ring just to the right of the upward (north) tick - same place
	// real ATC scopes put the scale annotation. - TripleA
	const float Fractions[] = { 0.25f, 0.50f, 0.75f };
	for (float F : Fractions)
	{
		const int32 Nm = FMath::RoundToInt(Range * F);
		const FString Label = FString::Printf(TEXT("%d"), Nm);
		const FVector2D Pos = ScopeCentre + FVector2D(4.f, -ScopePixelRadius * F - 6.f);
		UWidgetBlueprintLibrary::DrawText(Context, Label, Pos, Tint);
	}
}

void UClearanceInstructorPanel::DrawSelectedRing(FPaintContext& Context, FVector2D Centre, float RadiusPx)
{
	const FLinearColor Tint(1.00f, 1.00f, 1.00f, 0.85f);
	PaintCircle(Context, Centre, FMath::Max(4.f, RadiusPx), 32, Tint);
	// Inner tick marks at 4 corners so it reads as a reticle, not just a circle.
	const float Inner = RadiusPx * 0.7f;
	const float Outer = RadiusPx * 1.3f;
	PaintLine(Context, Centre + FVector2D(0, -Inner), Centre + FVector2D(0, -Outer), Tint);
	PaintLine(Context, Centre + FVector2D(0,  Inner), Centre + FVector2D(0,  Outer), Tint);
	PaintLine(Context, Centre + FVector2D(-Inner, 0), Centre + FVector2D(-Outer, 0), Tint);
	PaintLine(Context, Centre + FVector2D( Inner, 0), Centre + FVector2D( Outer, 0), Tint);
}

namespace
{
	// Pre-formatted text for one aircraft's data block. Returns 2 lines for
	// minimal mode, 4 lines for full ATC. - TripleA
	TArray<FString> BuildLabelLines(const FInstructorAircraftRow& Row, bool bShowFullDataBlock)
	{
		TArray<FString> Lines;
		Lines.Add(Row.Callsign.ToString());

		const int32 FL = FMath::RoundToInt(Row.Altitude / 100.f);
		if (bShowFullDataBlock)
		{
			const float AltDelta = Row.TargetAltitude - Row.Altitude;
			const TCHAR* VsTag = (FMath::Abs(AltDelta) < 50.f) ? TEXT("")
				: (AltDelta > 0.f) ? TEXT(" ^") : TEXT(" v");
			Lines.Add(FString::Printf(TEXT("FL%03d%s"), FL, VsTag));
			Lines.Add(FString::Printf(TEXT("%dkt"), FMath::RoundToInt(Row.Speed)));

			const float HdgDelta = FMath::Abs(FMath::FindDeltaAngleDegrees(Row.Heading, Row.TargetHeading));
			const int32 Hdg    = FMath::RoundToInt(Row.Heading);
			const int32 HdgTgt = FMath::RoundToInt(Row.TargetHeading);
			if (HdgDelta > 5.f)
			{
				Lines.Add(FString::Printf(TEXT("%03d > %03d"), Hdg, HdgTgt));
			}
			else
			{
				Lines.Add(FString::Printf(TEXT("%03d"), Hdg));
			}
		}
		else
		{
			// Minimum data block: callsign + "FL### HDG" on one line. Heading
			// is always-on in short mode so the controller can read it directly
			// instead of having to mentally correct for the off-centre parallax
			// when comparing the bearing vector against the rim's heading rose.
			// Real STARS / DSR scopes do the same in minimum-block mode. - TripleA
			const int32 Hdg = FMath::RoundToInt(Row.Heading);
			Lines.Add(FString::Printf(TEXT("FL%03d %03d"), FL, Hdg));
		}
		return Lines;
	}

	// Rough pixel estimate of the label bounding box given line count + max
	// chars per line. The default Slate font sits around 7-8px per character
	// at this size; rounded up so the overlap check has a safe margin. - TripleA
	FVector2D EstimateLabelSize(const TArray<FString>& Lines)
	{
		constexpr float LineHeight = 12.f;
		constexpr float CharWidth  = 7.f;
		int32 MaxLen = 1;
		for (const FString& L : Lines) { MaxLen = FMath::Max(MaxLen, L.Len()); }
		return FVector2D(MaxLen * CharWidth + 4.f, Lines.Num() * LineHeight + 2.f);
	}
}

void UClearanceInstructorPanel::DrawAircraftLabel(FPaintContext& Context, FVector2D Centre,
	const FInstructorAircraftRow& Row, bool bShowFullDataBlock)
{
	// Position offset: 1 o'clock from the symbol, clear of the bearing vector.
	const FVector2D LabelTopLeft = Centre + FVector2D(14.f, -22.f);

	const FLinearColor Tint = (Row.CurrentAlertLevel == EAlertLevel::Critical)
		? FLinearColor(1.00f, 0.24f, 0.24f, 1.f)
		: ColourForThreat(Row.ThreatClass);

	constexpr float LineHeight = 12.f;
	const TArray<FString> Lines = BuildLabelLines(Row, bShowFullDataBlock);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		UWidgetBlueprintLibrary::DrawText(Context, Lines[i],
			LabelTopLeft + FVector2D(0.f, i * LineHeight), Tint);
	}
}

void UClearanceInstructorPanel::DrawCoverageGrid(FPaintContext& Context,
	FVector2D ScopeCentre, float ScopePixelRadius)
{
	UWorld* World = GetWorld();
	if (!World || !CachedController) { return; }

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	if (!WhiteBrush) { return; }

	const FVector ControllerOrigin = CachedController->GetActorLocation();
	const float UnitsPerNm  = FMath::Max(1.f, CachedController->WorldUnitsPerNm);
	const float PixelsPerNm = ScopePixelRadius / FMath::Max(1.f, ScopeRangeNm);

	// No red full-scope background - tints the underlying scope and obscures
	// waypoints / zones / airways. Absence of green coverage already reads
	// as "no coverage" once you understand the overlay. - TripleA

	// Triangle-fan radial gradient per radar - centre vertex bright green,
	// edge vertices fully transparent. Slate interpolates vertex colors
	// across the triangles so the disc reads as a smooth falloff. Overlapping
	// discs blend additively-ish under standard alpha blending: the fusion
	// area is visibly more saturated/opaque than single-radar areas. - TripleA
	// Two-ring gradient: bright-ish centre, plateau-ish "shoulder" at 80%
	// of range, then fast falloff to transparent at the actual edge. Single
	// linear gradient looked like the disc only filled ~70% of true range
	// because the eye stops perceiving green below ~0.05 alpha; the shoulder
	// keeps alpha visible most of the way to the edge then drops sharply
	// for a soft boundary. - TripleA
	const FLinearColor CentreColor  (0.20f, 0.85f, 0.30f, 0.10f);
	const FLinearColor ShoulderColor(0.20f, 0.85f, 0.30f, 0.08f);
	const FLinearColor EdgeColor    (0.20f, 0.85f, 0.30f, 0.00f);
	constexpr int32 Segments = 48;
	constexpr float  ShoulderRadiusFrac = 0.80f;

	FSlateResourceHandle WhiteHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*WhiteBrush);

	for (TActorIterator<AClearanceRadarSite> It(World); It; ++It)
	{
		AClearanceRadarSite* Site = *It;
		if (!Site || !Site->Radar || !Site->Radar->IsEnabled()) { continue; }

		const FVector Rel = Site->GetActorLocation() - ControllerOrigin;
		const FVector2D PosNm = FVector2D(Rel.X, Rel.Y) / UnitsPerNm;
		const FVector2D PosPx = ScopeNmToPixel(PosNm, ScopeCentre, ScopePixelRadius);
		const float RangePx = Site->RangeNm * PixelsPerNm;
		if (RangePx <= 1.f) { continue; }

		// Vertices passed in widget-local pixel space - the engine's
		// FSlateVertex::Make factory bakes in the accumulated render
		// transform AND initialises all the per-vertex fields (clip rect,
		// local size, material UVs) that hand-rolling forgets, which is
		// what was producing the fullscreen-green-quad rendering bug. - TripleA
		const FSlateRenderTransform RenderXf = Context.AllottedGeometry.GetAccumulatedRenderTransform();
		const FVector2f LocalSize(static_cast<float>(Context.AllottedGeometry.GetLocalSize().X),
		                          static_cast<float>(Context.AllottedGeometry.GetLocalSize().Y));

		// Vertex layout: [0] = centre, [1..Segments] = shoulder ring at
		// ShoulderRadiusFrac, [Segments+1..2*Segments] = edge ring at full range.
		const int32 ShoulderBase = 1;
		const int32 EdgeBase     = 1 + Segments;
		TArray<FSlateVertex> Verts;
		Verts.SetNum(1 + 2 * Segments);

		auto MakeFanVert = [&RenderXf, &LocalSize](FVector2D LocalPos, const FLinearColor& Col)
		{
			const FVector2f Pos(static_cast<float>(LocalPos.X), static_cast<float>(LocalPos.Y));
			return FSlateVertex::Make<ESlateVertexRounding::Disabled>(
				RenderXf,
				Pos,
				FVector2f(0.5f, 0.5f),
				FVector2f(0.f, 0.f),
				Col.ToFColor(true));
		};

		const float ShoulderPx = RangePx * ShoulderRadiusFrac;
		Verts[0] = MakeFanVert(PosPx, CentreColor);
		for (int32 i = 0; i < Segments; ++i)
		{
			const float A = (2.f * PI * static_cast<float>(i)) / static_cast<float>(Segments);
			const float Cos = FMath::Cos(A), Sin = FMath::Sin(A);
			Verts[ShoulderBase + i] = MakeFanVert(
				FVector2D(PosPx.X + Cos * ShoulderPx, PosPx.Y + Sin * ShoulderPx),
				ShoulderColor);
			Verts[EdgeBase + i] = MakeFanVert(
				FVector2D(PosPx.X + Cos * RangePx, PosPx.Y + Sin * RangePx),
				EdgeColor);
		}

		// Inner fan: centre -> shoulder triangles. Outer ring: shoulder ->
		// edge quads (2 triangles per segment). - TripleA
		TArray<SlateIndex> Indices;
		Indices.Reserve(Segments * 9);
		for (int32 i = 0; i < Segments; ++i)
		{
			const int32 Next = (i + 1) % Segments;
			// Centre triangle
			Indices.Add(0);
			Indices.Add(static_cast<SlateIndex>(ShoulderBase + i));
			Indices.Add(static_cast<SlateIndex>(ShoulderBase + Next));
			// Shoulder -> edge quad (two triangles)
			Indices.Add(static_cast<SlateIndex>(ShoulderBase + i));
			Indices.Add(static_cast<SlateIndex>(EdgeBase + i));
			Indices.Add(static_cast<SlateIndex>(EdgeBase + Next));
			Indices.Add(static_cast<SlateIndex>(ShoulderBase + i));
			Indices.Add(static_cast<SlateIndex>(EdgeBase + Next));
			Indices.Add(static_cast<SlateIndex>(ShoulderBase + Next));
		}

		FSlateDrawElement::MakeCustomVerts(
			Context.OutDrawElements,
			Context.LayerId,
			WhiteHandle,
			Verts,
			Indices,
			nullptr, 0, 0,
			ESlateDrawEffect::None);
	}
}

TArray<int32> UClearanceInstructorPanel::GetRadarCoverageGrid(int32 Resolution, float SectorRadiusNm) const
{
	TArray<int32> Out;
	const int32 Res = FMath::Clamp(Resolution, 4, 256);
	const float SectorR = FMath::Max(1.f, SectorRadiusNm);
	Out.SetNumZeroed(Res * Res);

	UWorld* World = GetWorld();
	if (!World || !CachedController) { return Out; }

	const FVector ControllerOrigin = CachedController->GetActorLocation();
	const float UnitsPerNm = FMath::Max(1.f, CachedController->WorldUnitsPerNm);

	// Gather enabled radar sites once. Each entry is (sector-nm position,
	// range in nm). Iterating actors per-cell would be O(N*sites^2). - TripleA
	struct FSitePosRange { FVector2D PosNm; float RangeNmSq; };
	TArray<FSitePosRange> Sites;
	for (TActorIterator<AClearanceRadarSite> It(World); It; ++It)
	{
		AClearanceRadarSite* Site = *It;
		if (!Site || !Site->Radar || !Site->Radar->IsEnabled()) { continue; }
		const FVector Rel = Site->GetActorLocation() - ControllerOrigin;
		FSitePosRange S;
		S.PosNm     = FVector2D(Rel.X, Rel.Y) / UnitsPerNm;
		S.RangeNmSq = Site->RangeNm * Site->RangeNm;
		Sites.Add(S);
	}

	if (Sites.Num() == 0) { return Out; }

	// Sample grid - row 0 is south rim (-Y), row Res-1 is north rim (+Y).
	// col 0 is west rim (-X), col Res-1 is east rim (+X). Matches the truth
	// scope's east+X / north-Y convention so the BP can iterate the grid in
	// the same orientation it paints the scope. - TripleA
	const float Step = (2.f * SectorR) / static_cast<float>(Res);
	for (int32 Row = 0; Row < Res; ++Row)
	{
		const float CellY = -SectorR + (Row + 0.5f) * Step;
		for (int32 Col = 0; Col < Res; ++Col)
		{
			const float CellX = -SectorR + (Col + 0.5f) * Step;
			const FVector2D Cell(CellX, CellY);

			int32 Count = 0;
			for (const FSitePosRange& S : Sites)
			{
				if (FVector2D::DistSquared(Cell, S.PosNm) <= S.RangeNmSq)
				{
					++Count;
				}
			}
			Out[Row * Res + Col] = Count;
		}
	}

	return Out;
}

FVector2D UClearanceInstructorPanel::GetDeclutteredTrackPx(
	const FRadarTrack& Track,
	const TArray<FRadarTrack>& AllTracks,
	FVector2D ScopeCentre, float ScopePixelRadius) const
{
	const FVector2D Natural = ScopeNmToPixel(FVector2D(Track.Position.X, Track.Position.Y),
		ScopeCentre, ScopePixelRadius);

	constexpr float OverlapPx = 12.f;
	constexpr float OverlapSq = OverlapPx * OverlapPx;
	constexpr float NudgePx   = 10.f;

	struct FMember { FName Callsign; FVector2D Px; };
	TArray<FMember> Cluster;
	Cluster.Reserve(4);
	for (const FRadarTrack& Other : AllTracks)
	{
		const FVector2D OtherPx = ScopeNmToPixel(FVector2D(Other.Position.X, Other.Position.Y),
			ScopeCentre, ScopePixelRadius);
		if (FVector2D::DistSquared(OtherPx, Natural) <= OverlapSq)
		{
			Cluster.Add({ Other.TruthCallsign, OtherPx });
		}
	}

	if (Cluster.Num() <= 1) { return Natural; }

	Cluster.Sort([](const FMember& A, const FMember& B)
	{
		return A.Callsign.LexicalLess(B.Callsign);
	});

	int32 MyIdx = 0;
	for (int32 i = 0; i < Cluster.Num(); ++i)
	{
		if (Cluster[i].Callsign == Track.TruthCallsign) { MyIdx = i; break; }
	}

	FVector2D ClusterCentre = FVector2D::ZeroVector;
	for (const FMember& M : Cluster) { ClusterCentre += M.Px; }
	ClusterCentre /= static_cast<float>(Cluster.Num());

	const float AnglePerMember = (2.f * PI) / static_cast<float>(Cluster.Num());
	const float MyAngle = -0.5f * PI + AnglePerMember * static_cast<float>(MyIdx);
	const FVector2D Offset(FMath::Cos(MyAngle) * NudgePx, FMath::Sin(MyAngle) * NudgePx);

	return ClusterCentre + Offset;
}

FVector2D UClearanceInstructorPanel::GetDeclutteredSymbolPx(
	const FInstructorAircraftRow& Row,
	const TArray<FInstructorAircraftRow>& AllRows,
	FVector2D ScopeCentre, float ScopePixelRadius) const
{
	const FVector2D Natural = ScopeNmToPixel(Row.PositionNm, ScopeCentre, ScopePixelRadius);

	// Threshold = ~12px (typical symbol HalfSize is 12, so two symbols whose
	// centres are within 12px overlap visibly). - TripleA
	constexpr float OverlapPx = 12.f;
	constexpr float OverlapSq = OverlapPx * OverlapPx;
	constexpr float NudgePx   = 10.f;

	// Build the cluster: every aircraft (including self) whose natural pixel
	// position is within OverlapPx of mine. Sort by callsign so the per-
	// member index is deterministic - both this aircraft and its neighbours
	// will compute the same index for each member every frame. - TripleA
	struct FMember { FName Callsign; FVector2D Px; };
	TArray<FMember> Cluster;
	Cluster.Reserve(4);
	for (const FInstructorAircraftRow& Other : AllRows)
	{
		const FVector2D OtherPx = ScopeNmToPixel(Other.PositionNm, ScopeCentre, ScopePixelRadius);
		if (FVector2D::DistSquared(OtherPx, Natural) <= OverlapSq)
		{
			Cluster.Add({ Other.Callsign, OtherPx });
		}
	}

	if (Cluster.Num() <= 1) { return Natural; }

	// Deterministic ordering by callsign string. Same callsign list every
	// frame means the same index, means a stable offset (no jitter). - TripleA
	Cluster.Sort([](const FMember& A, const FMember& B)
	{
		return A.Callsign.LexicalLess(B.Callsign);
	});

	int32 MyIdx = 0;
	for (int32 i = 0; i < Cluster.Num(); ++i)
	{
		if (Cluster[i].Callsign == Row.Callsign) { MyIdx = i; break; }
	}

	// Distribute cluster members around a small circle: first at 12 o'clock
	// (-Y), rest evenly clockwise. Two-member clusters end up vertically
	// stacked, three-member triangular, etc. Cluster centre is the average
	// of the natural positions so symbols spread evenly around their
	// shared visual position. - TripleA
	FVector2D ClusterCentre = FVector2D::ZeroVector;
	for (const FMember& M : Cluster) { ClusterCentre += M.Px; }
	ClusterCentre /= static_cast<float>(Cluster.Num());

	const float AnglePerMember = (2.f * PI) / static_cast<float>(Cluster.Num());
	const float MyAngle = -0.5f * PI + AnglePerMember * static_cast<float>(MyIdx);
	const FVector2D Offset(FMath::Cos(MyAngle) * NudgePx, FMath::Sin(MyAngle) * NudgePx);

	return ClusterCentre + Offset;
}

void UClearanceInstructorPanel::DrawAllAircraftLabels(FPaintContext& Context,
	FVector2D ScopeCentre, float ScopePixelRadius,
	const TArray<FInstructorAircraftRow>& Rows, bool bShowFullDataBlock)
{
	// Candidate label-corner offsets relative to the symbol centre, in priority
	// order. First non-overlapping slot wins. Three tiers: close, mid, far -
	// at high traffic density (clustered aircraft) every close slot collides
	// with a neighbour, so labels fan out to mid/far to find space.
	// Screen Y is inverted (negative Y is upward). - TripleA
	static const FVector2D Candidates[] = {
		// Tier 1 - close (preferred when traffic is sparse)
		FVector2D(  14.f,  -22.f),   // 1 o'clock
		FVector2D( -74.f,  -22.f),   // 11 o'clock
		FVector2D(  14.f,   10.f),   // 5 o'clock
		FVector2D( -74.f,   10.f),   // 7 o'clock
		FVector2D(  22.f,   -6.f),   // 3 o'clock
		FVector2D( -82.f,   -6.f),   // 9 o'clock
		// Tier 2 - mid distance
		FVector2D(  35.f,  -55.f),
		FVector2D( -95.f,  -55.f),
		FVector2D(  35.f,   40.f),
		FVector2D( -95.f,   40.f),
		FVector2D(  55.f,  -10.f),
		FVector2D(-115.f,  -10.f),
		// Tier 3 - far (last resort at high density)
		FVector2D(  70.f,  -90.f),
		FVector2D(-130.f,  -90.f),
		FVector2D(  70.f,   75.f),
		FVector2D(-130.f,   75.f),
		FVector2D(  95.f,  -25.f),
		FVector2D(-155.f,  -25.f),
	};

	TArray<FBox2D> Placed;
	Placed.Reserve(Rows.Num());

	for (const FInstructorAircraftRow& Row : Rows)
	{
		// Use the decluttered position so leader sources track the nudged
		// symbol position when two aircraft cluster - keeps label <-> symbol
		// visual pairing intact even when traffic stacks. - TripleA
		const FVector2D SymbolPx = GetDeclutteredSymbolPx(Row, Rows, ScopeCentre, ScopePixelRadius);
		const TArray<FString> Lines = BuildLabelLines(Row, bShowFullDataBlock);
		const FVector2D LabelSize = EstimateLabelSize(Lines);

		// Find the first candidate slot that doesn't overlap a placed label.
		FVector2D ChosenOffset = Candidates[0];
		for (const FVector2D& Cand : Candidates)
		{
			const FBox2D Box(SymbolPx + Cand, SymbolPx + Cand + LabelSize);
			bool bOverlap = false;
			for (const FBox2D& P : Placed)
			{
				if (Box.Intersect(P)) { bOverlap = true; break; }
			}
			if (!bOverlap) { ChosenOffset = Cand; break; }
		}

		const FVector2D LabelTopLeft = SymbolPx + ChosenOffset;
		Placed.Add(FBox2D(LabelTopLeft, LabelTopLeft + LabelSize));

		const FLinearColor Tint = (Row.CurrentAlertLevel == EAlertLevel::Critical)
			? FLinearColor(1.00f, 0.24f, 0.24f, 1.f)
			: ColourForThreat(Row.ThreatClass);

		// Leader line: thicker tint-matched line from just outside the symbol
		// edge to the MIDPOINT of the closest label edge (not the corner -
		// corner anchors look detached, edge midpoints visually "touch" the
		// text block). Real ATC scopes do this for instant symbol <-> label
		// pairing even in clustered traffic. - TripleA
		const FVector2D LabelCentre = LabelTopLeft + LabelSize * 0.5f;
		const FVector2D Delta = LabelCentre - SymbolPx;
		FVector2D LeaderEnd;
		if (FMath::Abs(Delta.X) > FMath::Abs(Delta.Y))
		{
			// Label is more to the side - anchor on the vertical edge nearest
			// to the symbol, at half the label's height.
			LeaderEnd.X = (Delta.X > 0.f) ? LabelTopLeft.X : (LabelTopLeft.X + LabelSize.X);
			LeaderEnd.Y = LabelTopLeft.Y + LabelSize.Y * 0.5f;
		}
		else
		{
			// Label is more above/below - anchor on the horizontal edge nearest
			// the symbol, at half the label's width.
			LeaderEnd.X = LabelTopLeft.X + LabelSize.X * 0.5f;
			LeaderEnd.Y = (Delta.Y > 0.f) ? LabelTopLeft.Y : (LabelTopLeft.Y + LabelSize.Y);
		}

		const FVector2D ToLabel = (LeaderEnd - SymbolPx).GetSafeNormal();
		const FVector2D LeaderStart = SymbolPx + ToLabel * 13.f;

		FLinearColor LeaderTint = Tint;
		LeaderTint.A = 1.0f;
		PaintLine(Context, LeaderStart, LeaderEnd, LeaderTint, 2.5f);

		// Label text.
		constexpr float LineHeight = 12.f;
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			UWidgetBlueprintLibrary::DrawText(Context, Lines[i],
				LabelTopLeft + FVector2D(0.f, i * LineHeight), Tint);
		}
	}
}

void UClearanceInstructorPanel::DrawOperatorTrackLabels(FPaintContext& Context,
	FVector2D ScopeCentre, float ScopePixelRadius,
	const TArray<FRadarTrack>& Tracks, bool bShowFullDataBlock)
{
	// Same candidate slot table as DrawAllAircraftLabels. Kept inline rather
	// than shared because the two functions are siblings and the slot list
	// might evolve per-mode (operator scope is denser - more tracks per
	// aircraft when chaff is up). - TripleA
	static const FVector2D Candidates[] = {
		FVector2D(  14.f,  -22.f), FVector2D( -74.f,  -22.f),
		FVector2D(  14.f,   10.f), FVector2D( -74.f,   10.f),
		FVector2D(  22.f,   -6.f), FVector2D( -82.f,   -6.f),
		FVector2D(  35.f,  -55.f), FVector2D( -95.f,  -55.f),
		FVector2D(  35.f,   40.f), FVector2D( -95.f,   40.f),
		FVector2D(  55.f,  -10.f), FVector2D(-115.f,  -10.f),
		FVector2D(  70.f,  -90.f), FVector2D(-130.f,  -90.f),
		FVector2D(  70.f,   75.f), FVector2D(-130.f,   75.f),
		FVector2D(  95.f,  -25.f), FVector2D(-155.f,  -25.f),
	};

	AClearanceAirspaceManager* AM = CachedController ? CachedController->GetAirspaceManager() : nullptr;

	TArray<FBox2D> Placed;
	Placed.Reserve(Tracks.Num());

	for (const FRadarTrack& Trk : Tracks)
	{
		// Use the decluttered position so leader sources track the nudged
		// symbol position when ghosts cluster with real tracks (which they do
		// at the moment of chaff drop, before the aircraft moves on). - TripleA
		const FVector2D SymbolPx = GetDeclutteredTrackPx(Trk, Tracks, ScopeCentre, ScopePixelRadius);

		// Build the label content from the radar paint, not from truth.
		// "PRI" when the transponder didn't return - the operator can't
		// see the callsign on a primary-only paint. - TripleA
		TArray<FString> Lines;
		Lines.Add(Trk.bHasSecondary ? Trk.DisplayCallsign.ToString() : FString(TEXT("PRI")));
		const int32 FL = FMath::RoundToInt(Trk.Altitude / 100.f);
		if (bShowFullDataBlock)
		{
			Lines.Add(FString::Printf(TEXT("FL%03d"), FL));
			Lines.Add(FString::Printf(TEXT("%dkt"), FMath::RoundToInt(Trk.Speed)));
			Lines.Add(FString::Printf(TEXT("%03d"), FMath::RoundToInt(Trk.Heading)));
		}
		else
		{
			// Minimum block on operator scope: track callsign + "FL### HDG"
			// using the radar's estimated heading (not truth). Same parallax-
			// correction reasoning as the truth-scope label. - TripleA
			const int32 Hdg = FMath::RoundToInt(Trk.Heading);
			Lines.Add(FString::Printf(TEXT("FL%03d %03d"), FL, Hdg));
		}

		const FVector2D LabelSize = EstimateLabelSize(Lines);

		FVector2D ChosenOffset = Candidates[0];
		for (const FVector2D& Cand : Candidates)
		{
			const FBox2D Box(SymbolPx + Cand, SymbolPx + Cand + LabelSize);
			bool bOverlap = false;
			for (const FBox2D& P : Placed)
			{
				if (Box.Intersect(P)) { bOverlap = true; break; }
			}
			if (!bOverlap) { ChosenOffset = Cand; break; }
		}

		const FVector2D LabelTopLeft = SymbolPx + ChosenOffset;
		Placed.Add(FBox2D(LabelTopLeft, LabelTopLeft + LabelSize));

		// Affiliation tint from truth state, since the radar track doesn't
		// carry threat class. If the aircraft has been deregistered (crash,
		// exit) the lookup returns a default-constructed state -> Unknown
		// tint, which reads as "lost contact" in the operator scope. - TripleA
		EThreatClass Threat = EThreatClass::Unknown;
		if (AM)
		{
			const FAircraftState S = AM->GetAircraftState(Trk.TruthCallsign);
			if (S.bIsValid) { Threat = S.ThreatClass; }
		}

		FLinearColor Tint = ColourForThreat(Threat);
		const float A = FMath::Clamp(Trk.Confidence, 0.f, 1.f);
		Tint.A *= A;

		// Leader line to the closest corner of the label box.
		const FVector2D LabelCentre = LabelTopLeft + LabelSize * 0.5f;
		FVector2D LeaderEnd = LabelTopLeft;
		if (LabelCentre.X > SymbolPx.X) { LeaderEnd.X = LabelTopLeft.X; }
		else                             { LeaderEnd.X = LabelTopLeft.X + LabelSize.X; }
		if (LabelCentre.Y > SymbolPx.Y) { LeaderEnd.Y = LabelTopLeft.Y; }
		else                             { LeaderEnd.Y = LabelTopLeft.Y + LabelSize.Y; }

		const FVector2D ToLabel = (LeaderEnd - SymbolPx).GetSafeNormal();
		const FVector2D LeaderStart = SymbolPx + ToLabel * 13.f;

		FLinearColor LeaderTint = Tint;
		LeaderTint.A = FMath::Min(0.95f, LeaderTint.A + 0.10f); // leaders slightly more visible than label text
		PaintLine(Context, LeaderStart, LeaderEnd, LeaderTint, 2.0f);

		constexpr float LineHeight = 12.f;
		for (int32 i = 0; i < Lines.Num(); ++i)
		{
			UWidgetBlueprintLibrary::DrawText(Context, Lines[i],
				LabelTopLeft + FVector2D(0.f, i * LineHeight), Tint);
		}
	}
}

// --- Accessors for scope overlays -----------------------------------------

TArray<FInstructorZoneMarker> UClearanceInstructorPanel::GetZoneMarkers() const
{
	TArray<FInstructorZoneMarker> Out;
	if (!CachedController) { return Out; }

	const FVector Origin = CachedController->GetActorLocation();
	const float UnitsPerNm = FMath::Max(1.f, CachedController->WorldUnitsPerNm);

	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AClearanceViolationZone> It(W); It; ++It)
		{
			AClearanceViolationZone* Z = *It;
			if (!Z) { continue; }
			FInstructorZoneMarker M;
			M.Name = Z->ZoneName;
			const FVector RelWorld = Z->GetActorLocation() - Origin;
			M.PositionNm = FVector2D(RelWorld.X, RelWorld.Y) / UnitsPerNm;
			M.RadiusNm = Z->RadiusNm;
			M.bIsProtected = true;
			Out.Add(M);
		}
		for (TActorIterator<AClearanceRestrictedArea> It(W); It; ++It)
		{
			AClearanceRestrictedArea* R = *It;
			if (!R) { continue; }
			FInstructorZoneMarker M;
			M.Name = R->AreaName;
			const FVector RelWorld = R->GetActorLocation() - Origin;
			M.PositionNm = FVector2D(RelWorld.X, RelWorld.Y) / UnitsPerNm;
			M.RadiusNm = R->RadiusNm;
			M.bIsProtected = false;
			Out.Add(M);
		}
	}
	return Out;
}

TArray<FRunwayInfo> UClearanceInstructorPanel::GetRunwayMarkers() const
{
	if (!CachedController || !CachedController->GetAirspaceManager())
	{
		return TArray<FRunwayInfo>();
	}
	return CachedController->GetAirspaceManager()->GetAllRunways();
}

TArray<FInstructorChaffMarker> UClearanceInstructorPanel::GetActiveChaffMarkers() const
{
	TArray<FInstructorChaffMarker> Out;
	if (!CachedController || !CachedController->GetAirspaceManager()) { return Out; }

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	for (const FChaffCloud& C : CachedController->GetAirspaceManager()->GetActiveChaffClouds())
	{
		FInstructorChaffMarker M;
		M.PositionNm = FVector2D(C.PositionNm.X, C.PositionNm.Y);
		M.AltitudeFt = C.AltitudeFt;
		const float Age = Now - C.DropSessionTime;
		M.AgeFrac = FMath::Clamp(Age / FMath::Max(0.1f, C.LifetimeSec), 0.f, 1.f);
		Out.Add(M);
	}
	return Out;
}

TArray<FInstructorWaypointMarker> UClearanceInstructorPanel::GetWaypointMarkers() const
{
	TArray<FInstructorWaypointMarker> Out;
	if (!CachedController) { return Out; }
	const FVector Origin = CachedController->GetActorLocation();
	const float UnitsPerNm = FMath::Max(1.f, CachedController->WorldUnitsPerNm);
	if (UWorld* W = GetWorld())
	{
		for (TActorIterator<AClearanceWaypoint> It(W); It; ++It)
		{
			AClearanceWaypoint* WP = *It;
			if (!WP) { continue; }
			FInstructorWaypointMarker M;
			M.Name = WP->Name;
			const FVector Rel = WP->GetActorLocation() - Origin;
			M.PositionNm = FVector2D(Rel.X, Rel.Y) / UnitsPerNm;
			Out.Add(M);
		}
	}
	return Out;
}

TArray<FInstructorAirwaySegment> UClearanceInstructorPanel::GetAirwaySegments() const
{
	TArray<FInstructorAirwaySegment> Out;
	if (!CachedController) { return Out; }
	const FVector Origin = CachedController->GetActorLocation();
	const float UnitsPerNm = FMath::Max(1.f, CachedController->WorldUnitsPerNm);

	if (UWorld* W = GetWorld())
	{
		// First pass: cache every waypoint's nm position by name. - TripleA
		TMap<FName, FVector2D> ByName;
		TArray<AClearanceWaypoint*> All;
		for (TActorIterator<AClearanceWaypoint> It(W); It; ++It)
		{
			AClearanceWaypoint* WP = *It;
			if (!WP) { continue; }
			const FVector Rel = WP->GetActorLocation() - Origin;
			ByName.Add(WP->Name, FVector2D(Rel.X, Rel.Y) / UnitsPerNm);
			All.Add(WP);
		}

		// Second pass: emit each connection once. Dedupe by sorted pair key. - TripleA
		TSet<FString> Seen;
		for (AClearanceWaypoint* WP : All)
		{
			if (!WP) { continue; }
			for (const FName& Other : WP->ConnectedWaypoints)
			{
				if (Other == WP->Name) { continue; }
				const FVector2D* OtherPos = ByName.Find(Other);
				if (!OtherPos) { continue; }
				const FString A = WP->Name.ToString();
				const FString B = Other.ToString();
				const FString Key = (A < B) ? (A + TEXT("|") + B) : (B + TEXT("|") + A);
				if (Seen.Contains(Key)) { continue; }
				Seen.Add(Key);
				FInstructorAirwaySegment Seg;
				Seg.StartNm = ByName[WP->Name];
				Seg.EndNm   = *OtherPos;
				Out.Add(Seg);
			}
		}
	}
	return Out;
}

// --- Combo-box options + string<->enum helpers ----------------------------

TArray<FString> UClearanceInstructorPanel::GetEmergencyTypeOptions()
{
	return { TEXT("Mayday (7700)"), TEXT("Comms Failure (7600)"), TEXT("Hijack (7500)"), TEXT("Fuel Emergency") };
}

TArray<FString> UClearanceInstructorPanel::GetThreatClassOptions()
{
	return { TEXT("Friendly"), TEXT("Hostile"), TEXT("Unknown"), TEXT("Neutral") };
}

TArray<FString> UClearanceInstructorPanel::GetScenarioOptions()
{
	return {
		TEXT("baltic_intercept"),
		TEXT("hijack_response"),
		TEXT("mass_divert"),
		TEXT("mayday_engine_fire"),
		TEXT("nordo_inbound"),
		TEXT("cold_war_probe"),
		TEXT("mixed_ops")
	};
}

EEmergencyType UClearanceInstructorPanel::StringToEmergencyType(const FString& Label)
{
	if (Label.StartsWith(TEXT("Mayday")))   { return EEmergencyType::GeneralMayday; }
	if (Label.StartsWith(TEXT("Comms")))    { return EEmergencyType::CommsFailure; }
	if (Label.StartsWith(TEXT("Hijack")))   { return EEmergencyType::Hijack; }
	if (Label.StartsWith(TEXT("Fuel")))     { return EEmergencyType::FuelLow; }
	return EEmergencyType::None;
}

EThreatClass UClearanceInstructorPanel::StringToThreatClass(const FString& Label)
{
	if (Label.StartsWith(TEXT("Friendly"))) { return EThreatClass::Friendly; }
	if (Label.StartsWith(TEXT("Hostile")))  { return EThreatClass::Hostile; }
	if (Label.StartsWith(TEXT("Neutral")))  { return EThreatClass::Neutral; }
	return EThreatClass::Unknown;
}

FString UClearanceInstructorPanel::EmergencyTypeToString(EEmergencyType Kind)
{
	switch (Kind)
	{
	case EEmergencyType::GeneralMayday: return TEXT("Mayday (7700)");
	case EEmergencyType::CommsFailure:  return TEXT("Comms Failure (7600)");
	case EEmergencyType::Hijack:        return TEXT("Hijack (7500)");
	case EEmergencyType::FuelLow:       return TEXT("Fuel Emergency");
	default:                            return TEXT("None");
	}
}

FString UClearanceInstructorPanel::ThreatClassToString(EThreatClass Class)
{
	switch (Class)
	{
	case EThreatClass::Friendly: return TEXT("Friendly");
	case EThreatClass::Hostile:  return TEXT("Hostile");
	case EThreatClass::Neutral:  return TEXT("Neutral");
	default:                     return TEXT("Unknown");
	}
}

// --- Change detection (cheap, content-only comparisons) -------------------

bool UClearanceInstructorPanel::ScoreChanged(const FInstructorScoreView& A, const FInstructorScoreView& B)
{
	return A.Total != B.Total
		|| A.EfficiencyPct != B.EfficiencyPct
		|| A.Landings != B.Landings || A.Handoffs != B.Handoffs
		|| A.ResolvedConflicts != B.ResolvedConflicts || A.Intercepts != B.Intercepts
		|| A.Emergencies != B.Emergencies || A.GoArounds != B.GoArounds
		|| A.SepLoss != B.SepLoss || A.WakeBusts != B.WakeBusts
		|| A.TCAS != B.TCAS || A.Strayed != B.Strayed || A.MisID != B.MisID
		|| A.Violated != B.Violated || A.Crashed != B.Crashed || A.Busted != B.Busted
		|| !FMath::IsNearlyEqual(A.NextSpawnSec, B.NextSpawnSec, 0.25f);
}

bool UClearanceInstructorPanel::ScenarioChanged(const FInstructorScenarioView& A, const FInstructorScenarioView& B)
{
	return A.bRunning != B.bRunning
		|| A.Name != B.Name
		|| !FMath::IsNearlyEqual(A.ElapsedSec, B.ElapsedSec, 0.25f)
		|| A.FiredEvents != B.FiredEvents || A.TotalEvents != B.TotalEvents
		|| A.FiredTriggers != B.FiredTriggers || A.TotalTriggers != B.TotalTriggers;
}

bool UClearanceInstructorPanel::AircraftListChanged(const TArray<FInstructorAircraftRow>& A, const TArray<FInstructorAircraftRow>& B)
{
	if (A.Num() != B.Num()) { return true; }
	for (int32 i = 0; i < A.Num(); ++i)
	{
		const FInstructorAircraftRow& X = A[i];
		const FInstructorAircraftRow& Y = B[i];
		if (X.Callsign != Y.Callsign) { return true; }
		if (X.ThreatClass != Y.ThreatClass) { return true; }
		if (X.FlightPhase != Y.FlightPhase) { return true; }
		if (X.ActiveEmergency != Y.ActiveEmergency) { return true; }
		if (X.CurrentAlertLevel != Y.CurrentAlertLevel) { return true; }
		if (X.bJammingOn != Y.bJammingOn) { return true; }
		if (X.bUnderGCIControl != Y.bUnderGCIControl) { return true; }
		if (!FMath::IsNearlyEqual(X.Heading, Y.Heading, 1.f)) { return true; }
		if (!FMath::IsNearlyEqual(X.Altitude, Y.Altitude, 50.f)) { return true; }
		if (!FMath::IsNearlyEqual(X.Speed, Y.Speed, 5.f)) { return true; }
		if (!FVector2D(X.PositionNm - Y.PositionNm).IsNearlyZero(0.1f)) { return true; }
	}
	return false;
}

bool UClearanceInstructorPanel::NotificationsChanged(const TArray<FClearanceNotification>& A, const TArray<FClearanceNotification>& B)
{
	if (A.Num() != B.Num()) { return true; }
	for (int32 i = 0; i < A.Num(); ++i)
	{
		if (A[i].Text != B[i].Text) { return true; }
		if (!FMath::IsNearlyEqual(A[i].ServerTimeAdded, B[i].ServerTimeAdded, 0.05f)) { return true; }
	}
	return false;
}
