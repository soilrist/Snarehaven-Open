# 출시 서류 준비 (M4-07 · 루프#82, 2026-09-21 · M7-08 문서 몫 루프#99, 2026-09-21 · M8-07 분석 이벤트 루프#115, 2026-09-22)

> **무엇을 적어야 하는지를 우리 코드 기준으로 모은 한 장.** 개인정보처리방침 문안·스토어 설문 답변을 여기서 만든다.
> **루프 몫은 이 문서까지다.** 웹 게시·심사 제출·연락처·주소·계정 만들기는 사람 몫(Autonomy 🔴 3).
> 값이 바뀌면 여기부터 — 표 A·B 가 개인정보처리방침과 두 스토어(구글플레이 데이터 보안 · Apple App Privacy) 답변의 **원본**이다.

## 표 A — 우리가 실제로 다루는 데이터 (코드에서 확인한 것만)

| 데이터 | 무슨 값인가 | 어디로 가나 | 코드 근거 |
|---|---|---|---|
| 기기 식별자 | `FPlatformMisc::GetLoginId()` — 앱이 처음 켤 때 만든 무작위 GUID 를 기기 안에 저장해 다시 쓰는 값(iOS 는 엔진 저장값 `MachineId`, 안드로이드는 앱 내부 파일 `login-identifier.txt`). 광고 ID·IDFV 가 아니다 |  PlayFab 로그인 요청 본문(iOS `DeviceId` · Android `AndroidDeviceId` · 데스크톱 `CustomId`) | `Source/Snarehaven/Private/Backend/SnareBackendSubsystem.cpp:83`, 같은 파일 `:155~175` |
| 기기 식별자 (EOS) | EOS SDK 가 기기에서 직접 꺼낸다 — **우리 코드는 빈 토큰만 넘긴다** | Epic Online Services (Connect 게스트 로그인) | `Source/Snarehaven/Private/Online/SnareOnlineSubsystem.cpp:87~98` |
| PlayFab 계정 ID (`PlayFabId`) | 로그인 응답으로 받는 익명 계정 번호 | PlayFab 에 저장 · 기기 안 캐시 파일에도 적힌다 | `SnareBackendSubsystem.cpp:178~190`, `Public/Backend/SnarePlayerSnapshot.h:31` |
| EOS 계정 ID (PUID) | EOS 익명 계정 번호 | 같은 방(세션)에 있는 다른 플레이어에게 보인다 | `SnareOnlineSubsystem.cpp:144` |
| 닉네임 (표시 이름) | 플레이어가 직접 적는 3~12자 이름(한글·영문·숫자, 금칙어는 서버가 거절) | PlayFab Player Data `nickname` · PlayFab 표시 이름, 같은 방 호스트에게 보내 다른 플레이어 머리 위에 뜬다 | `SnareBackendSubsystem.cpp` `SubmitNickname`·`UpdateDisplayName` · `SnarehavenCharacter.cpp` `Server_ReportNickname` · `docs/DESIGN.md` 닉네임 줄 |
| EOS SDK 성능 진단 | EOS SDK 가 **스스로 신고한** 성능 데이터(계정에 연결 안 됨, 분석 목적). 우리 코드가 보내는 것이 아니라 SDK 안에서 나간다 | Epic | EOS SDK 매니페스트 `UE_5.8/Engine/Source/ThirdParty/EOSSDK/SDK/Bin/IOS/EOSSDK.framework/PrivacyInfo.xcprivacy` (`PerformanceData`, Linked=false) |
| 인벤토리 · 스네어 코인 잔액 | 아이템 ID(`item.pig_meat` …)와 개수, 가상 재화 `SC` 잔액 | PlayFab 이 원본, 클라이언트는 `GetUserInventory` 로 읽기만 | `SnareBackendSubsystem.cpp:203·209·279~283`, `docs/BACKEND.md` 데이터 모델 |
| 목장 상태 | `ranch` 키 — 슬롯 번호·동물 ID·`occupiedSinceUtc`·재고 | PlayFab Player Data (CloudScript 가 판정) | `docs/BACKEND.md` 데이터 모델 · CloudScript 표 |
| 통신 상대 주소 | 4~8인 PVE 는 **EOS P2P 릴레이**로 오간다 — 참가자끼리 IP 를 직접 주고받지 않는다 | Epic (릴레이) | `docs/DESIGN.md:157`, `docs/decisions/004-eos-ossv2.md` |
| 기기 안 저장 파일 | SaveGame 슬롯 `SnarePlayerCache` — 인벤토리·코인·`PlayFabId`·조회 시각. **표시용 캐시이고 밖으로 나가지 않는다** | 기기 내부에만 | `SnareBackendSubsystem.cpp:239·245`, `Public/Backend/SnarePlayerCacheSave.h` |
| 분석 이벤트 | 게임에서 일어난 일의 **이름과 칸** — 무엇을 잡았나(`pet_item_id`)·어느 존인가(`zone_id`)·목장 슬롯 번호·바뀐 잔액(`new_balance`)·새 계정인가·죽을 때 버린 지급 대기열 줄 수(`lost_pending_count` — 개수 하나뿐, 무엇이었는지는 안 보낸다). **사람 이름·위치·기기 정보·자유 입력 글은 칸에 없다**(칸 목록의 원본은 `docs/ANALYTICS.md` 표 A) | PlayFab `WritePlayerEvent` — 나가는 길은 통로 함수 **하나**뿐이고 이벤트마다 PlayFab 계정 ID 에 붙어 쌓인다 | `SnareBackendSubsystem.cpp:382` `WriteGameEvent`(보내는 자리는 같은 파일 `:418`) · 부르는 자리 10개 — `docs/ANALYTICS.md` 표 A |

