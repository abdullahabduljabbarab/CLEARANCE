// Base UUserWidget for the instructor station panel. The visual layout lives in
// a Blueprint subclass; this class owns the runtime contract: finds the local
// sim controller + operator PC, polls replicated state, fires BP events on
// change, and exposes BlueprintCallable wrappers for every Server_Inject*
// RPC. - TripleA
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/CLEARANCETypes.h"
#include "UI/ClearanceInstructorTypes.h"
#include "Simulation/ClearanceSimulationController.h"  // for FClearanceNotification
#include "ClearanceInstructorPanel.generated.h"

class AClearanceOperatorPC;
class AClearanceSimulationController;
class UCanvas;
class UTextureRenderTarget2D;

UCLASS(Blueprintable, Abstract)
class CLEARANCESIM_API UClearanceInstructorPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	UClearanceInstructorPanel(const FObjectInitializer& ObjectInitializer);

	// --- Lifecycle ---------------------------------------------------------

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	                          const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	                          int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	// Mouse handling for Overview drag + zoom. Gated to camera-feed mode +
	// Overview view inside the implementations; outside that they fall back
	// to Super so the rest of the panel still gets normal input. - TripleA
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

	// Override in BP to draw the scope. Runs every paint pass with the panel's
	// FPaintContext + pixel size. Call DrawScopeBoundary / DrawAffiliationSymbol
	// / DrawScopeChaffCloud from inside this. - TripleA
	UFUNCTION(BlueprintImplementableEvent, Category = "Instructor|Scope")
	void BP_PaintScope(UPARAM(ref) struct FPaintContext& Context, FVector2D PanelSize);

	// Override in BP to draw HUD overlays on top of the camera feed (runway
	// centerlines, approach corridors, etc.). Fires every paint pass when the
	// panel is in camera-feed mode. Call DrawCameraOverlayLines() from inside
	// this with Img_CameraFeed as the second arg. - TripleA
	UFUNCTION(BlueprintImplementableEvent, Category = "Instructor|Camera")
	void BP_PaintCameraOverlay(UPARAM(ref) struct FPaintContext& Context, FVector2D PanelSize);

	// Paint helper that draws all entries from GetCameraOverlayLines() into
	// the given PaintContext, using the camera-feed image widget's geometry
	// to translate the 0..1 UVs into panel-local pixel coords. Hand it the
	// same FPaintContext you got in BP_PaintCameraOverlay and the
	// Img_CameraFeed widget reference. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Camera")
	void DrawCameraOverlayLines(UPARAM(ref) struct FPaintContext& Context, class UImage* CameraImage);

	// --- Read views (call from BP to refresh on demand) -------------------
	// Most of the time you'll just bind the OnXxxChanged events below;
	// these are here for manual pulls (e.g. populating a combo box on open). - TripleA

	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	TArray<FInstructorAircraftRow> GetAircraftRows() const;

	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	FInstructorScoreView GetScoreView() const;

	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	FInstructorScenarioView GetScenarioView() const;

	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	TArray<FClearanceNotification> GetRecentNotifications() const;

	// DIS federation status snapshot for the instructor UI. Live flags + last
	// packet counts so the BP can bind to a status chip / activity light
	// without dipping directly into the DIS emitter/receiver internals. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DIS")
	bool IsDISEmitting() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DIS")
	bool IsDISReceiving() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DIS")
	int32 GetDISPacketsSent() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DIS")
	int32 GetDISPacketsReceived() const;

	// Per-second emit/receive rates for the federation panel activity indicators.
	// Sampled from the emitter/receiver cumulative counters once per second and
	// stored as an int rate - underlying counters reset to zero on Stop so the
	// sampler clamps negative deltas to 0. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DIS")
	int32 GetDISEmitRatePerSec() const { return DISEmitRatePerSec; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DIS")
	int32 GetDISRecvRatePerSec() const { return DISRecvRatePerSec; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DDS")
	bool IsDDSEmitting() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DDS")
	bool IsDDSReceiving() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DDS")
	int32 GetDDSPacketsSent() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DDS")
	int32 GetDDSPacketsReceived() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DDS")
	int32 GetDDSEmitRatePerSec() const { return DDSEmitRatePerSec; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|DDS")
	int32 GetDDSRecvRatePerSec() const { return DDSRecvRatePerSec; }

	// RTI Connext DDS federation status - same shape as the DDS getters
	// above, but reads from the RTI Connext emitter mirror. Publish-only
	// for now (no RTI receiver in the current build), so no Recv variant. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|RTI")
	bool IsRTIEmitting() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|RTI")
	int32 GetRTIPacketsSent() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|RTI")
	int32 GetRTIEmitRatePerSec() const { return RTIEmitRatePerSec; }

	// HLA federate status - IEEE 1516-2010 fourth wire. Publish-only for
	// MVP; RECV variant added when the ambassador starts subscribing to
	// peer object updates. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|HLA")
	bool IsHLAJoined() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|HLA")
	int32 GetHLAUpdatesSent() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|HLA")
	int32 GetHLAUpdateRatePerSec() const { return HLAUpdateRatePerSec; }

	// In-station operator's manual - one entry per section. Content is
	// hardcoded server-side so any packaged build carries the manual with it
	// (no data-only content dependency). BP renders each section's Title in
	// the TOC and Body in the content pane. Simple inline markup: **bold**,
	// `code`, [ACCENT]. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|Manual")
	TArray<FManualSection> GetManualSections() const;

	// Event log populator - append-only aware. Reuses existing WBP_EventLogRow
	// widget instances keyed on ServerTimeAdded so new entries append without
	// destroying + recreating the whole list (which flashes visibly at 5Hz).
	// Full reflow only when the server ring-buffer trims from the front.
	// Pass ScrollBox_EventLog + WBP_EventLogRow class + the array from
	// GetRecentNotifications(). - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	void PopulateEventLogScrollBox(class UScrollBox* ScrollBox,
		TSubclassOf<UUserWidget> RowClass,
		const TArray<FClearanceNotification>& Entries);

	// Restricted (civilian-must-avoid) + protected (hostile-must-not-reach)
	// airspace zones in the level, converted to sector-relative nm. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	TArray<FInstructorZoneMarker> GetZoneMarkers() const;

	// All runway thresholds the airspace manager knows about. Each runway
	// strip contributes both ends (so a 09/27 strip = two markers). - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	TArray<FRunwayInfo> GetRunwayMarkers() const;

	// Active (not-yet-expired) chaff clouds with their age fraction so the
	// scope can fade them out as they age. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	TArray<FInstructorChaffMarker> GetActiveChaffMarkers() const;

	// Named waypoints / fixes in the level (AClearanceWaypoint actors),
	// converted to sector-relative nm. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	TArray<FInstructorWaypointMarker> GetWaypointMarkers() const;

	// Airway segments between connected waypoints. De-duped: a connection
	// listed on both ends only appears once. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	TArray<FInstructorAirwaySegment> GetAirwaySegments() const;

	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	FName GetSelectedCallsign() const { return SelectedCallsign; }

	// --- Picture-in-picture camera feed -----------------------------------
	// A live 3D feed of the active preset camera (Tower / Approach / Overview /
	// Follow). Sits in the scope area, swapped in via ToggleScopeCameraView so
	// the same canvas region is used for either the truth scope or the camera
	// feed. Cycling between camera modes here doesn't disturb the operator's
	// main viewpoint - independent SceneCapture, independent state. - TripleA

	UFUNCTION(BlueprintCallable, Category = "Instructor|Camera")
	void ToggleScopeCameraView();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|Camera")
	bool IsScopeCameraViewActive() const { return bShowCameraView; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|Camera")
	UTextureRenderTarget2D* GetInstructorPipRT() const;

	UFUNCTION(BlueprintCallable, Category = "Instructor|Camera")
	void SetInstructorPipView(EClearanceCameraView View);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Camera")
	void CycleInstructorPipView();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|Camera")
	EClearanceCameraView GetInstructorPipView() const;

	// Bind the camera feed Image widget's brush to the current PIP render
	// target. Call this from the toggle handler instead of Event Construct -
	// the RT is allocated by the controller's BeginPlay, which may fire
	// after the widget's Construct, so a Construct-time bind would catch a
	// null RT and the image would stay blank forever. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|Camera")
	void RebindCameraFeedBrush(class UImage* TargetImage);

	// Pan the Tower view. UMG calls this from its Tick when the user is
	// holding a pan-left / pan-right button - degrees per tick = sweep
	// speed (deg/sec) * world delta-seconds. A 60deg/sec sweep gives a
	// full rotation in 6 seconds. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|Camera")
	void ApplyTowerYawDelta(float DeltaDeg);

	// Approach view runway picker. UMG pops a selector when the instructor
	// clicks APPROACH, populates it from GetApproachRunwayLabels, and calls
	// SetInstructorPipApproachRunway with the chosen index. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|Camera")
	TArray<FString> GetApproachRunwayLabels() const;

	UFUNCTION(BlueprintCallable, Category = "Instructor|Camera")
	void SetInstructorPipApproachRunway(int32 Index);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|Camera")
	int32 GetInstructorPipApproachRunwayIndex() const;

	// Label-dispatched runway picker - safer for UMG than the index variant.
	// Each runway button calls this with a literal string ("RWY 27R" etc.). - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|Camera")
	void PickApproachRunwayByLabel(const FString& Label);

	// Chase sub-angle cycle. Wire these to left/right arrow buttons that show
	// only when GetInstructorPipView() == Follow. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|Camera")
	void CycleChaseAngleNext();

	UFUNCTION(BlueprintCallable, Category = "Instructor|Camera")
	void CycleChaseAnglePrev();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|Camera")
	EClearanceFollowAngle GetChaseAngle() const;

	// Screen-space aircraft labels for the camera-feed HUD overlay. UMG
	// polls this each tick (or as bind delegate) and positions a widget
	// per entry at ScreenUV * ImageSize. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|Camera")
	TArray<FInstructorCameraLabel> GetCameraLabels() const;

	// Projected world-space lines for the camera-feed HUD overlay - runway
	// centerlines, extended approach centerlines. Neo paints each entry
	// between StartUV * ImageSize and EndUV * ImageSize. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|Camera")
	TArray<FInstructorCameraLine> GetCameraOverlayLines() const;

	// Projected text labels for the camera-feed HUD overlay - runway
	// designators at each threshold. Wire alongside DrawCameraOverlayLines
	// in the same paint pass. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Instructor|Camera")
	TArray<FInstructorCameraText> GetCameraOverlayText() const;

	// Paint helper that writes every runway designator into the camera
	// feed at its projected screen position. Uses the same image-paint-
	// space anchor as DrawCameraOverlayLines so lines and text agree on
	// where the runway is. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Camera")
	void DrawCameraOverlayText(UPARAM(ref) struct FPaintContext& Context, class UImage* CameraImage);

	// 1px cyan frame around the camera feed - reads as a real PIP display
	// rather than a raw texture. UMG Border widgets fill their whole area
	// so they can't be used for outline-only; this is the C++ paint path.
	// Call from BP_PaintCameraOverlay with Img_CameraFeed - fires on every
	// camera mode, the frame is a permanent HUD element. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Camera")
	void DrawCameraFeedBorder(UPARAM(ref) struct FPaintContext& Context, class UImage* CameraImage);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	void SetSelectedCallsign(FName NewSelection);

	// --- Change notifications (BindWidget these in your derived BP) ------

	UFUNCTION(BlueprintImplementableEvent, Category = "Instructor|Events")
	void OnAircraftListChanged(const TArray<FInstructorAircraftRow>& Rows);

	UFUNCTION(BlueprintImplementableEvent, Category = "Instructor|Events")
	void OnScoreChanged(const FInstructorScoreView& View);

	UFUNCTION(BlueprintImplementableEvent, Category = "Instructor|Events")
	void OnScenarioStateChanged(const FInstructorScenarioView& View);

	UFUNCTION(BlueprintImplementableEvent, Category = "Instructor|Events")
	void OnNotificationsChanged(const TArray<FClearanceNotification>& Notes);

	// --- Inject wrappers (hook to button OnClick) -------------------------
	// All forward to AClearanceOperatorPC::Server_Inject*. Each is a no-op if
	// the local PC isn't an AClearanceOperatorPC, so the widget is safe to
	// instantiate in dev situations without the proper GameMode wired. - TripleA

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectEmergency(FName Callsign, EEmergencyType Kind, float TimerMinutes = -1.f);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectClearEmergency(FName Callsign);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectClassify(FName Callsign, EThreatClass NewClass);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectScramble(FName BanditCallsign);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectJamming(FName Callsign, bool bOn);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectChaff(FName Callsign);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectSpawn();

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectClearTraffic();

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectSetWind(float DirectionDeg, float SpeedKts);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectLoadScenario(const FString& ScenarioName);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectStopScenario();

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectResetScenario();

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectSetPaused(bool bNewPaused);

	UFUNCTION(BlueprintCallable, Category = "Instructor|Inject")
	void InjectSetTimeScale(float Scale);

	// --- Aircraft list population ----------------------------------------
	// The panel owns the scroll-box rebuild so the row layout can stay 100%
	// Blueprint (one source of truth for visuals) while the data plumbing
	// lives here. - TripleA

	// Class of row widget the helper spawns into the scroll box. Defaulted in
	// the C++ constructor to /Game/UI/WBP_InstructorAircraftRow. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor|List")
	TSubclassOf<UUserWidget> AircraftRowClass;

	// Clear the scroll box and refill it with one row widget per Row. Each
	// spawned row gets its FInstructorAircraftRow via a BP function called
	// "SetRowData" (looked up by name so the row can stay in BP). The
	// currently-selected row also gets "SetSelected(true)" if it exposes
	// that function. - TripleA
	UFUNCTION(BlueprintCallable, Category = "Instructor|List")
	void PopulateAircraftScrollBox(class UScrollBox* ScrollBox, const TArray<FInstructorAircraftRow>& Rows);

	// --- Mini-scope helper ------------------------------------------------
	// Project a sector-relative nm position to a pixel inside the scope area.
	// ScopeCentre is the scope's centre in panel paint coords; ScopePixelRadius
	// is the outer-ring radius in pixels (i.e. the distance from centre to
	// where the ScopeRangeNm boundary should sit). - TripleA

	UFUNCTION(BlueprintCallable, Category = "Instructor|Scope")
	FVector2D ScopeNmToPixel(FVector2D PositionNm, FVector2D ScopeCentre, float ScopePixelRadius) const;

	// Paint a MIL-STD-2525C air-track affiliation symbol on the scope canvas.
	// Shape encodes threat class (friend = rectangle, hostile = diamond,
	// unknown = octagon-as-quatrefoil-stand-in, neutral = square). Bearing
	// vector points from centre along HeadingDeg. Alert level draws a
	// coloured ring around the symbol; bIsMilitary adds a small "+"
	// below. Alpha multiplies every drawn line / ring's alpha channel so
	// the operator-scope mode can fade symbols by FRadarTrack::Confidence -
	// EW-jammed tracks fade toward transparent. Defaults to 1.0 so the
	// existing truth-scope call sites stay untouched. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawAffiliationSymbol(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopePixelCentre,
		EThreatClass Threat,
		bool bIsMilitary,
		float HeadingDeg,
		EAlertLevel Alert,
		float HalfSizePx = 12.f,
		float Alpha = 1.f);

	// Sector outline + 25 / 50 / 75% range rings + N/E/S/W compass ticks.
	// Centred at ScopeCentre with the outer ring at ScopePixelRadius. Call
	// once at the top of the paint event before plotting aircraft. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawScopeBoundary(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopeCentre,
		float ScopePixelRadius);

	// Fading amber ring for an active chaff cloud. AgeFrac = 0 freshly dropped,
	// 1 about to expire - the ring shrinks and tints as it ages. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawScopeChaffCloud(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopePixelCentre,
		float AgeFrac);

	// Restricted / protected zone overlay: dashed circle at the zone's nm
	// position with kind-based colour. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawZoneMarker(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopeCentre,
		float ScopePixelRadius,
		const FInstructorZoneMarker& Zone);

	// Runway threshold: a short line at the runway position oriented along
	// the runway heading. Both ends of a strip show as separate markers. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawRunwayMarker(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopeCentre,
		float ScopePixelRadius,
		const FRunwayInfo& Runway);

	// Highlight ring around the currently-selected aircraft so the instructor
	// can spot which dot they're targeting. Call this in the per-aircraft
	// loop when the row's callsign matches GetSelectedCallsign(). - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawSelectedRing(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopePixelCentre,
		float RadiusPx = 18.f);

	// Named waypoint - small triangle marker + label. Drawn beneath aircraft. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawWaypointMarker(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopeCentre,
		float ScopePixelRadius,
		const FInstructorWaypointMarker& Waypoint);

	// Airway segment - thin line between two waypoint positions. Drawn under
	// everything else so it forms the background grid. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawAirwaySegment(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopeCentre,
		float ScopePixelRadius,
		const FInstructorAirwaySegment& Airway);

	// Range label text at the 25/50/75% rings showing the nm value those
	// rings represent. Call after DrawScopeBoundary so the text reads on top
	// of the ring line. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawRangeLabels(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopeCentre,
		float ScopePixelRadius);

	// Floating ATC-style data block next to the aircraft symbol. Two-line
	// minimum (callsign + flight level); expanded mode adds speed + heading
	// with target indicators. Position is offset to the upper-right of the
	// symbol so it doesn't overlap the bearing vector. Colour tracks threat
	// class, overridden to red by Critical alert. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawAircraftLabel(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopePixelCentre,
		const FInstructorAircraftRow& Row,
		bool bShowFullDataBlock = false);

	// Batch label render with leader lines + auto-avoid. Walks every aircraft
	// in Rows, picks a label position (1 o'clock preferred, falls back through
	// 11 / 5 / 7 o'clock if the preferred slot overlaps a label already placed),
	// draws a thin leader line from the symbol edge to the label, then writes
	// the label text. Call this INSTEAD of per-row DrawAircraftLabel inside
	// the aircraft loop - it replaces the whole label pass. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawAllAircraftLabels(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopeCentre,
		float ScopePixelRadius,
		const TArray<FInstructorAircraftRow>& Rows,
		bool bShowFullDataBlock = false);

	// Decluttered pixel position for one aircraft. Returns the natural
	// ScopeNmToPixel projection unless this aircraft sits within an overlap
	// threshold (~12px) of one or more other aircraft - in which case the
	// returned position is nudged a few px along a deterministic angle so
	// stacked symbols spread out into a visible cluster instead of drawing
	// on top of each other. Real-world ATC scopes (STARS, DSR) call this
	// "Datablock Stagger" or "Symbol Declutter". The BP scope ForEach should
	// use this for both the symbol position AND any leader source so the
	// data block clearly anchors to its specific symbol. Same algorithm is
	// used internally by DrawAllAircraftLabels for its leader sources. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	FVector2D GetDeclutteredSymbolPx(
		const FInstructorAircraftRow& Row,
		const TArray<FInstructorAircraftRow>& AllRows,
		FVector2D ScopeCentre,
		float ScopePixelRadius) const;

	// Operator-scope counterpart for FRadarTrack. Same decluster algorithm,
	// keyed on TruthCallsign so chaff ghost contacts (synthetic GHOST_xxxxx
	// callsigns) separate visibly from real aircraft tracks that happen to
	// paint at the same position - critical because at the moment of a chaff
	// drop the ghost track and the real aircraft share a pixel. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	FVector2D GetDeclutteredTrackPx(
		const FRadarTrack& Track,
		const TArray<FRadarTrack>& AllTracks,
		FVector2D ScopeCentre,
		float ScopePixelRadius) const;

	// Radar coverage analysis grid for the COVERAGE overlay on the truth
	// scope. Samples airspace at Resolution x Resolution points across the
	// sector and returns the number of enabled radars that cover each cell.
	// Row-major: index = row * Resolution + col. Cell at (row, col) maps to
	// sector-relative nm position:
	//   nm_x = ((col + 0.5) / Resolution - 0.5) * 2 * SectorRadiusNm
	//   nm_y = ((row + 0.5) / Resolution - 0.5) * 2 * SectorRadiusNm
	// (col grows east, row grows north - matches scope projection convention)
	//
	// Defence procurement specs call out the ">= 2 sensors" threshold
	// explicitly when evaluating sensor network robustness - this is the
	// metric. Iterates placed AClearanceRadarSite actors only (their RangeNm
	// + actor location are replicated as actor state, so both peers can
	// compute the same grid). BP can call this directly for custom analysis,
	// but for the standard heatmap paint use DrawCoverageGrid below - same
	// data but with the iteration + filled rects done in one C++ call. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	TArray<int32> GetRadarCoverageGrid(int32 Resolution, float SectorRadiusNm) const;

	// Paint the radar coverage heatmap into the scope. Single C++ call,
	// internally calls GetRadarCoverageGrid + iterates the cells + draws
	// each as a filled colored rect. Splice into BP_PaintScope between
	// the environmental layers and the aircraft layer when bShowCoverage
	// is on. Uses 50x50 grid with low-alpha tints (blind = faint red,
	// 1 radar = amber, 2+ = green) so aircraft + zones + airways stay
	// legible on top. Sector radius comes from ScopeRangeNm so the grid
	// matches the visible scope range. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawCoverageGrid(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopeCentre,
		float ScopePixelRadius);

	// Operator-scope counterpart to DrawAllAircraftLabels. Same auto-avoid
	// + leader-line pass but reads radar tracks (estimated alt / spd / hdg
	// + DisplayCallsign, or "PRI" when bHasSecondary is false) instead of
	// truth rows. Each label fades by Track.Confidence so EW-degraded
	// paints look degraded in the data block too. Threat class for the
	// label tint is looked up internally from AirspaceManager->GetAircraftState
	// (TruthCallsign), so a track for a deregistered aircraft falls back
	// to Unknown tint. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawOperatorTrackLabels(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopeCentre,
		float ScopePixelRadius,
		const TArray<FRadarTrack>& Tracks,
		bool bShowFullDataBlock = false);

	// Whether the per-aircraft data blocks should show the full ATC format
	// (callsign + FL + speed + heading) or the minimal two-line version.
	// Toggleable in BP so the instructor can hide-on-clutter / expand-on-detail. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor|Scope")
	bool bShowFullDataBlocks = false;

	// Scope <-> camera-feed swap state. BlueprintReadOnly so UMG can drive
	// widget visibility off it (truth scope visible when false, camera image
	// visible when true). - TripleA
	UPROPERTY(BlueprintReadOnly, Category = "Instructor|Camera")
	bool bShowCameraView = false;

	// Master gate for the scope + camera-overlay paint events. The TRUTH SCOPE
	// and CAMERA VIEW tabs are siblings of the PERFORMANCE tab in the layout,
	// but their paint runs from the panel root's NativePaint - so collapsing
	// child widgets in UMG isn't enough on its own to stop the vectors from
	// drawing through the Performance tab background. Set false when the
	// PERFORMANCE tab is active so neither BP_PaintScope nor BP_PaintCameraOverlay
	// fires; set true for the scope/camera tabs (the existing bShowCameraView
	// flag then decides which of the two paints). - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor|Tabs")
	bool bShowScopeOrCamera = true;

	// --- Tuning -----------------------------------------------------------

	// How many nm radius the mini-scope shows. Default ~80nm matches the
	// default radar range. Surfaceable as a UMG slider, OR overwritten each
	// tick by the auto-fit logic below. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor|Scope")
	float ScopeRangeNm = 80.f;

	// Auto-resize ScopeRangeNm each tick so every aircraft fits on screen.
	// Useful since scenario extents vary wildly (some at 80nm, some at 500nm).
	// Turn off to lock the range manually. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor|Scope")
	bool bAutoFitScopeRange = true;

	// Floor for the auto-fit so the scope doesn't collapse when no aircraft
	// are present. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor|Scope")
	float MinAutoFitRangeNm = 40.f;

	// Multiplier applied to the farthest aircraft's distance to leave a
	// margin between it and the outer ring. 1.15 = 15% breathing room. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor|Scope")
	float AutoFitMarginFactor = 1.15f;

	// Cap on recent notifications surfaced to the event log. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor")
	int32 MaxNotifications = 40;

	// How often the panel polls replicated state and rebuilds the aircraft
	// row list. 0.5s = 2Hz - the scope symbols repaint every frame in C++
	// regardless, this only throttles the widget row diff + score/scenario
	// view refresh, which do not need to run at 72Hz. VR game-thread win
	// with no perceptible UI hit unless a plane spawns mid-tick. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor")
	float RefreshIntervalSec = 0.5f;

	// --- Combo-box option lists + string<->enum helpers -------------------
	// BP combo boxes return strings; the inject RPCs want enums. These
	// populate the option list AND convert the user's selection back to an
	// enum value without manually wiring the enum pins. - TripleA

	UFUNCTION(BlueprintPure, Category = "Instructor|Options")
	static TArray<FString> GetEmergencyTypeOptions();

	UFUNCTION(BlueprintPure, Category = "Instructor|Options")
	static TArray<FString> GetThreatClassOptions();

	UFUNCTION(BlueprintPure, Category = "Instructor|Options")
	static TArray<FString> GetScenarioOptions();

	UFUNCTION(BlueprintPure, Category = "Instructor|Options")
	static EEmergencyType StringToEmergencyType(const FString& Label);

	UFUNCTION(BlueprintPure, Category = "Instructor|Options")
	static EThreatClass StringToThreatClass(const FString& Label);

	UFUNCTION(BlueprintPure, Category = "Instructor|Options")
	static FString EmergencyTypeToString(EEmergencyType Kind);

	UFUNCTION(BlueprintPure, Category = "Instructor|Options")
	static FString ThreatClassToString(EThreatClass Class);

	// --- Standard palette (read in BP for consistent theming) -------------

	UFUNCTION(BlueprintPure, Category = "Instructor|Theme") static FLinearColor PaletteNormal()    { return FLinearColor(0.31f, 0.78f, 1.00f, 1.f); }   // cyan
	UFUNCTION(BlueprintPure, Category = "Instructor|Theme") static FLinearColor PaletteWarning()   { return FLinearColor(1.00f, 0.70f, 0.24f, 1.f); }   // amber
	UFUNCTION(BlueprintPure, Category = "Instructor|Theme") static FLinearColor PaletteCritical()  { return FLinearColor(1.00f, 0.24f, 0.24f, 1.f); }   // red
	UFUNCTION(BlueprintPure, Category = "Instructor|Theme") static FLinearColor PaletteFriendly()  { return FLinearColor(0.31f, 1.00f, 0.47f, 1.f); }   // green
	UFUNCTION(BlueprintPure, Category = "Instructor|Theme") static FLinearColor PaletteBackground(){ return FLinearColor(0.06f, 0.08f, 0.11f, 1.f); }   // dark
	UFUNCTION(BlueprintPure, Category = "Instructor|Theme") static FLinearColor PaletteBorder()    { return FLinearColor(0.20f, 0.24f, 0.31f, 1.f); }   // grid line

