#!/usr/bin/env python3
"""바퀴 기록 감사 — loop.sh 가 바퀴마다 부른다. 표준 라이브러리만.

  render <lap.jsonl>            일꾼의 최종 답 + 실행한 명령 목록을 사람이 읽을 글로 출력
  check  <lap.jsonl> <pre-sha>  이번 바퀴가 STATUS 에 적은 "통과(`인용`)" 이 실제 도구 출력에 있는지 대조.
                                없으면 사유를 출력하고 종료코드 1 (loop.sh 가 alerts.log 에 올림)
  usage  <lap.jsonl>            이번 바퀴의 `턴,새입력토큰,출력토큰,캐시읽기토큰,추정비용` 한 줄 (loop.sh 가 usage.csv 에 붙임).
                                기록이 없으면 빈 칸 `,,,,` — 항상 종료코드 0
  report [usage.csv] [바퀴수]   최근 바퀴(기본 20)의 "일한 바퀴 하나당" 턴·토큰·추정비용, 헛바퀴가 쓴 몫,
                                모델·노력 단계별 비교를 사람이 읽을 글로 출력 (`loop/ctl.sh cost`)
  context <lap.jsonl>           이번 바퀴에서 모델이 호출마다 받은 컨텍스트 한 줄: `첫호출,최대,호출수,입력합,출력합`
                                (loop.sh 가 logs/context.csv 에 붙임 — 가벼운 바퀴가 실제로 줄였는지 재는 숫자)
  ctxreport [context.csv] [바퀴수]  최근 바퀴의 시작·최대 컨텍스트 평균 한 줄 (`loop/ctl.sh cost` 끝에 붙음)
  size   [루트]                 매 바퀴 다시 읽히는 문서가 불었는지: STATUS "지금 상태" 줄 수 · STATUS 전체 줄 수 ·
                                INBOX 글자 수와 종류별 개수. 한도를 넘으면 사유를 출력하고 종료코드 1
  tidy   [루트] [--dry]         STATUS 의 오래된 바퀴 기록과 "지금 상태"에서 넘친 항목을 docs/archive/ 로 옮김
                                (글자 하나도 버리지 않음 · 커밋은 감독이 — `loop/ctl.sh tidy`)
  statelines                    표준입력으로 받은 STATUS 의 "지금 상태" 줄 수 (커밋 훅이 씀)
  selftest                      내장 자료로 render/check/usage/context/report/size/tidy 동작 확인 (SELFTEST_OK)

왜: `--output-format text` 로는 최종 답만 남아 "정말 명령을 돌렸나"를 아무도 대조할 수 없었다.
stream-json 을 남기면 도구 호출·결과가 전부 있으니 인용을 기계로 대조한다 (Verification.md 1).
같은 기록의 마지막 `result` 줄에 턴 수·토큰·추정비용이 있다 — 설정을 바꾼 게 나아졌는지 감이 아니라 숫자로 본다.
추정비용은 API 값으로 환산한 것이라 구독 계정의 실제 청구액이 아니다. 전후 비교용으로만 쓴다.
"""
import csv
import datetime
import json
import os
import re
import subprocess
import sys

PASS_RE = re.compile(r"통과\(`([^`]+)`\)")
EYE_PASS_RE = re.compile(r"3층\s*통과")
IMAGE_EXT = (".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp")
USAGE_COLS = ("turns", "in_tok", "out_tok", "cache_read", "cost_usd")

# 매 바퀴 다시 읽히는 문서의 한도 — 넘으면 size 가 경고, 커밋 훅은 NOW_HARD 에서 거절
NOW_KEEP = 15          # "지금 상태" 권장 줄 수 (tidy 가 이만큼만 남김)
NOW_WARN = 20          # 넘으면 경고
NOW_HARD = 25          # 루프 커밋 훅이 거절하는 줄 수
STATUS_MAX_LINES = 1200
LAPS_KEEP = 30         # tidy 가 STATUS 에 남기는 최근 바퀴 수
INBOX_MAX_CHARS = 12000
HUMAN_KINDS = ("확인", "질문", "승인")   # 루프가 처리하지 않는 종류 — 쌓이면 읽는 값만 든다
LAP_HEAD_RE = re.compile(r"^## #", re.M)   # `## #0412 …` 일꾼 바퀴, `## #감독 …` 감독이 직접 한 일
NOW_HEAD = "## 지금 상태"


