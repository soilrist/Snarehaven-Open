# 기능 명세: EOS 로그인 · 세션 (접속)

- 상태: **초안 2판** (루프#34 작성 — 1판의 OSSv1 경로가 3층에서 막혀 OSSv2 로 다시 썼다, 결정 004)
- 마일스톤: M3 · 범위(커밋 scope): `net` ⚠️ **목록에 없는 새 범위** — INBOX `질문`(루프#34)으로 추가 제안 중. 반려되면 이 줄과 커밋 scope 를 같이 고친다
- DESIGN 연결: `DESIGN.md` 3.4(멀티플레이·호스트 이탈) · 4.1(EOS P2P 리슨 서버·권위 2단) · 4.4(계정 — EOS 는 세션·P2P 전용, PlayFab 과 연결하지 않음)
- ROADMAP 조각: M3-01 (아래 5장에서 M3-01a/b/c/d 로 쪼갬)
- 결정 기록: `docs/decisions/004-eos-ossv2.md`
- 감독 지시: INBOX 2026-09-19 11:25 — **엔진 내장 EOS 플러그인만 쓴다. 밖에서 받을 것 없음.** 비밀값은 `Config/EOS.ini`(gitignore) 에서만

## 1. 합격 기준 (한 문장)
> "같은 기계에서 `-game` 프로세스 둘을 띄우면 둘 다 EOS Connect 디바이스 ID 로그인에 성공하고, 한쪽이 만든 방을 다른 쪽이 찾아 들어가 **같은 맵에서 서로를 본다**."

M3-01 이 만드는 것은 **길**뿐이다. 동물 상태·포획을 실제로 맞추는 것은 M3-02, 방 해산은 M3-03, 사람이 누를 로비 화면은 M3-04 다.

## 2. 플레이어가 보는 것
- 입력: 이번 조각에는 **없다.** 로그인은 게임 시작 때 자동, 방 만들기/찾기는 콘솔 명령·자동 실행으로만 부른다 (사람 손은 M3-04 로비 UI)
- 화면: 바뀌는 것 없음. 확인은 **로그**로만 한다 (`LogEOSSDK`·`LogOnline`·`LogSnarehaven` 의 `[Online]` 줄 — 새 로그 카테고리를 만들지 않고 기존 것을 쓴다)
- 실패·예외:
  - EOS Artifact 설정이 비었다 → 로그인 시도조차 하지 않고 경고 한 줄. 게임은 그대로 돌아간다(싱글로)
  - 디바이스 ID 가 아직 이 기기에 없다 → `EOS_Connect_CreateDeviceId` 로 한 번 만들고 **한 번만** 재시도. 두 번째도 실패하면 경고 후 포기
  - 방을 못 찾았다 → 빈 결과. 오류 아님
  - 이미 로그인돼 있다 → 다시 부르지 않는다 (엔진이 "Already logged in" 경고를 낸다)

## 3. 숫자
| 이름 | 값 | 단위 | 출처 |
|---|---|---|---|
| 방 최대 인원 | **8** | 명 | DESIGN 3.4 "MVP 서버 규모" — 감독 결정(2026-09-19, 루프#34 질문 답). 최소 시작 인원 없음(혼자도 시작) |
| 로그인 재시도 횟수 | 1 | 회 | 🟢 기술값 — "디바이스 ID 를 만들고 다시" 한 번이면 충분. 늘리면 실패가 늘어질 뿐 |
| 세션 검색 결과 상한 | 50 | 개 | 🟢 기술값 — EOS 기본. 로비 UI(M3-04)가 생기면 화면에 맞춰 줄인다 |

> **새 숫자를 만들지 않았다.** 최대 인원은 감독이 DESIGN 3.4 에 8 로 확정했다(2026-09-19) — M3-01b 시작 가능.
> **방 표시는 공개 속성 `SNAREHAVEN_ROOM=1` 하나로 거른다**(EOS 버킷은 엔진의 `BuildUniqueId` 에 맡긴다) — 감독 확정(2026-09-19, 루프#37 가정 승인). 다른 빌드·다른 게임의 세션과 섞이는 일이 보이면 그때 버킷을 다시 본다.

## 4. 데이터·에셋 · 엔진에 이미 있는 것
| 종류 | 이름 | 있음/만들 것 |
|---|---|---|
| 플러그인 | `OnlineServicesEOSGS` | **켤 것** — 엔진 안에 있지만 `Snarehaven.uproject` 에 아직 없다. OSSv2 쪽 EOS. 결정 004 참고 |
| 플러그인 | `EOSShared`·`SocketSubsystemEOS` | **있음** — 이미 켜져 있고 OSSv2 도 같은 것을 쓴다 (`OnlineServicesEOSGS.cpp:39` 가 `FSocketSubsystemEOS` 를 직접 만든다) |
| 플러그인 | `OnlineSubsystemEOS`(OSSv1) | **끌 것** — 로그인이 안 되는 쪽(결정 004). 끄는 건 M3-01a 에서, 세션까지 OSSv2 로 옮겨진 뒤 |
| 플러그인 | `OnlineServicesEOS`(EAS 포함) | **켜지 않는다** — 켜면 `EOnlineServices::Epic` 이 Epic 계정 로그인 쪽으로 풀린다 (`OnlineServicesEOSModule.cpp:33` 의 우선순위 +1). 게스트 로그인 방침(DESIGN 4.4)과 충돌 |
| 설정(비밀) | `Config/UserEngine.ini` | **있음**(gitignore, 루프#34 가 만듦) — **`Config/EOS.ini` 는 엔진이 절대 읽지 않는다.** EOS 설정은 `Config=Engine` 이라 Engine ini 계층에서만 읽히고 그 계층에 `EOS.ini` 라는 층이 없다. `{PROJECT}/Config/UserEngine.ini` 가 계층의 **마지막(가장 센) 층**이다(`ConfigHierarchy.h:45`). 루프#34 가 `EOS.ini` 의 두 줄을 `grep` 으로 그대로 복사했다(값을 읽거나 옮겨 적지 않았다). 3층 로그의 `LogEOSSDK: [Boot] EOSSDK Platform Properties` 줄로 실제로 읽히는 것을 확인했다. `EOS.ini` 는 죽은 파일 — 지우는 건 사람 몫(🔴 삭제) |
| 설정(공개) | `Config/DefaultEngine.ini` | 고칠 것 — OSSv2 쪽 설정 절. **값이 아니라 스위치라 커밋해도 된다.** ⚠️ OSSv2 가 Artifact 를 어느 절 이름으로 읽는지는 아직 안 캤다(M3-01a 첫 일). OSSv1 용 `[/Script/OnlineSubsystemEOS.EOSSettings]` 절을 그대로 쓸 수 있는지부터 확인한다 |
| 빌드 | `Snarehaven.Build.cs` | 고칠 것 — `OnlineServicesInterface`·`EOSShared`·`EOSSDK` (`OnlineSubsystem`·`OnlineSubsystemUtils` 는 OSSv1 용이라 세션까지 옮긴 뒤 뺀다) |
| C++ 서브시스템 | `USnareOnlineSubsystem` (`Source/Snarehaven/Public/Online/`) | 만들 것 — `UGameInstanceSubsystem` |
| 맵 | `L_Hunt` | **있음** — 세션이 실제로 도는 무대는 사냥터 (목장은 개인 소유라 동기화 안 함, DESIGN 3.2) |

### 엔진 코드를 읽고 알아낸 것 (루프#34 — 다음 바퀴가 다시 캐지 않게)
1. **OSSv1 로는 이 엔진에서 게스트 로그인이 안 된다.** 3층에서 직접 부딪힌 벽이다:
   `LogEOSSDK: Error: LogEOSConnect: Invalid parameter EOS_Connect_LoginOptions.UserLoginInfo reason: must not be null`.
   `UserManagerEOS.cpp` 의 `UserLoginInfo` 대입이 전부 `#if ADD_USER_LOGIN_INFO` 안이고, 그 값은
   `OnlineSubsystemEOS.Build.cs:45` 에서 전 플랫폼 `false` 다. 자세한 근거와 대안 비교는 **결정 004**.
2. **OSSv2 는 같은 자리를 채워 준다.** `AuthEOSGS.cpp:117` 의 표가 `DeviceIdAccessToken` 에
   `EEOSConnectTranslationFlags::DisplayName` 을 달아 두고, `:193` 이 `DisplayName = "PlatformUser"` 를
   넣는다. 조건부 컴파일 없음.
3. **로그인 부르는 모양**(`Auth.h:158`·`AuthEOSGS.cpp:578`):
   `CredentialsType = LoginCredentialsType::ExternalAuth`,
   `CredentialsToken` 에 `FExternalAuthToken{ Type = ExternalLoginType::DeviceIdAccessToken, Data = "" }`,
   `PlatformUserId` 는 0번 로컬 유저. 이 조합일 때만 `LoginConnectImpl` 로 간다.
4. **디바이스 ID 는 OSSv2 도 안 만들어 준다.** 엔진 `Plugins/Online/` 어디에도
   `EOS_Connect_CreateDeviceId` 호출이 없다(grep 확인). 우리가 부른다 — 핸들은
   `IEOSSDKManager::Get()->GetActivePlatforms()`(EOSShared 공개 헤더) 에서 꺼내
   `EOS_Platform_GetConnectInterface`.
5. **P2P 는 OSS 버전과 무관하다.** `NetDriverEOS` 는 `SocketSubsystemEOS` 플러그인에 있고
   OSSv2 도 그것을 만든다(`OnlineServicesEOSGS.cpp:39`). 접속 문자열은
   `FOnlineServicesEOSGS::GetResolvedConnectString` 이 `[EOS:<PUID>]` 모양으로 준다.

## 5. 조각 나누기
1. **M3-01a** EOS Connect 디바이스 ID 로그인 (OSSv2) — 플러그인 켜기 + `Build.cs` + `USnareOnlineSubsystem`
   (`Initialize` 에서 `IAuth::Login`, 실패 1회에 한해 `EOS_Connect_CreateDeviceId` 후 재시도).
   합격: **3층 = `-game` 로그에 로그인 성공 줄과 PUID**, 2층 = 설정이 비었을 때 로그인을 시도하지 않는 것.
2. **M3-01b** 세션 만들기·찾기 — `ISessions`(`SessionsEOSGS`) 로 `CreateSession`/`FindSessions`/`JoinSession`.
   **🔴 방 최대 인원 값이 오기 전에는 시작하지 않는다**(3장).
3. **M3-01c** 두 클라이언트 접속 — 같은 맥의 두 프로세스는 **같은 디바이스 ID·같은 PUID** 라(루프#37 확인)
   EOS P2P 로는 자기 자신에게 붙는 꼴이 된다. 그래서 둘로 나눈다 — 감독 결정(2026-09-19):
   - **c1 (지금)** 호스트가 `?listen` 으로 `L_Hunt`, 게스트는 **`127.0.0.1` 직접 IP** 로 접속(EOS P2P 를 타지 않는다).
     EOS 방 만들기·참가(b)는 그대로 두고, 접속·트래블·리플리케이션 통로만 증명한다. 합격: 두 로그(`-log=` 로 분리)의 접속 줄 +
     호스트 로그에 게스트 `PlayerController` 생성 줄. **계정을 나눌 방법을 찾는 데 바퀴를 쓰지 않는다.**
   - **c2 (보류)** 진짜 EOS P2P `[EOS:<PUID>]` 접속은 **기기가 둘** 있어야 한다. 사용자가 두 번째 기기(맥·폰)를 주면 그때.
     M3-01d·M3-02 는 c2 를 기다리지 않는다.
4. **M3-01d** OSSv1 `OnlineSubsystemEOS` 플러그인 끄기 + 남은 참조 정리. b·c1 이 끝난 뒤에만.

## 6. 검사 방법
- 1층(기계): `Build.sh SnarehavenEditor Mac Development` **경고 0** · `tools/check_conventions.py`
- 2층(동작): `Snarehaven.Online.*` 자동화 테스트 — 설정이 비면 로그인을 시도하지 않는다 /
  우리가 넘기는 `ExternalLoginType`·`LoginCredentialsType` 이름이 엔진이 아는 것과 같다(오타 잡기).
  **실제 EOS 왕복은 2층에서 하지 않는다**(네트워크가 있어야 하는 검사는 테스트가 아니라 3층이다)
- 3층(눈·로그): `-game` 실행 후 `~/Library/Logs/Snarehaven/Snarehaven.log` 에서
  `LogSnarehaven`(`[Online]`)·`LogEOSSDK`·`LogOnlineServices` 줄. **캡처가 아니라 로그가 증거다**
  (화면이 안 바뀌는 조각이라)

## 7. 하지 않을 것
- 로비 화면·방 목록 UI (M3-04)
- 동물·플레이어 리플리케이션, 포획 RPC (M3-02)
- 호스트 이탈 처리·방 해산 (M3-03)
- **PlayFab 계정과 EOS 계정 잇기** — DESIGN 4.4 가 "MVP 는 두 로그인을 연결하지 않음" 으로 못 박았다
- Epic Account Services(EAS) 로그인 — `OnlineServicesEOS` 플러그인을 켜지 않는 것으로 끝난다. 게스트 로그인이 방침(DESIGN 4.4)이라 Epic 계정 창을 띄우지 않는다
- 전용 서버 (DESIGN 4.1 "안정화되면 검토")