**분석 이벤트 목록 — `docs/ANALYTICS.md` 표 A 와 같은 목록이어야 한다**(한쪽만 고치면 2층 `Snarehaven.Analytics.Disclosure.StoreReadinessMatchesDocument` 가 운다). 데이터 보안 양식·App Privacy 의 "앱 기능(Analytics)" 항목에 이 목록이 그대로 들어간다:
`animal_killed` · `animal_capture_succeeded` · `animal_capture_failed` · `ranch_pet_register_requested` · `ranch_harvest_requested` · `gacha_pull_requested` · `currency_balance_changed` · `session_started` · `zone_travelled` · `player_died`

처리자(우리가 아닌 회사가 받는 것): **PlayFab(Microsoft)** — 위 표의 계정·인벤토리·목장 · **Epic Online Services(Epic Games)** — 기기 식별자·PUID·세션 통신. 두 회사의 처리방침 링크를 방침 문안에 넣어야 한다(주소는 사람이 확인해 넣는다 — 루프는 웹을 보지 않는다).

## 표 B — 수집하지 않는 것 (근거는 "코드에 없다")

| 항목 | 지금 상태 | 확인 방법 |
|---|---|---|
| 실명·이메일·전화·생년월일 | 없음 — 게스트 로그인뿐이고 가입 화면이 없다. **닉네임은 받는다**(표 A — 실명이 아니라 플레이어가 지은 이름) | `LoginWith*DeviceID`/`CustomID` 외 로그인 경로 0건 · EOS 는 `DeviceIdAccessToken` 게스트(`SnareOnlineSubsystem.cpp:111`), Epic 계정 로그인(EAS) 안 씀. **EOS SDK 매니페스트가 이메일·이름을 신고하지만 EAS 로그인일 때 얘기라 우리 빌드에선 안 나간다** |
| 채팅·음성 | 없음 | `Source/Snarehaven` 에 `chat` 0건 |
| 광고 ID·광고 SDK | 없음 | `Snarehaven.uproject` 플러그인 목록에 광고 SDK 없음 · 안드로이드 `bSupportAdMob=False`(`Config/DefaultEngine.ini` 안드로이드 절 — 엔진 기본 true 면 매니페스트에 AdMob 줄이 붙는다) · `bRequestGAID` 기본 false |
| 분석·크래시 리포트 **SDK** | 없음 — 별도 SDK 는 안 붙였고 **크래시 리포트도 밖으로 안 나간다**. **다만 분석 이벤트 자체는 나간다**(M8-03~06) — PlayFab 통로로 보내므로 수집 항목은 **표 A 분석 이벤트 줄**에 적혀 있다 | 플러그인 목록 (PlayFab·EOS·MCP·Python·모델링툴뿐) · 이벤트가 나가는 길은 `WriteGameEvent` 하나(2층 `Snarehaven.Analytics.Channel.SinglePath`) · 크래시: iOS 는 PLCrashReporter 가 기기 안 파일에만 쓴다(`UE_5.8 Core/Private/IOS/IOSPlatformCrashContext.cpp:247`, 보낼 CrashReportClient 가 iOS 엔 없다), 안드로이드 크래시 코드에 업로드 주소 0건, 우리 `Config/*.ini` 에 크래시 설정 0줄. 엔진 매니페스트의 `CrashData` 신고는 엔진 공통 문구라 우리 라벨엔 안 넣는다 |
| 위치·연락처·사진·카메라·마이크 | 없음 — 권한을 **요청하는** 코드가 없어 권한 창이 뜨지 않는다. iOS Info.plist 에 카메라·마이크 **사유 문구 두 줄은 있다** — 엔진·EOS 바이너리가 카메라·마이크 API 를 참조만 해도 애플이 문구를 요구해서(TestFlight 처리 실패 90683, 2026-10-15) 넣은 것이고 문구 자체가 "쓰지 않는다" 이다. 지우면 업로드가 다시 막힌다 | `Config/DefaultEngine.ini` iOS 절 `AdditionalPlistData` · 안드로이드 매니페스트 권한은 엔진 고정분뿐 — `INTERNET`·`ACCESS_NETWORK_STATE`·`ACCESS_WIFI_STATE`·`CHECK_LICENSE`·`WAKE_LOCK`·`MODIFY_AUDIO_SETTINGS`·`VIBRATE`(`UE_5.8 UnrealBuildTool/Platform/Android/UEDeployAndroid.cs:3050~3080`, 대상 SDK 36 이라 저장소 권한 없음) + EOS aar 의 `INTERNET`. 위험 권한(위치·카메라·녹음·연락처) 0개 |
| 결제 | **없음 — 구매·영수증 검증 코드 0건.** 가상 재화는 둘이 됐다(`SC` 코인 · `GM` 젬, M7-03) — 다만 **젬은 읽기만 한다**(살 길이 없어 응답에 없으면 0) | 재화 코드 `SnareBackendSubsystem.cpp:201·206` · 젬을 읽는 유일한 곳 같은 파일 `:310` · `Source/Snarehaven` 에 `iap`·`receipt` 가 **주석 3줄뿐**, 코드 0건 |

