#!/usr/bin/env python3
"""
check_release_words.py — 출시 금지어 검사 (ROADMAP M21-10)

레퍼런스 게임의 이름·지명·NPC 이름이 게임 빌드에 들어가는 파일(`Content/` 에셋 바이트)과
소스(`Source/`·`Config/`·`tools/`)에 남아 있으면 오류. 참고 자료(`_Source/reference/`)·`docs/` 는 보지 않는다.

에셋은 바이너리라 바이트로 찾는다 — 영문은 대소문자 무시 ASCII·UTF-16LE 둘 다, 한글은 UTF-8·UTF-16LE 둘 다
(언리얼은 ASCII 밖 FString 을 UTF-16LE 로 저장한다).

사용:
  python3 tools/check_release_words.py            # 저장소 전체
  python3 tools/check_release_words.py <경로>...  # 그 파일·폴더만 (역검증용)

종료 코드: 금지어가 있으면 1, 없으면 0. 외부 패키지 없음.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

WORDS_ASCII = ["reference", "vinternote", "filemon", "kyrre", "sirola", "heimo"]
WORDS_KO = ["레퍼런스 게임"]
ASSET_SUFFIXES = {".uasset", ".umap"}
TEXT_ROOTS = ["Source", "Config", "tools"]
SELF = Path(__file__).resolve()


def _patterns() -> list[tuple[str, re.Pattern[bytes]]]:
    pats = []
    for w in WORDS_ASCII:
        pats.append((w, re.compile(re.escape(w.encode("ascii")), re.IGNORECASE)))
        pats.append((w, re.compile(re.escape(w.encode("utf-16-le")), re.IGNORECASE)))
    for w in WORDS_KO:
        pats.append((w, re.compile(re.escape(w.encode("utf-8")))))
        pats.append((w, re.compile(re.escape(w.encode("utf-16-le")))))
    return pats


def _targets(repo: Path, args: list[str]) -> list[Path]:
    if args:
        roots = [Path(a).resolve() for a in args]
    else:
        roots = [repo / "Content"] + [repo / r for r in TEXT_ROOTS]
    files = []
    for root in roots:
        if root.is_file():
            files.append(root)
        elif root.is_dir():
            files.extend(p for p in root.rglob("*") if p.is_file())
    return [f for f in files if f.resolve() != SELF and "__pycache__" not in f.parts]


def main() -> int:
    repo = Path(__file__).resolve().parent.parent
    pats = _patterns()
    hits = []
    files = _targets(repo, sys.argv[1:])
    for f in files:
        in_content = "Content" in f.relative_to(repo).parts[:1] if f.is_relative_to(repo) else False
        if in_content and f.suffix not in ASSET_SUFFIXES:
            continue
        data = f.read_bytes()
        found = sorted({w for w, p in pats if p.search(data)})
        if found:
            hits.append((f.relative_to(repo) if f.is_relative_to(repo) else f, found))
    for rel, found in hits:
        print(f"오류 금지어 {rel}: {', '.join(found)}")
    print(f"출시 금지어 검사: 파일 {len(files)}개 · 오류 {len(hits)}")
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
