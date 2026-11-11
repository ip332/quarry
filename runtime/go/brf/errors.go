package brf

import (
	"fmt"
	"strings"
)

// DecodeError reports why bytes are not a valid record for the expected
// schema. Offset is the byte offset of the problem relative to the start of
// the record (or array payload) being decoded when the error was created.
type DecodeError struct {
	Offset int
	Reason string
}

func (e *DecodeError) Error() string {
	return fmt.Sprintf("brf: decode: %s at byte offset %d", e.Reason, e.Offset)
}

// EncodeError reports why a value cannot be encoded.
type EncodeError struct {
	Reason string
}

func (e *EncodeError) Error() string { return "brf: encode: " + e.Reason }

// FieldError attributes an encode or decode error to a field. Nested record
// and array element errors accumulate a dotted path, for example
// "children[2].label". Use errors.As to reach the underlying DecodeError or
// EncodeError.
type FieldError struct {
	Path string
	Err  error
}

func (e *FieldError) Error() string { return e.Path + ": " + e.Err.Error() }

// Unwrap returns the underlying error.
func (e *FieldError) Unwrap() error { return e.Err }

// WrapField attributes err to the named field, extending an existing path.
// It returns nil when err is nil.
func WrapField(name string, err error) error {
	if err == nil {
		return nil
	}
	if field, ok := err.(*FieldError); ok {
		path := name
		if !strings.HasPrefix(field.Path, "[") {
			path += "."
		}
		return &FieldError{Path: path + field.Path, Err: field.Err}
	}
	return &FieldError{Path: name, Err: err}
}

// WrapElement attributes err to an array element of the named field.
func WrapElement(name string, index int, err error) error {
	if err == nil {
		return nil
	}
	return WrapField(name, WrapField(fmt.Sprintf("[%d]", index), err))
}

func decodeError(offset int, format string, args ...any) error {
	return &DecodeError{Offset: offset, Reason: fmt.Sprintf(format, args...)}
}

func encodeError(format string, args ...any) error {
	return &EncodeError{Reason: fmt.Sprintf(format, args...)}
}