**방침 문안을 쓸 때 표 B 를 "수집하지 않습니다" 로 그대로 옮기면 안 된다** — 가챠·IAP 가 켜지는 DESIGN 10.5 단계에서 표 B 의 마지막 줄이 바뀐다. 바뀌는 시점에 이 표부터 고친다.

**루프#99 시점의 사실:** 가챠는 **화면까지만** 섰고(M7-01~05) 젬 칸이 생겼지만, **뽑는 길도 젬을 사는 길도 아직 없다** — 결제 관련 수집 항목은 그대로 0건이다. 표 B 마지막 줄이 실제로 바뀌는 때는 **M7-08(젬 IAP)이 켜질 때**다(영수증·주문 ID 가 그때 생긴다).

## 표 B2 — 빌드에 실제로 들어가는 개인정보 선언 (2026-10-16 점검, 라벨은 이것과 맞아야 한다)

근거 빌드: `Saved/ShipBuild/Snarehaven_1015_2315.xcarchive` 의 `.app` 을 열어 본 것.

| 자리 | 파일 | 신고 내용 | 우리 라벨에서 |
|---|---|---|---|
| 앱 루트 | `Build/IOS/Resources/PrivacyInfo.xcprivacy` (**2026-10-16 추가** — 엔진 기본 경로 `AdditionalPrivacyInfoIOS`, `BaseEngine.ini:3469`) | 기기 ID·사용자 ID·제품 상호작용·게임 진행(전부 연결됨) · 성능 데이터(연결 안 됨) · 추적 없음 · API 이유 코드 C617.1·35F9.1·8FFB.1·E174.1·CA92.1 | 원본 — 아래 답과 1:1 |
| `.app/UEMetadata/` | 엔진 `Engine/Build/IOS/Resources/UEMetadata/PrivacyInfo.xcprivacy` | 제품 상호작용(연결 안 됨)·게임 진행·크래시 데이터 · 같은 API 이유 코드 | 크래시 데이터는 안 나가서 뺀다(표 B) · 나머지는 우리 줄이 덮는다 |
| `Frameworks/EOSSDK.framework/` | 엔진 `Source/ThirdParty/EOSSDK/SDK/Bin/IOS/EOSSDK.framework/PrivacyInfo.xcprivacy` | 게임 진행·제품 상호작용·성능 데이터·**이메일·이름**·사용자 ID · UserDefaults CA92.1 | 이메일·이름은 Epic 계정 로그인(EAS) 몫 — 우리는 게스트라 안 나간다(표 B). 성능 데이터는 넣는다 |
| 안드로이드 매니페스트 | 엔진이 만든다(`UEDeployAndroid.cs`) + EOS aar | 위험 권한 0개, 광고 ID 없음 | 표 B 권한·광고 줄 |

