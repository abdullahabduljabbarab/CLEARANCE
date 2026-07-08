# clearance_hla_subscriber

Standalone IEEE 1516-2010 (HLA Evolved) federate that connects to CLEARANCE's
federation and prints every `ATCManagedAircraft` attribute update it
receives. Companion to the `StandaloneDDSSubscriber` — same "prove the
wire works from a second process" pattern.

## What it does

1. Contacts `rtinode.exe` (must already be running — see
   `../ThirdParty/OpenRTI/README.md`).
2. Joins the `CLEARANCE` federation execution (creates it if absent).
3. Subscribes to `ATCManagedAircraft.{SquawkCode, FlightPhase,
   ActiveClearance, ATCFacility}` from the FOM extension XML.
4. Loops on `evokeMultipleCallbacks`. Every attribute update from the
   CLEARANCE publisher fires a `reflectAttributeValues` callback here,
   which decodes the four attributes and prints a coloured line.

## Build

```powershell
.\build_subscriber.bat
```

Needs Visual Studio 2022 or 2026 with C++ Build Tools. Produces
`clearance_hla_subscriber.exe` in this folder. Auto-copies the three
OpenRTI DLLs (`OpenRTI.dll`, `librti1516e.dll`, `libfedtime1516e.dll`)
alongside so the exe launches with no PATH setup.

## Live demo (three windows)

**Window 1 — `rtinode.exe`:**

```powershell
& "..\ThirdParty\OpenRTI\bin\rtinode.exe"
```

Silent — listens on `127.0.0.1:14321`. Leave it running.

**Window 2 — CLEARANCE editor:** load a scenario with aircraft.
Console → `clearance.hla.join`. Should log
`[HLA] Joined federation 'CLEARANCE'`.

**Window 3 — this subscriber:**

```powershell
.\clearance_hla_subscriber.exe
```

Should print connect + subscribe messages, then live output like:

```
[HLA-SUB] #1  DLH101 -> ATCManagedAircraft  Squawk=1200 Phase=Enroute  Facility=CLR_APP
[HLA-SUB] #2  BAW103 -> ATCManagedAircraft  Squawk=7700 Phase=Enroute  Facility=CLR_APP
[HLA-SUB] #3  UAL110 -> ATCManagedAircraft  Squawk=1200 Phase=Enroute  Facility=CLR_APP
...
```

`#N` counter climbs at ~5-10Hz depending on how many aircraft the sim is
tracking. Ctrl+C to resign cleanly.

## Command line arguments

```
clearance_hla_subscriber.exe [federation] [federate] [fom-path]
```

Defaults: `CLEARANCE` / `CLEARANCE-Subscriber` / `..\FOM\ClearanceRPR-FOM.xml`.

## Why this matters for the portfolio

DDS runtimes ship visualisation tools (RTI Admin Console for RTI Connext,
Fast DDS Monitor / rtiddsspy for eProsima). HLA has no equivalent open-
source visual — the wire protocol between federates and the RTI is
implementation-specific (OpenRTI's is proprietary and undocumented), so
Wireshark and generic monitoring tools can't dissect it meaningfully.

This subscriber IS the visual — a second federate written from the same
`FClearanceHLAFederate` codebase, subscribing to the FOM extension
authored for CLEARANCE, printing live attribute updates. Proves the full
publish→RTI→subscribe round trip on the fourth interoperability wire,
using only code you can point to in the repo.

For the video, run all three windows tiled: rtinode silent, CLEARANCE
publishing on its instructor panel, and this subscriber scrolling live
updates. That's the HLA equivalent of the RTI Admin Console screenshot.
