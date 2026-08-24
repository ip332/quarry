import gc
import pathlib
import unittest

import quarry
from quarry.runtime import BrfLimits, BrfTraversalEventKind, BrfTraversalLimits, ResourceLimitError
from runtime.python.tests.test_generic_runtime import _variable_chain


FIXTURE = pathlib.Path(__file__).resolve().parents[3] / "tests" / "fixtures" / "generic_runtime_conformance"


class GenericRuntimeTraversalTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        schema = quarry.load_qbs((FIXTURE / "schema.qbs").read_bytes())
        cls.record = quarry.validate_brf(schema, schema.record("Parent"), (FIXTURE / "record.brf").read_bytes())

    @staticmethod
    def normalize(events):
        return [(event.kind.value, event.field.index if event.field else None,
                 event.present, event.index, event.depth, event.value)
                for event in events]

    def test_canonical_fixture_order_and_values(self):
        events = list(self.record.traverse())
        kinds = [event.kind for event in events]
        self.assertEqual(kinds[0], BrfTraversalEventKind.RECORD_BEGIN)
        self.assertEqual(kinds[-1], BrfTraversalEventKind.RECORD_END)
        self.assertEqual([e.field.index for e in events if e.kind is BrfTraversalEventKind.FIELD],
                         [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 10,
                          0, 1, 0, 1, 0, 1, 0, 1, 11, 12])
        self.assertEqual(sum(e.kind is BrfTraversalEventKind.RECORD_BEGIN for e in events), 6)
        self.assertEqual(sum(e.kind is BrfTraversalEventKind.RECORD_END for e in events), 6)
        self.assertEqual(sum(e.kind is BrfTraversalEventKind.ARRAY_ELEMENT for e in events), 5)
        self.assertEqual(events[0].depth, 0)

        empty = next(e for e in events if e.kind is BrfTraversalEventKind.FIELD and e.field.index == 12)
        self.assertTrue(empty.present)
        optional = next(e for e in events if e.kind is BrfTraversalEventKind.FIELD and e.field.index == 11)
        self.assertFalse(optional.present)

    def test_repeated_and_independent_iterators(self):
        first = list(self.record.traverse())
        second = list(self.record.traverse())
        self.assertEqual(self.normalize(first), self.normalize(second))
        left, right = iter(self.record.traverse()), iter(self.record.traverse())
        self.assertEqual(next(left).kind, BrfTraversalEventKind.RECORD_BEGIN)
        self.assertEqual(next(right).kind, BrfTraversalEventKind.RECORD_BEGIN)
        self.assertEqual(next(left).kind, BrfTraversalEventKind.FIELD)
        self.assertEqual(next(right).kind, BrfTraversalEventKind.FIELD)

    def test_early_break_does_not_change_restart(self):
        for event in self.record.traverse():
            if event.kind is BrfTraversalEventKind.FIELD:
                break
        self.assertEqual(list(self.record.traverse())[0].kind, BrfTraversalEventKind.RECORD_BEGIN)

    def test_work_limit_is_exact(self):
        required = len(list(self.record.traverse()))
        self.assertEqual(len(list(self.record.traverse(BrfTraversalLimits(required, 1024)))), required)
        with self.assertRaises(ResourceLimitError):
            list(self.record.traverse(BrfTraversalLimits(required - 1, 1024)))

    def test_depth_limit_is_explicit(self):
        child = self.record["items"][0]["child"]
        self.assertEqual(next(child.traverse()).kind, BrfTraversalEventKind.RECORD_BEGIN)
        with self.assertRaises(ResourceLimitError):
            list(self.record.traverse(BrfTraversalLimits(1 << 20, 0)))

    def test_deep_traversal_is_iterative(self):
        schema, brf = _variable_chain(2048)
        root = quarry.validate_brf(schema, schema.records[0], brf,
                                   BrfLimits(max_nested_records=2048))
        count = sum(event.kind is BrfTraversalEventKind.RECORD_BEGIN
                    for event in root.traverse(BrfTraversalLimits(1 << 20, 2048)))
        self.assertEqual(count, 2049)
        with self.assertRaises(ResourceLimitError):
            for _ in root.traverse(BrfTraversalLimits(1 << 20, 2047)):
                pass

    def test_iterator_and_nested_view_survive_collection(self):
        def make_iterator():
            return self.record["items"][0]["child"].traverse()

        iterator = make_iterator()
        gc.collect()
        self.assertEqual(next(iterator).kind, BrfTraversalEventKind.RECORD_BEGIN)


if __name__ == "__main__":
    unittest.main()