## 스토어 개인정보 답 (표 A·B·B2 를 옮긴 것 — 사람이 콘솔에 넣는다)

**Apple App Privacy (영양표)** — 추적: **안 함**. 데이터 수집: **함**.

| Apple 분류 → 항목 | 목적 | 사용자 연결 | 추적 | 근거 |
|---|---|---|---|---|
| 식별자 → 기기 ID | 앱 기능 | 예 | 아니오 | 표 A 기기 식별자·EOS 기기 식별자 |
| 식별자 → 사용자 ID | 앱 기능 | 예 | 아니오 | PlayFab 계정 ID·EOS PUID·**닉네임**(Apple 정의상 화면 이름은 사용자 ID) |
| 사용 데이터 → 제품 상호작용 | 분석, 앱 기능 | 예 | 아니오 | 표 A 분석 이벤트 |
| 사용자 콘텐츠 → 게임 콘텐츠 | 앱 기능 | 예 | 아니오 | 인벤토리·코인·목장 |
| 진단 → 성능 데이터 | 분석 | **아니오** | 아니오 | EOS SDK 자체 신고(표 B2) |

넣지 않는 것: 연락처 정보(이름·이메일) · 위치 · 구매 내역 · 크래시 데이터 · 오디오·사진 · 광고 데이터 — 근거 표 B.

**구글플레이 데이터 보안(Data safety)**

| 질문 | 답 |
|---|---|
| 데이터를 수집하거나 공유하나 | 수집 **예** · 공유(제3자 전달) **아니오** — PlayFab·Epic 은 우리 대신 처리하는 **서비스 제공자**라 구글 정의상 공유가 아니다 |
| 전송 중 암호화 | 예 (PlayFab HTTPS · EOS TLS) |
| 삭제 요청 방법 | 있음 — 앱 안 `계정 삭제` + 웹 https://soilrist.github.io/snarehaven-site/delete-account/ |
| 개인 정보 → 이름 | 수집 · 필수(처음 켤 때 닉네임 입력) · 앱 기능 — 구글 정의상 닉네임은 "이름" |
| 개인 정보 → 사용자 ID | 수집 · 필수 · 앱 기능 (PlayFab 계정 ID·EOS PUID) |
| 앱 활동 → 앱 상호작용 | 수집 · 필수 · 분석, 앱 기능 (분석 이벤트) |
| 앱 활동 → 기타 사용자 생성 콘텐츠 | 수집 · 필수 · 앱 기능 (인벤토리·목장 진행) |
| 앱 정보 및 성능 → 진단 | 수집 · 필수 · 분석 (EOS SDK 성능 데이터) |
| 기기 또는 기타 ID | 수집 · 필수 · 앱 기능 (기기 식별자 — 광고 ID 아님) |
| 위치·연락처·사진·오디오·금융·건강·메시지·웹 기록·광고 ID | 수집 안 함 |

## 표 C — 연령등급 설문, 우리 답의 근거

