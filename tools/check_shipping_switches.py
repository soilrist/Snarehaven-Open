#!/usr/bin/env python3
"""M95: 출시(Shipping) 빌드에 검사 전용 스위치·디버그 글자가 새지 않는지 소스를 정적으로 검사한다.

규칙
 1. 게임 소스(Tests/ 제외)는 명령줄을 `SnareCheckSwitch::Param/Value`(Public/SnareCheckSwitch.h)로만 읽는다.
    `FCommandLine::Get`·`FParse::` 직접 호출은 그 헤더 밖에 있으면 안 된다.
    (그 헤더가 `UE_BUILD_SHIPPING` 이면 늘 false 를 돌려준다 — 헤더 자체도 이 검사가 확인한다.)
 2. 화면 디버그 글자(`AddOnScreenDebugMessage`·`DrawDebug*`)는 `#if !UE_BUILD_SHIPPING` 안에 있어야 한다.
종료 코드 0 = 통과, 1 = 위반.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "Source"
HELPER = SRC / "Snarehaven" / "Public" / "SnareCheckSwitch.h"
DIRECT = re.compile(r"FCommandLine::Get|FParse::")
SCREEN = re.compile(r"AddOnScreenDebugMessage|\bDrawDebug\w+\(")
READ = re.compile(r'SnareCheckSwitch::(?:Param|Value)\(\s*TEXT\("(Snare\w+)')


def strip_comment(line: str) -> str:
    i = line.find("//")
    return line if i < 0 else line[:i]


def in_shipping_guard(lines, idx):
    """idx 줄이 `#if !UE_BUILD_SHIPPING` 블록 안인지 (중첩 #if 를 센다)."""
    stack = []
    for line in lines[: idx + 1]:
        s = line.strip()
        if s.startswith("#if"):
            stack.append(s.replace(" ", "") == "#if!UE_BUILD_SHIPPING")
        elif s.startswith("#else") and stack:
            stack[-1] = False
        elif s.startswith("#endif") and stack:
            stack.pop()
    return any(stack)


def main():
    errors = []
    switches = set()
    files = [p for p in SRC.rglob("*") if p.suffix in (".cpp", ".h") and "Tests" not in p.parts]
    for p in files:
        lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
        for i, raw in enumerate(lines):
            line = strip_comment(raw)
            if p != HELPER and DIRECT.search(line):
                errors.append(f"{p.relative_to(ROOT)}:{i+1}: 명령줄 직접 읽기 — SnareCheckSwitch 를 쓸 것")
            if SCREEN.search(line) and not in_shipping_guard(lines, i):
                errors.append(f"{p.relative_to(ROOT)}:{i+1}: 화면 디버그 글자가 UE_BUILD_SHIPPING 가드 밖")
            switches.update(READ.findall(line))
    h = HELPER.read_text(encoding="utf-8")
    if h.count("#if UE_BUILD_SHIPPING") != 3 or h.count("return false;") != 3:
        errors.append("SnareCheckSwitch.h: 세 도우미 모두 출시 빌드에서 `return false;` 여야 한다")
    for e in errors:
        print("위반:", e)
    print(f"결과: {'통과' if not errors else '실패'} (위반 {len(errors)} / 스위치 읽기 이름 {len(switches)}종 / 파일 {len(files)})")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
