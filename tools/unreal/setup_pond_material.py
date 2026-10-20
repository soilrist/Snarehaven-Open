"""M19-02 — 연못·강 셰이더(`M_Pond_Water`)와 MI 둘만 다시 만든다 (`setup_sea.py` 의 `_pond_materials` 를 빌려 쓴다).

레벨·바다·낚시 자리는 건드리지 않는다 — 셰이더만 바뀔 때 두 맵을 다시 저장하지 않으려고. 실행:
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_pond_material.py" -unattended -nosplash -log
"""
import os
import sys

import unreal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup_sea  # noqa: E402

if setup_sea.__name__ == "__main__":
    unreal.log_error("[PondMaterial] setup_sea 가 전체 실행으로 불렸다 — 레벨을 건드렸을 수 있다")
    raise SystemExit(1)
made = setup_sea._pond_materials(setup_sea._load(setup_sea.WAVE_TEXTURE))
unreal.log("[PondMaterial] 다시 만듦 ({}): {}".format(__name__, ", ".join(sorted(made))))
