# Basic Encode/Decode

This example consumes the installed Quarry runtime package from CMake.

Build after installing Quarry to a prefix:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/install/prefix
cmake --build build
./build/quarry_basic_encode_decode
```

The example uses `find_package(Quarry CONFIG REQUIRED)`, links
`Quarry::runtime`, includes `<quarry/runtime/binary_record.hpp>`, and
performs a small BRF encode/decode round trip.

This example calls the runtime's low-level primitives directly (no schema),
so decode failures here only carry a compact error enum, not a `path` —
`path` is populated by generated code, which tracks its own `field_index` as
it decodes. For a demonstration of the full structured decode-failure API
(`.error`, `.path`, `.byte_offset` on a truncated and a corrupted payload),
see `examples/cpp/schema_compiler_cmake`.
