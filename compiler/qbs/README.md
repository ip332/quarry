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
