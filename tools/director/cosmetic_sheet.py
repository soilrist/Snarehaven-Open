"""꾸미기 접촉 시트 — 종류별로 행마다 한 장씩 찍어 한 판(8열 격자)으로 합친다.

감독 사용법: `python3 tools/director/cosmetic_sheet.py --kind hat --resume` → `captures/sheet_hats.jpg`.
한 판을 열어 합격/보정을 INBOX 한 줄에 몰아 적는다. 칸마다 행 이름·등급이 박힌다.
종류: hat · top · bottom · shoes · gloves · backpack · face (DT_ItemTable EquipSlot 기준).
캡처는 기존 스위치 `-SnareCosmetic=<행 이름>` 만 쓴다(게임 코드 0). 에디터가 Play 중이면 쓰지 않는다.
"""
import argparse
import json
import os
import subprocess
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ENGINE = "~/UE_5.8/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"

# 종류 -> (EquipSlot, 시트 파일 이름, 카메라 yaw, 맵, 카메라 거리)
KINDS = {
    "hat": ("Head", "sheet_hats", 180, "L_Ranch", 1300),
    "top": ("OutfitTop", "sheet_tops", 180, "L_Ranch", 600),
    "bottom": ("OutfitBottom", "sheet_bottoms", 180, "L_Ranch", 600),
    "shoes": ("Shoes", "sheet_shoes", 180, "L_Ranch", 600),
    "gloves": ("Gloves", "sheet_gloves", 180, "L_Ranch", 600),
    "backpack": ("Backpack", "sheet_backpacks", 0, "L_Ranch", 600),
    "face": ("FaceAccessory", "sheet_faces", 180, "L_Ranch", 400),
}
COLUMNS = 8
CELL = 256
LABEL_H = 34
GRADE_COLORS = {"Common": (200, 200, 200), "Rare": (110, 170, 255), "Epic": (200, 130, 255), "Legendary": (255, 200, 70)}


def select_rows(rows, kind):
    """꾸미기 행 중 이 종류(EquipSlot)만 표 순서대로."""
    slot = KINDS[kind][0]
    return [r for r in rows if r.get("ItemType") == "Cosmetic" and r.get("EquipSlot") == slot]


def grid_size(count, columns=COLUMNS, cell=CELL, label_h=LABEL_H):
    """칸 수 -> (가로, 세로) 픽셀."""
    rows = (count + columns - 1) // columns
    return columns * cell, rows * (cell + label_h)


def pending(rows, shot_dir, resume):
    """찍어야 할 행. resume 이면 이미 파일이 있는 칸은 건너뛴다."""
    return [r for r in rows if not (resume and os.path.isfile(os.path.join(shot_dir, r["Name"] + ".png")))]


def split_rows(rows, only):
    """(찍을 행, 판에 붙일 행). --only 는 찍는 칸만 줄이고 판은 항상 전체 칸으로 합친다."""
    shoot = [r for r in rows if r["Name"] == only] if only else rows
    return shoot, rows


def label_text(row):
    return "%s %s" % (row["Name"].replace("Item_", ""), row.get("Grade", ""))


def build_sheet(rows, shot_dir, out_path):
    """행마다 shot_dir/<Name>.png 를 격자에 붙인다. 없는 칸은 회색 빈칸. 붙인 칸 수를 돌려 준다."""
    from PIL import Image, ImageDraw
    width, height = grid_size(len(rows))
    sheet = Image.new("RGB", (width, height), (30, 30, 34))
    draw = ImageDraw.Draw(sheet)
    placed = 0
    for i, row in enumerate(rows):
        x, y = (i % COLUMNS) * CELL, (i // COLUMNS) * (CELL + LABEL_H)
        path = os.path.join(shot_dir, row["Name"] + ".png")
        if os.path.isfile(path):
            img = Image.open(path).convert("RGB")
            side = min(img.size)
            left, top = (img.width - side) // 2, (img.height - side) // 2
            sheet.paste(img.crop((left, top, left + side, top + side)).resize((CELL, CELL)), (x, y))
            placed += 1
        else:
            draw.rectangle((x, y, x + CELL - 1, y + CELL - 1), fill=(70, 70, 70))
        draw.text((x + 4, y + CELL + 10), label_text(row), fill=GRADE_COLORS.get(row.get("Grade"), (255, 255, 255)))
    sheet.save(out_path, quality=85)
    return placed


def capture(row, kind, shot_dir, dist=None):
    _, _, yaw, mapname, default_dist = KINDS[kind]
    dist = dist or default_dist
    out = os.path.join(shot_dir, row["Name"] + ".png")
    log = os.path.join(shot_dir, row["Name"] + ".log")
    cmd = [ENGINE, os.path.join(REPO, "Snarehaven.uproject"), "/Game/Game/Maps/" + mapname, "-game", "-RenderOffscreen",
           "-ResX=1280", "-ResY=720", "-unattended", "-nosplash", "-SnareOffline", "-SnareNoGuide",
           "-SnareCosmetic=" + row["Name"], "-SnareCameraDistance=%d" % dist, "-SnareCameraPitch=25",
           "-SnareCameraYaw=%d" % yaw, "-SnareCaptureDelay=8", "-SnareCapture=" + out,
           "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False",
           "-abslog=" + log]
    subprocess.run(cmd, timeout=240)
    return os.path.isfile(out)


def load_rows():
    out = subprocess.run([sys.executable, os.path.join(REPO, "tools", "playfab", "dump_item_rows.py")],
                         check=True, capture_output=True, text=True).stdout
    return json.loads(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kind", required=True, choices=sorted(KINDS))
    ap.add_argument("--resume", action="store_true", help="이미 찍은 칸은 건너뛴다")
    ap.add_argument("--dist", type=int, default=0, help="카메라 거리(cm) 덮어쓰기 — 모자 윗부분이 잘릴 때 키운다")
    ap.add_argument("--only", help="이 행 이름 하나만 다시 찍는다(판은 전체 칸으로 다시 합친다)")
    ap.add_argument("--no-capture", action="store_true", help="찍지 않고 있는 칸만 합친다")
    args = ap.parse_args()
    rows = select_rows(load_rows(), args.kind)
    shoot_rows, rows = split_rows(rows, args.only)
    shot_dir = os.path.join(REPO, "Saved", "CosmeticSheet", args.kind)
    os.makedirs(shot_dir, exist_ok=True)
    if not args.no_capture:
        for row in pending(shoot_rows, shot_dir, args.resume):
            print(row["Name"], "ok" if capture(row, args.kind, shot_dir, args.dist) else "FAIL", flush=True)
    out = os.path.join(REPO, "captures", KINDS[args.kind][1] + ".jpg")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    print("칸 %d / %d -> %s" % (build_sheet(rows, shot_dir, out), len(rows), out))


if __name__ == "__main__":
    main()
