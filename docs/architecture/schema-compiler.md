# Quarry Schema Compiler

## Status

Draft

## Purpose

The schema compiler turns Quarry schemas into generated artifacts consumed
by production devices, services, tools, and SDKs.

The compiler exists to move schema knowledge to build time. Runtime systems
should use generated artifacts rather than interpreting schemas during normal
operation.

The normative `.brd` source-language grammar and schema-language semantics are
defined by `docs/specifications/schema-language.md`.

---

# Inputs

The schema compiler consumes:

* Quarry schema language files
* imported schema definitions
* compatibility metadata
* reserved field metadata
* target language configuration
* target profile configuration

Target profiles may describe constraints such as embedded memory limits,
supported field types, maximum nesting depth, and enabled generated outputs.

---

# Outputs

The schema compiler may produce:

* generated accessors
* generated validators
* generated binary codecs
* generated inspector metadata
* generated documentation
* compatibility reports
* schema manifests
* language-specific SDK files

Generated outputs are build artifacts. Production systems consume these
artifacts directly.

---

# Generated Languages

The schema compiler supports multiple target languages.

Initial target languages may include:

* C
* C++
* Rust
* Go
* Python

Generated APIs follow language conventions while preserving the same
record, envelope, payload, schema name, schema reference, and fieldIndex
semantics.

---

# Generated Runtime Bindings

Generated accessors provide typed access to binary payload data.

They are expected to support:

* reading fields directly from binary record storage
* checking field presence
* constructing records
* avoiding full-record materialization when practical

Generated accessors are the normal production interface for schema-specific
payload access.

Generated runtime bindings distinguish absent fields from present fields.

The schema compiler is the only supported producer of production runtime
bindings.

---

# Generated Documentation

The schema compiler can generate human-readable documentation for schemas.

Generated documentation should include:

* schema name
* schema version
* schema reference
* field names
* field types
* field presence semantics
* units
* descriptions
* compatibility notes
* reserved fields

Documentation is generated from the same schema inputs used for code
generation.

---

# Generated Validators

Generated validators check whether a record or payload conforms to its schema.

Validators may check:

* envelope consistency
* schema reference
* payload structure
* field presence consistency
* field type validity
* bounds and profile limits
* compatibility constraints

Validators are available for devices, cloud ingestion, tests, and tooling.

---

# Generated Binary Codecs

Generated binary codecs encode and decode record payloads according to the
selected binary encoding.

Codecs are expected to:

* preserve compiler-generated field indexes
* support deterministic encoding
* support bounded parsing
* avoid unnecessary allocation
* support unknown-field handling when allowed by the encoding
* expose profile-specific limits for embedded targets

The binary codec is generated from the schema. Application code does not
hand-maintain payload encoding logic.

---

# Generated Inspector Metadata

Generated inspector metadata supports tools that need to display, debug, index,
or inspect records.

Inspector metadata may include:

* schema name
* schema version
* schema reference
* field names
* field indexes
* field types
* field presence metadata
* units
* descriptions
* display hints

Inspector metadata is generated at build time. Tools may load the metadata
they were built or packaged with; production systems should not depend on
dynamic schema interpretation for normal operation.

---

# Build Integration

The schema compiler integrates with normal build systems.

Supported integration patterns include:

* command-line invocation
* CMake integration
* Cargo build scripts
* Go generate
* Python packaging hooks
* CI validation
* generated artifact caching

Builds fail when schemas are invalid, compatibility rules are violated, or
generated artifacts are stale.

---

# Compile-Time Philosophy

Quarry treats schema knowledge as compile-time knowledge.

The preferred flow is:

```text
schema
    ↓
schema compiler
    ↓
Generated artifacts
    ↓
Firmware, Services, SDKs, and Tools
```

Normal production behavior should not require:

* runtime schema interpretation
* runtime schema downloads
* dynamic field discovery
* hand-maintained field indexes
* hand-written binary codecs

This keeps device implementations small, deterministic, portable, and easier to
validate.

The schema compiler may generate different artifact classes from the same
schema: runtime artifacts, validation artifacts, documentation artifacts, and
tooling artifacts. Not every schema attribute affects every artifact.

---

# Relationship to Other Documents

Related architecture documents:

* `schema-model.md` defines schema identity, schema references, and field index
  rules.
* `data-model.md` defines records, envelopes, payloads, and generated accessors.
