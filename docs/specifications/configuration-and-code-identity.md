# Configuration and Generated-Code Identity

## Purpose

Define Stage A identity, generation and compatibility contracts independently of
release numbering. Status: implemented for the v0.1.31-rc.1 candidate; formats and
epochs below describe the merged Stage A implementation.

## Scope

Configuration roots, QBS identity, generated dependency checks, C/C++ generated
identity format 1 and runtime epochs. Descriptor discovery, partial images,
translation and compatibility classification remain future Stage B work.

## Terminology

- Configuration: one root and its complete transitive import closure.
- `record_id`: the configuration-local hierarchical BRF type identifier.
- `schema_id`: algorithm plus 128-bit canonical wire configuration identity.
- `gid`: per-namespace, per-native-variant generated-definition identity.
- Epoch: generated-code/runtime API contract, independent of either identity.
- `lp(x)`: unsigned big-endian u32 byte length followed by exactly those bytes.

## Requirements

### Configuration and selection

REQ-ID-001: The compiler SHALL resolve the complete root import closure before
assigning IDs or computing configuration identity. A configuration document
SHALL contain only `configuration` and `imports`, declare no domain, and SHALL
NOT be imported. A record root remains valid but defines its own configuration.

REQ-ID-002: A configuration root without `--generate` SHALL emit every planned
namespace; a record root SHALL emit its own namespace by default. Explicit
selection SHALL affect emission only. Every selected namespace SHALL include all
its declarations, including contributions from multiple files. Unknown selections
SHALL fail before writing output. Separate invocations from one root with the
same effective options SHALL agree on shared outputs.

