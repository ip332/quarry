# Quarry Go runtime

`github.com/ip332/quarry/runtime/go/brf` contains the BRF v2 byte mechanics
used by Go code that `quarry-schema-compiler --language go` generates. It is
meant for cloud services and tools that exchange records with devices running
the C or C++ generated code: the encodings are byte-identical.

The package is small and schema-neutral. Generated code supplies record IDs,
compiler-computed slot offsets, presence bits, bounds and enum values; the
runtime writes headers, fixed slots, descriptors, varuints and array framing,
and validates records on decode. It uses only the Go standard library, has no
global state and no reflection, and needs Go 1.22 or newer.

## Using generated code

```sh
quarry-schema-compiler --language go --go-import-prefix example.com/svc/gen \
  --output-directory gen schema.brd
```

```go
import (
	"example.com/svc/gen/acme/telemetry"
	"github.com/ip332/quarry/runtime/go/brf"
)

sample := telemetry.Sample{Count: brf.Ptr(uint32(42)), Label: "ok"}
data, err := sample.MarshalBRF()

var decoded telemetry.Sample
err = decoded.UnmarshalBRF(data) // validates the whole record
```

Optional scalar, enum and nested-record fields are pointers and are nil when
absent; `brf.Ptr` helps set them. String, bytes and array fields are absent
when empty, as BRF v2 requires. `AppendBRF` appends to an existing buffer to
avoid an allocation per record.

A service receiving several record types can dispatch on the header:

```go
id, err := brf.PeekRecordID(data)
switch id {
case telemetry.SampleRecordID:
	var s telemetry.Sample
	err = s.UnmarshalBRF(data)
}
```

## Errors

Decoding returns `*brf.DecodeError` (with a byte offset) and encoding returns
`*brf.EncodeError`, wrapped in `*brf.FieldError` values whose path names the
field, for example `children[2].label`. Use `errors.As` to inspect them.

Decode rejects everything the BRF v2 specification requires: header version,
flags, size and record-ID mismatches, a record length different from the
input, nonzero unused presence bits, nonzero storage of absent fields,
non-contiguous or out-of-range variable objects, malformed or oversized array
framing, invalid bools, undeclared enum values, invalid UTF-8, and values over
the schema's `max_bytes` / `max_elements`. Legacy records that mark empty
strings, bytes or arrays as present are accepted; re-encoding normalizes them.

## Compatibility

`brf.GeneratedCodeAPIVersion` is the Go generated-code epoch (currently `1`).
Generated files contain a constant check that fails to compile against a
runtime with a different epoch. It is independent of the C, C++ and Python
epochs and of the BRF format version.

## Development

```sh
cd runtime/go
go vet ./... && go test ./...
```

The CMake test suite also runs these tests and checks generated Go against the
C codec when a Go toolchain is installed.
