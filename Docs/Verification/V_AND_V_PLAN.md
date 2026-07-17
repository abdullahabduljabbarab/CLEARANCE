# CLEARANCE Verification and Validation Plan

The strategy document: how requirements are verified. Companion to [`Requirements.md`](Requirements.md) (what the requirements are) and the [`Plugins/ClearanceSim/Source/ClearanceSim/Private/Tests/`](../Plugins/ClearanceSim/Source/ClearanceSim/Private/Tests/) directory (the tests themselves).

If `Requirements.md` answers "what is CLEARANCE supposed to do?", this doc answers "how do we prove it does?".

## 1. Purpose and scope

Verification and validation on a portfolio project is a proportionality exercise. CLEARANCE isn't going into a Category A avionics box, so it doesn't need DO-178C level rigour. It is a defence M&S portfolio piece, so it does need to demonstrate the discipline a Category A programme would exhibit: traceability, structured test tiers, and an honest accounting of what's automated versus manual.

### In scope

- Every requirement in `Requirements.md` (all 69 REQ-IDs across DIS, FED, COMMS, SAFETY, SCORE, SIM, and RADAR domains).
- Every automation test under `Tests/` and its REQ tag.
- Manual verification procedures for the subsystems that require an actor, a running federate, or a third-party tool (HLA join, RTI Admin Console, Wireshark, subscriber round-trip).

### Out of scope

- Formal safety case and DO-178C artefacts (design assurance level, MC/DC coverage, tool qualification). CLEARANCE is a demonstrator, not a certifiable product.
- Performance and load testing (frame rate under N aircraft, network saturation). Measured informally during scenario runs; not V&V.
- Localisation, accessibility, security testing. Not the portfolio narrative.

## 2. Test tiers

Three tiers, each with a different cost and confidence profile. The right test for a given requirement lives in the tier that gives the highest confidence at the lowest cost.

### Tier 1: unit (automated)

- **Definition.** Tests a single class, free function, or static helper in isolation, using `NewObject<>` in the transient package. No world, no actor spawn, no network, no filesystem. Runs sub-second.
- **Framework.** UE `IMPLEMENT_SIMPLE_AUTOMATION_TEST` under `EAutomationTestFlags::EditorContext | EngineFilter`.
- **Cost.** Very low. Sub-second execution, no environment prep.
- **Where they live.** `Plugins/ClearanceSim/Source/ClearanceSim/Private/Tests/*Tests.cpp`. One file per subsystem or requirement group.
- **When to use.** Any pure-logic requirement: algorithm correctness, data mapping, table lookups, wire-format serialisation. Every `REQ-DIS-*`, `REQ-FED-*`, `REQ-COMMS-*`, `REQ-SAFETY-*`, `REQ-SCORE-*`, `REQ-SIM-*`, and `REQ-RADAR-*` in CLEARANCE lives here.
- **Coverage.** 52 tests today across the seven domain files. See `Requirements.md` coverage table for the per-domain breakdown.

### Tier 2: integration (automated, but heavy)

- **Definition.** Tests interactions between multiple subsystems using a spawned `UWorld` and a minimal actor harness (typically an `AClearanceAirspaceManager` and an `AClearanceSimulationController`).
- **Framework.** UE `IMPLEMENT_COMPLEX_AUTOMATION_TEST` with a `FAutomationEditorCommonUtils::CreateNewMap()` style setup, or the `FUnrealAutomationTestFramework::EnqueueLatentCommand` idiom for tests that need multiple ticks.
- **Cost.** Medium. Seconds per test, requires a map load. Fragile against UE-side changes to actor lifecycle.
- **Where they would live.** `Tests/Integration/*Tests.cpp` in a separate folder so unit tests stay fast and integration tests can act as a gate step.
- **Status in CLEARANCE.** **Not implemented in this pass.** The subsystems that would need this tier (conflict detector, TCAS RA, wake turbulence detection, chaff cloud lifecycle, phraseology `Interpret`, scenario runner event triggering, checkpoint save/load) are documented in the Tier 3 manual verification list below. They are candidates for future integration-test conversion if a specific incident (a peer federate seeing wrong data, a scenario firing at the wrong time) warrants the fixture cost.

### Tier 3: manual (procedural, evidence-captured)

- **Definition.** Requirements that can only be exercised against external tooling (Wireshark, RTI Admin Console) or a running runtime (`rtinode.exe`, a second CLEARANCE instance). Verified by an operator following a written procedure and capturing evidence (screenshot, Output Log excerpt, wire capture).
- **Cost.** High. Requires bench setup, roughly 5-10 minutes per procedure. Can't run every commit; run pre-release and after touching the relevant subsystem.
- **Where they live.** Section 5 of this document. Each has a procedure, a pass criterion, and a suggested evidence-capture format.
- **Coverage.** 5 procedures covering the runtime-dependent gaps in Tiers 1 and 2.

