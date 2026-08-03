# Basic Encode/Decode in C

This example generates strict-C99 code, initializes caller-owned storage,
encodes a record, decodes it, and checks the decoded scalar, string, and bytes
fields.

After installing Quarry, build and run it from this directory:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/quarry/install
cmake --build build
./build/quarry_basic_encode_decode_c
```

The generated source is compiled as C99 with extensions disabled and links the
installed `Quarry::runtime_c` target. No heap allocation is required.
