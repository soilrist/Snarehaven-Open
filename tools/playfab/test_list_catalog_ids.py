"""list_catalog_ids.py 시험 — `python3 tools/playfab/test_list_catalog_ids.py`"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import list_catalog_ids as lci  # noqa: E402


def fake_rows(prefix, count):
    return [{"ItemID": "item.%s%d" % (prefix, i), "ItemType": "Cosmetic", "Grade": "Common",
             "SellPrice": 100, "DisplayName": "가짜 %d" % i} for i in range(count)]


class ListCatalogIdsTest(unittest.TestCase):
    def test_row_count_matches_dump(self):
        rows = lci.dump_rows()
        self.assertGreater(len(rows), 400)
        self.assertEqual(len(lci.ids_of(rows)), len(rows))

    def test_new_rows_only_ids_missing_before(self):
        before = fake_rows("a", 5)
        after = fake_rows("a", 5) + fake_rows("b", 7)
        added = lci.new_rows(before, after)
        self.assertEqual([r["ItemID"] for r in added], ["item.b%d" % i for i in range(7)])

    def test_new_rows_empty_when_nothing_added(self):
        rows = fake_rows("a", 4)
        self.assertEqual(lci.new_rows(rows, list(rows)), [])

    def test_removed_ids_are_not_listed(self):
        self.assertEqual(lci.new_rows(fake_rows("a", 5), fake_rows("a", 3)), [])

    def test_duplicate_id_fails(self):
        rows = fake_rows("a", 3) + fake_rows("a", 1)
        with self.assertRaises(ValueError):
            lci.ids_of(rows)

    def test_since_m88_plan_lists_23_new_ids(self):
        added = lci.new_rows(lci.rows_at("4279385b"), lci.dump_rows())
        ids = {r["ItemID"] for r in added}
        self.assertEqual(len(ids), 23)
        for must in ("item.meadow_ladybug", "item.harbor_submarine_pack", "item.harbor_argyle"):
            self.assertIn(must, ids)


if __name__ == "__main__":
    unittest.main(verbosity=2)
