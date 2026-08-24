import pathlib
import unittest

import quarry
from quarry.runtime.generic import BrfError, QbsError


FIXTURE = pathlib.Path(__file__).resolve().parents[3] / "tests" / "fixtures" / "generic_runtime_conformance"


def _mutations(kind):
    result = []
    for line in (FIXTURE / "mutations.txt").read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        mutation_kind, name, offset, replacement = line.split("|")
        if mutation_kind == kind:
            result.append((name, int(offset), replacement))
    return result


def _mutate(data, mutation):
    name, offset, replacement = mutation
    if replacement.startswith("TRUNCATE:"):
        return data[:offset]
    value = bytearray(data)
    replacement_bytes = bytes.fromhex(replacement)
    value[offset:offset + len(replacement_bytes)] = replacement_bytes
    return bytes(value)


class GenericRuntimeConformanceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.schema_bytes = (FIXTURE / "schema.qbs").read_bytes()
        cls.brf_bytes = (FIXTURE / "record.brf").read_bytes()
        cls.schema = quarry.load_qbs(cls.schema_bytes)
        cls.record = quarry.validate_brf(cls.schema, cls.schema.record("Parent"), cls.brf_bytes)

    def test_valid_fixture_matches_canonical_values(self):
        record = self.record
        self.assertEqual(record["sequence"], 42)
        self.assertEqual(record["delta"], -17)
        self.assertTrue(record["enabled"])
        self.assertEqual(record["temperature"], 12.5)
        self.assertEqual(record["ratio"], -3.25)
        self.assertEqual(record["state"], 1)
        self.assertEqual(record["name"], "quarry")
        self.assertEqual(record["payload"], b"\x01\x02\xff")
        self.assertEqual(list(record["samples"]), [1, 2, 3])
        self.assertEqual(record["child"]["value"], 100)
        self.assertEqual(record["child"]["label"], "child")
        self.assertEqual(len(record["items"]), 2)
        self.assertEqual(record["items"][0]["value"], -4)
        self.assertEqual(record["items"][1]["value"], 8)
        self.assertEqual(record["items"][0]["child"]["value"], 101)
        self.assertEqual(record["items"][1]["child"]["label"], "second-child")

    def test_presence_matches_fixture_contract(self):
        self.assertFalse(self.record.field_view("optional").present)
        empty = self.record.field_view("empty_samples")
        self.assertTrue(empty.present)
        self.assertEqual(len(empty.value), 0)

    def test_malformed_brf_is_rejected(self):
        for mutation in _mutations("BRF"):
            with self.subTest(mutation=mutation[0]), self.assertRaises(BrfError):
                quarry.validate_brf(self.schema, self.schema.record("Parent"), _mutate(self.brf_bytes, mutation))

    def test_malformed_qbs_is_rejected_at_load(self):
        for mutation in _mutations("QBS"):
            with self.subTest(mutation=mutation[0]), self.assertRaises(QbsError):
                quarry.load_qbs(_mutate(self.schema_bytes, mutation))


if __name__ == "__main__":
    unittest.main()
