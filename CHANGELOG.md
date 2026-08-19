# Changelog

All notable changes to this project are recorded here. Dates use the
`YYYY-MM-DD` format. The project follows a lightweight release cadence
tied to portfolio milestones rather than semantic versioning.

## v1.0 — 2026-08-19

First public release. End-to-end networked ATC and air defence synthetic
training ecosystem with the full four-month engineering surface visible
from a single build.

### Simulation core

- Authoritative airspace state owned by `AClearanceAirspaceManager`.
- Physics constrained aircraft behaviour: bank-limited turns, category
  climb rates, wake turbulence separation from the ICAO Doc 4444 matrix,
  wind drift on ground track, service ceilings, speed envelopes.
- Conflict detection with a three-level alert ladder
  (Advisory / Warning / Critical) and TCAS-style Resolution Advisories.
- Aircraft spawner with sector entry pacing and difficulty scaling.
- Cascade PID autopilot generated from Simulink and instanced per
  aircraft. See [autopilot-mbd](https://github.com/abdullahabduljabbarab/autopilot-mbd).

### Sensor stack

- Pulsed radar signal processor generated from Simulink:
  MVDR beamforming, matched filter, range-Doppler, CA-CFAR. One instance
  per radar site. See [radar-mbd](https://github.com/abdullahabduljabbarab/radar-mbd).
- Multi-radar fusion with per-aircraft coverage evaluation.
- Radar coverage heatmap available on the instructor scope.
- Electronic warfare: self-jamming that degrades a single aircraft's
  return, and chaff bursts that bloom into primary-only ghost returns.

### Weapons

- Vertical-launch surface-to-air missile with boost, pitchover, and
  terminal guidance. The Simulink 3-DOF missile model integrates through
  the same generation and wrapper pattern as the autopilot and radar,
  but the v1.0 live guidance path uses a C++ pursuit fallback; the
  generated model is documented as an integration companion rather than
  the live driver. See [missile-mbd](https://github.com/abdullahabduljabbarab/missile-mbd).

### Distributed simulation

- DIS (IEEE 1278.1) codec covering Entity State, Fire, Detonation,
  Electromagnetic Emission, Transmitter, and Signal PDUs.
- Fast DDS (OMG DDS via eProsima) publisher.
- RTI Connext DDS publisher on a second domain, sharing the IDL schema.
- HLA (IEEE 1516-2010) federate via OpenRTI using an RPR-FOM derived
  `ATCManagedAircraft` extension.
- All four wires publish from the same authoritative tick.
- See [clearance-federation](https://github.com/abdullahabduljabbarab/clearance-federation).

### Roles and networking

- VR operator in a diegetic tower cab: fingertip touch on world-space
  widgets, physical nine-button console, operator scope on a desk
  monitor, voice interaction.
- Desktop instructor with truth scope, scenario injects, PIP cameras
  (tower, chase, approach, overview, operator), simulation time
  controls, checkpoints, replay, performance reports, transcript,
  federation controls, and AAR export.
- LAN two-machine play with direct-IP join.

### Voice

- Bundled whisper.cpp server for offline speech recognition.
- Piper voices for speech synthesis, with an Edge TTS fallback.
- Real ATC phraseology parser covering heading, altitude, speed,
  approach clearance, handoff, and emergency responses with readbacks.
- Ten role-coloured transcript covering Operator, Pilot, System,
  Instructor, Tower, ACC, AWACS, GCI, ATIS, and MET.

### Tooling

- Full session recorder and replay with scrub bar and playback speed
  control.
- Checkpoint save and restore for scenario rehearsal.
- After Action Report exported as Markdown.
- In-station reference wiki accessible during a session.

### World

- Cesium streamed real-world terrain.
- Demo build at Warton airfield (EGNO).
- Seven authored training scenarios: Baltic Intercept, Hijack Response,
  Mass Divert, Mayday Engine Fire, NORDO Inbound, Cold War Probe,
  Mixed Ops.

### Verification and validation

- 52 automated tests mapped across 69 requirements covering DIS,
  federation, communications, safety, scoring, simulation, and radar.
- Manual verification procedures using independent processes and
  external tooling (Wireshark, RTI Administration Console, standalone
  subscribers).

### Packaging

- Windows 11 packaged build available on the [Releases page](../../releases/latest).
- Voice services (whisper.cpp, Piper, Edge TTS bridge) shipped as
  standalone executables inside the package so end users do not need a
  Python installation.
- Sample scenarios and the Warton map included in the shipped content.
