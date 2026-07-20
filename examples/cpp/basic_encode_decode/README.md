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
