"""레퍼런스 밤 불빛 — `L_Ranch` 등불·집 문간·항구, `L_Hunt` 거점 모닥불·등불에 따뜻한 점광을 세우고 `SnareNightLight` 태그를 단다.

실행 (절대경로):
  ~/UE_5.8/Engine/Binaries/Mac/UnrealEditor-Cmd <절대경로>/Snarehaven.uproject \
      -run=pythonscript -script="<절대경로>/tools/unreal/setup_night_lights.py" -unattended -nosplash \
      "-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False" \
      "-ini:EditorSettings:[/Script/UnrealEd.CrashReportsPrivacySettings]:bSendUnattendedBugReports=False" -log

`USnareDayNightSubsystem` 이 태그로 찾아 밤에만 켠다(DESIGN 3.3 「낮밤」). 레퍼런스 밤 화면(`steam_02`·`steam_08`)은 남보라
속에 모닥불·전구 주황 웅덩이만 밝고, 웅덩이 반경은 캐릭터 키의 약 3배다. 모바일이라 한 화면에 4개 이하·그림자 끔
(2층 `Snarehaven.World.DayNight.NightLights`). 목장 위치는 setup_ranch_buildings.py·setup_harbor.py 배치값,
사냥터는 setup_outposts.py 가 세운 `Outpost_<거점>_Campfire`·`_Lantern_L/R` 위.
재실행 안전: 라벨로 찾아 값만 다시 맞춘다.
"""
import unreal

LABEL = "NightLight_"
TAG = "SnareNightLight"
LAMP_COLOR = unreal.Color(r=255, g=160, b=80, a=255)   # 전구 주황 (시트: 모닥불·전구 점광원 주황)
FIRE_COLOR = unreal.Color(r=255, g=120, b=40, a=255)   # 모닥불은 더 붉은 주황
LAMP_INTENSITY = 80.0  # cd — 밤 해 ×0.35 화면에서 웅덩이 안 풀·길이 주황으로 뜨는 정도(🟡, 루프#275 캡처로 맞춤)
FIRE_INTENSITY = 120.0
RADIUS = 528.0         # cm — 캐릭터 키 176cm 의 3배 (시트 「반경 캐릭터 3배」)
RANCH_LIGHTS = {
    "Lantern_00": (-300.0, -190.0, 200.0),
    "Lantern_01": (-300.0, 190.0, 200.0),
    "HouseA_Door": (520.0, -330.0, 230.0),
    "HouseB_Door": (580.0, 470.0, 230.0),
    "Quay_West": (600.0, -2480.0, 250.0),
    "Quay_Mid": (1400.0, -2480.0, 250.0),
    "Quay_East": (2200.0, -2480.0, 250.0),
    "Ship_Deck": (2613.0, -4180.0, 950.0),
}
HUNT_SOURCES = {"_Campfire": (100.0, True), "_Lantern_L": (200.0, False), "_Lantern_R": (200.0, False)}  # 꼬리 → (높이, 모닥불?)

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def _apply(level, lights):
    """lights = {이름: (x, y, z, 모닥불?)} — 라벨로 찾아 맞추고, 표에 없는 옛 불빛은 지운다."""
    unreal.EditorLoadingAndSavingUtils.load_map(level)
    existing = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
    for name, (x, y, z, fire) in lights.items():
        actor = existing.get(LABEL + name)
        if actor is None:
            actor = actors.spawn_actor_from_class(unreal.PointLight, unreal.Vector(x, y, z))
            actor.set_actor_label(LABEL + name)
        actor.set_actor_location(unreal.Vector(x, y, z), False, False)
        actor.set_editor_property("tags", [unreal.Name(TAG)])
        light = actor.get_component_by_class(unreal.PointLightComponent)
        light.set_mobility(unreal.ComponentMobility.MOVABLE)
        light.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        light.set_editor_property("intensity", FIRE_INTENSITY if fire else LAMP_INTENSITY)
        light.set_editor_property("light_color", FIRE_COLOR if fire else LAMP_COLOR)
        light.set_editor_property("attenuation_radius", RADIUS)
        light.set_editor_property("cast_shadows", False)   # 모바일 점광 그림자는 비싸다
    for label, actor in existing.items():
        if label.startswith(LABEL) and label[len(LABEL):] not in lights:
            actors.destroy_actor(actor)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log_warning("[Night] {} 불빛 {} · 전구 {} cd · 모닥불 {} cd · 반경 {}".format(
        level, len(lights), LAMP_INTENSITY, FIRE_INTENSITY, RADIUS))


_apply("/Game/Game/Maps/L_Ranch", {n: p + (False,) for n, p in RANCH_LIGHTS.items()})

unreal.EditorLoadingAndSavingUtils.load_map("/Game/Game/Maps/L_Hunt")
hunt = {}
for actor in actors.get_all_level_actors():
    label = actor.get_actor_label()
    if not label.startswith("Outpost_"):
        continue
    for tail, (z, fire) in HUNT_SOURCES.items():
        if label.endswith(tail):
            loc = actor.get_actor_location()
            hunt[label[len("Outpost_"):]] = (loc.x, loc.y, z, fire)
_apply("/Game/Game/Maps/L_Hunt", hunt)