def _events(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line.startswith("{"):
                continue
            try:
                yield json.loads(line)
            except ValueError:
                continue


def _content_text(content):
    if isinstance(content, str):
        return content
    if isinstance(content, list):
        return "\n".join(b.get("text", "") for b in content if isinstance(b, dict))
    return ""


def parse(path):
    """→ (최종 답, [명령 문자열], 도구 출력 전체 문자열)"""
    final, cmds, outputs = "", [], []
    for ev in _events(path):
        t = ev.get("type")
        if t == "assistant":
            for b in ev.get("message", {}).get("content", []):
                if b.get("type") == "tool_use":
                    inp = b.get("input") or {}
                    cmds.append(inp.get("command") or inp.get("file_path") or inp.get("pattern") or b.get("name", "?"))
        elif t == "user":
            for b in ev.get("message", {}).get("content", []):
                if b.get("type") == "tool_result":
                    outputs.append(_content_text(b.get("content")))
        elif t == "result":
            final = str(ev.get("result") or "")
    return final, cmds, "\n".join(outputs)


def _squash(s):
    return re.sub(r"\s+", " ", s).strip()


def render(path):
    final, cmds, _ = parse(path)
    print("## 최종 답")
    print(final or "(없음)")
    print(f"\n## 실행한 도구 호출 ({len(cmds)}개)")
    for c in cmds:
        print("$ " + _squash(str(c))[:200])


def status_added_lines(pre_sha):
    diff = subprocess.run(
        ["git", "diff", f"{pre_sha}..HEAD", "--", "docs/STATUS.md"],
        capture_output=True, text=True,
    ).stdout
    return [l[1:] for l in diff.splitlines() if l.startswith("+") and not l.startswith("+++")]


def check_quotes(lines, tool_output):
    """STATUS 에 새로 적힌 통과 인용 중 도구 출력에 없는 것 → 목록"""
    hay = _squash(tool_output)
    missing = []
    for l in lines:
        for q in PASS_RE.findall(l):
            if _squash(q) not in hay:
                missing.append(q)
    return missing


def saw_image(events):
    """이번 바퀴에 그림을 실제로 열어 봤나 — Read 로 그림 파일을 열었거나, 도구 결과에 그림이 들어 있었나"""
    for ev in events:
        t = ev.get("type")
        m = ev.get("message")
        content = m.get("content") if isinstance(m, dict) else None
        if not isinstance(content, list):
            continue
        for b in content:
            if not isinstance(b, dict):
                continue
            if t == "assistant" and b.get("type") == "tool_use":
                fp = str((b.get("input") or {}).get("file_path") or "").lower()
                if b.get("name") == "Read" and fp.endswith(IMAGE_EXT):
                    return True
            elif t == "user" and b.get("type") == "tool_result":
                inner = b.get("content")
                if isinstance(inner, list) and any(isinstance(x, dict) and x.get("type") == "image" for x in inner):
                    return True
    return False


def eye_claim_without_look(lines, looked):
    """STATUS 에 새로 "3층 통과" 를 적었는데 이번 바퀴에 그림을 하나도 안 열었으면 True"""
    return (not looked) and any(EYE_PASS_RE.search(l) for l in lines)


def check(path, pre_sha):
    _, cmds, out = parse(path)
    lines = status_added_lines(pre_sha)
    quotes = sum(len(PASS_RE.findall(l)) for l in lines)
    if quotes and not cmds:
        print(f"통과 인용 {quotes}개인데 도구 호출 기록이 0개 (jsonl 비었음?)")
        return 1
    missing = check_quotes(lines, out)
    if missing:
        print("STATUS 의 통과 인용이 실제 도구 출력에 없음: " + " | ".join(m[:60] for m in missing))
        return 1
    if eye_claim_without_look(lines, saw_image(_events(path))):
        print("STATUS 에 '3층 통과' 라 적었는데 이번 바퀴에 캡처를 하나도 열어 보지 않음 (눈 검사는 그림을 실제로 열었을 때만 통과)")
        return 1
    print(f"인용 {quotes}개 전부 도구 출력과 일치")
    return 0


# ── 문서 살림: 매 바퀴 다시 읽히는 STATUS·INBOX 가 불지 않게 ─────────────────────────

def split_status(text):
    """STATUS → (머리말, 지금 상태 본문 줄들, [바퀴 덩어리]). 지금 상태 절이 없으면 본문은 빈 목록."""
    m = LAP_HEAD_RE.search(text)
    top, laps_text = (text[:m.start()], text[m.start():]) if m else (text, "")
    laps = [b for b in re.split(r"(?m)^(?=## #)", laps_text) if b.strip()]
    i = top.find(NOW_HEAD)
    if i < 0:
        return top, [], laps
    head_end = top.find("\n", i) + 1 or len(top)
    return top[:head_end], top[head_end:].splitlines(), laps


def now_line_count(now_lines):
    return sum(1 for l in now_lines if l.strip() and l.strip() != "---")


def split_now(now_lines, keep=NOW_KEEP):
    """지금 상태 본문을 최상위 항목(`- `) 단위로 잘라 앞쪽 keep 줄만 남긴다 → (남길 줄, 넘친 줄). 항목을 중간에서 자르지 않는다."""
    items, cur = [], []
    for l in now_lines:
        if l.startswith("- ") and cur:
            items.append(cur)
            cur = []
        cur.append(l)
    if cur:
        items.append(cur)
    kept, over, used = [], [], 0
    for idx, it in enumerate(items):
        n = now_line_count(it)
        if not over and (idx == 0 or used + n <= keep):
            kept.extend(it)
            used += n
        else:
            over.extend(l for l in it if l.strip() != "---")
    return kept, over


def inbox_stats(text):
    kinds = {}
    for l in text.splitlines():
        m = re.match(r"- \[.\] ([^(（\s]+)", l)
        if m:
            kinds[m.group(1)] = kinds.get(m.group(1), 0) + 1
    human = sum(n for k, n in kinds.items() if k in HUMAN_KINDS)
    standing = sum(1 for l in text.splitlines() if l.startswith("- [") and "상시" in l)
    return kinds, human, standing


def size_problems(status_text, inbox_text):
    _, now, laps = split_status(status_text)
    probs = []
    n = now_line_count(now)
    if n > NOW_WARN:
        probs.append(f'STATUS "지금 상태" {n}줄 (권장 {NOW_KEEP}줄 이내) — 일꾼이 매 바퀴 읽는다')
    total = status_text.count("\n")
    if total > STATUS_MAX_LINES:
        probs.append(f"STATUS {total}줄 · 바퀴 기록 {len(laps)}개 (한도 {STATUS_MAX_LINES}줄)")
    kinds, human, standing = inbox_stats(inbox_text)
    if len(inbox_text) > INBOX_MAX_CHARS:
        detail = " · ".join(f"{k} {v}" for k, v in sorted(kinds.items(), key=lambda kv: -kv[1]))
        probs.append(f"INBOX {len(inbox_text):,}글자 (한도 {INBOX_MAX_CHARS:,}) — {detail}")
    if human > 10:
        probs.append(f"INBOX 에 사람·감독이 닫아야 할 확인·질문·승인 {human}개가 쌓임")
    if standing:
        probs.append(f"INBOX 에 '상시' 규칙 {standing}줄 — INBOX 는 끝나면 지우는 우편함, 상시 규칙은 CLAUDE.md 로")
    return probs


def _read(path):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def size(root="."):
    probs = size_problems(_read(os.path.join(root, "docs/STATUS.md")), _read(os.path.join(root, "docs/feedback/INBOX.md")))
    if probs:
        print(" / ".join(probs) + " → 감독이 정리 (DIRECTOR.md 문서 살림)")
        return 1
    print("문서 크기 정상")
    return 0


def tidy_plan(status_text, keep_laps=LAPS_KEEP, keep_now=NOW_KEEP):
    """→ (새 STATUS, 보관 파일에 덧붙일 글, 옮긴 바퀴 수, 옮긴 지금-상태 줄 수)"""
    head, now, laps = split_status(status_text)
    kept_now, over_now = split_now(now, keep_now) if now_line_count(now) > keep_now else (now, [])
    old_laps = laps[keep_laps:]
    body = "\n".join(l for l in kept_now if l.strip() != "---").rstrip("\n")
    new_status = head + (body + "\n" if body else "")
    if laps:
        new_status = new_status.rstrip("\n") + "\n\n---\n\n" + "".join(laps[:keep_laps])
    today = datetime.date.today().isoformat()
    archive = ""
    if over_now:
        archive += f"## 지금 상태에서 넘친 것 ({today} 정리)\n" + "\n".join(over_now).rstrip("\n") + "\n\n"
    archive += "".join(old_laps)
    return new_status, archive, len(old_laps), now_line_count(over_now)


def tidy(root=".", dry=False):
    sp = os.path.join(root, "docs/STATUS.md")
    text = _read(sp)
    if not text:
        print("docs/STATUS.md 를 못 읽음")
        return 1
    new_status, archive, n_laps, n_now = tidy_plan(text)
    if not archive.strip():
        print("옮길 것 없음 (바퀴 기록·지금 상태 모두 한도 안)")
        return 0
    ap = os.path.join(root, "docs/archive", f"STATUS-{datetime.date.today().isoformat()}.md")
    # 한 글자도 버리지 않았는지: 옮긴 줄이 전부 새 STATUS 나 보관 글에 있어야 한다
    after = set(new_status.splitlines()) | set(archive.splitlines())
    lost = [l for l in text.splitlines() if l.strip() and l.strip() != "---" and l not in after]
    if lost:
        print(f"중단: 정리하면 {len(lost)}줄이 사라짐 (첫 줄: {lost[0][:60]}) — 아무것도 바꾸지 않음")
        return 1
    print(f"바퀴 기록 {n_laps}개 · 지금 상태 {n_now}줄 → {os.path.relpath(ap, root)} "
          f"(STATUS {text.count(chr(10))}줄 → {new_status.count(chr(10))}줄)")
    if dry:
        print("(--dry: 파일은 바꾸지 않음)")
        return 0
    os.makedirs(os.path.dirname(ap), exist_ok=True)
    with open(ap, "a", encoding="utf-8") as f:
        f.write(archive if archive.endswith("\n") else archive + "\n")
    with open(sp, "w", encoding="utf-8") as f:
        f.write(new_status)
    print(f"다음: git add docs/STATUS.md {os.path.relpath(ap, root)} → 커밋 `docs(status): 오래된 기록 보관`")
    return 0


def usage_fields(events):
    """마지막 result 줄 → (턴, 새 입력 토큰, 출력 토큰, 캐시 읽기 토큰, 추정비용). 없으면 전부 빈 문자열."""
    last = None
    for ev in events:
        if ev.get("type") == "result":
            last = ev
    if last is None:
        return ("", "", "", "", "")
    u = last.get("usage") or {}

    def num(v):
        return v if isinstance(v, (int, float)) and not isinstance(v, bool) else 0

    fresh = num(u.get("input_tokens")) + num(u.get("cache_creation_input_tokens"))
    cost = last.get("total_cost_usd")
    return (
        str(num(last.get("num_turns"))),
        str(fresh),
        str(num(u.get("output_tokens"))),
        str(num(u.get("cache_read_input_tokens"))),
        f"{cost:.4f}" if isinstance(cost, (int, float)) and not isinstance(cost, bool) else "",
    )


def usage(path):
    try:
        fields = usage_fields(_events(path))
    except OSError:
        fields = ("", "", "", "", "")
    print(",".join(fields))


def context_fields(events):
    """호출마다 모델이 받은 컨텍스트(입력+캐시) — 같은 message.id 는 한 번만 센다 (한 호출이 여러 줄로 나뉘어 기록된다)"""
    seen, sizes, out = set(), [], 0
    for ev in events:
        m = ev.get("message")
        if not isinstance(m, dict):
            continue
        u, mid = m.get("usage"), m.get("id")
        if ev.get("type") != "assistant" or not u or mid in seen:
            continue
        seen.add(mid)
        sizes.append(u.get("input_tokens", 0) + u.get("cache_read_input_tokens", 0) + u.get("cache_creation_input_tokens", 0))
        out += u.get("output_tokens", 0)
    if not sizes:
        return "0,0,0,0,0"
    return f"{sizes[0]},{max(sizes)},{len(sizes)},{sum(sizes)},{out}"


def context(path):
    try:
        print(context_fields(_events(path)))
    except OSError:
        print("0,0,0,0,0")


def ctxreport_line(rows, last_n=20):
    """context.csv 줄들 → 최근 바퀴의 시작·최대 컨텍스트 평균 한 줄. 잰 바퀴가 없으면 빈 문자열."""
    vals = []
    for r in rows[-last_n:]:
        try:
            first, peak, calls = int(r.get("first") or 0), int(r.get("peak") or 0), int(r.get("calls") or 0)
        except ValueError:
            continue
        if calls > 0:
            vals.append((first, peak, calls))
    if not vals:
        return ""
    n = len(vals)
    return (f"컨텍스트 (최근 {n}바퀴 평균): 바퀴 시작 {sum(v[0] for v in vals) / n / 1000:.1f}K · "
            f"최대 {sum(v[1] for v in vals) / n / 1000:.1f}K · 호출 {sum(v[2] for v in vals) / n:.0f}번")


def ctxreport(path="logs/context.csv", last_n=20):
    try:
        with open(path, encoding="utf-8", errors="replace", newline="") as f:
            line = ctxreport_line(list(csv.DictReader(f)), last_n)
    except OSError:
        return 0
    if line:
        print(line)
    return 0


def _f(row, key):
    try:
        return float(row.get(key) or "")
    except (TypeError, ValueError):
        return None


def _work(row):
    """commits 칸은 `전체/일한` — 일한 커밋 수"""
    try:
        return int(str(row.get("commits") or "0/0").split("/")[-1])
    except ValueError:
        return 0


def _avg(rows, key):
    vals = [v for v in (_f(r, key) for r in rows) if v is not None]
    return sum(vals) / len(vals) if vals else None


def _fmt(v, unit="", digits=0):
    return "?" if v is None else f"{v:,.{digits}f}{unit}"


def report_lines(rows, last_n=20):
    rows = [r for r in rows if (r.get("account") or "") != "dry"][-last_n:]
    if not rows:
        return ["기록된 바퀴가 없음"]
    measured = [r for r in rows if _f(r, "turns") is not None]
    worked = [r for r in measured if _work(r) > 0]
    idle = [r for r in measured if _work(r) == 0]
    out = [f"최근 {len(rows)}바퀴 (측정값 있는 바퀴 {len(measured)}개 · 일한 바퀴 {len(worked)}개 · 헛바퀴 {len(idle)}개)"]
    if not measured:
        out.append("턴·토큰 측정값이 아직 없음 — 키트 v2.3 이후 바퀴부터 쌓인다")
        return out
    out.append(
        "일한 바퀴 하나당: 턴 " + _fmt(_avg(worked, "turns"), "", 1)
        + " · 출력 " + _fmt(_avg(worked, "out_tok"), " 토큰")
        + " · 새 입력 " + _fmt(_avg(worked, "in_tok"), " 토큰")
        + " · 추정 $" + _fmt(_avg(worked, "cost_usd"), "", 2)
    )
    total = sum(v for v in (_f(r, "cost_usd") for r in measured) if v is not None)
    wasted = sum(v for v in (_f(r, "cost_usd") for r in idle) if v is not None)
    if total > 0:
        out.append(f"헛바퀴가 쓴 몫: 추정 ${wasted:,.2f} / ${total:,.2f} ({wasted / total * 100:.0f}%)")
    groups = {}
    for r in worked:
        groups.setdefault(((r.get("model") or "?"), (r.get("effort") or "기본")), []).append(r)
    if len(groups) > 1:
        out.append("설정별 (일한 바퀴만 — 바퀴 수가 5개 미만이면 믿지 말 것):")
        for (model, effort), rs in sorted(groups.items()):
            out.append(
                f"  {model} / 노력 {effort}: {len(rs)}바퀴 · 턴 " + _fmt(_avg(rs, "turns"), "", 1)
                + " · 추정 $" + _fmt(_avg(rs, "cost_usd"), "", 2)
            )
    out.append("추정비용은 API 값 환산 — 구독 계정의 청구액이 아니라 전후 비교용")
    return out


def report(path="logs/usage.csv", last_n=20):
    try:
        with open(path, encoding="utf-8", errors="replace", newline="") as f:
            rows = list(csv.DictReader(f))
    except OSError:
        print(f"{path} 를 못 읽음 — 루프가 한 바퀴도 안 돌았거나 경로가 다름")
        return 1
    print("\n".join(report_lines(rows, last_n)))
    return 0


def selftest():
    res = {"type": "result", "num_turns": 12, "total_cost_usd": 0.51234,
           "usage": {"input_tokens": 100, "cache_creation_input_tokens": 900,
                     "cache_read_input_tokens": 50000, "output_tokens": 4000}}
    assert usage_fields([{"type": "assistant"}, res]) == ("12", "1000", "4000", "50000", "0.5123"), "usage 칸 계산이 틀림"
    assert usage_fields([{"type": "assistant"}]) == ("", "", "", "", ""), "result 없는 기록에 값을 지어냄"
    assert usage_fields([{"type": "result"}]) == ("0", "0", "0", "0", ""), "빈 result 처리"
    rows = [
        {"account": "dry", "commits": "0/0"},
        {"account": "nc", "model": "sonnet", "commits": "2/1", "effort": "medium", "turns": "20", "in_tok": "1000", "out_tok": "5000", "cost_usd": "1.00"},
        {"account": "nc", "model": "sonnet", "commits": "1/0", "effort": "high", "turns": "30", "in_tok": "1000", "out_tok": "9000", "cost_usd": "3.00"},
        {"account": "nc", "model": "sonnet", "commits": "1/1"},
    ]
    rep = "\n".join(report_lines(rows))
    assert "최근 3바퀴 (측정값 있는 바퀴 2개 · 일한 바퀴 1개 · 헛바퀴 1개)" in rep, rep
    assert "턴 20.0" in rep and "$1.00" in rep, rep
    assert "$3.00 / $4.00 (75%)" in rep, rep
    assert report_lines([{"account": "dry"}]) == ["기록된 바퀴가 없음"]
    # 눈 검사: 그림을 안 열고 "3층 통과" 라 쓰면 잡는다
    read_img = [{"type": "assistant", "message": {"content": [{"type": "tool_use", "name": "Read", "input": {"file_path": "captures/0007_ui_inventory.PNG"}}]}}]
    tool_img = [{"type": "user", "message": {"content": [{"type": "tool_result", "content": [{"type": "image", "source": {}}]}]}}]
    read_txt = [{"type": "assistant", "message": {"content": [{"type": "tool_use", "name": "Read", "input": {"file_path": "docs/STATUS.md"}}]}}]
    assert saw_image(read_img) and saw_image(tool_img) and not saw_image(read_txt), "그림을 열었는지 판정이 틀림"
    assert not saw_image([{"type": "user", "message": "글자뿐인 메시지"}]), "message 가 글자일 때 죽으면 안 됨"
    assert eye_claim_without_look(["검사: 1층 통과 / 3층 통과(레퍼런스 대조: docs/reference/inv.md)"], False)
    assert not eye_claim_without_look(["검사: 1층 통과 / 3층 통과(레퍼런스 대조: docs/reference/inv.md)"], True)
    assert not eye_claim_without_look(["검사: 1층 통과 / 3층 못 함(에디터 꺼짐)"], False), "못 함을 지적함"
    # 문서 살림: 넘친 것만 옮기고 한 줄도 버리지 않는다
    st = "# STATUS\n> 머리말\n\n## 지금 상태 (루프가 매 바퀴 갱신)\n" + "".join(f"- 항목 {i}\n  덧붙인 줄 {i}\n" for i in range(1, 21)) \
        + "\n---\n\n" + "".join(f"## #{n:04d} · 조각 M0-{n}\n- 한 것: {n}\n\n" for n in range(40, 0, -1))
    head, now, laps = split_status(st)
    assert now_line_count(now) == 40 and len(laps) == 40, (now_line_count(now), len(laps))
    new, arch, n_laps, n_now = tidy_plan(st)
    h2, now2, laps2 = split_status(new)
    assert n_laps == 10 and len(laps2) == 30 and laps2[0].startswith("## #0040"), (n_laps, len(laps2))
    assert now_line_count(now2) == 14 and n_now == 26, (now_line_count(now2), n_now)
    assert "- 항목 7\n" in new and "- 항목 8\n" in arch and "## #0010" in arch and "## #0011" in new
    both = set(new.splitlines()) | set(arch.splitlines())
    assert all(l in both for l in st.splitlines() if l.strip() and l.strip() != "---"), "정리하다 줄을 잃음"
    assert tidy_plan(new)[1].strip() == "", "한도 안인데 또 옮기려 함"
    one = "## 지금 상태\n- " + "\n  ".join(f"긴 항목의 줄 {i}" for i in range(30)) + "\n- 둘째\n"
    kept, over = split_now(split_status(one)[1])
    assert len(kept) == 30 and over == ["- 둘째"], "첫 항목은 길어도 통째로 남겨야 함"
    inbox = "# INBOX\n" + "".join(f"- [ ] 확인(루프#{i}, 2026-09-18 10:00) **가정 {i}.** 본문\n" for i in range(12)) \
        + "- [ ] 지시(감독←형 상시 규칙, 2026-09-21 00:00) **검증 확실히.** 삭제: 하지 않음\n"
    probs = " / ".join(size_problems(st, inbox))
    assert "지금 상태" in probs and "40줄" in probs and "확인·질문·승인 12개" in probs and "상시" in probs, probs
    assert size_problems(new, "# INBOX\n- [ ] 지시(감독, 2026-09-21 00:00) **고블린 속도 3.5.**\n") == [], size_problems(new, "")
    out = "✔ 규칙 검사 통과 (경고 0 / 미정 숫자 0)\nPassed: 3, Failed: 0"
    ok = ["검사: 1층 통과(`✔ 규칙 검사 통과 (경고 0 / 미정 숫자 0)`) / 2층 통과(`Passed: 3, Failed: 0`)"]
    bad = ["검사: 1층 통과(`✔ 규칙 검사 통과 (경고 1)`)"]
    assert check_quotes(ok, out) == [], "일치하는 인용을 놓침"
    assert check_quotes(bad, out) == ["✔ 규칙 검사 통과 (경고 1)"], "없는 인용을 통과시킴"
    ev = lambda i, c: {"type": "assistant", "message": {"id": i, "usage": {"input_tokens": 1, "cache_read_input_tokens": c, "output_tokens": 5}}}
    assert context_fields([ev("a", 99), ev("a", 99), ev("b", 199)]) == "100,200,2,300,10", "같은 id 두 줄은 한 번만 세야 함"
    assert context_fields([{"type": "result"}]) == "0,0,0,0,0", "호출 없는 기록"
    ctx = ctxreport_line([{"first": "20000", "peak": "90000", "calls": "30"}, {"first": "0", "peak": "0", "calls": "0"}, {"first": "30000", "peak": "110000", "calls": "50"}])
    assert "바퀴 시작 25.0K" in ctx and "최대 100.0K" in ctx and "최근 2바퀴" in ctx, ctx
    assert ctxreport_line([{"first": "0", "peak": "0", "calls": "0"}]) == "", "잰 바퀴가 없는데 줄을 냄"
    assert check_quotes(["검사: 3층 못 함(에디터 꺼짐)"], "") == [], "인용 없는 줄을 지적함"
    assert _content_text([{"type": "text", "text": "a"}, {"type": "text", "text": "b"}]) == "a\nb"
    print("SELFTEST_OK")


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[:1] == ["render"] and len(a) == 2:
        render(a[1])
    elif a[:1] == ["check"] and len(a) == 3:
        sys.exit(check(a[1], a[2]))
    elif a[:1] == ["usage"] and len(a) == 2:
        usage(a[1])
    elif a[:1] == ["context"] and len(a) == 2:
        context(a[1])
    elif a[:1] == ["ctxreport"] and len(a) <= 3:
        sys.exit(ctxreport(a[1] if len(a) > 1 else "logs/context.csv", int(a[2]) if len(a) > 2 and a[2].isdigit() else 20))
    elif a[:1] == ["report"] and len(a) <= 3:
        sys.exit(report(a[1] if len(a) > 1 else "logs/usage.csv", int(a[2]) if len(a) > 2 and a[2].isdigit() else 20))
    elif a[:1] == ["size"] and len(a) <= 2:
        sys.exit(size(a[1] if len(a) > 1 else "."))
    elif a[:1] == ["tidy"] and len(a) <= 3:
        rest = [x for x in a[1:] if x != "--dry"]
        sys.exit(tidy(rest[0] if rest else ".", dry="--dry" in a))
    elif a[:1] == ["statelines"]:
        print(now_line_count(split_status(sys.stdin.read())[1]))
    elif a[:1] == ["selftest"]:
        selftest()
    else:
        print(__doc__)
        sys.exit(2)
