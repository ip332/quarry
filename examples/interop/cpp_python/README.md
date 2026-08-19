# C++ BRF v2 encode/decode

This focused example generates a C++ BRF v2 encoder and decoder for the same
schema. Python remains on the BRF v1 compatibility path during the staged
backend migration, so this example no longer claims C++/Python wire
interoperability.

Build the C++ producer after installing Quarry:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/quarry/install
cmake --build build
./build/quarry_cpp_python_encode encoded.brf
```

Run the generated C++ decoder against the encoded record:

```sh
./build/quarry_cpp_python_encode --decode encoded.brf
```
