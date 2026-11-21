"""Explicit public truck contract; no compiler internals or identity algorithm.

Fields are listed in source order so generic QBS numeric access works in minimal
images too. Distinct array elements detect dropped/reordered nested records.
"""
RECORDS = [
    ('truck.common.Timestamp', 0x40001, [('unix_ms', 'u64', 42)]),
    ('truck.engine.FaultCode', 0x50002, [('time', 'truck.common.Timestamp', None),
                                     ('spn', 'u32', 1234), ('fmi', 'u8', 7)]),
    ('truck.engine.EngineStatus', 0x50001, [('time', 'truck.common.Timestamp', None),
        ('rpm', 'u16', 1450), ('coolant_c', 'i16', -12),
        ('faults', 'truck.engine.FaultCode[]', None)]),
    ('truck.transmission.GearState', 0x60001, [('time', 'truck.common.Timestamp', None),
        ('gear', 'i8', -2), ('engine', 'truck.engine.EngineStatus', None)]),
    ('truck.brakes.AbsEvent', 0x20001, [('time', 'truck.common.Timestamp', None), ('wheel', 'u8', 3)]),
    ('truck.brakes.BrakeStatus', 0x20002, [('time', 'truck.common.Timestamp', None),
        ('air_pressure_kpa', 'u16', 800), ('last_abs_event', 'truck.brakes.AbsEvent', None)]),
    ('truck.cms.CameraHealth', 0x30001, [('time', 'truck.common.Timestamp', None),
        ('camera', 'string', 'front'), ('ok', 'bool', True)]),
    ('truck.VehicleSnapshot', 0x10001, [('time', 'truck.common.Timestamp', None),
        ('engine', 'truck.engine.EngineStatus', None), ('gear', 'truck.transmission.GearState', None),
        ('brakes', 'truck.brakes.BrakeStatus', None), ('cameras', 'truck.cms.CameraHealth[]', None)]),
]
RECORDS.append(('matrix.EnumSample', 0x10001, [('mode', 'enum', 3)]))
BY_NAME = {name: (identity, fields) for name, identity, fields in RECORDS}
SCHEMA_ID = '9200ca494f010801b830d0857cf66dde'


def values(name, seed=0):
    if seed < 0:
        return {}
    result = {}
    for field, kind, value in BY_NAME[name][1]:
        if kind.endswith('[]'):
            value = [values(kind[:-2], seed), values(kind[:-2], seed + 1)]
        elif kind in BY_NAME:
            value = values(kind, seed)
        elif kind == 'string':
            value += str(seed)
        elif kind not in ('bool', 'enum'):
            value += seed
        result[field] = value
    return result
