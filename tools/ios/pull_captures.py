#!/usr/bin/env python3
"""기기(iPhone)에 떨어진 3층 캡처·로그를 앱 컨테이너에서 꺼내 온다 (ROADMAP M15-06).

왜 필요한가: `-SnareCapture=` 는 파일을 **앱 샌드박스 안**에 쓴다. 맥에서 찍을 때는 그 자리가
`Saved/` 라 바로 열리지만, 아이폰에서는 앱마다 따로인 컨테이너 안이라 Finder 로 갈 수 없다.
`xcrun devicectl`(Xcode 15+)이 개발 서명된 앱의 컨테이너를 통째로 내려받을 수 있는 유일한 길이다.

사용:
  python3 tools/ios/pull_captures.py                      → 붙어 있는 아이폰에서 Documents 를 받아
                                                            captures/device/ 에 푼다
  python3 tools/ios/pull_captures.py --list               → 붙어 있는 기기만 보여 준다
  python3 tools/ios/pull_captures.py --device <UDID>      → 기기가 여럿일 때 하나를 고른다
  python3 tools/ios/pull_captures.py --source Documents/Snarehaven/Saved/M15
                                                          → 컨테이너 안의 일부만 받는다
  python3 tools/ios/pull_captures.py --dry-run            → 돌릴 명령만 찍고 끝낸다

받은 뒤 어디를 봐야 하나: 게임 로그의 `검사 캡처 요청: <경로>` 줄이 곧 기기 안 주소다
(`ASnarehavenGameMode::ResolveCapturePath` 가 상대 경로를 `Saved/` 밑으로 옮겨 적는다).

종료 코드: 0 = 받음 · 2 = 붙어 있는 기기가 없음 · 3 = devicectl 이 실패 · 4 = Xcode 도구가 없음
"""
import argparse
import json
import pathlib
import subprocess
import sys
import tempfile

# iOS 번들 ID — `Config/DefaultEngine.ini` 의 `[/Script/IOSRuntimeSettings.IOSRuntimeSettings]` 와 같은 값이다.
# 사람이 출시 전에 바꾸면 여기도 같이 바뀐다 (두 자리에 적힌 같은 답).
BUNDLE_ID = "com.soilrist.snarehaven.ios"

# 컨테이너 루트에서 받아 올 기본 자리. 캡처도 로그도 `Saved/` 밑이고, iOS 는 그 `Saved` 를
# 앱 컨테이너의 Documents 아래로 옮겨 앉힌다 (엔진 `FIOSPlatformFile::ConvertToPlatformPath`).
DEFAULT_SOURCE = "Documents"

# 받은 것을 푸는 자리. 맥에서 찍은 캡처(`captures/<바퀴>_<이름>.png`)와 섞이지 않게 하위 폴더로 나눈다.
DEFAULT_DEST = "captures/device"

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


def _run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True)


def connected_devices() -> list[dict]:
    """붙어 있는(=지금 명령을 받을 수 있는) 기기만 돌려준다.

    `devicectl list devices` 는 **전에 한 번 짝지은 기기까지** 전부 적고 `tunnelState` 칸으로 가른다.
    케이블이 빠진 기기는 `unavailable` 로 나오는 것을 확인했다(루프#197, 아이폰 두 대) — 그 줄에
    명령을 보내면 몇 분씩 매달렸다가 실패하므로 여기서 걸러 낸다. 붙어 있을 때의 칸 값은
    기기가 없어 못 봤으므로, `unavailable` 만 빼고 나머지는 시도한다.
    """
    with tempfile.NamedTemporaryFile("r", suffix=".json", delete=False) as out:
        json_path = out.name
    result = _run(["xcrun", "devicectl", "list", "devices", "--json-output", json_path])
    if result.returncode != 0:
        sys.stderr.write(result.stderr or result.stdout)
        return []
    try:
        payload = json.loads(pathlib.Path(json_path).read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        sys.stderr.write(f"devicectl 출력을 못 읽었다: {error}\n")
        return []

    devices = []
    for device in payload.get("result", {}).get("devices", []):
        state = device.get("connectionProperties", {}).get("tunnelState", "")
        properties = device.get("deviceProperties", {})
        hardware = device.get("hardwareProperties", {})
        if state == "unavailable":
            continue
        devices.append({
            "udid": device.get("identifier", ""),
            "name": properties.get("name", "?"),
            "model": hardware.get("marketingName", "?"),
        })
    return devices


def main() -> int:
    parser = argparse.ArgumentParser(description="아이폰 앱 컨테이너에서 3층 캡처를 꺼낸다")
    parser.add_argument("--device", help="기기 UDID (여러 대가 붙어 있을 때)")
    parser.add_argument("--bundle-id", default=BUNDLE_ID, help=f"앱 번들 ID (기본 {BUNDLE_ID})")
    parser.add_argument("--source", default=DEFAULT_SOURCE, help=f"컨테이너 안 경로 (기본 {DEFAULT_SOURCE})")
    parser.add_argument("--dest", default=DEFAULT_DEST, help=f"풀 자리 (기본 {DEFAULT_DEST})")
    parser.add_argument("--list", action="store_true", help="붙어 있는 기기만 보여 준다")
    parser.add_argument("--dry-run", action="store_true", help="돌릴 명령만 찍는다")
    args = parser.parse_args()

    if _run(["xcrun", "--find", "devicectl"]).returncode != 0:
        sys.stderr.write("devicectl 이 없다 — Xcode 15 이상이 깔려 있어야 한다.\n")
        return 4

    udid = args.device
    if udid is None or args.list:
        devices = connected_devices()
        if args.list:
            if not devices:
                print("붙어 있는 기기가 없다.")
            for device in devices:
                print(f"{device['udid']}  {device['name']}  ({device['model']})")
            return 0 if devices else 2
        if not devices:
            sys.stderr.write(
                "붙어 있는 아이폰이 없다 — 케이블로 잇고 기기에서 '이 컴퓨터를 신뢰' 를 누른 뒤 다시.\n")
            return 2
        if len(devices) > 1:
            sys.stderr.write("기기가 여러 대다 — --device 로 하나를 골라라:\n")
            for device in devices:
                sys.stderr.write(f"  {device['udid']}  {device['name']} ({device['model']})\n")
            return 2
        udid = devices[0]["udid"]
        print(f"기기: {devices[0]['name']} ({devices[0]['model']}) {udid}")

    dest = pathlib.Path(args.dest)
    if not dest.is_absolute():
        dest = REPO_ROOT / dest

    command = [
        "xcrun", "devicectl", "device", "copy", "from",
        "--device", udid,
        "--domain-type", "appDataContainer",
        "--domain-identifier", args.bundle_id,
        "--source", args.source,
        "--destination", str(dest),
    ]

    if args.dry_run:
        print(" ".join(command))
        return 0

    dest.mkdir(parents=True, exist_ok=True)
    result = _run(command)
    sys.stdout.write(result.stdout)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.stderr.write(
            "꺼내기 실패 — 앱이 개발 서명(Xcode 자동 서명)으로 깔려 있어야 컨테이너를 열 수 있다.\n")
        return 3

    pulled = sorted(path for path in dest.rglob("*.png"))
    print(f"받은 자리: {dest}")
    print(f"png {len(pulled)}장")
    for path in pulled[-10:]:
        print(f"  {path.relative_to(dest)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
