package brf

import (
	"bytes"
	"errors"
	"math"
	"strings"
	"testing"
)

func TestVaruintRoundTripAndBounds(t *testing.T) {
	for _, v := range []uint64{0, 1, 127, 128, 255, 16383, 16384, math.MaxUint32, math.MaxUint64} {
		b := AppendVaruint(nil, v)
		if len(b) != VaruintSize(v) {
			t.Fatalf("size of %d: got %d want %d", v, len(b), VaruintSize(v))
		}
		got, off, err := ReadVaruint(b, 0)
		if err != nil || got != v || off != len(b) {
			t.Fatalf("round trip %d: got %d off %d err %v", v, got, off, err)
		}
	}
	if got := AppendVaruint(nil, 300); !bytes.Equal(got, []byte{0xAC, 0x02}) {
		t.Fatalf("LEB128 of 300: % x", got)
	}
	for name, input := range map[string][]byte{
		"truncated":        {0x80},
		"empty":            {},
		"overflow 64 bits": {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02},
		"eleven bytes":     {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00},
	} {
		if _, _, err := ReadVaruint(input, 0); err == nil {
			t.Errorf("%s: accepted % x", name, input)
		}
	}
	max := []byte{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01}
	if v, _, err := ReadVaruint(max, 0); err != nil || v != math.MaxUint64 {
		t.Fatalf("max uint64: %d %v", v, err)
	}
}

func TestLengthPrefixWidensAndShiftsElement(t *testing.T) {
	for _, n := range []int{0, 1, 127, 128, 300, 20000} {
		b := []byte{0xEE}
		b, pos := ReserveLengthPrefix(b)
		element := bytes.Repeat([]byte{0x5A}, n)
		b = append(b, element...)
		b = FinishLengthPrefix(b, pos)
		want := append(AppendVaruint([]byte{0xEE}, uint64(n)), element...)
		if !bytes.Equal(b, want) {
			t.Fatalf("n=%d: prefix not widened correctly", n)
		}
	}
}

func record(t *testing.T, recordID, fixed uint32, build func(b []byte, base int) []byte) []byte {
	t.Helper()
	b := BeginRecord(nil, recordID, fixed)
	b = build(b, 0)
	b, err := FinishRecord(b, 0)
	if err != nil {
		t.Fatal(err)
	}
	return b
}

// One u32 at 17 (bit 0) and one string descriptor at 21 (bit 1); bitmap 1 byte.
func sample(t *testing.T, value uint32, text string) []byte {
	return record(t, 7, 13, func(b []byte, base int) []byte {
		PutUint32(b[base+17:], value)
		SetPresent(b[base:], 0)
		if text != "" {
			start := len(b)
			b = append(b, text...)
			if err := EndVariable(b, base, 21, start, 1); err != nil {
				t.Fatal(err)
			}
		}
		return b
	})
}

func TestEncodeLayoutMatchesSpecification(t *testing.T) {
	got := sample(t, 0x01020304, "hi")
	want := []byte{
		2, 0, 0, 16, 0, 0, 0, 7, 0, 0, 0, 13, 0, 0, 0, 31, // header
		0x03,       // presence bits 0 and 1
		1, 2, 3, 4, // u32
		0, 0, 0, 29, 0, 0, 0, 2, // descriptor: absolute offset 29, length 2
		'h', 'i',
	}
	if !bytes.Equal(got, want) {
		t.Fatalf("got  % x\nwant % x", got, want)
	}
}

func TestReaderAcceptsValidRecord(t *testing.T) {
	data := sample(t, 9, "hi")
	r, err := NewReader(data, 7, 13, 1, 2)
	if err != nil {
		t.Fatal(err)
	}
	if !r.Present(0) || r.Uint32(17) != 9 {
		t.Fatal("scalar slot")
	}
	text, err := r.Variable(21)
	if err != nil || string(text) != "hi" {
		t.Fatalf("variable: %q %v", text, err)
	}
	if err := r.Finish(); err != nil {
		t.Fatal(err)
	}
}

func TestReaderRejectsMalformedRecords(t *testing.T) {
	valid := sample(t, 9, "hi")
	mutate := func(at int, v byte) []byte {
		b := append([]byte(nil), valid...)
		b[at] = v
		return b
	}
	cases := map[string]struct {
		data   []byte
		offset int
	}{
		"truncated header": {valid[:15], 0},
		"version":          {mutate(0, 1), 0},
		"flags":            {mutate(1, 1), 1},
		"header size":      {mutate(3, 17), 2},
		"record id":        {mutate(7, 8), 4},
		"fixed size":       {mutate(11, 12), 8},
		"record size":      {mutate(15, 30), 12},
		"trailing byte":    {append(append([]byte(nil), valid...), 0), 12},
		"unused bit":       {mutate(16, 0x07), 16},
	}
	for name, c := range cases {
		_, err := NewReader(c.data, 7, 13, 1, 2)
		var de *DecodeError
		if !errors.As(err, &de) {
			t.Errorf("%s: expected DecodeError, got %v", name, err)
			continue
		}
		if de.Offset != c.offset {
			t.Errorf("%s: offset %d, want %d", name, de.Offset, c.offset)
		}
	}
}

func TestPeekRecordID(t *testing.T) {
	data := sample(t, 9, "hi")
	if id, err := PeekRecordID(data); err != nil || id != 7 {
		t.Fatalf("peek: %d %v", id, err)
	}
	if _, err := PeekRecordID(data[:8]); err == nil {
		t.Fatal("accepted a truncated header")
	}
	data[0] = 1
	if _, err := PeekRecordID(data); err == nil {
		t.Fatal("accepted a BRF v1 header")
	}
}

