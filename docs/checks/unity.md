# 검사 모듈: Unity  (조선제일검 루프에서 검증됨, 2026-09)

> 공통 원칙은 `docs/conventions/Verification.md`. 여기엔 **Unity 에서 실제로 어떻게** 만 적는다.
> Unity MCP 가 연결돼 있으면 MCP 명령, 없으면 배치 모드 명령. 둘 다 안 되면 그 층은 **"못 함"**.

## 1층 — 기계 검사 (커밋 전 필수)
| 검사 | Unity MCP | MCP 없을 때 |
|---|---|---|
| 에셋 새로고침 후 컴파일 에러 0 | `refresh_unity` → `read_console` (Error 만) | `Unity -batchmode -quit -projectPath <경로> -logFile -` 에서 `error CS` 검색 |
| 이름·폴더·.meta 짝·자리표시 | — | `python3 tools/check_conventions.py --changed` |
| EditMode 테스트 | `run_tests` (EditMode) | `Unity -batchmode -runTests -testPlatform EditMode -testResults logs/editmode.xml` |
| 데이터 무결성 (ID 중복·빈 참조) | EditMode 테스트 `DataIntegrityTests` 로 만든다 | 동일 |
| 빠진 스크립트 참조(Missing Script) | 씬·프리팹 검사 EditMode 테스트로 만든다 | 동일 |

## 2층 — 동작 검사
- **PlayMode 테스트**로 실제 입력을 넣는다: Input System 의 `InputTestFixture` 로 키·버튼 누름을 흉내.
- **UI 는 `onClick.Invoke()` 금지** → `EventSystem` 에 포인터 이벤트 주입 (`ExecuteEvents.Execute(..., pointerClickHandler)` 를 **레이캐스트로 찾은 대상**에). 레이캐스트로 안 찾아지면 = 가려진 버튼 = 실패.
- 테스트 이름: `<시스템>_<상황>_<기대>` (`Combat_ThreeHits_KnocksBack`)

## 3층 — 눈 검사 (캡처)
- Game 뷰 캡처: Unity MCP 스크린샷 또는 `ScreenCapture.CaptureScreenshot` 을 PlayMode 테스트 끝에.
- 해상도 고정: 〔1920x1080 / 픽셀아트면 기준 해상도 × 정수배〕
- 체크리스트: Verification.md "캡처 눈 검사 체크리스트"
- **자동으로 잡을 수 있는 것은 코드로:** 분홍색(#FF00FF 근처) 픽셀 비율, 완전 흰/검은 화면, TMP 텍스트에 `TBD`·`New Text` 포함 여부 → PlayMode 테스트로.

## 알려진 함정
- 에디터가 켜진 상태에서 배치 모드 실행 불가(프로젝트 잠김) → MCP 를 쓰거나 "못 함".
- 컴파일 직후 도메인 리로드 중 `read_console` 은 빈 결과가 나올 수 있다 → 새로고침 후 몇 초 기다렸다 다시 읽기, 두 번 비면 "못 함".
- Force Text 직렬화가 아니면 YAML 병합이 안 된다 → M0 에서 확인.