### Which tier for a new requirement?

```
Pure-logic function?
  YES -> Tier 1 (unit test, ships with the code)
  NO  -> Requires an actor / world?
           YES -> Requires external tooling too?
                    YES -> Tier 3 (manual procedure)
                    NO  -> Tier 2 (integration test) OR Tier 3 if too costly
           NO  -> External tooling -> Tier 3
```

Default to the lowest-cost tier that works. Only escalate when the lower tier genuinely can't cover it.

## 3. Traceability

The tests themselves ARE the traceability record. Every test file has a leading comment block with the REQ-IDs it covers:

```cpp
// Covers requirements:
//   REQ-SCORE-001  Every EIncidentType shall map to a point delta...
//   REQ-SCORE-002  LogIncident shall append one FIncidentRecord...
```

`Requirements.md` collects these into a table. That table is the traceability matrix.

### Generating the matrix from source

The matrix in `Requirements.md` is currently hand-maintained. A CI job could regenerate it from the source with a one-liner grep and awk:

```bash
# scripts/gen_traceability.sh
# Emits a Markdown table mapping REQ-ID -> test file:line.
grep -rEn "REQ-[A-Z]+-[0-9]{3}" \
    Plugins/ClearanceSim/Source/ClearanceSim/Private/Tests/ \
  | awk -F':' '{
      match($0, /REQ-[A-Z]+-[0-9]{3}/, id);
      printf "| %s | %s:%s |\n", id[0], $1, $2
    }' \
  | sort -u
```

Output shape:

```
| REQ-COMMS-001 | ClearanceInstructionValidatorTests.cpp:14 |
| REQ-COMMS-002 | ClearanceInstructionValidatorTests.cpp:15 |
...
```

Committing this script into `Scripts/gen_traceability.sh` and running it as a pre-commit hook would keep the `Requirements.md` table automatically in sync with the source. For now the table is hand-updated when a REQ tag is added. Deliberately lightweight to suit portfolio-project cadence.

### Auditing drift

Two failure modes the traceability discipline guards against:

1. **REQ in the doc with no test.** Grep the REQ-ID across `Plugins/ClearanceSim/Source/ClearanceSim/Private/Tests/`. Zero hits means broken traceability. This should be a CI check.
2. **Test with a REQ tag that isn't in the doc.** The reverse of the above. Also a CI check.

Neither is currently automated. Both are one-liner shell scripts away from being CI gates and are on the roadmap for the CI/CD phase of the V&V build-out.

## 4. Coverage targets

CLEARANCE is a portfolio project, not a certifiable product, so these targets are self-imposed discipline goals rather than regulatory obligations.

### Target 1: REQ-ID automation coverage

**Every requirement in a "verifiable" domain shall have at least one automated test.**

Verifiable domains:

- REQ-DIS: 100% automated (wire format is pure C++)
- REQ-FED: 100% automated (mapping is pure C++)
- REQ-COMMS: 100% automated (validator is stateless)
- REQ-SAFETY: 100% automated (constants + envelope)
- REQ-SCORE: 100% automated (`UClearanceScoring` standalone)
- REQ-SIM: 100% automated (`UClearanceSessionRecorder` standalone)
- REQ-RADAR: 100% automated (range equation is pure C++)

**Current status.** 69 of 69 requirements in verifiable domains have at least one covering test. **Target met.**

### Target 2: manual verification coverage

**Every subsystem that can't reasonably be automated shall have a documented manual procedure with a pass criterion and evidence capture.**

Subsystems in this bucket: HLA federation join, RTI Connext live publisher, Fast DDS live publisher and subscriber, Wireshark wire inspection, two-federate federation. All documented in Section 5.

**Current status.** 5 of 5 procedures documented. **Target met.**

### Target 3: test cadence

- **Every commit:** Tier 1 tests run automatically (currently local; the CI/CD roadmap phase will move this to GitHub Actions).
- **Before every release, video, or portfolio update:** all Tier 1 tests and all Tier 3 manual procedures re-run and evidence re-captured.
- **When a subsystem is touched:** its own Tier 1 tests plus any Tier 3 procedure that involves it re-run before commit.

### Target 4: regression policy

If a test that was previously green is now red, the fix goes in before any new feature work. No new REQ-IDs get added while an existing one is failing.

Applies equally to Tier 3 manual procedures. If a procedure that used to produce clean evidence now produces a warning in Wireshark or a red status in RTI Admin Console, the subsystem is considered regressed until the cause is understood.