private:
	UPROPERTY(Transient) TObjectPtr<AClearanceSimulationController> CachedController;
	UPROPERTY(Transient) TObjectPtr<AClearanceOperatorPC> CachedOperatorPC;

	// Optional bind for the camera-feed image - so the native mouse handlers
	// can read its cached geometry to convert cursor pixels into normalized
	// image-space. BlueprintReadOnly because the existing BP graph also
	// reads it (BP_PaintCameraOverlay -> DrawCameraOverlayLines etc.) -
	// without that markup, the BP's Get nodes drop the reference and the
	// overlay paint silently breaks. Optional in case a derived BP names
	// the widget something else; mouse drag/zoom is a no-op when not
	// bound. - TripleA
	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Instructor|Camera")
	TObjectPtr<class UImage> Img_CameraFeed;

	// Optional bind for the replay scrub slider - so the native paint pass
	// can read its geometry and draw seam tick marks directly over the track.
	// Optional in case a derived BP renames it; paint is a no-op when not
	// bound. - TripleA
	UPROPERTY(Transient, BlueprintReadOnly, meta = (BindWidgetOptional, AllowPrivateAccess = "true"), Category = "Instructor|Replay")
	TObjectPtr<class USlider> Slider_Scrub;

	// Overview drag state. No UPROPERTY needed - internal only. - TripleA
	bool bOverviewDragging = false;
	FVector2D OverviewDragLastScreenPos = FVector2D::ZeroVector;
	bool IsOverviewActiveForInput() const;

	// Last-tick snapshots so we only fire BP events on actual change. Saves
	// the UMG paint loop a lot of wasted work. - TripleA
	UPROPERTY(Transient) TArray<FInstructorAircraftRow>  LastAircraft;
	UPROPERTY(Transient) FInstructorScoreView            LastScore;
	UPROPERTY(Transient) FInstructorScenarioView         LastScenario;
	UPROPERTY(Transient) TArray<FClearanceNotification>  LastNotifications;

	FName SelectedCallsign = NAME_None;
	float TimeSinceRefreshSec = 0.f;

	// Sliding-window rate sampler for the federation EMIT/RECV /s indicators.
	// Emitters/receivers only expose cumulative counts, so we snapshot once per
	// second and derive the per-second rate from the delta. On Stop the
	// underlying counters go back to 0, which shows up here as a negative delta
	// - clamped to 0 so the panel just reads "0/s" instead of a garbage value
	// after teardown. - TripleA
	float RateSampleAccumSec = 0.f;
	int32 DISEmitRatePerSec = 0;
	int32 DISRecvRatePerSec = 0;
	int32 DDSEmitRatePerSec = 0;
	int32 DDSRecvRatePerSec = 0;
	int32 LastDISEmitSample = 0;
	int32 LastDISRecvSample = 0;
	int32 LastDDSEmitSample = 0;
	int32 LastDDSRecvSample = 0;
	int32 RTIEmitRatePerSec = 0;
	int32 LastRTIEmitSample = 0;
	int32 HLAUpdateRatePerSec = 0;
	int32 LastHLAUpdateSample = 0;

	// Callsigns currently rendered in the scroll box, in row order. Lets the
	// populate path skip the destroy-and-recreate when only field values
	// changed (heading, altitude, speed) and rebuild only when aircraft
	// enter/leave the list. - TripleA
	UPROPERTY(Transient)
	TArray<FName> CurrentRowCallsigns;

	// Persistent row widget instances keyed by callsign. Holds strong refs so
	// widgets survive the ScrollBox reflow when the list order changes. Only
	// re-created when a genuinely new callsign appears. - TripleA
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UUserWidget>> AircraftRowByCallsign;

	// Same idea for the event log - key is ServerTimeAdded rounded to ms
	// (int32). Rounding avoids float-precision mismatches after replication
	// that would otherwise fail exact equality every refresh and invalidate
	// the whole panel. Value is the row widget. - TripleA
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UUserWidget>> EventRowByTimeMs;

	// Ordered ServerTimeAdded-in-ms of the entries currently in the ScrollBox.
	// Lets the populate helper find the in-sync prefix in O(N) so only the
	// diverged suffix gets re-added (avoids ClearChildren flash). - TripleA
	UPROPERTY(Transient)
	TArray<int32> CurrentEventTimesMs;

	void RefreshLocalRefs();
	bool BuildAircraftRows(TArray<FInstructorAircraftRow>& Out) const;
	void BuildScoreView(FInstructorScoreView& Out) const;
	void BuildScenarioView(FInstructorScenarioView& Out) const;
	void BuildNotifications(TArray<FClearanceNotification>& Out) const;

	static bool ScoreChanged(const FInstructorScoreView& A, const FInstructorScoreView& B);
	static bool ScenarioChanged(const FInstructorScenarioView& A, const FInstructorScenarioView& B);
	static bool AircraftListChanged(const TArray<FInstructorAircraftRow>& A, const TArray<FInstructorAircraftRow>& B);
	static bool NotificationsChanged(const TArray<FClearanceNotification>& A, const TArray<FClearanceNotification>& B);
};
