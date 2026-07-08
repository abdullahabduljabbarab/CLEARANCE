# Standalone DDS Subscriber

*Sits at `Plugins/ClearanceSim/StandaloneDDSSubscriber/` — deliberately
outside the UBT source tree so Unreal doesn't try to compile
`subscriber_main.cpp` into the ClearanceDDS plugin module.*

Cross-process C++ subscriber to the six `clearance/*` DDS topics.
Runs **outside Unreal** — separate PID, separate address space,
so Fast DDS cannot use its intra-process transport shortcut.
That means **user data samples show up on the wire** and Wireshark's
`rtps` filter captures them for the demo video.

## Build

```
build_subscriber.bat
```

Needs Visual Studio 2022 or 2026 with the C++ Build Tools workload
installed. `vswhere.exe` (ships with the VS Installer) is used to
locate `vcvars64.bat`. The Fast DDS SDK is expected at
`C:\Program Files\eProsima\fastdds 3.6.1.0` — override with the
`FASTDDS_HOME` environment variable if installed elsewhere.

Produces `clearance_dds_subscriber.exe` in this folder.

## Run

```
clearance_dds_subscriber.exe [domain]
```

Domain defaults to 0. Must match whatever CLEARANCE is publishing on
(the console command inside CLEARANCE is `clearance.dds.start <N>`).

Ctrl-C to exit — the total received count is printed on shutdown.

## What you should see

- `[AircraftState] entity=...` per aircraft per tick
- `[EmissionSnapshot] entity=... emitter=4830 painted=N` per radar
- `[TransmitterState] entity=... freq=121500000Hz state=1` per radio
- `[FireEvent] firer=... target=... event=...` on SCRAMBLE
- `[DetonationEvent] firer=... target=... result=2 event=...` on intercept
- `[SignalEvent] entity=... text="Speedbird 472..."` on voice comms

Every 5 seconds a `[rate]` line prints to stderr with the running
sample total.

## For the demo video

Split-screen: CLEARANCE + Wireshark + this subscriber's terminal.
Run `clearance.dds.start` in the sim, then run this executable. You'll
see:

1. RTPS traffic in Wireshark climbing steadily (not just discovery)
2. Sample output scrolling in the subscriber terminal
3. Ability to correlate: on SCRAMBLE, both the FireEvent line in the
   terminal AND a DATA sample in Wireshark appear at the same
   instant, on the same PDU

That correlation - "sim publishes, wire carries, second process
receives" - is the money shot for federation interop.
