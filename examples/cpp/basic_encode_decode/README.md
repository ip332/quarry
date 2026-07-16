# Basic Encode/Decode

This example consumes the installed Breadcrumbs runtime package from CMake.

Build after installing Breadcrumbs to a prefix:

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/install/prefix
cmake --build build
./build/breadcrumbs_basic_encode_decode
```

The example uses `find_package(Breadcrumbs CONFIG REQUIRED)`, links
`Breadcrumbs::runtime`, includes `<breadcrumbs/runtime/binary_record.hpp>`, and
performs a small BRF encode/decode round trip.
