# Protobuf descriptor set to BRD

This example demonstrates the translator workflow without adding protobuf
support to the Quarry compiler:

```text
device.proto + common.proto
        │ protoc --descriptor_set_out --include_imports
        ▼
schema.pb
        │ quarry-protobuf-translator --options bounds.yaml
        ▼
generated-brd/ (one .brd unit per reachable message)
        │ quarry-schema-compiler (one explicit root at a time)
        ▼
generated-cpp/
```

Build the two tools from the Quarry source tree first:

```sh
cmake --preset debug
cmake --build --preset debug --target quarry-protobuf-translator quarry-schema-compiler
```

From this directory, set paths to those build-tree tools and run:

```sh
TRANSLATOR=/path/to/quarry/build/debug/tools/schema-translators/protobuf/quarry-protobuf-translator
COMPILER=/path/to/quarry/build/debug/tools/quarry-schema-compiler

mkdir -p build
protoc --descriptor_set_out=build/schema.pb \
  --include_imports --include_source_info --proto_path=. device.proto

"$TRANSLATOR" --descriptor-set build/schema.pb --root demo.Device \
  --options bounds.yaml --output-dir build/generated-brd

"$COMPILER" --language cpp --output-directory build/generated-cpp \
  build/generated-brd/demo/common/reading/reading.brd
"$COMPILER" --language cpp --output-directory build/generated-cpp \
  build/generated-brd/demo/device/device.brd
```

The translator emits `manifest.json` with the complete declaration mapping and
the explicit roots. The dependency root is generated separately before the
root record, and both compiler invocations share `build/generated-cpp`.

This example uses a bounded string and a nested imported record. It does not
claim protobuf wire compatibility: the translator maps logical protobuf
declarations into ordinary Quarry BRD source, which is then compiled into BRF
code by Quarry.
