# Runtime

This module owns generic runtime support for Breadcrumbs binary records.

The first runtime boundary is encoding-only. Generated C++ schema code calls
the header-only `breadcrumbs_runtime` target for byte-level mechanics while the
generated code keeps schema-specific knowledge such as `record_id`,
`field_index`, field type, and enum value sets.

Current support:

* top-level Binary Record Format v0.1 header emission
* Field Directory emission sorted by `fieldIndex`
* unsigned LEB128 `varuint` emission for directory offsets and lengths
* big-endian scalar byte emission
* owning `std::vector<std::byte>` encode results
* failure reported as `std::nullopt`

Out of scope:

* decoding
* dynamic Schema IR or manifest interpretation
* strings, bytes, arrays, and nested-record payload encoding
* zero-copy or caller-provided output buffers
* install/export packaging

Runtime code must not depend on compiler libraries, YAML, Schema IR protobufs,
source-schema models, symbols, semantic validation, layout, or backend code.
