// ROADMAP M3-01a — EOS Connect 디바이스 ID(게스트) 로그인.
// 명세 `docs/specs/net-eos-session.md`, 결정 `docs/decisions/004-eos-ossv2.md`.
// OSSv1(`IOnlineIdentity`)이 아니라 **OSSv2 `OnlineServicesEOSGS`** 를 쓴다 — 이 엔진의 OSSv1 은
// `ADD_USER_LOGIN_INFO=0` 이라 디바이스 ID 로그인이 원천적으로 거절된다(결정 004).
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/EngineBaseTypes.h"
#include "Net/Core/Connection/NetEnums.h"
#include "Online/Auth.h"
#include "Online/OnlineServices.h"
#include "Online/Sessions.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "SnareOnlineSubsystem.generated.h"

/** 로그인이 어디까지 갔는지. 세션(M3-01b)이 이 값을 보고 움직인다. */
UENUM()
enum class ESnareLoginState : uint8
{
	NotStarted,
	InProgress,
	LoggedIn,
	Failed
};

/** 방(EOS 세션)이 어디까지 갔는지. M3-01c 가 이 값을 보고 맵을 연다. */
UENUM()
enum class ESnareRoomState : uint8
{
	None,
	Creating,
	Hosting,
	Searching,
	Joining,
	Joined,
	Failed
};

/**
 * 게임이 시작될 때 EOS 에 게스트(디바이스 ID)로 로그인한다.
 * 화면에 보이는 것은 없다 — 확인은 로그(`LogSnarehaven` 의 `[Online]` 줄)로 한다.
 */
