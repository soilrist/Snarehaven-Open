# 검사 모듈: Godot  〔DRAFT — 사람 승인 후 DRAFT 표시 삭제〕

> 공통 원칙은 `docs/conventions/Verification.md`. 여기엔 **Godot 에서 실제로 어떻게** 만 적는다.
> Godot 에디터 MCP 가 연결돼 있으면 MCP 명령, 없으면 `--headless` 명령. 둘 다 안 되면 그 층은 **"못 함"**.

## 1층 — 기계 검사 (커밋 전 필수)
| 검사 | 에디터 MCP | MCP 없을 때 |
|---|---|---|
| 파싱·컴파일 에러 0 | 로그(Output) 조회 | `godot --headless --check-only --path <프로젝트>` (또는 `--script` 로 전 스크립트 로드) |
| 이름·폴더·자리표시 | — | `python3 tools/check_conventions.py --changed --engine godot` |
| 유닛 테스트 (GUT 또는 내장) | 로그로 결과 확인 | `godot --headless --path <프로젝트> -s addons/gut/gut_cmdln.gd -gdir=res://game/scripts/tests` |
| 데이터 무결성 (ID 중복·빈 참조) | GUT 테스트로 만든다 | 동일 |
| 빠진 리소스 참조(Broken Dependency) | `--headless --check-only` 로 못 잡는 것은 GUT 테스트로 리소스 로드해 확인 | 동일 |

## 2층 — 동작 검사
- **GUT (또는 내장 테스트)**로 실제 입력을 넣는다: `Input.parse_input_event()` 로 `InputEventKey`/`InputEventAction` 을 흉내내고, 노드가 `_input`/`_unhandled_input` 에서 실제로 받는지 확인.
- **UI 는 시그널 직접 `emit()` 호출 금지** → 버튼 위에 `InputEventMouseButton` 을 주입해 `gui_input` 경로로 통과시킨다. 다른 컨트롤에 가려져 있으면(`get_viewport().gui_get_focus_owner()` 등으로 확인) 실패해야 정상.
- 테스트 이름: `test_<시스템>_<상황>_<기대>` (GUT 관례: `test_combat_three_hits_knocks_back`)

## 3층 — 눈 검사 (캡처)
- 캡처: 테스트 안에서 `get_viewport().get_texture().get_image().save_png(path)`. 헤드리스에서도 `--headless` + 소프트웨어 렌더러로 캡처 가능(정확한 셰이더 결과는 GPU 필요 — 안 되면 "못 함(헤드리스 렌더 제한)").
- 해상도 고정: 〔1920x1080 / 픽셀아트면 기준 해상도 × 정수배〕
- 체크리스트: Verification.md "캡처 눈 검사 체크리스트"
- **자동으로 잡을 수 있는 것은 코드로:** 분홍색(#FF00FF 근처, 누락 텍스처 기본색) 픽셀 비율, 완전 흰/검은 화면, `Label`/`RichTextLabel` 텍스트에 플레이스홀더 문자열 포함 여부 → GUT 테스트로.

## 알려진 함정
- 에디터가 켜진 상태에서 같은 프로젝트를 `--headless` 로 또 열면 `.godot/` 캐시 잠금 충돌이 날 수 있다 → 에디터를 닫거나 MCP 를 쓰거나 "못 함".
- `--headless` 는 기본적으로 렌더링 백엔드가 제한적 — 3층 캡처의 시각 결과(셰이더·라이팅)는 실제 GPU 렌더와 다를 수 있다, 중요한 비주얼 검사는 에디터에서 직접.
- `.import` 캐시가 오래되면(`--headless --check-only` 전에 임포트가 안 끝난 상태) 리소스 로드가 실패로 보일 수 있다 → 먼저 `godot --headless --path <프로젝트> --quit` 한 번 돌려 임포트를 끝낸다.