func TestReaderValidatesAbsentSlotsAndVariableRegion(t *testing.T) {
	data := sample(t, 9, "")
	data[22] = 1 // absent descriptor byte
	r, err := NewReader(data, 7, 13, 1, 2)
	if err != nil {
		t.Fatal(err)
	}
	var de *DecodeError
	if err := r.Absent(21, 8); !errors.As(err, &de) || de.Offset != 22 {
		t.Fatalf("absent nonzero: %v", err)
	}

	gap := sample(t, 9, "hi")
	gap[24]++ // descriptor offset 30 instead of 29: not contiguous
	r, _ = NewReader(gap, 7, 13, 1, 2)
	if _, err := r.Variable(21); err == nil {
		t.Fatal("accepted a gap before a variable object")
	}

	long := sample(t, 9, "hi")
	long[28] = 3 // length 3 > remaining
	r, _ = NewReader(long, 7, 13, 1, 2)
	if _, err := r.Variable(21); err == nil {
		t.Fatal("accepted a variable object past the record end")
	}

	r, _ = NewReader(sample(t, 9, "hi"), 7, 13, 1, 2)
	if err := r.Finish(); err == nil {
		t.Fatal("accepted an unread variable object as trailing bytes")
	}
}

func TestStringBytesAndBoolValidation(t *testing.T) {
	if _, err := String([]byte("héllo"), 6); err != nil {
		t.Fatal(err)
	}
	for name, p := range map[string][]byte{
		"overlong":      {0xC0, 0x80},
		"surrogate":     {0xED, 0xA0, 0x80},
		"truncated":     {0xE2, 0x82},
		"lone continue": {0x80},
	} {
		if _, err := String(p, 8); err == nil {
			t.Errorf("%s: accepted invalid UTF-8", name)
		}
	}
	if _, err := String([]byte("abc"), 2); err == nil {
		t.Error("accepted a string over max_bytes")
	}
	if err := CheckString("\xff", 4); err == nil {
		t.Error("encode accepted invalid UTF-8")
	}
	if err := CheckString("a\x00b", 3); err != nil {
		t.Errorf("embedded NUL is valid: %v", err)
	}
	if b, err := Bytes([]byte{1, 2}, 2); err != nil || !bytes.Equal(b, []byte{1, 2}) {
		t.Errorf("bytes: %v %v", b, err)
	}
	if _, err := Bytes([]byte{1, 2, 3}, 2); err == nil {
		t.Error("accepted bytes over max_bytes")
	}
	for v, want := range map[byte]bool{0: false, 1: true} {
		if got, err := Bool([]byte{v}); err != nil || got != want {
			t.Errorf("bool %d", v)
		}
	}
	if _, err := Bool([]byte{2}); err == nil {
		t.Error("accepted bool 2")
	}
}

func TestArrayFraming(t *testing.T) {
	payload := AppendVaruint(nil, 2)
	payload = AppendUint16(payload, 0x0102)
	payload = AppendUint16(payload, 0x0304)
	a, n, err := NewArray(payload, 4)
	if err != nil || n != 2 {
		t.Fatalf("count: %d %v", n, err)
	}
	if err := a.ExpectFixed(n, 2); err != nil {
		t.Fatal(err)
	}
	for _, want := range []uint16{0x0102, 0x0304} {
		p, err := a.Fixed(2)
		if err != nil || Uint16(p) != want {
			t.Fatalf("element: %v", err)
		}
	}
	if err := a.Finish(); err != nil {
		t.Fatal(err)
	}
	if _, _, err := NewArray(payload, 1); err == nil {
		t.Error("accepted count over max_elements")
	}
	a, n, _ = NewArray(payload[:len(payload)-1], 4)
	if err := a.ExpectFixed(n, 2); err == nil {
		t.Error("accepted a truncated fixed array")
	}

	prefixed := AppendVaruint(nil, 1)
	prefixed = AppendVaruint(prefixed, 3)
	prefixed = append(prefixed, "abcX"...)
	a, _, _ = NewArray(prefixed, 1)
	if p, err := a.Prefixed(); err != nil || string(p) != "abc" {
		t.Fatalf("prefixed: %q %v", p, err)
	}
	if err := a.Finish(); err == nil {
		t.Error("accepted trailing bytes after the last element")
	}
	a, _, _ = NewArray([]byte{1, 5, 'a'}, 1)
	if _, err := a.Prefixed(); err == nil {
		t.Error("accepted an element length past the payload")
	}
}

func TestFloatBitsArePreserved(t *testing.T) {
	nan32 := math.Float32frombits(0x7FC12345)
	p := make([]byte, 4)
	PutFloat32(p, nan32)
	if math.Float32bits(Float32(p)) != 0x7FC12345 {
		t.Fatal("float32 NaN payload changed")
	}
	negZero := math.Copysign(0, -1)
	q := AppendFloat64(nil, negZero)
	if math.Float64bits(Float64(q)) != 0x8000000000000000 {
		t.Fatal("float64 negative zero changed")
	}
}

func TestFieldErrorPaths(t *testing.T) {
	base := &DecodeError{Offset: 3, Reason: "bad"}
	err := WrapField("outer", WrapElement("children", 2, WrapField("label", base)))
	if !strings.HasPrefix(err.Error(), "outer.children[2].label: ") {
		t.Fatalf("path: %s", err)
	}
	var de *DecodeError
	if !errors.As(err, &de) || de != base {
		t.Fatal("underlying DecodeError not reachable")
	}
	if WrapField("x", nil) != nil || WrapElement("x", 0, nil) != nil {
		t.Fatal("nil error must stay nil")
	}
}

func TestPtr(t *testing.T) {
	p := Ptr(uint32(5))
	if *p != 5 {
		t.Fatal("Ptr")
	}
}
