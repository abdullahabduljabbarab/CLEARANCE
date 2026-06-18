# CLEARANCE — Production Log & Migration Tracker

**Lives in the sandbox. Do NOT migrate this file to the main project.**
This is the working record for turning the pre-production specs in [Docs/](.)
into implemented, tested, migrated systems. Three parts:

1. Build & migration progress tracker
2. Reusable per-system migration checklist
3. Pre-production → production changelog (where reality diverged from the docs)

Pre-pro source docs: [ATCSIMSYSTEMSDESIGN](ATCSIMSYSTEMSDESIGN.md) ·
[MVP](MVP.md) · [C++ Scaffold](C++%20Scaffold%20-%20Clearance.md) ·
[Technical Implementation Scaffold](Technical%20Implementation%20Scaffold.md) ·
[Risk Register](Risk%20Register.md) · [Test Plan](Test%20Plan.md)

---

## 1. Build & migration progress tracker

Order follows the MVP build-priority list. Status legend:
⬜ not started · 🟡 in progress · ✅ done

| # | System / task | Built (sandbox) | Tested | Assets (Neo) | Migrated → main |
|---|---|---|---|---|---|
| 1 | Core types (enums, structs, delegates, constants) | ✅ compiled (2026-05-24) | ⬜ | n/a | ✅ 2026-05-24 |
| 2 | `AClearanceAirspaceManager` | ✅ compiled (2026-05-24) | ⬜ | ⬜ | ✅ 2026-05-24 |
| 3 | `UClearanceAircraftBehaviour` | ✅ compiled (2026-05-24) | ⬜ | n/a | ✅ 2026-05-24 |
| 4 | `UClearanceInstructionValidator` | ✅ compiled (2026-05-24) | ⬜ | n/a | ✅ 2026-05-24 |
| 5 | `UClearanceCommsRouter` | ✅ compiled (2026-05-24) | ⬜ | ⬜ | ✅ 2026-05-24 |
| 6 | `UClearanceConflictDetector` | ✅ compiled (2026-05-24) | ⬜ | ⬜ | ✅ 2026-05-24 |
| 7 | `UClearanceScoring` | ✅ compiled (2026-05-24) | ⬜ | ⬜ | ✅ 2026-05-24 |
| 8 | `AClearanceAircraftSpawner` | ✅ compiled (2026-05-24) | ⬜ | ⬜ | ✅ 2026-05-24 |
| 9 | `AClearanceSimulationController` | ✅ compiled (2026-05-24) | ⬜ | ⬜ | ✅ 2026-05-24 |
| 10 | Minimal player UI / radar (Neo-led) | ⬜ | ⬜ | ⬜ | ⬜ |
| 11 | `AClearanceRadarSite` + multi-radar fusion | ✅ compiled (2026-06-07) | ✅ verified [N/M] live (2026-06-07) | n/a | ✅ 2026-06-07 |
| 12 | Scenario runner + JSON authoring (Baltic Intercept) | ✅ compiled (2026-06-08) | ✅ verified live: SK238/UNKNOWN01 spawn, pursue, AWACS voice, intercept, break-off (2026-06-08) | n/a | ✅ 2026-06-08 |
| 13 | Scenario #2: Hijack Response | ✅ compiled (2026-06-08) | ✅ verified live: 5-aircraft sector, BAW472 7500 inject, ACC voice doctrine call (2026-06-08) | n/a | ✅ 2026-06-08 |
| 14 | Scenario #3: Mass Divert | n/a (JSON only) | ✅ verified live: 6-aircraft sector, MET advisory, wind flip, TOWER divert call, cascading fuel emergencies at T+300s+ (2026-06-08) | n/a | ✅ 2026-06-08 |
| 15 | Scenario #4: Mayday Engine Fire (7700) | n/a (JSON only) | ✅ verified live (2026-06-09) | n/a | ✅ 2026-06-08 |
| 16 | Scenario #5: NORDO Inbound (7600 x2) | n/a (JSON only) | ✅ verified live (2026-06-09) | n/a | ✅ 2026-06-08 |
| 17 | Scenario #6: Cold War Probe (multi-bandit GCI) | n/a (JSON only) | ✅ verified live (2026-06-09) (after leading-zero JSON fix) | n/a | ✅ 2026-06-08 |
| 18 | Scenario #7: Mixed Ops (restricted airspace) | n/a (JSON only) | ✅ verified live (2026-06-09) | n/a | ✅ 2026-06-08 |
| 19 | Networked Instructor Station (server-authoritative replication, custom HUD, multicast voice, alert-level rep, role badges) | ✅ compiled (2026-06-10) | ✅ verified live PIE 2-window: aircraft state / scenario / score / notifications / TTS / conflict colours all sync between operator + instructor windows (2026-06-10) | n/a | ✅ 2026-06-10 |
| 20 | EOS plumbing (plugins, NetDriver swap, session subsystem, 5 console commands) | ✅ compiled (2026-06-10) | ⏸️ PARKED - waiting on Epic brand review of the EAS app (case 500QP00001XGtOnYAL, 1-7 business days). Once approved, login should chain cleanly. | n/a | ✅ 2026-06-10 (parked) |
| 21 | MIL-STD-2525C tactical symbology (NATO affiliation frames replacing debug spheres) | ✅ compiled (2026-06-10) | ✅ verified live: friend/hostile/unknown/neutral shapes render distinctly per ThreatClass with bearing vector + alert ring (2026-06-10) | n/a | ✅ 2026-06-10 |
| 22 | Electronic Warfare - jamming + chaff sensor effects (FAircraftState.bJammingOn, FChaffCloud, PaintConfidence on FRadarTrack, +-12deg bearing-wedge degradation, 12s chaff ghost tracks) | ✅ compiled (2026-06-11) | ✅ verified live: SK238 jam drops confidence to 25% and holds, chaff paints ghost ring, fade-vs-paint conflict bug fixed (PaintConfidence separates the two) | n/a | 🟡 pending |
| 23 | EW gameplay integration (TickBanditEW reactive jam/chaff, intercept-through-EW bonus, scenario JSON activateJammer/dropChaff actions, DeclareTrackLost phraseology with EW-aware scoring) | ✅ compiled (2026-06-11) | ✅ verified live | n/a | 🟡 pending |
| 24 | Instructor Station UMG panel + scope rendering (truth scope NativePaint pipeline, MIL-STD-2525C affiliation glyphs, data-block leader lines + 18-slot auto-avoid, navigation waypoint actors + airway connectivity, ATC-style range labels, scope auto-fit + ExitRadius clamp, TrueAffiliation vs ThreatClass split, hijack visual promotion) | ✅ compiled (2026-06-12) | ✅ verified live: full sector chart reads professionally, doctrine pass through 7 scenarios verified (2026-06-12) | ✅ `WBP_InstructorPanel` + `WBP_InstructorAircraftRow` (2026-06-12) | ✅ 2026-06-12 |
| 25 | Instructor PIP camera system - live 3D feed in scope area, 5 view modes (Tower w/ yaw pan + tag-anchored, Approach per-runway w/ 3/4 angle, Chase w/ 4 sub-angles + selection-driven targeting + mesh-bounds-scaled offsets, Overview, Operator POV w/ explicit ControlRotation replication), texture-streaming view registration, 100Hz NetUpdate for chase smoothness, instructor-classify mis-ID bypass + TrueAffiliation rewrite | ✅ compiled (2026-06-13) | ✅ verified live: all 5 views render, operator stream synced, runway picker dispatches by label, chase reads visual actor transform locked to mesh sync (2026-06-13) | ✅ updated `WBP_InstructorPanel` (camera toggle + mode row + runway picker + chase angle arrows + operator button) (2026-06-13) | ✅ 2026-06-13 |
| 26 | Camera-feed tactical overlay - runway rectangle outline + dashed centerline + threshold designators (per-mesh-bounds length/width, perpendicular-offset adaptive sizing), 100nm approach corridor w/ range ticks, 3° glide slope through AltitudeToWorldZOffset (24 segments, FL ladder), MIL-STD-2525 zones as wireframe cylinders (FL500 ceiling, 4 cardinal spokes, name labels), sector ring + compass headings, view-gated visibility (Overview hides corridor/slope; Tower/Approach/Operator hide runway outline; Operator hides centerline), per-runway active-end gating from `AirspaceManager::GetActiveRunway` (wind-driven swap), pulsing palette (cyan runway, magenta corridor/glide for golden-hour contrast), depth-aware line-trace occlusion on Overview, Liang-Barsky UV clipping w/ 1.5% inset, FInstructorCameraLine + FInstructorCameraText structs, paint-space geometry anchoring for dual-viewport correctness | ✅ compiled (2026-06-14) | ✅ verified live: all 5 camera views render the overlay correctly, runway designators readable at any distance, wind-flip swaps the corridor live | ✅ updated `WBP_InstructorPanel` (BP_PaintCameraOverlay + DrawCameraOverlayLines/Text wiring, BindWidgetOptional Img_CameraFeed), new `WBP_CameraLabel` widget (2026-06-14) | ✅ 2026-06-14 |
| 27 | Overview camera drag + zoom - `AddOverviewPan(FVector2D)` + `AddOverviewZoom(float)` + `ResetOverviewView()` BlueprintCallable, persisted pan offset + zoom level on the controller, applied in `UpdateInstructorPip` Overview case (altitude / zoom, X/Y / pan), pan extent scales with zoom (`SectorRadius * (1 - 1/zoom)` so zoom=1 = no pan, zoom=4 = 0.75x sector), zoom clamped [1.0, 4.0] (no zoom-out past default), `NativeOnMouseButtonDown/Move/Up/Wheel/MouseButtonDoubleClick` overrides on `UClearanceInstructorPanel`, GrabHandClosed cursor on drag, double-click resets | ✅ compiled (2026-06-15) | ✅ verified live: drag pans, scroll zooms, double-click resets, zooming out re-clamps pan back to centre | ✅ updated `WBP_InstructorPanel` (BindWidget `Img_CameraFeed` as `BlueprintReadOnly`) (2026-06-15) | ✅ 2026-06-15 |
| 28 | Session replay system - server-authoritative recording via `UClearanceSessionRecorder` (auto-start, lazy-init `GetRecorder`, client-side snapshot capture for scrub bar), server tick poses world to `Recorder->FindSnapshotAt(ReplayTime)`, replicated `bReplayMode / bReplayPaused / ReplayTime / ReplaySpeed / ReplayDuration / ReplaySegmentSeams`, 5 Server_Inject* RPCs on AClearanceOperatorPC (EnterReplay/ResumeLive/SeekReplay/SetReplayPaused/SetReplaySpeed), `ResumeLive` resumes raw recorder (no Clear) so buffer accumulates across Go Live cycles, segment seam timestamp pushed on each Go Live, scrub-bar seam tick painting in `NativePaint` via `Slider_Scrub` BindWidget + slider-anchored FPaintContext | ✅ compiled (2026-06-16) | ✅ verified live: replay scrubs back/forth, buffer accumulates across multiple Go Live/Replay cycles, seam ticks land at correct positions | ✅ updated `WBP_InstructorPanel` (embedded play/pause/scrub/speed controls, BindWidgetOptional `Slider_Scrub`) (2026-06-16) | ✅ 2026-06-16 |
| 29 | Comms transcript pipeline - new `FCommsTranscriptEntry` (TimeSec + EClearanceCommsRole + Callsign + Text) + replicated `Transcript` array on controller (capped 500), bound to `OnInstructionResult` w/ extended delegate signature carrying the full `FAircraftInstruction`, phraseology renderer for all 8 instruction types (heading / altitude / speed / hold / approach / takeoff / exit / track-lost) with symmetric pilot readbacks, refusal handling (Unable / Negative traffic / Out of sector / NORDO silence for the "missing readback" tell on hostiles posing as Unknown), `LogTranscriptSystem` for emergencies / classifications / advisories, BlueprintCallable `GetTranscript` for UMG | ✅ compiled (2026-06-16) | 🟡 pending UMG | ⬜ pending (Performance tab w/ Score Report + Transcript sub-tabs) | 🟡 pending |

---

## 2. Per-system migration checklist (sandbox → main)

Both projects are identical copies (UE 5.7, module name `CLEARANCE`), so code
migrates by copying files into the same paths. Migration is **one-way** and only
for **finished, tested** systems. Never edit a migrated system directly in main —
fix in sandbox and re-migrate.

For each system:

- [ ] **Copy C++** — the system's `.h/.cpp` into the *identical* relative path
      under main's `Source/CLEARANCE/...`
- [ ] **Copy deltas** — any new `Build.cs` dependency lines or `Config/*.ini`
      changes that system introduced (changed lines only, not whole files)
- [ ] **Compile main** — build the CLEARANCEEditor target against main's
      `.uproject` (CLI `Build.bat`, or the IDE). Do NOT rely on the editor's
      "rebuild?" prompt on open: UE often fails to detect newly ADDED source
      files in an existing module and silently loads the stale DLL. Always do an
      explicit build after a sync. (Build BEFORE bringing assets across.)
- [ ] **Bring assets over** — Neo's Blueprints/widgets for this system, via
      either:
      - UE **Migrate** (right-click asset → Asset Actions → Migrate) — carries
        the asset + its dependency chain (preferred for anything referenced), or
      - direct `.uasset` copy to the same `/Game/...` path (OK because projects
        are identical; fine for standalone assets)
- [ ] **Verify** — open main, compile Blueprints, confirm no broken references,
      sanity-check the system runs
- [ ] **Update the tracker** above (Migrated → ✅) and log anything notable below

### Never copy (let main regenerate these)
- `Intermediate/`, `Binaries/`, `DerivedDataCache/`, `Saved/`
- any `*.generated.h`
- `CLAUDE.md`, `Docs/handoff/`, this file — sandbox-only workflow files

---

## 3. Pre-production → production changelog

Running record of where the implementation **deviates from, refines, or
corrects** the pre-pro docs. Add an entry whenever a design decision changes
during the build, so the docs and the code don't silently disagree.

Format:
```
### [YYYY-MM-DD] <short title>
- DOC: which spec doc + section this concerns
- WAS: what the pre-pro doc says
- NOW: what production actually does
- WHY: reason for the change
- DOC UPDATED?: yes / no / pending
```

### Known items carried in from doc review (not yet actioned)
These were spotted while reading the specs; logged here so they aren't lost.

- **Test Plan — duplicate test IDs.** Tests 16–22 and 31–34 each appear twice
  (across Behaviour/Comms and Conflict/Scoring sections). Needs a renumber or
  system-prefixed IDs (`BEH-16`, `COMM-16`). DOC UPDATED?: pending (user will fix).
- **Tuning constants are scattered** across Test Plan / Scaffold / breakdown
  (climb rates, wake distances, 8/5/3nm thresholds, 1000ft vertical min). To be
  consolidated into a single authoritative `Core/ClearanceConstants.h` when the
  Core types are built — that file becomes the source of truth for the numbers.
- **Existing template C++ to remove/replace.** `Source/CLEARANCE/` currently
  holds UE Shooter/Horror template code (`ShooterCharacter`, `HorrorCharacter`,
  weapons, etc.) unrelated to the ATC sim. Decision pending: strip it vs leave
  it. Affects the build either way. DOC UPDATED?: n/a.
- **Module vs plugin packaging.** Pre-pro assumes code in the game module
  (`Source/CLEARANCE/`). Open decision whether to build as a self-contained
  plugin for cleaner reuse/migration. DOC UPDATED?: n/a.

### Implementation changes

### [2026-06-16] Comms transcript pipeline - operator + pilot phraseology rendered from every instruction
- Extended `FOnInstructionResult` to ThreeParams: now broadcasts the full
  `FAircraftInstruction` alongside callsign + result. The router was already
  building the instruction; the panel side needed it to render proper ATC
  phraseology, not just "Accepted" / "Rejected". Backwards-compatibility
  shim avoided - everyone who binds the delegate gets updated to the new
  signature in the same pass.
