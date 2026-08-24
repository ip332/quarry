import struct
import unittest
import gc

from quarry.runtime.generic import BrfError, BrfLimits, QbsField, QbsRecord, QbsSchema, QbsType, ResourceLimitError, TypeCode, validate_brf


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


def _variable_chain(depth):
    """Build a linear variable-record QBS/BRF chain without recursion."""
    schema = QbsSchema(b"", [], [], [], [], False)
    schema.types = tuple(QbsType(TypeCode.RECORD, False, 0, i + 1, 0, 0) for i in range(depth))
    records = []
    for i in range(depth + 1):
        records.append(QbsRecord(schema, i, i + 1, True, 1 if i < depth else 0,
                                 9 if i < depth else 0, 0, f"R{i}", None, i if i < depth else depth, 1 if i < depth else 0))
    schema.records = tuple(records)
    schema.fields = tuple(QbsField(0, i, 1, 0, 0, 0, 8, 2, 1, None, records[i]) for i in range(depth))
    value = bytes([2, 0, 0, 16]) + struct.pack(">III", depth + 1, 0, 16)
    for i in range(depth - 1, -1, -1):
        payload = bytes([1]) + struct.pack(">II", 25, len(value)) + value
        value = bytes([2, 0, 0, 16]) + struct.pack(">III", i + 1, 9, 25 + len(value)) + payload
    return schema, value


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

    def test_nested_and_array_fail_during_root_validation(self):
        schema = _schema()
        child = bytearray(_record(2, 7))
        child[7] = 9  # child record id
        item = _record(3, 11)
        array = b"\x01" + item
        fixed = bytearray(30); fixed[0] = 3; fixed[1:22] = child
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        with self.assertRaises(BrfError):
            validate_brf(schema, schema.records[0], root)

    def test_array_element_limit_is_checked_before_view_access(self):
        schema = _schema()
        schema.types = schema.types[:3] + (QbsType(TypeCode.ARRAY, False, 0, 2, 1, 0),)
        child = _record(2, 7); item = _record(3, 11)
        array = b"\x02" + item + item
        fixed = bytearray(30); fixed[0] = 3; fixed[1:22] = child
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        with self.assertRaises(ResourceLimitError):
            validate_brf(schema, schema.records[0], root)

    def test_record_array_max_elements_exact_boundary(self):
        schema = _schema()
        schema.types = schema.types[:3] + (QbsType(TypeCode.ARRAY, False, 0, 2, 2, 0),)
        child = _record(2, 7); item = _record(3, 11)
        array = b"\x02" + item + item
        fixed = bytearray(30); fixed[0] = 3; fixed[1:22] = child
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        value = validate_brf(schema, schema.records[0], root)
        self.assertEqual(len(value["items"]), 2)
        array = b"\x03" + item + item + item
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        with self.assertRaises(ResourceLimitError):
            validate_brf(schema, schema.records[0], root)

    def test_primitive_array_max_elements_exact_boundary(self):
        schema = _schema()
        schema.types = schema.types[:3] + (QbsType(TypeCode.ARRAY, False, 0, 0, 2, 0),)
        values = struct.pack(">II", 10, 20)
        array = b"\x02" + values
        fixed = bytearray(30); fixed[0] = 2
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        value = validate_brf(schema, schema.records[0], root)
        self.assertEqual(list(value["items"]), [10, 20])
        array = b"\x03" + values + struct.pack(">I", 30)
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        with self.assertRaises(ResourceLimitError):
            validate_brf(schema, schema.records[0], root)

    def test_noncanonical_record_array_count_is_rejected(self):
        schema = _schema()
        child = _record(2, 7); item = _record(3, 11)
        array = b"\x80\x00" + item
        fixed = bytearray(30); fixed[0] = 3; fixed[1:22] = child
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        with self.assertRaises(BrfError):
            validate_brf(schema, schema.records[0], root)

    def test_index_lookup_is_local_to_each_record_without_names(self):
        schema = _schema()
        schema.records = tuple(
            QbsRecord(schema, r.index, r.record_id, r.variable_size, r.presence_bitmap_size,
                      r.fixed_region_size, r.complete_fixed_record_size, r.identity, None,
                      r.field_start, r.field_count) for r in schema.records
        )
        schema.fields = tuple(
            QbsField(f.index, f.type_index, f.byte_offset, f.bit_offset, f.bit_width,
                     f.presence_bit, f.slot_size, f.storage, f.descriptor_kind, None,
                     schema.records[f.owner.index]) for f in schema.fields
        )
        child = _record(2, 7); item = _record(3, 11)
        array = b"\x01" + item
        fixed = bytearray(30); fixed[0] = 3; fixed[1:22] = child
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        value = validate_brf(schema, schema.records[0], root)
        self.assertEqual(value.field_view(0).value.field(0), 7)
        self.assertEqual(value.field(1)[0].field(0), 11)
        with self.assertRaises(KeyError):
            schema.record("Parent")

    def test_qbs_load_rejects_bad_header_and_truncation(self):
        from quarry.runtime.generic import QbsError, load_qbs
        with self.assertRaises(QbsError):
            load_qbs(b"QBS\0")
        bad = bytearray(40); bad[:4] = b"QBS\0"; bad[4:6] = b"\x02\x00"
        with self.assertRaises(QbsError):
            load_qbs(bad)

    def test_field_view_preserves_presence(self):
        schema = _schema()
        child = _record(2, 7); item = _record(3, 11)
        array = b"\x01" + item
        fixed = bytearray(30); fixed[0] = 3; fixed[1:22] = child
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        value = validate_brf(schema, schema.records[0], root)
        self.assertTrue(value.field_view("child").present)
        self.assertTrue(value.field_view("items").present)

    def test_record_array_sequence_and_returned_views_survive_parent_collection(self):
        schema = _schema()
        child = _record(2, 7); item = _record(3, 11)
        array = b"\x01" + item
        fixed = bytearray(30); fixed[0] = 3; fixed[1:22] = child
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        value = validate_brf(schema, schema.records[0], root)
        child_view, items_view, item_view = value["child"], value["items"], value["items"][0]
        del value, root
        gc.collect()
        self.assertEqual(child_view["value"], 7)
        self.assertEqual(len(items_view), 1)
        self.assertEqual(items_view[-1]["value"], 11)
        self.assertIs(item_view, items_view[0])
        with self.assertRaises(IndexError):
            _ = items_view[1]

    def test_record_array_trailing_payload_fails_during_validation(self):
        schema = _schema()
        child = _record(2, 7); item = _record(3, 11)
        array = b"\x01" + item + b"\0"
        fixed = bytearray(30); fixed[0] = 3; fixed[1:22] = child
        fixed[22:30] = struct.pack(">II", 46, len(array))
        root = bytes([2, 0, 0, 16]) + struct.pack(">III", 1, 30, 16 + 30 + len(array)) + fixed + array
        with self.assertRaises(BrfError):
            validate_brf(schema, schema.records[0], root)

    def test_variable_chain_uses_production_frames_at_depth_2048(self):
        schema, root = _variable_chain(2048)
        value = validate_brf(schema, schema.records[0], root,
                             limits=BrfLimits(max_record_bytes=len(root) + 1, max_work_items=2048,
                                              max_nested_records=2048))
        for _ in range(8):
            value = value[0]
        self.assertIsNotNone(value)
        with self.assertRaises(ResourceLimitError):
            validate_brf(schema, schema.records[0], root,
                         limits=BrfLimits(max_record_bytes=len(root) + 1, max_work_items=2048,
                                          max_nested_records=2047))

    def test_exact_work_limit_for_two_record_chain(self):
        schema, root = _variable_chain(2)
        limits = BrfLimits(max_record_bytes=len(root) + 1, max_work_items=2, max_nested_records=2)
        validate_brf(schema, schema.records[0], root, limits=limits)
        limits = BrfLimits(max_record_bytes=len(root) + 1, max_work_items=1, max_nested_records=2)
        with self.assertRaises(ResourceLimitError):
            validate_brf(schema, schema.records[0], root, limits=limits)


if __name__ == "__main__":
    unittest.main()
