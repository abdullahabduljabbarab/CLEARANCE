# CLEARANCE Requirements

**Project:** CLEARANCE
**Author:** Abdullah Ameed Abduljabbar
**Role:** Systems Designer
**Status:** Shipped verification traceability document.

Traceability table for the requirements currently covered by automation tests, grouped by domain and traced to their verifying tests. Deferred integration and manual coverage is called out separately at the end. Each REQ-ID is tagged in a leading code comment on the tests that verify it, so the truth lives in the source and this document just tabulates.

Companion to [`V_AND_V_PLAN.md`](V_AND_V_PLAN.md) (the strategy and process) and the [`Plugins/ClearanceSim/Source/ClearanceSim/Private/Tests/`](../Plugins/ClearanceSim/Source/ClearanceSim/Private/Tests/) directory (the tests themselves).

## Numbering scheme

```
REQ-<DOMAIN>-<###>
```

| Prefix        | Domain                                                  |
| ------------- | ------------------------------------------------------- |
| `REQ-DIS-`    | IEEE 1278.1 DIS wire-format compliance                  |
| `REQ-FED-`    | Federation, RPR-FOM, cross-vendor mapping               |
| `REQ-COMMS-`  | ATC comms: phraseology, readback, instruction gate      |
| `REQ-SAFETY-` | Doctrinal separation, wake, envelope compliance         |
| `REQ-SCORE-`  | Scoring and difficulty adjustment                       |
| `REQ-SIM-`    | Session recorder, playback, checkpoint                  |
| `REQ-RADAR-`  | Monostatic radar range equation and detection physics   |

Numbers ascend inside a domain and are never re-used. Deprecated REQ-IDs stay in place with a `[DEPRECATED]` marker rather than being reused.

## REQ-DIS: IEEE 1278.1 wire-format compliance

Verified by the tests under `Tests/ClearanceDIS*Tests.cpp`. Every requirement traces to a specific section of the IEEE 1278.1-2012 standard.

