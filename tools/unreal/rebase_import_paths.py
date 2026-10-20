"""에셋 가져온 원본 경로 바꾸기 — `_Source/raw/Snare/` → `_Source/raw/Snare/` (재반입 아님, 모양 불변).

실행 (uproject·script 절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd ~/UNREAL/Snarehaven/Snarehaven.uproject \
      -run=pythonscript -script="~/UNREAL/Snarehaven/tools/unreal/rebase_import_paths.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" -log
환경변수 REBASE_DRY=1 이면 바꾸지 않고 세기만 한다. 결과는 로그 `REBASE:` 줄.
"""
import os

import unreal

OLD = "raw/Snare/"
NEW = "raw/Snare/"
DRY = os.environ.get("REBASE_DRY") == "1"
REPORT = "~/UNREAL/Snarehaven/Saved/rebase_report.txt"

reg = unreal.AssetRegistryHelpers.get_asset_registry()
assets = reg.get_assets_by_path("/Game", recursive=True)
changed, skipped, saved = 0, 0, []
for data in assets:
    path = str(data.package_name)
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None:
        continue
    try:
        aid = asset.get_editor_property("asset_import_data")
    except Exception:
        continue
    if aid is None:
        continue
    files = list(aid.extract_filenames())
    if not any(OLD in f for f in files):
        continue
    if DRY:
        changed += 1
        continue
    new_files = [f.replace(OLD, NEW) for f in files]
    for i, nf in enumerate(new_files):
        aid.scripted_add_filename(nf, i, "")
    after = list(aid.extract_filenames())
    if any(OLD in f for f in after):
        skipped += 1
        saved.append("FAIL " + path)
        continue
    asset.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    changed += 1
with open(REPORT, "w") as fh:
    fh.write("changed=%d failed=%d dry=%s\n%s\n" % (changed, skipped, DRY, "\n".join(saved)))
unreal.log_warning("REBASE: changed=%d failed=%d dry=%s" % (changed, skipped, DRY))
