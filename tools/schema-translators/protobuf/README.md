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
  --options bounds.yaml \
  --output-dir generated
```

`--options` is the preferred spelling for the Quarry-native external bounds
file. `--bounds` remains a compatibility alias for existing scripts; the two
options cannot be supplied together. The translator does not support
`--options-format nanopb` yet.

## Bounds options and precedence

First-party protobuf schemas may import the option definition shipped in this
directory:

```proto
import "quarry_options.proto";

option (quarry.protobuf.file_default_bounds).max_bytes = 256;

message Sample {
  option (quarry.protobuf.message_default_bounds).max_elements = 32;
  string name = 1 [(quarry.protobuf.field_bounds).max_bytes = 64];
}
```

`Bounds` is one grouped option with optional `max_bytes` and `max_elements`
members. It is available on files, messages, and fields. The descriptor set
must include `quarry_options.proto` (use `--include_imports`); the translator
resolves the extensions through a protobuf `DescriptorPool` and does not
decode unknown option numbers itself.

For each reachable field, values are resolved deterministically in this order:

1. external YAML (`--options`, or the legacy `--bounds`) fills values absent
   from the schema;
2. file defaults override those external fallback values;
3. message defaults override file defaults;
4. field options override message defaults.

There are no implicit limits. Strings and bytes require `max_bytes`, repeated
fields require `max_elements`, and repeated strings/bytes require both. The
manifest records each final value, its source type, source file, and the
complete precedence chain. Host-specific absolute paths are omitted. Missing,
zero, conflicting, or unused bounds are diagnosed before any output is
published.

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

The current slice supports reachable non-negative protobuf enums. A top-level
enum is owned by the lexicographically first reachable message that references
it; a nested enum is owned by its enclosing translated message. Each enum is
emitted exactly once, and other generated records import that owner unit when
needed. Repeated enum fields use the same explicit `max_elements` bound as
other repeated fields.

The translator rejects enum aliases, negative enum values, and enum names or
values that cannot be represented as Quarry identifiers. It also rejects
oneof and proto3 optional
presence, maps, groups, services, extensions, public/weak import semantics,
`google.protobuf.Any`, proto2 required/default semantics, recursive messages,
and unsupported scalar kinds. Unreachable declarations are not translated.

`manifest.json` contains deterministic declaration and field migration
metadata, including protobuf field numbers, translated ordinals, enum values,
owners, and applied bounds. Protobuf field numbers are migration metadata only;
they are not BRF field indexes, and translated BRD is Quarry source rather than
protobuf wire data. No protobuf/BRF interoperability claim is made by this
tool. Manifest comparison is intentionally deferred until a compatibility
policy can be defined independently of protobuf wire compatibility.

Nanopb options are not parsed or inferred. Nanopb-compatible matching and
precedence are deferred to a separate translator change; use the Quarry-native
YAML file or the first-party protobuf options above for this release.
