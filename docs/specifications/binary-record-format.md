# Record Header v0.1

```
headerVersion   uint8
flags           uint8
recordVersion   uint8
reserved        uint8
recordId        uint32
sequenceNumber  uint32
payloadLength   uint32


## Nested Records

Nested records are encoded as payload fragments.

A nested record SHALL NOT contain a Record Header.

The type of a nested record is determined exclusively by the parent schema.

Only top-level records contain Record Headers.

This design preserves deterministic parsing, minimizes encoding overhead, and follows the Breadcrumbs principle of compile-time knowledge over runtime discovery.

Polymorphic nested records are not supported in v0.1.


# Array size

The array element count SHALL be encoded using the smallest fixed-width unsigned integer capable of representing the schema-defined max_elements value.

The count SHALL NOT exceed max_elements.

Array elements SHALL be encoded immediately after the count, in index order.

Unbounded arrays are not supported.