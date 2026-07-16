| ID    | Decision                                                        | Spec                 |
| ----- | --------------------------------------------------------------- | -------------------- |
| D-001 | Use record terminology instead of message terminology           | Binary Record Format |
| D-002 | Record Header is fixed-size (16 bytes)                          | Binary Record Format |
| D-003 | Field Directory lists present fields                           | Binary Record Format |
| D-004 | Nested record fields contain complete embedded BRF records | Binary Record Format |
| D-005 | Field Directory entries include fieldIndex, fieldOffset, and fieldLength | Binary Record Format |
| D-006 | Record-array elements are length-delimited complete embedded BRF records; compact shared-header and homogeneous-envelope formats are deferred until measured wire-size pressure justifies a BRF vNext migration | Binary Record Format |

## D-006: Record Arrays Use Complete Embedded Records

PR-062 considered a homogeneous BRF envelope for record arrays:

```text
header:
  version
  flags
  reserved
  record_id
  item_count
  payload_length

payload:
  repeated item_count times:
    item_length: varuint
    record_body:
      directory_entry_count: varuint
      Field Directory
      Field Data
```

That design is structurally coherent and saves about 15 bytes for each
additional small record after the first, but it requires a new BRF header
version, changes the runtime parser and emitter, changes every exact-byte
runtime and generated-code fixture from PR-056 through PR-061, introduces
generated record-body helpers, and changes nested-record representation. It
also makes an empty record array larger than the v0.1 representation because
the empty array would carry a full homogeneous envelope.

BRF v0.1 therefore keeps the smaller PR-062 boundary: record-array elements are
length-delimited complete embedded BRF records. Reconsider a homogeneous
envelope only after measured bandwidth or storage pressure justifies a BRF
vNext migration.
