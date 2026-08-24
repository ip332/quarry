import struct
import unittest

from quarry.runtime.generic import QbsField, QbsRecord, QbsSchema, QbsType, TypeCode, validate_brf


def _schema():
    schema = QbsSchema(b"", [], [], [], [], False)
    u32 = QbsType(TypeCode.U32, True, 4, 0, 0, 0)
    child_t = QbsType(TypeCode.RECORD, True, 21, 1, 0, 0)
    item_t = QbsType(TypeCode.RECORD, True, 21, 2, 0, 0)
    array_t = QbsType(TypeCode.ARRAY, False, 0, 2, 8, 0)
    schema.types = (u32, child_t, item_t, array_t)
    schema.records = (
        QbsRecord(schema, 0, 1, False, 1, 30, 46, "Parent", "Parent", 0, 2),
        QbsRecord(schema, 1, 2, False, 1, 5, 21, "Child", "Child", 2, 1),
        QbsRecord(schema, 2, 3, False, 1, 5, 21, "Item", "Item", 3, 1),
    )
    schema.fields = (
        QbsField(0, 1, 1, 0, 0, 0, 21, 0, 0, "child", schema.records[0]),
        QbsField(1, 3, 22, 0, 0, 1, 8, 2, 1, "items", schema.records[0]),
        QbsField(0, 0, 1, 0, 0, 0, 4, 0, 0, "value", schema.records[1]),
        QbsField(0, 0, 1, 0, 0, 0, 4, 0, 0, "value", schema.records[2]),
    )
    return schema


def _record(record_id, value):
    return bytes([2, 0, 0, 16]) + struct.pack(">III", record_id, 5, 21) + bytes([1]) + struct.pack(">I", value)


class GenericRuntimeTest(unittest.TestCase):
    def test_nested_and_record_array_use_explicit_validation_graph(self):
        schema = _schema()
        child = _record(2, 7)
        item0, item1 = _record(3, 11), _record(3, 12)
        array = b"\x02" + item0 + item1
        fixed = bytearray(30)
        fixed[0] = 3
        fixed[1:22] = child
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        value = validate_brf(schema, schema.records[0], root)
        self.assertEqual(value["child"]["value"], 7)
        self.assertEqual([item["value"] for item in value["items"]], [11, 12])
        self.assertIs(value["child"], value["child"])


if __name__ == "__main__":
    unittest.main()