## 5. Manual verification procedures

Each procedure covers a Tier 3 requirement that Tier 1 or 2 can't reach. Follow the steps, capture the evidence, note the result.

Evidence captures live under `Docs/verification_evidence/<procedure>/<YYYY-MM-DD>/` as screenshots, log excerpts, or `.pcapng` captures. That folder is optional and gitignored. The important thing is that the procedure has been run.

### MP-01: Wireshark verifies DIS wire compliance

**Purpose.** Confirms every DIS PDU CLEARANCE emits parses cleanly in Wireshark's built-in IEEE 1278.1 dissector. Third-party validation of REQ-DIS-* wire-format claims that automation tests can't provide (the automation tests exercise our own encoder against our own decoder; Wireshark's dissector is independent of our code).

**Prerequisites.**

- Wireshark 4.x installed with the DIS dissector plugin enabled (default in stock installs).
- Loopback capture adapter selected in Wireshark.
- CLEARANCE editor with a scenario loaded and aircraft populated.

**Procedure.**

1. Start Wireshark capturing on the loopback adapter with display filter `udp.port == 3000`.
2. In CLEARANCE console: `clearance.dis.start`.
3. Wait 10 seconds. Confirm packet count is greater than 0 in Wireshark.
4. For each PDU type (1, 2, 3, 23, 25, 26): click a packet of that type, expand the DIS section in the dissector pane, and confirm every field is decoded (no "Malformed Packet" or grey-out).
5. Save the capture as `Docs/verification_evidence/mp-01/<YYYY-MM-DD>/dis-loopback.pcapng`.

**Pass criteria.**

- All six PDU types (1, 2, 3, 23, 25, 26) appear at expected rates (aircraft state ~5 Hz per aircraft, transmitter ~5 Hz per radio, fire and detonation in bursts, emission ~5 Hz per active radar).
- No "Malformed Packet" annotations from Wireshark for any DIS packet.
- Every field in the Entity State PDU that our documentation claims to encode is decoded to a value in the expected range.

**Covers.** All REQ-DIS-* via independent third-party dissection.

### MP-02: RTI Admin Console verifies Connext live publisher

**Purpose.** Confirms the ClearanceRTI publisher is a legitimate RTI Connext DDS federate discoverable by RTI's own tooling, not a mock or a Fast DDS impersonator. Third-party validation for the RTI Connext compliance claim.

**Prerequisites.**

- RTI Connext DDS 7.7.0 installed with a valid `rti_license.dat`.
- RTI Administration Console launched (from RTI Launcher, Tools).
- CLEARANCE editor with a scenario loaded.

**Procedure.**

1. Launch RTI Admin Console. Confirm domain 1 shows no participant initially.
2. In CLEARANCE console: `clearance.rti.start`.
3. Refresh Admin Console. Confirm the CLEARANCE participant appears on domain 1 within 5 seconds.
4. Expand the participant. Confirm all 6 topics (`clearance/aircraft/state`, `clearance/weapons/fire`, `clearance/weapons/detonation`, `clearance/radar/emission`, `clearance/radio/transmitter`, `clearance/radio/signal`) appear.
5. Confirm the vendor ID column shows `Real-Time Innovations, Inc. (RTI) - Connext DDS (0x0101)`.
6. Screenshot the Admin Console showing all 6 topics and the vendor stamp. Save under `Docs/verification_evidence/mp-02/<YYYY-MM-DD>/`.

**Pass criteria.**

- Participant discovered on the correct domain.
- All 6 topics visible.
- Vendor ID exactly `0x0101` (RTI). If it says `0x010f` (eProsima), we're accidentally federating on Fast DDS: regression.
- Best-effort QoS on every DataWriter (matches CLEARANCE's shipped QoS).

**Covers.** REQ-FED interop claims for the commercial DDS runtime.

### MP-03: standalone subscribers verify round-trip

**Purpose.** Confirms CLEARANCE isn't just publishing to a void. An independent process joins the same domain or federation and receives attribute updates.

**Prerequisites.**

- `clearance_dds_subscriber.exe` built under `Plugins/ClearanceSim/StandaloneDDSSubscriber/`.
- `clearance_hla_subscriber.exe` built under `Plugins/ClearanceSim/StandaloneHLASubscriber/`.
- `rtinode.exe` accessible under `Plugins/ClearanceSim/ThirdParty/OpenRTI/bin/`.

**Procedure, DDS side.**

1. In CLEARANCE console: `clearance.dds.start`.
2. Double-click `clearance_dds_subscriber.exe` from Explorer.
3. Confirm the subscriber console shows live samples scrolling on all six `clearance/*` topics within 5 seconds.
4. Let it run for 30 seconds. Confirm the sample counter climbs.

**Procedure, HLA side.**

1. Launch `rtinode.exe`. Confirm blank listening console.
2. In CLEARANCE console: `clearance.hla.join`. Confirm Event Log shows `HLA: joined 'CLEARANCE' as 'CLEARANCE-Instructor'`.
3. Double-click `clearance_hla_subscriber.exe`.
4. Confirm the subscriber console shows `[HLA-SUB] #N <callsign> -> ATCManagedAircraft ...` lines scrolling with `#N` climbing.

**Pass criteria for both sides.**

- Subscriber counter increments continuously.
- Values in the subscriber output match aircraft state in the CLEARANCE scope (callsign, squawk, flight phase).

**Covers.** REQ-FED interop claims for both DDS runtimes and HLA via independent second-process federate ingestion.

### MP-04: two-federate live federation

**Purpose.** Confirms two CLEARANCE processes can federate over DIS, Fast DDS, and RTI Connext simultaneously with distinct Site IDs, and that ownership and ATC-state propagation work as documented.

**Prerequisites.**

- Two separate CLEARANCE Standalone processes launched (not two PIE client windows; see `DIS_INTEROPERABILITY.md` §4b).

**Procedure.** Follow the full §4b procedure verbatim. Load different scenarios on each federate.

**Pass criteria.**

- Local aircraft on each show the `OWN` chip.
- Peer aircraft appear on each scope with the correct `SITE N` chip.
- Squawk and flight-phase changes made on Instance A appear on Instance B's scope within 2 seconds.
- Attempting to `RECLASSIFY` a peer-owned aircraft on the wrong federate is rejected (ownership protection).

**Covers.** The federation ownership and state propagation portfolio claims that don't have automation tests.

### MP-05: automation test suite full pass

**Purpose.** Sanity-check that every Tier 1 test still passes after significant edits (module refactor, new subsystem, UE version upgrade).

**Prerequisites.** CLEARANCE editor with the ClearanceSim plugin enabled.

**Procedure.**

1. Editor > Tools > Session Frontend > Automation tab.
2. Filter to `Clearance.`.
3. Confirm the test count matches expected (52 as of this doc; edit this number when tests are added).
4. Tick the top-level `Clearance` checkbox and start tests.
5. Wait for all tests to complete. Confirm all green.

**Pass criteria.** All shown tests green, none skipped or timed out.

**Covers.** Regression coverage across all seven REQ domains.

## 6. When to run what

| Trigger | MP-01 | MP-02 | MP-03 | MP-04 | MP-05 | Tier 1 |
|---|:---:|:---:|:---:|:---:|:---:|:---:|
| Every source-file commit |  |  |  |  |  | ✅ |
| Touching a DIS PDU serialiser | ✅ |  |  |  |  | ✅ |
| Touching a DDS or RTI runtime adapter |  | ✅ | ✅ |  |  | ✅ |
| Touching the HLA federate implementation |  |  | ✅ |  |  | ✅ |
| Touching federation ownership or ATC state code |  |  |  | ✅ |  | ✅ |
| UE version upgrade | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Before recording or re-recording the demo video | ✅ | ✅ | ✅ | ✅ | ✅ | ✅ |
| Before applying to a job with this on the CV |  |  |  |  | ✅ | ✅ |

## 7. Change control

This V&V plan lives with the code. Changes to the strategy are committed alongside the code changes that motivate them, not as standalone process updates.

- **New REQ-ID.** Update `Requirements.md`. Add the test. That's enough.
- **New manual procedure.** Add a new `MP-NN` section here. Update the "when to run what" matrix.
- **Removing a REQ-ID.** Mark it `[DEPRECATED]` in `Requirements.md`, don't reuse the number. Update the tests that referenced it.
- **Changing a manual procedure.** Increment the procedure's version in the section header when the change is material enough that old evidence captures no longer prove the current implementation.

## 8. What this document doesn't do

Deliberately doesn't cover:

- **Formal safety analysis (FMEA, FTA, HAZOP).** Not portfolio-scale.
- **Software integrity level assignment.** Not applicable.
- **Independent verification by a separate team.** Solo project. The closest thing is third-party tooling (Wireshark, RTI Admin Console) doing the independent dissection.
- **Configuration management beyond Git.** Git commit hashes are the configuration identifier for the code and docs; no separate CM tool.

If CLEARANCE were shipping into an actual defence programme, every bullet above would need to be addressed. The point of documenting what's not done is to demonstrate that the omissions are conscious scope decisions, not oversights.
