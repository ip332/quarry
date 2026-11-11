package brf

import (
	"encoding/binary"
	"math"
	"unicode/utf8"
)

// BeginRecord appends a BRF v2 header and a zeroed fixed region of
// fixedRegionSize bytes (which includes the presence bitmap) to b. The record
// starts at len(b) before the call; pass that offset to FinishRecord.
// Offsets used with the record's Put and EndVariable helpers are relative to
// the record start, exactly as the compiler's layout reports them.
func BeginRecord(b []byte, recordID, fixedRegionSize uint32) []byte {
	base := len(b)
	b = append(b, make([]byte, HeaderSize+int(fixedRegionSize))...)
	record := b[base:]
	record[0] = FormatVersion
	binary.BigEndian.PutUint16(record[2:], HeaderSize)
	binary.BigEndian.PutUint32(record[4:], recordID)
	binary.BigEndian.PutUint32(record[8:], fixedRegionSize)
	return b
}

// FinishRecord writes the record_size header field of the record starting at
// base, after every field and variable object has been appended.
func FinishRecord(b []byte, base int) ([]byte, error) {
	size := len(b) - base
	if uint64(size) > math.MaxUint32 {
		return b, encodeError("record size %d exceeds the 32-bit BRF v2 bound", size)
	}
	binary.BigEndian.PutUint32(b[base+12:], uint32(size))
	return b, nil
}

// SetPresent sets presence bit `bit` of the record starting at record[0].
func SetPresent(record []byte, bit uint32) {
	record[HeaderSize+bit/8] |= 1 << (bit % 8)
}

// EndVariable completes a present variable field whose object was appended to
// b starting at start: it writes the descriptor at record offset `at` and sets
// presence bit `bit` for the record starting at base.
func EndVariable(b []byte, base int, at uint32, start int, bit uint32) error {
	offset, length := start-base, len(b)-start
	if uint64(offset) > math.MaxUint32 || uint64(length) > math.MaxUint32 {
		return encodeError("variable object exceeds the 32-bit BRF v2 bound")
	}
	record := b[base:]
	binary.BigEndian.PutUint32(record[at:], uint32(offset))
	binary.BigEndian.PutUint32(record[at+4:], uint32(length))
	SetPresent(record, bit)
	return nil
}

// PutBool writes a BRF bool (0x00 or 0x01).
func PutBool(p []byte, v bool) {
	if v {
		p[0] = 1
	} else {
		p[0] = 0
	}
}

// PutUint8 writes one byte.
func PutUint8(p []byte, v uint8) { p[0] = v }

// PutUint16 writes a big-endian uint16.
func PutUint16(p []byte, v uint16) { binary.BigEndian.PutUint16(p, v) }

// PutUint32 writes a big-endian uint32.
func PutUint32(p []byte, v uint32) { binary.BigEndian.PutUint32(p, v) }

// PutUint64 writes a big-endian uint64.
func PutUint64(p []byte, v uint64) { binary.BigEndian.PutUint64(p, v) }

// PutFloat32 writes the IEEE 754 bits of v, big-endian (NaN payloads and
// negative zero are preserved).
func PutFloat32(p []byte, v float32) { binary.BigEndian.PutUint32(p, math.Float32bits(v)) }

// PutFloat64 writes the IEEE 754 bits of v, big-endian.
func PutFloat64(p []byte, v float64) { binary.BigEndian.PutUint64(p, math.Float64bits(v)) }

// AppendBool appends a BRF bool.
func AppendBool(b []byte, v bool) []byte {
	if v {
		return append(b, 1)
	}
	return append(b, 0)
}

// AppendUint16 appends a big-endian uint16.
func AppendUint16(b []byte, v uint16) []byte { return binary.BigEndian.AppendUint16(b, v) }

// AppendUint32 appends a big-endian uint32.
func AppendUint32(b []byte, v uint32) []byte { return binary.BigEndian.AppendUint32(b, v) }

// AppendUint64 appends a big-endian uint64.
func AppendUint64(b []byte, v uint64) []byte { return binary.BigEndian.AppendUint64(b, v) }

// AppendFloat32 appends the big-endian IEEE 754 bits of v.
func AppendFloat32(b []byte, v float32) []byte {
	return binary.BigEndian.AppendUint32(b, math.Float32bits(v))
}

// AppendFloat64 appends the big-endian IEEE 754 bits of v.
func AppendFloat64(b []byte, v float64) []byte {
	return binary.BigEndian.AppendUint64(b, math.Float64bits(v))
}

// AppendVaruint appends v as unsigned LEB128.
func AppendVaruint(b []byte, v uint64) []byte {
	for v >= 0x80 {
		b = append(b, byte(v)|0x80)
		v >>= 7
	}
	return append(b, byte(v))
}

// VaruintSize returns the encoded size of v as unsigned LEB128.
func VaruintSize(v uint64) int {
	n := 1
	for v >= 0x80 {
		v >>= 7
		n++
	}
	return n
}

// ReserveLengthPrefix appends a one-byte placeholder for the varuint byte
// length of an element appended next, and returns its position. Call
// FinishLengthPrefix once the element has been appended.
func ReserveLengthPrefix(b []byte) ([]byte, int) {
	return append(b, 0), len(b)
}

// FinishLengthPrefix writes the varuint length of the element appended after
// the placeholder at pos, widening the prefix (and shifting the element) when
// the length needs more than one byte.
func FinishLengthPrefix(b []byte, pos int) []byte {
	n := len(b) - pos - 1
	extra := VaruintSize(uint64(n)) - 1
	if extra > 0 {
		b = append(b, make([]byte, extra)...)
		copy(b[pos+1+extra:], b[pos+1:pos+1+n])
	}
	AppendVaruint(b[pos:pos], uint64(n))
	return b
}

// CheckCount rejects an array with more than max elements.
func CheckCount(n int, max uint32) error {
	if uint64(n) > uint64(max) {
		return encodeError("array length %d exceeds max_elements=%d", n, max)
	}
	return nil
}

// CheckString rejects a string longer than max bytes or not valid UTF-8.
func CheckString(s string, max uint32) error {
	if uint64(len(s)) > uint64(max) {
		return encodeError("string length %d exceeds max_bytes=%d", len(s), max)
	}
	if !utf8.ValidString(s) {
		return encodeError("string is not valid UTF-8")
	}
	return nil
}

// CheckBytes rejects a byte string longer than max bytes.
func CheckBytes(v []byte, max uint32) error {
	if uint64(len(v)) > uint64(max) {
		return encodeError("bytes length %d exceeds max_bytes=%d", len(v), max)
	}
	return nil
}

// InvalidEnum reports an enum value the schema does not declare.
func InvalidEnum(typeName string, value uint64) error {
	return encodeError("%d is not a valid %s value", value, typeName)
}

// WrongSize reports a nested fixed-size record that did not encode to its
// schema-determined slot size (an internal consistency failure).
func WrongSize(got, want int) error {
	return encodeError("fixed-size nested record encoded to %d bytes, expected %d", got, want)
}