| ID | Requirement | Verified by | Source |
|---|---|---|---|
| REQ-DIS-001 | Entity State PDU shall serialise to exactly 144 bytes for a valid entity with no articulations. | `Clearance.DIS.EntityStatePDU.Roundtrip` | IEEE 1278.1 §7.3.4 |
| REQ-DIS-002 | Entity State PDU header shall carry PDU type = 1, protocol family = 1 (Entity Information). | `Clearance.DIS.EntityStatePDU.Roundtrip` | IEEE 1278.1 §5.2 |
| REQ-DIS-003 | Entity State PDU header shall carry protocol version = 7 (IEEE 1278.1-2012). | `Clearance.DIS.EntityStatePDU.Roundtrip` | IEEE 1278.1 §5.2.24 |
| REQ-DIS-004 | Entity State PDU shall write ForceId as a single uint8 at spec offset 18. | `Clearance.Federation.RPRFOM.ForceId.*` | IEEE 1278.1 §7.3.4.6 |
| REQ-DIS-005 | Fire PDU shall serialise to exactly 96 bytes. | `Clearance.DIS.FirePDU.Roundtrip` | IEEE 1278.1 §7.3.3 |
| REQ-DIS-006 | Fire PDU shall round-trip firing entity, target entity, munition entity, event number, munition kind, warhead kind, fuse kind, quantity, range, world location, and velocity byte-exactly. | `Clearance.DIS.FirePDU.Roundtrip` | IEEE 1278.1 §7.3.3 |
| REQ-DIS-007 | Fire PDU parser shall reject buffers shorter than the fixed 96 bytes. | `Clearance.DIS.FirePDU.MalformedRejection` | IEEE 1278.1 §7.3.3 |
| REQ-DIS-008 | Detonation PDU shall serialise to exactly 104 bytes for a proximate-detonation result. | `Clearance.DIS.DetonationPDU.Roundtrip` | IEEE 1278.1 §7.3.4 |
| REQ-DIS-009 | Detonation PDU shall round-trip firing / target / munition entity IDs matching the paired Fire event. | `Clearance.DIS.DetonationPDU.Roundtrip` | IEEE 1278.1 §7.3.4 |
| REQ-DIS-010 | Detonation PDU parser shall reject buffers shorter than the fixed 104 bytes. | `Clearance.DIS.DetonationPDU.MalformedRejection` | IEEE 1278.1 §7.3.4 |
| REQ-DIS-011 | Emission PDU shall serialise the fixed 100-byte header plus one variable-length track/jam block per painted entity. | `Clearance.DIS.EmissionPDU.Roundtrip` | IEEE 1278.1 §7.6.2 |
| REQ-DIS-012 | Emission PDU shall round-trip emitter name, function, frequency low/high, ERP, PRF, pulse width, and beam azimuth byte-exactly. | `Clearance.DIS.EmissionPDU.Roundtrip` | IEEE 1278.1 §7.6.2 |
| REQ-DIS-013 | Emission PDU shall enumerate every painted entity's Entity ID in the Track/Jam list. | `Clearance.DIS.EmissionPDU.Roundtrip` | IEEE 1278.1 §7.6.2 |
| REQ-DIS-014 | Emission PDU parser shall reject buffers with truncated track/jam blocks. | `Clearance.DIS.EmissionPDU.MalformedRejection` | IEEE 1278.1 §7.6.2 |
| REQ-DIS-015 | Transmitter PDU shall serialise to exactly 104 bytes. | `Clearance.DIS.TransmitterPDU.Roundtrip` | IEEE 1278.1 §7.7.2 |
| REQ-DIS-016 | Transmitter PDU shall round-trip radio entity ID, transmit state, input source, modulation, transmit frequency, and transmit power. | `Clearance.DIS.TransmitterPDU.Roundtrip` | IEEE 1278.1 §7.7.2 |
| REQ-DIS-017 | Transmitter PDU shall correctly encode operator entities as ForceId 0 with entity number = `kOperatorGroundStationEntity` (60000). | `Clearance.DIS.TransmitterPDU.OperatorEntity` | CLEARANCE convention |
| REQ-DIS-018 | Transmitter PDU parser shall reject buffers shorter than the fixed 104 bytes. | `Clearance.DIS.TransmitterPDU.MalformedRejection` | IEEE 1278.1 §7.7.2 |
| REQ-DIS-019 | Signal PDU header shall be exactly 32 bytes; payload shall be padded to a 32-bit boundary per §7.7.3. | `Clearance.DIS.SignalPDU.PaddingBoundary` | IEEE 1278.1 §7.7.3 |
| REQ-DIS-020 | Signal PDU shall round-trip radio reference ID, encoding class, TDL type, sample rate, and payload bytes. | `Clearance.DIS.SignalPDU.Roundtrip` | IEEE 1278.1 §7.7.3 |
| REQ-DIS-021 | Signal PDU shall route to the operator ground-station entity for operator-side voice transmissions. | `Clearance.DIS.SignalPDU.OperatorEntity` | CLEARANCE convention |
| REQ-DIS-022 | Signal PDU parser shall reject buffers with declared payload length exceeding the buffer bounds. | `Clearance.DIS.SignalPDU.MalformedRejection` | IEEE 1278.1 §7.7.3 |

## REQ-FED: Federation affiliation mapping

DIS ForceId values are used by the federation mapping layer. Drift in these mappings would break consistent affiliation handling across DIS and HLA/RPR-FOM style integrations.

| ID | Requirement | Verified by | Source |
|---|---|---|---|
| REQ-FED-001 | ForceId 0 shall represent Other/Unknown affiliation. | `Clearance.Federation.RPRFOM.ForceId.Other` | IEEE 1278.1 §7.3.4.6 |
| REQ-FED-002 | ForceId 1 shall represent Friendly affiliation. | `Clearance.Federation.RPRFOM.ForceId.Friendly` | IEEE 1278.1 §7.3.4.6 |
| REQ-FED-003 | ForceId 2 shall represent Opposing (Hostile) affiliation. | `Clearance.Federation.RPRFOM.ForceId.Hostile` | IEEE 1278.1 §7.3.4.6 |
| REQ-FED-004 | ForceId 3 shall represent Neutral affiliation. | `Clearance.Federation.RPRFOM.ForceId.Neutral` | IEEE 1278.1 §7.3.4.6 |
| REQ-FED-005 | Entity State PDU wire encoding shall write ForceId byte-exactly at spec offset 18 for every legal value. | `Clearance.Federation.RPRFOM.ForceId.*` | IEEE 1278.1 §7.3.4 |
| REQ-FED-006 | Non-standard (extended-enum) ForceId values shall be preserved on the wire, not clamped to a spec value. | `Clearance.Federation.RPRFOM.ForceId.UnknownPreserved` | Interop convention |

## REQ-COMMS: ATC communications and instruction validation

