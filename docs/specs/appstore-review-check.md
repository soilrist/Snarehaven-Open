# App Store 심사 대비 점검표 (루프#1100, 2026-10-14)

> INBOX 지시(사람 2026-10-14 00:30) 5칸. 1.0 = 아이폰 전용·무료·젬 판매 없음. 칸마다 증거를 적는다. **합격은 감독이 표와 캡처를 보고 정한다.**
> 이 표는 코드·설정·시험으로 확인한 것까지다. **Apple 심사 통과를 보장하지는 않는다**(실제 업로드·심사는 사람 몫).

| # | 심사 조항 | 확인한 것 | 증거 | 판정 |
|---|---|---|---|---|
| ① | 5.1.1(v) 앱 안 계정 삭제 | 로비 → `설정` → `계정 삭제` → 확인 두 번 → CloudScript `DeleteMyAccount` → 기기 ID 연결 끊기·캐시 삭제. 닿는 데 **누름 2번**(설정, 계정 삭제), 확인까지 3번 | 캡처 `captures/1100_lobby_settings.png`(설정 화면에 `계정 삭제` 단추, 스위치 `-SnareLobbySettings`) · 코드 `LobbyWidget.cpp:377`(버튼), `SnareBackendSubsystem.cpp:893`(`DeleteAccount`) · 시험 `Snarehaven.Account.Delete.LobbyAsksTwiceThenRequests`·`CacheGoesOnlyAfterServerSays` Success · CloudScript 시험 `test_cloudscript.js:3598·3613` | 앱 쪽 통과. CloudScript `DeleteMyAccount` 는 **Rev 36(10-02) 배포됨**(BACKEND.md:94) — 라이브에선 `DeletePlayer` 가 예외라 비우기 경로, 마스터 계정은 운영자가 처리 |
| ② | 3.1.1 확률형 아이템 확률 표시 | 뽑기 화면에 등급 4줄 + 아이템별 확률이 백분율로 상시 표시. 표는 `DT_GachaRates`·`GACHA_DISCLOSURE.md` 와 2층 검사로 묶임 | 캡처 `captures/956_gem_hidden_Gacha.png`(커먼 70.0%·레어 22.0%·에픽 6.5%·레전더리 1.5% + 「아이템별 확률」, 코인 뽑기만, 젬 글자 없음) · `GachaScreenWidget.cpp:191`(`GetRateLineText`) · 시험 `Snarehaven.Gacha.Table.GradeRatesSumTo100` | 통과(캡처는 #956 것 — 이후 화면 변경 없음을 이번에 재촬영하지는 않았다). 「아이템별 확률」 목록이 판 높이에 잘려 스크롤이 필요함 — 감독 눈 확인 |
| ③ | 개인정보 매니페스트 `PrivacyInfo.xcprivacy` | **우리가 따로 둔 파일은 없다.** 엔진이 앱에 `UEMetadata/PrivacyInfo.xcprivacy` 를 넣고(엔진 원본 `Engine/Build/IOS/Resources/UEMetadata/`), EOS SDK 프레임워크가 자기 것을 넣는다. 엔진 것 Required Reason API: FileTimestamp `C617.1` · SystemBootTime `8FFB.1`·`35F9.1` · DiskSpace `E174.1` · UserDefaults `CA92.1`. 수집 선언: ProductInteraction·GameplayContent·CrashData, 추적 없음. EOS 프레임워크 것: UserDefaults `CA92.1`, 수집 선언에 Email·Name·UserID·PerformanceData 등 | 앱 번들 `Build/IOS/Snarehaven.app/UEMetadata/PrivacyInfo.xcprivacy`, `…/Frameworks/EOSSDK.framework/PrivacyInfo.xcprivacy`(10-11 빌드 산출물, git 밖) | 파일은 번들에 들어감. 🟡 **PlayFab 은 소스 편입이라 자기 매니페스트가 없고**, 위 엔진 것이 우리 사용을 덮는지는 업로드 때 App Store Connect 의 매니페스트 경고로만 최종 확인된다. EOS 선언이 Email·Name 을 적었지만 우리는 안 보낸다(STORE_READINESS 표 B) — App Privacy 설문은 표 A·B 대로 |
| ④ | 암호화 수출 `ITSAppUsesNonExemptEncryption=false` | 엔진이 `IOSRuntimeSettings.bUsesNonExemptEncryption`(기본 false)을 읽어 Info.plist 에 쓴다(`UEDeployIOS.cs:378~381`). `DefaultEngine.ini` 가 이 칸을 건드리지 않는다 | `Build/IOS/Snarehaven.app/Info.plist:81-82` `ITSAppUsesNonExemptEncryption` `<false/>` · `Build/IOS/UBTGenerated/Info.Template.plist:42-43` 같은 값 | 통과(10-11 빌드 산출물 기준 — 최종 업로드 빌드에서 같은 줄을 다시 본다) |
| ⑤ | 2.1 미완성·막힌 결제 길 | `bGemsEnabled=False`(`Config/DefaultGame.ini:36`)가 잔액 줄 젬 칸·뽑기 젬 줄·옷 상점 입구·상자 젬 열쇠 단추·배낭 젬 칸을 접는다. `Source/Snarehaven` 에 「곧 출시」 류 글 0건(`준비 중` 한 건은 목장 배치 서버 대기 `RanchHudWidget.cpp:681` — 막힌 기능 광고가 아니라 상태 안내) | 시험 `Snarehaven.Store.GemsHidden.ScreensHaveNoGem`·`CrateKeyButton` Success · 캡처 `captures/956_gem_hidden_Gacha.png` | 통과. 로그인 실패 글은 「서버에 연결하지 못했어요 - 혼자 시작」 으로 바꿈(#1101, 감독 결정 — 「멀티는 준비 중」 은 미완성으로 읽힐 수 있었음) |

## 남은 것 (루프가 못 하는 것)
- App Store Connect 업로드·매니페스트 경고 확인(③) · 최종 빌드 Info.plist 재확인(④) — 사람·감독.
- 이번 바퀴 코드 변경은 검사 스위치 `-SnareLobbySettings`(설정 화면을 바로 연다, 출시 빌드에선 `SnareCheckSwitch` 가 무시) 하나뿐이다.
