import sys

from demo.interop.schema import Sample


with open(sys.argv[1], "rb") as encoded_file:
    decoded = Sample.decode(encoded_file.read())

if decoded.count != 42 or decoded.label != "hello":
    raise SystemExit("Python decoded value does not match the C++ value")

print(f"decoded count: {decoded.count}")
