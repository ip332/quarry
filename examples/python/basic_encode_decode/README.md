# Basic Encode/Decode in Python

This example uses the installed pure-Python runtime and compiler-generated
dataclasses.

From this directory, create a clean environment with the runtime installed,
then generate the package and run the consumer:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install /path/to/quarry-runtime-python-0.1.0-py3-none-any.whl
quarry-schema-compiler --language python \
  --output-directory generated schema.brd
PYTHONPATH=generated python main.py
```

The compiler must be the installed Quarry compiler. The output package and the
runtime package are separate: generated files are downstream-owned, while the
runtime is installed in the virtual environment.
