package brf

import (
	"encoding/binary"
	"math"
	"unicode/utf8"
)

// Reader validates one BRF v2 record against its compiler-computed layout and
// gives generated code bounds-checked access to its slots and variable
// objects. Variable objects must be read in schema declaration order, which
// is how BRF v2 requires them to be laid out.
type Reader struct {
	data     []byte
	fixedEnd uint32
	tail     uint32
	size     uint32
}

// NewReader validates the record header and presence bitmap of data. It
// rejects any record whose version, flags, header size, record ID, fixed
// region size or record size do not match the schema, whose length is not
// exactly record_size, or whose unused presence bits are set.
func NewReader(data []byte, recordID, fixedRegionSize, presenceBitmapSize, fieldCount uint32) (Reader, error) {
	if len(data) < HeaderSize {
		return Reader{}, decodeError(0, "truncated record header (%d bytes)", len(data))
	}
	if data[0] != FormatVersion {
		return Reader{}, decodeError(0, "unsupported BRF format version %d", data[0])
	}
	if data[1] != 0 {
		return Reader{}, decodeError(1, "unsupported flags 0x%02x", data[1])
	}
	if h := binary.BigEndian.Uint16(data[2:]); h != HeaderSize {
		return Reader{}, decodeError(2, "unsupported header size %d", h)
	}
	if id := binary.BigEndian.Uint32(data[4:]); id != recordID {
		return Reader{}, decodeError(4, "unexpected record id %d (expected %d)", id, recordID)
	}
	if fixed := binary.BigEndian.Uint32(data[8:]); fixed != fixedRegionSize {
		return Reader{}, decodeError(8, "fixed region size %d does not match the schema (%d)", fixed, fixedRegionSize)
	}
	size := binary.BigEndian.Uint32(data[12:])
	if uint64(size) != uint64(len(data)) {
		return Reader{}, decodeError(12, "record size %d does not match the input length %d", size, len(data))
	}
	fixedEnd := uint64(HeaderSize) + uint64(fixedRegionSize)
	if fixedEnd > uint64(size) {
		return Reader{}, decodeError(8, "fixed region extends beyond the record")
	}
	if presenceBitmapSize > fixedRegionSize {
		return Reader{}, decodeError(HeaderSize, "presence bitmap is larger than the fixed region")
	}
	if used := fieldCount % 8; presenceBitmapSize != 0 && used != 0 {
		last := HeaderSize + int(presenceBitmapSize) - 1
		if data[last]&^byte((1<<used)-1) != 0 {
			return Reader{}, decodeError(last, "unused presence bits are set")
		}
	}
	return Reader{data: data, fixedEnd: uint32(fixedEnd), tail: uint32(fixedEnd), size: size}, nil
}

// PeekRecordID returns the record_id of a BRF v2 record after checking its
// version, flags and header size, without validating the rest of the record.
// Services receiving several record types can use it to choose the generated
// type to decode into; that decode then validates the whole record.
func PeekRecordID(data []byte) (uint32, error) {
	if len(data) < HeaderSize {
		return 0, decodeError(0, "truncated record header (%d bytes)", len(data))
	}
	if data[0] != FormatVersion || data[1] != 0 || binary.BigEndian.Uint16(data[2:]) != HeaderSize {
		return 0, decodeError(0, "not a BRF v2 record header")
	}
	return binary.BigEndian.Uint32(data[4:]), nil
}

// Present reports whether presence bit `bit` is set.
func (r *Reader) Present(bit uint32) bool {
	return r.data[HeaderSize+bit/8]&(1<<(bit%8)) != 0
}

// Absent verifies that an absent field's slot is canonical zero storage.
func (r *Reader) Absent(at, size uint32) error {
	for i, v := range r.data[at : at+size] {
		if v != 0 {
			return decodeError(int(at)+i, "absent field has nonzero storage")
		}
	}
	return nil
}

// Slot returns the fixed-region bytes of a field.
func (r *Reader) Slot(at, size uint32) []byte { return r.data[at : at+size : at+size] }

// Variable reads the descriptor at record offset `at` of a present variable
// field and returns its object. Objects must be contiguous and in
// declaration order; zero-length objects may share an offset.
func (r *Reader) Variable(at uint32) ([]byte, error) {
	offset := binary.BigEndian.Uint32(r.data[at:])
	length := binary.BigEndian.Uint32(r.data[at+4:])
	if offset != r.tail {
		return nil, decodeError(int(at), "variable object at offset %d is not contiguous with the previous object (expected %d)", offset, r.tail)
	}
	if offset > r.size || length > r.size-offset {
		return nil, decodeError(int(at), "variable object [%d, +%d) extends beyond the record", offset, length)
	}
	r.tail = offset + length
	return r.data[offset:r.tail:r.tail], nil
}

// Finish verifies that the variable objects cover the record exactly.
func (r *Reader) Finish() error {
	if r.tail != r.size {
		return decodeError(int(r.tail), "unexpected trailing bytes after the variable region")
	}
	return nil
}

// Bool decodes a one-byte BRF bool slot at record offset `at`.
func (r *Reader) Bool(at uint32) (bool, error) { return decodeBool(r.data[at], int(at)) }

// Uint8 reads the byte at record offset `at`.
func (r *Reader) Uint8(at uint32) uint8 { return r.data[at] }

// Uint16 reads a big-endian uint16 at record offset `at`.
func (r *Reader) Uint16(at uint32) uint16 { return binary.BigEndian.Uint16(r.data[at:]) }

// Uint32 reads a big-endian uint32 at record offset `at`.
func (r *Reader) Uint32(at uint32) uint32 { return binary.BigEndian.Uint32(r.data[at:]) }