| 설문이 묻는 것 | 지금 게임의 사실 | 근거 |
|---|---|---|
| 폭력 (사람·생물에 대한) | 동물을 때려 체력을 깎고 포획한다. **피·상처·죽는 연출은 없다** — 혈흔 VFX 도, 나이아가라 이펙트도 코드에 없다 | `docs/DESIGN.md` 3.1 · `Source` 에 `Blood`·`Niagara` 0건 |
| 공포·성적 표현·약물·욕설 | 없음 | 설계서 3장 전체에 해당 요소 없음 |
| 확률형 아이템 (도박 소재) | **빌드에 뽑기 화면이 있다**(올가미 기계, M7-04) — 등급 네 줄 확률이 백분율로 떠 있다. 다만 **아직 뽑을 수 없다**(판정할 백엔드 M7-06 이 없어 두 버튼이 꺼져 있다). 설문에는 **"확률형 아이템 있음"** 으로 답할 준비를 하고, 뽑기가 실제로 굴러가는 순간(M7-06) 답을 확정한다 | `docs/GACHA_DISCLOSURE.md`(확률 원본) · 화면 `Source/Snarehaven/Private/UI/GachaScreenWidget.cpp:142`(확률 줄) · 같은 파일 `:268~276`(버튼 꺼짐) |
| 인앱결제 | **아직 없다.** 젬 잔액 칸(`GM`)은 생겼지만 **살 길이 없다**(상품 등록·결제·영수증 검증 전부 0건, M7-08 미착수) — 젬을 파는 순간 "디지털 상품 구매" 로 답이 바뀐다 | 표 B 결제 줄 · `docs/BACKEND.md` 데이터 모델(젬은 IAP 단계에서 활성) |
| 사용자 간 상호작용 | 4~8인이 같은 방에서 함께 논다(온라인 상호작용 **있음**). **글·말로 주고받는 통로는 없다**(채팅·음성 없음). 플레이어가 정한 **닉네임**만 다른 플레이어에게 보인다(3~12자, 금칙어 서버 거름) | `docs/DESIGN.md` 4장 · 표 A 닉네임 줄 · 표 B |
| 사용자 위치 공유 · 개인정보 공유 | 없음 — 다른 플레이어에게 보이는 건 익명 EOS 계정 번호뿐 | 표 A |

→ DESIGN 4.7 의 "전연령~12세 예상" 과 어긋나는 항목은 없다. **실제 등급은 심사에서 확정된다** — 이 표는 설문에 답할 때 근거로 쓰는 것이지 등급을 정하는 것이 아니다.

## 표 D — 제출물 체크 (누가 하나)

