# RadarGenerated - drop-in guide

Landing zone for Simulink / Embedded Coder output produced by the
standalone `radar-mbd` repository. The CLEARANCE-side
`ClearanceRadarMBD` module detects it at build time and switches
from a compile-only stub to the real generated radar DSP.

## 1. Directory layout expected by ClearanceRadarMBD.Build.cs

```
ThirdParty/RadarGenerated/
├── include/            <-- generated .h files
│   ├── radar.h
│   ├── radar_types.h
│   ├── radar_private.h
│   ├── rtwtypes.h
│   ├── rtw_continuous.h    (vendored from MATLAB simulink/include)
│   ├── rtw_solver.h        (vendored from MATLAB simulink/include)
│   ├── rt_defines.h
│   ├── rt_nonfinite.h
│   └── rtGetNaN.h
└── src/                <-- generated .c files
    ├── radar.c
    ├── rt_nonfinite.c
    └── rtGetNaN.c
```

Build.cs auto-detects presence of `include/` and flips
`CLEARANCE_RADAR_MBD_HAVE_CODEGEN=1`. The compilation shim at
`Plugins/ClearanceSim/Source/ClearanceRadarMBD/Private/RadarGeneratedUnit.cpp`
`#include`s `radar.c` (plus the runtime helpers) so UBT compiles
them without needing a per-file compilation rule.

## 2. What the radar-mbd CI produces

The workflow at `radar_repo/.github/workflows/ci.yml` uploads two
artefacts on every green build:

- `radar-generated-c` - the `.c` / `.h` files after `rtwbuild('radar')`.
  This is the drop-in payload.
- `radar-sim-artefacts` - MATLAB sim outputs, traceability CSV/HTML,
  reference baseline. Not consumed by CLEARANCE.

## 3. Drop-in procedure

1. Download the `radar-generated-c` artefact from the latest green
   build of `radar-mbd`, or run `rtwbuild('radar')` locally.
2. Extract into `include/` and `src/` below this README. Two
   Simulink Coder runtime headers (`rtw_continuous.h`, `rtw_solver.h`)
   are also needed - vendor them from
   `<MATLAB_ROOT>/simulink/include/` if they aren't already present.
3. Regenerate project files, rebuild the plugin. `Build.cs` picks
   up the drop automatically.

## 4. Verifying

- `FRadarWrapper::HasGeneratedCode()` returns `true` at runtime.
- Console command `clearance.radar.mbd.test` reports
  `HasGeneratedCode: YES (real DSP)`.
- Automation test suite still passes:
  `Automation RunTests Clearance.*` in the editor session frontend.

## 5. Version pinning

Generated code from Simulink is not source-of-truth - the .slx
model is. When updating this drop-in, note the source commit of
`radar-mbd` in a sibling `VERSION.txt` (or the commit message) so
the provenance chain from model to generated code to integrated
build stays audit-trailable.