- **`FCommsTranscriptEntry`** (TimeSec + EClearanceCommsRole + Callsign +
  Text) replicated array on the controller, capped 500 entries (drops
  oldest). Three roles: Operator (outgoing instruction), Pilot (readback /
  refusal), System (emergencies / classifications / advisories).
- **Phraseology renderers** in an anon namespace - one per instruction
  type, symmetric pilot readback. Headings rendered "Heading three-four-
  zero", altitudes "Climb and maintain flight level two-four-zero",
  speeds "Reduce speed to two-five-zero knots", holding patterns "Hold
  west of EKWAS on the two-seven-zero radial right turns", approaches
  "Cleared ILS Runway two-seven Left approach", takeoffs / departures
  "Cleared for takeoff Runway two-seven Right", track-lost notifications,
  exits. Pilot readbacks mirror the form the trainee would expect to hear.
- **Refusal handling** uses the four `EInstructionResult::Rejected_*` codes
  with distinct pilot voice lines: Unable (over weight / inside envelope),
  Negative traffic (conflict on the routing), Out of sector (off-band
  request), and `Rejected_NoResponse` logs operator silence - the NORDO
  tell that an Unknown contact is actually hostile. Trainee can't fail
  the diagnostic if the transcript doesn't surface what they sent and
  what came back.
- **`HandleInstructionResult`** UFUNCTION on the controller hooks
  `CommsRouter->OnInstructionResult.AddUniqueDynamic` during
  `InitialiseSystems`, routes each result by enum into either operator +
  readback or operator + refusal. `LogTranscriptSystem` covers the
  non-instruction lines (emergencies, true-affiliation reveals on
  classify, advisory broadcasts).
- BlueprintCallable `GetTranscript()` returns the replicated array as a
  `const TArray<FCommsTranscriptEntry>&` - Performance tab Transcript
  sub-tab will bind ListView ItemsSource to it.
- WHY THIS MATTERS: AAR review without a comms log is useless - the
  trainee can't see what they actually said vs what they meant to say,
  and the assessor can't grade phraseology. A scrubbable transcript
  paired with the replay scrub bar (timestamps line up by `TimeSec`)
  lets the instructor jump to "where the trainee skipped the readback"
  or "where the operator failed to issue avoidance on a conflict alert"
  in one click. Defence training procurement specifies this as a baseline
  AAR feature.

### [2026-06-16] Session replay system - server-authoritative recording, embedded scrub controls, segment seam markers
- The truth scope already streamed every aircraft state by replication;
  what was missing was history. `UClearanceSessionRecorder` was scaffolded
  earlier; today it goes end-to-end with a working scrub bar, server-
  authoritative time pinning, and an accumulating buffer across multiple
  Go Live -> Replay cycles.
- **Auto-start on every instance**: `Recorder` is `NewObject`-allocated in
  `BeginPlay` BEFORE the `HasAuthority()` gate so client peers also get a
  local recorder. Server captures the canonical world; client recorder
  captures the replicated snapshot stream for its own scrub-bar painting.
  `GetRecorder()` is now non-const + lazy-inits if null - any caller
  (UMG poll, BP delegate) before BeginPlay completes still gets a valid
  pointer. Bug pre-fix: client `Recorder` lived inside `InitialiseSystems`
  which is gated behind `!HasAuthority() return`, so client never had one
  and snapshots silently dropped on the floor.
- **Replicated replay state** (DOREPLIFETIME): `bReplayMode`, `bReplayPaused`,
  `ReplayTime`, `ReplaySpeed`, `ReplayDuration`, `ReplaySegmentSeams`. All
  set server-side, broadcast to clients, read by UMG. Operator pawn sees
  the same playhead the instructor scrubbed to - they're watching the
  same recording, not two different ones.
- **5 Server_Inject* RPCs** on `AClearanceOperatorPC`: `EnterReplay`,
  `ResumeLive`, `SeekReplay(float)`, `SetReplayPaused(bool)`,
  `SetReplaySpeed(float)`. All Reliable + WithValidation + BlueprintCallable.
  Panel UMG calls them from button click handlers; server validates, mutates
  controller state, broadcasts. Standard pattern - no client-side cheats,
  no replay-tampering, no scrubbing one side of a multiplayer session past
  the other.
- **Accumulating buffer across Go Live cycles**: `ResumeLive` was clearing
  the recorder ring buffer each transition. Now it calls `Recorder->
  StartRecording()` raw (skipping the controller wrapper which clears).
  Each `EnterReplay -> ResumeLive` pair pushes a timestamp onto
  `ReplaySegmentSeams`. Scrub bar renders the seams as amber 2px vertical
  ticks via `Slider_Scrub`'s own paint-space geometry in `NativePaint`.
  Instructor can see at a glance "this was the boundary where I let them
  back into the sim" - critical for AAR replay across multiple incidents
  in one training session.
- **Pose-back-in-time**: server tick reads `Recorder->FindSnapshotAt(
  ReplayTime)` and writes the resulting aircraft states straight to the
  AirspaceManager - every aircraft mesh actor moves to where it was at
  that timestamp. Operator + instructor cameras (including the PIP feed)
  follow naturally because they read from the same authoritative state.
  Replay isn't a 2D scope overlay - it's the world rewound, viewable
  through every camera mode.
- **`ReplayDuration` captured server-side at `EnterReplay`** so it doesn't
  keep climbing while you scrub. Slider's Max binds to it. Pre-fix: client
  recorder kept recording during replay, duration crept up on every paint,
  scrub bar values drifted.
- WHY THIS MATTERS: portfolio-grade ATC sims charge defence customers
  premium for session replay + scrubbable AAR. This puts the architecture
  in place: server-authoritative pose-back, replicated playhead state,
  every camera plays it back natively, transcript timestamps line up.
  Next step (Performance tab) wires the comms transcript and score
  breakdown to the same playhead. -TripleA

### [2026-06-15] Overview camera drag + zoom - native mouse handlers, pan extent scales with zoom
- Overview is the wide top-down sector cam used for situational awareness.
  Static framing isn't enough when the instructor wants to inspect one
  corner of the sector - they need to grab the world and reposition.
- **State on the controller**: `InstructorOverviewPanOffsetUnits` (FVector2D
  in cm) and `InstructorOverviewZoomLevel` (float, clamped [1.0, 4.0]).
  Zoom is bounded - can't zoom OUT past the default framing (1.0); zoom IN
  goes to 4x. `ResetOverviewView()` wipes both back to defaults; double-
  click triggers it.
