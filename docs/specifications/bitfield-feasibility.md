# Packed Bit-Field Fields — Feasibility Study

## Status

**This is a feasibility study, not a specification.** It is exploratory analysis only. No
schema syntax, layout algorithm, wire-format bit, or runtime API described in this document
exists, is approved, is scheduled, or is authorized for implementation. Nothing here overrides
or amends [Schema Language](schema-language.md), [Binary Record Format](binary-record-format.md),
or [Quarry Binary Schema](quarry-binary-schema.md).

This document answers one question: **given Quarry's current architecture, is it technically
feasible to support arbitrary-bit-length, sub-byte-packed fields — similar in spirit to the
automotive DBC/CAN signal format — and what would actually be involved?** It does not propose
that Quarry build this. It exists so that a future decision to pursue (or decline) the feature
can be made against real facts about the codebase rather than assumptions.

Everything after this section describes candidate ideas and open questions for a possible
future specification. None of it is normative. Per the
[Specification Authoring Guide](spec-authoring-guide.md), only numbered requirements in a
published specification are normative; this document's numbered items are **candidate
requirements**, explicitly marked as such, offered as a starting point if this work is ever
chartered — not as commitments.

## Purpose

DBC (the CAN-bus database format) lets a signal occupy an arbitrary bit range within a frame —
for example, an 11-bit value starting at bit 3, sharing a byte with two unrelated flags. Quarry's
Binary Record Format (BRF) is currently byte-aligned throughout: every field starts and ends on
a byte boundary. This study determines whether BRF/Quarry could be extended to support
DBC-like bit-packed fields without a foundational redesign, and if so, what the shape of that
extension would need to be.

