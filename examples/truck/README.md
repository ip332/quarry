# Class 8 Truck: One Configuration, Many Bindings

This example shows how the components of one system share record IDs. The engine
ECU uses embedded C bindings for its own namespace. A cloud service uses Python
bindings for every namespace. Both are generated, separately, from the same
**configuration root**, so a report the ECU encodes is decoded by the cloud.

## The configuration

`schemas/configuration.brd` names the configuration and imports the truck's
schemas. It declares nothing itself:

```yaml
configuration: class8_truck
imports:
  - vehicle.brd
  - engine.brd
  - transmission.brd
  - brakes.brd
  - cms.brd
```

The compiler always compiles the root's complete import closure. Record IDs are
`(domain << 16) | local`: domains number the namespaces in sorted order, and
local IDs number each namespace's records by name.

| Record | Defined in | `record_id` |
|---|---|---|
| `truck.VehicleSnapshot` | `vehicle.brd` | `0x00010001` |
| `truck.brakes.AbsEvent` | `abs_event.brd` | `0x00020001` |
| `truck.brakes.BrakeStatus` | `brakes.brd` | `0x00020002` |
| `truck.cms.CameraHealth` | `cms.brd` | `0x00030001` |
| `truck.common.Timestamp` | `common_time.brd` | `0x00040001` |
| `truck.engine.EngineStatus` | `engine.brd` | `0x00050001` |
| `truck.engine.FaultCode` | `engine_faults.brd` | `0x00050002` |
| `truck.transmission.GearState` | `transmission.brd` | `0x00060001` |

Two files contribute to `truck.engine` and two to `truck.brakes`. Each namespace
is generated as one unit containing all of its records.

## Generating the bindings

The ECU needs only its own namespace and the shared timestamp:

```sh
quarry-schema-compiler --language c --generate truck.engine,truck.common \
  --output-directory ecu-generated schemas/configuration.brd
```

Without `--generate`, a configuration root emits every namespace. The cloud
service uses that:

```sh
quarry-schema-compiler --language python --output-directory cloud-generated \
  schemas/configuration.brd
```

Whatever is selected, the IDs, the QBS image (`--emit-qbs`) and its `schema_id`
are the configuration's. `--list-outputs` with the same options prints exactly
the files a run writes. Separate runs selecting different namespaces from the
same root write the same files as one full run.

## Building and running

After installing Quarry to a prefix and installing the Python runtime
(`pip install runtime/python`):

```sh
cmake -S ecu -B build/ecu -DCMAKE_PREFIX_PATH=/path/to/install/prefix
cmake --build build/ecu
./build/ecu/quarry_truck_engine_ecu engine_status.brf

quarry-schema-compiler --language python --output-directory build/cloud \
  schemas/configuration.brd
PYTHONPATH=build/cloud python3 cloud/decode.py engine_status.brf
```

Expected output:

```text
EngineStatus record_id 0x00050001, 102 bytes
record_id 0x00050001: rpm=1450 coolant=88C faults=[(110, 16)]
```

## Why the root matters

Compiling `engine.brd` as its own root is valid but defines a different
configuration. It contains only `truck.common` (domain 1) and `truck.engine`
(domain 2), so `EngineStatus` becomes `0x00020001`. Cloud bindings generated
that way reject the ECU's report:

```text
DecodeError: unexpected record id: 327681 (expected 131073)
```

Generate every component's bindings from the same configuration root.
Generated dependencies check configuration identity and generated-code identity
at compile time. Preserve consistent effective per-namespace storage policies
across separate invocations, including unselected dependencies. See the
[consumer compatibility and migration guide](../../docs/generated-binding-compatibility.md).