REQ-ID-003: Configuration identity SHALL use QBS algorithm 1, SHA-256 truncated
to the first 16 bytes of the existing canonical QBS identity input. The input
SHALL include all record IDs, record/used-enum FQNs and wire definitions, including
nested references. Field names, enum member names, unused enums, comments, paths,
configuration labels, compiler/release metadata, selection, naming prefixes and
C host-storage policy SHALL NOT contribute. The exact canonical wire input is
specified by [QBS](quarry-binary-schema.md#schema_id).

REQ-ID-004: Every generated backend SHALL embed the full configuration identity,
not a selected subset identity. Each generated dependency edge SHALL check the
expected algorithm and full value: C/C++ at compile time, Python at import time,
Go through compile-time constant checks. Missing older metadata SHALL fail
visibly. Independent generated sets without a dependency edge SHALL NOT imply a
whole-program compatibility check.

### Generated-definition identity format 1

REQ-ID-005: C (variant 1), C++ owning (2), and C++/C-storage (3) SHALL compute a
separate `gid` as SHA-256 truncated to its first 16 bytes over this encoding:

```text
ASCII "QUARRY-GENERATED-CODE-ID" followed by 0x00
u8 format=1; u8 variant; u8 schema_id_algorithm; 16 schema_id bytes
lp(canonical namespace FQN)
u32 file_count
  for each file in role order: u8 role (1 header, 2 source); lp(normalized content)
u32 direct_dependency_count
  for each direct dependency in bytewise FQN order:
    lp(canonical FQN); u8 variant; u8 gid_format; 16 gid bytes
```

All multibyte integers are big-endian. File roles and dependency FQNs are unique;
self-dependencies and malformed placeholders are rejected. This is the existing
format, not a new identity algorithm.

REQ-ID-006: Normalized content SHALL be emitted by the renderer using typed
placeholders, without post-render text replacement to discover tokens:

| Bytes between `0x1e` and `0x1f` | Meaning |
|---|---|
| `S` plus `h`, `H`, `b`, or `0`–`3` | Own gid: lowercase/uppercase hex, byte list, or one word literal |
| `P` plus canonical namespace FQN | Generated include/output path for that namespace |
| `M` plus canonical namespace FQN | Encoded identity macro stem |

REQ-ID-007: Identity computation SHALL cover all namespaces in dependency order,
including unselected dependencies and their effective storage policies. Final
rendering SHALL agree with normalized content before placeholder substitution.
Changing emitted definitions, dependency identities or variant SHALL re-identify
the affected namespace and dependents. Compiler version strings and output path,
extension or root-stem options alone SHALL NOT re-identify normalized definitions.
`--name-prefix` MAY change gid because it changes emitted public definitions.

REQ-ID-008: Native generated dependency edges SHALL check gid metadata presence,
format and all four 32-bit words after configuration checks. Guards SHALL include
the gid; conflicting definitions of the same namespace/variant in one translation
unit SHALL fail explicitly. Identical copies MAY deduplicate. C++ definitions
SHALL use `quarry_gen_<32 lowercase hex digits>` inline linkage namespaces,
including plain-C headers consumed as C++20 and the C-shaped storage layer.

REQ-ID-009: Generated metadata SHALL expose these existing names:

| Variant | Metadata |
|---|---|
| C++ owning | `kQuarrySchemaIdAlgorithm`, `kQuarrySchemaId`, `kQuarryGeneratedCodeIdFormat`, `kQuarryGeneratedCodeId`; `QUARRY_GENERATED_CPP_NS_<body>_SCHEMA_ID_*` / `CODE_ID_*` |
| C | `QUARRY_NS_<body>_SCHEMA_ID_{ALGORITHM,W0,W1,W2,W3}`, `quarry_ns_<body>_schema_id()`; `QUARRY_GENERATED_C_NS_<body>_CODE_ID_{FORMAT,W0,W1,W2,W3}` |
| C++/C storage | `QUARRY_NS_<body>_SCHEMA_ID_*`, `quarry_ns_<body>_schema_id()` and `QUARRY_GENERATED_CPP_C_NS_<body>_CODE_ID_*` |
| Python | `QUARRY_SCHEMA_ID_ALGORITHM`, `QUARRY_SCHEMA_ID` |
| Go | `QuarrySchemaIDAlgorithm`, `QuarrySchemaID()`, `QuarrySchemaIDHi`, `QuarrySchemaIDLo` |

The detailed guard/constant spellings and reserved-name diagnostics are recorded
in [the generated-code contract](../design/configuration-identity.md#16-generated-code-identity-in-c-and-c-bindings-111-g1g5).
Python and Go do not expose the native per-namespace gid mechanism.

REQ-ID-010: Namespace identity stems SHALL preserve case, escape `q` as `qq` and
`_` as `q0` within segments, join segments with `_`, and use `0` for the unnamed
namespace. With an explicit prefix it SHALL be the leading encoded segment.
Generated identity names SHALL be checked against schema-derived identifiers;
Python SHALL apply its established reserved-module-name renaming policy and
reject resulting duplicates. Unprefixed C public symbol spellings remain legacy
and are not made injective by this identity encoding (issue #119).

### Accessors and compatibility

REQ-ID-011: Accessors SHALL return the stored QBS identity without new hashing,
initialization or mandatory allocation. C `quarry_qbs_schema_id()` SHALL require an
already parsed view with unchanged live backing bytes, clear non-null output on
failure, return `QUARRY_GENERIC_INVALID_ARGUMENT` for null arguments/backing or
fewer than 40 bytes, and `QUARRY_GENERIC_MALFORMED_QBS` for unsupported algorithm
or length metadata. These checks SHALL NOT be treated as validation of a fabricated
view or cryptographic verification. C++ `ValidatedQbsView::schema_id()` SHALL read
validated/cached metadata (a default view returns zeros). Python `QbsSchema.schema_id`
and `.schema_id_algorithm` SHALL be read-only; headerless hand-built metadata
SHALL raise `QbsError`. Python may return a small bytes value. C++ QBS views remain
compiler-owned tooling, not an installed standalone C++ QBS SDK.

REQ-ID-012: Current epochs SHALL remain C++ owning 3, C/C-storage 7, Python 1,
Go 1. BRF 2, QBS 1, Schema IR 1, schema identity algorithm 1 and gid format 1
SHALL remain separate version contracts. Equal epochs/identities SHALL NOT be
interpreted as native ABI compatibility or authentication. A4.2 and gid changes
preserve epochs because they do not change the generated-code/runtime call
contract; consumers still regenerate and rebuild for changed generated linkage,
guards, types or layouts. Existing global C++ forward declarations are insufficient
for identity-scoped types; include the generated header.

## Examples

The authoritative truck root has schema_id
`9200ca494f010801b830d0857cf66dde`; EngineStatus is `0x00050001`.
Revision B adds MirrorFold `0x00030002`, keeping EngineStatus's numeric ID but
changing configuration identity to `806e9a4e11d0df71c4e5b51045f85010`.
`--name-prefix rev_a` yields `rev_a::truck::engine` in owning C++ and
`revq0a_truck_engine_EngineStatus_t` in C. This does not change wire IDs or QBS.

## Validation

Pinned encoding vectors live in `tests/support/generated_code_identity_test.cpp`.
The [readiness matrix](../release-readiness-v0.1.31-rc.1.md) maps allocation,
identity, ODR, packaging and all 25 language exchanges to maintained tests.
Identical inputs/options are reproducible; import reordering preserves wire IDs,
schema_id and QBS, but declaration-order source-byte invariance remains #90.

## Related Documents

- [BRF](binary-record-format.md)
- [QBS](quarry-binary-schema.md)
- [Compiler](schema-compiler.md)
- [Consumer compatibility and migration](../generated-binding-compatibility.md)
- [Version and release-tag metadata](../versioning.md)
- [CLI contracts](../../tools/README.md)
