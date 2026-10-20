# 엔진 모듈: Godot

> 공통 원칙은 `docs/conventions/AssetNaming.md` · `CodingStyle.md` · `FolderStructure.md` · `DataDriven.md`.
> 여기엔 **Godot 에서 실제로 어떻게** 만 적는다. 검사(테스트·캡처) 방법은 `docs/checks/godot.md`.

## 에셋 이름
출처: [Godot 공식 스타일 가이드 — Project organization](https://docs.godotengine.org/en/stable/tutorials/best_practices/project_organization.html).

**Godot 공식은 접두어 체계를 쓰지 않는다.** Unity/Unreal 표를 그대로 가져오지 않고, **snake_case + 폴더로 종류를 구분**한다.
| 종류 | 이름 규칙 | 예시 |
|---|---|---|
| 씬(`.tscn`) | snake_case, 폴더로 종류 구분 | `enemies/goblin.tscn` |
| 스크립트(`.gd`) | 씬과 같은 이름 (씬 하나에 스크립트 하나 원칙) | `goblin.gd` |
| 리소스(`.tres`) | snake_case, 데이터 접두어 `res_` (이 프로젝트 관례 — 공식 아님) | `res_enemy_goblin.tres` |
| 텍스처 | snake_case + 접미어(Unity 표와 동일 개념) | `rock_d.png`, `rock_n.png` |
| 오토로드(싱글톤) | PascalCase (클래스 취급) | `GameManager.gd` (autoload 이름은 PascalCase 관례) |
| 폴더 | snake_case (공식 권장) | `enemies/`, `ui/` |

이름만 보고 종류를 알아야 한다는 원칙(AssetNaming.md 한 줄 요약)은 Godot 에선 **접두어 대신 폴더 경로**로 만족한다 (`res://game/enemies/goblin.tscn` 자체가 "적·고블린"임을 말해준다).

## 코드 — 이름 (GDScript 공식 스타일 가이드)
출처: [GDScript 스타일 가이드](https://docs.godotengine.org/en/stable/tutorials/scripting/gdscript/gdscript_styleguide.html)

| 대상 | 규칙 | 예 |
|---|---|---|
| 클래스(`class_name`) | PascalCase | `class_name EnemyData` |
| 노드 이름 | PascalCase | `HealthBar` |
| 함수·변수·시그널 | snake_case | `take_damage()`, `move_speed`, `signal health_changed` |
| 상수(`const`) | CONSTANT_CASE | `const MAX_STACK = 99` |
| private 관례(강제 아님, 언더스코어 접두) | `_snake_case` | `var _current_health` |
| enum | PascalCase 타입, 값은 CONSTANT_CASE | `enum DamageType { FIRE, ICE }` |
| 시그널 이름 | 과거형/이벤트 서술 (Unity 의 이벤트 명명과 대응) | `signal health_changed(new_value)`, `signal died` |
| bool | `is_`/`has_`/`can_` 접두 | `is_dead`, `can_dash` |

**Godot 은 `m_`/`k_`/`s_` 접두어를 쓰지 않는다.** private 관례는 언더스코어 하나(`_current_health`)뿐이고 강제되지 않는다 — 엔진이 접근을 막지 않는다(GDScript 에 진짜 private 없음).

## 코드 — Godot 규칙 (실제 버그를 막는 부분)
1. **인스펙터(Inspector) 노출은 `@export`**, 그 외 멤버는 노출하지 않는다.
2. **`_process`/`_physics_process` 에서 `get_node`/`find_child` 반복 호출 금지** → `@onready var` 로 `_ready()` 시점에 한 번 캐싱.
3. **밸런스 숫자를 코드에 박지 않는다** → 데이터(`Resource`/`.tres`)로 (DataDriven.md).
4. **문자열 경로로 노드 찾기 지양**(`get_node("../../Foo")`) → `@export var foo_ref: Node` 로 에디터에서 직접 연결, 또는 그룹(`add_to_group`).
5. **시그널 연결은 `_ready()`에서 `connect`/`.connect()`, 필요하면 `_exit_tree()`에서 해제.** (Godot 4 는 노드 파괴 시 자동 해제되지만, 오토로드가 붙잡고 있는 연결은 수동 해제 필요.)
6. **파괴된 노드 참조는 `is_instance_valid()`로 검사** — Godot 4 는 `null` 비교만으로 부족한 경우가 있다(큐드 삭제 대기 중인 노드).
7. 타이머·트윈은 노드 파괴 시 자동 정리되지만, 오토로드에서 만든 건 직접 `queue_free`/`kill()`.
8. `print`/`push_warning` 은 태그로 시작: `print("[Combat] ...")`. 커밋 전 임시 로그 삭제.
9. 에디터 전용 코드는 `if Engine.is_editor_hint(): return` 으로 가드 (Unity `#if UNITY_EDITOR` 와 대응).

## 코드 — 예시
```gdscript
# health.gd
class_name Health
extends Node

signal health_changed(new_value: float)
signal died

const MIN_HEALTH := 0.0

@export var config: HealthConfig   # 최대 체력·무적 시간은 데이터(res_player_hero_health.tres)에서

var _current: float
var _invincible_until: float

static var alive_count := 0

func is_dead() -> bool:
    return _current <= MIN_HEALTH

func _ready() -> void:
    _current = config.max_health
    alive_count += 1

func take_damage(amount: float) -> void:
    # 무적 중이거나 이미 죽었으면 무시 (사망 이벤트 중복 방지)
    if is_dead() or Time.get_ticks_msec() / 1000.0 < _invincible_until:
        return

    _current = max(MIN_HEALTH, _current - amount)
    _invincible_until = Time.get_ticks_msec() / 1000.0 + config.invincible_seconds
    health_changed.emit(_current)

    if is_dead():
        alive_count -= 1
        died.emit()
```
> `HealthConfig` **클래스**(`Resource` 상속)는 접두어 없음, 그걸로 만든 **리소스 파일**만 관례상 `res_` (`res_player_hero_health.tres`).

## 폴더 구조
```
게임 루트/ (project.godot 이 있는 곳)
├─ game/                    ← ★ 우리 것 전부 (Unity 의 _Project 와 대응)
│  ├─ characters/  enemies/  environment/  ui/  vfx/  audio/
│  ├─ data/                 ← Resource(.tres) 데이터
│  │  ├─ items/  enemies/  skills/  quests/  dialogue/  balance/
│  ├─ scenes/               (레벨 씬)
│  ├─ scripts/
│  │  ├─ autoload/          ← 오토로드(싱글톤)
│  │  └─ tests/             (GUT 또는 내장 테스트)
│  └─ ui/
├─ sandbox/                 ← 실험용. 합격하면 game/ 으로 옮김
└─ addons/                  ← 플러그인만 (수정 금지, 필요하면 복사해서 game/ 로)
```

## 고닷 파일 다루기 (연결이 끊기는 사고 방지)
`.tscn`/`.tres` 는 **텍스트 포맷**(Unity YAML·Unreal 바이너리와 다름) — 그래서 git diff/merge 가 실제로 동작한다. 단:
1. **씬·리소스도 가능하면 에디터(FileSystem 도크)에서 이동·이름 변경** — Godot 4 는 `.tscn` 안의 리소스 경로를 `res://` 절대경로로 저장해서 파일시스템 `mv` 로도 대개 안 끊기지만, 크로스-리소스 `uid://` 참조가 있으면 에디터 경유가 안전.
2. **`.import` 사이드카 파일은 손대지 않는다** (자동 생성, 텍스처·오디오마다 하나) — 삭제되면 에디터가 재생성하지만 임포트 설정(압축 등)이 초기화된다. 커밋은 하되 手 편집 금지.
3. `.tscn`/`.tres` 텍스트 직접 편집은 **급할 때만, 노드 구조는 손대지 말고 값만** — 형식이 단순해서(YAML 유사) Unity 보다 안전하지만 여전히 에디터 저장을 신뢰 소스로 둔다.
4. 바이너리 리소스(`.res`, 임포트된 텍스처의 `.ctex` 캐시)는 diff 불가 — 텍스트(`.tscn`/`.tres`)를 우선 사용.

## 브랜치 합치기 충돌
`.tscn`/`.tres` 는 **텍스트라 git 기본 병합이 대체로 동작**한다(Unity YAML 병합기·Unreal 바이너리 문제가 없음). 그래도 같은 씬의 같은 노드를 동시에 고치면 충돌 마커(`<<<<<<<`)가 남을 수 있다 — 노드 트리 구조가 얽힌 충돌은 손으로 텍스트를 고치지 말고 에디터로 씬을 열어 확인 후 다시 저장.

## 버전
`project.godot` 의 `[application] config/version` 을 `CHANGELOG.md`/태그와 같은 값으로.

## 에디터 연결 (MCP)
- **조회는 자유:** 출력(Output) 로그, 씬 트리, 노드 속성 읽기.
- **변경은 조건부:** 씬 생성·저장, 노드 추가는 — 감독/수동 작업이면 승인 후, 루프면 이번 조각 범위 안에서만.
- **금지:** 사람이 에디터에서 플레이(F5) 중이거나 INBOX 에 `보류:` 가 있을 때 에디터 변경. 프로젝트 설정·플러그인 변경은 INBOX 지시가 있을 때만.