UCLASS()
class SNAREHAVEN_API USnareOnlineSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** 게임 인스턴스가 내려갈 때 = 이 사람이 게임을 끌 때. 호스트면 여기서 방이 해산된다(M3-03). */
	virtual void Deinitialize() override;

	/** 로그인을 시작한다. 이미 되어 있거나 하는 중이면 아무것도 안 한다. */
	void StartLogin();

	ESnareLoginState GetLoginState() const { return LoginState; }

	/** 로그인에 성공했을 때만 유효. M3-01b 세션이 이 계정으로 방을 만든다. */
	const UE::Online::FAccountId& GetLocalAccountId() const { return LocalAccountId; }

	/**
	 * 시작하자마자 알아서 로그인할 상황인가.
	 * **실제 게임 실행(`-game`·패키지)일 때만 참.** 에디터·커맨드릿·자동화 검사에서는 거짓 —
	 * 검사 한 번 돌릴 때마다 EOS 에 접속하지 않기 위해서다. PIE 에서 붙여 볼 때는 `StartLogin()` 을 직접 부른다.
	 */
	static bool ShouldAutoLogin();

	/**
	 * EOS 플랫폼 설정이 이 기계에 있는가 (`[EOSSDK.Platform.<이름>]` 절).
	 * 비밀값은 `Config/UserEngine.ini`(gitignore) 에만 있으므로 갓 받은 저장소에서는 거짓이다.
	 */
	static bool HasEosPlatformConfig();

	/** 로그인에 넘길 값. 디바이스 ID = 외부 인증(ExternalAuth) 의 한 종류다. */
	static UE::Online::FAuthLogin::Params MakeDeviceIdLoginParams();

	/**
	 * M20-05: 로그인이 멈춘 자리 한 줄 — `[Online] EOS 실패: <단계> <결과 코드>`. 폰 로그에서 이 줄 하나로 어디서 끊겼는지 본다.
	 * 단계는 `sdk`·`config`·`services`·`auth`·`login`·`platform`·`connect`·`device_id`, 코드가 비면 `unknown`.
	 * 키 값(ProductId·ClientSecret 등)은 절대 넣지 않는다 — 단계와 코드만.
	 */
	static FString MakeEosFailureLine(const FString& Stage, const FString& ResultCode);

	// --- M3-01b 방(세션) ------------------------------------------------------
	// 사람 손으로 부르는 자리는 아직 없다(로비 UI 는 M3-04) — 콘솔 명령 `Snare.Online.*` 으로 부른다.

	/** 방을 만든다(호스트). 로그인이 끝난 뒤에만 된다. */
	void CreateRoom();

	/** 우리 게임 방을 찾는다. 결과는 `GetFoundRooms()`. 0개는 오류가 아니다(명세 2장). */
	void FindRooms();

	/** 찾은 방 중 Index 번째에 들어간다. */
	void JoinFoundRoom(int32 Index);

	ESnareRoomState GetRoomState() const { return RoomState; }

	/**
	 * 로그인이 끝난 뒤 명령줄이 시킨 대로 방을 연다 — `-SnareRoom=host` 는 만들고,
	 * `-SnareRoom=join` 은 찾아서 첫 방에 들어간다. 헤드리스(`-game`)에는 키보드가 없어서
	 * 콘솔 명령을 칠 수 없다 — 3층 검사와 M3-01c 의 두 프로세스가 이 스위치로 움직인다.
	 */
	void RunCommandLineRoomMode();
	const TArray<UE::Online::FOnlineSessionId>& GetFoundRooms() const { return FoundRoomIds; }

	/** 방 최대 인원 — DESIGN 3.4, 감독 결정 2026-09-19. */
	static constexpr uint32 MaxRoomPlayers = 8;

	/** 검색 결과 상한 — 명세 3장(🟢 기술값, EOS 기본). */
	static constexpr uint32 MaxRoomSearchResults = 50;

	/** 세션의 지역 이름. 한 프로세스가 방 하나만 갖는 동안은 고정이다. */
	static const FName RoomSessionName;

	/**
	 * "이건 Snarehaven 방이다" 표시. 만들 때 공개 속성으로 심고 찾을 때 같은 값으로 거른다 —
	 * 이게 없으면 EOS 샌드박스의 남의 세션까지 딸려 온다. EOS 버킷은 따로 주지 않는다:
	 * 엔진이 `BuildUniqueId` 를 버킷으로 써서(`SessionsEOSGS.cpp:349`) 같은 빌드끼리 이미 갈린다.
	 */
	static const FName RoomTagKey;
	static const TCHAR* RoomTagValue;

	/**
	 * 방 만들기에 넘길 값. 네트워크 없이 2층이 검사할 수 있게 따로 뺐다.
	 * @param HostConnectAddress 게스트가 접속할 주소(M3-05). 비어 있으면 표지판을 달지 않는다 —
	 *        M3-01b 처럼 EOS 쪽 방만 세워 보는 길이 남아 있어야 한다.
	 */
	static UE::Online::FCreateSession::Params MakeCreateRoomParams(const UE::Online::FAccountId& AccountId,
		const FString& HostConnectAddress = FString());

	/** 방 찾기에 넘길 값. 위와 같은 이유로 따로. */
	static UE::Online::FFindSessions::Params MakeFindRoomsParams(const UE::Online::FAccountId& AccountId);

	// --- M3-03 호스트 이탈 → 방 해산 → 로비 복귀 --------------------------------
	// DESIGN 3.4: 호스트가 끊기면 **방 해산**. 호스트 마이그레이션은 없다.

	/**
	 * 지금 들어가 있는 방에서 나온다. 호스트면 방 자체를 EOS 에서 지운다(해산) — 남겨 두면
	 * 아무도 없는 방이 남의 검색 결과에 계속 뜬다. 들어간 방이 없으면 아무것도 하지 않는다.
	 */
	void LeaveRoom();

	/**
	 * 연결이 끊겼을 때 로비로 돌아갈 쪽인가. **게스트(NM_Client)만 참이다.**
	 * 리슨 서버인 호스트도 게스트 하나가 떨어지면 같은 실패 알림을 받는데, 여기서 갈라 두지 않으면
	 * 호스트가 남은 사람들을 두고 혼자 로비로 나가 방이 통째로 죽는다.
	 */
	static bool ShouldReturnToLobby(ENetMode NetMode);

	/**
	 * 방에 있는 채 앱이 이만큼 백그라운드면 방을 떠난다(호스트면 해산) — DESIGN 3.4 「백그라운드 유예」(M21-08a).
	 * 전화·알림을 잠깐 보고 돌아오는 사람까지 쫓아내지 않을 만큼만 기다린다.
	 */
	static constexpr double BackgroundGraceSeconds = 30.0;

	/**
	 * 백그라운드로 간 지 `AwaySeconds` 지났을 때 방을 떠날지. 방 밖(혼자 사냥·로비)이면 언제나 거짓.
	 * 2층이 시계 없이 본다.
	 */
	static bool ShouldDisbandAfterBackground(bool bInRoom, double AwaySeconds);

	/**
	 * 받는 소켓이 죽었나 — 데이터를 꺼내지 않고 엿봐서(Peek) "기다리면 온다" 가 아닌 오류면 참. 소켓이 없어도 참.
	 * iOS 는 앱이 멈춘 동안 리슨 서버 UDP 소켓을 거둬 가는데, 돌아온 뒤 엔진 수신 루프가 그 소켓에서
	 * `SE_ENOTCONN` 을 끝없이 받아 한 프레임에 갇힌다(10-04 폰 로그 76만 줄·8초 멈춤, INBOX 18:20).
	 */
	static bool IsRecvSocketBroken(class FSocket* Socket);

	/** 앱이 돌아왔을 때 리슨 서버 소켓을 다시 열지. 리슨 서버이고 소켓이 죽었을 때만 — 게스트·혼자(서버 아님)는 손대지 않는다. */
	static bool ShouldReopenListenSocket(ENetMode NetMode, bool bSocketBroken);

	/**
	 * 리슨 서버 소켓이 죽었으면 넷 드라이버를 내리고 같은 URL 로 다시 연다. 죽은 소켓에 붙어 있던 게스트는
	 * 어차피 못 받으니 끊긴다. 다시 열었으면 참.
	 */
	bool ReopenListenSocketIfBroken();

	/** 방이 끝나 로비로 돌아온 상태인가. 게임모드가 이걸 보고 자동 진입을 멈춘다. */
	bool HasReturnedFromRoom() const { return bReturnedFromRoom; }

	/**
	 * 마지막 멀티 입장 거절 코드 — `DT_EntryRejectTable` 행 이름(`room_full`·`version_mismatch`·`room_join_failed`).
	 * 거절이 없었으면 빈 글자. 로비 상태 줄이 이걸 문구로 바꿔 보인다. 새로 방을 찾거나 들어가면 지운다.
	 */
	const FString& GetLastRejectCode() const { return LastRejectCode; }

	/** 방 참가 실패 → 거절 코드. EOS 가 `session_full` 이면 `room_full`, 그 밖은 `room_join_failed`. 2층이 네트워크 없이 본다. */
	static FString ClassifyJoinError(const UE::Online::FOnlineError& Error);

	/** 연결 실패 → 거절 코드. 버전이 달라 엔진이 끊었으면(`Outdated*`) `version_mismatch`, 아니면 빈 글자. */
	static FString ClassifyNetworkFailure(ENetworkFailure::Type FailureType);

	/**
	 * 방이 끝났을 때 돌아갈 맵. 〔가정: 타이틀 맵이 곧 로비다 — 로비 화면(`WBP_Lobby`)은 M3-04 가 이 맵 위에 올린다〕
	 */
	static const FName LobbyLevelName;

	/**
	 * 방 떠나기에 넘길 값. 위 둘과 같은 이유로 따로 뺐다(네트워크 없이 2층이 본다).
	 * @param bDestroy 호스트면 참 — 나가는 게 아니라 방을 지운다.
	 */
	static UE::Online::FLeaveSession::Params MakeLeaveRoomParams(const UE::Online::FAccountId& AccountId, bool bDestroy);

	// --- M3-05 방에 들어가면 사냥 맵으로 ----------------------------------------
	// DESIGN 3.4 는 EOS P2P **리슨 서버**다 — 호스트가 곧 서버다. 그래서 EOS 방(세션)은 게임을 나르지 않고
	// "어디로 접속하면 되는지" 만 적어 두는 표지판이고, 실제 게임은 언리얼 네트워크가 연다.
	// 방을 연 사람은 서버가 되러 떠나고(ServerTravel), 들어간 사람은 그 표지판을 읽어 접속한다(ClientTravel).

	/** 호스트가 방에 적어 두는 "여기로 접속해라" 칸. 게스트가 참가 직후 읽는다. */
	static const FName RoomAddressKey;

	/**
	 * 호스트가 방에 실어 보낼 주소를 만든다.
	 * 기본은 EOS P2P 주소(`EOS:<PUID>` — `EOSSharedTypes.h` 의 `EOS_CONNECTION_URL_PREFIX` 형식).
	 * `AddressOverride` 가 있으면 그 값이 이긴다: 같은 기계 두 프로세스는 디바이스 ID 가 하나라 PUID 도 같아
	 * P2P 로 서로를 가릴 수 없다(M3-01c2 가 두 번째 기기를 기다리는 이유) — 3층 두 프로세스 검사는
	 * `-SnareHostAddr=127.0.0.1` 로 같은 길을 직접 IP 로 걷는다.
	 * @return 방에 실을 주소. 줄 것이 없으면 빈 문자열.
	 */
	static FString MakeHostConnectAddress(const FString& PuidString, const FString& AddressOverride);

	/** 방에 적힌 접속 주소를 꺼낸다. 칸이 없거나 글자가 아니면 빈 문자열(= 표지판 없는 방). */
	static FString ReadHostConnectAddress(const UE::Online::FCustomSessionSettingsMap& CustomSettings);

	/**
	 * 호스트가 떠날 URL — 사냥 맵을 **리슨 서버로** 연다. `?listen` 이 빠지면 호스트 혼자 놀고
	 * 게스트는 접속할 곳이 없어진다.
	 */
	static FString MakeHuntServerTravelUrl();

	/**
	 * 존 하나로 떠날 URL (ROADMAP M6-02a). 맵 이름은 `DT_ZoneTable` 이 정하고 포탈이 넘긴다 —
	 * 여기서 맵 이름을 만들지 않는다 (DataDriven.md 1). 위 사냥 URL 도 이걸 부른다.
	 * @param LevelName 갈 맵 이름 (`FZoneRow::LevelName`)
	 * @return `<맵>?listen` — 맵 이름이 비었으면 빈 문자열(떠나지 않는다)
	 */
	static FString MakeZoneServerTravelUrl(FName LevelName);

