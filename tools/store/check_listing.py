#!/usr/bin/env python3
"""스토어 문안 한도 시험 (M94). docs/store/listing_{ko,en}.md 의 각 ``` 칸 글자 수·바이트 수를 센다.

실행: python3 tools/store/check_listing.py   (끝줄 `결과: 통과|실패`, 종료 코드 0|1)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# 칸 제목 앞부분 -> (최대 글자 수, 최대 바이트 수) — None 은 안 센다
LIMITS = [
    ("이름", 30, None), ("Name", 30, None),
    ("부제", 30, None), ("Subtitle", 30, None),
    ("홍보 문구", 170, None), ("Promotional text", 170, None),
    ("설명", 4000, None), ("Description", 4000, None),
    ("키워드", None, 100), ("Keywords", None, 100),
    ("스크린샷", 40, None), ("1 ", 40, None), ("2 ", 40, None), ("3 ", 40, None), ("4 ", 40, None), ("5 ", 40, None),
    ("1번", 40, None), ("2번", 40, None), ("3번", 40, None), ("4번", 40, None), ("5번", 40, None),
]
REQUIRED_KO = ["이름", "부제", "홍보 문구", "설명", "키워드", "지원 URL", "개인정보 처리방침 URL", "저작권"]
REQUIRED_EN = ["Name", "Subtitle", "Promotional text", "Description", "Keywords", "Support URL", "Privacy policy URL", "Copyright"]
LEGAL = "확률형 아이템 포함"
SUPPORT = "https://soilrist.github.io/snarehaven-site/"
PRIVACY = "https://soilrist.github.io/snarehaven-site/privacy/"
EMAIL = "snarehaven@gmail.com"


def parse(path):
    """(제목, 본문) 목록 — 제목은 `##`/`###` 줄, 본문은 그 아래 첫 ``` 칸."""
    out, title, lines, infence = [], None, [], False
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.startswith("```"):
            if infence:
                out.append((title, "\n".join(lines).strip("\n")))
                lines, infence = [], False
            else:
                infence = True
            continue
        if infence:
            lines.append(raw)
        elif raw.startswith("#"):
            title = raw.lstrip("#").strip()
    return out


def limit_for(title):
    for key, ch, by in LIMITS:
        if title.startswith(key):
            return ch, by
    return None, None


def check(path, required, errors):
    fields = parse(path)
    titles = [t for t, _ in fields]
    for need in required:
        if not any(t.startswith(need) for t in titles):
            errors.append(f"{path.name}: 칸 없음 — {need}")
    shots = 0
    for title, body in fields:
        ch, by = limit_for(title)
        if not body.strip():
            errors.append(f"{path.name}: 빈 칸 — {title}")
        if ch is not None and len(body) > ch:
            errors.append(f"{path.name}: {title} {len(body)}자 > {ch}자")
        if by is not None and len(body.encode("utf-8")) > by:
            errors.append(f"{path.name}: {title} {len(body.encode('utf-8'))}바이트 > {by}바이트")
        if title.startswith(("Keywords", "키워드")) and (", " in body or "\n" in body or " ," in body):
            errors.append(f"{path.name}: 키워드에 공백/줄바꿈")
        if re.match(r"^[1-5]", title):
            shots += 1
        print(f"  {path.name} · {title}: {len(body)}자 / {len(body.encode('utf-8'))}바이트")
    if shots != 5:
        errors.append(f"{path.name}: 스크린샷 문구 {shots}개 (5개여야 함)")
    return fields


def main():
    errors = []
    ko = check(ROOT / "docs/store/listing_ko.md", REQUIRED_KO, errors)
    en = check(ROOT / "docs/store/listing_en.md", REQUIRED_EN, errors)
    desc_ko = next((b for t, b in ko if t.startswith("설명")), "")
    if not desc_ko.startswith(LEGAL):
        errors.append(f"한국어 설명이 「{LEGAL}」 로 시작하지 않음")
    for name, fields in (("ko", ko), ("en", en)):
        allbody = "\n".join(b for _, b in fields)
        for need in (SUPPORT, PRIVACY, EMAIL):
            if need not in allbody:
                errors.append(f"listing_{name}: {need} 없음")
        if "©" not in allbody:
            errors.append(f"listing_{name}: 저작권 줄 없음")
    for e in errors:
        print("오류:", e)
    print("결과:", "실패" if errors else f"통과 (오류 0 / 칸 {len(ko)}+{len(en)})")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
