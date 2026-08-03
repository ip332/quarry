# C++ to Python Interoperability

This focused example generates the same schema for C++ and Python. The C++
consumer writes BRF bytes; the Python consumer reads and decodes those bytes
through the installed Python runtime.

Build the C++ producer after installing Quarry:

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/quarry/install
cmake --build build
./build/quarry_cpp_python_encode encoded.brf
```

Generate Python code into the same example build tree and run the decoder with
the installed runtime:

```sh
quarry-schema-compiler --language python \
  --output-directory build/python schema.brd
```

```sh
PYTHONPATH=build/python python decode.py encoded.brf
```

The two backends use the existing BRF contract; no adapter or special wire
handling is involved.
