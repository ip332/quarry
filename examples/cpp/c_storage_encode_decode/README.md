# C++ interface over C storage

Generate fixed-capacity C storage and C++20 wrappers backed by the shared C codec:

```sh
quarry-schema-compiler --language cpp --cpp-storage c --output-directory generated \
  examples/c/basic_encode_decode/schema.brd
```

The generated `.generated.hpp` contains the C struct, descriptors, and a class
in `quarry::cxx`, named from the C symbol (`demo_c_Sample`). The `.generated.cpp`
is an include-only compatibility stub; it need not be compiled. The class has
exactly the C struct's size and uses no heap. The default `--cpp-storage owning`
continues to generate the existing dynamic-container C++ API.

Build this example against an installation containing the new compiler:

```sh
cmake -S examples/cpp/c_storage_encode_decode -B /tmp/quarry-cpp-c-storage \
  -DCMAKE_PREFIX_PATH=/path/to/quarry/install
cmake --build /tmp/quarry-cpp-c-storage
/tmp/quarry-cpp-c-storage/quarry_cpp_c_storage_example
```

Add `-DQUARRY_EXAMPLE_COMPACT_PRESENCE=ON` for packed bits. Generate all imported
schemas with the same storage/presence options. Link `Quarry::runtime_c`.
This option is currently selected through the compiler CLI/custom commands,
not through the installed `quarry_generate()` CMake helper.

- Scalar/enum/record setters mark presence; `clear_<field>()` clears it.
- String/byte/array setters return false if capacity is exceeded, leaving the
  old value unchanged. Strings are NUL-terminated, but their byte length remains
  authoritative. Empty values become absent. Encode validates UTF-8/enums and
  nested element bounds; setters check container capacity only.
- String/byte/array getters return non-owning const views. Nested records and
  record/string/bytes array elements use their native C types; construct nested
  values with another wrapper and pass its `native()` value to the setter.
- `native()` provides mutable/const C storage access. Keep native lengths/counts
  within their declared capacities before requesting views.
- `encoded_size()` returns status and size. `encode(span<byte>)` returns status
  and bytes written. Neither allocates output storage.
- `decode(span<const byte>)` writes into this object's storage and returns the
  runtime status/error offset. On failure, contents may be cleared or partially
  populated: discard them. Static encode/decode overloads accept native storage
  references without converting or copying a whole record.

Changing from owning to C storage changes the C++ API and native layout, while
preserving BRF v2 wire compatibility. Field/class spellings are escaped where
needed to avoid C/C++ keywords and wrapper-method collisions; inspect the header
when using unusual schema names.

Encoding validates fields as it writes them. On any encode error, discard or
clear the affected output span; `bytes_written` reports its extent even on
failure, including partial nested writes. It is not a valid record length on error. The codec does not roll back failed writes. `encoded_size()` remains
available for a separate validating size calculation.