private:
	void HandleLoginComplete(const UE::Online::TOnlineResult<UE::Online::FAuthLogin>& Result);

	/** 이 기기에 디바이스 ID 가 아직 없을 때 하나 만들고 로그인을 딱 한 번 더 시도한다. */
	void CreateDeviceIdThenRetry();

	ESnareLoginState LoginState = ESnareLoginState::NotStarted;
	UE::Online::FAccountId LocalAccountId;

	/** 재시도는 1회(명세 3장) — 이 깃발이 두 번째를 막는다. */
	bool bTriedDeviceIdCreate = false;

	/** 로그인이 끝났고 세션 인터페이스가 살아 있으면 돌려준다. 아니면 이유를 로그에 남기고 널. */
	UE::Online::ISessionsPtr GetSessionsForRoom();

	/** 명령줄 `-SnareHostAddr=` 값. 헤드리스 두 프로세스 검사용 — 없으면 빈 문자열. */
	static FString GetHostAddressOverride();

	/** 호스트가 사냥 맵으로 떠난다(M3-05). 방 만들기가 성공한 뒤에만 부른다. */
	void TravelToHuntAsServer();

	/** 게스트가 방에 적힌 주소로 접속한다(M3-05). 참가가 성공한 뒤에만 부른다. */
	void TravelToJoinedRoom();

	void HandleCreateRoomComplete(const UE::Online::TOnlineResult<UE::Online::FCreateSession>& Result);
	void HandleFindRoomsComplete(const UE::Online::TOnlineResult<UE::Online::FFindSessions>& Result);
	void HandleJoinRoomComplete(const UE::Online::TOnlineResult<UE::Online::FJoinSession>& Result);

	ESnareRoomState RoomState = ESnareRoomState::None;

	/** `-SnareRoom=join` 일 때만 참 — 찾기가 끝나면 첫 방으로 이어 간다. */
	bool bAutoJoinFoundRoom = false;

	TArray<UE::Online::FOnlineSessionId> FoundRoomIds;

	/** 엔진이 연결 끊김을 알려 주는 곳(M3-03). 끊긴 뒤에는 걸 수 없어 시작할 때 걸어 둔다. */
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	FDelegateHandle NetworkFailureHandle;

	// --- M21-08a 앱 백그라운드 유예 --------------------------------------------
	// 폰은 백그라운드에서 멈출 수 있어 티커만 믿지 않는다 — 돌아왔을 때도 지난 시간으로 한 번 더 본다.
	void HandleAppWillEnterBackground();
	void HandleAppEnteredForeground();
	bool HandleBackgroundGraceTick(float DeltaTime);

	/** 유예가 지났으면 방을 떠나 로비로 간다. 둘 중 먼저 온 쪽(티커·돌아옴)만 실제로 떠난다. */
	void DisbandIfBackgroundTooLong();
	void ClearBackgroundGrace();

	bool IsInRoom() const { return RoomState == ESnareRoomState::Hosting || RoomState == ESnareRoomState::Joined; }

	FDelegateHandle AppBackgroundHandle;
	FDelegateHandle AppForegroundHandle;
	FTSTicker::FDelegateHandle BackgroundGraceTicker;

	/** 방에 있는 채 백그라운드로 간 시각. 0 이면 지금 백그라운드가 아니거나 방 밖에서 갔다. */
	FDateTime BackgroundedAtUtc;

	bool bReturnedFromRoom = false;

	FString LastRejectCode;
};
