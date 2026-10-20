"""M15-04 레퍼런스 낮 조명 — `L_Hunt`·`L_Ranch` 의 해·하늘빛을 따뜻하게, 후처리 볼륨을 세운다(하늘 대기는 모바일이 못 그려 세우지 않는다).

실행 (uproject·script 절대경로, `.umap` 은 텍스트로 못 고친다):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_world_light.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

근거: `docs/reference/world_environment.md` 「조명·시간: 낮은 따뜻한 노란 태양광 + 긴 부드러운 그림자」. 전에는 흰 해
10 lux + 자동 노출 + 하늘 없음이라 화면이 허옇게 바랬다(2026-09-25 캡처 `now_L_Hunt.png`). 여기 숫자는 전부 배치값(🟡).
재실행 안전: 라벨로 찾아 값만 다시 맞춘다.
"""
import unreal

LEVELS = {"/Game/Game/Maps/L_Hunt": ("MinStageSun", "MinStageSky"),
          "/Game/Game/Maps/L_Ranch": ("RanchSun", "RanchSky")}
SUN_ROTATION = unreal.Rotator(roll=0.0, pitch=-40.0, yaw=35.0)   # 낮은 해 → 긴 그림자
SUN_COLOR = unreal.Color(r=255, g=232, b=196, a=255)              # 따뜻한 노랑
SUN_LUX = 4.0         # 6.0·하늘 2.6 은 그늘/양지 초록 비 0.45 — steam_17 은 약 0.68 (그늘 (0,75~95), 양지 (42,114~128))(w26)
SKY_INTENSITY = 4.5   # 2.0 은 그늘 풀 (0,51,28)·그늘 잎 (0,50,36) 로 레퍼런스보다 어두웠다(w25). 선형 비로 해 ×0.65·하늘 ×1.7
# 하늘빛은 엔진 낮 큐브맵으로 — 하늘 대기 캡처는 모바일에서 거의 검게 찍혀 그림자가 (1,27,13) 로 떴다(w12).
# steam_17 그늘진 풀 (0,75,13)~(5,95,41), 해 받는 풀 (42,114,77) — 그늘이 양지의 약 ⅔ 밝기
SKY_CUBEMAP = "/Engine/MapTemplates/Sky/DaylightAmbientCubemap"
SATURATION = 1.05   # 획 바닥이 이미 짙은 초록 — 1.18 은 형광(w2 캡처)
CONTRAST = 1.06
EXPOSURE_BIAS = 0.0   # 큐브맵 하늘빛이 더해진 뒤 0.4 는 해 받는 풀이 (65,160,73) — steam_17 (42,114,77) 보다 밝았다(w13)


def _actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _find(label):
    for actor in _actors().get_all_level_actors():
        if actor.get_actor_label() == label:
            return actor
    return None


def _find_or_spawn(actor_class, label):
    actor = _find(label)
    if not actor:
        actor = _actors().spawn_actor_from_class(actor_class, unreal.Vector(0.0, 0.0, 500.0))
        actor.set_actor_label(label)
    return actor


def _post_process(volume):
    volume.set_editor_property("unbound", True)
    s = volume.get_editor_property("settings")
    # 모바일에서도 같은 밝기 — 자동 노출이 넓은 풀밭을 보고 화면을 올려 바래게 하던 것을 막는다.
    s.set_editor_property("override_auto_exposure_method", True)
    s.set_editor_property("auto_exposure_method", unreal.AutoExposureMethod.AEM_MANUAL)
    s.set_editor_property("override_auto_exposure_apply_physical_camera_exposure", True)
    s.set_editor_property("auto_exposure_apply_physical_camera_exposure", False)
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", EXPOSURE_BIAS)
    s.set_editor_property("override_color_saturation", True)
    s.set_editor_property("color_saturation", unreal.Vector4(SATURATION, SATURATION, SATURATION, 1.0))
    s.set_editor_property("override_color_contrast", True)
    s.set_editor_property("color_contrast", unreal.Vector4(CONTRAST, CONTRAST, CONTRAST, 1.0))
    s.set_editor_property("override_vignette_intensity", True)
    s.set_editor_property("vignette_intensity", 0.25)
    s.set_editor_property("override_bloom_intensity", True)
    s.set_editor_property("bloom_intensity", 0.2)
    volume.set_editor_property("settings", s)


level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
for level, (sun_label, sky_label) in LEVELS.items():
    level_subsystem.load_level(level)
    sun = _find(sun_label)
    sky = _find(sky_label)
    if not sun or not sky:
        unreal.log_error("[WorldLight] {} 해·하늘빛 없음".format(level))
        raise SystemExit(1)
    sun.set_actor_rotation(SUN_ROTATION, False)
    light = sun.get_component_by_class(unreal.DirectionalLightComponent)
    light.set_mobility(unreal.ComponentMobility.MOVABLE)   # 라이트맵 없이 그림자 — "LIGHTING NEEDS TO BE REBUILT" 방지
    light.set_editor_property("intensity", SUN_LUX)
    light.set_editor_property("light_color", SUN_COLOR)
    light.set_editor_property("atmosphere_sun_light", False)   # 하늘 대기가 없다 — 해가 대기를 찾지 않게

    # 모바일 렌더러는 SkyAtmosphere 를 못 그려 폰 화면에 빨간 영문 경고를 매 프레임 찍는다(2026-09-29 INBOX).
    # 하늘빛은 아래 큐브맵 하늘빛이 맡으므로 대기 액터는 세우지 않고, 남아 있으면 지운다.
    for actor in list(_actors().get_all_level_actors()):
        if isinstance(actor, unreal.SkyAtmosphere):
            _actors().destroy_actor(actor)
    sky_comp = sky.get_component_by_class(unreal.SkyLightComponent)
    sky_comp.set_mobility(unreal.ComponentMobility.MOVABLE)   # 불러올 때 큐브맵을 한 번 찍어 그림자 속을 채운다
    sky_comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    sky_comp.set_editor_property("cubemap", unreal.load_asset(SKY_CUBEMAP))
    sky_comp.set_editor_property("intensity", SKY_INTENSITY)
    sky_comp.recapture_sky()

    _post_process(_find_or_spawn(unreal.PostProcessVolume, level.rsplit("/", 1)[1] + "_PostProcess"))
    level_subsystem.save_current_level()
    unreal.log("[WorldLight] {} 해 {} {} lux · 하늘빛 {} · 채도 {} · 노출 {}".format(
        level, SUN_ROTATION, SUN_LUX, SKY_INTENSITY, SATURATION, EXPOSURE_BIAS))
