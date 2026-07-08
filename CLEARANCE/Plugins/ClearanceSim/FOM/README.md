# CLEARANCE RPR-FOM Extension

`ClearanceRPR-FOM.xml` is an IEEE 1516-2010 **FOM Module** that extends
the SISO Real-time Platform Reference Federation Object Model (RPR-FOM
2.0) with CLEARANCE-specific ATC-domain classes and interactions.

## What's in this file

### Object class extension

```
HLAobjectRoot
└── BaseEntity                            (from RPR-FOM 2.0 base module)
    └── PhysicalEntity                    (from RPR-FOM 2.0)
        └── Platform                       (from RPR-FOM 2.0)
            └── Aircraft                   (from RPR-FOM 2.0)
                └── ATCManagedAircraft    ← CLEARANCE adds this subclass
```

`ATCManagedAircraft` inherits all the standard Aircraft attributes
(`WorldLocation`, `VelocityVector`, `Orientation`, `EntityType`,
`EntityIdentifier`, `ForceIdentifier`, `Marking`, ...) and adds:

| Attribute | Type | Transport | Semantics |
|-----------|------|-----------|-----------|
| `SquawkCode` | `HLAinteger16BE` | Best-effort | Mode A/3 SSR code (1200, 7500, 7600, 7700...) |
| `FlightPhase` | `FlightPhaseEnum` | Best-effort | Enroute / Approach / Landing / GoAround / Departing / Exiting |
| `ActiveClearance` | `HLAunicodeString` | Reliable | Human-readable text of the most recent ATC clearance |
| `ATCFacility` | `HLAunicodeString` | Reliable | ICAO identifier of the facility currently controlling the aircraft |

**Why extend rather than pollute the base**: a stock RPR-FOM
sensor-fusion federate (e.g. a fighter simulator with a radar warning
receiver) can ignore `ATCManagedAircraft` entirely and treat instances
as vanilla `Aircraft`. All the shared attributes are still there. An
ATC-aware federate (a peer tower simulator, an AWACS controller
station, a training after-action review tool) subscribes to the
`ATCManagedAircraft` leaf and gets the full ATC picture. The base
FOM stays clean.

### Interaction hierarchy

```
HLAinteractionRoot
└── ATCEvent                              ← root for ATC-domain events
    ├── Handoff                           ← facility A releases → facility B accepts
    └── ClearanceIssued                   ← a clearance was spoken by ATC
```

`Handoff` parameters: `AircraftInstance`, `FromFacility`, `ToFacility`,
`HandoffTimeSeconds`.

`ClearanceIssued` parameters: `AircraftInstance`, `ClearanceKind`,
`ClearanceText`, `IssuingFacility`.

Both use reliable transport - losing a handoff creates an ambiguous
control state, and losing a clearance creates dangerous SA drift on
peer stations.

### Custom datatypes

- **`FlightPhaseEnum`** (HLAoctet) — mirrors CLEARANCE's
  `EFlightPhase` C++ enum
- **`AtcClearanceKindEnum`** (HLAoctet) — mirrors CLEARANCE's
  `EInstructionType` C++ enum

## How this maps to CLEARANCE's existing PODs

Field-for-field mapping from CLEARANCE's DIS/DDS `AircraftState` POD
to the RPR-FOM 2.0 base attributes:

| CLEARANCE field | RPR-FOM 2.0 `Aircraft` attribute |
|---|---|
| `EntityNumber` | `EntityIdentifier.EntityNumber` |
| `Marking` | `Marking` (charset + 11 chars) |
| `ForceId` | `ForceIdentifier` |
| `EntityKind`/`Domain`/`Country`/`Category`/... | `EntityType` (fixed record) |
| `XMeters` / `YMeters` / `ZMeters` | `WorldLocation` (fixed record of 3 doubles) |
| `VxMps` / `VyMps` / `VzMps` | `VelocityVector` (fixed record of 3 floats) |
| `PsiRad` / `ThetaRad` / `PhiRad` | `Orientation` (fixed record of 3 floats) |

