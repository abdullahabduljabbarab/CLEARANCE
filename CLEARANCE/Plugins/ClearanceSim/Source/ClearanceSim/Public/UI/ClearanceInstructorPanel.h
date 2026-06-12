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

	// Override in BP to draw the scope. Runs every paint pass with the panel's
	// FPaintContext + pixel size. Call DrawScopeBoundary / DrawAffiliationSymbol
	// / DrawScopeChaffCloud from inside this. - TripleA
	UFUNCTION(BlueprintImplementableEvent, Category = "Instructor|Scope")
	void BP_PaintScope(UPARAM(ref) struct FPaintContext& Context, FVector2D PanelSize);

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

	UFUNCTION(BlueprintCallable, Category = "Instructor|Views")
	void SetSelectedCallsign(FName NewSelection) { SelectedCallsign = NewSelection; }

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
	void InjectEmergency(FName Callsign, EEmergencyType Kind);

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
	// below. - TripleA
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "Instructor|Scope")
	void DrawAffiliationSymbol(
		UPARAM(ref) struct FPaintContext& Context,
		FVector2D ScopePixelCentre,
		EThreatClass Threat,
		bool bIsMilitary,
		float HeadingDeg,
		EAlertLevel Alert,
		float HalfSizePx = 12.f);

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

	// Whether the per-aircraft data blocks should show the full ATC format
	// (callsign + FL + speed + heading) or the minimal two-line version.
	// Toggleable in BP so the instructor can hide-on-clutter / expand-on-detail. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor|Scope")
	bool bShowFullDataBlocks = false;

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
	int32 MaxNotifications = 16;

	// How often the panel polls replicated state. 0.2s = 5Hz, plenty for an
	// instructor view without thrashing the BP layer. - TripleA
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Instructor")
	float RefreshIntervalSec = 0.2f;

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

	// Last-tick snapshots so we only fire BP events on actual change. Saves
	// the UMG paint loop a lot of wasted work. - TripleA
	UPROPERTY(Transient) TArray<FInstructorAircraftRow>  LastAircraft;
	UPROPERTY(Transient) FInstructorScoreView            LastScore;
	UPROPERTY(Transient) FInstructorScenarioView         LastScenario;
	UPROPERTY(Transient) TArray<FClearanceNotification>  LastNotifications;

	FName SelectedCallsign = NAME_None;
	float TimeSinceRefreshSec = 0.f;

	// Callsigns currently rendered in the scroll box, in row order. Lets the
	// populate path skip the destroy-and-recreate when only field values
	// changed (heading, altitude, speed) and rebuild only when aircraft
	// enter/leave the list. - TripleA
	UPROPERTY(Transient)
	TArray<FName> CurrentRowCallsigns;

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
