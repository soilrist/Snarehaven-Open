"""`Saved/Generated/Audio/SFX_*.wav` 를 `/Game/Game/Audio/SFX` 에 사운드 웨이브로 들인다 (감독 2026-09-25).

  python3 tools/audio/make_sounds.py   # 먼저 wav 를 만든다
  UnrealEditor-Cmd <절대경로>/Snarehaven.uproject -run=pythonscript -script="<절대경로>/tools/unreal/import_audio.py" \
      -unattended -nosplash (크래시 리포트·MCP 자동 시작 끄는 -ini 인자 둘 — `setup_plateaus.py` 머리말)

`SFX_Amb_*` (파도·바람)와 `LOOP_PREFIXES` (전기톱 공회전, 전기 윙, 릴 감기, 발전기, 라디오 가락)는 반복 재생이라 `looping` 을 켠다.
`SNARE_AUDIO_ONLY=Chainsaw_,Reel_` 처럼 이름 앞부분(`SFX_` 뒤)을 쉼표로 주면 그것만 들인다(남의 자산 안 건드림). 재실행 안전(같은 이름은 덮어쓴다).
"""
import os

import unreal

SRC_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "Saved", "Generated", "Audio"))
DEST_DIR = "/Game/Game/Audio/SFX"
# 전기톱은 공회전(`Idle`)만 — 베기 소리 `Rev` 는 한 번 트는 소리라 되풀이면 폰에서 안 멈췄다(INBOX 2026-10-05 18:20).
LOOP_PREFIXES = ("SFX_Amb_", "SFX_Chainsaw_Idle_", "SFX_ElectricHum_", "SFX_Reel_", "SFX_Generator_Loop_", "SFX_Radio_Loop_")


def main():
    files = sorted(f for f in os.listdir(SRC_DIR) if f.startswith("SFX_") and f.endswith(".wav"))
    only = tuple("SFX_" + p for p in os.environ.get("SNARE_AUDIO_ONLY", "").split(",") if p)
    if only:
        files = [f for f in files if f.startswith(only)]
    tasks = []
    for f in files:
        task = unreal.AssetImportTask()
        task.filename = os.path.join(SRC_DIR, f)
        task.destination_path = DEST_DIR
        task.destination_name = f[:-4]
        task.automated = True
        task.replace_existing = True
        task.save = True
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    made = 0
    for f in files:
        path = "{}/{}".format(DEST_DIR, f[:-4])
        wave = unreal.EditorAssetLibrary.load_asset(path)
        if wave is None:
            unreal.log_error("[Audio] 못 들였다: " + path)
            continue
        if f.startswith(LOOP_PREFIXES):
            wave.set_editor_property("looping", True)
            unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        made += 1
    unreal.log("[Audio] 사운드 웨이브 {}/{} 개 → {}".format(made, len(files), DEST_DIR))


main()
