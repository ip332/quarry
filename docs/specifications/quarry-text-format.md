# Quarry Text Format v1

Quarry Text Format v1 (QTF v1) is a canonical, human-readable representation
of one validated BRF record. QTF is inspired by Protocol Buffers Text Format,
but is not protobuf TextFormat and has no protobuf wire or parser
compatibility.

QTF represents values; QBS remains authoritative for the record identity,
field types, widths, enum membership, and structure. QTF is not
self-describing: the same token such as `42` may be a signed integer,
unsigned integer, enum, or another QBS-supported type depending on the QBS
field.

## Canonical syntax

The root is an unnamed record body:

```qtf
{
  field: value
}
```

Canonical output is UTF-8, uses `\n` line endings, two spaces per structural
level, one present field per line in QBS field order, no trailing spaces, and
ends with a newline. The root braces occupy their own lines. Fields are not
alphabetically sorted.

For reflective QBS, a field's QBS name is emitted. For minimal QBS, the stable
field index within its owning record is emitted as `@N`, for example `@0`.
Field omission is not permitted for present values, and absent optional fields
are omitted entirely. `{}` therefore represents a record with no emitted
present fields. A present empty value is still emitted.

The v1 scalar forms are:

## Lexical grammar

The future v1 parser accepts space, horizontal tab, carriage return, and
newline as whitespace between tokens. Canonical export uses only newline for
line endings and does not emit tabs or carriage returns except inside escaped
strings. A carriage-return/line-feed pair is one line ending when parsing.

The punctuation tokens are `{`, `}`, `[`, `]`, `:`, and `,`. Reflective field
identifiers use `[A-Za-z_][A-Za-z0-9_]*`; QBS field names are constrained to
this identifier form. Minimal identifiers use `@` followed by one or more
decimal digits, with no sign or leading zeroes except `@0`. The digits denote
the exact stable QBS field index in the owning record and must name an
existing field.

Integer tokens are `0`, a nonzero decimal digit followed by decimal digits,
or `-` followed by one of those forms. A leading `+`, hexadecimal, octal,
binary, and unnecessary leading zeroes are not canonical. The QBS type
controls signedness and range checking; enum tokens use this same decimal
grammar and must be members of the QBS enum.

Finite float tokens use the locale-independent shortest round-trip decimal
form produced by `std::to_chars` with general formatting. `nan`, `inf`, and
`-inf` are reserved float tokens. The exporter preserves negative zero as
`-0`; NaN payload and sign bits are not represented by QTF v1.

Quoted strings contain UTF-8 and the escapes listed below. A bytes token is
`hex"` followed by zero or more pairs of hexadecimal digits and a closing
`"`; parsers should accept either hex case, while canonical output is
lowercase. Odd-length byte tokens are invalid.

Records contain fields separated by whitespace; commas are not allowed
between record fields. Primitive arrays and record arrays require commas
between elements. Trailing commas are invalid in all arrays and are never
canonical. Empty arrays use `[]`. Unknown fields and duplicate fields are
parser errors. A parser must use the selected QBS record to resolve every
field and reject a field whose identifier is not present in that record.

Comments are not part of QTF v1 and are invalid input; this keeps the lexer
small and the canonical grammar unambiguous.

The grammar is intentionally schema-driven. In compact EBNF, the structural
forms are:

```text
record       = "{" , { field } , "}" ;
field        = identifier , ":" , value ;
value        = scalar | record | array ;
array        = "[" , [ value , { "," , value } ] , "]" ;
identifier   = name | "@" , decimal-index ;
```

The lexer must distinguish `-inf`/`nan` from numeric tokens using the selected
QBS float type. A string or bytes literal ends only at its unescaped closing
quote. Whitespace is never significant inside a quoted string or bytes
literal.

| QBS value | Canonical QTF |
|---|---|
| boolean | `true` or `false` |
| signed/unsigned integer | decimal, without `+` or unnecessary zeroes |
| enum | its exact decimal numeric value |
| finite float | shortest `std::to_chars` round-trip spelling |
| NaN/infinity | `nan`, `inf`, or `-inf` |
| string | quoted UTF-8 |
| bytes | `hex"` followed by lowercase hexadecimal and `"` |

Floating-point special values are represented because the generic BRF encoder
and reader preserve them. A future parser must map them to the corresponding
QBS float type. Negative zero is emitted with its sign when the conversion
library preserves that spelling.

Strings escape `"`, `\\`, newline, carriage return, and tab as `\"`, `\\`,
`\n`, `\r`, and `\t`. Other ASCII control bytes use `\u00hh`. Valid UTF-8
non-ASCII bytes remain readable UTF-8. Bytes are never emitted as strings;
empty bytes are `hex""`.

QBS currently exposes enum numeric values but not reliable symbolic value
names in `QbsEnumView`, so v1 emits enum numbers for both reflective and
minimal QBS.

## Arrays and records

Arrays use QTF's deliberate bracket extension, not repeated-field protobuf
TextFormat syntax:

```qtf
samples: [10, 20, 30]
items: [
  {
    @0: 1
  },
  {
    @0: 2
  }
]
```

Primitive arrays are emitted on one line, including `[]`. Record arrays use
one object per element, preserve logical element order, and use commas between
objects. Nested records use a field followed by a multi-line record body.
Absent arrays and records are omitted; present-empty arrays are emitted as
`[]`.

A present empty nested record is emitted as:

```qtf
child: {
  }
```

The closing brace is indented at the nested record's structural depth. An
empty root record is exactly `{\n}\n`.

The exporter emits no comments. Comment syntax is reserved for a future
parser and is not part of QTF v1 canonical output.

## Canonical exporter

`export_qtf(const ValidatedBrfRecordView&, BrfTraversalLimits)` consumes only
the validated structured reader and `traverse_brf()`. It does not parse BRF,
inspect descriptors, or validate again. The result is atomic: a successful
call returns complete text; a traversal or value failure returns no text.
Traversal work and depth limits apply to export. Future import support must
reject duplicate fields and use QBS to interpret every value token.
