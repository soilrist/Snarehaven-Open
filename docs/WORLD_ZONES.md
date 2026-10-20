# WORLD_ZONES — 존 연결 현황표 (M6-00)

> **이 문서는 "지금 존이 어떻게 이어져 있나" 를 한 장으로 모은 것이다.** 값을 정하는 곳이 아니다 —
> 존 이름·연결은 M6-01 이 `DT_ZoneTable` 에, 실제 이동은 M6-02(포탈)·M6-03(왕복)이 코드에 쓴다.
> 근거: `docs/DESIGN.md` 1.3(월드 구조)·8(확장 순서) · `docs/decisions/005-open-world-exploration-pillar.md` · 조각 목록은 `docs/ROADMAP.md` M6 절.
> **범위 밖:** 던전 밀도·작물 숫자·탈것 속도 (M6-04·05·06, 전부 🔴 숫자 대기 — 이 문서에 추천값을 적지 않는다).
> 마지막 갱신: 루프#101 (2026-09-21, 신규). 표의 코드 줄은 **이 시점의 `develop`** 을 말한다.

## A. 지금 있는 맵 4개

`Content/Game/Maps/` 에 `.umap` 은 넷뿐이다 (`find Content -name "*.umap"`).

| 맵 | 종류 | 게임모드 | 지금 들어가는 길 | 나가는 길 |
|---|---|---|---|---|
| `L_Boot` | 부팅 | `ASnarehavenGameMode` (프로젝트 기본, `Config/DefaultEngine.ini:4`) | **프로젝트 기본 맵** — `Config/DefaultEngine.ini:2` `GameDefaultMap`. 방이 해산되면 엔진이 게스트를 여기에 먼저 내려놓기도 한다(`SnareOnlineSubsystem.cpp:595`) | `BeginPlay` 가 바로 로비를 연다 — `SnarehavenGameMode.cpp:68` `OpenLevel(GetNextLevelName())`, `L_Boot -> L_Title`(`SnarehavenGameMode.cpp:38`) |
| `L_Title` | 로비 | `ASnarehavenGameMode` (기본) | `L_Boot` 의 자동 진입(위 칸). **흐름의 종착이다** — `GetNextLevelName` 이 여기서 `NAME_None` 을 준다(`SnarehavenGameMode.cpp:48`) | 로비 버튼 넷(표 B) |
| `L_Hunt` | 사냥존 | `ASnarehavenGameMode` (기본) | 길이 **셋**: ① 로비 `혼자 사냥` → `OpenLevel`(`LobbyWidget.cpp:323`) ② 방 만들기 → 호스트 `ServerTravel("L_Hunt?listen")`(`SnareOnlineSubsystem.cpp:474`) ③ 방 참가 → 게스트 `SetClientTravel`(호스트 주소, `SnareOnlineSubsystem.cpp:515`) | 일시정지 메뉴 `로비로` → `OpenLevel(L_Title)`(`PauseMenuWidget.cpp:230`) · 방 해산(`SnareOnlineSubsystem.cpp:595`) |
| `L_Ranch` | 거점존 | `ARanchGameMode` (레벨 오버라이드, `tools/unreal/setup_ranch_stage.py:96`) | **없음.** 게임 안에서 여기로 가는 코드가 **0줄**이다 — `grep -rn "L_Ranch" Source/` 가 짚는 곳은 캡처 주석 한 줄(`SnarehavenGameMode.cpp:80`)과 검사 코드(`SnarehavenBootSpec.cpp:76`)뿐. 지금까지는 **명령줄로 맵을 직접 띄워서** 봤다(`/Game/Game/Maps/L_Ranch -game …`, 3층 캡처 방식) | 일시정지 메뉴 `로비로`(`PauseMenuWidget.cpp:230`) — 들어갈 길이 없으니 실제로 쓰일 일이 아직 없다 |

**이 표가 M6 의 출발점이다:** 존은 넷인데 **게임 안에서 이어진 것은 셋**이고, 거점존은 끊겨 있다.

## B. 존 사이를 옮기는 코드 (지금 있는 길 전부)

