# Protobuf to BRD translator

`quarry-protobuf-translator` is an isolated migration and evaluation tool.
It consumes protobuf `FileDescriptorSet` files and emits deterministic Quarry
`.brd` source bundles for a deliberately bounded subset. It does not link to
the Quarry compiler, construct Schema IR, decode protobuf wire bytes, or
invoke `protoc` or the Quarry compiler.

## Input

Create a descriptor set externally with imported descriptors included:

```sh
protoc \
  --descriptor_set_out=schemas.pb \
  --include_imports \
  --include_source_info \
  --proto_path=path/to/protos \
  path/to/protos/root.proto
```

List descriptor contents without translation:

```sh
quarry-protobuf-translator --descriptor-set schemas.pb --list
```

## Bounded translation

Every reachable string and bytes field requires `max_bytes`; every repeated
field requires `max_elements`. Repeated strings and bytes require both:

```yaml
bounds:
  telemetry.Sample.name:
    max_bytes: 64
  telemetry.Sample.payload:
    max_bytes: 256
  telemetry.Sample.values:
    max_elements: 32
  telemetry.Sample.children:
    max_elements: 8
  telemetry.Child.label:
    max_bytes: 32
```

Translate one selected protobuf message:

```sh
quarry-protobuf-translator \
  --descriptor-set schemas.pb \
  --root telemetry.Sample \
  --bounds bounds.yaml \
  --output-dir generated
```

The translator emits one `.brd` source unit per reachable message plus
`manifest.json`. The protobuf declaration `telemetry.Sensor.Reading` maps to
the Quarry namespace `telemetry.sensor.reading`, record `Reading`, and output
`telemetry/sensor/reading/reading.brd`. The complete declaration path is used
to prevent simple-name collisions. The manifest records the mapping,
dependency-first order, generated explicit roots, and the bounds entries.

Generated dependencies are ordinary relative `.brd` imports. Invoke the
Quarry compiler separately for every path in `manifest.json`'s
`explicit_roots`, placing all generated backend outputs in the desired common
output directory. The translator does not perform those compiler invocations
automatically.

The scalar mapping is logical, not protobuf-wire-compatible: `bool` maps to
`bool`; signed 32-bit kinds map to `i32`; unsigned 32-bit kinds to `u32`;
signed 64-bit kinds to `i64`; unsigned 64-bit kinds to `u64`; `float` to
`f32`; `double` to `f64`; and bounded `string`/`bytes` retain those Quarry
types. Repeated fields use the corresponding bounded Quarry array type.

The current slice rejects reachable enums, oneof and proto3 optional
presence, maps, groups, services, extensions, public/weak import semantics,
`google.protobuf.Any`, proto2 required/default semantics, recursive messages,
and unsupported scalar kinds. Unreachable declarations are not translated.
Enum ownership and protobuf field-number sidecars remain deferred. Generated
BRD is Quarry source, not protobuf wire data; no protobuf/BRF interoperability
claim is made by this tool.
