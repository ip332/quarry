"""Cloud service: decodes an engine ECU report with Python bindings generated
from the same truck configuration root as the ECU firmware."""

import sys

from truck.engine.schema import EngineStatus


with open(sys.argv[1], "rb") as report:
    data = report.read()

status = EngineStatus.decode(data)  # validates the configuration's record IDs
if status.rpm != 1450 or status.coolant_c != 88 or len(status.faults) != 1:
    raise SystemExit(f"unexpected EngineStatus: {status}")
fault = status.faults[0]
if fault.spn != 110 or fault.fmi != 16 or fault.time.unix_ms != 1767225599000:
    raise SystemExit(f"unexpected FaultCode: {fault}")

print(f"record_id 0x{int.from_bytes(data[4:8], 'big'):08x}: rpm={status.rpm} "
      f"coolant={status.coolant_c}C faults={[(f.spn, f.fmi) for f in status.faults]}")
