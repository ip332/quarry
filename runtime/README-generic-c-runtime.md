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

Phase 2 adds primitive arrays and fixed/variable nested and record-array views.
Use `quarry_brf_validate_with_workspace()` when structural fields are present;
the workspace must provide node, field-map, relation, array-element, and frame
storage. `quarry_brf_get_array()`, `quarry_brf_get_record()`, and
`quarry_brf_get_record_array()` return zero-copy validated views, and indexed
access never reparses or revalidates the BRF graph. A structural view remains
valid until the workspace is reset/reused or its source buffers are released.

The validator uses an explicit frame stack and validates the complete graph
before returning the root view. Present arrays and records therefore fail at
root validation if malformed; absent structural fields retain Phase 1's
canonical zero-storage semantics. The implementation uses checked bytewise
big-endian decoding and is MISRA-oriented, but is not formally MISRA-certified.

For new generic C applications, `quarry_brf_validate_with_workspace()` is the
recommended entry point even for flat records: it keeps the validation path
stable when a schema later gains nested or array fields. The original
`quarry_brf_validate()` remains a lightweight Phase 1 convenience API and
intentionally rejects present structural fields with
`QUARRY_GENERIC_UNSUPPORTED_TYPE`.

A structural caller supplies the additional fixed-capacity stores explicitly:

```c
quarry_brf_record_node_t nodes[64];
quarry_brf_field_state_t field_states[256];
uint32_t field_maps[256], array_elements[64];
quarry_brf_child_relation_t children[64];
quarry_brf_record_array_relation_t arrays[32];
quarry_brf_validation_frame_t frames[64];
/* Assign these buffers and capacities in quarry_workspace_t. */
quarry_brf_validate_with_workspace(&schema, type, brf, brf_size, &record,
                                   &workspace, &limits);
```

Insufficient node, field-map, relation, element, or frame capacity returns
`QUARRY_GENERIC_WORKSPACE_EXHAUSTED`; configured nesting/array/work limits
return `QUARRY_GENERIC_RESOURCE_LIMIT` or malformed-BRF status as appropriate.
The workspace count fields are diagnostic usage values written after a
successful structural validation; callers should treat them as read-only and
must reset the workspace before reuse.
