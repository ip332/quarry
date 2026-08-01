# Python Backend

The Python backend consumes validated Schema IR and emits importable dataclass
modules. Its generated public representation is unchanged across PR-117
through PR-126: scalar, enum, string, bytes, nested record, and supported
array fields are
`Optional[...]` values where `None` means absent and an empty list means a
present empty array.

Supported array element types are fixed-width scalars, same-namespace
non-negative enums, bounded strings, and bounded bytes. String and bytes
arrays use the existing BRF count-plus-length-delimited-element encoding and
carry the element `max_bytes` constraint from Schema IR into the runtime
helper call. Same-namespace nested record fields are supported by composing
generated record helpers. Same-namespace record arrays use the same generated
child helpers and existing variable-width array framing. Nested arrays and
cross-namespace references remain unsupported.

The backend does not modify Schema IR or the compiler pipeline and does not
depend on the C or C++ backend. See [the design document](../../docs/design/python-backend.md)
for the complete lowering, runtime, naming, testing, and packaging contract.
