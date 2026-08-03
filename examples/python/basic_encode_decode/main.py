from demo.python.schema import Sample


sample = Sample(count=42, label="hello")
decoded = Sample.decode(sample.encode())
if decoded != sample:
    raise SystemExit("decoded value does not match the original")

print(f"decoded count: {decoded.count}")
