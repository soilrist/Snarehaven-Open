# 결정 004: EOS 는 OSSv1 이 아니라 OSSv2(`OnlineServicesEOSGS`) 로 붙인다

- 날짜: 2026-09-19
- 정한 사람: 루프#34 (🟢 "사용할 엔진 기능" — Autonomy.md 초록. 방향이 갈리는 선택이라 결정 기록을 남긴다)
- 상태: 확정

## 상황
M3-01a(게스트 로그인)를 엔진 OSSv1 `OnlineSubsystemEOS` 의 `IOnlineIdentity::Login`
+ `externalauth:DeviceIdAccessToken` 으로 만들어 3층(`-game` 로그)까지 돌렸더니 EOS SDK 가
거절했다.

```
LogEOSSDK: Error: LogEOSConnect: Invalid parameter EOS_Connect_LoginOptions.UserLoginInfo reason: must not be null
LogOnline: Warning: EOS: ConnectLoginNoEAS(0) failed with EOS result code (EOS_InvalidParameters)
```

원인은 우리 코드가 아니라 엔진 빌드 스위치다.
- `UserManagerEOS.cpp` 의 `EOS_Connect_UserLoginInfo` 대입은 **전부** `#if ADD_USER_LOGIN_INFO`
  안에 있다 (`:1137`·`:1230`·`:1334`·`:1386`·`:1435`).
- 그 값은 `OnlineSubsystemEOS.Build.cs:45` 의 `protected virtual bool bAddUserLoginInfo => false`.
  콘솔 플랫폼 확장만 `true` 로 덮어쓰는데, 이 엔진 설치에는 확장이 하나도 없다
  (`grep -rn bAddUserLoginInfo --include=*.cs` 결과가 그 파일 두 줄뿐).
- EOS SDK 는 `EOS_ECT_DEVICEID_ACCESS_TOKEN` 에 `UserLoginInfo.DisplayName` 을 **필수**로 받는다.

→ **이 엔진의 OSSv1 EOS 로는 Mac·iOS·Android 어디서도 디바이스 ID 게스트 로그인이 되지 않는다.**
엔진을 고치는 것은 금지(공용 설치 + Autonomy 🔴-6 규칙 밖), 포크도 과하다.

## 고른 것
`OnlineServicesEOSGS` 플러그인(OSSv2)으로 로그인·세션을 붙인다. 같은 EOS SDK·같은
`SocketSubsystemEOS`/`NetDriverEOS` 를 쓰므로 P2P 리슨 서버 계획(DESIGN 4.1)은 그대로다.

- `AuthEOSGS.cpp:193` 이 `DeviceIdAccessToken` 에 `DisplayName = "PlatformUser"` 를 채워 넣는다
  (`EEOSConnectTranslationFlags::DisplayName`, 표는 `:117`). 조건부 컴파일이 없다.
- 세션은 같은 플러그인의 `SessionsEOSGS.cpp`, 로비는 `LobbiesEOSGS.cpp`.
- `OnlineServicesEOS`(EAS 포함) 는 켜지 **않는다**. 그러면 `EOnlineServices::Epic` 이
  EAS 없는 `FOnlineServicesEOSGS` 로 풀린다 (`OnlineServicesEOSGSModule.cpp:47`) — 게스트 로그인
  방침(DESIGN 4.4)과 정확히 맞는다.
- `EOS_Connect_CreateDeviceId` 는 **여전히 우리가 부른다.** 엔진 전체에 그 호출이 없다
  (`grep -rn CreateDeviceId Engine/Plugins/Online/` → SDK 헤더뿐).

## 고려했지만 안 고른 것
- **OSSv1 유지 + Connect 로그인을 SDK 로 직접** — `FOnlineSessionEOS` 가 `UserManagerEOS` 에
  등록된 로컬 유저의 PUID 를 보고 움직인다. 우리가 밖에서 로그인해도 OSS 는 모르므로 세션이 안 된다.
- **엔진 수정(`bAddUserLoginInfo` 를 true 로)** — 공용 엔진 설치를 고치는 것. 엔진 업데이트마다 날아가고,
  다른 사람 기계에서 재현이 안 된다.
- **EAS(Epic 계정) 로그인** — DESIGN 4.4 가 게스트 로그인으로 못 박았다. Epic 계정 창을 띄우게 된다.

## 결과 (좋아지는 것 / 감수하는 것)
- 좋아지는 것: 조건부 컴파일에 안 걸린다. 세션·로비·통계가 한 플러그인 안에 있다. Epic 의 현재 방향이다.
- 감수하는 것: 인터페이스가 OSSv1 과 다르다(`TOnlineAsyncOpHandle`·`TOnlineResult`·`FAccountId`).
  `IOnlineSession` 예제를 그대로 못 베낀다. 명세 `docs/specs/net-eos-session.md` 5장을 다시 썼다.
- 되돌리기: 이 저장소에 OSSv1 코드가 커밋된 적이 없다(루프#34 가 3층에서 막혀 지웠다). 되돌릴 빚이 없다.