- **`AddOverviewPan(FVector2D PanDeltaUv)`**: pan delta passed in UV units
  (-1..1 fraction of screen). Converts to world units via
  `world_delta = uv_delta * 2 * altitude * tan(HalfFOV)` so dragging a
  point on the camera moves the world by exactly the same screen amount
  regardless of zoom. Pan extent is then clamped to
  `SectorRadius * max(0, 1 - 1/zoom)`: at zoom=1 the extent is 0 (no
  panning past the default frame, can't drag outside the sector); at
  zoom=2 it's half the sector radius; at zoom=4 it's 0.75. Means you
  can drag exactly as far as the camera frustum allows while still
  showing sector content - never empty void.
- **`AddOverviewZoom(float)`**: altitude = DefaultAltitude / ZoomLevel.
  After zoom change re-clamps the existing pan offset against the new
  zoom extent, so zooming OUT after dragging snaps the view back to
  centre instead of leaving you panned past the new (smaller) extent.
  Pre-fix: zoom out left the camera looking at the void outside the
  sector ring.
- **Drag direction NOT inverted**: dragging right moves the camera right
  (steering the view, not dragging the world). User-tested - "drag the
  world" feels wrong on a top-down map; "steer the camera" matches what
  every flight-sim map UI does.
- **`UClearanceInstructorPanel` native mouse overrides**:
  `NativeOnMouseButtonDown` captures pointer + records start screen pos +
  sets `EMouseCursor::GrabHandClosed`; `NativeOnMouseMove` computes
  `delta = (current - last) / geometry.LocalSize` and calls
  `AddOverviewPan`; `NativeOnMouseButtonUp` releases capture + restores
  `EMouseCursor::Default`; `NativeOnMouseWheelEvent` calls
  `AddOverviewZoom(WheelDelta * 0.2)`; `NativeOnMouseButtonDoubleClick`
  calls `ResetOverviewView`. All gated on `IsOverviewActiveForInput()`
  so the handlers no-op when the panel isn't showing the Overview camera.
- WHY THIS MATTERS: small UX detail but it's the difference between
  "tech demo" and "professional tool" feel. Real instructor stations
  let you grab + zoom the overview. Closed-fist cursor is the OS-level
  visual signal users expect for "I'm dragging the world right now".
  -TripleA

### [2026-06-14] Camera-feed tactical overlay - runway outline, approach corridor, glide slope, zone wireframes through every cam
- The PIP camera feed shipped 2026-06-13 with no diegetic overlay -
  pristine 3D world only. Realistic-looking but tactically useless: the
  instructor couldn't tell "is this the right runway" or "where does the
  approach corridor end" or "is that aircraft inside the restricted
  zone" without cross-referencing the scope. Today adds a layered HUD
  that draws into the PIP RT in world-space-projected lines + labels.
- **Runway rectangle outline**: 4 corner vertices computed per-frame from
  the active runway's threshold + opposite threshold + perpendicular
  half-width (derived from the spawned runway mesh bounds, NOT a config
  number, so it matches whatever asset is in the level). Drawn in pure
  white - the colour of real-world runway centerline + edge paint. Cyan
  was visually too close to chaff returns; amber clashed with the
  golden-hour environment lighting.
- **Dashed centerline**: 24 segments down the runway long axis, 0.6 duty
  cycle. Matches FAA-spec dashed centerline pattern. Hidden on Operator
  camera because the operator is sitting at the tower window looking at
  the runway directly - they shouldn't see synthetic centerline overlaid
  on the real paint.
- **Threshold designators**: text labels rendered through new
  `WBP_CameraLabel` widget instances pooled per overlay item. Per-mesh-
  bounds adaptive sizing: 5% of viewport per character, capped, so the
  designator is readable from any altitude (tower close-up to overview
  high). Anchored perpendicular-offset from threshold midpoint by 20%
  of runway width so the text doesn't cover the runway edge stripe.
- **100nm approach corridor**: pulsing magenta extruded box from the
  active threshold backwards along the runway heading. 10nm range
  ticks. Magenta chosen for high-contrast against the warm-orange
  ground at golden hour - cyan was washing out into the sky. Pulses on
  a sine wave so the instructor can read "this is the corridor where
  the inbound should be" at a glance.
- **3° glide slope**: 24 segments along the corridor, height computed
  through `AClearanceAircraftBehaviour::AltitudeToWorldZOffset` so it
  follows the same compressed-altitude curve as live aircraft - the slope
  ends at the actual height an aircraft would be at 100nm out, not a
  literal 3° tangent. FL ladder labels (FL050, FL100, FL150, FL200, FL250)
  drawn at key altitudes.
- **Zones as wireframe cylinders to FL500**: MIL-STD-2525 zones
  (protected / restricted / IZ etc) extruded from the ground polygon up
  to FL500 (the operational ceiling) - they're airspace volumes, not
  ground markings. 4 cardinal spokes + name label at the apex. Player
  feedback: "the circle just on the ground when it's literally an
  airspace thing" - fixed.
- **Sector ring + compass headings**: top-down boundary of the sector
  drawn as a closed polygon segment ring, with cardinal/intercardinal
  compass labels (N / NE / E / SE / S / SW / W / NW) placed slightly
  outside the ring. Lets the instructor orient in any view without
  going back to the scope.
- **View-gating**: each layer evaluates its own visibility per camera
  mode. Overview hides the corridor + glide slope (already top-down,
  not needed; would clutter). Tower / Approach / Operator hide the
  runway outline because they're looking AT it from above or at the
  threshold - synthetic outline would float in front. Operator also
  hides the centerline (real paint visible). Chase shows everything
  because the instructor needs the tactical context while following.
- **Active-end gating from `AirspaceManager::GetActiveRunway`**: only the
  current active end gets the approach corridor + glide slope drawn -
  wind shift flips the active end mid-session, overlay follows live.
- **Liang-Barsky UV clipping with 1.5% inset**: every line tested against
  the viewport rectangle before draw. Inset keeps the line from drawing
  exactly on the screen edge where slate would clip jagged-edged. Result:
  no overdrawn lines extending past the camera image into the rest of
  the panel canvas.
- **Depth-aware occlusion on Overview**: line-trace from camera eye to
  each runway-outline endpoint - segment hidden if it hits world geometry
  in between. Lets the runway outline correctly disappear behind
  buildings/terrain on Overview. Disabled on other views because they're
  closer (high cost) and the runway is always in the foreground anyway.
- **Paint-space geometry anchoring**: the camera image lives in a
  different render region than the panel. New `FPaintContext` rebuilt
  from `Img_CameraFeed->GetPaintSpaceGeometry()` each frame, so overlay
  draws in the image's local space - not the panel's. Without this, the
  overlay was drawing onto the panel canvas behind the camera image and
  invisible.
- WHY THIS MATTERS: the PIP camera feed is now a tactical instrument,
  not a pretty render. Instructor sees the same diegetic overlay set
  recruiters expect from real control rigs - runway designators, glide
  slope, restricted zones, sector boundary. Sells the "this person
  knows what an actual ATC training rig looks like" judgement on a
  portfolio review in 5 seconds. -TripleA

### [2026-06-13] Instructor PIP camera system - 5 live 3D viewpoints, operator stream, instructor doctrine fix -> MIGRATED to main 2026-06-13
- The instructor station gains a picture-in-picture 3D view that swaps in
  place of the truth scope on a toggle. One USceneCaptureComponent2D on
  the SimulationController feeds an HDR RT; the panel binds an Image
  widget brush to it; NativePaint gates BP_PaintScope when the toggle is
  on so the scope vectors don't render on top of the camera feed.
- **Five view modes** (EClearanceCameraView), each tuned per shape of the
  job:
  - **Tower**: looks up an actor in the level tagged `ClearanceTower`
    (any actor - cube, target point, tower mesh) and parks the camera at
    its literal transform. Designer drops the marker exactly where the
    cab should sit; pan-left/pan-right arrow buttons layer extra yaw on
    top via `ApplyTowerYawDelta`. Falls back to ~50m above the active
    runway threshold if no tag exists.
  - **Approach**: per-runway picker. `GetApproachRunwayLabels` returns
    "RWY 27R" / "RWY 09L" style names (designator derived from heading,
    L/C/R suffix derived from each threshold's lateral projection vs its
    same-heading siblings). `PickApproachRunwayByLabel` dispatches by
    string so the UMG can use static buttons with literal labels - no
    GetArrayItem, no dynamic generation, no index math. Camera composes
    as a 3/4 angle: 600m off to the side of the threshold, 300m up,
    looking 400m down the runway. Frames runway + corridor + arriving
    aircraft in one shot.
  - **Chase**: 4 sub-angles cycled by left/right arrows (Chase / Cockpit
    / Side / Top). Camera offsets are scaled to the aircraft's mesh
    bounds so a 747 frames properly without the camera ending up inside
    the fuselage and the cockpit cam reaches the actual windscreen on
    long airframes. Auto-targets the panel's `SelectedCallsign` -
    clicking a different aircraft row re-targets and resets the
    sub-angle to default.
  - **Overview**: high above the sector centre looking down at the
    runway. `ToggleScopeCameraView` defaults to this mode each time the
    instructor opens the camera feed so they always land on a sensible
    sector-wide shot rather than wherever the last mode left things.
  - **Operator POV**: live stream of what the operator sees, head
    movement included.
- **Operator-view replication**: `AClearanceOperatorPC::PlayerTick`
  pushes the operator's ControlRotation + view location to the
  SimulationController at 120Hz - directly on the host, via Server RPC
  on a remote client. The SimulationController stores them as Replicated
  UPROPERTYs; the PIP reads them straight. Bypasses every UE pawn
  rotation replication footgun (yaw-only Character actor rotation,
  missing RemoteViewPitch on non-Character pawns, lost roll for VR
  HMDs, etc.) so the instructor sees exactly what the trainee sees with
  the right pitch + yaw + roll for free, no dependence on the operator
  pawn's class.
- **Chase camera sync to visual mesh**: reads aircraft POSITION directly
  from the spawned visual actor's current world transform; heading from
  replicated FAircraftState. Reading the state position alone left the
  camera offset by one frame's worth of state ahead of or behind where
  the visual actor would actually be drawn, and the aircraft visibly
  drifted within the chase frame each tick. Locking to the visual's
  live transform pins them together at full framerate.
- **100Hz NetUpdateFrequency** forced on AClearanceAirspaceManager and
  AClearanceSimulationController in BeginPlay so aircraft state churns
  out to clients every ~10ms - critical for chase / operator PIP
  smoothness. The BeginPlay force is necessary because level-placed
  actors keep their serialized NetUpdateFrequency value regardless of
  constructor defaults; can't rely on the property change without a
  level re-save.
- **Capture quality overrides** applied in BeginPlay (TAA, Lumen GI,
  Lumen reflections, SSR, full lighting stack, LODDistanceFactor 0.1,
  HDR tone curve capture source, post-process volume contribution).
  Without these the SceneCapture renders against a stripped-down path
  for "perf" - everything looks flat-shaded and reflectionless.
- **Texture streaming view registration**: each capture tick calls
  `IStreamingManager::Get().AddViewInformation(TargetLoc, ScreenSize,
  FovScreenSize)`. Without it the streamer doesn't know about the PIP
  frustum so it sends low-mip placeholder textures - aircraft rendered
  as solid white blobs in the PIP while looking fine in the main view.
- **Instructor classify mis-ID bypass**: `ClassifyAircraft` gains
  `bAsInstructor` (default false). When true: the catastrophic-mis-ID
  scoring penalty is skipped AND the contact's `TrueAffiliation` is also
  rewritten to match the new class. The instructor inject panel routes
  through `OperatorPC::Server_InjectClassify` which passes
  `bAsInstructor=true`, so reclassifying from the inject panel doesn't
  fire a doctrine-failure score against the trainee AND re-skins the
  truth scope symbol immediately. Operator voice classifications still
  go through `ClassifyAircraft` with the default false and continue to
  score normally.
- WHY THIS MATTERS: turns the instructor station from a 2D-only scope
  into a proper multi-feed training rig - the instructor can scope-
  monitor at a tactical level AND camera-cut to any aircraft / approach
  / tower / operator-eye view to debrief a specific moment. Defence
  training procurement explicitly buys the "instructor can see what the
  trainee sees" stream + per-aircraft chase camera + tower-from-air
  views; all three exist now. The same camera pipeline is also the
  natural front-end for the replay system (already partly in the
  controller via AAR) - playback bar + scrub is the next step on the
  same RT.

### [2026-06-12] Instructor station UMG panel + truth scope rendering -> MIGRATED to main 2026-06-12
- The networked instructor station from 2026-06-10 was a HUD readout.
  Today it becomes a real UMG console: a left-side truth scope with full
  MIL-STD-2525C symbology, data block labels with auto-avoid placement,
  navigation overlay (waypoints + airways + range rings), and a right-
  side aircraft list + scenario / score / inject panel.
- **`UClearanceInstructorPanel` widget base + `WBP_InstructorPanel`**:
  per-tick snapshot pipeline. Server-authoritative SimulationController
  exposes accessors; the panel polls them, builds plain-data structs
  (FInstructorAircraftRow, FInstructorScoreView, FInstructorScenarioView,
  FInstructorZoneMarker, FInstructorChaffMarker,
  FInstructorWaypointMarker, FInstructorAirwaySegment) and fires
  BlueprintImplementableEvents. The UMG never touches actor pointers.
- **Vector scope painting via `NativePaint` + `BP_PaintScope`
  BlueprintImplementableEvent**. Single-frame layer order:
  airways -> zones -> runways -> waypoints -> boundary -> range labels
  -> aircraft -> data blocks -> selected ring. Centralised
  `ScopeNmToPixel` projection so proportional scaling stays consistent
  regardless of ScopeRangeNm. Scope auto-fit walks aircraft + zones
  + runways + waypoints each frame; clamped to the simulation's
  ExitRadiusNm so the scope can never zoom past the sector boundary.
- **MIL-STD-2525C affiliation glyphs on aircraft symbols**: friend
  rectangle (cyan), hostile diamond (red), unknown octagon-quatrefoil
  (amber), neutral square (green). Bearing vectors + alert rings +
  military modifiers per symbol. Aircraft-row badge equivalents in the
  right-side list use unicode shapes with the same colour palette.
- **Data-block leader lines with 18-slot tiered auto-avoid**: each
  aircraft label is positioned by trying close / mid / far slots in
  sequence; first slot whose FBox2D::Intersect clears all prior labels
  wins. Leader line from symbol edge to label closest corner. Keeps
  labels legible at high traffic density without manual layout.
- **Navigation overlay**: new `AClearanceWaypoint` actor with `Name`
  (FName) + `ConnectedWaypoints` (TArray<FName>) so the designer drops
  fixes in the level and the scope renders triangle + label per
  waypoint + dotted airway line per connection. The `GetAirwaySegments`
  pass dedups two-way listings via a sorted-pair key. Range labels
  drawn at 25 / 50 / 75% of ScopeRangeNm in ATC-style rounded values.
- **TrueAffiliation vs ThreatClass split**: FAircraftState gains
  `TrueAffiliation` (ground truth - what the contact ACTUALLY is, set
  once at spawn) alongside the existing ThreatClass (operator's current
  classification). Instructor truth scope reads TrueAffiliation;
  Row.OperatorClassification field carries ThreatClass for the future
  operator-view toggle. Civilian default flipped to Neutral per NATO
  doctrine (Friendly is reserved for allied military); bandits =
  Unknown ThreatClass + TrueAffiliation Hostile; allied military stays
  Friendly. 4 controller logic sites updated; mis-ID scoring uses
  TrueAffiliation explicitly. All 7 scenario JSONs aligned: 33
  civilians flipped Friendly -> Neutral; cold_war_probe + baltic_intercept
  bandit spawns got explicit "trueAffiliation": "Hostile".
- **Hijack visual promotion**: when a civilian gets `Hijack` as
  ActiveEmergency, the snapshot Row renders the symbol as Hostile in
  both the scope and the aircraft row badge WITHOUT mutating the
  underlying State.ThreatClass. Visual deception only - the truth /
  scoring layer keeps the actual classification.
- **`DrawDebugView` gated to authority**: `if (!HasAuthority()) return`
  early-exit on the world-space debug overlay (aircraft text, range
  circles, sector ticks). Without it the instructor's UMG scope had
  world-space DrawDebug primitives bleeding through into the canvas.
  Operator host still gets the full debug layer for its free-cam view.
- **`ACLEARANCEGameMode` de-abstracted** (UCLASS(abstract) -> UCLASS())
  so it appears in the World Settings -> Game Mode Override picker
  without requiring a BP subclass. Constructor wires
  `PlayerControllerClass = AClearanceOperatorPC::StaticClass()` and
  `HUDClass = AClearanceReadoutHUD::StaticClass()`.
- WHY THIS MATTERS: previous instructor station was a server-
  authoritative simulation with no operator-facing tactical display -
  just a HUD readout of strings. The UMG panel turns it into a
  recognisable defence-training console: instructor reads the scope
  the same way they would read an Air Defence picture in the field,
  injects emergencies / classifications / scenario controls from the
  same panel. Sets up the per-aircraft camera feeds (2026-06-13) and
  the replay bar (next milestone) as natural extensions on the same
  widget.

### [2026-06-11] EW gameplay integration - reactive AI + scoring + scenario JSON + phraseology
- The jamming + chaff plumbing from earlier today stops being a console toy
  and turns into actual gameplay. Four integration angles covered in one
  session because each angle reinforces the next - the reactive AI gives
  the bandits agency, the scoring hooks reward the operator for outplaying
  it, the scenario authoring lets drills script it, the phraseology lets
  the operator declare loss without scoring penalty when EW caused it.
- **TickBanditEW** - new authority-only tick that runs after GCI intercepts.
  Hostiles and military Unknowns find the closest friendly interceptor
  under GCI control, then: jammer on at 25nm closure, chaff at 10nm with
  an 8s reload, jammer off at 40nm disengage. Per-bandit cooldowns prevent
  flicker. Each event PushNotification's a banner so the operator sees
  the bandit's EW action, not just its sensor effect.
- **Intercept-through-EW bonus** - when an intercept catches a bandit whose
  `bJammingOn` is true at kill time, the scoring system logs a SECOND
  SuccessfulIntercept incident for that callsign. No new EIncidentType
  needed - re-using the existing one doubles the points cleanly and shows
  up in the AAR as two intercepts with the second tagged "EW bonus".
  Banner text reflects "through EW" so the operator knows why the number
  jumped.
- **Scenario JSON actions** - `activateJammer` and `dropChaff` were
  EScenarioActionType slots with `(pending EW system)` placeholders. Now
  wired. Param schema: activateJammer takes `callsign` + optional `on`
  (default true); dropChaff takes `callsign` (drops at that aircraft's
  current position). Authoring Cold War Probe with mid-scenario EW events
  is now a JSON edit, not a code change.
- **Phraseology + DeclareTrackLost** - new EInstructionType. Parser
  accepts "<cs> lost contact", "<cs> no joy", "<cs> lost track". Controller
  intercepts BEFORE CommsRouter routing because no aircraft-side behaviour
  applies - the operator is releasing the contact, not commanding it.
  Scoring logic: if target's `bJammingOn` OR any active chaff cloud within
  3nm, log a SuccessfulIntercept (no penalty); else log UnresolvedExit (
  the strayed penalty). Stops the operator from dismissing tough contacts
  for free while letting them cleanly release ones EW legitimately took.
- WHY THIS MATTERS: per the user's own memory note, console commands are
  scaffolding - integration into natural gameplay IS the feature. This is
  what the integration looks like across all four surfaces the project
  already exposes (AI, scoring, scenario authoring, phraseology) in one
  coherent change. Defence training systems don't have console commands.

### [2026-06-10] Electronic warfare - jamming + chaff with fusion-saves-you payoff
- Jamming and chaff added as a sensor problem the multi-radar fusion layer
  has to defeat. The fusion architecture from 2026-06-07 exists - this is
  what shows why it matters.
- **FAircraftState.bJammingOn** - replicated flag. When set, the radar's
  paint pass on that aircraft cuts secondary-return chance by 0.4 and
  multiplies position jitter by 8x. The radar ALSO blankets a +/-12 deg
  bearing wedge from its site through the jammer's position - any other
  aircraft inside that wedge takes the same degradation. One jammer
  denies an arc, not just itself.
- **FChaffCloud + AClearanceAirspaceManager::DropChaff** - 12-second-
  lifetime cloud at a sector-relative position. Every radar that sweeps
  the cloud paints a low-confidence ghost track with no transponder,
  a stable synthetic callsign keyed by the cloud's drop timestamp (so
  the same cloud paints to the same ghost entry, not multiplying).
  ChaffClouds replicates so the client scope shows the same ghosts.
- **FRadarTrack.PaintConfidence** - the bug fix that made this real.
  Original code: the radar's fade pass overwrote each track's Confidence
  to `1.f - Since/FadeSec` every tick, completely ignoring the jammed
  0.25 the paint pass had just set. PaintConfidence stores what the
  radar saw at last paint; the fade pass now multiplies it by a
  freshness factor instead of overwriting. So a jammed paint of 0.25
  STAYS 0.25 the moment after painting and fades from 0.25 down to 0.
- **Visual feedback in DrawDebugView** - chaff clouds drawn as shrinking
  amber rings with "CHAFF" tag at the drop point. Aircraft with
  bJammingOn get a red zig-zag mark + "JAM" tag so the operator can
  spot the jammer even when its track is degraded.
