"""cosmetic_sheet.py 순수 파이썬 시험 — 가짜 칸 5장으로 격자·칸 수·건너뛰기·행 이름 줄을 본다."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cosmetic_sheet as cs
from PIL import Image

ROWS = [{"Name": "Item_Hat%d" % i, "ItemType": "Cosmetic", "EquipSlot": "Head", "Grade": "Rare"} for i in range(5)]


class CosmeticSheetTest(unittest.TestCase):
    def make_shots(self, d, names):
        for n in names:
            Image.new("RGB", (1280, 720), (200, 50, 50)).save(os.path.join(d, n + ".png"))

    def test_select_rows_by_slot(self):
        rows = ROWS + [{"Name": "Item_Top", "ItemType": "Cosmetic", "EquipSlot": "OutfitTop"},
                       {"Name": "Item_Axe", "ItemType": "Equipment", "EquipSlot": "Head"}]
        self.assertEqual([r["Name"] for r in cs.select_rows(rows, "hat")], [r["Name"] for r in ROWS])

    def test_grid_size(self):
        self.assertEqual(cs.grid_size(5), (8 * cs.CELL, 1 * (cs.CELL + cs.LABEL_H)))
        self.assertEqual(cs.grid_size(9)[1], 2 * (cs.CELL + cs.LABEL_H))

    def test_build_sheet_counts_cells(self):
        with tempfile.TemporaryDirectory() as d:
            self.make_shots(d, [r["Name"] for r in ROWS])
            out = os.path.join(d, "s.jpg")
            self.assertEqual(cs.build_sheet(ROWS, d, out), 5)
            self.assertEqual(Image.open(out).size, cs.grid_size(5))

    def test_missing_cell_not_counted(self):
        with tempfile.TemporaryDirectory() as d:
            self.make_shots(d, [r["Name"] for r in ROWS[:3]])
            self.assertEqual(cs.build_sheet(ROWS, d, os.path.join(d, "s.jpg")), 3)

    def test_resume_skips_existing(self):
        with tempfile.TemporaryDirectory() as d:
            self.make_shots(d, [ROWS[0]["Name"], ROWS[1]["Name"]])
            self.assertEqual(len(cs.pending(ROWS, d, True)), 3)
            self.assertEqual(len(cs.pending(ROWS, d, False)), 5)

    def test_only_keeps_whole_sheet(self):
        shoot, sheet = cs.split_rows(ROWS, "Item_Hat2")
        self.assertEqual([r["Name"] for r in shoot], ["Item_Hat2"])
        self.assertEqual(len(sheet), len(ROWS))
        with tempfile.TemporaryDirectory() as d:
            self.make_shots(d, [r["Name"] for r in ROWS])
            self.assertEqual(cs.build_sheet(sheet, d, os.path.join(d, "s.jpg")), len(ROWS))
        self.assertEqual(len(cs.split_rows(ROWS, None)[0]), len(ROWS))

    def test_label_has_row_name_and_grade(self):
        self.assertEqual(cs.label_text(ROWS[0]), "Hat0 Rare")


if __name__ == "__main__":
    unittest.main()
