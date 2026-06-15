#include "UI/ClearanceInstructorPanel.h"
#include "Simulation/ClearanceOperatorPC.h"
#include "Airspace/ClearanceAirspaceManager.h"
#include "Airspace/ClearanceViolationZone.h"
#include "Airspace/ClearanceRestrictedArea.h"
#include "Airspace/ClearanceWaypoint.h"
#include "Scenario/ClearanceScenarioRunner.h"
#include "Components/ScrollBox.h"
#include "Components/Image.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
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

	return Result;
}

void UClearanceInstructorPanel::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

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
	Out.Departures         = CachedController->RepScoreDepartures;
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

// --- Inject forwarders ----------------------------------------------------

void UClearanceInstructorPanel::InjectEmergency(FName Callsign, EEmergencyType Kind)
{
	if (CachedOperatorPC) { CachedOperatorPC->Server_InjectEmergency(Callsign, Kind); }
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

	// In-place update when the same callsign set is on screen in the same order.
	// Rebuilding every refresh tears down + recreates the widget tree, which
	// stutters visibly when state ticks at 5Hz. - TripleA
	bool bSameSet = (CurrentRowCallsigns.Num() == Rows.Num())
		&& (ScrollBox->GetChildrenCount() == Rows.Num());
	if (bSameSet)
	{
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			if (CurrentRowCallsigns[i] != Rows[i].Callsign) { bSameSet = false; break; }
		}
	}

	if (bSameSet)
	{
		for (int32 i = 0; i < Rows.Num(); ++i)
		{
			UUserWidget* RowWidget = Cast<UUserWidget>(ScrollBox->GetChildAt(i));
			PushRowData(RowWidget, Rows[i]);
			PushRowSelected(RowWidget, Rows[i].Callsign == SelectedCallsign && SelectedCallsign != NAME_None);
		}
		return;
	}

	// Aircraft entered or left - full rebuild. Only happens on actual list
	// composition change, not on per-aircraft state ticks. - TripleA
	ScrollBox->ClearChildren();
	CurrentRowCallsigns.Reset();
	CurrentRowCallsigns.Reserve(Rows.Num());

	for (const FInstructorAircraftRow& Row : Rows)
	{
		UUserWidget* RowWidget = CreateWidget<UUserWidget>(this, AircraftRowClass);
		if (!RowWidget) { continue; }

		PushRowData(RowWidget, Row);
		PushRowSelected(RowWidget, Row.Callsign == SelectedCallsign && SelectedCallsign != NAME_None);

		ScrollBox->AddChild(RowWidget);
		CurrentRowCallsigns.Add(Row.Callsign);
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
	bool bIsMilitary, float HeadingDeg, EAlertLevel Alert, float HalfSizePx)
{
	const FLinearColor Frame = (Alert == EAlertLevel::Critical) ? ColourForAlert(Alert) : ColourForThreat(Threat);
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

	// Bearing vector. Screen Y inverted: north (0deg) = -Y.
	const float Rad = FMath::DegreesToRadians(HeadingDeg);
	const FVector2D BearingTip = Centre + FVector2D(FMath::Sin(Rad), -FMath::Cos(Rad)) * H * 1.6f;
	Add(Centre, BearingTip);

	PaintPolyline(Context, Frags, Frame);

	if (Alert != EAlertLevel::None)
	{
		PaintCircle(Context, Centre, H * 1.4f, 24, ColourForAlert(Alert));
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

	const FLinearColor Border  = FLinearColor(0.20f, 0.24f, 0.31f, 0.85f);
	const FLinearColor Faint   = FLinearColor(0.20f, 0.24f, 0.31f, 0.45f);
	const FLinearColor Compass = FLinearColor(0.31f, 0.78f, 1.00f, 0.55f);

	// Three range rings (25 / 50 / 75% of outer) + outer boundary.
	PaintCircle(Context, ScopeCentre, Outer * 0.25f, 32, Faint);
	PaintCircle(Context, ScopeCentre, Outer * 0.50f, 48, Faint);
	PaintCircle(Context, ScopeCentre, Outer * 0.75f, 56, Faint);
	PaintCircle(Context, ScopeCentre, Outer,          64, Border);

	// Compass tick crosses through centre, slightly past the outer ring.
	PaintLine(Context, ScopeCentre + FVector2D(0, -Outer - 6.f), ScopeCentre + FVector2D(0, -Outer + 2.f), Compass);
	PaintLine(Context, ScopeCentre + FVector2D(Outer + 6.f, 0),  ScopeCentre + FVector2D(Outer - 2.f, 0),  Compass);
	PaintLine(Context, ScopeCentre + FVector2D(0, Outer + 6.f),  ScopeCentre + FVector2D(0, Outer - 2.f),  Compass);
	PaintLine(Context, ScopeCentre + FVector2D(-Outer - 6.f, 0), ScopeCentre + FVector2D(-Outer + 2.f, 0), Compass);
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

	// 12px-long line oriented along heading + a perpendicular tick to mark
	// the threshold edge.
	const FLinearColor Tint(0.92f, 0.92f, 0.92f, 0.75f);
	PaintLine(Context, Threshold, Threshold + Along * 12.f, Tint, 1.5f);
	PaintLine(Context, Threshold - Cross * 4.f, Threshold + Cross * 4.f, Tint, 1.5f);
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
			Lines.Add(FString::Printf(TEXT("FL%03d"), FL));
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
		const FVector2D SymbolPx = ScopeNmToPixel(Row.PositionNm, ScopeCentre, ScopePixelRadius);
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

		// Leader line: thin tint-matched line from just outside the symbol edge
		// to the closest corner of the label box. Drawn at full alpha + 2px
		// thickness so it reads clearly against the dark scope background. - TripleA
		const FVector2D LabelCentre = LabelTopLeft + LabelSize * 0.5f;
		FVector2D LeaderEnd = LabelTopLeft;
		if (LabelCentre.X > SymbolPx.X)  { LeaderEnd.X = LabelTopLeft.X; }
		else                              { LeaderEnd.X = LabelTopLeft.X + LabelSize.X; }
		if (LabelCentre.Y > SymbolPx.Y)  { LeaderEnd.Y = LabelTopLeft.Y; }
		else                              { LeaderEnd.Y = LabelTopLeft.Y + LabelSize.Y; }

		const FVector2D ToLabel = (LeaderEnd - SymbolPx).GetSafeNormal();
		const FVector2D LeaderStart = SymbolPx + ToLabel * 13.f;

		FLinearColor LeaderTint = Tint;
		LeaderTint.A = 0.95f;
		PaintLine(Context, LeaderStart, LeaderEnd, LeaderTint, 2.0f);

		// Label text.
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
		|| A.Landings != B.Landings || A.Departures != B.Departures
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
