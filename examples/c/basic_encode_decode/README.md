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

Only `main.c` is compiled, as C99 with extensions disabled. The generated
header contains the data types, constant descriptors, and inline type-specific
wrappers. Link the installed `Quarry::runtime_c` target for the shared codec;
there is no schema-specific implementation to compile and no heap allocation.
The compiler also emits an include-only `.c` compatibility file, which this
example does not compile.

A complete [generated header snapshot](generated-header/demo/c.generated.h)
is checked in for inspection. It is unmodified output for [schema.brd](schema.brd),
using C generated-code API epoch 4. It contains the structs, layout/member
descriptors, and inline sizing/encode/decode wrappers. The CMake example
regenerates its own header in the build directory rather than compiling against
this snapshot.

To refresh the snapshot after changing the schema or generator, run from the
repository root:

```sh
quarry-schema-compiler --language c --output-directory generated \
  examples/c/basic_encode_decode/schema.brd
cp generated/demo/c.generated.h \
  examples/c/basic_encode_decode/generated-header/demo/c.generated.h
```
