#!/usr/bin/env python3
"""시험 경로 검사 — 어떤 시험 경로가 다른 시험 경로의 앞부분이면(예: A.B 와 A.B.C) 엔진 목록에서 A.B 가 조용히 빠진다.
사용: python3 tools/check_test_paths.py   (겹침이 있으면 종료 코드 1)
"""
import glob
import re
import sys

paths = {}
for f in glob.glob('Source/**/*.cpp', recursive=True):
    t = open(f, errors='ignore').read()
    for m in re.finditer(r'IMPLEMENT_(?:SIMPLE|COMPLEX)_AUTOMATION_TEST\s*\(\s*\w+\s*,\s*"([^"]+)"', t):
        paths.setdefault(m.group(1).lower(), (m.group(1), f))

bad = [(v, paths[p2][0]) for p, v in paths.items() for p2 in paths if p2.startswith(p + '.')]
for (name, f), child in bad:
    print(f'겹침: {name} ({f}) 는 {child} 의 앞부분이라 엔진 목록에서 빠진다')
print(f'시험 경로 {len(paths)}개, 겹침 {len(bad)}개')
sys.exit(1 if bad else 0)