| 제출물 | 지금 | 누가 |
|---|---|---|
| 개인정보처리방침 웹페이지 | **섰다 2026-10-11** — https://soilrist.github.io/snarehaven-site/privacy/ (원본 `docs/PRIVACY_POLICY.md`) | 사람 (주소·게시) |
| 구글플레이 데이터 보안(Data safety) 양식 | 답 완성 — 「스토어 개인정보 답」 절 | 사람 (콘솔 입력) |
| Apple App Privacy (영양표) | **게시됨 2026-10-17** — 감독이 ASC 에 직접 입력·게시. 게임 플레이 콘텐츠·사용자 ID·기기 ID → 앱 기능(연결됨) · 제품 상호작용 → 분석·앱 기능(연결됨) · 실적 데이터 → 분석(연결 안 됨) · 추적 전부 아님 | 감독 (끝) |
| 연령등급 설문(IARC) | Google Play 는 근거 표 C · **Apple 2025 설문 저장됨 2026-10-17 → 13+ (한국 12+, 브라질 16+)**. 답은 「App Store Connect 입력표」 연령 줄, 단 폭력·무기 빈도는 실제 입력 「빈번」(사냥이 기본 반복이라 낮춰 답하면 심사 반려 위험) | Apple 감독 (끝) · Google 사람 |
| **계정 삭제 요청 경로** | **웹 삭제 주소(구글 플레이 필수) 섰다 — https://soilrist.github.io/snarehaven-site/delete-account/ (2026-10-16, HTTP 200 확인).** **앱 안에 섰다 — 서버 배포됨(Rev 36, 2026-10-02, `BACKEND.md` 「DeleteMyAccount」). 라이브는 `server.DeletePlayer` 예외로 비우기 경로(`fullyDeleted:false`).** 로비 `계정 삭제` → 확인 두 번 → CloudScript `DeleteMyAccount`(타이틀 계정 삭제 + 기록) → 기기 ID 연결 끊기·캐시 삭제. 흐름·남는 것은 `BACKEND.md` 「계정 삭제」. 구글플레이가 따로 요구하는 **웹 삭제 요청 주소**는 아직 없다 | 사람: 마스터 계정 주기 정리 · 웹 요청 주소(표 E 2·4) |
| 확률형 아이템 **게임 내 표시** | **섰다**(M7-04) — 뽑기 화면에 등급별 확률이 백분율로 뜬다. 숫자 원본은 `GACHA_DISCLOSURE.md` 이고, 문서와 표가 어긋나면 2층 검사가 운다(M7-02) | 루프 (완료) |
| 확률형 아이템 **웹 게시** | **없음** — DESIGN 10.4 는 게임 웹사이트에도 확률을 **검색 가능한 문자열**로 올리라고 한다(이미지로 올리면 안 된다). 문안은 `GACHA_DISCLOSURE.md` 를 그대로 쓴다 | 사람 (주소·게시 — 표 E 1 과 같은 곳) |
| 광고·마케팅 자료 **"확률형 아이템 포함"** 문구 | **없음** — 문구 원문은 아래 상자. 스토어 설명문·스크린샷 문구·외부 광고 **전부**가 대상이다 | 사람 (스토어 콘솔·광고 계정) |
| 젬 IAP — 스토어 상품 등록 · 영수증 검증 | **없음** — 코드 0건(M7-08 본체). 가격표는 정해졌다(**사용자 확정 2026-09-23**, `docs/DESIGN.md` 10.2b — 6종, 100~11,000젬/1,500~119,000원) | 사람 (스토어 콘솔·PlayFab) |

> **광고·마케팅 자료에 넣을 법정 문구 (DESIGN 10.4 — 게임산업법 2024.3.22 시행):**
>
> ```
> 확률형 아이템 포함
> ```
>
> **어디에:** 스토어 설명문 · 스토어 스크린샷에 얹는 문구 · 게임 웹사이트 · 외부 광고(영상·배너·SNS) 전부.
> **왜 이 게임이 대상인가:** 무료 재화(스네어 코인)로도 뽑을 수 있어도 **젬(유상)이 얽히는 순간 가챠 전체가 표시 대상**이다(DESIGN 10.4 — 간접 유상 포함). 어긴 경우 **2년 이하 징역 또는 2000만원 이하 벌금**.
> **문구를 바꾸지 마라** — 줄이거나 다른 말로 풀어 쓰면 표시로 인정되지 않을 수 있다. 확률 수치 자체는 `docs/GACHA_DISCLOSURE.md` 가 원본이다.

> **사람 결정 2026-10-11:** 첫 출시(1.0)는 **젬 판매 없이 무료만** — 젬 IAP 는 출시 뒤 업데이트. 문의·개인정보 연락처는 **`snarehaven@gmail.com`**(게임 전용, 사람이 2026-10-11 만듦). Apple Developer Program 결제 끝(개인) → **2026-10-13 승인**, 10-14 App ID `com.soilrist.snarehaven.ios` 등록(감독). 남은 사람 몫: App Store Connect 앱 등록·업로드 인증(Xcode 계정 또는 API 키).

## 표 E — 미정 (사람 답이 있어야 채운다, 루프가 추측하지 않는다)

