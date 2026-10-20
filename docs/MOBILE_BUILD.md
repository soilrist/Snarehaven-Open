# MOBILE_BUILD — 모바일 빌드 설정 현황표 (M4-00)

> **이 문서는 "지금 어떤 상태인가" 를 한 장으로 모은 것이다.** 값을 정하는 곳이 아니다 —
> 이름·숫자·방향은 사람이 INBOX 답으로 주고, 실제 설정은 M4-02(안드로이드)·M4-06(iOS)이 `Config/` 에 쓴다.
> 근거: `docs/DESIGN.md` 4.2(모바일 성능)·4.7(플랫폼) · 조각 목록은 `docs/ROADMAP.md` M4 절.
> **범위 밖:** 서명 키·스토어 업로드·Apple/Google 개발자 계정 (유료·계정 = Autonomy 🔴 3, 루프 금지).
> 마지막 갱신: 루프#81 (2026-09-21, 표 H 추가). 표 D 의 점검 결과는 **루프#51 시점의 이 기계**를 말한다.

## A. 지금 프로젝트에 있는 것 / 없는 것

| 자리 | 지금 값 | 모바일에 필요한가 |
|---|---|---|
| `Snarehaven.uproject` `TargetPlatforms` | `Android`, `IOS`, `Mac` | 이미 들어 있다 |
| `Config/DefaultEngine.ini` 절 | `GameMapsSettings` · `RendererSettings` · `Engine` · `OnlineServices` · `EOSSDK` · **`IOSRuntimeSettings` 6개** | **iOS 절은 루프#81 이 넣었다(M4-06, 표 H)** · 안드로이드 절은 아직 **없다** → M4-02 |
| `Config/DefaultEngine.ini` 렌더러 | `r.Nanite.ProjectEnabled=False` · `r.DynamicGlobalIlluminationMethod=0` · `r.ReflectionMethod=0` · `r.Mobile.ShadingPath=0` · `r.Mobile.EnableNoPrecomputedLightingCSMShader=1` | DESIGN 4.2 대로 이미 모바일 렌더러 · **M4 에서 바꾸지 않는다** |
| `Config/DefaultGame.ini` `CompanyName` | `soilrist` (**루프#81 이 채웠다** — DESIGN 4.7) | 패키지 이름·번들 ID 를 짓는 근거. 번들 ID 는 이 값에 기대지 않고 `BundleIdentifier` 에 통째로 적혀 있다 |
| `Config/DefaultGame.ini` `ProjectVersion` | `0.0.0` | 스토어 버전 표시에 쓰인다(M4 범위 밖, Versioning.md) |
| `Config/DefaultInput.ini` 터치 | `DefaultTouchInterface=/Engine/MobileResources/HUD/DefaultVirtualJoysticks` · `bAlwaysShowTouchInterface=False`(엔진 기본 — M1-10a 에서 True 에서 내렸다, 데스크톱에서는 `-faketouches` 로 본다) | M0-03 에서 섰고 **M4-04 가 터치 경로에서 확인했다(표 F)** |
| `Config/DefaultDeviceProfiles.ini` | `[Android DeviceProfile]`·`[IOS DeviceProfile]` 에 `+CVars=t.MaxFPS=30` 한 줄씩 | **루프#80 이 만들었다(M4-05, 표 G)** |
| `Config/DefaultScalability.ini` | **파일이 없다 — 일부러 안 만들었다** | 적을 값이 전부 미정 숫자(해상도 배율·품질 단계)라 엔진 기본이 그대로 산다. 답이 오면 그때 만든다 |
| 플러그인(모바일에 실리는 것) | `OnlineServicesEOSGS` · `EOSShared` · `SocketSubsystemEOS` · `PlayFab` | 에디터 전용(`PythonScriptPlugin`·`ModelContextProtocol`·`AllToolsets`)은 `TargetAllowList: Editor` 라 모바일 패키지에 안 들어간다 |
| `AndroidFileServer` 플러그인 | `Enabled: false` | 기기 파일 접근용. 지금은 필요 없다 |
| `Source/Snarehaven.Target.cs` | 플랫폼 분기 없음 (`Type=Game` 만) | 안드로이드용 분기는 아직 필요 없다 — M4-03 에서 필요하면 그때 |

## B. 사람이 정해야 채워지는 칸 (🔴 — 루프는 추천값도 쓰지 않는다)

INBOX `질문`(루프#50, 2026-09-20 15:20)과 **같은 표다.** 답이 오면 여기와 INBOX 를 같이 정리한다.

| 항목 | 쓰이는 자리 | 엔진 기본값(참고 — 우리 값이 아니다) | 값 |
|---|---|---|---|
| 안드로이드 패키지 이름 | `[/Script/AndroidRuntimeSettings.AndroidRuntimeSettings] PackageName` | 없음(빈 값 · `[PROJECT]` 치환 규칙만 있다) | 미정 |
| iOS 번들 ID | `[/Script/IOSRuntimeSettings.IOSRuntimeSettings] BundleIdentifier` | `com.YourCompany.[PROJECT_NAME]` | **`com.soilrist.snarehaven.ios`** (10-14 `.ios` 붙임 — 맨 이름은 옛 무료 팀 소유, 감독 결정 2026-09-20 · 루프#81 이 적었다) |
| 최소 안드로이드 SDK | `MinSDKVersion` | 생성자에 기본값 없음 · 헤더 주석은 "26 아래로 내리지 말 것" | 미정 |
| 대상 안드로이드 SDK | `TargetSDKVersion` | 같음 | 미정 |
| 최소 iOS 버전 | `MinimumiOSVersion` | 엔진 `Config/IOS/IOS_SDK.json` `MinSoftwareVersion: 15.0` | **엔진 기본 그대로** (감독 결정 2026-09-20) — 그래서 **우리 Config 에 적지 않는다**. 2층 검사가 “적혀 있지 않은 상태” 를 지킨다 |
| 모바일 목표 프레임 | M4-05 의 device profile(`t.MaxFPS` 계열) | 엔진 기본(모바일 30) | **30 fps** (감독 결정 2026-09-20 · DESIGN 4.7 표 · 루프#80 이 `Config/DefaultDeviceProfiles.ini` 에 적었다) |
| 화면 방향 | 안드로이드 `Orientation` · iOS `bSupports*Orientation` 네 칸 | `EAndroidScreenOrientation::Landscape` (`AndroidRuntimeSettings.cpp:23`) | **SensorLandscape(가로만, 세로 없음)** (감독 결정 2026-09-20). iOS 쪽은 루프#81 이 적었다 — 안드로이드 쪽은 M4-02 |
| 안드로이드 SDK 설치 주체 | 표 D 의 막힘을 푸는 사람 | — | 미정 |

> 방향은 카메라 합격폭(M0-04, 720cm·60°)과 묶여 있다 — 세로로 가면 그 폭을 다시 잰다.

## C. 엔진 5.8 이 요구하는 도구 버전 (우리가 정한 숫자가 아니라 엔진이 적어 둔 것)

| 도구 | 요구 | 출처 |
|---|---|---|
| Android NDK | `r27c` = `27.2.12479018` (min `r27c` · max `r29`) | `UE_5.8/Engine/Config/Android/Android_SDK.json` |
| Android platforms | `android-36` | 같은 파일 |
| Android build-tools | `36.0.0` (SetupAndroid.command 는 `36.1.0` 을 깐다) | 같은 파일 · `Engine/Extras/Android/SetupAndroid.sh:33` |
| CMake | `3.22.1` | 같은 파일 |
| JDK | Android Studio 번들 JBR (`<Studio>/Contents/jbr/Contents/Home` 를 `JAVA_HOME` 으로) | `Engine/Extras/Android/SetupAndroid.command:139` |
| Xcode (iOS) | min `15.2.0` · 권장 `26.1.1` · max `27.9.0` | `UE_5.8/Engine/Config/Apple/Apple_SDK.json` |
| iOS 최소 OS | `15.0` | `UE_5.8/Engine/Config/IOS/IOS_SDK.json` |

## D. 이 기계의 전제 점검 결과 (M4-01 · 루프#135 재점검, 2026-09-22 — 사람이 SDK 를 깐 뒤)

| 확인한 것 | 명령 | 결과 |
|---|---|---|
| 엔진이 보는 플랫폼 유효성 | `RunUBT.sh -Mode=ValidatePlatforms -Platforms=Android+IOS+Mac` | `##PlatformValidate: Android VALID` · `IOS VALID` · `Mac VALID` |
| Turnkey SDK 확인 | `RunUAT.sh Turnkey -command=VerifySdk -platform=Android -project=<uproject>` | `Android: (Status=Valid, MinAllowed_Sdk=r27c, MaxAllowed_Sdk=r29, Current_Sdk=r27c, Allowed_AutoSdk=r27c, Current_AutoSdk=, Flags="InstalledSdk_ValidVersionExists, Support_FullSdk, Sdk_HasBestVersion")` · `BUILD SUCCESSFUL` · `logs/turnkey_android_loop135.log` |
| SDK 폴더 | `ls ~/Library/Android/sdk` | `build-tools/ cmake/ cmdline-tools/ licenses/ ndk/ platform-tools/ platforms/` |
| 플랫폼·NDK·빌드툴 버전 | `ls .../platforms .../ndk .../build-tools` | `android-36` · `27.2.12479018` · `36.1.0` — 표 C 요구값(r27c = 27.2.x · android-36) 과 맞다 |
| Android Studio | `ls -d "/Applications/Android Studio.app"` | 있다 (JBR 포함) |
| JDK | `"$JAVA_HOME/bin/java" -version` | **JDK 17 을 쓴다** — `openjdk version "17.0.20.1" 2026-08-18`(Homebrew `openjdk@17`, `/opt/homebrew/opt/openjdk@17/libexec/openjdk.jdk/Contents/Home`). Android Studio 번들 JBR 은 `25.0.3` 이라 엔진이 쓰는 Gradle 8.13 이 못 받는다(루프#136 이 부딪혔고 감독이 2026-09-22 에 JDK 17 을 깔아 풀었다 — 표 I·J). `loop/env.sh` 63행이 `JAVA_HOME` 을 JDK 17 로 export 한다 |
| adb | `~/Library/Android/sdk/platform-tools/adb version` | `Android Debug Bridge version 1.0.41` |
| Xcode | `xcodebuild -version` | `Xcode 26.6` — 표 C 범위(15.2 ~ 27.9) 안 |

**판정: 안드로이드 전제는 섰다.** 루프#51 을 막았던 것(SDK·NDK·JDK 부재)은 사람이 2026-09-22 에 풀었다. M4-02·M4-03 이 이어서 돈다.

**루프#135 가 새로 확인한 두 가지(다음 조각이 알아야 할 것):**
1. **환경변수는 UBT·Turnkey 판정에 필요 없다.** `env -u ANDROID_HOME -u NDKROOT -u NDK_ROOT -u JAVA_HOME` 로 둘 다 다시 돌려도 `Android VALID` · `Status=Valid` 그대로다 — 엔진이 기본 경로 `~/Library/Android/sdk` 를 스스로 찾는다(`~/.config/Unreal Engine/UnrealBuildTool/BuildConfiguration.xml` 은 아예 없다). 즉 이 판정은 셸 설정이 아니라 **디스크에 깔린 것**을 본 결과다.
2. **비대화 셸(루프가 쓰는 셸)에는 `~/.zshrc` 가 안 읽힌다** — `ANDROID_HOME`·`NDKROOT`·`JAVA_HOME` 이 전부 빈 값이고 `java` 도 PATH 에 없다(`Unable to locate a Java Runtime`). 위 1 때문에 M4-01 은 그래도 통과했지만, **M4-03 의 Gradle 은 `java` 를 직접 찾을 수 있다** — 패키징이 JDK 를 못 찾는다고 하면 먼저 이 네 줄을 `export` 하고 다시 돌린다(값은 `~/.zshrc` 16·18·22·23행). **2026-09-22 부터 `loop/env.sh` 가 `JAVA_HOME`(JDK 17)을 직접 export 하므로 루프 셸에는 자동으로 선다** — `ANDROID_HOME`·`NDKROOT`·`NDK_ROOT` 는 여전히 명령 앞에 붙인다.

> **이전 점검 기록 — 루프#51, 2026-09-20:** `Android INVALID` · Turnkey 가 안드로이드를 대상 목록에도 못 올림 · 환경변수 셋 다 빈 값 · `~/Library/Android` 자체가 없음 · Android Studio 없음 · `java -version` = `Unable to locate a Java Runtime`. 전문은 `logs/turnkey_android_loop51.log`.

## E. 조각별로 고칠 자리 (이 표가 M4 의 지도다)

| 조각 | 건드릴 파일 | 선행 |
|---|---|---|
| M4-01 | (이 문서 표 D) | — **끝남(루프#135 재점검 — 루프#51 은 `INVALID` 라 열려 있었다)** |
| M4-02 | `Config/DefaultEngine.ini` 새 절 `[/Script/AndroidRuntimeSettings.AndroidRuntimeSettings]` | 표 B 의 안드로이드 4칸 |
| M4-03 | (빌드 산출물만) `RunUAT BuildCookRun` | — **끝남(루프#137, 표 J)**. 루프#136 의 JDK 막힘은 감독이 JDK 17 을 깔아 풀었고, 같은 명령이 `.apk` 117MB 를 냈다 |
| M4-04 | 문서·캡처 (`Config/DefaultInput.ini` 는 이미 섬) + 3층 스위치 `-SnareTouch=` | — **끝남(루프#79, 표 F)** |
| M4-05 | `Config/DefaultDeviceProfiles.ini`(새로) | — **끝남(루프#80, 표 G)**. `DefaultScalability.ini` 는 적을 값이 전부 미정이라 안 만들었다 |
| M4-06 | `Config/DefaultEngine.ini` 새 절 `[/Script/IOSRuntimeSettings.IOSRuntimeSettings]` | — **끝남(루프#81, 표 H)** |
| M4-07 | `docs/STORE_READINESS.md`(새로) | — |

## F. 터치 조작 점검 결과 (M4-04 · 루프#79, 2026-09-21)

**어떻게 봤나:** 실기기가 없어 맥에서 `-game -RenderOffscreen -faketouches` 로 띄우고, 화면에 **진짜 터치 이벤트**를
넣어(`FSlateApplication::OnTouchStarted/Moved/Ended` — 3층 스위치 `-SnareTouch=stick|attack|capture`) 위젯이
자기 히트테스트로 받는지 봤다. 함수를 직접 부르거나 키를 넣는 길은 쓰지 않았다 — 그건 모바일 경로를 본 것이 아니다.

| 본 것 | 어떻게 | 결과 |
|---|---|---|
| 가상 조이스틱이 화면에 뜬다 | `-faketouches` (터치 기기에서는 이 스위치 없이 뜬다 — `bAlwaysShowTouchInterface=False` 가 엔진 기본) | **통과** — 좌·우 스틱 두 개 (`captures/79_touch_stick.png`) |
| 조이스틱 터치가 이동으로 이어진다 | `-SnareTouch=stick` — 스틱 중심 (169, 551) 에 손가락을 놓고 반지름만큼 아래 (169, 671) 로 끌어 잡고 있는다 | **통과** — 입력 크기 0.91(달리기), 속도 534.5cm/s 로 -X 로 달린다 (`logs/game_loop79_stick.log`) |
| 공격 버튼이 터치를 받는다 | `-SnareTouch=attack` — 버튼 한가운데 (962, 641) 를 누른 채로 | **통과** — 다음 초부터 `공격 중`, 돼지 HP 바가 깎인다 (`captures/79_touch_attack.png`) |
| 포획 버튼이 터치를 받는다 | `-SnareTouch=capture -SnareAutoAttack` — 버튼이 켜지는 순간 (1151, 641) 을 눌렀다 뗀다 | **통과** — `포획한 펫 item.pet_pig …`, 돼지가 사라진다 (`captures/79_touch_capture.png`) |
| 모바일 렌더러(ES3.1) 경로에서도 같은가 | `-FeatureLevelES31` 을 붙여 같은 `stick` 실행 (`rhifeaturelevel="ES3_1"`·`shaderplatform="METAL_ES3_1"`) | **통과** — 입력 크기 0.91·속도 534.5 로 같다 (`captures/79_touch_stick_es31.png`). 첫 실행은 ES3.1 셰이더 콜드 캐시를 굽느라(약 8분) 화면이 회색이라 다시 돌려 찍었다 |

**아직 못 본 것 (정직하게):**
- **실기기 터치는 못 봤다** — 아이폰·안드로이드 기기에 올린 적이 없다. 손가락 크기·멀티터치(조이스틱을 잡은 채 공격
  버튼 누르기)·화면 안전영역은 기기에서만 보인다. M4-03(안드로이드 패키징)·iOS 서명이 서야 볼 수 있다.
- **오른쪽 스틱은 아무 데도 안 걸려 있다** — 엔진 기본 터치 인터페이스가 스틱 두 개짜리라 화면에 같이 뜨지만,
  `IMC_Default` 는 왼쪽(`Gamepad_Left2D`)만 받는다. 카메라는 고정각이라 오른쪽 스틱이 할 일이 없다 (DESIGN 3.3).
  화면에서 지울지는 UI 겉모습(M5-05) 때 정할 일이고, 숫자·방향이 아니라 보이는 것이라 여기서 정하지 않았다.
- 2층에서는 터치 인터페이스 에셋에 이동 스틱(`Gamepad_LeftX`/`LeftY`)이 있는지만 본다
  (`Snarehaven.Input.Touch.DefaultInterfaceHasMoveStick`) — IMC 쪽 짝은 `Snarehaven.Input.DefaultContext.*` 몫이다.

---

## G. 모바일 품질 설정 결과 (M4-05 · 루프#80, 2026-09-21)

**무엇을 적었나:** `Config/DefaultDeviceProfiles.ini` 를 새로 만들어 `[Android DeviceProfile]`·`[IOS DeviceProfile]`
에 `+CVars=t.MaxFPS=30` 을 한 줄씩 더했다. **적은 숫자는 이 하나뿐이고 사람이 정한 것이다**(DESIGN 4.7 표, 감독 결정
2026-09-20). 절 이름은 엔진이 이미 쓰는 것을 그대로 쓰고 새 프로파일을 만들지 않는다 — 엔진이 플랫폼별로 넣어 둔
모바일 설정(HZB·EarlyZPass·오클루전 …)을 지우지 않기 위해서다.

**안 적은 것 (미정 숫자라서):** 해상도 배율(`r.MobileContentScaleFactor`)·품질 단계(`sg.*`). 그래서
`Config/DefaultScalability.ini` 는 **만들지 않았다** — 빈 파일은 엔진 기본과 같은데 "우리가 정한 값" 처럼 보인다.

| 본 것 | 어떻게 | 결과 |
|---|---|---|
| 설정 파일이 실제로 읽힌다 | 2층 `Snarehaven.Mobile.Profile.TargetFrameRate` — 두 절의 `CVars` 배열에서 `t.MaxFPS` 를 읽어 30 인지 본다 | **통과**. 파일을 잠깐 치우고 같은 검사를 돌리면 `Result={Fail}`(`EXIT CODE: -1`) — 엔진 기본에는 `t.MaxFPS` 가 없다(`BaseDeviceProfiles.ini` grep 0줄)라 이 통과는 우리 파일 덕이다 |
| 모바일 렌더러 전제가 안 깨진다 | 2층 `Snarehaven.Mobile.Profile.KeepsMobileRenderer` + 3층 로그 `Set CVar [[r.Nanite.ProjectEnabled:0]]`·`[[r.Mobile.ShadingPath:0]]` | **통과** (DESIGN 4.2) |
| 안드로이드 프로파일이 우리 줄을 집어 간다 | 3층 — 맥에서 `-DP=Android` 로 프로파일을 강제하고 부팅 (`logs/game_loop80_dp_android.log`) | **통과** — `LogDeviceProfileManager: Pushing Device Profile CVar: [[t.MaxFPS:0 -> 30]]` |
| 부팅이 끝난 뒤에도 30 이 남는가 | 같은 실행에서 `-ExecCmds="t.MaxFPS"` 로 되읽기 | **실패(못 지킴)** — `t.MaxFPS = "0"  LastSetBy: DeviceProfile`. 아래 참조 |
| 실기기에서 30fps 로 도는가 | — | **못 함** — 기기가 없다 (표 F 와 같은 한계) |

**`t.MaxFPS` 가 0 으로 돌아가는 것에 대해 (아직 못 밝힘):** 같은 패스에서 푸시된 **엔진의 다른 안드로이드 CVar 는
끝까지 살아 있다** — 되읽으면 `r.OcclusionFeedback.Enable = "1"`·`r.Streaming.MaxTempMemoryAllowed = "25"`·
`r.LocalExposure = "0"` 전부 프로파일 값 그대로다(`logs/game_loop80_dp_ctrl.log`). 그러니 "프로파일 적용 자체가
무효" 가 아니라 **`t.MaxFPS` 한 칸만** 되돌아간다. 되돌리는 코드는 못 찾았다 — `LogDeviceProfileManager Verbose` 로
다시 돌려도 `Popping Device Profile CVar` 줄이 없고(`logs/game_loop80_dp_verbose.log`), 엔진 전체에서
`t.MaxFPS` 를 쓰는 곳은 읽기(`CsvProfiler.cpp:4044`·`LaunchEngineLoop.cpp:1541`)와 선언
(`UnrealEngine.cpp:12137`, `TAutoConsoleVariable<float>`)뿐이다. **맥에서 안드로이드 프로파일을 흉내 낸 실행에서만
본 것이라, 진짜 모놀리식 모바일 빌드에서도 같은지는 기기가 서야 안다.** 프레임 상한이 실제로 걸리는지는
M4-03(안드로이드 패키징)·iOS 서명이 서고 기기에 올린 뒤 다시 본다 — 그때까지 **"30fps 로 돈다" 고 쓰지 않는다.**


## H. iOS 설정 절 결과 (M4-06 · 루프#81, 2026-09-21)

**무엇을 적었나:** `Config/DefaultEngine.ini` 에 `[/Script/IOSRuntimeSettings.IOSRuntimeSettings]` 절을 새로 만들었다.
값은 **전부 DESIGN 4.7 표(감독 결정 2026-09-20)에서 왔고 루프가 만든 숫자·이름은 0개**다.

| 칸 | 적은 값 | 근거 |
|---|---|---|
| `BundleIdentifier` | `com.soilrist.snarehaven.ios` | DESIGN 4.7 — 안드로이드 패키지 이름과 같은 문자열. 출시 전 사람이 바꿀 수 있다 |
| `bSupportsPortraitOrientation` · `bSupportsUpsideDownOrientation` | `False` | DESIGN 4.7 "가로만, 세로 없음" |
| `bSupportsLandscapeLeftOrientation` · `bSupportsLandscapeRightOrientation` | `True` | 〃 — 안드로이드의 `SensorLandscape`(기기를 뒤집으면 따라 돌되 세로로는 안 간다)에 해당하는 iOS 표현이 이 네 칸이다 |
| `FrameRateLock` | `PUFRL_30` | DESIGN 4.7 목표 프레임 30fps. **iOS 에서 프레임 상한의 진짜 자리가 이 칸**이다 — M4-05 의 `t.MaxFPS` 와 같은 답을 두 자리에 적은 것이지 다른 숫자가 아니다 |
| `CompanyName` (`Config/DefaultGame.ini`) | `soilrist` | 〃 — 비어 있던 칸을 채웠다 |

**일부러 적지 않은 것:**
- **`MinimumiOSVersion`** — 답이 "엔진 5.8 기본값 그대로"(DESIGN 4.7)다. 값을 박으면 엔진이 기본을 올릴 때 우리 줄이 옛 값으로
  붙잡는다. 그래서 **적지 않는 것**이 답을 지키는 길이고, 2층 검사 `Snarehaven.Mobile.IOS.LeavesMinimumVersionToEngine`
  이 "우리가 박지 않은 상태" 를 지킨다. 지금 엔진 기본은 15.0 (표 C).
- **앱 아이콘 그림** — 아이콘은 이 절이 아니라 `Build/IOS/Resources/Graphics/` 의 PNG 14장(`Icon1024.png`·`Icon60@2x.png` …)이고,
  **그림은 아직 없다**(아트 몫). **빈 폴더를 미리 만들면 오히려 해롭다** — UBT 는 그 폴더가 *있기만 하면* 엔진 기본 대신
  거기만 보므로(`IOSToolChain.cs:850`), 폴더가 비어 있으면 아이콘이 **하나도** 안 들어간다. 그래서 자리만 여기 적고 폴더는 안 만들었다.
  참고로 **이 엔진 설치본에는 `Engine/Build/IOS` 자체가 없다** — 즉 기댈 기본 아이콘도 없다. iOS 패키징을 실제로 돌릴 때
  (서명·사람 몫) 아이콘 14장이 먼저 있어야 한다.

**범위 밖(손대지 않음):** 서명·프로비저닝 프로파일·`.ipa` 패키징·App Store 업로드 — Apple 개발자 계정은 유료·사람 몫이다(Autonomy 🔴 3).

| 본 것 | 어떻게 | 결과 |
|---|---|---|
| 설정값이 사람 답과 같다 | 2층 `Snarehaven.Mobile.IOS.MatchesDecidedValues`(번들 ID·방향 네 칸·프레임 상한) | **통과** — `logs/tests_loop81.log`, 121개 전부 `Result={Success}`·`EXIT CODE: 0` |
| 최소 iOS 버전을 우리가 안 박았다 | 2층 `Snarehaven.Mobile.IOS.LeavesMinimumVersionToEngine` | **통과** — 같은 로그 |
| 그 통과가 우리 파일 덕인가 | iOS 절을 잠깐 치우고 같은 검사 | **확인** — `MatchesDecidedValues` 가 `Result={Fail}`·`EXIT CODE: -1`(`logs/tests_loop81_negative.log`). 파일은 바로 되돌렸다 |
| 설정을 넣고도 게임이 그대로 뜬다 | 3층 부팅 캡처 | **통과** — `captures/81_boot_lobby.png`(로비 4버튼) · 우리 `Error:` 0줄(`logs/game_loop81.log` 의 58줄은 전부 `LogPython` 엔진 잡음) |
| iOS 빌드가 이 값을 실제로 쓰는가 | — | **못 함** — iOS 패키징은 서명·개발자 계정이 있어야 돈다(범위 밖). 맥 실행은 iOS 절을 읽지 않는다 |
| 실기기에서 가로 고정·30fps 로 도는가 | — | **못 함** — 기기가 없다 (표 F·G 와 같은 한계) |


## I. 안드로이드 패키징 1회 시도 (M4-03 · 루프#136, 2026-09-22) — **네 단계 중 셋 통과, Gradle 에서 막혔다** (→ 표 J 에서 풀림)

돌린 명령 (한 줄, `logs/package_android_loop136.log`):

```
RunUAT.sh BuildCookRun -project=<uproject> -noP4 -platform=Android -cookflavor=ASTC \
  -clientconfig=Development -build -cook -stage -package -pak -archive \
  -archivedirectory=<repo>/Build/Android -nocompileeditor -unattended -utf8output
```

`ANDROID_HOME`·`NDKROOT`·`NDK_ROOT`·`JAVA_HOME` 을 먼저 `export` 했다 — 표 D 의 2번(비대화 셸엔 `~/.zshrc` 가 안 읽힌다) 때문이다. **덕분에 Gradle 이 JDK 자체는 찾았다**(못 찾는 것이 아니라 버전이 문제였다).

| 단계 | 한 일 | 결과 | 인용 |
|---|---|---|---|
| ① Build | NDK clang 으로 arm64 `.so` 컴파일 (93개 작업) | **통과** | `Output binary: <repo>/Binaries/Android/Snarehaven-arm64.so` · `Result: Succeeded` · `Total execution time: 107.47 seconds` |
| ② Cook | 에디터 커맨드릿이 에셋을 `Android_ASTC` 로 다시 굽는다 | **통과** | `LogCook: Display: Cooked packages 559 Packages Remain 0 Total 559` · `Took 1,183.14s to run UnrealEditor-Cmd, ExitCode=0` (20분) |
| ③ Stage/Pak | `UnrealPak IoStore` 로 묶고 스테이징 | **통과** | `Took 14.02s to run UnrealPak, ExitCode=0` · `Saved/StagedBuilds/Android_ASTC` 67M · `Saved/Cooked/Android_ASTC` 63M |
| ④ Package(Gradle) | `gradlew :app:assembleDebug` 로 `.apk` 를 싼다 | **실패** | `BUG! exception in phase 'semantic analysis' in source unit '_BuildScript_' Unsupported class file major version 69` · `BUILD FAILED in 12s` · `AutomationTool exiting with ExitCode=1 (Error_Unknown)` |

**`.apk` 는 안 나왔다.** `find Binaries/Android Build/Android Saved/StagedBuilds -name "*.apk"` 가 빈 결과다 — 로그에 적힌 목표 이름은 `BaseApkName = <repo>/Binaries/Android/Snarehaven.apk` 였다.

### 원인 (추측이 아니라 두 값이 맞물린다)

| 값 | 어디서 | 확인 |
|---|---|---|
| Gradle **8.13** | 엔진이 만든 래퍼 `Intermediate/Android/arm64/gradle/gradle/wrapper/gradle-wrapper.properties` | `distributionUrl=...gradle-8.13-all.zip` · 로그 `Welcome to Gradle 8.13!` |
| JDK **25** | Android Studio 번들 JBR — 이 기계의 **유일한** JDK(표 D) | `openjdk version "25.0.3"` |
| 클래스 파일 major **69** | 자바 클래스 포맷 번호 = `44 + 자바 버전` → `44 + 25 = 69` | 실패 메시지가 가리키는 숫자가 정확히 69 다 |

즉 **Gradle 8.13 이 자바 25 로 컴파일된 클래스를 읽지 못한다.** 엔진 5.8 은 Gradle 버전을 래퍼에 박아 두고, 이 기계엔 자바가 JBR 25 하나뿐이라 고를 여지가 없다.

### 고치는 법 (🔴 — 루프 몫이 아니다, 저장소 밖 설치다)

셋 중 하나. **1번을 권한다** — 기존 SDK·NDK 를 안 건드리고 되돌리기 쉽다.

1. **JDK 17 또는 21 을 따로 깔고 `JAVA_HOME` 을 그쪽으로** (예: Temurin / Microsoft OpenJDK). 엔진·SDK·NDK 는 그대로다.
2. Android Studio 를 JBR 이 21 인 버전으로 내린다 — SDK 경로·플러그인까지 같이 흔들린다.
3. 엔진의 Gradle 래퍼 버전을 올린다 — `UE_5.8/Engine/` 안을 고치는 일이라 **저장소 밖이고 엔진 업데이트 때 날아간다**. 권하지 않는다.

**이번에 밝혀진 것:** 표 C 의 "JDK = Android Studio 번들 JBR" 은 **엔진 문서가 가리키는 것일 뿐 버전 상한을 말하지 않는다** — 스튜디오가 새로울수록 JBR 도 새로워져 엔진이 박아 둔 Gradle 을 앞지른다. M4-01 의 `Android VALID` 는 SDK·NDK 만 보고 JDK 버전은 안 본다(그래서 전제 점검은 통과했는데 패키징은 여기서 멈춘다).

> **로그의 `ERROR:` 27줄은 실패가 아니다.** `ERROR: Process … not active but did not get exit message` 는 **UBA(Unreal Build Accelerator)가 이 맥에서 DYLD 후킹을 못 해 로컬 컴파일로 물러난 흔적**이다(바로 뒤에 `Use "UbaCli -validate …" to verify DYLD env support` 가 붙는다). UBT 는 그 작업을 로컬로 다시 돌려 `[93/93]` 을 채웠다 — 판정은 `Result: Succeeded` 한 줄로 한다(PROMPT ③-12: 이름·문구가 아니라 실제 결과로).


## J. 안드로이드 패키징 성공 (M4-03 · 루프#137, 2026-09-22) — **`.apk` 가 나왔다**

표 I 의 막힘을 감독이 1번 방법으로 풀었다: `brew install openjdk@17` + `loop/env.sh` 63행에 `JAVA_HOME` export.
**명령은 루프#136 과 글자 하나 다르지 않다** — 바뀐 것은 `JAVA_HOME` 이 가리키는 자바뿐이다.
명령을 띄운 셸에서 먼저 찍은 값: `openjdk version "17.0.20.1" 2026-08-18` / `OpenJDK Runtime Environment Homebrew (build 17.0.20.1+0)`
(루프#136 은 같은 자리에서 `25.0.3` 이었다). 로그 `logs/package_android_loop137.log` — **UAT 로그 자체엔 JDK 경로가 안 찍힌다**, 그래서 실행 셸 쪽 출력으로 남긴다.

| 단계 | 결과 | 인용 | 루프#136 대비 |
|---|---|---|---|
| ① Build | **통과** | `Target is up to date` · `Result: Succeeded` · `Total execution time: 2.90 seconds` · `Build command time: 3.35 s` | 107초 → 3초. `.so` 를 다시 안 만들었다(`No build products were made` — 루프#136 산출물이 그대로 섰다) |
| ② Cook | **통과** | `LogCook: Display: Cooked packages 559 Packages Remain 0 Total 559` · `Took 37.21s to run UnrealEditor-Cmd, ExitCode=0` | **1,183초 → 37초**(DDC 가 따뜻하다) |
| ③ Stage/Pak | **통과** | `Took 2.96s to run UnrealPak, ExitCode=0` · `Took 1.03s` · `Took 1.19s` (청크 셋) | 14.02초 → 5초 |
| ④ Package(Gradle) | **통과** | `Making .apk with Gradle...` · **`BUILD SUCCESSFUL in 1m 20s`** | **`Unsupported class file major version 69` → 사라짐** |
| ⑤ Archive | **통과** | `Archive command time: 0.15 s` · `BUILD SUCCESSFUL` · `AutomationTool exiting with ExitCode=0 (Success)` · 전체 `BuildCookRun time: 154.50 s` | 새로 도달 |

### 나온 파일

| 자리 | 경로 | 크기 |
|---|---|---|
| 빌드 산출물 | `Binaries/Android/Snarehaven-arm64.apk` | 122,494,311 바이트 (117MB) |
| 아카이브(`-archivedirectory`) | `Build/Android/Snarehaven-arm64.apk` | 같은 파일 |

**이름이 아니라 내용으로 확인했다**(PROMPT ③-12) — `unzip -l` 로 `AndroidManifest.xml`(23,516) · `classes.dex`(10,411,296) · `lib/arm64-v8a/libUnreal.so`(270,791,456), 항목 **504개**. `aapt2 dump badging` 이 읽은 값은 M4-02 가 `DefaultEngine.ini` 에 적은 DESIGN 4.7 값과 **그대로 같다**:

```
package: name='com.soilrist.snarehaven' versionCode='1' versionName='1.0' compileSdkVersion='36'
minSdkVersion:'26'
targetSdkVersion:'36'
native-code: 'arm64-v8a'
uses-implied-feature: name='android.hardware.screen.landscape' reason='one or more activities have specified a landscape orientation'
```

즉 **M4-02 의 설정이 실제 패키지까지 살아서 도착한다** — 2층 ini 검사(`Snarehaven.Mobile.Android.MatchesDecidedValues`)가 보던 값을 패키지 쪽에서 되짚은 셈이다(`arm64` 하나뿐인 것도 `native-code` 한 줄로 확인된다).

**아직 못 한 것:** 기기에 설치해 띄우는 것(3층). 기기가 없다 — 표 F·G 와 같은 한계다. 이 `.apk` 는 **Development 서명(디버그 키)** 이라 스토어에는 못 올린다(출시용 `Shipping` + 업로드 키는 M4-07·M7-08 몫).

## K. EOS 비밀 ini 가 iOS 패키지에 실리는가 (M20-05 · 루프#316, 2026-10-03) — **실린다. 폰 접속 실패는 다른 단계다**

| 확인한 것 | 결과 | 근거 |
|---|---|---|
| 비밀 절 `[EOSSDK.Platform.Snarehaven]` 이 있는 파일 | `Config/UserEngine.ini`(gitignore, 09-19 수정) — 값은 이 표에 쓰지 않는다 | `.gitignore:105` |
| 스테이징이 그 파일을 넣는가 | **넣는다** — `Saved/StagedBuilds/IOS/Manifest_UFSFiles_IOS.txt`(10-02 22:30) 1657행 `Snarehaven/Config/UserEngine.ini 2026-09-19T02:45:20.554Z` | 엔진 `CopyBuildToStagingDirectory.Automation.cs` `ShouldStageConfigFile` 가 `User*.ini` 를 허용·거부 어느 쪽에도 안 넣어 `null` → `ShouldStage ?? true` 로 경고만 찍고 싣는다 |
| 폰에서 읽는가 | 계층의 마지막 층 `GameDirUser` = `{PROJECT}/Config/User{TYPE}.ini`, 패키지라고 빼는 깃발 없음 | `Engine/Source/Runtime/Core/Public/Misc/ConfigHierarchy.h:45` (`NoExpand` 하나뿐) — **폰 실행으로는 아직 못 봤다** |
| 같이 실리는 다른 비밀 파일 | `Config/EOS.ini`(1659행)·`Config/PlayFab.ini` 도 실린다 — `EOS.ini` 는 엔진 계층에 없어 읽히지 않는 사본 | 위 매니페스트 |
| 폰 로그에서 볼 줄 | `[Online] EOS 실패: <단계> <결과 코드>` — 단계 `sdk`·`config`·`services`·`auth`·`login`·`platform`·`connect`·`device_id` | `SnareOnlineSubsystem.cpp` `MakeEosFailureLine`, 2층 `Snarehaven.Online.Login.FailureLineFormat` |

즉 「EOS 키가 패키지에 없다」는 가설은 매니페스트로 빠진다. 다음 폰 빌드에서 `EOS 실패:` 줄이 `config` 면 계층 읽기, `login`·`device_id` 면 EOS 포털(클라이언트 정책·샌드박스) 쪽이다. 알아 둘 것: EOS 클라이언트 자격(ClientId·ClientSecret)은 원래 클라이언트에 실리는 값이라 패키지 안에 있는 게 정상이다 — 서버 비밀(PlayFab 개발자 키)은 클라이언트에 없어야 한다(BACKEND.md).

### K-2. 폰 `EOS 실패: services no_services` (루프#661, 2026-10-08) — **원인: 엔진 플러그인이 iOS 를 막아 둠 → 프로젝트 사본으로 풂**

| 확인한 것 | 결과 | 근거 |
|---|---|---|
| 폰 로그 | `config` 단계는 지났고(비밀 절 읽힘) `GetServices(EOnlineServices::Epic)` 가 빈 값 | `SnareOnlineSubsystem.cpp` `services no_services` 줄 |
| 엔진 `OnlineServicesEOSGS.uplugin`·`SocketSubsystemEOS.uplugin` | 모듈 `PlatformAllowList` 에 **`IOS` 없음**(EOSGS = Win64·Mac·Linux·LinuxArm64·Android, 소켓 = Win64·Mac·Android) → iOS 타깃에 모듈이 안 들어감 | `UE_5.8/Engine/Plugins/Online/<이름>/<이름>.uplugin` |
| 엔진 소스는 iOS 를 지원하나 | **한다** — `OnlineServicesEOSGS/Source/Private/Online/IOS/IOSEOSAuthLoginOptions.cpp`, `Build.cs` iOS 분기, `EOSSDK/SDK/Bin/IOS/EOSSDK.framework`. `EOSShared`·`OnlineServicesEpicCommon` 은 원래 IOS 허용 | 엔진 소스 |
| 해결 | `Plugins/OnlineServicesEOSGS`·`Plugins/SocketSubsystemEOS` = 엔진 원본 사본(소스 `diff -r` 동일) + uplugin 모듈에 `"IOS"` 한 줄. 엔진·UBT 둘 다 같은 이름이면 프로젝트 쪽을 쓴다 | `PluginManager.cpp:1280` · `UnrealBuildTool/System/Plugins.cs:304` · 에디터 로그 `prioritizing project plugin` |
| iOS 빌드에 들어가나 | **들어간다** — `Build.sh Snarehaven IOS Development` 성공, `[4/333] Compile [arm64] OnlineServicesEOSGSModule.cpp`·`IOSEOSAuthLoginOptions.cpp` 컴파일, 실행 파일 `nm` 에 `FOnlineServicesEOSGS` 66개, `Snarehaven.app/Frameworks/EOSSDK.framework` | `loop/tmp/661/build_ios.log` |
| 지키는 시험 | 2층 `Snarehaven.Mobile.IOS.AllowsEosPlugins` — 사본이 지워지거나 `IOS` 가 빠지면 깨짐 | `SnarehavenMobileProfileTest.cpp` |
| 남은 것 | 폰 패키징·설치 뒤 로그에 `services` 실패 줄이 사라지고 다음 단계(`auth`·`login`)로 가는지 — **폰 확인 못 함(감독)** | — |

엔진을 올릴 때(5.8.x→) 사본을 엔진 새 원본으로 다시 떠서 `IOS` 줄만 다시 더한다(엔진이 IOS 를 허용하게 되면 사본을 지운다).

## iOS Shipping 점검 (감독, 2026-10-14 새벽)

명령: `RunUAT BuildCookRun -platform=IOS -clientconfig=Shipping …` (로그 `logs/package_ios_shipping_director.log`·`logs/cook_ios_shipping_director.log`, 저장소 밖)

| 단계 | 결과 | 근거 |
|---|---|---|
| C++ 컴파일·링크 | **통과** (고친 뒤) | 처음엔 `SnarehavenMinimapTest.cpp` 오류 14개 — 시험이 `#endif` 뒤에 있어 Shipping 에서 깨짐. `#endif` 를 파일 끝으로 옮긴 뒤 `Link [arm64] Snarehaven-IOS-Shipping` 성공. 재발 막음: `check_conventions.py` R16 |
| 쿡 | **통과** | `Success - 0 error(s), 2 warning(s)` |
| 서명·스테이지·패키지 | **막힘 — 사람 몫** | `error: No Accounts: Add a new account in Accounts settings.` · `No profiles for 'com.soilrist.snarehaven.ios'` → 스테이지는 영수증(`.target`) 없음으로 멈춤 |
| 앱 아이콘 | **있음** | `Build/IOS/Resources/Assets.xcassets/AppIcon.appiconset/` 4장, `Icon1024.png` 1024px·알파 없음(`sips`) |

**사람이 할 일 (한 번):** Xcode → Settings → Accounts 에 유료 팀 Apple ID 추가 → 같은 명령을 다시 돌리면 자동 서명이 프로필을 만든다. 업로드는 Xcode Organizer 또는 `xcrun altool`(App Store Connect 앱을 먼저 만든 뒤).
