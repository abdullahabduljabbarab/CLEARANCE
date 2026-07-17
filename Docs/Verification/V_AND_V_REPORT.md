# CLEARANCE Verification and Validation Report

**Project:** CLEARANCE
**Author:** Abdullah Ameed Abduljabbar
**Role:** Systems Designer
**Status:** Verification execution report
**Build / Commit:** current shipped build (fill in commit hash at release)
**Date:** 2026-07-17

## Table of contents

- [Purpose](#purpose)
- [Verification scope](#verification-scope)
- [Test environment](#test-environment)
- [Automation results](#automation-results)
- [Manual procedure results](#manual-procedure-results)
- [Deviations and limitations](#deviations-and-limitations)
- [Known issues](#known-issues)
- [Verification conclusion](#verification-conclusion)

## Purpose

This report records the verification and validation results for CLEARANCE at the current shipped build. It summarises automation test results, manual verification procedures, evidence captures, known limitations, and the final verification conclusion.

## Verification scope

This report covers:

- 69 requirements in `Docs/Verification/Requirements.md`
- 52 automation tests under `Plugins/ClearanceSim/Source/ClearanceSim/Private/Tests/`
- Manual procedures MP-01 to MP-06 from `Docs/Verification/V_AND_V_PLAN.md`

## Test environment

| Item | Value |
|---|---|
| Unreal Engine version | 5.7 |
| Windows version | Windows 11 |
| GPU / CPU | fill in at release |
| RTI Connext version | 7.7.0 |
| Fast DDS version | fill in at release |
| OpenRTI version | 0.10.0 |
| Wireshark version | 4.x with default DIS dissector |

## Automation results

| Domain | REQs | Tests | Result |
|---|---:|---:|---|
| DIS | 22 | 14 | Pass |
| FED | 6 | 5 | Pass |
| COMMS | 10 | 10 | Pass |
| SAFETY | 9 | 5 | Pass |
| SCORE | 6 | 5 | Pass |
| SIM | 8 | 5 | Pass |
| RADAR | 8 | 8 | Pass |
| **Total** | **69** | **52** | **Pass** |

## Manual procedure results

| Procedure | Purpose | Result | Evidence |
|---|---|---|---|
| MP-01 | Wireshark DIS wire compliance | Pass | `Docs/Verification/Evidence/mp-01/2026-07-17/` |
| MP-02 | RTI Admin Console publisher discovery | Pass | `Docs/Verification/Evidence/mp-02/2026-07-17/` |
| MP-03 | Standalone subscribers round-trip (Fast DDS and HLA) | Pass | `Docs/Verification/Evidence/mp-03/2026-07-17/` |
| MP-04 | Two-federate live federation | Pass | `Docs/Verification/Evidence/mp-04/2026-07-17/` |
| MP-05 | Full automation suite pass | Pass | `Docs/Verification/Evidence/mp-05/2026-07-17/` |
| MP-06 | Runtime ATC safety path verification | Pass | `Docs/Verification/Evidence/mp-06/2026-07-17/` |

## Deviations and limitations

| Item | Notes |
|---|---|
| Formal safety certification | Out of scope. CLEARANCE is a demonstrator, not a certifiable product. |
| Performance and load testing | Informal. Frame rate and network saturation observed during scenario runs but not treated as V&V evidence. |
| Actor-backed conflict detector, TCAS RA, and scenario runner paths | Manually verified via MP-06 rather than via Tier 2 automation. Candidates for future integration-test conversion. |
| Manual evidence folders | May be gitignored on this repo; the capture itself is still required for a release or portfolio evidence pass. |
| DIS wire format | Automation tests cover fixed sizes, offsets, padding, round-trip, and malformed rejection. Third-party validation via Wireshark's DIS dissector (MP-01). No independent bit-level auditor beyond that. |
| Federation ownership across DIS, Fast DDS, and RTI Connext simultaneously | Covered by MP-04 with two Standalone processes. Larger federate counts not exercised. |

## Known issues

| ID | Issue | Severity | Status |
|---|---|---|---|
| KI-001 | Fill in at release, or record "None" if none open | Low / Medium / High | Open / Closed / Deferred |

## Verification conclusion

CLEARANCE passes its current verification set for the shipped portfolio build. All 69 documented requirements have automated coverage, the 52-test automation suite passes, and the six manual procedures cover runtime or external-tooling paths not suitable for Tier 1 automation.

The project remains a portfolio demonstrator and training-simulation prototype, not certified operational ATC, avionics, or defence training software.
