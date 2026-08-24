# Generic QBS-driven Python runtime

The generic runtime is additive to Quarry's generated Python runtime. It
accepts a validated QBS v1 image and exposes schema metadata without generated
classes:

```python
from quarry import load_qbs, validate_brf

schema = load_qbs(qbs_bytes)
record_type = schema.record_by_id(1)
record = validate_brf(schema, record_type, brf_bytes)
value = record[0]
child = record["child"]
samples = record["samples"]
for sample in samples:
    print(sample)
items = record["items"]
print(items[0]["value"])
```

QBS remains authoritative for field types, widths, storage, and limits. The
validated record retains the BRF backing buffer; byte fields are exposed as
`memoryview` objects, while decoded scalar and text values are ordinary Python
values. `record.fields()` includes absent fields, whose `present` member is
false and whose value is `None`. Generated schema-specific modules are not
replaced by this API.

The complete nested graph is validated before `validate_brf` returns. Nested
records and arrays are cache-backed read-only views and do not revalidate on
repeated access. `record.field_view(index_or_name)` and `record.fields()`
expose explicit presence, so an absent field is distinct from a present-empty
value or array. Minimal QBS images can use field indexes without reflective
names. Python `int` is the logical integer carrier; QBS metadata retains the
exact field width.
