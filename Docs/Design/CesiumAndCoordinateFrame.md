# Cesium and coordinate frame

CLEARANCE ships on real-world terrain streamed through Cesium for
Unreal at Warton airfield (EGNO, 53.7°N 3°W). The choice of a
photorealistic geospatial world under a live ATC sim forces two
non-obvious engineering decisions:

1. Aircraft, runways, and label headings have to agree between the
   scope and the camera at all times, but the sim's internal heading
   frame does not compose with Cesium's local ENU at Warton as a
   clean rotation.
2. Terrain tiles stream in and out at LOD boundaries during the
   session, so any actor with a fixed Z coordinate can end up either
   floating above or clipping below the terrain surface.

This document records how each is handled.

## Contents

- [Cesium scene setup](#cesium-scene-setup)
- [The Cesium mirror convention](#the-cesium-mirror-convention)
- [Where the mirror is applied](#where-the-mirror-is-applied)
- [Runway dimensions on an oblique-heading strip](#runway-dimensions-on-an-oblique-heading-strip)
- [Terrain streaming and the ground clamp pad](#terrain-streaming-and-the-ground-clamp-pad)
- [VR performance envelope for the Cesium scene](#vr-performance-envelope-for-the-cesium-scene)

## Cesium scene setup

`LVL_Warton.umap` is built around a `CesiumGeoreference` actor with
origin at Warton, a `Cesium3DTileset` sourced from Google
Photorealistic 3D Tiles, an OSM Buildings tileset, and a
`CesiumSunSky` at solar time 17:30 for the demo cinematography. The
control tower actor and `AClearanceRunway` sit at the physical runway
location.

The georeference at Warton does not project the sim's internal +X onto
the overview camera's screen +X. Cesium's local ENU there inverts one
axis relative to the internal frame, which is where the mirror
convention comes in.

## The Cesium mirror convention

The sim's aircraft behaviour computes direction of motion from
`(sin H, cos H)` in its internal heading frame. On the Warton
georeference, a strip authored at `LandingHeadingDeg = 290` visually
lands on the real 070 tarmac when the whole scene is rendered through
Cesium's local ENU. That is a reflection, not a rotation, so no
single scalar rotation offset can reconcile the aircraft label with
its visual direction of motion across both scope and camera views
simultaneously.

The chosen convention is to store internal, and mirror at every
user-facing surface:

```
display_heading_deg = 360 - internal_heading_deg
```

The mirror is a scalar reflection about the north axis. Applying it
consistently everywhere the operator or trainee reads or writes a
heading keeps the scope, the camera overlays, and the operator's
voice inputs all agreeing on the same number for the same aircraft.

`WorldNorthOffsetDeg` on the `SimulationController` is unused for
Warton (set to zero) and remains available for future non-Warton
scenes that only need a clean rotation.

## Where the mirror is applied

Every surface that touches a heading value in the display frame:

**Scope draw sites**
- Aircraft data-block heading text (`BuildLabelLines`).
- Aircraft bearing vector (`DrawAffiliationSymbol`).
- Runway strip direction (`DrawRunwayMarker`).
- Compass rose (positions live in raw scope frame; labels stay raw
  because the pixel frame IS the display frame under the mirror).
- Pixel mapping (`ScopeNmToPixel` negates X).

**Camera overlay**
- Aircraft label heading (`GetCameraLabels`).
- Runway designator text (`GetCameraOverlayText`).
- Compass rose placement (world `(-sin, cos)`).
- Runway designator formula fallback when the actor's
  `DesignatorNumberOverride` is zero.

**Inbound intent**
- Operator's "cleared heading N" vector command mirrored on ingest in
  `UClearanceAircraftBehaviour::ApplyInstruction`. The operator says
  070, the internal setpoint becomes 290, the aircraft visually flies
  070.
- Scenario JSON headings mirrored on ingest in
  `ClearanceScenarioRunner::FireInitialSpawns` and mid-run
  `SpawnAircraft` actions.

Both the input side and every draw site share the same conversion, so
what the operator says, sees on scope, and sees in the camera all
resolve to the same visual direction of motion.

## Runway dimensions on an oblique-heading strip

`GetRunwayBounds` returns the axis-aligned bounding box of the
oriented runway rectangle in world space. When the runway is
axis-aligned in the world frame, the AABB extent equals the runway
dimensions and projecting the mesh extent onto the inbound and
perpendicular vectors reconstructs length and width cleanly.

Warton's runway sits at roughly 070°, and the AABB of a long thin
rectangle at that heading is nearly square. Projecting the mesh
extent leaks length into width, and the sim ends up drawing a
1.5 km wide runway on scope and cameras.

The runway actor now carries `OverrideLengthUnits` and
`OverrideWidthUnits`. When both are set, `SimulationController` uses
them directly and skips the AABB reconstruction. For Warton
(2296 m × 45 m) the overrides are `229600` and `4500` (both in Unreal
centimetres).

Non-Warton, axis-aligned meshes without overrides still fall back to
the AABB projection.

## Terrain streaming and the ground clamp pad

Cesium tiles stream in and out at LOD boundaries during a session. The
runway actor is a fixed-world-space rectangle at a single Z. The
Cesium terrain at the runway centre climbs from roughly Z=6559.81 in
the editor to higher values as tiles refine at runtime. That means an
aircraft using the runway actor's Z as its landing altitude can end
up clipping below the surface.

An invisible collision pad (`SM_RunwayGroundPad`) sits across the full
runway strip at the runway actor's Z with hidden-in-game enabled and
`BlockAll` collision. Any ground trace during landing, taxi, or roll
hits the pad regardless of the current tile state, so aircraft settle
on a consistent surface.

The v1.0 landing code path still writes the runway actor's Z directly
into the aircraft state during touchdown and taxi. A follow-up patch
replaces the direct write with a line trace against the pad, which
avoids the class of bugs where a very high-LOD tile still slightly
disagrees with the pad Z.

## VR performance envelope for the Cesium scene

Cesium on VR at 72 Hz needs the engine tuned around what the tileset
streams every frame. The Warton scene settles at roughly 30 ms game
thread and 22 ms GPU on a Quest 3 via Link with the following
configuration:

- `r.RayTracing = False`
- `r.Shadow.Virtual.Enable = 0` (VSM off)
- `r.Streaming.PoolSize = 3000` (Cesium tiles need well above the VR
  default)
- `r.StaticMeshLODDistanceScale = 0.3`
- Aggressive sky-atmosphere sample-count trims
- Foveated rendering on

The `r.Streaming.PoolSize` change is load-bearing. The Cesium tileset
at Warton streams enough distinct meshes per frame that the VR
default pool traps the tile streamer in eviction thrashing.

Tuning is per-scene rather than global: a lower-detail level would
prefer different trade-offs.