Requirements from the phraseology and instruction validator layers. The validator is a stateless gatekeeper: it decides whether an instruction is physically feasible before the mover ever sees it.

| ID | Requirement | Verified by | Source |
|---|---|---|---|
| REQ-COMMS-001 | A valid feasible instruction on a valid aircraft state shall return Accepted. | `Clearance.Comms.Validator.AcceptedBaseline` | Doc 4444, ICAO PANS-ATM |
| REQ-COMMS-002 | An instruction on an aircraft with `bIsValid=false` shall return `Rejected_InvalidCallsign`. | `Clearance.Comms.Validator.RejectInvalidCallsign` | CLEARANCE design |
| REQ-COMMS-003 | A system-issued go-around (`bIsGoAround=true`) shall bypass envelope checks and always return Accepted. | `Clearance.Comms.Validator.GoAroundBypasses` | ICAO safety-override doctrine |
| REQ-COMMS-004 | An instruction to an aircraft in the `Exiting` flight phase shall return `Rejected_AircraftExited`. | `Clearance.Comms.Validator.RejectExiting` | CLEARANCE design |
| REQ-COMMS-005 | An AltitudeChange above the aircraft category's service ceiling shall return `Rejected_PhysicallyImpossible`. | `Clearance.Comms.Validator.RejectAltitudeAboveCeiling` | Representative published aircraft performance data |
| REQ-COMMS-006 | An AltitudeChange below zero ft shall return `Rejected_PhysicallyImpossible`. | `Clearance.Comms.Validator.RejectAltitudeNegative` | CLEARANCE design |
| REQ-COMMS-007 | A SpeedChange below the aircraft category's minimum operating speed shall return `Rejected_PhysicallyImpossible`. | `Clearance.Comms.Validator.RejectSpeedBelowMin` | Representative published aircraft performance data |
| REQ-COMMS-008 | A SpeedChange above the aircraft category's max operating speed shall return `Rejected_PhysicallyImpossible`. | `Clearance.Comms.Validator.RejectSpeedAboveMax` | Representative published aircraft performance data |
| REQ-COMMS-009 | NaN and non-finite (Inf) target values shall be rejected as `Rejected_PhysicallyImpossible`. | `Clearance.Comms.Validator.RejectNonFinite` | IEEE 754 float handling |
| REQ-COMMS-010 | Military airframes (`bIsMilitary=true`) shall use the fighter envelope (Vmax ~1050 kts, ceiling 50000 ft) instead of the civil wake-category envelope. | `Clearance.Comms.Validator.MilitaryEnvelope` | CLEARANCE design; representative fighter performance envelope |

## REQ-SAFETY: separation and envelope doctrine

Tuning constants pinned to ICAO doctrine. Monotonic ordering invariants guard against inversions when someone edits the numbers.

| ID | Requirement | Verified by | Source |
|---|---|---|---|
| REQ-SAFETY-001 | Advisory horizontal separation shall be 8 nm. | `Clearance.Safety.HorizontalSeparation` | ICAO Doc 4444 |
| REQ-SAFETY-002 | Warning horizontal separation shall be 5 nm. | `Clearance.Safety.HorizontalSeparation` | ICAO Doc 4444 |
| REQ-SAFETY-003 | Critical horizontal separation shall be 3 nm. | `Clearance.Safety.HorizontalSeparation` | ICAO Doc 4444 |
| REQ-SAFETY-004 | Vertical minimum shall be 1000 ft (RVSM airspace). | `Clearance.Safety.VerticalMinimum` | ICAO RVSM (Annex 2 App 4) |
| REQ-SAFETY-005 | Wake separation matrix shall match ICAO Doc 4444 §5.8: Light-behind-Heavy 6 nm, Medium-behind-Heavy 5 nm, Light-behind-Medium 5 nm, Heavy-behind-Heavy 4 nm, standard minimum 3 nm. | `Clearance.Safety.WakeSeparationMatrix` | ICAO Doc 4444 §5.8 |
| REQ-SAFETY-006 | Alert level thresholds shall be strictly monotonic: Advisory > Warning > Critical (nm). | `Clearance.Safety.HorizontalSeparation` | Ordering invariant |
| REQ-SAFETY-007 | Every wake category's max operating speed shall exceed its min operating speed and lie within 50..500 kts. | `Clearance.Safety.CategoryPerformance` | Representative published aircraft performance data |
| REQ-SAFETY-008 | Military fighter envelope shall strictly exceed the Heavy civil category on max operating speed and service ceiling. | `Clearance.Safety.CategoryPerformance` | Representative fighter performance envelope |
| REQ-SAFETY-009 | `GetEffectivePerformance(cat, bIsMilitary)` shall route to the fighter envelope when `bIsMilitary=true` and to the wake-category envelope otherwise. | `Clearance.Safety.EffectivePerformanceRouting` | CLEARANCE design |

