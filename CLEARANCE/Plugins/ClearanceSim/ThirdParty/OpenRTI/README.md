# OpenRTI 0.10.0 — vendored for CLEARANCE HLA integration

`ClearanceHLA` module links against these binaries.

## What's here

- `include/rti1516e/` — IEEE 1516-2010 (HLA Evolved) public headers
- `lib/rti1516e.lib` — RTIambassador + FederateAmbassador API import lib
- `lib/fedtime1516e.lib` — LogicalTime factory + interval helpers
- `lib/OpenRTI.lib` — core runtime (transport, message dispatch)
- `bin/librti1516e.dll` — HLA-Evolved API runtime
- `bin/libfedtime1516e.dll` — logical time runtime
- `bin/OpenRTI.dll` — core runtime
- `bin/rtinode.exe` — standalone RTI process federates connect to

## How the vendored binaries were built

Source: `https://github.com/onox/OpenRTI.git` (commit at time of vendoring).

### Prerequisites

- CMake 3.5+ (built with 4.3.4)
- Visual Studio 2022 or 2026 with C++ workload
- Windows SDK 10.0.26100 (auto-picked by CMake)

### Build recipe (Windows x64 Release)

```powershell
$src = "<clone dir>\OpenRTI"
$build = "<any build dir>"
$env:CXXFLAGS = "/EHsc /Zc:__cplusplus"   # critical - see below

cmake -S $src -B $build `
    -G "Visual Studio 18 2026" -A x64 `
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 `
    -DCMAKE_CXX_STANDARD=17 `
    -DOPENRTI_ENABLE_RTI13=OFF `
    -DOPENRTI_ENABLE_RTI1516=OFF

cmake --build $build --config Release `
    --target rti1516e --target fedtime1516e --target OpenRTI --target rtinode `
    --parallel 4
```

Outputs land at `$build/{lib,bin}/Release/`.

### CMake 4.x compat patch

OpenRTI's `CMakeLists.txt` still uses `cmake_minimum_required(VERSION 2.8.12)`
called AFTER `project()`. CMake 4.x rejects both. Before configuring, edit
the top of `CMakeLists.txt`:

```
cmake_minimum_required(VERSION 3.5)   # add this line 1
project(OpenRTI CXX)                   # already line 1 in upstream
# delete the existing cmake_minimum_required(VERSION 2.8.12) line
```

### Why `/Zc:__cplusplus`

`rti1516e/RTI/SpecificConfig.h` gates `std::auto_ptr` vs `std::unique_ptr`
on `__cplusplus < 201703L`. MSVC reports `__cplusplus = 199711L` by default
even when compiling as C++17. `/Zc:__cplusplus` makes it report the actual
language version so the header picks `unique_ptr`, matching modern consumers
(Unreal Engine's C++20 mode). Skip the flag and you get link errors at
`RTIambassadorFactory::createRTIambassador` because the DLL exports the
`auto_ptr` variant.

## Running the HLA demo

`ClearanceHLA` is a federate. It needs an RTI process (`rtinode.exe`)
listening on the loopback before joining.

### Start the RTI

Open a terminal:

```powershell
& "<CLEARANCE root>\Plugins\ClearanceSim\ThirdParty\OpenRTI\bin\rtinode.exe"
```

Default listen: TCP `127.0.0.1:14321`. Add `-i <address>:<port>` to bind
elsewhere. Leave the terminal open - closing it kills the federation.

### Join from CLEARANCE

In-editor console (with a scenario running):

```
clearance.hla.join
```

Uses defaults: federation `CLEARANCE`, federate `CLEARANCE-Instructor`,
FOM `<ProjectPluginsDir>/ClearanceSim/FOM/ClearanceRPR-FOM.xml`.

Output Log should show:

```
[HLA] Joined federation 'CLEARANCE' as 'CLEARANCE-Instructor' with FOM '...'
[HLA] EmitStates SiteId=1 NumStates=3 Sample=[AFR101,UAL102,AFR103] UpdatesTotal=12
```

`UpdatesTotal` increments per aircraft per tick.

### Verify from a second federate (optional)

The `rtinode` includes a companion `hla-test-lrc` utility; or a second
CLEARANCE instance can join the same federation and see peer object updates
(receive path is a follow-up - MVP is publish-only).

## Runtime notes

- All four DLLs (`librti1516e.dll`, `libfedtime1516e.dll`, `OpenRTI.dll`,
  and `rtinode.exe` if launched from the same folder) must be in the same
  directory or on `PATH`. UBT auto-copies the three lib DLLs to the game's
  `Binaries/Win64/` via `RuntimeDependencies` — `rtinode.exe` is manual.
- OpenRTI is LGPL v3. Dynamic linking (which we do) keeps the CLEARANCE
  source proprietary. See `LICENSE`, `lgpl-2.1.txt`, `lgpl-3.txt` under
  the OpenRTI source tree for full terms.
