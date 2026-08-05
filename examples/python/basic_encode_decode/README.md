# Basic Encode/Decode in Python

This example uses the installed pure-Python runtime and compiler-generated
dataclasses.

From the repository root, create a clean environment, build the runtime wheel,
and install the wheel produced in a dedicated distribution directory:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install 'build>=1.2'
python -m build --wheel --outdir build/python-dist runtime/python
python -m pip install build/python-dist/*.whl
quarry-schema-compiler --language python \
  --output-directory build/python-generated \
  examples/python/basic_encode_decode/schema.brd
PYTHONPATH=build/python-generated \
  python examples/python/basic_encode_decode/main.py
```

The compiler must be the installed Quarry compiler. The output package and the
runtime package are separate: generated files are downstream-owned, while the
runtime is installed in the virtual environment. The `*.whl` glob avoids
hard-coding the Git-derived Quarry version; use a dedicated output directory
so it contains only the wheel for this build.