| # | 물어야 할 것 | 왜 루프가 못 정하나 |
|---|---|---|
| 1 | **정해짐 2026-10-11: https://soilrist.github.io/snarehaven-site/privacy/** (공개 저장소 `soilrist/snarehaven-site`, GitHub Pages — 방침을 고치면 그 저장소 `privacy.md` 도 같이) · 개인정보처리방침을 올릴 주소 | 밖으로 나가는 것 (Autonomy 🔴 3) |
| 2 | ~~문의·삭제 요청을 받을 연락처~~ **정해짐: `snarehaven@gmail.com` (2026-10-11)** | 개인정보 (🔴 3) |
| 3 | **정해짐(감독 2026-10-11, `PRIVACY_POLICY.md`): 보관 = 계정 삭제 때까지, 운영자 삭제 처리 30일 이내.** 데이터 보관 기간 · 삭제 요청 처리 방법 (PlayFab 콘솔에서 사람이 지우나, 앱이 API 로 지우나) | 기간은 숫자 (🔴 1), 방법은 계정 (🔴 3) |
| 4 | **정해짐(감독 2026-10-11): 앱 안 — 로비 `계정 삭제` → 확인 두 번 → `DeleteMyAccount`, 앱을 못 쓰면 메일.** 계정 삭제 경로를 앱 안에 넣을지 웹으로 받을지 | 되돌리기 비싼 방향 + 계정 (🔴 2·3) — **앱 안 경로는 감독 지시로 섰다(🟡).** 남은 것은 웹 요청 주소(구글플레이 데이터 보안 양식에 적는 칸) |
| 5 | **정해짐(감독 2026-10-11): 아동 대상 아님.** 한국 만 14세 미만 · COPPA 13세 미만을 대상에 넣을지 | 대상 설정 (🔴 2) — 아동 대상이면 광고·데이터 규칙이 통째로 달라진다 |

## App Store Connect 입력표 (M94 ③ · 루프#953 — 1.0 아이폰 전용, 감독 🟡 2026-10-11)

칸마다 넣을 값 한 줄. 문안 원본은 `docs/store/listing_ko.md`·`listing_en.md`(한도 시험 `tools/store/check_listing.py`), 그림은 `docs/store/screens/ko_0N.png`. Apple 화면의 칸 이름은 바뀔 수 있으니 사람이 입력할 때 대조한다.

