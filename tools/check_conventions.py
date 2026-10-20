#!/usr/bin/env python3
"""
check_conventions.py — 이름·폴더·.meta·자리표시 자동 검사기

규칙 원문은 docs/conventions/ 에 있다. 이 파일은 그걸 "못 어기게" 만드는 검사일 뿐.
규칙 문서를 고치면 아래 PREFIX 표 등도 같이 고칠 것.

사용:
  python3 tools/check_conventions.py                    # 전체 검사 (엔진은 마커 파일로 자동 감지)
  python3 tools/check_conventions.py --changed           # git 에서 바뀐 파일만 (작업 중)
  python3 tools/check_conventions.py --staged            # 커밋에 올린 파일만 (pre-commit 훅이 씀)
  python3 tools/check_conventions.py --engine unreal     # 자동 감지가 틀렸을 때 직접 지정
  python3 tools/check_conventions.py --root unity        # 엔진 프로젝트가 하위 폴더일 때 (자동 탐색도 함)

이름·폴더·자리표시 검사는 Unity/Unreal/Godot 공통. .meta 짝 검사·C# 검사(R05, R08~R11, R14)는
Unity 전용 — 다른 엔진은 아직 정적 검사기가 못 잡는다(에셋이 대부분 바이너리라서), docs/checks/<엔진>.md 참고.

종료 코드: 오류 있으면 1, 없으면 0 (경고는 통과)
외부 패키지 없음 — 파이썬 3.8+ 표준 라이브러리만.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

# ─────────────────────────────────────────────
# 규칙 표 (AssetNaming.md 와 맞출 것)
# ─────────────────────────────────────────────
PREFIX_BY_EXT: dict[str, set[str]] = {
    # 그림
    ".png": {"SPR", "SPS", "T", "UI", "ICO"},
    ".psd": {"SPR", "SPS", "T", "UI", "ICO"},
    ".jpg": {"T", "UI", "ICO"},
    ".jpeg": {"T", "UI", "ICO"},
    ".tga": {"T"},
    ".exr": {"T"},
    ".spriteatlas": {"SA"},
    ".spriteatlasv2": {"SA"},
    ".ttf": {"FNT"},
    ".otf": {"FNT"},
    # 3D / 재질 / 효과
    ".fbx": {"SM", "SK"},
    ".obj": {"SM"},
    ".blend": {"SM", "SK"},
    ".mat": {"M", "MI"},
    ".shader": {"SH"},
    ".shadergraph": {"SH"},
    ".shadersubgraph": {"SH"},
    ".vfx": {"VFX"},
    # 움직임
    ".anim": {"A"},
    ".controller": {"AC"},
    ".overridecontroller": {"AOC"},
    ".playable": {"TLN"},
    ".mask": {"AM"},
    # 소리
    ".wav": {"BGM", "SFX", "VO", "AMB"},
    ".ogg": {"BGM", "SFX", "VO", "AMB"},
    ".mp3": {"BGM", "SFX", "VO", "AMB"},
    ".aif": {"BGM", "SFX", "VO", "AMB"},
    ".aiff": {"BGM", "SFX", "VO", "AMB"},
    ".mixer": {"MIX"},
    # 조립품 / 데이터
    ".prefab": {"PF", "VFX", "TP"},        # 파티클 프리팹은 VFX_, 타일 팔레트는 TP_
    ".asset": {"SO", "TL", "PP", "RT", "FNT"},  # SO 데이터, 타일, 볼륨 프로필, 렌더텍스처, TMP 폰트 에셋
    ".rendertexture": {"RT"},
    ".inputactions": {"IA"},
    ".physicmaterial": {"PHM"},
    ".physicsmaterial2d": {"PHM"},
}
NO_PREFIX_EXT = {".cs", ".unity", ".asmdef", ".asmref", ".md", ".txt", ".json", ".csv", ".uss", ".uxml", ".hlsl", ".cginc"}
TEXTURE_SUFFIXES = {"D", "N", "M", "E", "R", "A"}
BANNED_PARTS = {"new", "untitled", "copy", "final", "test", "temp", "tmp", "asdf"}

# 이름 규칙을 적용하지 않는 폴더 (.meta 짝 검사는 함)
NAMING_EXEMPT_DIRS = {"ThirdParty", "Plugins", "TextMesh Pro", "Samples", "_Sandbox", "Editor Default Resources",
                      "StreamingAssets", "Resources", "AddressableAssetsData", "Settings"}

# ─────────────────────────────────────────────
# 엔진 프로필 — 이름·폴더 검사가 엔진마다 다른 부분만 여기 모은다.
# Unreal/Unity 마커가 함께 있으면 Unreal 을 우선한다 (Unreal 은 언리얼-비-uproject 폴더에도
# Assets 를 둘 수 있지만 반대는 드묾).
# ─────────────────────────────────────────────
ENGINE_PROFILES: dict[str, dict] = {
    "unity": {
        "content_subdir": "Assets",
        "project_dir": "_Project",
        "case": "pascal",
        "prefix_by_ext": PREFIX_BY_EXT,
        "no_prefix_ext": NO_PREFIX_EXT,
        "code_ext": ".cs",
        "meta_pairing": True,
        "placeholder_ext": {".asset"},
    },
    "unreal": {
        "content_subdir": "Content",
        "project_dir": "Game",
        "case": "pascal",
        "prefix_by_ext": {},   # .uasset 은 확장자로 타입을 못 가른다 — 파일명 접두어 검사는 아직 못 함
        "no_prefix_ext": set(),
        "code_ext": None,      # C++/Blueprint 정적 검사는 아직 없음
        "meta_pairing": False,
        "placeholder_ext": set(),  # .uasset 은 바이너리라 텍스트 검사 불가
    },
    "godot": {
        "content_subdir": "",
        "project_dir": "game",
        "case": "snake",
        "prefix_by_ext": {},   # Godot 은 접두어 시스템이 없음 (AssetNaming.md)
        "no_prefix_ext": set(),
        "code_ext": None,      # GDScript 정적 검사는 아직 없음
        "meta_pairing": False,
        "placeholder_ext": {".tres"},
    },
}

ENGINE_MARKERS = {
    "unity": lambda repo: any(c.is_dir() and (c.parent / "ProjectSettings").is_dir()
                               for c in [repo / "Assets", *repo.glob("*/Assets")]),
    "unreal": lambda repo: any(True for _ in [*repo.glob("*.uproject"), *repo.glob("*/*.uproject")]),
    "godot": lambda repo: (repo / "project.godot").is_file() or any(True for _ in repo.glob("*/project.godot")),
}


def detect_engine(repo: Path) -> str | None:
    for name in ("unity", "unreal", "godot"):
        if ENGINE_MARKERS[name](repo):
            return name
    return None


PLACEHOLDER_RE = re.compile(r":\s*-1(\.0+)?\s*$|\bTBD\b", re.MULTILINE)
ASCII_NAME_RE = re.compile(r"^[A-Za-z0-9_.\-]+$")
PART_RE_BY_CASE = {
    "pascal": re.compile(r"^([A-Z][A-Za-z0-9]*|\d{2,})$"),
    "snake": re.compile(r"^([a-z][a-z0-9]*|\d{2,})$"),
}
DIR_RE_BY_CASE = {
    "pascal": re.compile(r"^_?[A-Z][A-Za-z0-9]*$"),
    "snake": re.compile(r"^_?[a-z][a-z0-9_]*$"),
}

RULES = {
    "R01": "AssetNaming - 영어·숫자·밑줄만 (공백·한글 금지)",
    "R02": "AssetNaming - 확장자별 접두어",
    "R03": "AssetNaming - PascalCase 부분 / 번호 두 자리",
    "R04": "AssetNaming - 금지 이름(New, Copy, final, test…)",
    "R05": "FolderStructure - .meta 짝 (원본↔.meta) [Unity 전용]",
    "R06": "FolderStructure - 빈 폴더 금지",
    "R07": "FolderStructure - 우리 에셋은 우리 폴더(project_dir) 안",
    "R08": "CodingStyle - 파일 이름 = 타입 이름 [Unity(C#) 전용]",
    "R09": "CodingStyle - public 필드 대신 [SerializeField] private [Unity(C#) 전용]",
    "R10": "CodingStyle - TODO 주석 금지 [Unity(C#) 전용]",
    "R11": "CodingStyle - namespace 필수 [Unity(C#) 전용]",
    "R12": "DataDriven - 안 채운 숫자(-1/TBD) 목록 — 루프가 DESIGN §6 에 근거와 함께 채울 것",
    "R13": "AssetNaming - AI 생성 에셋 라이선스 기록(_Source/LICENSES.md)",
    "R14": "CodingStyle - 필드 접두어 (m_ 멤버 · k_ 상수 · s_ 스태틱, Unity 공식) [Unity(C#) 전용]",
    "R15": "Reference - Art/UI 에셋이 있으면 레퍼런스 시트(docs/reference/*.md)가 있어야 함",
    "R16": "CodingStyle - 자동 시험은 #if WITH_DEV_AUTOMATION_TESTS 안에 (밖이면 Shipping 빌드가 깨진다) [Unreal 전용]",
}


class Report:
    def __init__(self) -> None:
        self.errors: list[tuple[str, str, str]] = []
        self.warnings: list[tuple[str, str, str]] = []
        self.checked: set[str] = set()
        self.not_checked: dict[str, str] = {}

    def err(self, rule: str, where: str, msg: str) -> None:
        self.errors.append((rule, where, msg))

    def warn(self, rule: str, where: str, msg: str) -> None:
        self.warnings.append((rule, where, msg))


# ─────────────────────────────────────────────
# 찾기
# ─────────────────────────────────────────────
def find_repo_root(start: Path) -> Path:
    try:
        out = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=start, capture_output=True, text=True)
        if out.returncode == 0:
            return Path(out.stdout.strip())
    except FileNotFoundError:
        pass
    return start


def find_content_dirs(repo: Path, engine: str, profile: dict, explicit: str | None) -> list[Path]:
    subdir = profile["content_subdir"]

    def to_content(root: Path) -> Path:
        return (root / subdir) if subdir else root

    if explicit:
        p = to_content(repo / explicit)
        return [p] if p.is_dir() else []

    if engine == "unity":
        found = [c for c in [repo / "Assets", *repo.glob("*/Assets")]
                 if c.is_dir() and (c.parent / "ProjectSettings").is_dir()]
        return found or ([repo / "Assets"] if (repo / "Assets").is_dir() else [])
    if engine == "unreal":
        roots = [p.parent for p in [*repo.glob("*.uproject"), *repo.glob("*/*.uproject")]]
        return [c for r in roots for c in [to_content(r)] if c.is_dir()]
    if engine == "godot":
        roots = [repo] if (repo / "project.godot").is_file() else [p.parent for p in repo.glob("*/project.godot")]
        return [to_content(r) for r in roots]
    return []


def changed_paths(repo: Path, staged_only: bool = False) -> set[Path] | None:
    if staged_only:
        cmds = [["git", "diff", "--name-only", "--cached", "--diff-filter=ACMR"]]
    else:
        cmds = [
            ["git", "diff", "--name-only"],
            ["git", "diff", "--name-only", "--cached"],
            ["git", "ls-files", "--others", "--exclude-standard"],
        ]
    paths: set[Path] = set()
    try:
        for c in cmds:
            out = subprocess.run(c, cwd=repo, capture_output=True, text=True)
            if out.returncode != 0:
                return None
            paths.update((repo / line.strip()).resolve() for line in out.stdout.splitlines() if line.strip())
    except FileNotFoundError:
        return None
    return paths


def is_exempt(rel_parts: tuple[str, ...], project_dir: str) -> bool:
    """예외 폴더는 content 바로 아래 또는 우리 폴더 바로 아래일 때만 (깊은 곳의 같은 이름은 예외 아님)."""
    if not rel_parts:
        return False
    if rel_parts[0] in NAMING_EXEMPT_DIRS:
        return True
    return rel_parts[0] == project_dir and len(rel_parts) > 1 and rel_parts[1] in NAMING_EXEMPT_DIRS


# ─────────────────────────────────────────────
# 검사
# ─────────────────────────────────────────────
def check_name(rep: Report, path: Path, rel: str, profile: dict) -> None:
    ext = path.suffix.lower()
    stem = path.stem if path.is_file() else path.name
    case = profile["case"]
    part_re = PART_RE_BY_CASE[case]
    style = "PascalCase" if case == "pascal" else "snake_case"

    rep.checked.add("R01")
    if not ASCII_NAME_RE.match(path.name):
        rep.err("R01", rel, f"이름에 공백·한글·특수문자: '{path.name}'")
        return

    parts = stem.split("_")
    rep.checked.add("R04")
    bad = [p for p in parts if p.lower() in BANNED_PARTS or re.fullmatch(r"\(\d+\)", p)]
    if bad or re.search(r"\s\(\d+\)$", stem):
        rep.err("R04", rel, f"금지 이름 조각 {bad or ['(n)']}")

    if path.is_dir():
        rep.checked.add("R03")
        if not DIR_RE_BY_CASE[case].match(path.name):
            rep.err("R03", rel, f"폴더는 {style}: '{path.name}'")
        return

    if ext in profile["no_prefix_ext"]:
        if ext in {".cs", ".unity"}:
            rep.checked.add("R03")
            if not all(part_re.match(p) for p in parts):
                rep.err("R03", rel, f"{style} 가 아님: '{stem}'")
        return

    allowed = profile["prefix_by_ext"].get(ext)
    if allowed is None:
        return  # 모르는 확장자·이 엔진은 접두어 검사가 없음 → 건너뜀

    rep.checked.add("R02")
    prefix = parts[0]
    if prefix not in allowed or len(parts) < 2:
        want = " / ".join(sorted(f"{a}_" for a in allowed))
        rep.err("R02", rel, f"접두어 필요: {want}  (현재 '{stem}')")
        return

    rep.checked.add("R03")
    body = parts[1:]
    # 텍스처 접미어(_N, _D …)는 한 글자 대문자 허용
    if prefix == "T" and body and body[-1] in TEXTURE_SUFFIXES:
        body = body[:-1]
    for p in body:
        if p.isdigit() and len(p) < 2:
            rep.err("R03", rel, f"번호는 두 자리: '_{p}' → '_{int(p):02d}'")
        elif not part_re.match(p):
            rep.err("R03", rel, f"{style} 부분이 아님: '{p}' (in '{stem}')")


def check_meta(rep: Report, assets: Path, targets: list[Path], repo: Path) -> None:
    rep.checked.add("R05")
    for t in targets:
        rel = t.relative_to(repo).as_posix()
        if t.name.endswith(".meta"):
            original = t.with_name(t.name[:-5])
            if not original.exists():
                rep.err("R05", rel, ".meta 만 있고 원본이 없음 (원본 삭제 후 .meta 가 남음)")
        else:
            if t.name.startswith(".") or t.name.endswith("~"):
                continue
            if not t.with_name(t.name + ".meta").exists():
                rep.err("R05", rel, ".meta 없음 — 에디터에서 새로고침 후 같이 커밋")


def check_cs(rep: Report, path: Path, rel: str) -> None:
    try:
        src = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return
    stripped = re.sub(r"//.*?$|/\*.*?\*/", "", src, flags=re.MULTILINE | re.DOTALL)

    rep.checked.add("R08")
    types = re.findall(r"\b(?:class|struct|interface|enum|record)\s+([A-Za-z_]\w*)", stripped)
    if types and path.stem not in types:
        rep.err("R08", rel, f"파일 이름 '{path.stem}' 과 같은 이름의 타입이 없음 (찾은 것: {', '.join(types[:3])})")

    rep.checked.add("R11")
    if types and not re.search(r"^\s*namespace\s+[\w.]+", stripped, re.MULTILINE):
        rep.err("R11", rel, "namespace 없음")

    rep.checked.add("R09")
    for i, line in enumerate(stripped.splitlines(), 1):
        if re.match(r"^\s*public\s+(?!const|static|readonly|event|override|abstract|virtual|class|struct|enum|interface|record|sealed|partial)"
                    r"[\w<>\[\],\s]+?\s+_?[a-zA-Z]\w*\s*(=[^;]*)?;\s*$", line) and "(" not in line and "=>" not in line:
            rep.warn("R09", f"{rel}:{i}", f"public 필드 → [SerializeField] private: {line.strip()}")

    rep.checked.add("R14")
    field_re = re.compile(
        r"^\s*(?:\[[^\]]*\]\s*)*"
        r"(?P<mods>(?:(?:private|protected|internal|public|static|readonly|const|volatile|new)\s+)+)"
        r"(?P<type>[\w<>\[\],.?]+(?:\s*<[^;=()]*>)?)\s+(?P<name>[A-Za-z_]\w*)\s*(?:=[^;]*)?;\s*$")
    for i, line in enumerate(stripped.splitlines(), 1):
        if ("(" in line and "=" not in line) or "=>" in line or "{" in line:
            continue  # 메서드·프로퍼티·식 본문 멤버는 필드가 아님
        m = field_re.match(line)
        if not m or " event " in f" {m.group('mods')} " or m.group("type") in {"class", "struct", "enum", "interface", "record"}:
            continue
        mods = m.group("mods").split()
        name = m.group("name")
        where = f"{rel}:{i}"
        if "public" in mods and "const" not in mods and "static" not in mods:
            continue  # public 필드는 R09 가 따로 경고
        if "const" in mods:
            if not re.fullmatch(r"k_[A-Z]\w*", name):
                rep.err("R14", where, f"상수는 k_PascalCase: '{name}'")
        elif "static" in mods:
            if "public" not in mods and not re.fullmatch(r"s_[A-Z]\w*", name):
                rep.err("R14", where, f"static 필드는 s_PascalCase: '{name}'")
        elif not re.fullmatch(r"m_[A-Z]\w*", name):
            rep.err("R14", where, f"멤버 필드는 m_PascalCase: '{name}'")

    rep.checked.add("R10")
    for i, line in enumerate(src.splitlines(), 1):
        if re.search(r"//\s*(TODO|FIXME|HACK)\b", line):
            rep.warn("R10", f"{rel}:{i}", "TODO 주석 → INBOX/ROADMAP 로 옮기기")


def check_placeholders(rep: Report, path: Path, rel: str) -> None:
    rep.checked.add("R12")
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return
    for m in PLACEHOLDER_RE.finditer(text):
        line_no = text.count("\n", 0, m.start()) + 1
        line = text.splitlines()[line_no - 1].strip()
        rep.warn("R12", f"{rel}:{line_no}", f"안 채운 숫자 (DESIGN §6 에 근거와 함께 정할 것): {line}")


def git_lines(repo: Path, *args: str) -> list[str]:
    try:
        out = subprocess.run(["git", *args], cwd=repo, capture_output=True, text=True)
    except FileNotFoundError:
        return []
    return [l.strip() for l in out.stdout.splitlines() if l.strip()] if out.returncode == 0 else []


def check_staged_index(rep: Report, repo: Path, assets_dirs: list[Path], profile: dict) -> set[Path]:
    """커밋에 올린 것 기준으로 .meta 짝을 본다 (디스크가 아니라 index 기준). 검사할 폴더 경로도 돌려준다."""
    index = set(git_lines(repo, "ls-files", "--cached"))
    staged = git_lines(repo, "diff", "--cached", "--name-status")
    roots = [a.resolve().relative_to(repo.resolve()).as_posix() + "/" for a in assets_dirs]
    folders: set[Path] = set()
    if profile["meta_pairing"]:
        rep.checked.add("R05")
    for row in staged:
        status, _, path = row.partition("\t")
        path = path.split("\t")[-1]
        if not any(path.startswith(r) for r in roots):
            continue
        folders.update((repo / path).resolve().parents)
        if not profile["meta_pairing"]:
            continue
        is_meta = path.endswith(".meta")
        pair = path[:-5] if is_meta else path + ".meta"
        pair_is_dir = is_meta and (repo / pair).is_dir()
        if status.startswith("D"):
            if pair in index and not pair_is_dir:
                rep.err("R05", path, f"삭제했는데 짝 '{pair}' 은 커밋에 남음 — 둘 다 지울 것")
        elif not pair_is_dir and pair not in index and not (is_meta and (repo / pair).is_dir()):
            if is_meta and any(i.startswith(pair + "/") for i in index):
                continue  # 폴더의 .meta
            rep.err("R05", path, f"짝 '{pair}' 이 커밋에 없음 — 같이 git add")
    return folders


def run(repo: Path, content_dirs: list[Path], only: set[Path] | None, profile: dict, staged: bool = False) -> Report:
    rep = Report()
    if staged:
        only = set(only or set()) | check_staged_index(rep, repo, content_dirs, profile)

    project_dir = profile["project_dir"]
    for content in content_dirs:
        all_entries = sorted(p for p in content.rglob("*"))
        if only is not None:
            entries = [p for p in all_entries if p.resolve() in only
                       or (p.name.endswith(".meta") and p.with_name(p.name[:-5]).resolve() in only)]
            # 새로 생긴 원본의 .meta 가 없어도 잡히게
            entries += [p for p in only if p.exists() and str(p).startswith(str(content.resolve()) + "/") and p not in entries]
        else:
            entries = all_entries

        if not staged and profile["meta_pairing"]:
            meta_targets = [p for p in entries
                            if not any(part.startswith(".") for part in p.resolve().relative_to(content.resolve()).parts)]
            check_meta(rep, content, meta_targets, repo)

        rep.checked.add("R07")
        for p in entries:
            if p.name.endswith(".meta") or p.name.startswith("."):
                continue
            rel_to_content = p.relative_to(content) if p.is_absolute() and str(p).startswith(str(content)) else None
            if rel_to_content is None:
                try:
                    rel_to_content = p.resolve().relative_to(content.resolve())
                except ValueError:
                    continue
            parts = rel_to_content.parts
            rel = p.resolve().relative_to(repo.resolve()).as_posix()

            # 최상위에 흩어진 우리 파일
            if len(parts) == 1 and p.is_file() and p.suffix.lower() in (set(profile["prefix_by_ext"]) | profile["no_prefix_ext"]):
                rep.warn("R07", rel, f"{profile['content_subdir'] or '엔진 프로젝트'} 바로 아래 파일 → {project_dir}/ 안으로")
            if not parts or parts[0] != project_dir or is_exempt(parts, project_dir):
                continue

            check_name(rep, p, rel, profile)
            if p.is_dir():
                rep.checked.add("R06")
                if not any(c for c in p.iterdir() if not c.name.endswith(".meta") and not c.name.startswith(".")):
                    rep.warn("R06", rel, "빈 폴더")
            elif profile["code_ext"] and p.suffix == profile["code_ext"]:
                check_cs(rep, p, rel)
            elif p.suffix in profile["placeholder_ext"] and "Data" in parts:
                check_placeholders(rep, p, rel)

    # AI 생성 에셋 라이선스
    picked = repo / "_Source" / "picked"
    if picked.is_dir() and any(picked.iterdir()):
        rep.checked.add("R13")
        if not (repo / "_Source" / "LICENSES.md").is_file():
            rep.err("R13", "_Source/LICENSES.md", "생성·외부 에셋이 있는데 라이선스 기록 파일이 없음")

    # 레퍼런스 시트 — 보이는 에셋이 있는데 시트가 하나도 없으면 경고 (Reference.md §2)
    # ponytail: 폴더 단위 존재 검사만. 에셋↔시트 1:1 대응은 감독이 캡처 검수 때 본다
    visible = [p for c in content_dirs for name in ("Art", "UI")
               for p in [c / project_dir / name] if p.is_dir() and any(p.rglob("*"))]
    if visible:
        rep.checked.add("R15")
        if not any((repo / "docs" / "reference").glob("*.md")):
            rep.warn("R15", "docs/reference/", "Art/UI 에셋이 있는데 레퍼런스 시트가 없음 — 대충 비슷하게 만든 흔적")

    # 시험이 #endif 뒤에 붙으면 에디터 빌드는 되고 Shipping(출시) 빌드만 깨진다 (2026-10-14 SnarehavenMinimapTest)
    source = repo / "Source"
    if source.is_dir():
        rep.checked.add("R16")
        for f in source.rglob("*.cpp"):
            lines = f.read_text(encoding="utf-8", errors="ignore").split("\n")
            if not any("WITH_DEV_AUTOMATION_TESTS" in l for l in lines):
                continue
            imp = [i for i, l in enumerate(lines) if "IMPLEMENT_" in l and "AUTOMATION_TEST" in l]
            ends = [i for i, l in enumerate(lines) if l.startswith("#endif")]
            if imp and (not ends or imp[-1] > ends[-1]):
                rep.err("R16", f"{f.relative_to(repo).as_posix()}:{imp[-1] + 1}", "자동 시험이 #if WITH_DEV_AUTOMATION_TESTS 밖 — #endif 를 파일 끝으로")

    for rid in RULES:
        if rid not in rep.checked and rid not in rep.not_checked:
            rep.not_checked[rid] = "해당 파일 없음"
    return rep


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--changed", action="store_true", help="git 에서 바뀐 파일만 검사")
    ap.add_argument("--staged", action="store_true", help="커밋에 올린(staged) 파일만 검사")
    ap.add_argument("--quiet", action="store_true", help="오류가 없으면 한 줄만 출력")
    ap.add_argument("--root", help="엔진 프로젝트 폴더 (저장소 기준 상대경로)")
    ap.add_argument("--engine", choices=["unity", "unreal", "godot"], help="자동 감지가 틀렸을 때 직접 지정")
    args = ap.parse_args()

    repo = find_repo_root(Path.cwd())
    engine = args.engine or detect_engine(repo) or "unity"
    profile = ENGINE_PROFILES[engine]
    content_dirs = find_content_dirs(repo, engine, profile, args.root)

    only = None
    if args.changed or args.staged:
        only = changed_paths(repo, staged_only=args.staged)
        if only is None:
            print("⚠ git 변경 목록을 못 읽음 → 전체 검사로 진행")

    if not content_dirs:
        print(f"⚠ {engine} 엔진 프로젝트 폴더를 못 찾음 — 이름·폴더 검사는 '못 함'")
        rep = run(repo, [], only, profile)
        for rid in [r for r in RULES if r != "R13"]:
            rep.not_checked[rid] = "엔진 프로젝트 폴더 없음"
    else:
        rep = run(repo, content_dirs, only, profile, staged=args.staged)

    placeholders = [w for w in rep.warnings if w[0] == "R12"]
    others = [w for w in rep.warnings if w[0] != "R12"]
    if args.quiet and not rep.errors:
        print(f"✔ 규칙 검사 통과 (경고 {len(others)} / 미정 숫자 {len(placeholders)})")
        return 0

    def show(title: str, rows: list[tuple[str, str, str]]) -> None:
        print(f"\n### {title} ({len(rows)})")
        if not rows:
            print("없음")
            return
        for rule, where, msg in rows:
            print(f"- [{rule}] {where} — {msg}")

    print(f"엔진: {engine} · 검사 대상: {', '.join(a.relative_to(repo).as_posix() for a in content_dirs) or '-'}"
          f"{'  (바뀐 파일만)' if only is not None else ''}")
    show("오류 — 커밋 금지", rep.errors)
    show("경고", others)
    show("안 채운 숫자 (루프가 DESIGN §6 에 근거와 함께 채울 것)", placeholders)

    print("\n### 점검한 규칙")
    for rid in sorted(rep.checked):
        print(f"- {rid} {RULES[rid]}")
    print("\n### 못 본 규칙")
    for rid, why in sorted(rep.not_checked.items()):
        if rid not in rep.checked:
            print(f"- {rid} {RULES[rid]} — {why}")

    print(f"\n결과: {'실패' if rep.errors else '통과'} (오류 {len(rep.errors)} / 경고 {len(others)} / 미정 숫자 {len(placeholders)})")
    return 1 if rep.errors else 0


if __name__ == "__main__":
    sys.exit(main())
