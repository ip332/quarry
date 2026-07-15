# Runtime

This module owns generic runtime support for Breadcrumbs binary records.

Generated C++ schema code calls the header-only `breadcrumbs_runtime` target for
byte-level mechanics while generated code keeps schema-specific knowledge such
as `record_id`, `field_index`, field type, and enum value sets.

Current support:

* top-level Binary Record Format v0.1 header emission
* top-level Binary Record Format v0.1 header parsing
* Field Directory emission sorted by `fieldIndex`
* structural Field Directory parsing and field lookup
* unsigned LEB128 `varuint` emission for directory offsets and lengths
* unsigned LEB128 `varuint` parsing for directory offsets and lengths
* big-endian scalar byte emission
* big-endian scalar byte parsing
* owning `std::vector<std::byte>` encode results
* compact runtime parse/read errors used by generated decoders

Generated decoders currently materialize generated record values from
caller-owned byte spans. Unknown field indexes are ignored after structural
validation. Present known fields whose types are not yet supported by the
generated scalar decoder cause generated decoding to fail.

Out of scope:

* dynamic Schema IR or manifest interpretation
* strings, bytes, arrays, and nested-record payload encoding or decoding
* unknown-field preservation
* generated read/view APIs
* zero-copy or caller-provided output buffers
* install/export packaging

Runtime code must not depend on compiler libraries, YAML, Schema IR protobufs,
source-schema models, symbols, semantic validation, layout, or backend code.