| 길 | 부르는 곳 | 옮기는 코드 | 누가 옮기나 |
|---|---|---|---|
| 부팅 -> 로비 | `ASnarehavenGameMode::BeginPlay`(`SnarehavenGameMode.cpp:59`) | `UGameplayStatics::OpenLevel`(`:68`) | 로컬 (아직 방이 없다) |
| 로비 -> 사냥(혼자) | 로비 `혼자 사냥` 버튼(`LobbyWidget.cpp:320`) | `OpenLevel(HuntLevelName)`(`:323`) | 로컬 (싱글 — 방을 안 연다) |
| 로비 -> 사냥(호스트) | `HandleCreateRoomComplete`(`SnareOnlineSubsystem.cpp:333`) | `TravelToHuntAsServer` -> `World->ServerTravel("L_Hunt?listen")`(`:474`, URL 은 `MakeHuntServerTravelUrl`(`:450`)) | **서버** — 게스트는 엔진이 따라 옮긴다 |
| 로비 -> 사냥(게스트) | `HandleJoinRoomComplete`(`SnareOnlineSubsystem.cpp:414`) | `TravelToJoinedRoom` -> `SetClientTravel(호스트 주소)`(`:515`) | 클라이언트 (맵 이름이 아니라 **호스트 주소**로 간다) |
| 사냥/거점 -> 로비 | 일시정지 `로비로` 버튼(`PauseMenuWidget.cpp:215`) | `LeaveRoom()` 뒤 `OpenLevel(LobbyLevelName)`(`:230`) | 로컬 |
| 방 해산 -> 로비 | `HandleNetworkFailure`(`SnareOnlineSubsystem.cpp:571`, 로비 복귀 판정 `:585`) | **맵을 직접 열지 않는다** — 엔진이 내려놓는 `L_Boot` 에서 위의 자동 진입이 이어 달린다(`:595` 주석) | 엔진 + 로컬 |

**M6-02 가 반드시 볼 칸:** 위에서 `OpenLevel` 을 쓰는 세 줄은 전부 **방 밖**(부팅·로비·방을 떠날 때)이다. 방 안에서 존을 옮기는 길은 `ServerTravel` **하나뿐**이고, 그 옆이 존 URL 이 들어갈 자리다(`SnareOnlineSubsystem.cpp:450`). 방 안에서 클라이언트가 `OpenLevel` 을 부르면 방이 갈라진다 — M6-02 의 합격 기준("클라이언트가 직접 여는 길이 0줄")은 이 구분을 말한다.

## C. 앞으로 이을 존과 방향

DESIGN 1.3 이 정한 순서 그대로다. **새 맵을 몇 장 만들지·언제 만들지는 이 문서가 정하지 않는다.**

| 존 | 무엇인가 | 새 맵이 필요한가 | 맡은 조각 | 지금 상태 |
|---|---|---|---|---|
| 사냥존 | 기존 `L_Hunt` | 아니다 | M6-03 | 있다 (들어가는 길 셋) |
| 거점존 | 기존 `L_Ranch` (DESIGN 3.2) | 아니다 | **M6-03** | 맵은 있고 **길이 없다** — M6 의 첫 목표 |
| 위험구역(던전) | `L_Hunt` 안 한 구역의 **동물 밀도만** 높인다 (결정 005-3) | **아니다** — 새 맵·인스턴스를 만들지 않는다 | M6-04 | 🔴 숫자 대기 (구역 크기·동물 수·재생성 간격) |
| 채집·농사존 | 따로 만들지 않는다 — **거점존 슬롯 확장으로 흡수** (결정 005-2) | 아니다 | M6-05 | 🔴 숫자 대기 (작물 종류·재배 시간·수확량) |
| (탈것) | 존이 아니라 존 사이 이동 편의 (DESIGN 8-3) | 아니다 | M6-06 | 🔴 숫자 대기 (속도·탑승 조건) |

## D. 이후 조각이 고칠 자리

| 조각 | 고칠 파일 | 이 표의 어느 칸이 바뀌나 |
|---|---|---|
| ~~M6-01 `DT_ZoneTable`~~ **(루프#102 완료)** | `Content/Game/Data/World/DT_ZoneTable` · `Source/Snarehaven/Public/Data/ZoneRow.h` · `tools/unreal/create_data_tables.py` | A 의 맵 둘이 **표의 행**이 됐다 — `zone.hunt`=`L_Hunt` · `zone.ranch`=`L_Ranch`, 서로 이어짐. **로비·부팅은 존이 아니라 행이 없다.** `HuntLevelName`·`LobbyLevelName` C++ 상수를 표로 갈아끼우는 것은 M6-02 몫 |
| M6-02 `AZonePortalActor` | `Source/Snarehaven/…/World/` (새 액터) · `SnareOnlineSubsystem`(존 URL) | B 에 "존 -> 존" 한 줄이 늘어난다 |
| M6-03 사냥터 <-> 거점존 | `L_Hunt`·`L_Ranch` 에 포탈 배치 | A 의 `L_Ranch` "지금 들어가는 길" 이 **없음 -> 포탈**로 바뀐다 |
| M6-04·05·06 | 🔴 답이 온 뒤 | C 의 아래 세 줄 |

**이 문서는 코드가 바뀌면 같이 고친다.** 줄 번호가 밀리는 것은 표 A·B 뿐이고, 표 C 는 DESIGN 1.3 을 옮겨 적은 것이라 설계가 바뀔 때만 손댄다.
