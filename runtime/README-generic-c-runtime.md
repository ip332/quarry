# Generic C QBS/BRF runtime

The generic C runtime is an additive, read-only C99 API for embedded callers.
`quarry_qbs_parse()` indexes caller-owned QBS bytes into caller-provided typed
workspace arrays; it performs no hidden allocation. `quarry_brf_validate()`
then validates scalar, string, and bytes fields before exposing a record view.

```c
quarry_qbs_view_t schema;
quarry_brf_record_view_t record;
quarry_qbs_record_view_t records[8];
quarry_qbs_field_view_t fields[64];
quarry_qbs_type_view_t types[32];
quarry_qbs_enum_view_t enums[4];
uint64_t enum_values[32];
quarry_workspace_t workspace = { records, 8, fields, 64, types, 32,
                                 enums, 4, enum_values, 32 };

quarry_workspace_reset(&workspace);

quarry_qbs_parse(qbs, qbs_size, &schema, &workspace, &limits);
const quarry_qbs_record_view_t* type;
quarry_qbs_find_record_by_id(&schema, 1, &type);
quarry_brf_validate(&schema, type, brf, brf_size, &record, &limits);

uint64_t sequence;
quarry_brf_get_uint(&record, 0, &sequence);
```

The API uses `uint64_t`/`int64_t` logical integer carriers, independent of
the QBS field width. Strings and bytes are borrowed spans with explicit
lengths. QBS and BRF buffers, plus workspace, must outlive their views.

The public views are deliberately small POD values containing immutable source
references and stable indexes/offsets; they do not point into temporary parser
state. Field indexes are `uint16_t`, matching the bounded QBS field metadata.
All getters use the same `(record, field_index, out_value)` convention and
initialize their output to an empty/zero value before returning an error.

Workspace storage is always caller-owned. `quarry_workspace_reset()` clears
its typed entries while preserving capacities. Parsing or validation overwrites
workspace entries, and reset or reuse invalidates all views that refer to that
workspace; source QBS/BRF buffers must remain alive independently. A failed
parse/validation leaves its output view zeroed and no successful view may be
used. There is no hidden allocation or fallback storage.

String and byte results are zero-copy spans. They are not NUL-terminated, and
their pointers remain valid only while the BRF source buffer remains alive.
Zero-length present values return a borrowed pointer with size zero; callers
must use the size rather than dereference it. The current QBS integer model is
I8/U8, I16/U16, I32/U32, and I64/U64; the generic API intentionally exposes
logical `int64_t`/`uint64_t` carriers rather than width-specific getters.

Floating-point decoding requires the repository's normal IEEE-754 `float` and
`double` target model. Decoding is bytewise and does not require alignment or
type-punning serialized storage.

Phase 1 has no nested-record or array support: a present field of those types
returns `QUARRY_GENERIC_UNSUPPORTED_TYPE`. Their absent zeroed slots are
accepted, which permits scalar-only records to be read without copying or
allocating. The implementation uses checked bytewise big-endian decoding and
is MISRA-oriented, but is not formally MISRA-certified.
