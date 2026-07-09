# AutopilotGenerated — drop-in guide

This directory is the landing zone for Simulink / Embedded Coder
output produced by the standalone `autopilot-mbd` repository. The
CLEARANCE-side `ClearanceAutopilotMBD` module detects it at build time
and switches from a compile-only stub to the real generated model.

Contents of this file are what a future integrator (probably me,
weeks later) needs to look at when picking up where the toolchain
setup left off.

---

## 1. Directory layout expected by ClearanceAutopilotMBD.Build.cs

```
ThirdParty/AutopilotGenerated/
├── include/            <-- generated .h files
├── src/                <-- generated .c files (only used if lib/ absent)
└── lib/                <-- optional static library
    └── autopilot.lib   (Windows)
    └── libautopilot.a  (Linux)
```

`Build.cs` prefers `lib/autopilot.lib` when present; otherwise the
integrator is expected to add the .c files to the module's own
compilation (uncomment the `PrivateAdditionalFiles` block in the
constructor).

## 2. What the autopilot-mbd CI produces

The workflow at `autopilot_repo/.github/workflows/ci.yml` uploads two
artefacts on every green build:

- `autopilot-generated-c` — the .c/.h files under `codegen_out/`
  after `rtwbuild(model)`. This is the drop-in payload for
  `ThirdParty/AutopilotGenerated/`.
- `autopilot-sim-artefacts` — smoke sim outputs, Test Manager
  results, traceability CSV/HTML. Not consumed by CLEARANCE - kept
  for compliance / audit.

## 3. Drop-in procedure

1. Download the `autopilot-generated-c` artefact from the latest
   green build of `autopilot-mbd`.
2. Extract into `include/` (headers) and `src/` (sources) below this
   README. If Embedded Coder produced a static library instead,
   place `autopilot.lib` in `lib/`.
3. Open `Plugins/ClearanceSim/Source/ClearanceAutopilotMBD/Public/AutopilotWrapper.h`.
   Uncomment the `#include "autopilot.h"` (or whatever the actual
   header filename is) inside the
   `CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN` guard.
4. Regenerate project files, rebuild. `Build.cs` will detect
   `include/` and define `CLEARANCE_AUTOPILOT_MBD_HAVE_CODEGEN=1`
   automatically, which flips the wrapper from stub to real.

## 4. Version pinning

Generated code from Simulink is not source-of-truth — the .slx model
is. When updating this drop-in, note the source commit of
`autopilot-mbd` in a sibling `VERSION.txt` (or commit message) so the
provenance chain from model → generated code → integrated build is
audit-trailable.

## 5. What to check after landing generated code

- `FAutopilotWrapper::HasGeneratedCode()` returns `true` at runtime.
- `Step(In)` outputs track the smoke sim recorded in
  `ci_artifacts/simOut_smoke.mat` on the same input sequence
  (within numeric tolerance).
- Automation test suite still passes:
  `Automation RunTests Clearance.*` in the editor session frontend.
