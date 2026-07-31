"""Unit tests for quarry.runtime.python.binary_record (PR-119 scalars,
PR-120 enums).

Run directly: PYTHONPATH=runtime/python/src python3 -m unittest
runtime/python/tests/test_binary_record.py -v

Exercised only through the public module API (pack_scalar/unpack_scalar/
pack_enum/unpack_enum/append_varuint/read_varuint/encode_record/
parse_record) -- these tests know nothing about the Python backend's code
generator; they validate the runtime primitive layer in isolation, matching
this project's "runtime components must not depend on the compiler"
boundary.
"""

import unittest
from enum import IntEnum

from quarry.runtime.python import binary_record as brf


class Status(IntEnum):
    OK = 0
    WARNING = 1
    ERROR = 2


class ScalarRoundTripTest(unittest.TestCase):
    def test_bool_round_trip(self):
        self.assertEqual(brf.pack_scalar("bool", True), b"\x01")
        self.assertEqual(brf.pack_scalar("bool", False), b"\x00")
        self.assertIs(brf.unpack_scalar("bool", b"\x01"), True)
        self.assertIs(brf.unpack_scalar("bool", b"\x00"), False)

    def test_bool_rejects_non_bool_value(self):
        with self.assertRaises(brf.EncodeError):
            brf.pack_scalar("bool", 1)

    def test_bool_rejects_invalid_byte(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_scalar("bool", b"\x02")

    def test_bool_rejects_wrong_length(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_scalar("bool", b"")
        with self.assertRaises(brf.DecodeError):
            brf.unpack_scalar("bool", b"\x00\x00")

    def _round_trip(self, type_name, value, expected_hex):
        encoded = brf.pack_scalar(type_name, value)
        self.assertEqual(encoded.hex(), expected_hex)
        self.assertEqual(brf.unpack_scalar(type_name, encoded), value)

    def test_int8_boundaries(self):
        self._round_trip("int8", -128, "80")
        self._round_trip("int8", 127, "7f")
        self._round_trip("int8", 0, "00")

    def test_uint8_boundaries(self):
        self._round_trip("uint8", 0, "00")
        self._round_trip("uint8", 255, "ff")

    def test_int16_boundaries(self):
        self._round_trip("int16", -32768, "8000")
        self._round_trip("int16", 32767, "7fff")

    def test_uint16_boundaries(self):
        self._round_trip("uint16", 0, "0000")
        self._round_trip("uint16", 65535, "ffff")

    def test_int32_boundaries(self):
        self._round_trip("int32", -2147483648, "80000000")
        self._round_trip("int32", 2147483647, "7fffffff")

    def test_uint32_boundaries(self):
        self._round_trip("uint32", 0, "00000000")
        self._round_trip("uint32", 4294967295, "ffffffff")

    def test_int64_boundaries(self):
        self._round_trip("int64", -9223372036854775808, "8000000000000000")
        self._round_trip("int64", 9223372036854775807, "7fffffffffffffff")

    def test_uint64_boundaries(self):
        self._round_trip("uint64", 0, "0000000000000000")
        self._round_trip("uint64", 18446744073709551615, "ffffffffffffffff")

    def test_float32_round_trip(self):
        encoded = brf.pack_scalar("float32", 1.5)
        self.assertEqual(encoded.hex(), "3fc00000")
        self.assertEqual(brf.unpack_scalar("float32", encoded), 1.5)

    def test_float64_round_trip(self):
        encoded = brf.pack_scalar("float64", 3.14)
        self.assertEqual(encoded.hex(), "40091eb851eb851f")
        self.assertEqual(brf.unpack_scalar("float64", encoded), 3.14)

    def test_out_of_range_integers_are_rejected(self):
        for type_name, bad_value in [
            ("int8", 128), ("int8", -129),
            ("uint8", 256), ("uint8", -1),
            ("int16", 32768), ("int16", -32769),
            ("uint16", 65536), ("uint16", -1),
            ("int32", 2147483648), ("int32", -2147483649),
            ("uint32", 4294967296), ("uint32", -1),
            ("int64", 9223372036854775808), ("int64", -9223372036854775809),
            ("uint64", 18446744073709551616), ("uint64", -1),
        ]:
            with self.subTest(type_name=type_name, bad_value=bad_value):
                with self.assertRaises(brf.EncodeError):
                    brf.pack_scalar(type_name, bad_value)

    def test_wrong_length_field_value_is_rejected_on_decode(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_scalar("uint32", b"\x00\x00\x00")
        with self.assertRaises(brf.DecodeError):
            brf.unpack_scalar("uint32", b"\x00\x00\x00\x00\x00")

    def test_unsupported_type_name_is_rejected(self):
        with self.assertRaises(brf.EncodeError):
            brf.pack_scalar("string", "hello")
        with self.assertRaises(brf.DecodeError):
            brf.unpack_scalar("string", b"hello")


class EnumRoundTripTest(unittest.TestCase):
    def test_round_trip_by_enum_member(self):
        encoded = brf.pack_enum(Status, Status.WARNING, "uint8")
        self.assertEqual(encoded, b"\x01")
        decoded = brf.unpack_enum(Status, "uint8", encoded)
        self.assertIs(decoded, Status.WARNING)

    def test_round_trip_by_raw_int_matching_a_member(self):
        # A raw int equal to a defined member's value is accepted (this is
        # how a plain-int field value that happens to match a member ends
        # up validated-by-construction), and produces byte-identical output
        # to encoding the enum member directly.
        encoded_from_int = brf.pack_enum(Status, 1, "uint8")
        encoded_from_member = brf.pack_enum(Status, Status.WARNING, "uint8")
        self.assertEqual(encoded_from_int, encoded_from_member)

    def test_pack_matches_pack_scalar_for_the_same_width_and_value(self):
        # pack_enum must be wire-identical to pack_scalar for the
        # underlying width/value -- it is validate-then-delegate, not a
        # different wire representation.
        self.assertEqual(brf.pack_enum(Status, Status.ERROR, "uint8"),
                        brf.pack_scalar("uint8", int(Status.ERROR)))

    def test_pack_rejects_a_value_not_defined_by_the_enum(self):
        with self.assertRaises(brf.EncodeError):
            brf.pack_enum(Status, 99, "uint8")

    def test_unpack_rejects_a_decoded_value_not_defined_by_the_enum(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_enum(Status, "uint8", bytes([99]))

    def test_unpack_rejects_wrong_length_data(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_enum(Status, "uint8", b"")
        with self.assertRaises(brf.DecodeError):
            brf.unpack_enum(Status, "uint8", b"\x00\x00")

    def test_wider_width_type_round_trips(self):
        encoded = brf.pack_enum(Status, Status.ERROR, "uint32")
        self.assertEqual(len(encoded), 4)
        self.assertIs(brf.unpack_enum(Status, "uint32", encoded), Status.ERROR)


class VaruintTest(unittest.TestCase):
    def _round_trip(self, value, expected_hex):
        buffer = bytearray()
        brf.append_varuint(buffer, value)
        self.assertEqual(bytes(buffer).hex(), expected_hex)
        decoded_value, offset = brf.read_varuint(bytes(buffer), 0)
        self.assertEqual(decoded_value, value)
        self.assertEqual(offset, len(buffer))

    def test_zero_is_single_byte(self):
        self._round_trip(0, "00")

    def test_single_byte_boundary(self):
        self._round_trip(127, "7f")

    def test_multi_byte_value(self):
        self._round_trip(300, "ac02")

    def test_large_value(self):
        self._round_trip(2**63, "80808080808080808001")

    def test_negative_value_is_rejected(self):
        with self.assertRaises(brf.EncodeError):
            brf.append_varuint(bytearray(), -1)

    def test_truncated_varuint_is_rejected(self):
        with self.assertRaises(brf.DecodeError):
            brf.read_varuint(b"\x80", 0)

    def test_offset_past_end_is_rejected(self):
        with self.assertRaises(brf.DecodeError):
            brf.read_varuint(b"\x01", 1)

    def test_reads_starting_mid_buffer(self):
        data = b"\xff\xac\x02"
        value, offset = brf.read_varuint(data, 1)
        self.assertEqual(value, 300)
        self.assertEqual(offset, 3)


class RecordEncodeDecodeTest(unittest.TestCase):
    def test_empty_record_round_trip(self):
        data = brf.encode_record(1, [])
        self.assertEqual(data.hex(), "01000000000000010000000000000000")
        record_id, fields = brf.parse_record(data)
        self.assertEqual(record_id, 1)
        self.assertEqual(fields, {})

    def test_scalar_fields_round_trip(self):
        fields_in = [
            (0, brf.pack_scalar("bool", True)),
            (1, brf.pack_scalar("uint32", 42)),
            (2, brf.pack_scalar("float64", 3.14)),
        ]
        data = brf.encode_record(1, fields_in)
        record_id, fields_out = brf.parse_record(data)
        self.assertEqual(record_id, 1)
        self.assertEqual(brf.unpack_scalar("bool", fields_out[0]), True)
        self.assertEqual(brf.unpack_scalar("uint32", fields_out[1]), 42)
        self.assertEqual(brf.unpack_scalar("float64", fields_out[2]), 3.14)

    def test_fields_are_sorted_regardless_of_input_order(self):
        data_in_order = brf.encode_record(
            1, [(0, b"\x01"), (1, b"\x02\x02\x02\x02")])
        data_reversed = brf.encode_record(
            1, [(1, b"\x02\x02\x02\x02"), (0, b"\x01")])
        self.assertEqual(data_in_order, data_reversed)

    def test_duplicate_field_index_is_rejected(self):
        with self.assertRaises(brf.EncodeError):
            brf.encode_record(1, [(0, b"\x01"), (0, b"\x02")])

    def test_absent_field_has_no_directory_entry(self):
        data = brf.encode_record(1, [(1, brf.pack_scalar("uint32", 7))])
        _, fields = brf.parse_record(data)
        self.assertNotIn(0, fields)
        self.assertIn(1, fields)

    def test_truncated_header_is_rejected(self):
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(b"\x01\x00\x00\x00")

    def test_unsupported_header_version_is_rejected(self):
        data = bytearray(brf.encode_record(1, []))
        data[0] = 2
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(bytes(data))

    def test_nonzero_flags_are_rejected(self):
        data = bytearray(brf.encode_record(1, []))
        data[1] = 1
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(bytes(data))

    def test_nonzero_reserved_fields_are_rejected(self):
        data = bytearray(brf.encode_record(1, []))
        data[3] = 1
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(bytes(data))

        data2 = bytearray(brf.encode_record(1, []))
        data2[8] = 1
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(bytes(data2))

    def test_truncated_buffer_is_rejected(self):
        data = brf.encode_record(1, [(0, brf.pack_scalar("uint32", 42))])
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(data[:-1])

    def test_extra_trailing_data_is_rejected(self):
        data = brf.encode_record(1, [(0, brf.pack_scalar("uint32", 42))])
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(data + b"\x00")

    def test_truncated_field_directory_is_rejected(self):
        data = bytearray(brf.encode_record(1, [(0, brf.pack_scalar("uint32", 42))]))
        # Truncate right after the directoryEntryCount says one entry
        # exists, but only part of that entry's bytes are present.
        truncated = bytes(data[:17])
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(truncated)

    def test_unsorted_directory_is_rejected(self):
        # Hand-build a malformed record: header claims 2 entries, but the
        # directory bytes list field index 1 before field index 0.
        directory = bytes([1, 0, 1, 0, 0, 1])  # idx1 off0 len1, idx0 off1 len0
        payload = b"\x01"
        body = directory + payload
        header = brf._HEADER_STRUCT.pack(1, 0, 2, 0, 1, 0, len(body))
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(header + body)

    def test_duplicate_directory_entry_is_rejected(self):
        directory = bytes([0, 0, 1, 0, 1, 0])  # idx0 off0 len1, idx0 off1 len0
        payload = b"\x01"
        body = directory + payload
        header = brf._HEADER_STRUCT.pack(1, 0, 2, 0, 1, 0, len(body))
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(header + body)

    def test_field_range_outside_payload_is_rejected(self):
        directory = bytes([0, 5, 1])  # offset 5, length 1, but payload is empty
        body = directory
        header = brf._HEADER_STRUCT.pack(1, 0, 1, 0, 1, 0, len(body))
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(header + body)

    def test_overlapping_field_ranges_are_rejected(self):
        directory = bytes([0, 0, 2, 1, 1, 2])  # idx0 [0,2), idx1 [1,3)
        payload = b"\x00\x00\x00"
        body = directory + payload
        header = brf._HEADER_STRUCT.pack(1, 0, 2, 0, 1, 0, len(body))
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(header + body)

    def test_too_many_fields_is_rejected(self):
        fields = [(index, b"\x00") for index in range(256)]
        with self.assertRaises(brf.EncodeError):
            brf.encode_record(1, fields)


if __name__ == "__main__":
    unittest.main()