## REQ-SCORE: scoring and difficulty

`UClearanceScoring` runs as a plain UObject with no world dependency. These requirements verify the reward and penalty table plus session bookkeeping.

| ID | Requirement | Verified by | Source |
|---|---|---|---|
| REQ-SCORE-001 | Every `EIncidentType` shall map to a point delta matching the scoring table (`Points*` for reward, `Penalty*` for penalty). | `Clearance.Scoring.PointsPerIncident` | CLEARANCE scoring policy |
| REQ-SCORE-002 | `LogIncident` shall append exactly one `FIncidentRecord` per call to the session log. | `Clearance.Scoring.IncidentLog` | CLEARANCE design |
| REQ-SCORE-003 | `ResetSession` shall clear score, log, per-category counters, and reset spawn interval to `BaseSpawnIntervalSeconds`. | `Clearance.Scoring.ResetSession` | CLEARANCE design |
| REQ-SCORE-004 | A `SuccessfulHandoff` shall shrink the spawn interval by exactly `DifficultySecondsPerHandled` seconds. | `Clearance.Scoring.DifficultyRamp` | CLEARANCE design |
| REQ-SCORE-005 | The spawn interval shall clamp at `MinSpawnIntervalSeconds` regardless of the number of successful handoffs. | `Clearance.Scoring.DifficultyRamp` | CLEARANCE design |
| REQ-SCORE-006 | `GetEfficiency()` shall return a value in [0, 100] regardless of the sequence of incidents recorded. | `Clearance.Scoring.RecordInstruction` | Bounds invariant |

## REQ-SIM: session recorder and playback

The recorder captures per-tick snapshots and timestamped events. Testable without a world; the `ApplySnapshotTo` path (which requires an AirspaceManager actor) is exercised via integration tests only.

| ID | Requirement | Verified by | Source |
|---|---|---|---|
| REQ-SIM-001 | `StartRecording()` shall set `IsRecording()` true; `StopRecording()` shall set it false. | `Clearance.Recorder.StartStop` | CLEARANCE design |
| REQ-SIM-002 | `CaptureSnapshot` shall append one snapshot per call while recording; shall be a no-op when not recording. | `Clearance.Recorder.StartStop` | CLEARANCE design |
| REQ-SIM-003 | `GetDurationSeconds()` shall return `(last snapshot timestamp - first snapshot timestamp)`; zero for empty timelines. | `Clearance.Recorder.Duration` | CLEARANCE design |
| REQ-SIM-004 | `FindSnapshotAt(secondsFromStart)` shall return the most recent snapshot with absolute time <= `(firstTimestamp + secondsFromStart)`, clamping to the first snapshot on negative input and to the last on overshoots. | `Clearance.Recorder.PoseBack` | CLEARANCE design |
| REQ-SIM-005 | `FindSnapshotAt` shall return `nullptr` only when the timeline is empty. | `Clearance.Recorder.PoseBack` | CLEARANCE design |
| REQ-SIM-006 | Every field of `FAircraftState` (altitude, speed, heading, callsign, flight phase) captured in a snapshot shall be identical to the input under `FindSnapshotAt` at the same time. | `Clearance.Recorder.PoseBack` | CLEARANCE design |
| REQ-SIM-007 | `LogEvent` shall append to the events list while recording; `GetEventsInRange(from, to)` shall return events with timestamps inclusively in `[from, to]`. | `Clearance.Recorder.EventsInRange` | CLEARANCE design |
| REQ-SIM-008 | `ClearRecording` shall empty snapshots and events but leave the `IsRecording` flag untouched. Callers who want to halt shall call `StopRecording()` explicitly. | `Clearance.Recorder.Clear` | CLEARANCE design |

## REQ-RADAR: radar range equation

Verified by `Tests/ClearanceRadarEquationTests.cpp`. Traces to Skolnik, *Introduction to Radar Systems*, 3rd ed., ch. 2-4 (the monostatic pulse-radar equation and single-pulse detection theory).

