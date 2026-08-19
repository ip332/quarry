"""Unit tests for quarry.runtime.python.binary_record (PR-119 scalars,
PR-120 enums, PR-121 string/bytes, PR-122 fixed-width arrays, PR-123
variable-width string/bytes arrays).

Run directly: PYTHONPATH=runtime/python/src python3 -m unittest
runtime/python/tests/test_binary_record.py -v

Exercised only through the public module API (pack_scalar/unpack_scalar/
 pack_enum/unpack_enum/pack_array_of_scalar/unpack_array_of_scalar/
pack_array_of_enum/unpack_array_of_enum/pack_array_of_string/
unpack_array_of_string/pack_array_of_bytes/unpack_array_of_bytes/
append_varuint/read_varuint/
encode_record/parse_record) -- these tests know nothing about the Python
backend's code generator; they validate the runtime primitive layer in
isolation, matching this project's "runtime components must not depend on the
compiler" boundary.
"""

import struct
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


class StringBytesRoundTripTest(unittest.TestCase):
    def test_string_round_trip(self):
        encoded = brf.pack_string("café", 16)
        self.assertEqual(encoded, "café".encode("utf-8"))
        self.assertEqual(brf.unpack_string(encoded, 16), "café")

    def test_string_empty_value(self):
        encoded = brf.pack_string("", 16)
        self.assertEqual(encoded, b"")
        self.assertEqual(brf.unpack_string(encoded, 16), "")

    def test_string_embedded_nul_is_valid(self):
        value = "ab\x00cd"
        encoded = brf.pack_string(value, 16)
        self.assertEqual(brf.unpack_string(encoded, 16), value)

    def test_string_maximum_length_value(self):
        value = "x" * 16
        encoded = brf.pack_string(value, 16)
        self.assertEqual(len(encoded), 16)
        self.assertEqual(brf.unpack_string(encoded, 16), value)

    def test_string_over_length_value_is_rejected_on_encode(self):
        with self.assertRaises(brf.EncodeError):
            brf.pack_string("x" * 17, 16)

    def test_string_over_length_value_is_rejected_on_decode(self):
        # A field whose byte length exceeds max_bytes must be rejected even
        # if it happens to be otherwise-valid UTF-8 -- the bound is checked
        # before content is decoded, mirroring the C++/C runtimes' own
        # bounds-check-first ordering.
        with self.assertRaises(brf.DecodeError):
            brf.unpack_string(b"x" * 17, 16)

    def test_string_rejects_non_str_value_on_encode(self):
        with self.assertRaises(brf.EncodeError):
            brf.pack_string(b"not a str", 16)
        with self.assertRaises(brf.EncodeError):
            brf.pack_string(123, 16)

    def test_string_rejects_malformed_utf8_on_decode(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_string(b"\xff\xfe", 16)

    def test_string_rejects_lone_surrogate_on_encode(self):
        # A lone surrogate code point can exist in a Python str (e.g. via
        # surrogateescape decoding) but has no UTF-8 encoding -- the one
        # "invalid content" case pack_string must still catch even though
        # it delegates UTF-8 validation to the stdlib.
        lone_surrogate = "\udc80"
        with self.assertRaises(brf.EncodeError):
            brf.pack_string(lone_surrogate, 16)

    def test_bytes_round_trip(self):
        value = bytes([0x00, 0xFF, 0x80, 0x01, 0xC0])
        encoded = brf.pack_bytes(value, 16)
        self.assertEqual(encoded, value)
        self.assertEqual(brf.unpack_bytes(encoded, 16), value)

    def test_bytes_empty_value(self):
        encoded = brf.pack_bytes(b"", 16)
        self.assertEqual(encoded, b"")
        self.assertEqual(brf.unpack_bytes(encoded, 16), b"")

    def test_bytes_maximum_length_value(self):
        value = bytes(range(16))
        encoded = brf.pack_bytes(value, 16)
        self.assertEqual(len(encoded), 16)
        self.assertEqual(brf.unpack_bytes(encoded, 16), value)

    def test_bytes_over_length_value_is_rejected_on_encode(self):
        with self.assertRaises(brf.EncodeError):
            brf.pack_bytes(bytes(17), 16)

    def test_bytes_over_length_value_is_rejected_on_decode(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_bytes(bytes(17), 16)

    def test_bytes_never_validates_utf8(self):
        # Arbitrary binary content that is not valid UTF-8 must round-trip
        # through bytes fields unchanged -- proving bytes fields never
        # validate it, unlike string fields.
        invalid_utf8 = bytes([0xFF, 0xFE, 0x00, 0x80])
        encoded = brf.pack_bytes(invalid_utf8, 16)
        self.assertEqual(brf.unpack_bytes(encoded, 16), invalid_utf8)

    def test_bytes_accepts_bytearray(self):
        encoded = brf.pack_bytes(bytearray([1, 2, 3]), 16)
        self.assertEqual(encoded, b"\x01\x02\x03")
        self.assertIsInstance(encoded, bytes)

    def test_bytes_rejects_non_bytes_value_on_encode(self):
        with self.assertRaises(brf.EncodeError):
            brf.pack_bytes("not bytes", 16)
        with self.assertRaises(brf.EncodeError):
            brf.pack_bytes(123, 16)


class ArrayRoundTripTest(unittest.TestCase):
    def test_scalar_array_round_trip_and_wire_framing(self):
        encoded = brf.pack_array_of_scalar("uint16", [1, 0x2345, 0xFFFF], 3)
        self.assertEqual(encoded, b"\x03\x00\x01\x23\x45\xff\xff")
        self.assertEqual(brf.unpack_array_of_scalar("uint16", encoded, 3),
                         [1, 0x2345, 0xFFFF])

    def test_empty_scalar_array_is_present_and_has_zero_count(self):
        encoded = brf.pack_array_of_scalar("uint32", [], 4)
        self.assertEqual(encoded, b"\x00")
        self.assertEqual(brf.unpack_array_of_scalar("uint32", encoded, 4), [])

    def test_scalar_array_supports_all_fixed_width_element_kinds(self):
        cases = [
            ("bool", [True, False]),
            ("int8", [-2, 127]),
            ("uint8", [0, 255]),
            ("int16", [-2, 32767]),
            ("uint16", [0, 65535]),
            ("int32", [-2, 2147483647]),
            ("uint32", [0, 4294967295]),
            ("int64", [-2, 9223372036854775807]),
            ("uint64", [0, 18446744073709551615]),
            ("float32", [-1.5, 2.5]),
            ("float64", [-1.5, 2.5]),
        ]
        for type_name, values in cases:
            with self.subTest(type_name=type_name):
                encoded = brf.pack_array_of_scalar(type_name, values, len(values))
                self.assertEqual(brf.unpack_array_of_scalar(type_name, encoded, len(values)),
                                 values)

    def test_enum_array_round_trip_and_membership(self):
        encoded = brf.pack_array_of_enum(Status, "uint8",
                                         [Status.OK, Status.ERROR], 2)
        self.assertEqual(encoded, b"\x02\x00\x02")
        self.assertEqual(brf.unpack_array_of_enum(Status, "uint8", encoded, 2),
                         [Status.OK, Status.ERROR])
        with self.assertRaises(brf.EncodeError):
            brf.pack_array_of_enum(Status, "uint8", [99], 1)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_enum(Status, "uint8", b"\x01\x63", 1)

    def test_array_length_bound_is_checked_on_encode_and_decode(self):
        with self.assertRaises(brf.EncodeError):
            brf.pack_array_of_scalar("uint8", [1, 2, 3], 2)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_scalar("uint8", b"\x03\x01\x02\x03", 2)

    def test_array_payload_must_be_exactly_consumed(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_scalar("uint16", b"\x02\x00\x01", 2)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_scalar("uint16", b"\x01\x00\x01\x00", 2)

    def test_array_count_rejects_truncated_or_malformed_varuint(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_scalar("uint8", b"", 2)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_scalar("uint8", b"\x80", 2)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_scalar("uint8", b"\x80\x80\x80\x80\x80\x80\x80\x80\x80\x02", 2)

    def test_string_array_round_trip_and_empty_element(self):
        values = ["", "ASCII", "café", "🌍"]
        encoded = brf.pack_array_of_string(values, 4, 8)
        self.assertEqual(encoded, b"\x04\x00\x05ASCII\x05caf\xc3\xa9\x04\xf0\x9f\x8c\x8d")
        self.assertEqual(brf.unpack_array_of_string(encoded, 4, 8), values)

    def test_string_array_preserves_empty_array(self):
        encoded = brf.pack_array_of_string([], 4, 8)
        self.assertEqual(encoded, b"\x00")
        self.assertEqual(brf.unpack_array_of_string(encoded, 4, 8), [])

    def test_string_array_enforces_encoded_byte_and_count_bounds(self):
        with self.assertRaises(brf.EncodeError):
            brf.pack_array_of_string(["ééé"], 1, 5)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_string(b"\x01\x06abcdef", 1, 5)
        with self.assertRaises(brf.EncodeError):
            brf.pack_array_of_string(["a", "b"], 1, 1)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_string(b"\x02\x01a\x01b", 1, 1)

    def test_string_array_rejects_malformed_utf8(self):
        with self.assertRaises(brf.DecodeError) as context:
            brf.unpack_array_of_string(b"\x01\x01\xff", 1, 8)
        self.assertIn("array element 0", str(context.exception))

    def test_string_array_rejects_malformed_lengths_truncation_and_trailing_bytes(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_string(b"\x80", 1, 8)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_string(b"\x01\x80", 1, 8)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_string(b"\x01\x03ab", 1, 8)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_string(b"\x00\x00", 1, 8)

    def test_bytes_array_round_trip_and_empty_element(self):
        values = [b"", bytes([0, 255, 128]), b"abc"]
        encoded = brf.pack_array_of_bytes(values, 3, 8)
        self.assertEqual(encoded, b"\x03\x00\x03\x00\xff\x80\x03abc")
        self.assertEqual(brf.unpack_array_of_bytes(encoded, 3, 8), values)

    def test_bytes_array_enforces_element_and_count_bounds(self):
        with self.assertRaises(brf.EncodeError):
            brf.pack_array_of_bytes([b"123456789"], 1, 8)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_bytes(b"\x01\x09" + b"123456789", 1, 8)
        with self.assertRaises(brf.EncodeError):
            brf.pack_array_of_bytes([b"a", b"b"], 1, 1)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_bytes(b"\x02\x01a\x01b", 1, 1)

    def test_bytes_array_rejects_malformed_lengths_truncation_and_trailing_bytes(self):
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_bytes(b"\x80", 1, 8)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_bytes(b"\x01\x80", 1, 8)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_bytes(b"\x01\x03ab", 1, 8)
        with self.assertRaises(brf.DecodeError):
            brf.unpack_array_of_bytes(b"\x00\x00", 1, 8)


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
    def test_record_array_framing_composes_existing_record_helpers(self):
        child = brf.encode_record(2, [(0, brf.pack_scalar("uint32", 7))])
        payload = bytearray()
        brf.append_varuint(payload, 2)
        brf.append_varuint(payload, len(child))
        payload.extend(child)
        brf.append_varuint(payload, len(child))
        payload.extend(child)
        count, offset = brf.read_varuint(bytes(payload), 0)
        self.assertEqual(count, 2)
        decoded = []
        for _ in range(count):
            length, offset = brf.read_varuint(bytes(payload), offset)
            decoded.append(brf.parse_record(bytes(payload)[offset:offset + length]))
            offset += length
        self.assertEqual(decoded[0][0], 2)
        self.assertEqual(brf.unpack_scalar("uint32", decoded[1][1][0]), 7)
        self.assertEqual(offset, len(payload))

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

    def test_embedded_record_payload_composes_with_existing_record_helpers(self):
        child = brf.encode_record(2, [(0, brf.pack_scalar("uint32", 7))])
        parent = brf.encode_record(1, [(0, child)])
        parent_id, parent_fields = brf.parse_record(parent)
        child_id, child_fields = brf.parse_record(parent_fields[0])
        self.assertEqual(parent_id, 1)
        self.assertEqual(child_id, 2)
        self.assertEqual(brf.unpack_scalar("uint32", child_fields[0]), 7)

    def test_malformed_embedded_record_is_rejected_by_child_parse(self):
        child = brf.encode_record(2, [])
        parent = brf.encode_record(1, [(0, child + b"\x00")])
        _, parent_fields = brf.parse_record(parent)
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(parent_fields[0])
        with self.assertRaises(brf.DecodeError):
            brf.parse_record(child[:-1])

    def test_brf_v2_reference_record_is_byte_exact(self):
        fields = [
            (0, 17, 4, 0, 0, brf.pack_scalar("uint32", 1)),
            (1, 21, 8, 2, 1, brf.pack_string("abc", 16)),
            (2, 29, 2, 0, 2, brf.pack_scalar("uint16", 2)),
            (3, 31, 8, 2, 3, brf.pack_array_of_scalar("uint16", [10, 20], 4)),
        ]
        encoded = brf.encode_record_v2(1, 23, 1, fields)
        self.assertEqual(
            encoded.hex(),
            "0200001000000001000000170000002f"
            "0f00000001000000270000000300020000"
            "002a0000000561626302000a0014",
        )
        decoded = brf.parse_record_v2(
            encoded, 1, 23, 1,
            [(0, 17, 4, 0, 0), (1, 21, 8, 2, 1),
             (2, 29, 2, 0, 2), (3, 31, 8, 2, 3)],
        )
        self.assertEqual(brf.unpack_scalar("uint32", decoded[0]), 1)
        self.assertEqual(decoded[1], b"abc")
        self.assertEqual(brf.unpack_scalar("uint16", decoded[2]), 2)
        self.assertEqual(brf.unpack_array_of_scalar("uint16", decoded[3], 4), [10, 20])

    def test_brf_v2_absent_and_present_empty_values_are_distinct(self):
        metadata = [(0, 17, 8, 2, 0), (1, 25, 8, 2, 1), (2, 33, 8, 2, 2)]
        absent = brf.encode_record_v2(1, 25, 1, [])
        self.assertEqual(brf.parse_record_v2(absent, 1, 25, 1, metadata), {})
        present = brf.encode_record_v2(
            1, 25, 1,
            [(0, 17, 8, 2, 0, b""), (1, 25, 8, 2, 1, b""),
             (2, 33, 8, 2, 2, b"\x00")],
        )
        self.assertEqual(brf.parse_record_v2(present, 1, 25, 1, metadata),
                         {0: b"", 1: b"", 2: b"\x00"})

    def test_brf_v2_rejects_invalid_presence_and_descriptor_data(self):
        metadata = [(0, 17, 4, 0, 0), (1, 21, 8, 2, 1)]
        encoded = bytearray(brf.encode_record_v2(
            1, 13, 1,
            [(0, 17, 4, 0, 0, b"\x00\x00\x00\x01"),
             (1, 21, 8, 2, 1, b"abc")],
        ))
        encoded[16] |= 0x80
        with self.assertRaises(brf.DecodeError):
            brf.parse_record_v2(bytes(encoded), 1, 13, 1, metadata)
        encoded = bytearray(brf.encode_record_v2(
            1, 13, 1,
            [(0, 17, 4, 0, 0, b"\x00\x00\x00\x01")],
        ))
        encoded[21:29] = struct.pack(">II", 30, 4)
        encoded[16] |= 0x02
        with self.assertRaises(brf.DecodeError):
            brf.parse_record_v2(bytes(encoded), 1, 13, 1, metadata)

    def test_brf_v2_presence_uses_declaration_position_not_field_index(self):
        metadata = [(7, 17, 4, 0, 0), (12, 21, 4, 0, 1)]
        encoded = brf.encode_record_v2(
            1, 9, 1,
            [(12, 21, 4, 0, 1, b"\x00\x00\x00\x07")],
        )
        self.assertEqual(encoded[16], 0x02)
        self.assertEqual(
            brf.parse_record_v2(encoded, 1, 9, 1, metadata),
            {12: b"\x00\x00\x00\x07"},
        )
if __name__ == "__main__":
    unittest.main()