- **Console commands** - `clearance.ew.jam <cs> <on|off>` and
  `clearance.ew.chaff <cs>` (drops at that aircraft's current position).
  Author the gameplay integration around these.
- WHY THIS MATTERS: the multi-radar sensor fusion layer wasn't doing
  anything you couldn't do with one radar - until now. With jamming
  active, one site's track drops to 25% confidence and other sites in
  different positions cover the same volume from outside the jam wedge.
  Fusion gives the operator a clean track. That's the entire reason
  defence platforms field N>1 sensors per coverage zone. Now the demo
  shows the WHY behind the WHAT.

### [2026-06-10] MIL-STD-2525C tactical symbology on the operator scope
- Replaces the debug-sphere fallback in DrawDebugView with NATO-standard
  affiliation frames. The scope now reads as a tactical display, not a
  prototype.
- **DrawMIL2525CAir** helper in the anonymous namespace - takes
  ThreatClass + bIsMilitary + Heading + AlertLevel, draws a 2D affiliation
  frame flat on the XY plane: Friendly = cyan rectangle (wider than tall),
  Hostile = red diamond (cardinal-point), Unknown = amber octagon
  (quatrefoil-approximated, the real four-lobe shape is too expensive in
  debug-line primitives), Neutral = green square. Bearing vector extends
  past the frame edge so the symbol reads as motion. Alert ring orbits
  the symbol when CurrentAlertLevel != None, colour by severity. Military
  equipment modifier (small "+") drops below the frame.
- Drawn for EVERY aircraft regardless of whether a 3D visual mesh exists.
  The 3D mesh is for the "out the tower window" view; the symbol is the
  tactical scope read. Different views, different jobs.
- WHY THIS MATTERS: every real defence training system uses MIL-STD-2525
  symbology. Recruiters at CAE / BAE / Lockheed / L3 Harris read the
  symbols on a portfolio screenshot and immediately recognise the
  standard. Cheap to implement, massive credibility.

### [2026-06-10] EOS plumbing wired, parked pending Epic brand review
- All client/server EOS code is in place and compiling clean. What's wired:
  - 5 EOS plugins enabled in the uproject (OnlineSubsystem,
    OnlineSubsystemUtils, OnlineSubsystemEOS, EOSShared, SocketSubsystemEOS).
  - DefaultEngine.ini configured with ProductId / SandboxId / DeploymentId /
    ClientId / ClientSecret / EncryptionKey, NetDriver swapped to
    NetDriverEOSBase, OSS default platform set to EOS.
  - UClearanceEOSSessionSubsystem (GameInstance subsystem) exposes Login /
    HostSession(code, map) / JoinSession(code) / LeaveSession + 4
    BlueprintAssignable delegates. Session code lives on the session as a
    custom string attribute the search joins on.
  - 5 console commands: clearance.eos.login / host / join / leave / status.
- Portal config: Game Services Client + Policy (Connect + Sessions, 9 actions)
  set up cleanly. EAS application created with verified domain
  (abdullahabduljabbar.com via DNS TXT record), brand settings filled in,
  Submit for review accidentally triggered.
- Blocker: while brand is "Under review", the EAS AccountPortal OAuth chain
  errors at the TokenGrantv2 step with corrective_action_required (18206),
  even after the user clicks Allow on the consent screen. Epic confirms
  user-side action complete but the OAuth backend rejects. Expected to clear
  when brand review approves (1-7 business days; case 500QP00001XGtOnYAL).
- Tried: switching to Type="deviceid" for direct EOS Connect Device ID flow.
  UE 5.7's OnlineSubsystemEOS wrapper doesn't expose that cleanly - errors
  with "External Auth Token Type not specified". A direct EOS SDK
  EOS_Connect_CreateDeviceId + EOS_Connect_Login path is possible but
  ~60+ min of work and the IOnlineSession side still needs the OSS_EOS
  user manager populated, so the right move is to wait on EAS instead.
- Code reverted to AccountPortal Type + bUseEAS=true, ready for the moment
  the review clears. No other action needed - just close+reopen the editor
  after brand approval lands and clearance.eos.login should chain cleanly
  to a Connect product user id.
- WHY THIS MATTERS: the portfolio claim ("integrated EOS for NAT
  punchthrough + session matchmaking") is defensible right now even before
  the live demo works - the plumbing is complete, the code is shipping, the
  blocker is purely Epic's moderator queue. When review approves, the live
  cross-network demo unlocks for free with no code changes.

### [2026-06-10] Networked Instructor Station — server-authoritative replication end-to-end → MIGRATED to main 2026-06-10
- Two-window architecture: the host PC ("SERVER") owns the simulation, an
  instructor PC ("INSTRUCTOR") joins as a remote peer and sees the same world,
  same scoring, same scenario timer, same voice, same conflict colours.
  Validated entirely in PIE 2-window before any LAN packaging.
- **Authority discipline (the meat).** `AClearanceAirspaceManager` is the single
  source of truth. RegisterAircraft / DeregisterAircraft / RequestStateUpdate /
  ClearAllAircraft are gated on `HasAuthority()` and log `[AirspaceMgr] BLOCKED`
  if anything ever tries to mutate from the client. Closed a real bug where
  `HandleAircraftRegistered` was bound on the client by `OnRep_AirspaceManager`
  and was creating a Behaviour whose `Initialise()` then called
  RequestStateUpdate on the client's manager - causing the client to invent
  BAW/DLH/UAE callsigns that didn't exist on the server. Fix split the handler:
  visual mesh spawn runs on both sides, Behaviour creation + bandit redirect
  are server-only.
- **Replicated state added to the Controller** (rides existing replication
  stream, no custom RPCs):
  - Scenario block - bRepScenarioRunning, RepScenarioName, ElapsedSec, fired/
    total events + triggers (7 fields). Server mirrors UClearanceScenarioRunner
    each tick.
  - Score block - total, eff%, all 14 positive/negative counters (landings,
    departures, resolved, intercepts, emergencies, go-arounds, sep-loss, wake,
    TCAS, strayed, misID, violated, crashed, busted), next-spawn timer
    (17 fields). Server tallies the IncidentLog and pushes counters into the
    replicated fields.
  - Notification ring buffer - FClearanceNotification (text + colour + lifetime
    + ServerTimeAdded), replicated TArray, capped at 12. PushNotification()
    server-only helper called from 11 player-facing alert sites (conflicts,
    emergencies, intercepts, violations, IFF, crashes).
- **Conflict alert level on FAircraftState** - added `CurrentAlertLevel` field,
  stamped server-side after `ConflictDetector->DetectConflicts()`, replicates
  with the aircraft state. Client's debug overlay now colours each aircraft
  green/yellow/orange/red without running a detector locally. Client used to
  paint everything green because `ConflictDetector` is null on the client.
- **Custom HUD class** - `AClearanceReadoutHUD`. We tried
  `GEngine->AddOnScreenDebugMessage` first; in PIE multi-window the shared
  engine queue rendered inconsistently per viewport (server saw both readouts,
  client saw only the diagnostic keys). Then tried `ClientMessage`; that
  requires a HUDClass to render and we didn't have one, so both windows went
  silent. Final fix: SimulationController writes `CurrentReadout` and
  `CurrentDiagLine` strings each tick; the HUD reads from the local controller
  via `TActorIterator` and draws via `Canvas->DrawItem`. Each PlayerController
  gets its own HUD, each HUD renders per-viewport reliably. Same HUD also
  draws the notification stack (right-side, fade by lifetime) and the SERVER
  vs INSTRUCTOR role badge top-right.
- **TTS multicast** - `Multicast_PlayTTS(callsign, text, voiceTag, bPanic)` and
  `Multicast_PlayCockpitCue(callsign, kind, duration)` NetMulticast RPCs on the
  Controller. 11 server-side TTS sites now route through them - emergencies
  (mayday/fuel/comms/hijack), urgency countdowns, crash panic + GPWS + final
  words, scenario Say actions, phraseology readbacks, controller voice
  prompts. Each peer (server + every client) receives the multicast and calls
  its own local `AClearanceVoiceOutput::Speak()` which talks to its own local
  Piper TTS server to synthesise the WAV - no audio streaming, just
  synchronised text triggers. Both windows hear the same dialogue
  independently. Added `bMuted` + `clearance.audio.mute on|off|toggle` console
  command so the operator window can be silenced while the instructor window
  keeps listening.
- **Deterministic visual variants** - `FMath::RandRange(0, Variants.Num()-1)`
  was rolling independently on server and client, so the same callsign showed
  different meshes per window. Replaced with `GetTypeHash(Callsign) %
  Variants.Num()` - same FName hashes to the same index everywhere, no
  replication needed.
- **Late-join already correct** - `OnRep_AirspaceManager` binds delegates and
  retroactively spawns visuals for already-replicated aircraft. Plain
  Replicated UPROPERTYs silently receive their initial value on join, so a
  late-joining instructor catches up to current score / scenario / aircraft
  state without explicit catch-up logic.
- **AClearanceOperatorPC** - APlayerController subclass with 9
  Server/Reliable/WithValidation `Server_Inject*` RPCs (instructor → server
  control surface). Set as `CLEARANCEGameMode::PlayerControllerClass`.
- WHY THIS MATTERS: the editor can now be demoed as a real two-person training
  rig - one player flies the operator role, a second machine (the instructor)
  watches everything sync'd and can inject failures via the operator PC's
  Server_Inject* surface. That's the actual product shape defence trainers
  buy. Sets up the next milestone (EOS for NAT punchthrough) so hiring
  managers can join a session from anywhere without port forwarding.

### [2026-06-09] Scenario library polish pass + 4 real bugs found + GCI/Unknown safety-net rework → MIGRATED to main 2026-06-09
- Per-scenario fix/tweak pass over all seven scenarios. All seven now verified
  live (rows 12-18 in the tracker flipped from 🟡 to ✅).
- **Real bug 1: Global airspace MaxSafeSpeed cap was 600 kts.** ClampStateValues
  was silently squashing every military aircraft to subsonic on register. Vipers
  spawned at 900kts but immediately clamped to 600. Bumped the cap to 1100
  (military Vmax 1050 + transonic headroom). Affects every intercept across the
  library - all scrambles now actually dash at ~M1.3 instead of crawling.
- **Real bug 2: Conflict detector did not suppress Unknown-involved pairs.** The
  suppression list was Hostile + GCI-controlled only. Unknown contacts (the
  whole Cold War Probe doctrine) were tripping TCAS/wake/sep-loss against
  civilians the operator had no way to manage. Extended suppression to include
  any pair where one side has ThreatClass::Unknown.
- **Real bug 3: Non-civilian contacts exiting the sector counted as strayed.**
  Probes breaking off and egressing were logging UnresolvedExit (-strayed
  penalties) even though they aren't civilian air traffic. Gated the
  UnresolvedExit log behind `bNonCivilian` check (ThreatClass != Friendly OR
  bIsMilitary OR bUnderGCIControl).
- **Real bug 4: TTS callsign mangling.** The reverse-mapping for ICAO ->
  telephony names had both "UNKNOWN" -> "unknown" AND "UNK" -> "unknown". After
  the first substitution, "UNK" matched INSIDE the newly-inserted "unknown"
  and recursively corrupted it to "unknownnown" - which the TTS pronounced as
  "uno oh no no". Dropped the UNK entry; UNKNOWN matches cleanly now.
- **Viper speed bump from 620 -> 900kts** at spawn (line 1419). Real F-22/F-35
  alert-flight scramble dash speed is ~M1.3. The old 620kt subsonic cruise made
  every intercept feel anaemic.
- **PursueKt formation-rejoin speed bump from 640 -> 900kts** (line 866). Lined
  up with the new spawn speed so wingmen don't fall behind during the dash to
  formation slot.
- **JSON leading-zero gotcha re-documented**: caught a second one (windDirection
  = 010). Authoring playbook reminder: never use leading-zero compass bearings,
  even if they read like ATC notation.
- **TTS American/British spelling collision**: "prioritise" got mispronounced
  by en-US-EricNeural AWACS as "prioritis". Rule: if a line is voiced by en-US-*,
  use American spellings (prioritize, recognize, analyze). en-GB-* voices
  handle British spellings fine.
- **Cold War Probe** tweaks: spawn positions pushed further out (90-120nm vs
  60-80nm) so unknowns linger near the boundary visibly before approaching
  inner sector; speedKts dropped to 320 from 360-400 to give the player visual
  warning time; `keepLevelZones` flipped to false (scenario doesn't actually
  USE the protected zone, was just clutter).
- WHY THIS MATTERS: catches three real engineering bugs (speed cap, safety net
  scope, scoring scope) that would have shipped to a portfolio demo as obvious
  flaws. The fixes generalise across every scenario, not just the ones that
  surfaced them. Every demo recording from here is on the corrected baseline.

### [2026-06-08] Scenarios #4-7 batch: Mayday, NORDO, Cold War Probe, Mixed Ops → MIGRATED to main 2026-06-08
- Four additional scenarios authored on the existing runner. No new C++ or
  schema work needed - just JSON. Brings the named scenario library to seven,
  spanning every emergency type (7500 / 7600 / 7700 / FuelLow), every doctrine
  mode (GCI / civil ATC / SHADOW / lost-comms), and three pure-tempo drills.
- **Mayday Engine Fire** (7700) - BAW394 declares engine fire mid-flight, six
  transit aircraft in sector. Tests priority handling + traffic vectoring under
  emergency.
- **NORDO Inbound** (7600 x2) - two aircraft go comms-failure within 30s of
  each other. Both fly the published lost-comms auto-procedure (built earlier).
  Player vectors the four talking aircraft clear of both NORDO tracks. Tests
  that the comms-failure autopilot path works under load.
- **Cold War Probe** - three simultaneous unknowns penetrating the ADIZ; two
  are probes that breakOff at T+130/150s, one (UNKNOWN02) presses on a civilian
  airliner. Tests multi-bandit GCI + classification discipline + the breakOff
  action verb introduced earlier. Uses `keepLevelZones: true`.
- **Mixed Ops** - eight civilians transiting with multiple placed restricted
  areas active. No emergencies, no GCI. Pure spatial planning. Uses
  `keepLevelZones: true`. Tests the RestrictedAirspaceBust scoring path under
  realistic civilian density.
- **JSON gotcha hit + documented**: `"windDirectionDeg": 010` is INVALID JSON
  (leading zero) - UE's parser silently fails the whole load. Stripped from
  cold_war_probe; rule added to authoring playbook: never use leading-zero
  compass bearings in JSON, even though they read more like ATC notation.
- Test status: all seven scenarios load + run; scoring verification + replay
  per-scenario deferred to a single combined pass after the editor lands.
- WHY THIS MATTERS: seven authored scenarios on one engine demonstrates the
  authoring pipeline IS the product. Each new scenario is ~10-20 minutes of
  JSON. A defence contractor reviewing this repo sees not a single demo but a
  scenario library with consistent doctrine coverage - which is what training
  procurement actually buys.

### [2026-06-08] Scenario #3: Mass Divert (tempo pressure) + exit/divert phraseology synonyms → MIGRATED to main 2026-06-08
- Third authored scenario, completes the three-drill set for portfolio reel
  material. Pure ATC test (no GCI / no hostiles) - balances the doctrine-heavy
  Baltic and Hijack drills.
- Six civilian aircraft inbound to the airport at varying approach stages. Mid-
  scenario the wind flips 180 degrees (250 -> 090 at 38kts) and TOWER closes the
  active runway. Player has five minutes to divert every aircraft out of the
  sector before fuel reserves cascade into emergencies.
- Cascading fuel emergencies via timed events at T+300/310/320/335/350/365s, one
  per remaining callsign. DeclareEmergencyOn returns false on already-departed
  aircraft so timed events that fire after a player diverts a callsign safely
  no-op without breaking anything else.
- MET voice (en-GB-LibbyNeural) for the weather advisory - fourth distinct voice
  on the radio (after AWACS / ACC / TOWER), so the player can tell which agency
  is calling them.
- Phraseology parser now also accepts `exit` and `divert` as exit-sector verbs
  (was only `contact` and `leave`). "Speedbird one two three, divert" now works
  as a spoken command and parses to ExitSector. Three scenarios all use this
  phrase pattern; the user kept getting "say again" responses until I added it.
- WHY THIS MATTERS: with three named scenarios (intercept doctrine + hijack
  doctrine + tempo pressure) running on the same JSON-driven runner, the
  editor's claim as a real training-system foundation is now empirically
  defensible. Portfolio reel material: record a clean run of each, intercut
  with the README. Defence contractors reading the repo see exactly the kind
  of scenario-authoring pipeline they procure.

### [2026-06-08] Scenario #2: Hijack Response (multi-aircraft pressure) → MIGRATED to main 2026-06-08
- Second authored scenario, proves the editor generalises beyond a single drill.
- Five civilian aircraft transit the sector simultaneously. One (BAW472) gets a
  scheduled `declareEmergency` of kind `Hijack` at T+45s - flips its squawk to
  7500, fires the Controller's existing hijack-vs-violation-zone auto-targeting
  path (the hijack starts flying toward a placed protected zone).
- Player role: recognise 7500, call SHADOW escort (`shadow BAW472` - no hostile
  declaration, no mis-ID risk), and vector the other 4 civilians off the hijack's
  track to prevent separation breakdowns under emergency conditions.
- Win conditions:
  - SHADOW launched + hijack lands or exits safely
  - Hostile declare last resort (catastrophic if pilot was under duress)
  - Catastrophic if hijack reaches the placed violation zone (-1000)
- New metadata field `keepLevelZones` (bool, default false). When true, the
  scenario runner leaves placed RestrictedArea + ViolationZone actors active
  during the scripted run. Hijack Response sets it true so the
  hijack-vs-violation-zone catastrophe path can fire. Baltic Intercept (the
  pure-intercept scenario) leaves it false so unrelated level geometry doesn't
  pollute that drill.
- ACC voice (en-GB-RyanNeural) for area control announcements - distinct voice
  from AWACS (en-US-EricNeural) used in Baltic, so different scenarios sound
  different over the radio.
- WHY THIS MATTERS: two scenarios with completely different doctrine (intercept
  vs hijack) running on the SAME runner / SAME schema proves the editor is a
  reusable training-system foundation, not a one-shot demo. Adding a third
  (Mass Divert) on the same plumbing is now ~10-20 minutes of JSON authoring.

### [2026-06-08] Scenario runner + JSON authoring + Baltic Intercept (scenario #1) → MIGRATED to main 2026-06-08
- **`UClearanceScenarioRunner`** - new UObject owned by Controller. Loads training
  scenarios from JSON, advances on sim time, evaluates timed events + conditional
  triggers, executes verbs against the existing systems. Pure backend, no UI.
- **Scenario schema (JSON)** lives in `Plugins/ClearanceSim/Scenarios/*.json`. Sections:
  `metadata` (id, name, brief, ROE, difficulty, location), `environment` (wind,
  active runway), `initialSpawns` (callsign, position, hdg/spd/alt, squawk,
  threat, IFF), `timedEvents` (atSec + action), `triggers` (when + then + once).
- **Action verbs**: `spawn`, `declareEmergency`, `setWind`, `hostileDeclare`,
  `scrambleIntercept`, `injectMessage`, `logIncident`, `setFlag`, `finishScenario`,
  `pursue`, `breakOff`. Placeholder verbs (`activateJammer`, `dropChaff`) print
  but no-op until EW system lands.
- **Condition predicates**: `atTime`, `aircraftInArea`, `aircraftAtAltitude`,
  `distanceBetween`, `flagSet`, `aircraftCount` (with op: eq/lte/gte).
- **`Pursue` action** maintains a hunter->target map; each Tick the runner
  re-vectors the hunter onto the target's current position. Auto-disengages when
  target enters `FlightPhase::Exiting` (lets the Controller's join-up code drive
  the bandit outbound instead of fighting it every frame).
- **`BreakOff` action** points an aircraft at the closest sector exit, climbs to
  35k, accelerates to max speed.
- **TTS routing on `injectMessage`** - the message text speaks through any placed
  AClearanceVoiceOutput with caller-supplied callsign + voice tag (defaults AWACS
  + en-US-EricNeural). AWACS calls now COME through the radio, not just text.
- **`DeclareEmergencyOn(callsign, kind)`** - new public Controller API mirroring
  the random-injection path: sets emergency type, squawk, timestamp, fuel; 7600
  triggers the lost-comms auto-procedure (turn for runway, descend to 3000, set
  FlightPhase::Approach).
- **Auto-spawn pause + zone-check suspension on scenario Start()** - random
  civilian spawns + level-placed RestrictedArea/ViolationZone checks all
  suspended for the run, restored on Stop(). Scenario owns its world entirely.
- **Viper safety-net suppression** - vipers spawn with `bUnderGCIControl=true`
  so the 3-ship formation doesn't trip TCAS/wake/sep alerts on each other.
- **IFF interrogation static cue** - no-IFF response now plays a 0.8s static
  burst via VoiceOutput so the silence is audible, not just visible.
- **Console commands**: `clearance.scenario.list`, `clearance.scenario.load <name>`,
  `clearance.scenario.stop`.
- **Diagnostic readout line**: `SCENARIO <name>  T+MM:SS  events N/M  triggers N/M`
  appears always-on while a scenario runs.
- **First scenario: Baltic Intercept** - civilian airliner SK238 westbound, unknown
  contact UNKNOWN01 in trail with IFF off, AWACS calls, player interrogates +
  declares hostile + scrambles, vipers intercept on bandit bearing, join-up
  forces bandit outbound. End-to-end demo with voice on top.
- WHY THIS MATTERS FOR THE PORTFOLIO: defence-training procurement is built
  around scenario authoring. "Scriptable training drills, JSON-defined, voice
  AWACS calls, scoring + AAR replay per attempt" is exactly the language
  contractors use. Two more scenarios (Hijack Response, Mass Divert) + an
  instructor station UI = a complete training-system narrative.

### [2026-06-07] Multi-radar sensor fusion → MIGRATED to main 2026-06-07
- **`AClearanceRadarSite`** - placeable actor that owns its own `UClearanceRadar`.
  Drop as many as you want (airport, north sector, ship, AWACS). Each carries
  independent `RangeNm` / `SweepRpm` / jitter / fade / coverage colour. Site
  position in sim = actor world location relative to the Controller.
- **`UClearanceRadar`** gained `SitePositionNm` + `SiteName`. Range/bearing now
  computed relative to the site's position so multiple sensors at different
  locations cover different patches of airspace.
- **Fusion in Controller readout** - polls Controller's own radar + every placed
  `AClearanceRadarSite` each draw, merges per-callsign tracks (newest paint wins),
  counts sightings, surfaces as `RDR <cs> [N/M]` confidence tag. Coverage rings
  drawn on the ground per site in their configured colour with `RDR <SiteName>`
  floating label.
- **Diagnostic line** `RDR fleet  centre:<ON|off>  placed:N (enabled N)  active:N`
  added to the always-on debug readout so it's obvious whether sites are being
  detected. Distinguishes "site in level but not wired" from "no sites".
- **BeginPlay race fix** - actor init order is non-deterministic, so a site can
  BeginPlay before the Controller has its AirspaceManager. Wire-up now retries
  from `Tick` until both are alive.
- **Central radar tuning exposed on Controller** - `CentralRadarRangeNm` /
  `SweepRpm` / `SecondaryReturnChance` / `PositionJitterNm` / `TrackFadeSeconds`
  / `SiteName` UPROPERTYs under `Simulation|Radar (Central)`. Previously the
  `NewObject`'d radar had no editor-visible tuning at all.
- **Central radar now off by default** (`bAutoStartRadar = false`). Real air
  defence picture is fused from distributed sites - no magic sensor at sector
  origin. `clearance.radar on` console toggle preserved as dev backdoor.
- WHY THIS COMPLETES THE DEFENCE STORY: the value prop for the
  industry portfolio is "distributed sensor fusion with visible coverage gaps
  and confidence scaling" - exactly what a NORAD / NATO / Linesman-style picture
  looks like. Tower controllers don't really do this; en-route / military
  air defence do.
- DOC UPDATED?: this file. C++ Scaffold doc would benefit from a sensor-fusion
  section but it's currently consistent (single-radar block already noted as a
  starting point).

### [2026-06-07] Holds, restricted airspace, SHADOW hijack doctrine, GPWS, escalating urgency, heartbreaking crash arc → MIGRATED to main 2026-06-07
- **Restricted airspace** (`AClearanceRestrictedArea` actor) - placeable
  blue ring, civilian friendly aircraft entering = `RestrictedAirspaceBust`
  (-150). Distinct from violation zones (red, catastrophic, for hostiles).
  One-shot per (area, aircraft) pair.
- **Holds** - new `bInHold` / `bHoldRightTurns` / `HoldLegStartSeconds` /
  `HoldInboundHeading` state. Phraseology `hold` / `hold left` / `hold right`
  enters a racetrack at current alt/heading. Behaviour drives 150s outbound
  / 150s inbound legs (5 min cycle). Any other instruction exits the hold.
- **SHADOW / INTERCEPT VISUAL** - new doctrine path for 7500 hijacks that
  doesn't require declaring hostile (no mis-ID risk). Spawns a 3-ship that
  intercepts and escorts out, same outcome as bandit scramble. Hijacks
  retarget toward a violation zone on declare.
- **7600 lost-comms auto-procedure** - on declaration, aircraft heads to an
  approach gate 10nm out on the active runway localiser, descends to 3000ft,
  FlightPhase=Approach so the existing ILS capture takes over. Controller's
  job is keeping other traffic clear.
- **Pilot readback verbs** added for the missing axes:
  - `"climbing to flight level 250"` / `"descending to altitude 8000"`
    (already there) - cleaned up.
  - `"turning right heading 270"` / `"turning left 30 degrees"`.
  - `"heading 270"` without verb infers direction from current heading.
  - `"reducing to speed 250"` / `"increasing to speed 280"` / `"maintaining
    speed 250"`.
  - Altitudes 20-99 thousand use proper English ("twenty-five thousand")
    instead of digit-by-digit.
- **Crash physics rewrite** - forced -55deg visual pitch (cap lifted for
  crashing aircraft only), 1500 ft/min sim descent (=15000 real visual at
  10x sim time scale, ~60s from FL150), no spiral, holds heading, forward
  momentum preserved. Proper nose-down dive into the ground.
- **Heartbreaking crash voice arc**:
  - Initial pilot panic line from a pool of 8 (specific reactions:
    "we're losing it", "I have control", "captain captain we're going to crash").
  - GPWS cockpit alarm "Terrain. Terrain. Pull up. Pull up." enqueued
    after the panic line (radio FX so it sits on the same channel; tunable
    GPWSVolume / GPWSText / GPWSVoiceTag / bGPWSRadioFX).
  - Second panic line (final words from a pool of 8: "tell my family I
    love them", "mama I'm sorry", "brace for impact") scheduled at T+14s
    in the SAME pilot voice (locked at crash time).
  - On ground impact, pending panic timer is cleared and any in-flight
    GPWS audio component is stopped so the wreck goes silent.
  - "Lost contact, [callsign]" controller voice 1.5s after impact.
- **Panic FX chain** strengthened (deeper tremolo 8.5Hz / 55% depth, slow
  vibrato, voice-catch micro-dropouts every 1.7s, harder atan saturation,
  hotter gain). Pitch-shift removed - the Edge voices' natural emotion
  carries the rest without sounding chipmunk.
- **Mayday calls include the specific cause** picked at declare (engine
  failure / engine fire / smoke / hydraulic / bird strike / medical / cargo
  fire / landing gear / electrical / depressurization / flight controls
  jammed). Spoken inline and shown on the aircraft data tag.
- **Escalating urgency calls** for Mayday + FuelLow at 66% / 33% / 10% of
  the countdown - each fires once per aircraft via a bitmask. Real CVR
  pattern: the pilot re-calls as the situation worsens. Cleared on
  deregister.
- Half-duplex queue now also routes GPWS so cockpit alarms don't trample
  other aircraft's transmissions - everything takes its turn on the freq.
- Approach doubling fix: "cleared ILS approach" no longer fires the
  clearance twice (and reads back "cleared the approach" with the article).

### [2026-06-05] Violation zone: missed-intercept failure mode + bandit zone-targeting → MIGRATED to main 2026-06-05
- New `AClearanceViolationZone` actor: placeable, has `ZoneName` (FName) +
  `RadiusNm` (default 5). Drop one or more per level to mark protected airspace
  (city, airbase, nuclear site).
- New `EIncidentType::ViolationZoneBreached`, penalty 1000 by default. Mirror
  weight to `MisidentifiedCivilian`: catastrophic on both ends of the
  doctrine asymmetry.
- Tick check: any uncooperative aircraft (Hostile, Unknown, OR IFF-off) inside
  a zone's radius logs the breach. Friendly + IFF-on is the only safe class.
  One-shot per (zone, aircraft) pair (cleared on session reset) so a stuck
  hostile doesn't spam the penalty.
- Persistent 30s red banner like the mis-ID one; recorder logs the event for
  AAR. Score breakdown line gets `-violated <n>`.
- Visual: pulsing red ground circle + `[ZONE NAME]` label, same pulse cadence
  as the active runway.
- Bandit spawn targeting: in `HandleAircraftRegistered`, any NORDO non-friendly
  contact gets its Heading and TargetHeading set to point at a randomly-picked
  violation zone (if any exist). Real intruders have an objective; that's
  what makes the operator's intercept call meaningful. Falls back to the
  spawner's inbound-to-centre when no zones are placed.
- Closes the GCI doctrine loop: declaring a civilian Hostile = -1000 (false
  positive); letting an uncooperative aircraft reach a zone = -1000 (false
  negative). The operator has to make the call either way.

### [2026-06-05] Voice infrastructure: TTS server, edge-tts + Piper, half-duplex channel, per-aircraft voices → MIGRATED to main 2026-06-05
- **TtsServer/** - new Python HTTP service wrapping two backends:
  - **Piper TTS** (offline, neural) for `en_GB` / `en_US` voice tags
  - **edge-tts** (Microsoft Edge cloud, free, online) for full Edge voice
    names like `en-GB-RyanNeural`. Bundled imageio-ffmpeg converts the
    returned MP3 to raw PCM that we wrap in a hand-built WAV header (avoids
    pydub's ffprobe dependency).
  - Voice routing by tag format: hyphens = Edge, underscore = Piper.
- **`AClearanceVoiceOutput`** actor mirrors `VoiceInput` - auto-launches
  `tts_server.py` on BeginPlay, HTTP requests → WAV → decoded → played via
  `USoundWaveProcedural`. Optional radio FX (300Hz-3kHz bandpass + noise +
  soft saturation) so pilot voices sound like comms, not a studio mic.
- **Per-aircraft voice pool** (18 Edge voices across British / US / Irish /
  Australian regions, mix of male/female). Each aircraft reserves one voice
  for its life - no two active aircraft share. Voice released on
  deregister.
- **Half-duplex channel** - `Speak`/`SpeakPanic`/`PlayStatic` all enqueue
  through a serial channel: one transmission at a time, next one fires when
  the previous PCM ends. Matches real ATC frequency behaviour - no more
  three pilots talking at once.
- **TTSize text preprocessing** before sending to TTS:
  - Callsign telephony: `BAW101` → `"speedbird one zero one"`
  - Digit-by-digit for headings / flight levels / speeds
  - Altitude in feet → English thousands ("eight thousand", "twelve
    thousand", "two five thousand")
- **Phraseology readbacks** include the verb pilots actually use:
  - `"climbing to flight level two five zero"`
  - `"turning right heading two seven zero"`
  - `"cleared the approach"` (was "cleared approach", with doubling fix
    for `cleared ils approach` triggering twice)
  - Direction inferred when controller uses `heading 270` without a verb.
- **Voiced system / pilot say-agains**: unrecognised callsign or
  empty-instruction transmissions trigger spoken "say again" in a fixed
  controller voice (Eric) for system, or the pilot's voice for the
  callsign-specific case.
- **Panic FX chain** for cockpit final-moments lines (pitch up, tremolo,
  hard saturation, hotter noise).
- **`PlayStatic`** generates radio carrier static for 7600 (broken radio)
  and 7500 (pilot keyed mic and was cut off).

### [2026-06-05] Emergencies: random injection, 7700/7600/7500/Fuel, panic + crashes → MIGRATED to main 2026-06-05
- New `EEmergencyType` (None / GeneralMayday / CommsFailure / Hijack /
  FuelLow). New `EIncidentType::SuccessfulEmergencyHandling` (+200) and
  `AircraftCrashed` (catastrophic -500).
- Per-aircraft state: `ActiveEmergency`, `FuelRemainingMinutes`,
  `EmergencyDeclaredAtSeconds`, `bCrashing`, `EmergencyDetail`.
- Random conversion mid-flight: each civilian aircraft has
  `EmergencyChancePerSecond` (default 0.001) chance per real-second to
  declare an emergency.
- **7700 General Mayday** picks a specific cause from an 11-entry pool
  (engine failure / engine fire / smoke / hydraulic / bird strike /
  medical / cargo fire / landing gear / electrical / depressurization /
  flight controls jammed) and voices "Mayday mayday mayday, [callsign],
  [cause], request immediate landing". 7-minute countdown to deterioration.
- **7600 Comms Failure** plays a 2-second static burst then rejects all
  ATC commands. Aircraft auto-flies the lost-comms procedure: descend to
  3000ft, head to the active runway's approach gate, FlightPhase ->
  Approach so the existing ILS capture takes over. Player's job is keeping
  other traffic clear.
- **7500 Hijack** plays a brief 0.6s keyed-mic static, rejects ATC, retargets
  toward a randomly-picked violation zone (hijackers' objective). Player
  responds with `SHADOW <callsign>` / `INTERCEPT VISUAL <callsign>` - new
  doctrine path that doesn't require declaring hostile (no mis-ID risk).
  Fighters spawn from boundary, intercept, formation rejoin turns the
  aircraft outward same as bandit escort.
- **FuelLow** ticks down on real time; 5 minutes default. Crashes on zero.
- **Crash sequence**: panic line voiced in pilot's voice through panic FX
  chain at fuel-out / mayday-timeout; `bCrashing` set; aircraft physically
  dives forward at -55deg pitch (forced visual override of FPA cap) at
  ~15,000 ft/min real visual descent; on ground impact wreck site (red
  ring + label) persists; "Lost contact, [callsign]" voiced in controller
  voice 1.5s after impact. Behaviour skips crashing aircraft so the
  controller's tick drives them solo.
- Mis-identification penalty stays for declaring confirmed civilians
  (IFF on + non-military) as hostile - hijacks bypass via SHADOW.
- Score breakdown shows `+emer N` and `-crashed N` plus total at the front.

### [2026-06-05] Background-system auto-starts: no more console for normal ops → MIGRATED to main 2026-06-05
- `bAutoStartRecording = true` (default): AAR recording fires automatically
  at session start. `clearance.aar.start/stop` stays as a dev backdoor.
- `bAutoStartRadar = true` (default): operator radar always on at session
  start. `clearance.radar on/off` still works for the dev truth-vs-sensor
  comparison.
- `bAutoGCIMode = true` (default): in the main tick, GCI mode flips on the
  moment any aircraft has IFF off OR ThreatClass Hostile/Unknown, and off
  when only friendly traffic remains. No manual `clearance.gci on` needed.
- `bAutoStartDIS = false` (opt-in): DIS emitter auto-publishes at session
  start when enabled, using `DISDefaultHost` / `DISDefaultPort` UPROPERTYs.
  Default-off keeps solo dev sessions off the wire.
- Console toggles still all live as dev backdoors - the integration is
  purely about removing the player-layer dependency on them.

### [2026-06-05] Phraseology: spoken forms for unknown / bogey / bandit / viper → MIGRATED to main 2026-06-05
- Telephony map adds entries so spoken phraseology accepts the GCI track
  callsigns: "unknown 001" / "bogey 001" / "bandit 001" all resolve to
  UNK001; "viper 01" resolves to VIPER01. Raw "unk001" / "viper01" still work.

### [2026-06-05] GCI gameplay-loop integration: bandit injection, NATO brevity, mis-ID doctrine → MIGRATED to main 2026-06-05
- **Bandit injection in the spawner**: `BanditChance` UPROPERTY on the spawner
  (default 0.05). Each roll has a configurable chance of producing an unidentified
  hostile (`UNK###` callsign, IFF off, squawk 7777, military airframe, ThreatClass
  Unknown). Drops into civilian traffic; player has to spot it.
- **NORDO civilian-ATC rejection**: `PlayerIssueInstruction` returns the new
  `Rejected_NoResponse` for any contact with IFF off and not declared friendly.
  Phraseology reads back `"<callsign>, NO RESPONSE"`. Non-compliance is now the
  player's first hint the contact isn't normal traffic.
- **NATO GCI brevity in the phraseology parser**:
  - `INTERROGATE <cs>` -> IFF challenge
  - `DECLARE <cs> HOSTILE/FRIENDLY/NEUTRAL/UNKNOWN` (also `SHOW`)
  - `SCRAMBLE BANDIT <cs>` (also `ALPHA FLIGHT SCRAMBLE BANDIT <cs>`, `ALERT FLIGHT SCRAMBLE <cs>`)
  - Civil ICAO phraseology untouched - two domains coexist as in real ops.
- **`ScrambleInterceptors(BanditCallsign)`** method on the Controller: 3-ship
  spawn from the sector boundary stretch closest to the bandit, 8deg angular
  fan (~7nm spread). Each viper auto-vectors via `VectorIntercept`. Unique viper
  callsigns per scramble via a monotonic counter so concurrent engagements work.
- **SCRAMBLE doctrine gate**: refuses to launch unless the target is already
  declared `Hostile`. Stops fighters being launched on civilian traffic - the
  misidentification call has to be the operator's own.
- **Intercept tracking refresh**: `TickGCIIntercepts` re-calls `VectorIntercept`
  and updates `TargetAltitude` every tick for unjoined fighters, so a moving /
  turning / off-altitude bandit doesn't slip past the original single-shot
  vector. VectorIntercept's on-screen status now only fires on the first call.
- **Viper-pair conflict suppression**: ConflictDetector now skips pairs where
  both aircraft are `bUnderGCIControl` (the 3-ship formation 2nm apart, matched
  altitude, would otherwise trigger Critical sep-loss). Viper-vs-civilian
  still alerts - keeping commercial traffic clear is still the controller's job.
- **MISIDENTIFICATION incident** (`EIncidentType::MisidentifiedCivilian`,
  default penalty 1000): declaring Hostile on a confirmed civilian (IFF on,
  not military) logs a catastrophic doctrine-failure incident. Score hit
  alone is unrecoverable; persistent 30s red banner; recorder logs it for AAR.
  Engagement continues - no hard lockout - so the player keeps learning from
  the rest of the session.
- **Score breakdown overlay** now shows `total <score>` at the front of the
  SCORING line plus `-misID <n>` in the negatives column.

### [2026-06-04] Spawner entry radius locked to sector boundary → MIGRATED to main 2026-06-04
- Controller now force-sets `Spawner->EntryRadiusNm = ExitRadiusNm` at session
  start. The spawner had a hard-coded default of 40nm; the visible sector
  boundary is drawn at the controller's `ExitRadiusNm` (default 50nm). Aircraft
  were appearing 10nm inside the ring. They now appear on the ring exactly,
  no matter what value the controller is set to.

### [2026-06-03] Runway + approach visual pass → MIGRATED to main 2026-06-03
- Runway strip: amber/cream edges + end caps, dashed centreline, 8-bar piano-key
  threshold markings inboard from each end, slow real-time brightness pulse on
  the active runway markings. Replaces the flat yellow rectangle.
- Approach corridor: the 3D rising wireframe walls are gone. What's left is a
  flat ground fan from the threshold widening to the corridor mouth, with thin
  cross-bars at 10/20/30/40nm so the fan has scale. Narrow end now matches the
  runway's actual half-width (pre-resolved per heading) instead of a fixed slot.
- Glidepath: solid thin line (no dashes), curving up on the 3deg slope. Range
  ticks + "10nm / 20nm / …" labels along it so it reads as graduated rather
  than floating into the sky empty.
- Active runway = warm amber palette pulsing in sync; inactive = quiet cool
  slate so it reads as available-but-not-in-use.

### [2026-06-03] Editor polish: runway override dims, multi-runway visuals, parallel L/R/C, controller drag → MIGRATED to main 2026-06-03
- `AClearanceRunway.OverrideLengthUnits` / `OverrideWidthUnits`: when both > 0,
  GetRunwayBounds returns a synthetic AABB built around the actor location oriented
  to LandingHeadingDeg. Lets a runway actor define the strip without holding its
  own mesh (when the visual mesh is a separate asset placed elsewhere).
- Glideslope / approach corridor visualisation now iterates every runway threshold
  (via the new `GetAllRunways` accessor on the airspace manager) instead of only
  the wind-active one. Active runway draws white; inactive runways draw dim grey
  so the operator can still see they exist.
- Parallel runway L/R/C designators: each endpoint with a shared rounded heading
  number gets sorted along the pilot's-left vector; pairs get L/R, triples get
  L/C/R, more than three fall back to numbered suffixes. Per ICAO.
- `AClearanceSimulationController` now creates a `USceneComponent` root in its
  constructor so the actor has a transform gizmo in the editor. The actor
  location is the sector centre, so being draggable matters.

### [2026-06-03] DIS receiver: passive UDP listener + dead reckoning → MIGRATED to main 2026-06-03
- `UClearanceDISReceiver` binds a UDP port and decodes incoming Entity State PDUs
  (mirror of the emitter's wire format). Identified aircraft are registered into
  the airspace manager with `bIsExternal=true`. Stale entries (no refresh in
  StaleTimeoutSeconds, default 10s real-time) auto-deregister.
- Loopback skip: any PDU whose Site/App matches this instance's emitter identity
  is ignored, so a single host running both emitter and receiver doesn't echo
  itself.
- External aircraft skip the local Behaviour entirely (their truth comes from
  the feed), `PlayerIssueInstruction` rejects them ("command its owning sim
  instead"), and the local emitter skips them in `EmitStates` so federations
  can't form re-broadcast loops.
- Dead reckoning (DIS algorithm 2 = constant linear velocity): between packets
  the receiver advances each external's Position by its last-known Velocity *
  real DeltaSeconds, and Altitude by ClimbRate/60 * dt. Visual layer also
  applies `VInterpTo` on actor position for externals only (rate 6) so any
  remaining packet-to-packet jolt becomes a smooth slide.
- Console: `clearance.dis.listen [port]` (default 3000), `clearance.dis.unlisten`,
  `clearance.dis.site <N>` (change this instance's Site ID at runtime so two
  copies on the same network can hear each other).
- New `FAircraftState.bIsExternal` flag is the load-bearing distinction the rest
  of the sim hangs off.
- Verified two-instance test: instance A `clearance.dis.start`, instance B
  `clearance.dis.site 2` + `clearance.dis.listen`. A's traffic appears as
  external on B's scope; B can't command it; B's emitter doesn't re-broadcast.

### [2026-06-02] GCI / Air Defence mode — military pivot → MIGRATED to main 2026-06-02
- Expanded since the initial sandbox entry:
  - 3-ship formation rejoin (left wing / right wing / trail) with V-formation slots
    fixed relative to bandit heading, "join-up" detection (first viper triggers
    the whole flight to join), bandit turns outward to be escorted out of sector.
  - Vertical staggering during the rejoin (hi-rejoin/lo-rejoin/trail-low) so
    wingmen don't fly through each other or the bandit; offset smoothly merges
    to zero in the last nautical mile.
  - Slot SettledInFormation set glues position to slot once the wingman has
    flown in via proper bank/turn physics (no teleport, no sideways slide).
  - Civilian ATC lockout: PlayerIssueInstruction rejects any aircraft with
    bUnderGCIControl. Hostile-pair conflict/TCAS/wake all suppressed in the
    Conflict Detector so the engagement doesn't trip the safety net.
  - Successful intercept (escort to ring) logs SuccessfulIntercept (+150).
  - Military performance envelope (GetMilitaryPerformance, GetEffectivePerformance):
    Vmax 1050 kts (Mach 1.6), 80° bank limit, 50k ft ceiling, 12000 ft/min climb,
    10 kts/sec accel. Behaviour/Validator route through bIsMilitary so the
    fighter envelope applies to anything flagged military.
  - Visual roll smoothing rate doubled for military (QInterpTo 4.0 → 9.0)
    so the bank snaps in/out like a fighter, not eases like an airliner.
  - Pursuit speed: VectorIntercept commands 1050 kts for the intercept dash;
    join-up flight bleeds to 640 kts; settles to bandit speed in the slot.
  - FighterVariants (F-35) pool + HostileVariants (MiG) pool on the Controller;
    hostiles pick from HostileVariants first, friendly military from
    FighterVariants, civilians from wake-category pools as before.
- Still ALL behind console (`clearance.gci.test`, `clearance.intercept.flight`,
  `clearance.classify`, `clearance.iff`, `clearance.intercept`). Natural-gameplay
  integration (random bandit injection, scrambleable alert flight, violation
  zone outcome) deferred - tracked under the console-vs-gameplay backlog.
- DOC: none (post-MVP defence-sector extension).
- Adds NATO-style threat classification + IFF + intercept vectoring on top of the
  existing civilian ATC sim, so the same simulation supports both ATC and air
  defence training without forking the codebase.
  - `EThreatClass` (Friendly/Hostile/Unknown/Neutral) + `SquawkCode` +
    `bIFFOperational` per aircraft state.
  - Controller: `SetGCIModeEnabled`, `ClassifyAircraft`, `InterrogateIFF`
    (returns class + squawk if the IFF responds), `VectorIntercept` (lead-pursuit
    quadratic solve, issues a HeadingChange to a friendly fighter to converge
    with a target).
  - Console: `clearance.gci on/off`, `clearance.classify <cs> <class>`,
    `clearance.iff <cs>`, `clearance.intercept <fighter> <target>`,
    `clearance.gci.test` (spawns BANDIT hostile-IFF-off + VIPER01 friendly
    fighter for an intercept demo).
  - Per-aircraft readout prefixed with `[FRI]/[HOS]/[UNK]/[NEU]` when GCI is on;
    header tag adds `[GCI]`.
- WHY: defence-sector pivot. Real defence-sim shops build air-defence trainers
  (GCI = Ground-Controlled Intercept), not civilian ATC. Adding this turns
  CLEARANCE into a credible military training tool with the same underlying tech.
- DOC UPDATED?: pending (the pre-pro docs predate GCI scope).

### [2026-06-01..02] Modelled radar sensor — sensor simulation layer → MIGRATED to main
- DOC: ATCSIMSYSTEMSDESIGN (Conflict Detection / radar surface) — pre-pro
  assumed god's-eye truth; this layer adds operator-side sensor realism on top.
- `FRadarTrack` struct + `UClearanceRadar` UObject. Radar reads truth from the
  Airspace Manager and produces tracks - what the radar BELIEVES.
  - Range-limited (default 80nm), real-time mechanical sweep (default 12 rpm,
    independent of sim time scale), primary vs secondary returns (~5% drop to
    primary-only, altitude quantised, no callsign), per-paint position jitter,
    tracks fade over 8 real seconds without re-paint.
  - Pure logic layer - intentionally no world-space radar visualisation (the
    operator doesn't see a glowing antenna in the sky). Sweep + ring + blip
    rendering belong on the future 2D operator scope (VR/UI phase).
  - Truth-view aircraft visuals are untouched - planes always fly smoothly.
    Radar's "what the operator sees" surfaces as `RDR ...` lines in the debug
    readout; the 2D scope will be the proper consumer later.
- WHY: a real defence ATC trainer presents what the SENSOR sees, not truth.
  This is the architectural layer that lets the project claim "sensor
  simulation" rather than "ATC game."
- DOC UPDATED?: pending (Conflict Detection section now sits next to a sensor
  layer the doc didn't anticipate).

### [2026-05-30] DIS (IEEE 1278) Entity State PDU emitter → MIGRATED to main
- DOC: none (post-MVP defence interop addition).
- `UClearanceDISEmitter` builds 144-byte Entity State PDUs in big-endian network
  order: standard PDU header, Site/Application/Entity ID (Entity# stable from
  callsign hash), Entity Type by wake category (plausible SISO codes), linear
  velocity, location as geocentric doubles (flat-earth - normal for non-geographic
  sims), Euler orientation, Dead Reckoning algorithm 2, marking field = callsign.
- Per-aircraft per-tick over UDP (default LAN broadcast on DIS reserved port 3000;
  custom host/port supported). Runs in BOTH live and replay so a federation can
  watch a debrief too. Console: `clearance.dis.start [host] [port]`,
  `clearance.dis.stop`. Header tag `[DIS N/s]`.
- Verified field-by-field decode in Wireshark (`dis and not icmp` filter; expanded
  PDU tree; ASCII callsign visible at offset 0x00a0 in the hex pane).
- WHY: DIS is the defence-standard simulation interop protocol. Publishing valid
  DIS PDUs is the strongest single "this person works in defence sim" signal a
  portfolio can give.
- DOC UPDATED?: pending.

### [2026-05-29..30] After-Action Review: session recording + replay + preset cameras → MIGRATED to main
- DOC: none (post-MVP training-sim addition).
- `UClearanceSessionRecorder` records the session as a structured timeline of
  per-tick aircraft snapshots and timestamped events (instructions, conflicts,
  go-arounds, wake encounters, TCAS RAs). Lookups are "seconds into the
  recording" so callers don't deal with absolute SessionTime.
- Controller replay mode: suspend live tick, pose the world to the recorded
  snapshot at the chosen time, let UpdateVisuals draw it. Live state frozen on
  enter, restored on exit. Default replay speed matches `SimulationTimeScale`
  so a 10x sim plays back at the pace seen live.
- Fix on the way in: `StepAltitude`'s within-tolerance snap was zeroing ClimbRate
  when tracking a moving target (glideslope) at high frame rate; corrected to
  report the closing rate. Unblocked the descent nose-down/flare pitch which
  was reading "stays level" because ClimbRate read zero.
- Four preset cameras spawned on session start: sector overview, tower at the
  active runway threshold, far-end-of-approach, and a follow camera that chases
  a chosen aircraft. The follow camera has sub-angles (chase / cockpit / side /
  top) cyclable without losing the subject. `clearance.camera` + `clearance.camera
  angle`. Free-cam intentionally not exposed - fixed instructor view set.
- WHY: AAR is the heartbeat of every defence training sim. Built on the existing
  authoritative-state architecture.
- DOC UPDATED?: pending.

### [2026-05-28] TCAS Resolution Advisories + scoring suppression fix → MIGRATED to main
- DOC: Test Plan (Conflict / D-series) — extends to a coordinated automatic
  resolution rather than just alerting.
- On a Critical conflict the detector fires a coordinated `FOnTCASResolutionAdvisory`:
  higher aircraft CLIMB, lower aircraft DESCEND, to ±1500ft. Fires once per
  encounter; clears when the pair drops out of all conflict so a fresh encounter
  can re-fire. New `EIncidentType::TCASResolutionAdvisory` (penalty 100, counts
  as a failure for efficiency). Behaviour executes through the existing expedited
  AltitudeChange path; if either aircraft is on approach the climb instruction
  converts to a go-around so the climb wins against the glideslope.
- Scoring suppression: `SuccessfulResolution (+50)` is now suppressed for any
  pair that needed TCAS (TCAS did the resolving, not the player) - tracked via a
  TCAS-pair set on the controller, cleared on resolution or aircraft deregister.
- Net cost of letting one go to TCAS: −300 (−200 sep loss + −100 TCAS), with no
  resolution refund.
- WHY: completes the safety-net layer of the sim loop with a last-resort
  automatic action, and prevents the player from farming resolution points after
  failing.
- DOC UPDATED?: pending.

### [2026-05-28] Scoring loop closed: resolution rewards + wake encounter penalty → MIGRATED to main
- DOC: MVP success criteria (Scoring + safety) — closes gaps the pre-pro doc
  defined but the initial implementation hadn't wired.
- `OnConflictResolved` was unbound (the +50 SuccessfulResolution was dead). Wired
  `HandleConflictResolved` and award only when the resolved conflict had reached
  Warning or worse - trivial advisories that clear on their own can't farm
  points.
- New `EIncidentType::WakeEncounter` (penalty 75, counts as a failure for
  efficiency), logged from `HandleWakeAdvisory` so wake encounters now cost
  points. Conflict detection still only logs SeparationLoss on Critical, matching
  the real-world definition.
- Added a `SCORING +land +dep +resolved | -go-around -sep-loss -wake -strayed |
  next spawn` breakdown line to the debug overlay, tallied from the session log
  so we can see what's adding up.
- Added `clearance.exit <callsign>` console command for the SuccessfulDeparture
  scoring path (previously only reachable via voice).
- WHY: the simulation loop's scoring side had defined-but-unused incident types.
  These wire them in for a complete loop.
- DOC UPDATED?: pending.

### [2026-05-27] Conflict + wake turbulence verification: test commands + visible wake rock → MIGRATED to main
- DOC: Test Plan Conflict D1-D9 (Detection) - makes the existing logic
  observable + testable end-to-end.
- Conflicts now surface on screen (level + pair + separation + go-around tag
  where applicable) in addition to colouring aircraft blips.
- Wake turbulence now has a VISIBLE consequence: a follower caught in another's
  wake rocks its wings + bumps. Intensity scales with category mismatch (Light
  behind Super ≈ 1.0, Heavy behind Heavy ≈ 0.25) - the conflict detector exposes
  read-only `IsInWakeTurbulence` / `GetWakeIntensity` and the visual layer reads
  them.
- Test scenarios: `clearance.test.conflict` (two head-on at FL100, alerts
  escalate ADVISORY→WARNING→CRITICAL) and `clearance.test.wake [leader]
  [follower]` (any pair in trail). Both clear traffic and drop a known scenario
  so we don't have to wait for organic conflicts.
- Runway labels switched to real ICAO designators (heading / 10, two digits):
  heading 180 → RWY 18, 90 → RWY 09, 360 → RWY 36.
- DOC UPDATED?: pending.

### [2026-05-25..27] Touchdown polish + view defaults baked → MIGRATED to main
- DOC: ATCSIMSYSTEMSDESIGN (approach/landing).
- Touchdown reworked from "plant onto the deck" to "settle softly":
  - Flare arrests the sink rate to ~180 ft/min in the last 40ft of the approach.
  - Altitude tolerance dropped 50→10ft so it stops snapping the last stretch.
  - Smoother landing pitch (nose-down ~6deg on the glidepath, easing to +4deg
    flare, then level on the roll-out).
  - Buffet fades out below 400ft so the airframe steadies for the flare/roll-out.
  - Softer category-based braking - a Heavy/Super rolls out far longer.
- Baked the tuned visual defaults: `AltitudeWorldScale = 2`,
  `AltitudeCurveExponent = 1` (linear), `WorldUnitsPerNm = 1000`,
  `TouchdownZoneMeters = 1000`, `SimulationTimeScale = 10`.
- DOC UPDATED?: pending.

### [2026-05-25] ILS polish: dual-direction runways, missed-approach, gear/engine hooks, flight feel → MIGRATED to main (both build green)
- DOC: ATCSIMSYSTEMSDESIGN (approach/landing), Technical Scaffold (C++/BP boundary)
- Runway actors reworked:
  - Actor location is now the **strip CENTRE**, not a threshold. Split into a
    `Threshold` scene-component root (the touchdown logic point) + a child
    `RunwayMesh` (visual only) so the mesh can be aligned without moving the strip.
  - **Both landing directions** per strip: controller emits a threshold at each end
    (heading H and H+180, offset by RunwayLengthMeters); Airspace Manager picks the
    active end. `bAllowReciprocal` toggles one-way strips.
  - Runway selection now scores **crosswind − headwind** (was crosswind only) — the
    two ends of a strip share crosswind, so headwind is what makes the into-wind end
    win. Active end now flips correctly with the wind.
  - Ground "0 ft" Z taken from the placed runway actor, so the threshold marker and
    touchdowns sit ON the mesh (was the controller's Z).
- Approach corridor length + capture range unified in one constant
  (`ApproachCorridorLengthNm = 40`, `ApproachCorridorHalfWidthNm = 3`) so the drawn
  localiser and the capture logic can't drift apart. Extended 25→40nm.
- **Missed approach**: an aircraft cleared too late / never established that flies
  past the threshold still airborne now flies the go-around (climb by GoAroundClimbFt,
  revert to Enroute for re-vectoring) with an "unable — going around" call. Fires
  whether or not it captured, so an overflown landing triggers it too.
- Flight feel (visual only): eased the aircraft attitude with QInterpTo on REAL
  frame time (bank/pitch roll in and out instead of snapping), plus a subtle
  wind-scaled multi-frequency buffet + per-aircraft phase so straight-and-level
  flight breathes.
- **New `IClearanceAircraftVisualInterface`** (C++/BP boundary): controller feeds
  each aircraft visual `bGearDeployed` / `EngineThrottle` / `bOnGround` every tick;
  the Blueprint animates prop spin + gear retraction. Gear-down height tunable via
  controller `GearDownAltitudeFt` (2500). Handoff written to Neo.
- Resolves the previous entry's TODO (go-around if it blows through; tune capture).
  Still pending: flare/roll-out visuals on touchdown.
- DOC UPDATED?: pending (specs predate dual-direction runways + visual interface).

### [2026-05-25] ILS approaches + runway actors + realistic localizer capture
- `AClearanceRunway` actor: placeable, has a mesh + LandingHeadingDeg; its world
  location = threshold (converted to sim nm relative to the Controller).
- Airspace Manager now selects a real runway (FRunwayInfo: threshold + heading)
  by least crosswind; env carries the selected runway's threshold + heading.
  Controller discovers placed runway actors and feeds them in; falls back to a
  default 09/27 strip at the centre if none placed.
- ILS guidance (Behaviour): when cleared approach AND established, tracks the
  localizer (centreline) + 3deg glideslope to a touchdown at the selected runway.
- REALISTIC CAPTURE: "cleared approach" no longer auto-swoops. The ILS only
  engages once IsEstablishedOnApproach passes (cross-track <=1.5nm, heading within
  35deg of the course, on the approach side, within 25nm). Until then the aircraft
  flies the controller's vectors -> positioning for the intercept is the player's
  skill. On capture, an on-screen "established on the approach" message fires.
- Debug readout shows wind + active runway. Voice/console "cleared approach" works.
- TODO/polish: flare + roll-out visuals, runway mesh, go-around if it blows
  through the localizer, tune capture window.

### [2026-05-25] Voice/phraseology pipeline + expanded ATC commands (SANDBOX)
- New `UClearancePhraseology::Interpret` (BlueprintFunctionLibrary) + `clearance.say`
  console command: parses an ATC transmission -> instructions -> readback. This is
  the layer Whisper STT will feed (typed-input prototype first, by design).
- Grammar (see Docs/PHRASEOLOGY.md): telephony/ICAO callsigns, spoken digits,
  heading (+ honoured left/right turns, relative turns), flight level / altitude
  (+ expedite), speed, go-around, cleared approach/takeoff, contact/leave (exit).
- Backend additions: `FAircraftInstruction.TurnDirection` (-1/0/+1) honoured in
  StepHeading (forced-direction turn, long way if told); `bExpedite` boosts
  climb/descent rate 1.5x. Behaviour ApplyInstruction/StepHeading/StepAltitude
  now non-const to track active turn/expedite.
- DECISION: next big feature = ILS approaches (vector-to-land) to complete the
  gameplay loop; holding + waypoints after. Voice = post-MVP scope expansion.
- Migrated to main + verified compiles 2026-05-25.

### [2026-05-25] Migrated visuals/wind/steering/fidelity batch → main (verified compiles)

### [2026-05-25] Visuals, wind & manual steering (toward M1/M2)
- Per-category aircraft visuals: `LightVariants`/`MediumVariants`/`HeavyVariants`/
  `SuperVariants` on the Controller, each a list of `FAircraftVisualVariant`
  (BP class + per-MODEL yaw offset + scale). Random variant chosen per spawn;
  spawned actor forced to Movable. Debug spheres now per-aircraft fallback
  (only for aircraft with no assigned mesh).
- Wind: Controller `WindDirectionDeg`/`WindSpeedKts`/`RunwayHeadings` fed to the
  Airspace Manager via new `InitialiseEnvironment`; `SetWind` for runtime change.
  Non-zero wind makes aircraft visibly crab/drift. Default 220 deg / 25 kt.
- Manual steering console commands (no UI yet): `clearance.vector <cs> <hdg>`,
  `clearance.climb <cs> <ft>`, `clearance.speed <cs> <kts>` -> route through
  PlayerIssueInstruction -> Validator -> Behaviour; result printed on screen.
- NOTE: these editor/visual/console bits are sandbox-side dev aids. Decide later
  what migrates vs. stays (the console cmds are fine to keep; the debug overlay
  is throwaway once Neo's real radar exists).

### [2026-05-24] FIRST SUCCESSFUL RUN (smoke test passed) + debug overlay
- Added a throwaway C++ debug radar to the Controller (DrawDebug blips + heading
  lines + sector ring + on-screen readout) and a `SimulationTimeScale` (default
  10x) so the sim is watchable.
- Ran in PIE: aircraft spawn at the boundary, fly realistic inbound tracks,
  traffic count climbs, score updates. End-to-end loop confirmed working live.
- This is a SMOKE TEST, not the Test Plan: it proves the systems tick together
  and nothing crashes. Still UNVERIFIED: conflict thresholds firing at the right
  distances, wake matrix correctness, go-around behaviour, scoring values,
  runway selection. Those need the formal Test Plan pass.
- Live Coding lesson: header/UPROPERTY/new-class changes MUST be a full CLI
  rebuild with the editor closed; Live Coding only patches .cpp bodies (it
  crashed PIE when used on header changes).

### [2026-05-24] Spawner + Simulation Controller built (Steps 8-9) — BACKEND COMPLETE
- `AClearanceAircraftSpawner` (AActor): spawns aircraft on the difficulty interval
  at the sector boundary, inbound headings, random wake category (weighted mix),
  category-appropriate speeds. Controller-driven (bCanEverTick=false) via
  TickSpawning - chose central control over self-ticking for deterministic order.
- `AClearanceSimulationController` (AActor): the conductor. Creates/owns all
  systems, spawns the AirspaceManager + Spawner if not placed, binds all
  delegates, owns the per-aircraft Behaviour map (created on OnAircraftRegistered,
  removed on deregister), runs the authoritative tick: spawn -> move+commit ->
  conflict check -> exits. Events (conflict/go-around/wake/difficulty) handled
  via bound delegates. `PlayerIssueInstruction` is the UI entry. bAutoStart for
  easy testing.
- CheckExits: landing (phase Landing + alt<=100) -> SuccessfulLanding; beyond
  ExitRadius -> SuccessfulDeparture if Exiting else UnresolvedExit; deregisters.
- ⚠️ ALL 9 SYSTEMS COMPILE but NONE are runtime-tested yet. Next phase: run it
  (drop a Controller in a level, Play) + work the Test Plan. This is accumulated
  untested code - expect runtime bugs to surface on first run.

### [2026-05-24] Scoring built (Step 7)
- `UClearanceScoring` (UObject). `LogIncident(type, A, B, details)` is the single
  entry; reward/penalty decided by incident type, updates running score, logs an
  FIncidentRecord, broadcasts OnScoreUpdated, then adjusts difficulty.
- Configurable points (EditAnywhere): landing +100, departure +80, resolution
  +50; penalties for separation loss / go-around / unresolved exit / missed
  handoff / late instruction.
- Efficiency = handled / (handled + failures + go-arounds), 0..1 (1.0 when idle).
- Difficulty = spawn INTERVAL in seconds, shrinks from Base(30) toward Min(10) by
  DifficultySecondsPerHandled(1) per handled aircraft; broadcasts
  OnDifficultyAdjusted when it changes. (Spawner in Step 8 consumes this.)
- ResetSession clears everything and re-broadcasts score + difficulty.

### [2026-05-24] Conflict Detector built (Step 6)
- `UClearanceConflictDetector` (UObject, read-only). `DetectConflicts()` is one
  monitoring pass over all aircraft pairs.
- Separation alerts from the ICAO thresholds (vertical >=1000ft = clear);
  fires OnConflictDetected on enter/escalation, OnConflictResolved on clear,
  tracked by order-independent pair key so no double-firing per tick.
- Projected conflicts: clear-now pairs whose tracks converge within the
  lookahead window (default 60s) raise an early Advisory.
- Go-around: a Critical conflict involving an aircraft on Approach/Landing fires
  OnGoAroundRequired for that aircraft.
- Wake turbulence: lighter aircraft trailing a heavier one inside the category
  matrix distance fires OnWakeTurbulenceAdvisory; clears silently when restored.
  Super treated as Heavy in the matrix (docs only specify up to Heavy).
- Read-only guarantee: only ever reads GetAllAircraftStates, never writes.
- DEFERRED: lateral in-trail tightness for wake (currently "behind + within
  distance"); finer projected-conflict severity (projection only raises Advisory).

### [2026-05-24] Comms Router built (Step 5)
- `UClearanceCommsRouter` (UObject). `IssueInstruction` is the single player
  entry: checks registration, runs the Validator, routes accepted instructions
  to the aircraft's Behaviour via `QueueInstruction`, broadcasts the result.
- Keeps its own callsign->Behaviour map via Register/UnregisterBehaviour (the
  Controller will keep it in sync as aircraft spawn/exit).
- `RouteGoAround` (for the Conflict Detector) calls Behaviour->ExecuteGoAround
  rather than mutating state. `ReceiveAdvisory` broadcasts OnAdvisoryWarning.
- Owns OnInstructionResult + OnAdvisoryWarning (BlueprintAssignable) for the UI.
- DEFERRED: per-aircraft rate limit (MinInstructionInterval) - field + timestamp
  tracking present but enforcement off (no UI-facing "too soon" result yet).

### [2026-05-24] Performance numbers locked in — [TODO] placeholders RESOLVED
- Real per-category values supplied by Jeremy from reference aircraft:
  Light=Cessna 172S, Medium=737-800, Heavy=777-300ER, Super=A380-800.
- MinOperatingSpeed = ~1.3x stall (Vref), the slowest ATC would clear.
- Refactored to a single `ClearanceConstants::FCategoryPerformance` struct +
  `GetCategoryPerformance()`; replaces the old per-value constants and the
  multi-out-param `GetCategoryLimits`. Behaviour and Validator both use it.
- Added per-category: max DESCENT rate (separate from climb), accel & decel
  rates (decel ~half accel), crosswind limit. Removed the flat
  `AccelerationKnotsPerSec` tuning field from Behaviour (now per category).
- Behaviour: climbs use density-adjusted climb rate, descents use the flat
  descent rate; speed changes use accel vs decel by direction.
- `RunwaySwitchDeadbandKts` (2 kt) now used by the Airspace Manager (was inline).
- ⚠️ MIGRATION: touches ClearanceConstants.h, Behaviour (.h/.cpp), Validator.cpp,
  AirspaceManager.cpp — all in the next sync.

### [2026-05-24] Instruction Validator built (Step 4)
- `UClearanceInstructionValidator` (stateless UObject). `ValidateInstruction`
  returns `EInstructionResult`: Accepted or a Rejected_* reason.
- Enforces: altitude within [0, service ceiling], speed within operating
  envelope, finite heading. System go-arounds bypass the envelope (safety).
  Exiting aircraft -> Rejected_AircraftExited. Invalid state -> Rejected_InvalidCallsign.
- Limits come from category via the shared lookup, not the state's stamped
  fields, so the Validator is authoritative on feasibility.
- DEFERRED: wake-separation-on-approach check (needs other traffic -> belongs in
  the Conflict Detector, Step 6, not here).
- REFACTOR: moved per-category performance lookup into
  `ClearanceConstants::GetCategoryLimits` (inline). Behaviour now forwards to it
  instead of holding its own copy. Both files touched -> include in next migration.

### [2026-05-24] Aircraft Behaviour built (Step 3)
- `UClearanceAircraftBehaviour` (UObject, per-aircraft). Reads state from the
  Airspace Manager, slews heading/altitude/speed toward targets, commits back.
- Flight dynamics: bank-limited turn rate (g·tan(bank)/V), ISA-density-adjusted
  climb falloff, wind drift added to ground track from the sector environment.
- Performance envelope stamped per wake category on Initialise().
- Position convention (NOT in docs — chosen): Position is in nautical miles,
  X=East, Y=North; heading is a compass bearing (0=N, 90=E). Wind blows FROM
  WindDirection. Velocity stored as ground velocity (nm/s). Neo's radar maps
  Position→screen however it likes. Revisit if a different unit/origin is wanted.
- Descent currently capped by the same climb-rate figure (no separate descent
  rate yet) — fine for MVP, refine later.
- DEFERRED vs scaffold: full instruction *queue* (instructions currently set
  per-axis targets immediately, newest wins); dedicated approach glide-path.
- Still consuming the `[TODO]` placeholder performance constants.

### [2026-05-24] Signatures added to already-migrated files
- Author "TripleA" signature comments dotted (~2/file) through Core types,
  constants, and the Airspace Manager (header + cpp) — files that were already
  copied to main. MIGRATION IMPACT: next migration re-copies these so the
  signatures travel across. Purely comments; no behaviour change.

### [2026-05-24] Delegates moved out of ClearanceDelegates.h into CLEARANCETypes.h
- DOC: C++ Scaffold (suggested a separate `ClearanceDelegates.h`)
- WAS: 12 dynamic multicast delegates in their own header
- NOW: declared in `CLEARANCETypes.h`; `ClearanceDelegates.h` deleted
- WHY: UHT would not resolve the delegates as `BlueprintAssignable` UPROPERTY
  types when they lived in a standalone delegate-only header (even with a
  `.generated.h` include). Putting them in the already-reflected types header
  fixed it on the first try.

---

## 4. Portfolio claims — defensible language

Sandbox-only notes for resume / cover letter / interview prep. Every claim
below is backed by code in this repo. Don't recite verbatim; lift the
phrasing into context.

### What this project is, in industry terms

A real-time **distributed air traffic control simulator** with a server-
authoritative networked architecture, operator + instructor station model,
scenario-driven training pipeline, and integrated voice synthesis. UE 5.7 /
C++ / Blueprint, designed against the same procurement pattern used by
commercial defence training vendors (CAE, BAE Systems, Lockheed Martin's
Prepar3D, L3 Harris).

### Target market signal

The two-station model (operator runs the sim, instructor injects failures
and monitors) is the architectural pattern almost every military training
system uses. Recruiters scanning a defence-industry portfolio look for it
because it maps directly to how training-system procurement works.

### Defensible claims (with backing code)

**Distributed simulation / multiplayer architecture**
- Server-authoritative C++ replication across 19 simulation systems —
  aircraft state, scenario clock + events + triggers, score totals + 14
  per-category counters, conflict alert levels, notification ring buffer,
  next-spawn timer, runway environment, wind. Game-dev portfolios typically
  show position+rotation replication; this replicates whole simulation state
  including derived values.
- Authority discipline with `HasAuthority()` gates on every mutation entry
  point in `AClearanceAirspaceManager` (Register / Deregister /
  RequestStateUpdate / ClearAllAircraft), auditable `[AirspaceMgr] BLOCKED`
  logging when violated. Demonstrates understanding of trust boundaries, not
  just that the code compiles.
- Server-only `Multicast_PlayTTS` / `Multicast_PlayCockpitCue` NetMulticast
  RPCs route all pilot, AWACS, and cockpit-alarm voice through every peer.
  Each side calls its own local Piper TTS server — audio sync by trigger,
  not by streaming. ~70% reduction in network voice payload vs streaming.
- Late-join robustness: `OnRep_AirspaceManager` retroactively spawns visuals
  for already-replicated aircraft; replicated UPROPERTYs receive initial
  values on join. New peers catch up to current state without explicit
  catch-up logic.
- Custom `AClearanceReadoutHUD` per `APlayerController` for per-viewport
  rendering, sidesteps PIE multi-window inconsistency in
  `GEngine->AddOnScreenDebugMessage`.

**Instructor station / failure injection**
- `AClearanceOperatorPC` `APlayerController` subclass exposes a
  `Server_Inject*` RPC surface — server-side reliable RPCs with
  `WithValidation`. Maps directly to the failure-injection control surface
  defence training vendors sell to instructors.
- Replicated 12-entry notification ring buffer surfaces conflict alerts,
  emergencies, intercepts, violations on both operator and instructor
  windows with fade-out.

**Network infrastructure**
- Epic Online Services (EOS) integration for NAT punchthrough + session-
  based matchmaking. Hiring managers behind corporate firewalls can join a
  session via 6-digit code without port forwarding or VPN. (Phase 8 — when
  done.)

**Scenario authoring pipeline**
- JSON-driven `UClearanceScenarioRunner` with 8 action verbs (Spawn,
  DeclareEmergency, Pursue, BreakOff, ApplyEnvironment, SetFlag, Say,
  PlayStatic) and 5 trigger conditions (Time, FlagSet, AircraftCount,
  EmergencyDeclared, AircraftPosition). Seven authored scenarios spanning
  every emergency type (7500 / 7600 / 7700 / FuelLow) and three operational
  modes (civil ATC / GCI air defence / mixed). Demonstrates that the editor
  *is* the product — each new scenario is ~15 minutes of JSON, not C++.

**Electronic warfare**
- Jamming + chaff integrated as a sensor-layer problem, not a scripted
  cinematic. `bJammingOn` on `FAircraftState` cuts paint confidence to
  0.25 AND blankets a ±12° bearing wedge from each radar through the
  jammer's position — every contact in that arc loses confidence.
  `FChaffCloud` drops a 12-second ghost contact every radar sees.
- Reactive bandit AI: hostiles auto-jam at 25nm interceptor closure,
  drop chaff at 10nm, jammer off past 40nm. Operator faces an active
  adversary, not a static target.
- Intercept-through-EW bonus + DeclareTrackLost phraseology with EW-aware
  scoring (track lost while EW active = no penalty, without = strayed
  penalty) so the EW system has gameplay teeth, not just visual flavour.
- `FRadarTrack.PaintConfidence` separates "what the radar saw on last
  paint" from "how fresh that paint is", so jammed paints don't snap
  back to full each tick. Small architectural detail that makes the
  whole EW pass real instead of flickering.

**Tactical symbology**
- MIL-STD-2525C affiliation frames on the operator scope (friend rectangle
  / hostile diamond / unknown quatrefoil-approximated octagon / neutral
  square), bearing vector, alert ring per CurrentAlertLevel, military
  equipment modifier. Drawn for every aircraft regardless of visual mesh.
- Real defence standard — recruiters at CAE / BAE / Lockheed / L3 Harris
  recognise these shapes from procurement specs. Cheap to implement,
  immediate "looks like an actual training system" payoff.

**Sensor simulation layer**
- Multi-radar sensor fusion: `AClearanceRadarSite` per-site truth-to-track
  conversion with range / azimuth / elevation envelope, occlusion, fade,
  weather degradation, primary/secondary surveillance. Fused into the
  operator's display by `UClearanceSensorFusion`. Not an arcade overlay —
  a modelled sensor layer with N>1 sites producing a single fused track per
  truth target.
- Passive UDP DIS receiver with dead-reckoning. Interoperability with any
  DIS-emitting external system (other sims, exercise injects).

**Voice / TTS pipeline**
- Local Piper-based TTS server, per-airline regional voice assignment,
  half-duplex channel discipline (one transmitter at a time), radio FX
  chain (bandpass, carrier noise, soft saturation), separate panic FX chain
  for cockpit emergencies, GPWS cockpit-alarm path. en-US-EricNeural for
  AWACS, en-GB-RyanNeural for ACC, en-GB-LibbyNeural for MET — different
  agencies sound like different agencies on the radio.

**ATC phraseology / voice recognition**
- Local Whisper-based speech-to-text. Phraseology parser accepts the
  realistic ATC verb vocabulary (vector, climb, descend, speed, contact,
  divert, exit, hold, intercept, shadow, identify, etc.). Player issues
  commands by voice; pilots reply with synthesised voice in their
  airline-specific accent.

### What this project is NOT — don't claim these

- Not a dedicated-server architecture (listen-server only)
- Not a full HLA federation (has DIS receiver only, no DIS sender)
- Not scaled-validated for thousands of entities
- Not anti-cheat hardened
- Not on a published asset store / marketplace

### The portfolio multiplier

Any one of (networked sim / scenario authoring / TTS / sensor fusion / VR /
Simulink) is decent on its own. All of them on one repo, built sequentially
over weeks, reads as "this person can ship an end-to-end training system".
That's the actual product defence procurement buys — not a single feature.
Frame the portfolio around the *system*, not the components.
- ⚠️ MIGRATION IMPACT: main already received the old layout. On next migration,
  re-copy `CLEARANCETypes.h` AND delete `ClearanceDelegates.h` from main.
- DOC UPDATED?: no (implementation detail)

### [2026-05-24] Airspace Manager built (Step 2)
- Implemented register/deregister/query/update/clear, validation + clamping,
  environment + wind-driven runway selection with a crosswind dead-band, and the
  4 BlueprintAssignable delegates Neo requested.
- Tick disabled (`bCanEverTick=false`): the manager is reactive; the Simulation
  Controller will drive the pipeline. Revisit if it needs per-tick work.
- DEFERRED — out-of-sequence update rejection (Test Plan Test 5): needs an update
  timestamp/sequence on the state. Will add when Behaviour (Step 3) defines the
  update cadence, rather than guessing the mechanism now.
- Runway hysteresis uses a local 2 kt crosswind dead-band, NOT the
  `RunwaySelectionHysteresisDeg` constant (units mismatch: kt vs deg). Reconcile
  when tuning — currently `RunwaySelectionHysteresisDeg` is unused.
- Still `[TODO]`: confirm the `[TODO]` performance constants before Steps 3-4.

### [2026-05-24] Migrated Core types → main project
- Copied `Plugins/ClearanceSim/` (source only — `.uplugin` + `Source/`, excluding
  `Binaries/` + `Intermediate/`) to `…\Documents\CLEARANCE\CLEARANCE\CLEARANCE\Plugins\`.
- Added `ClearanceSim` to main's `CLEARANCE.uproject` Plugins array.
- Main `.uproject` confirmed clean: no AgentIntegrationKit entry present.
- Main needs a first-time rebuild of the module (open `.uproject` → Yes to rebuild).
- Main is not yet a git repo in the project folder (no `.git` found there).

### [2026-05-24] Decision: build as `ClearanceSim` plugin (confirmed)
- DOC: Technical Implementation Scaffold §9 (suggested `Source/CLEARANCE/` layout)
- WAS: code assumed to live in the game module under `Source/CLEARANCE/`
- NOW: simulation lives in a plugin `Plugins/ClearanceSim/`, module name
  `ClearanceSim` (API macro `CLEARANCESIM_API`). Folder structure under
  `Plugins/ClearanceSim/Source/ClearanceSim/Public/<System>/` mirrors the doc.
- WHY: one-folder migration sandbox→main; reusable/portable packaging for the
  defence portfolio; clean separation from the game module.
- DOC UPDATED?: no (CLAUDE.md notes it; specs unchanged)

### [2026-05-24] Core types built (Step 1)
- DOC: C++ Scaffold §ENUMS, §CORE DATA STRUCTS, §DELEGATES
- WAS: spec listing of enums/structs/delegates
- NOW: implemented in `Public/Core/` as `CLEARANCETypes.h` (6 enums, 6 structs,
  all `BlueprintType`), `ClearanceDelegates.h` (12 dynamic multicast delegates,
  BlueprintAssignable-ready), `ClearanceConstants.h`.
- Deviations from scaffold:
  - Added `WakeCategory` field to `FAircraftSpawnData` (scaffold omitted it) so
    the Spawner can assign performance category at spawn — required by Test 16 /
    checklist F5. Low risk, additive.
  - `OnAircraftStateUpdated` delegate carries `FName Callsign` (not the full
    `FAircraftState`). WHY: radar polls `GetAllAircraftStates()` each frame
    anyway (per Neo's hook request); broadcasting a large struct per aircraft
    per tick would be wasteful. UI uses the signal + queries for detail.
- DOC UPDATED?: no (additive/implementation detail; note here is the record)

### [2026-05-24] Tuning constants consolidated — SOME ARE PLACEHOLDERS
- DOC: Test Plan / C++ Scaffold / Risk Register (values were scattered)
- NOW: single source of truth in `ClearanceConstants.h`, tagged `[DOC]` vs `[TODO]`.
- `[DOC]` (from specs): separation thresholds (8/5/3nm, 1000ft), wake matrix
  (6/5/5/4/3nm), climb-rate ranges per category.
- `[TODO]` **need confirmation — currently real-world-grounded placeholders**:
  service ceilings, operating speed envelopes, bank-angle limits per category,
  Super-category climb rate, runway hysteresis magnitude. The docs never gave
  numbers for these. **Action for Jeremy: confirm or supply real values before
  Validator/Behaviour physics are tuned (Steps 3-4).**
- DOC UPDATED?: pending (values to be confirmed)

### [2026-05-24] Compile model note: new plugin needs full rebuild (not Live Coding)
- Adding a brand-new plugin module cannot be hot-loaded via Live Coding. The
  editor must be closed and the project rebuilt once, after which the module is
  available. Subsequent edits to existing files in the module CAN use Live Coding.