| ID | Requirement | Verified by | Source |
|---|---|---|---|
| REQ-RADAR-001 | Received power shall follow the R^4 range law: doubling range shall divide received power by 16. | `Clearance.Radar.Equation.R4RangeLaw` | Skolnik §2.1 |
| REQ-RADAR-002 | Received power shall be linear in target radar cross-section (sigma). | `Clearance.Radar.Equation.RcsScaling` | Skolnik §2.1 |
| REQ-RADAR-003 | Receiver thermal noise floor shall equal `k * T * B * F` (Boltzmann * temperature * bandwidth * noise figure). | `Clearance.Radar.Equation.NoiseFloorKtbf` | Skolnik §2.4 |
| REQ-RADAR-004 | Wavelength shall equal `c / f` (speed of light divided by frequency). Validated at S-band (2.8 GHz) and X-band (10 GHz). | `Clearance.Radar.Equation.Wavelength` | Physics constant |
| REQ-RADAR-005 | Probability of detection at SNR = required-SNR shall equal 0.5 exactly (logistic centred on threshold). | `Clearance.Radar.Equation.PdAtCentre` | CLEARANCE design |
| REQ-RADAR-006 | Probability of detection shall be monotonically non-decreasing across a 60 dB SNR sweep. | `Clearance.Radar.Equation.PdMonotonic` | Skolnik §2.5 |
| REQ-RADAR-007 | Default parameters (1.4 MW peak, 34 dB antennas, 6 dB loss, 3 dB NF, 290 K, 1 MHz B, 2.8 GHz, 13 dB required SNR) shall place the Pd = 0.5 crossing for a 10 m² target between 40 and 200 nm; and Pd(Heavy 100 m²) > Pd(Light 1 m²) at 40 nm. | `Clearance.Radar.Equation.DefaultCalibration` | ASR-9 specification |
| REQ-RADAR-008 | `dB ↔ linear` conversion shall round-trip within numerical tolerance for the standard test values (0, 3, 6, 10, 13, 30, 60, -20, -40 dB); `LinearFromDb(0) = 1` exactly; `DbFromLinear(0)` returns the -1000 dB sentinel (not NaN or -inf). | `Clearance.Radar.Equation.DbRoundTrip` | CLEARANCE design |

## Coverage summary

| Domain | REQs | Tests | Ratio |
|---|---|---|---|
| DIS | 22 | 14 | 1.57 : 1 |
| FED | 6 | 5 | 1.20 : 1 |
| COMMS | 10 | 10 | 1.00 : 1 |
| SAFETY | 9 | 5 | 1.80 : 1 |
| SCORE | 6 | 5 | 1.20 : 1 |
| SIM | 8 | 5 | 1.60 : 1 |
| RADAR | 8 | 8 | 1.00 : 1 |
| **Total** | **69** | **52** | **1.33 : 1** |

A ratio greater than 1 means some tests cover multiple related requirements. This is expected where one test verifies a grouped invariant, such as recorder pose-back semantics or DIS PDU round-trip behaviour. A 1:1 ratio is also acceptable where the requirement is narrow.

Uncovered subsystems (deferred to integration-test or manual pass):

- **Full actor-backed conflict detector lifecycle.** Requires an `AClearanceAirspaceManager` actor spawned in a `UWorld`. Pure logic covered under REQ-SAFETY; the full runtime path is manual verification via the instructor scope during scenario runs.
- **Full TCAS RA execution path through the Simulation Controller.** As above.
- **Chaff and wake turbulence detection.** As above.
- **Phraseology `Interpret`.** Requires an `AClearanceSimulationController`. Manual verification via the `clearance.say` console command against a running scenario.
- **HLA federation join lifecycle.** Requires a running `rtinode.exe`. Documented in `DIS_INTEROPERABILITY.md` §4b as a manual test.

These are captured in `V_AND_V_PLAN.md` as manual verification procedures, not swept under the rug.

## Adding a new requirement

1. Write the test first. Tag the top comment block with the new `REQ-<DOMAIN>-<###>` you're claiming.
2. Add a row to the domain's table above with the requirement text, the test that verifies it, and the source (an IEEE / ICAO section reference, a certification data source, or "CLEARANCE design").
3. Bump the count in the Coverage summary table.

That's it. The convention is intentionally lightweight. Heavier process wouldn't survive a portfolio project's cadence, and V&V rigour is proven by the test-tagging discipline, not by the document's typography.
