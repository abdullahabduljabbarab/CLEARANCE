# Development log

A running record of the CLEARANCE main-repo engineering work. Only
entries directly touching the Unreal project, its plugin module, or
its content are captured here. Companion repositories keep their own
devlogs:

- Autopilot: [autopilot-mbd/DEVLOG.md](https://github.com/abdullahabduljabbarab/autopilot-mbd/blob/main/DEVLOG.md)
- Radar: [radar-mbd/DEVLOG.md](https://github.com/abdullahabduljabbarab/radar-mbd/blob/main/DEVLOG.md)
- Missile: [missile-mbd/DEVLOG.md](https://github.com/abdullahabduljabbarab/missile-mbd/blob/main/DEVLOG.md)
- Federation: [clearance-federation/DEVLOG.md](https://github.com/abdullahabduljabbarab/clearance-federation/blob/main/DEVLOG.md)

The log reads chronologically, most recent first.

---

## 2026-08 — Release polish and packaging

### 2026-08-16 — Runway ground pad for aircraft landing on streamed terrain

Aircraft landing on the Warton runway were clipping through the surface
whenever Cesium tiles streamed in at a higher LOD than the runway
actor's fixed Z coordinate. The runway sits at Z=6659.66 while the
terrain at the runway centre climbed to Z=6559.81 in the editor and
kept rising at runtime. Placed an invisible collision pad
(`SM_RunwayGroundPad`) across the full runway strip with hidden-in-game
enabled and block-all collision so that the runtime ground trace hits
the pad regardless of Cesium tile state. The landing code path still
uses the runway actor Z directly for v1.0; a follow-up line trace
against `SM_RunwayGroundPad` is scheduled for a future patch.

### 2026-08-15 — VR world-space widget rendering constraint documented

World-space `WidgetComponent` actors on the tower monitors do not
render nested `UUserWidget` sub-widgets on their capture texture. Only
primitive Slate widgets (`UBorder`, `UTextBlock`, `UButton`,
`UHorizontalBox`, `UVerticalBox`) rasterise correctly. The operator
strip and scope monitor now build their runtime children exclusively
from primitive widgets, and any richer content is drawn through
`NativePaint` on the widget root rather than composed from sub-widgets.
The design constraint is documented in the plugin so future world-space
widgets adopt the same discipline from the first commit.

### 2026-08-12 — Options dialog and main menu sub-dialog restyling

Full pass on the main menu sub-dialogs. Consistent glass panel
background `(0.03, 0.06, 0.08, 0.85)`, EuroStyle font throughout,
cyan primary text, standardised button order (Cancel left, primary
action right), matching border and focus styles. Applies to Host, Join,
Quit-Confirm, and Options. New Options page wires Master Volume,
Resolution Scale, and Window Mode against `GameUserSettings`, with
expensive resolution changes deferred to `OnMouseCaptureEnd` so the
slider stays responsive during drag.

### 2026-08-11 — Missile launcher and SAM engagement wiring

Placed the vertical-launch surface-to-air missile actor at Warton and
wired the SCRAMBLE / ENGAGE control path so the operator can direct a
missile at the selected hostile track. Guidance is a C++ pursuit
fallback for v1.0; the Simulink model is integrated but not driving
live for the reason recorded in the missile-mbd repository.

## 2026-07 — Simulation, MBD, and federation

### 2026-07-13 — Voice pipeline packaged as standalone executables

The bundled TTS server was auto-launched by the game, but on machines
where `python` on the PATH resolved to the Windows Store
`AppExecutionAlias` stub, `CreateProc` returned a valid handle, the
stub flashed a console, and nothing forwarded to a real Python. Fine
on a developer box, silently broken for any downloader.

Rebuilt the TTS server as a single standalone binary via PyInstaller
(`--onefile`), bundling the Python interpreter, piper-tts, edge-tts,
FastAPI, and all voice models into `TtsServer/tts_server.exe`. The
launcher now prefers the executable when present and falls back to
`python tts_server.py` for developer iteration. Whisper server received
the same treatment. Both executables ship with the packaged build via
`RuntimeDependencies.Add(...)` in the plugin `Build.cs`, so a downloader
gets voice on first launch without a Python install. See
[Docs/Design/VoicePipeline.md](Design/VoicePipeline.md).

### 2026-07-13 — Runway designator override decoupled from heading math

Iterating `LandingHeadingDeg` alone was thrashing the displayed ICAO
designator between the two runway ends because the sim's internal
heading frame and the Cesium local ENU do not compose to a clean
rotation at Warton. Split the concern: `LandingHeadingDeg` still drives
the strip's world direction, and a new `DesignatorNumberOverride`
(1 to 36) on `AClearanceRunway` forces the primary end's ICAO number.
The reciprocal auto-derives as `(N + 18) mod 36`. Wired through
`FRunwayInfo` so the client scope, camera overlay, and approach picker
all read the same authoritative designator.

### 2026-07-13 — Scope auto-fit smoothing and waypoint inclusion

Loading a scenario would collapse the scope to whatever aircraft was
farthest, then jerk larger as another aircraft crossed a wider radius
next tick. Two fixes in the auto-fit block: include waypoints and
violation zones in the `MaxDistNm` loop alongside aircraft, and make
growth instantaneous while shrink smooths toward the target range by
eight per cent per refresh. A single aircraft flying inward no longer
yanks the scope in and back out every second.

### 2026-07-13 — Scenario and vector-command heading mirror at Warton

Scenario JSON authors headings in the operator frame ("hostile inbound
heading 070"). The sim internal frame at Warton on Cesium is a mirror
of the display frame, so storing the JSON value directly in
`State.Heading` meant the aircraft flew the reflected direction and the
label reported the reflected value. Applied `Internal = 360 - Magnetic`
at ingest for both `FireInitialSpawns` and the mid-run scenario
spawner, and applied the same conversion to the operator's cleared
heading vector command. Recorded end-to-end in
[Docs/Design/CesiumAndCoordinateFrame.md](Design/CesiumAndCoordinateFrame.md).

### 2026-07-13 — Overview camera runway highlight no longer occluded

The runway outline on the camera overlay was invisible on the Overview
camera at Warton because `bDepthOcclude` was on and the depth trace hit
Cesium tile collision meshes long before it reached the tarmac. Turned
depth-occlude off across all camera modes; the outline is a persistent
HUD element in every view.

### 2026-07-12 — EGNO Warton geospatial scene on Cesium 3D Tiles

Replaced the earlier airport-test level. The bought asset set was
consuming 826 ms of game thread and 708 ms of GPU with 14 million
primitives, which was unshippable in VR at 72 Hz. Rebuilt around
CesiumForUnreal with Google Photorealistic 3D Tiles and OSM Buildings
at Warton (53.7°N, 3°W). Georeference origin at Warton, sun-sky at
17:30 solar for the demo cinematography, `r.RayTracing=False`,
`r.Shadow.Virtual.Enable=0`, `r.Streaming.PoolSize=3000`,
`r.StaticMeshLODDistanceScale=0.3`, and aggressive sky-atmosphere
trims. Held roughly 30 ms game / 22 ms GPU on a Quest 3 via Link.

### 2026-07-12 — Coordinate frame refactor to the Cesium mirror convention

The sim's internal `(sin H, cos H)` direction math produced a strip at
`LandingHeadingDeg = 290` that visually landed on Warton's real 070
tarmac when rendered through the Cesium local ENU. That is a
reflection, not a rotation, so no single scalar rotation offset could
reconcile the label with the visual direction of motion across both
scope and camera views. Adopted the reflection: display frame equals
`360 - internal`, applied consistently at every user-facing surface.

### 2026-07-12 — Runway dimensions bypass AABB reconstruction

`GetRunwayBounds` returns the AABB of the oriented runway rectangle in
world space. The AABB of a long thin rectangle at Warton's oblique
heading is nearly square, so projecting the mesh extent onto the
inbound and perpendicular vectors leaked length into width and drew a
1.5 km wide runway on scope and cameras. Added
`OverrideLengthUnits` / `OverrideWidthUnits` on the runway actor;
`SimulationController` uses them directly when set, falling back to
the AABB reconstruction for axis-aligned meshes.

### 2026-07-09 — Simulink radar integrated live

Third pass on the sensor stack. The radar signal processor authored in
Simulink is now generated to portable C, wrapped by
`ClearanceRadarMBD`, and drives every `UClearanceRadar` in the sim.
The v2 long-range waveform profile trades unambiguous Doppler for the
range CLEARANCE needs (100 Hz PRF, 810 nm unambiguous range). The
matching layer switched to range-only detection under that profile. EW
rewritten: barrage jamming degrades the jamming aircraft's own return
rather than nulling entire bearings, and chaff bursts produce a
five-blip primary-only ghost cluster that fades with cloud lifetime.
Autopilot solver stability fix landed in the same session; the wing
rock on vector commands at 60 fps was traced to the model's derivative
filter eigenvalue sitting on the RK4 stability boundary at the fixed
0.02 s step. Regenerated with `Kd_phi = Kd_theta = 0`. Details in
[radar-mbd/DEVLOG.md](https://github.com/abdullahabduljabbarab/radar-mbd/blob/main/DEVLOG.md)
and [autopilot-mbd/DEVLOG.md](https://github.com/abdullahabduljabbarab/autopilot-mbd/blob/main/DEVLOG.md).

### 2026-07-08 — HLA over OpenRTI as the fourth federation wire

Added `ClearanceHLA` as a fourth sibling to `ClearanceDIS`,
`ClearanceDDS`, and `ClearanceRTI`. Same isolation discipline: pure
C++ public API, RTI ambassador types hidden behind PImpl, engine-free
surface. Publishes an `ATCManagedAircraft` object class defined in an
RPR-FOM derived extension XML. Verified end-to-end with an
independent `StandaloneHLASubscriber` executable that joins the same
federation. HLA notes and the Portico compatibility gap are recorded
in [clearance-federation/DEVLOG.md](https://github.com/abdullahabduljabbarab/clearance-federation/blob/main/DEVLOG.md).

### 2026-07-08 — Live two-federate DIS federation

Closes out the DIS federation milestone. Two Play-Standalone CLEARANCE
processes on the same host now share an airspace picture. Bugs fixed
during the pass: emitter socket needed `IP_MULTICAST_LOOP` and receiver
needed `IP_ADD_MEMBERSHIP`; `LastPacketsReceived` was resetting inside
`Poll` so downstream sampling saw only spot values; instructor panel
getters were reading through unreplicated subobject pointers on the
client and short-circuiting to zero. Added replicated mirror fields
on `AClearanceSimulationController` for every wire's cumulative
counters and activity flags.

### 2026-07-08 — RTI Connext DDS as third federation wire

Added `ClearanceRTI` as a sibling to `ClearanceDDS`, running the same
IDL schema through RTI's commercial runtime. Discovered by the RTI
Administration Console on domain 1 with the CLEARANCE participant
tagged `Real-Time Innovations, Inc. (RTI) – Connext DDS`. Full build
integration battles, IDL namespace collisions, and Windows.h leakage
fixes recorded in
[clearance-federation/DEVLOG.md](https://github.com/abdullahabduljabbarab/clearance-federation/blob/main/DEVLOG.md).

### 2026-07-07 — Automation test suite locking requirements

44 automation tests across ten files covering DIS PDU wire formats,
RPR-FOM ForceId mapping, instruction validator, safety constants,
scoring, and session recorder. Each test tags the specific requirement
IDs it verifies in a leading comment. Grew to 52 tests / 69
requirements by v1.0. Full accounting in
[Docs/Verification/Requirements.md](Verification/Requirements.md) and
[Docs/Verification/V_AND_V_REPORT.md](Verification/V_AND_V_REPORT.md).

### 2026-07-07 — Full two-federate DIS and DDS federation

Two CLEARANCE instances on one host share an airspace picture over DIS
and DDS simultaneously. Each federate owns its own aircraft and
ingests peer aircraft as external, display-only tracks. Ownership
protection prevents a peer from clobbering the local operator's
classifications, injected emergencies, or aircraft state. Extended the
IDL schema to carry the ATC state peers need (TrueAffiliation,
SquawkCode, ActiveEmergency, FlightPhase) so a DIS-DDS-HLA bridge is a
struct-to-struct copy for every field.

### 2026-07-07 — RPR-FOM extension XML

Shipped `ClearanceRPR-FOM.xml`, an RPR-FOM derived HLA FOM extension
introducing the `ATCManagedAircraft` object class with attributes
`SquawkCode`, `FlightPhase`, `ActiveClearance`, and `ATCFacility`.
Attribute transportation declarations mix `HLAreliable` and
`HLAbestEffort` to demonstrate the QoS split visible to a subscribing
federate.

### 2026-07-07 — Fast DDS integration

Added `ClearanceDDS` as a plugin module publishing typed DDS topics
generated from IDL over eProsima Fast DDS / RTPS. Standalone consumers
provide independent evidence outside the Unreal process.

### 2026-07-06 — DIS protocol layer isolation

Split the DIS codec into its own `ClearanceDIS` module with a pure
C++ public API and no Unreal dependencies. Enables the same codec to
be used by test executables and standalone tooling without pulling in
the engine.

### 2026-07-06 — DIS PDU family expansion

Added Emission (Type 23), Fire (Type 2), Detonation (Type 3), and
Signal (Type 26) PDUs alongside the existing Entity State. Six PDU
types cover the entity motion, weapons, radio, and electronic warfare
strands of a training exercise.

### 2026-07-02 — Instructor UX polish pass

Countdown indicators on emergency injects, filter chips on the
aircraft list, standardised communications role colouring, and list
reflow so that the panel scans as a real ATC ops console rather than
a debug UI.

### 2026-06-28 — Radar coverage heatmap overlay

Multi-radar fusion emits a per-sector coverage evaluation. The
instructor scope draws a coverage heatmap overlay so gaps and
multi-site overlap are visible at a glance. Useful for placing new
radar sites in a scenario and for post-run analysis of why a track
went unseen.

## 2026-06 — Instructor station and safety

### 2026-06 — Instructor panel and PIP cameras

Full instructor station UI: truth scope, scenario controls, PIP cameras
(tower, chase, approach, overview, operator POV), performance tab with
score report and communications transcript, After Action Report export.
Extensive iteration log against the paint chain and per-widget layout
in the sandbox devlog; the design decisions are consolidated in the
main [Docs/Design/SystemsDesign.md](Design/SystemsDesign.md).

### 2026-06 — Conflict detection and TCAS

Three-level conflict alert ladder (Advisory, Warning, Critical) with a
TCAS-style Resolution Advisory splitting pairs vertically at Critical.
Wake turbulence separation matrix from ICAO Doc 4444 applied to
same-runway and crossing-track pairs.

### 2026-06 — Session recorder and replay

Server-authoritative snapshot recorder with a scrub bar, playback speed
control, and instructor replay poses the world back to earlier
recorded states. See [Docs/Design/ReplayAndAAR.md](Design/ReplayAndAAR.md).

## 2026-05 — Foundations

### 2026-05 — Authoritative airspace state and simulation controller

`AClearanceAirspaceManager` as the sole owner of aircraft and
environment state. `AClearanceSimulationController` orchestrates the
ordered tick pipeline. `UClearanceAircraftBehaviour` as the single
movement executor per aircraft. The architectural boundaries that
carried the whole build are set in this month.

### 2026-05 — Communications validator and phraseology parser

`UClearanceInstructionValidator` accepts or rejects a raw instruction
against current aircraft state before it reaches behaviour. The
phraseology parser translates real ATC speech into structured
instructions and produces spoken readbacks. See
[Docs/Design/VoicePipeline.md](Design/VoicePipeline.md).

### 2026-05 — Project scaffolding

C++ module layout under `Source/`, plugin module under
`Plugins/ClearanceSim/Source/ClearanceSim/`, folder structure by
subsystem (`Core/`, `Airspace/`, `Aircraft/`, `Comms/`, `Safety/`,
`Scoring/`, `Simulation/`, `UI/`). Delegate map, tick order, and
type surfaces per
[Docs/Design/TechnicalArchitecture.md](Design/TechnicalArchitecture.md)
and [Docs/Design/CppScaffold.md](Design/CppScaffold.md).
