"""create_data_tables.py 를 에디터 없이 돌려 DT_ItemTable 행을 JSON 으로 내보낸다 (node 시험용).

`unreal` 모듈을 빈 껍데기로 바꿔 끼운다 — 에셋은 하나도 만들지 않고, 파이썬이 계산한 ITEM_ROWS 만 꺼낸다.
`--rows=ANIMAL_ROWS` 처럼 주면 그 표 행을 꺼낸다(ClaimLoot 허용 목록 대조용).
"""
import json
import os
import runpy
import sys
import types


class _Stub:
    def __init__(self, *args, **kwargs):
        pass

    def __getattr__(self, name):
        return _Stub()

    def __call__(self, *args, **kwargs):
        return _Stub()

    def __iter__(self):
        return iter(())

    def __bool__(self):
        return False

    def __str__(self):
        return ""

    def __format__(self, spec):
        return ""


repo_root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
fake = types.ModuleType("unreal")
fake.__getattr__ = lambda name: _Stub()
fake.Paths = types.SimpleNamespace(project_dir=lambda: repo_root + os.sep)
sys.modules["unreal"] = fake

# 인자 = 역검증용 생성기 사본(일부러 행을 뺀 것). 기본은 실제 파일.
args = [a for a in sys.argv[1:] if not a.startswith("--rows=")]
rows_name = next((a[len("--rows="):] for a in sys.argv[1:] if a.startswith("--rows=")), "ITEM_ROWS")
script = args[0] if args else os.path.join(repo_root, "tools", "unreal", "create_data_tables.py")
stdout = sys.stdout
sys.stdout = open(os.devnull, "w")
try:
    result = runpy.run_path(script)
finally:
    sys.stdout.close()
    sys.stdout = stdout
json.dump(result[rows_name], sys.stdout, ensure_ascii=False)