// Uint64 reads a big-endian uint64 at record offset `at`.
func (r *Reader) Uint64(at uint32) uint64 { return binary.BigEndian.Uint64(r.data[at:]) }

// Float32 reads big-endian IEEE 754 bits at record offset `at`.
func (r *Reader) Float32(at uint32) float32 { return math.Float32frombits(r.Uint32(at)) }

// Float64 reads big-endian IEEE 754 bits at record offset `at`.
func (r *Reader) Float64(at uint32) float64 { return math.Float64frombits(r.Uint64(at)) }

func decodeBool(v byte, offset int) (bool, error) {
	switch v {
	case 0:
		return false, nil
	case 1:
		return true, nil
	}
	return false, decodeError(offset, "invalid bool byte 0x%02x", v)
}

// Bool decodes one BRF bool array element.
func Bool(p []byte) (bool, error) { return decodeBool(p[0], 0) }

// Uint16 decodes a big-endian uint16 array element.
func Uint16(p []byte) uint16 { return binary.BigEndian.Uint16(p) }

// Uint32 decodes a big-endian uint32 array element.
func Uint32(p []byte) uint32 { return binary.BigEndian.Uint32(p) }

// Uint64 decodes a big-endian uint64 array element.
func Uint64(p []byte) uint64 { return binary.BigEndian.Uint64(p) }

// Float32 decodes a big-endian IEEE 754 float32 array element.
func Float32(p []byte) float32 { return math.Float32frombits(binary.BigEndian.Uint32(p)) }

// Float64 decodes a big-endian IEEE 754 float64 array element.
func Float64(p []byte) float64 { return math.Float64frombits(binary.BigEndian.Uint64(p)) }

// String decodes a string object: at most max bytes of valid UTF-8.
func String(p []byte, max uint32) (string, error) {
	if uint64(len(p)) > uint64(max) {
		return "", decodeError(0, "string length %d exceeds max_bytes=%d", len(p), max)
	}
	if !utf8.Valid(p) {
		return "", decodeError(0, "string is not valid UTF-8")
	}
	return string(p), nil
}

// Bytes decodes a bytes object of at most max bytes, returning a copy (nil
// for an empty object).
func Bytes(p []byte, max uint32) ([]byte, error) {
	if uint64(len(p)) > uint64(max) {
		return nil, decodeError(0, "bytes length %d exceeds max_bytes=%d", len(p), max)
	}
	if len(p) == 0 {
		return nil, nil
	}
	return append([]byte(nil), p...), nil
}

// ReadVaruint reads one unsigned LEB128 value from data at off and returns it
// with the offset just past it. It rejects truncated input and values that
// need more than 64 bits or 10 bytes.
func ReadVaruint(data []byte, off int) (uint64, int, error) {
	var value uint64
	start := off
	for shift := uint(0); shift < 7*MaxVaruintBytes; shift += 7 {
		if off >= len(data) {
			return 0, off, decodeError(start, "truncated varuint")
		}
		c := data[off]
		off++
		payload := uint64(c & 0x7F)
		if shift == 63 && payload > 1 {
			return 0, off, decodeError(start, "varuint overflows 64 bits")
		}
		value |= payload << shift
		if c&0x80 == 0 {
			return value, off, nil
		}
	}
	return 0, off, decodeError(start, "malformed varuint (exceeds %d bytes)", MaxVaruintBytes)
}

// Array walks a variable array object: a varuint element count followed by
// fixed-width or length-prefixed elements.
type Array struct {
	data []byte
	off  int
}

// NewArray reads the element count of an array object and rejects counts
// above max.
func NewArray(data []byte, max uint32) (Array, int, error) {
	count, off, err := ReadVaruint(data, 0)
	if err != nil {
		return Array{}, 0, err
	}
	if count > uint64(max) {
		return Array{}, 0, decodeError(0, "array count %d exceeds max_elements=%d", count, max)
	}
	return Array{data: data, off: off}, int(count), nil
}

// ExpectFixed verifies that exactly count elements of width bytes remain.
func (a *Array) ExpectFixed(count, width int) error {
	if remaining := len(a.data) - a.off; uint64(remaining) != uint64(count)*uint64(width) {
		return decodeError(a.off, "array payload has %d bytes after the count, expected %d for %d elements", remaining, uint64(count)*uint64(width), count)
	}
	return nil
}

// Fixed returns the next element of width bytes.
func (a *Array) Fixed(width int) ([]byte, error) {
	if width > len(a.data)-a.off {
		return nil, decodeError(a.off, "truncated array element")
	}
	p := a.data[a.off : a.off+width : a.off+width]
	a.off += width
	return p, nil
}

// Prefixed returns the next varuint-length-prefixed element.
func (a *Array) Prefixed() ([]byte, error) {
	n, off, err := ReadVaruint(a.data, a.off)
	if err != nil {
		return nil, err
	}
	if n > uint64(len(a.data)-off) {
		return nil, decodeError(off, "array element length %d exceeds the remaining %d bytes", n, len(a.data)-off)
	}
	end := off + int(n)
	p := a.data[off:end:end]
	a.off = end
	return p, nil
}

// Finish verifies that the elements consumed the whole array object.
func (a *Array) Finish() error {
	if a.off != len(a.data) {
		return decodeError(a.off, "trailing bytes in array payload")
	}
	return nil
}

// UnknownEnum reports a decoded enum value the schema does not declare.
func UnknownEnum(typeName string, value uint64) error {
	return decodeError(0, "%d is not a valid %s value", value, typeName)
}
