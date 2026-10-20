"""DT_ItemTable 의 아이템 ID 표를 찍는다 — 감독이 PlayFab 카탈로그에 넣을 새 ID 를 한 명령으로 뽑는 읽기 전용 도구.

    python3 tools/playfab/list_catalog_ids.py                  # 지금 표 전체
    python3 tools/playfab/list_catalog_ids.py --since <rev>    # 그 리비전 뒤에 새로 생긴 ID 만

`dump_item_rows.py` 와 같은 길(에디터 없이 `create_data_tables.py` 의 ITEM_ROWS 계산)이고, 네트워크·키·쓰기는 없다.
"""
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
GENERATOR = "tools/unreal/create_data_tables.py"


def dump_rows(generator_path=None):
    """ITEM_ROWS 를 리스트로 꺼낸다. generator_path 가 없으면 저장소의 실제 생성기."""
    cmd = [sys.executable, os.path.join(HERE, "dump_item_rows.py")]
    if generator_path:
        cmd.append(generator_path)
    out = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def rows_at(rev):
    """리비전 rev 시점의 생성기로 만든 행. rev 에 생성기가 없으면 예외."""
    src = subprocess.run(["git", "-C", REPO, "show", "%s:%s" % (rev, GENERATOR)],
                         check=True, capture_output=True, text=True).stdout
    with tempfile.NamedTemporaryFile("w", suffix=".py", delete=False, encoding="utf-8") as f:
        f.write(src)
        tmp = f.name
    try:
        return dump_rows(tmp)
    finally:
        os.remove(tmp)


def ids_of(rows):
    """행 목록의 ItemID 목록. 같은 ID 가 두 번 나오면 ValueError (카탈로그에 한 번만 들어가야 한다)."""
    ids = [r["ItemID"] for r in rows]
    dup = sorted({i for i in ids if ids.count(i) > 1})
    if dup:
        raise ValueError("ItemID 중복: " + ", ".join(dup))
    return ids


def new_rows(before_rows, after_rows):
    """after 에만 있는 ItemID 의 행 (after 순서 그대로)."""
    old = set(ids_of(before_rows))
    ids_of(after_rows)
    return [r for r in after_rows if r["ItemID"] not in old]


def format_row(r):
    return "%s\t%s\t%s\t%s\t%s" % (r["ItemID"], r.get("ItemType", ""), r.get("Grade", ""),
                                   r.get("SellPrice", ""), r.get("DisplayName", ""))


def main(argv):
    since = None
    if "--since" in argv:
        since = argv[argv.index("--since") + 1]
    rows = dump_rows()
    ids_of(rows)
    if since:
        rows = new_rows(rows_at(since), rows)
    print("ItemID\tItemType\tGrade\tSellPrice\tDisplayName")
    for r in rows:
        print(format_row(r))
    print("합계: %d개%s" % (len(rows), " (%s 뒤 새 ID)" % since if since else ""), file=sys.stderr)


if __name__ == "__main__":
    main(sys.argv[1:])