| 칸 | 넣을 값 | 근거 |
|---|---|---|
| 앱 이름 | `Snarehaven` | listing 이름 |
| 부제 | 한국어 `사냥하고 포획하고 목장을 키워요` · 영어 `Hunt, capture, grow your ranch` | listing 부제 |
| 번들 ID | `com.soilrist.snarehaven.ios` (Apple 개발자 계정 등록 끝 10-14) | `Config/DefaultEngine.ini` iOS 절 |
| 기본 언어 | 한국어 (영어 지역화 추가) | 한국 출시 우선, 확률 고지 문구가 한국 법 |
| 기본 카테고리 / 보조 | 게임 → 시뮬레이션 / 보조: 게임 → 캐주얼 | 사람 결정(M94 지시) |
| 가격 | 무료 (인앱 구입 없음 — 1.0 에 젬 판매 없음) | 사람 결정 2026-10-11 |
| 지원 기기 | 아이폰 전용 · 가로 화면 (`bSupportsIPad=False`, 세로 꺼짐) | 감독 🟡 2026-10-11 · `DefaultEngine.ini` |
| 스크린샷 | 6.9인치 가로 2868×1320 한 벌 5장 `ko_01~05.png` (아이패드 칸 없음) | 아이폰 전용 |
| 홍보 문구 / 설명 / 키워드 | listing 의 해당 칸 그대로 (설명 첫 줄 `확률형 아이템 포함` — 표 D 상자 원문) | listing |
| 지원 URL | `https://soilrist.github.io/snarehaven-site/` | 사람 결정 |
| 마케팅 URL | 비워 둠 (선택 칸) | — |
| 개인정보 처리방침 URL | `https://soilrist.github.io/snarehaven-site/privacy/` | 표 E 1 |
| 저작권 | `© 2026 soilrist` | listing |
| 연락처 (심사·지원) | `snarehaven@gmail.com` | 표 E 2 |
| 심사용 로그인 | 필요 없음 — 기기 자동 로그인(게스트). 메모: 「기기 ID 로 익명 계정이 자동 생성됩니다. 계정 삭제는 로비 → 계정 삭제」 | 표 A · 표 D 계정 삭제 줄 |
| 앱 개인정보 — 추적 | 추적 안 함 (광고 SDK·광고 ID 없음, `NSPrivacyTracking=false`) | 표 B · 표 B2 |
| 앱 개인정보 — 수집 항목 | 식별자: 기기 ID·사용자 ID(PlayFab/EOS 계정 번호·닉네임) → 앱 기능 · 사용 데이터: 제품 상호작용(분석 이벤트 목록) → 분석, 앱 기능 · 사용자 콘텐츠: 게임 콘텐츠(인벤토리·목장) → 앱 기능 — **여기까지 사용자에게 연결됨** · 진단: 성능 데이터(EOS SDK) → 분석, **연결 안 됨**. 전부 추적 아님. 넣지 않는 것: 이름·이메일·크래시 데이터·위치·구매 | 「스토어 개인정보 답」 절 · 표 B2 (`Build/IOS/Resources/PrivacyInfo.xcprivacy` 와 1:1) |
| 연령 등급 설문 (2025 개편판 — 칸 이름은 입력할 때 화면과 대조) | **콘텐츠:** 만화·판타지 폭력 가끔/약함(동물을 때려 포획, 피·상처 없음) · 현실적 폭력 없음 · **총·무기 가끔**(권총·소총으로 동물 사냥, `DESIGN.md` 장비 표) · 공포·성적·노출·약물·술·담배·욕설·성인 주제 없음 · 의료·치료 정보 없음 · 건강·웰니스 주제 없음 · 도박 모의 없음 · 상금 콘테스트 없음 · **확률형 아이템(룻박스) 있음**(무료 코인으로 뽑음, 1.0 유료 결제 없음). **앱 안 통제:** 자녀 보호 기능 없음 · 연령 확인 없음. **기능:** 웹 무제한 접근 없음 · **사용자 생성 콘텐츠 있음(닉네임만 — 다른 플레이어에게 보임, 서버 금칙어 거름)** · 메시지·채팅 없음 · 광고 없음 | 표 C · 표 A 닉네임 줄 |
| 수출 규정 | 표준 암호화(HTTPS)만 사용 — `ITSAppUsesNonExemptEncryption=NO` (`Build/IOS` 의 Info.plist 에 이미 키 있음, 값은 빌드 때 확인) | 사람 확인 |
| 내용 권한(Content Rights) | 「제3자 콘텐츠 포함, 권리 있음」 — Kenney CC0 소리·Mixamo 동작(Adobe 무료 사용 허가, 게임 안 반입만), 나머지는 자체 제작 (`DESIGN.md` 4.6) | DESIGN 4.6 · 감독 2026-10-11 |
| 버전 / 빌드 | 1.0 / 1 | `Versioning.md` |
| 심사 노트 | 「온라인 멀티는 EOS P2P 로 방 만들기/참가. 혼자서도 전 기능 확인 가능. 인앱 구입 없음.」 | 표 D |

## 이 문서를 고쳐야 하는 때
- **신고·차단이 들어갈 때**(INBOX 2026-10-16 결정) → 표 A 에 신고 기록 줄, 「스토어 개인정보 답」·`Build/IOS/Resources/PrivacyInfo.xcprivacy`·`PRIVACY_POLICY.md` 같이
- **엔진·EOS SDK 를 올릴 때** → 표 B2 의 매니페스트 세 개를 다시 열어 신고 항목이 늘었는지 본다
- **뽑기가 실제로 굴러갈 때**(M7-06 CloudScript) → 표 C 3번을 "있음" 으로 확정, 표 D 웹 게시 줄
- **젬을 파는 길이 생길 때**(M7-08 IAP) → 표 B 결제 줄(영수증·주문 ID 가 그때 생긴다), 표 C 4번. 가격표 자체는 이미 정해짐(DESIGN 10.2b)
- 안드로이드 절(M4-02)이 서면서 권한이 붙을 때 → 표 B 권한 줄
- 새 SDK(광고·분석)를 넣을 때 → 표 A·B 둘 다
- **분석 이벤트를 더하거나 뺄 때**(`docs/ANALYTICS.md` 표 A) → 표 A 의 이벤트 목록도 같이. 한쪽만 고치면 2층이 운다 — 신고 목록과 실제로 보내는 목록이 갈라지는 것이 심사에서 문제가 되는 어긋남이다
- **가챠 이벤트가 켜질 때**(M8-05b) → 표 A 목록 아래 "아직 한 줄도 안 나간다" 문장을 지운다 (목록 자체는 안 바뀐다)