Everything above is stock RPR-FOM — no CLEARANCE-specific mapping
required. Same DIS-native units (ECEF metres, m/s, radians) so a
DIS↔HLA bridge is a struct-to-struct copy.

The four CLEARANCE-only fields — `SquawkCode`, `FlightPhase`,
`ActiveClearance`, `ATCFacility` — go onto the `ATCManagedAircraft`
subclass this FOM defines.

## Loading this FOM into a real RTI

For OpenRTI, Portico, RTI Connext HLA, or Pitch pRTI — same pattern:

```cpp
try {
    RTIambassador->createFederationExecution(
        L"CLEARANCE_Exercise",
        std::vector<std::wstring>{
            L"RPR-FOM-2.0-Base.xml",         // SISO reference - download from sisostds.org
            L"ClearanceRPR-FOM.xml"          // this file
        });
} catch (FederationExecutionAlreadyExists&) {
    // benign - another federate created it already
}

RTIambassador->joinFederationExecution(
    L"CLEARANCE_Instance_1",                 // federate name
    L"CLEARANCE_Exercise",
    std::vector<std::wstring>{
        L"ClearanceRPR-FOM.xml"              // additional modules this federate contributes
    });
```

The RTI merges the two XML files into a single federation object
model. Federates from different vendors that load compatible modules
can all publish/subscribe interoperably.

## Publish/subscribe declarations

CLEARANCE, when the runtime lands, would declare:

```cpp
// Publish - CLEARANCE owns its own aircraft.
rti->publishObjectClassAttributes(
    ATCManagedAircraftClass, {
        WorldLocationHandle, VelocityVectorHandle, OrientationHandle,
        EntityTypeHandle, EntityIdentifierHandle, ForceIdentifierHandle,
        MarkingHandle,
        SquawkCodeHandle, FlightPhaseHandle,
        ActiveClearanceHandle, ATCFacilityHandle
    });
rti->publishInteractionClass(HandoffHandle);
rti->publishInteractionClass(ClearanceIssuedHandle);

// Subscribe - receive OTHER federates' aircraft.
rti->subscribeObjectClassAttributes(
    ATCManagedAircraftClass, /* same attributes as published */);
rti->subscribeInteractionClass(HandoffHandle);
rti->subscribeInteractionClass(ClearanceIssuedHandle);
```

Reflected updates arrive on the FederateAmbassador's
`reflectAttributeValues()` callback; interactions arrive on
`receiveInteraction()`. Standard IEEE 1516-2010 API.

## Status

**Ships today as a spec artifact.** No runtime integration. See
[`Docs/HLA_INTEGRATION.md`](../../../Docs/HLA_INTEGRATION.md) for the
architecture doc that describes the ambassador class shape and the
deferral rationale.

**Portfolio claim earned:** *"Authored RPR-FOM 2.0 FOM module
extending the SISO base with an ATC-domain subclass hierarchy
(ATCManagedAircraft, Handoff / ClearanceIssued interactions,
FlightPhaseEnum and AtcClearanceKindEnum data types). HLA runtime
integration deferred pending RTI vendor selection - see
HLA_INTEGRATION.md."*

## References

- **SISO RPR-FOM 2.0** — https://www.sisostds.org/DigitalLibrary.aspx (SISO-STD-001.1-2015)
- **IEEE 1516-2010** — HLA-Evolved standard
- **HLA 1516-2010 DIF (Data Interchange Format)** — the XML schema this FOM validates against
- **CLEARANCE HLA architecture** — [`Docs/HLA_INTEGRATION.md`](../../../Docs/HLA_INTEGRATION.md)
- **CLEARANCE DIS wire** — [`Docs/DIS_INTEROPERABILITY.md`](../../../Docs/DIS_INTEROPERABILITY.md)
- **CLEARANCE protocol layer isolation pattern** — [`Docs/PROTOCOL_LAYER.md`](../../../Docs/PROTOCOL_LAYER.md)
