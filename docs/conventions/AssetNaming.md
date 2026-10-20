# 에셋 이름 규칙 (공통 원칙)

> 접두어 표·케이스(PascalCase/snake_case)는 엔진마다 다르다 → **`docs/engines/<엔진>.md` "에셋 이름" 절이 진짜 표.**
> 여기엔 세 엔진 다 통하는 원칙만 적는다.
> 자동 검사: `tools/check_conventions.py --engine <엔진>`.

## 한 줄 요약
**이름만 보고 "뭐인지·누구 건지·어떤 버전인지" 알 수 있게.**
- Unity/Unreal: `접두어_이름_변형_접미어` (예 `T_Weapon_Sword_N`, `PF_Enemy_Goblin_Elite`)
- Godot: 접두어 대신 **폴더 경로**로 종류를 구분 (예 `enemies/goblin.tscn`) — 자세한 표는 `docs/engines/godot.md`.

**이름(또는 이름+경로) = ID.** 검색 한 번으로 관련 에셋이 다 걸린다.
- `Goblin` 검색 → 그 대상의 그림·모션·조립품·데이터가 한 번에 줄 선다.
- 데이터 ID 와 1:1: 에셋 이름 ↔ `id = "enemy.goblin"` (DataDriven.md). 이름에서 ID 를, ID 에서 이름을 기계적으로 만들 수 있다.

## 기본 규칙
1. **케이스는 엔진 컨벤션을 따른다** (Unity/Unreal 은 PascalCase, Godot 은 snake_case) — 표는 `docs/engines/<엔진>.md`.
2. **영어·숫자·밑줄만.** 공백·한글·특수문자 금지. 왜: 빌드·깃·스크립트에서 깨진다.
3. 번호는 **두 자리** `_01`, `_02`. 왜: 정렬했을 때 10이 2보다 앞에 오는 문제 방지.
4. `New Material`, `Untitled`, `Copy`, `(1)`, `final`, `test` 같은 이름 금지.
5. **가운데 이름(또는 폴더) = `분류_대상`** (`Enemy_Goblin`, `Player_Hero` / Godot 는 `enemies/goblin`). 같은 대상의 에셋끼리는 이 부분을 **똑같이** — 그래야 데이터 ID 와 1:1 로 맞는다. 배경·UI 처럼 대상이 없는 에셋은 `분류_이름`.

## 변형(Variant) 이름
색·강화 단계·상태 차이는 뒤에 붙인다: `_Red`, `_Elite`, `_Broken`, `_Lv2`.
**원본에는 변형을 안 붙인다.** (`PF_Enemy_Goblin` 원본 → `PF_Enemy_Goblin_Elite` 변형)

## AI 생성 이미지 (ComfyUI 등)
- **생성 전에 레퍼런스 시트**(`docs/reference/<대상>.md`, Reference.md §2)가 있어야 한다. 프롬프트는 시트의 실루엣·비율·색·규격을 그대로 옮긴다.
- 원본 대량 생성물은 `_Source/raw/` (깃 제외) → **여러 시드 중 시트와 가장 가까운 것**만 `_Source/picked/` → 가공(배경 제거·크기 규격 맞춤·팔레트·픽셀화) 끝난 것만 `Assets/_Project/Art/` 로 **이름을 규칙대로 바꿔서** 들인다. 가공 안 된 생성물·자리표시 도형을 `Art/` 에 넣지 않는다.
- 생성 조건(프롬프트·시드·모델)은 같은 이름의 `.txt` 를 `_Source/picked/` 에 남긴다. 왜: 같은 스타일로 다시 만들 수 있어야 한다.
- **들이기 전에 그 모델·도구의 상업 이용 가능 여부를 확인**하고 `_Source/LICENSES.md` 에 "모델/도구 이름 · 라이선스 · 상업 이용 가능 여부 · 확인 날짜" 한 줄. 확인 안 된 모델로 만든 건 `_Sandbox/` 에만. 외부에서 받은 에셋(에셋스토어·무료 사이트)도 같은 파일에 출처를 적는다.
