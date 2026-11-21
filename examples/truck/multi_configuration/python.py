"""Both prefixed Python packages coexist in one process; selection is out of band."""
from truck.common.schema import Timestamp as Time0
from truck.engine.schema import EngineStatus as Engine0
from rev_a.truck.schema import VehicleSnapshot as SnapshotA
from rev_b.truck.schema import VehicleSnapshot as SnapshotB
from rev_a.truck.common.schema import Timestamp as TimeA
from rev_b.truck.common.schema import Timestamp as TimeB
from rev_a.truck.engine.schema import EngineStatus as EngineA, QUARRY_SCHEMA_ID as ID_A
from rev_b.truck.engine.schema import EngineStatus as EngineB, QUARRY_SCHEMA_ID as ID_B
from rev_b.truck.cms.schema import MirrorFold

assert SnapshotA.__module__ != SnapshotB.__module__
assert ID_A.hex() == '9200ca494f010801b830d0857cf66dde' and ID_A != ID_B
a = EngineA(time=TimeA(unix_ms=42), rpm=1450)
b = EngineB(time=TimeB(unix_ms=42), rpm=1450)
wa, wb = a.encode(), b.encode()
assert wa == wb == Engine0(time=Time0(unix_ms=42), rpm=1450).encode() and int.from_bytes(wa[4:8], 'big') == 0x00050001
assert EngineA.decode(wa).rpm == EngineB.decode(wb).rpm == 1450
assert EngineA.decode(wa).time.unix_ms == EngineB.decode(wb).time.unix_ms == 42
mirror = MirrorFold(folded=True).encode()
assert int.from_bytes(mirror[4:8], 'big') == 0x00030002
assert MirrorFold.decode(mirror).folded is True
print('Python: both packages round-trip; schema identities differ, EngineStatus bytes agree')
