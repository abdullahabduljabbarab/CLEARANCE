# CLEARANCE Verification and Validation Report

**Project:** CLEARANCE
**Author:** Abdullah Ameed Abduljabbar
**Role:** Systems Designer
**Status:** DRAFT pending video release. MP-01 to MP-05 evidence exists in the federation companion repository and its release video. MP-06 evidence exists in the unreleased C++ technical breakdown video. Promoted to Final once the C++ technical breakdown video is published.
**Build / Commit:** to be stamped at release with the actual Git commit hash
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
| Windows version | Windows 11 Home |
| GPU / CPU | NVIDIA GeForce RTX 4070 Ti SUPER / Intel Core i7-14700KF |
| RTI Connext version | 7.7.0 |
| Fast DDS version | 3.6 |
| OpenRTI version | 0.10.0 |
| Wireshark version | 4.x with the default IEEE 1278.1 DIS dissector |

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

Automation result captured from the Unreal Session Frontend using filter `Clearance.` on 2026-07-17.

## Manual procedure results

| Procedure | Purpose | Result | Evidence |
|---|---|---|---|
| MP-01 | Wireshark DIS wire compliance | Pass | Federation companion repository + release video |
| MP-02 | RTI Admin Console publisher discovery | Pass | Federation companion repository + release video |
| MP-03 | Standalone subscribers round-trip (Fast DDS and HLA) | Pass | Federation companion repository + release video |
| MP-04 | Two-federate live federation | Pass | Federation companion repository + release video |
| MP-05 | Full automation suite pass | Pass | Federation companion repository + release video |
| MP-06 | Runtime ATC safety path verification | Pass (evidence in unreleased C++ technical breakdown video) | C++ technical breakdown video, pending publication |

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
| None | No known open verification-blocking issues at time of report | N/A | Closed |

## Verification conclusion

All 69 documented requirements have automated coverage, and the 52-test automation suite passes. MP-01 through MP-05 have their evidence in the federation companion repository and its release video. MP-06 has been captured in the C++ technical breakdown video, which is not yet published. This report will be promoted from **DRAFT** to a final release report once that video is published.

The project remains a portfolio demonstrator and training-simulation prototype, not certified operational ATC, avionics, or defence training software.
