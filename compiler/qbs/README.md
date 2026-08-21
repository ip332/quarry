# QBS model

The QBS model is the compiler-owned normalized representation immediately
before future binary serialization:

```text
Schema IR + canonical BRF Layout IR
                 |
          QbsModelBuilder
                 |
          QbsImageModel
                 |
          future QBS serializer
```

`QbsModelBuilder` consumes the canonical `LayoutModel` for every BRF physical
location, slot size, presence bit, storage classification, and encoded width.
It does not allocate or rewrite `record_id`; stable persistent allocation is a
future compiler prerequisite for schema exchange.

Minimal and reflective construction modes share one model. Reflective names
are deduplicated and sorted by UTF-8 byte order; they are excluded from the
canonical schema identity input.

This layer does not serialize `.qbs` bytes, parse external QBS images, or
implement a generic runtime.

QBS images include a mandatory Identity String Section containing the canonical
record and enum FQNs. It is structural metadata and is separate from the
optional reflective string section; its global 1/2/4-byte offset width is
selected canonically from the ISS payload size.
# QBS parser

Externally supplied QBS is treated as untrusted input:

```text
untrusted QBS bytes -> parse_qbs -> structural validation
                    -> canonical validation -> schema-ID verification
                    -> ValidatedQbsView
```

The parser is bytes-only and does not rebuild compiler state. The mandatory
Identity String Section (ISS) is scanned before descriptor references are
resolved; record and enum identity offsets must point to exact string starts.
Identity references use the image-wide canonical 1-, 2-, or 4-byte width, and
record/enum descriptor strides are derived from that width.

Canonical type identities are reconstructed with an explicit work stack so
attacker-controlled nesting cannot consume the native call stack. Parser
resource limits bound image size, table counts, strings, and validation work.
The resulting `ValidatedQbsView` borrows the input span and must not outlive it.
