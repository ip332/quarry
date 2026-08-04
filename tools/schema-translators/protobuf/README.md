# Protobuf descriptor-set loader

`quarry-protobuf-translator` is an isolated migration and evaluation tool for
Protocol Buffers. This first implementation loads and deterministically lists
protobuf `FileDescriptorSet` contents. It does not generate BRD files yet.

Generate the input with `protoc`, including imported descriptors:

```sh
protoc \
  --descriptor_set_out=schema.pb \
  --include_imports \
  --include_source_info \
  --proto_path=path/to/protos \
  path/to/protos/root.proto
```

Inspect it with:

```sh
quarry-protobuf-translator --descriptor-set schema.pb --list
```

The listing is deterministic and includes files, packages, imports, messages,
fields, nested declarations, enums, scalar descriptor types, labels, and
protobuf field numbers. The tool does not invoke `protoc` automatically.

BRD emission, bounds configuration, protobuf type mapping, field-number
compatibility policy, and benchmark integration are planned for later PRs.