The short answer is that this is **more feasible than a cold read of BRF would suggest**: the
newly-introduced BRF v2 and QBS formats already carry an unused `bit_offset`/`bit_width` hook
reserved for exactly this purpose (see [Evidence](#evidence-from-the-current-codebase) below).
This study exists to make that fact visible and to sketch what completing the hook would
actually require.

## Scope

This study covers:

* what bit-level support would look like at the schema, layout, wire-format, and runtime layers;
* which parts of the current codebase already anticipate this feature versus which parts
  actively forbid it today;
* how DBC's specific signal model (start bit, length, byte order, scale/offset, multiplexing)
  maps onto Quarry's architecture, and which parts of that model are in scope versus explicitly
  not;
* candidate design decisions and their trade-offs;
* a candidate incremental implementation sequence, offered only as a starting point.

This study does not define:

* actual schema syntax (a sketch is offered as an illustrative, non-normative example only);
* an actual layout algorithm, wire-format bit numbering amendment, or runtime API;
* scale/offset (physical-value transform) semantics — treated here as an explicitly separate,
  out-of-scope future feature;
* multiplexed/discriminated-union signals — likewise explicitly out of scope (see
  [Non-Goals](#non-goals));
* a BRF or QBS format version number for this feature, since no such version has been chartered.

## Terminology

* **Bit-packed field** / **packed field** — a field whose encoded value occupies fewer than 8
  bits, or otherwise does not begin and end on a byte boundary, sharing its containing byte(s)
  with one or more other fields.
* **DBC** — the CAN-bus signal database format this study uses as a reference point for what
  "arbitrary bit length" support looks like in an existing, widely-deployed system. Quarry has
  no relationship to DBC beyond this comparison.
* **BRF v2** — Quarry's current physical binary record representation. See
  [Binary Record Format](binary-record-format.md).
* **QBS** — Quarry Binary Schema, the runtime schema-metadata serialization introduced alongside
  BRF v2. See [Quarry Binary Schema](quarry-binary-schema.md).
* **Layout IR** / **BRF Layout IR** — compiler-owned physical layout metadata produced by
  `LayoutComputer` (`compiler/layout/`).
* **`FieldLocation`** — the compiler's per-field position record (`compiler/layout/layout.hpp`),
  which already contains `byte_offset`, `bit_offset`, and `bit_width` members.

## Evidence from the current codebase

This section is factual, grounded in the repository at the commit this study was performed
against (`main`, `21976b6`). It is the basis for the "more feasible than expected" conclusion
above.

* `FieldLocation` (`compiler/layout/layout.hpp:55-59`) already has the shape:
  ```cpp
  struct FieldLocation {
      std::uint32_t byte_offset = 0;
      std::uint8_t  bit_offset  = 0;
      std::uint32_t bit_width   = 0;
  };
  ```
  `construct_record_layout` (`compiler/layout/layout.cpp:367-385`) currently always sets
  `bit_offset = 0` and `bit_width = slot_size * 8` — computed, but only ever in whole-byte
  multiples.
* QBS's field descriptor already carries `bit_offset`/`bit_width` on the wire today
  (`docs/specifications/quarry-binary-schema.md`, field table, byte offsets 8 and 10 of the
  28-byte descriptor), with an explicit "Bit-precision provision" section stating: *"QBS v1
  carries `byte_offset`, `bit_offset`, and `bit_width` in every field descriptor... The v1
  physical format reserves the representation needed for future packed fields but does not
  define public packed-field syntax."* QBS's own structural validator already only requires
  `bit_offset <= 7` (`compiler/qbs/qbs.cpp:747-748`), not `== 0` — looser than the BRF v2 decoder
  itself.
* The BRF v2 specification has already pre-committed to a bit-numbering convention it does not
  yet use (`docs/specifications/binary-record-format.md:761-769`): *"If packed fields are added,
  the specification MUST define bit numbering separately from byte endianness. The reserved
  direction is least-significant-bit first within each addressed byte."* This is DBC's "Intel"
  bit-numbering convention.
* `binary_record_v2.hpp` actively rejects non-byte-aligned fields today
  (`include/quarry/runtime/binary_record_v2.hpp:320-323`, repeated at `:465`, `:605-610`):
  `field.bit_offset != 0 || field.bit_width != slot_size * 8` is a hard decode error. The
  guardrail exists precisely where a future implementation would need to relax it.
  Correspondingly, the format's byte-order rule already forbids the trap most bitfield
  implementations fall into: BRF v2 requires *"explicit byte-wise encoding and decoding that is
  safe for unaligned storage"* and disallows relying on native C/C++ struct layout or compiler
  bit-fields for the wire format — so this feature, if built, would not be able to take the
  fragile shortcut of `struct { unsigned a:3; unsigned b:5; }`.
* The presence bitmap (`include/quarry/runtime/binary_record_v2.hpp:256-272`) already computes
  `byte = bitmap_offset + bit_index / 8; mask = 1 << (bit_index % 8)` — the exact arithmetic
  shape a general bit-field read/write primitive would need, currently hardwired to single-bit
  presence flags rather than generalized to arbitrary widths.

## What is missing, layer by layer

| Layer | Current state | Gap for arbitrary bit-width fields |
|---|---|---|
| Schema language | `canonical_primitive_type()` (`compiler/semantic/semantic.cpp:25-60`) matches fixed literal type names only (`bool`, `i8`…`f64`) | No `uint:N` / bit-field syntax exists |
| Schema IR | `PrimitiveType` (`proto/quarry/schema_ir.proto:139-152`) is a closed enum | No width attribute on the message |
| Layout | `FieldLocation` has the fields; `layout.cpp` never sets them to anything but byte-aligned | Needs a bit-cursor, not just a byte-cursor, and a packing algorithm |
| Runtime (C/C++) | `quarry_c_write_u8`/`read_u8` etc. are the only primitives (`runtime_c/binary_record.h:104,216`) — byte-granular only | No cross-byte-boundary shift/mask read-write, no sign-extension for signed sub-byte values |
| Codegen | Generates one accessor per fixed scalar type | Would need to emit bit-extract/insert calls with compile-time-constant `(byte_offset, bit_offset, bit_width)` |

## Mapping the DBC signal model

DBC's signal model is `start_bit | length @ byte_order (scale, offset) [min|max]`. Evaluated
piece by piece against Quarry's architecture:

* **Start bit + length within a record** — maps directly onto `bit_offset`/`bit_width`. This is
  the part already reserved in BRF v2 and QBS.
* **Per-signal byte order** (DBC's Intel vs. Motorola bit numbering, which can differ *per
  signal within the same frame*) — this is the single largest real-world source of DBC/CAN
  interoperability bugs. A candidate recommendation is to **not** replicate this: use the one
  convention BRF v2 already reserved (LSB-first) uniformly across every packed field. Supporting
  mixed per-field bit order would roughly double the runtime primitive surface and reintroduce
  the exact bug class DBC tooling is well known for.
* **Scale/offset physical-value transform** — orthogonal to bit-packing itself; see
  [Non-Goals](#non-goals).
* **Multiplexed signals** — a discriminated union; see [Non-Goals](#non-goals).
* **Signed sub-byte values** — would need sign-extension on decode. Mechanically simple, but a
  common source of off-by-one-bit errors and worth explicit, careful specification if pursued.

## Non-Goals

Explicitly out of scope for this study and for any minimal first version of this feature, should
it ever be chartered:

* **Scale/offset/unit annotations.** Quarry's runtime is deliberately "representation-neutral
  byte mechanics" with no value-transformation today (see Compile-Time Knowledge in
  `docs/principles.md`). A physical-value transform is a legitimate, separate future feature
  (additive schema metadata, generated as constants for application code to apply) and should
  not gate or complicate bit-packing itself.
* **Multiplexed/discriminated-union signals.** [Quarry Binary Schema](quarry-binary-schema.md)
  already lists unions and variants as *"not currently represented by Quarry's canonical Layout
  IR."* This is a materially larger, independent feature that bit-packing does not require.
* **Per-field byte order.** See above.

## Candidate design questions

These are open questions a future specification would need to resolve, not decisions made here.

1. **Explicit grouping versus automatic bin-packing.** Native C bitfields auto-pack in
   declaration order, which is exactly the implementation-defined, reorder-fragile behavior BRF
   v2 already forbids relying on for the wire format. A candidate direction is an **explicit
   packed-field group** in schema syntax, where every sub-field's bit position is assigned
   deterministically only within that group — so an unrelated field added elsewhere in the
   record can never silently shift existing bit offsets. This would need to be weighed against
   QBS's stated goal of stable field indexes/offsets for compatibility. See the schema syntax
   sketch under [Examples](#examples), which takes this further and requires every member's bit
   position to be stated explicitly rather than auto-assigned even within the group.
2. **Presence semantics for packed fields.** DBC signals have no per-signal optionality — a
   candidate direction is that packed fields are always non-optional (no presence bit), leaving
   the existing presence-bitmap machinery untouched.
3. **Format versioning.** Any real implementation would need a new BRF/QBS version gate, the
   same pattern already used to introduce v2, rather than a retrofit of the current format.

## Candidate Requirements (Non-Normative)

The following are illustrative candidate requirements only, in the numbering style
`docs/specifications/spec-authoring-guide.md` defines for actual specifications. **None of these
are approved, binding, or part of any published specification.** They exist to show what a real
specification's Requirements section might need to cover, and to give a future author a
starting point rather than a blank page.

* CAND-BITFIELD-001 — A packed field's bit position SHOULD be assigned only relative to other
  fields declared within the same explicit packed group, never relative to the whole record.
* CAND-BITFIELD-002 — Packed-field bit numbering SHOULD use one fixed convention
  (least-significant-bit first within each addressed byte), matching the direction BRF v2
  already reserves. Per-field byte order SHOULD NOT be supported in an initial version.
* CAND-BITFIELD-003 — A packed field SHOULD NOT participate in the presence bitmap.
* CAND-BITFIELD-004 — A signed packed field's decoded value SHOULD be sign-extended from its
  declared bit width to its logical type's full width.
* CAND-BITFIELD-005 — No runtime bit read/write primitive SHOULD depend on native C/C++ compiler
  bitfield layout, struct padding, or host endianness.
* CAND-BITFIELD-006 — A packed member's `bit` position SHALL be stated explicitly in schema
  source and SHALL NOT be auto-assigned from declaration order.
* CAND-BITFIELD-007 — A packed group SHALL carry at most one presence bit for the entire group;
  individual members SHALL NOT have their own presence bit.
* CAND-BITFIELD-008 — A packed member's declared `width` SHALL be between 1 and 64 bits
  inclusive.
* CAND-BITFIELD-009 — The compiler SHALL reject two packed members of the same group whose
  `[bit, bit + width)` ranges overlap.
* CAND-BITFIELD-010 — Initial packed-member type support SHOULD be limited to `uint`, `int`, and
  `bool`; enum and other member types are deferred.

## Examples

### Informative Example — candidate schema syntax sketch

This sketch is illustrative only; it does not exist in [Schema Language](schema-language.md) and
is not proposed for adoption exactly as written. It follows Quarry's existing `.brd` YAML field
style rather than inventing a new schema syntax family.

```yaml
namespace: sensors.frame
record: SensorFrame
version: 1
type: data
fields:
  timestamp:
    type: uint32
  status_flags:
    type: packed
    bits:
      mode:
        type: uint
        width: 3
        bit: 0
      raw_reading:
        type: uint
        width: 11
        bit: 3
      fault:
        type: bool
        bit: 14
      reserved:
        type: uint
        width: 1
        bit: 15
  battery_mv:
    type: uint16
```

Design choices this sketch makes, each a candidate decision rather than a settled one:

* **A packed group is declared like any other field** (`status_flags: { type: packed, ... }`),
  reusing the existing `fields:` map rather than introducing a new top-level schema section.
  This keeps the schema-language surface change minimal.
* **Every member's `bit:` position is explicit, not auto-assigned**, matching DBC's own model
  (every signal states its own start bit) and avoiding the reorder-fragility of C-style
  declaration-order bin-packing (see Candidate Design Question 1). Inserting a new member later
  requires picking an unused bit range explicitly, exactly as inserting a new DBC signal does.
* **`bit:` is relative to the group's own bit-space, not the record's.** `mode`'s `bit: 0` and
  some unrelated field elsewhere in the record are never in the same numbering space — this is
  what makes the group boundary meaningful rather than cosmetic.
* **The group, not its members, carries the presence bit.** `status_flags` behaves like any
  other optional Quarry field (`has_status_flags`) at the record level; `mode`, `raw_reading`,
  `fault`, and `reserved` are mandatory once the group is present, with no individual presence
  bits of their own — consistent with CAND-BITFIELD-007 and with DBC signals having no
  per-signal optionality.
* **Only `uint`, `int`, and `bool` are candidate member types.** These are new type keywords
  distinct from the existing fixed-width names (`uint32`, etc.) precisely because their width is
  supplied separately via `width:`; `bool` implies `width: 1` and does not accept an explicit
  `width:`. Packing an `enum` into a sub-byte width is a plausible future extension, not covered
  by this sketch.
* **In memory, members stay ordinary flat fields.** Generated code would expose `mode`,
  `raw_reading`, `fault`, and `reserved` as normal struct members with normal full-width types
  (e.g. a plain `uint32_t mode`, not a native C bitfield) — bit-packing would be confined to the
  wire encode/decode step, mirroring how strings and arrays already keep a simple in-memory
  representation distinct from their specialized wire encoding.

An alternative considered and set aside for a first version: auto-assigning `bit:` positions in
declaration order within the group (lower verbosity, closer to native C bitfields). It was set
aside because it reintroduces exactly the "inserting a field shifts everything after it"
fragility BRF v2's byte-aligned design already avoids — just scoped down to one group instead of
the whole record. It remains a plausible lower-verbosity opt-in mode to revisit later, not ruled
out permanently.

### Informative Example — candidate `FieldLocation` values for the sketch above

Assuming `status_flags` falls at byte offset 5 in `SensorFrame` (after the presence bitmap and
`timestamp`):

```text
status_flags (group) : byte_offset=5, occupies 2 bytes, one shared presence bit
  mode        : byte_offset=5, bit_offset=0, bit_width=3
  raw_reading : byte_offset=5, bit_offset=3, bit_width=11
  fault       : byte_offset=6, bit_offset=6, bit_width=1
  reserved    : byte_offset=6, bit_offset=7, bit_width=1
```

`raw_reading`'s 11 bits span the boundary between byte 5 (bits 3-7, 5 bits) and byte 6 (bits 0-5,
6 bits) — exactly the cross-byte-boundary case a real implementation's runtime primitives would
need to handle correctly, and exactly the case DBC has decades of hard-won field experience with
getting wrong.

## Candidate Incremental Path

Offered only as a starting point, in the phased style
[Quarry Binary Schema](quarry-binary-schema.md) itself uses for its own proposed sequence:

```text
1  Schema syntax + semantic validation for an explicit packed-field group
2  schema_ir.proto width attribute + layout bit-cursor in layout.cpp
3  Runtime bit read/write primitives (C and C++), with sign-extension for signed widths
4  Codegen support: emit bit-extract/insert calls with compile-time-constant locations
5  QBS/BRF v2 validators relax bit_offset==0 into real bit-range validation
6  New fixtures/tests: cross-byte-boundary widths, width=1, width=64-unaligned,
   signed round-trip, and group-isolation compatibility tests
```

## Open Questions

1. Should this feature be chartered at all, or does Quarry's target domain (schema-driven
   edge-to-cloud records) have enough demand for sub-byte packing to justify the added runtime
   and specification surface?
2. If chartered, does it belong in a BRF v3, or as an additive capability within BRF v2's
   existing version number (given the hooks were already reserved there)?
3. Would an explicit packed-group schema construct compose cleanly with existing schema
   evolution/compatibility rules, or does it need its own compatibility chapter?
4. Is DBC genuinely the right reference model, or would a narrower "just support small enum-like
   sub-byte fields" scope satisfy the actual motivating use case with less complexity?

## Related Documents

* [Binary Record Format](binary-record-format.md) — defines BRF v2's `FieldLocation` bit
  reservation and its current byte-alignment requirement.
* [Quarry Binary Schema](quarry-binary-schema.md) — defines the QBS field descriptor's
  `bit_offset`/`bit_width` carriage and its current validation rules.
* [Schema Language](schema-language.md) — current schema syntax and type system; any future
  packed-field syntax would be specified here.
* [Specification Authoring Guide](spec-authoring-guide.md) — structure and normative-language
  rules this document follows for its candidate-requirements section.
