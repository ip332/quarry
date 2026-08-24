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

Phase 1 has no nested-record or array support: a present field of those types
returns `QUARRY_GENERIC_UNSUPPORTED_TYPE`. Their absent zeroed slots are
accepted, which permits scalar-only records to be read without copying or
allocating. The implementation uses checked bytewise big-endian decoding and
is MISRA-oriented, but is not formally MISRA-certified.
