#include "Online/SnareOnlineSubsystem.h"
#include "SnareCheckSwitch.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "IEOSSDKManager.h"
#include "IpNetDriver.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Online/OnlineAsyncOpHandle.h"
#include "Online/OnlineErrorDefinitions.h"
#include "Online/OnlineResult.h"
#include "Online/OnlineServices.h"
#include "Online/Sessions.h"
#include "Snarehaven.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "SnarehavenGameMode.h"

#include "EOSSharedTypes.h"
#include "eos_sdk.h"
#include "eos_connect.h"

using namespace UE::Online;

namespace
{
	/** EOS 에 보이는 기기 이름. 계정이 아니라 기기 구분용이라 사람이 정할 숫자가 아니다. */
	const char* DeviceModelUtf8 = "Snarehaven";

	/** 콜백이 돌아왔을 때 서브시스템이 이미 사라졌을 수 있다 — 약한 참조를 힙에 얹어 보낸다. */
	using FWeakSubsystem = TWeakObjectPtr<USnareOnlineSubsystem>;
}

void USnareOnlineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// M3-03: 연결이 끊긴 **뒤에는** 델리게이트를 걸 수 없다 — 로그인과 달리 에디터에서도 먼저 걸어 둔다.
	if (GEngine != nullptr)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &USnareOnlineSubsystem::HandleNetworkFailure);
	}

	// M21-08a: 홈 버튼·전화로 앱이 내려가면 유예를 센다(DESIGN 3.4).
	AppBackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddUObject(
		this, &USnareOnlineSubsystem::HandleAppWillEnterBackground);
	AppForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(
		this, &USnareOnlineSubsystem::HandleAppEnteredForeground);

	if (!ShouldAutoLogin())
	{
		return;
	}

	StartLogin();
}

void USnareOnlineSubsystem::Deinitialize()
{
	if (GEngine != nullptr && NetworkFailureHandle.IsValid())
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		NetworkFailureHandle.Reset();
	}
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(AppBackgroundHandle);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(AppForegroundHandle);
	ClearBackgroundGrace();

	// 호스트가 앱을 끄는 것도 이탈이다(DESIGN 3.4) — 방을 남기면 빈 방이 검색에 계속 뜬다.
	LeaveRoom();

	Super::Deinitialize();
}

bool USnareOnlineSubsystem::ShouldAutoLogin()
{
	// 에디터(PIE 포함)·커맨드릿에서는 자동으로 붙지 않는다. 자동화 검사가 매번 EOS 를 두드리지 않게.
	return IsRunningGame() && !GIsEditor;
}

bool USnareOnlineSubsystem::HasEosPlatformConfig()
{
	IEOSSDKManager* const Manager = IEOSSDKManager::Get();
	if (Manager == nullptr)
	{
		return false;
	}

	const FString& PlatformConfigName = Manager->GetDefaultPlatformConfigName();
	if (PlatformConfigName.IsEmpty())
	{
		return false;
	}

	return Manager->GetPlatformConfig(PlatformConfigName, /*bLoadIfMissing=*/true) != nullptr;
}

FAuthLogin::Params USnareOnlineSubsystem::MakeDeviceIdLoginParams()
{
	// `AuthEOSGS.cpp` 는 이 세 가지가 맞아떨어질 때만 Connect 로그인으로 간다:
	// ExternalAuth + DeviceIdAccessToken + 0번 로컬 유저. 토큰 본문은 비어 있는 게 맞다 —
	// 디바이스 ID 는 SDK 가 기기에서 꺼내 쓴다.
	FAuthLogin::Params Params;
	Params.PlatformUserId = FPlatformMisc::GetPlatformUserForUserIndex(0);
	Params.CredentialsType = LoginCredentialsType::ExternalAuth;
	Params.CredentialsToken.Emplace<FExternalAuthToken>(
		FExternalAuthToken{ ExternalLoginType::DeviceIdAccessToken, FString() });
	return Params;
}

void USnareOnlineSubsystem::StartLogin()
{
	if (LoginState == ESnareLoginState::InProgress || LoginState == ESnareLoginState::LoggedIn)
	{
		return;
	}

	if (IEOSSDKManager::Get() == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s"), *MakeEosFailureLine(TEXT("sdk"), TEXT("no_manager")));
		LoginState = ESnareLoginState::Failed;
		return;
	}

	if (!HasEosPlatformConfig())
	{
		// 설정이 없는 것은 오류가 아니다 — 저장소를 갓 받은 사람은 싱글로 계속 논다 (명세 2장).
		// 폰에서는 이 줄이 「비밀 ini 가 패키지에 안 실렸다」는 뜻이다(docs/MOBILE_BUILD.md 표 I).
		UE_LOG(LogSnarehaven, Warning,
			TEXT("[Online] EOS 설정이 없어 로그인을 건너뛴다 — Config/UserEngine.ini 의 [EOSSDK.Platform.<이름>] 절"));
		UE_LOG(LogSnarehaven, Warning, TEXT("%s"), *MakeEosFailureLine(TEXT("config"), TEXT("missing_platform_config")));
		LoginState = ESnareLoginState::Failed;
		return;
	}

	const IOnlineServicesPtr Services = GetServices(EOnlineServices::Epic);
	if (!Services.IsValid())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] EOS 온라인 서비스를 만들지 못했다 — OnlineServicesEOSGS 플러그인 확인"));
		UE_LOG(LogSnarehaven, Warning, TEXT("%s"), *MakeEosFailureLine(TEXT("services"), TEXT("no_services")));
		LoginState = ESnareLoginState::Failed;
		return;
	}

	const IAuthPtr Auth = Services->GetAuthInterface();
	if (!Auth.IsValid())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] EOS 인증 인터페이스가 없다"));
		UE_LOG(LogSnarehaven, Warning, TEXT("%s"), *MakeEosFailureLine(TEXT("auth"), TEXT("no_auth_interface")));
		LoginState = ESnareLoginState::Failed;
		return;
	}

	LoginState = ESnareLoginState::InProgress;
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] EOS 게스트 로그인 시작 (디바이스 ID%s)"),
		bTriedDeviceIdCreate ? TEXT(", 재시도") : TEXT(""));

	Auth->Login(MakeDeviceIdLoginParams())
		.OnComplete(this, &USnareOnlineSubsystem::HandleLoginComplete);
}

void USnareOnlineSubsystem::HandleLoginComplete(const TOnlineResult<FAuthLogin>& Result)
{
	if (Result.IsOk())
	{
		LocalAccountId = Result.GetOkValue().AccountInfo->AccountId;
		LoginState = ESnareLoginState::LoggedIn;
		UE_LOG(LogSnarehaven, Log, TEXT("[Online] EOS 로그인 성공 — PUID %s"), *ToLogString(LocalAccountId));
		RunCommandLineRoomMode();
		return;
	}

	const FString ErrorText = Result.GetErrorValue().GetLogString();
	if (!bTriedDeviceIdCreate)
	{
		// 이 기기에 디바이스 ID 가 아직 없을 때가 첫 실행의 정상 흐름이다. 하나 만들고 딱 한 번 더.
		UE_LOG(LogSnarehaven, Log, TEXT("[Online] EOS 로그인 실패 — 디바이스 ID 를 만들고 다시 시도한다: %s"), *ErrorText);
		bTriedDeviceIdCreate = true;
		LoginState = ESnareLoginState::NotStarted;
		CreateDeviceIdThenRetry();
		return;
	}

	LoginState = ESnareLoginState::Failed;
	UE_LOG(LogSnarehaven, Warning, TEXT("[Online] EOS 로그인 포기 — %s"), *ErrorText);
	UE_LOG(LogSnarehaven, Warning, TEXT("%s"), *MakeEosFailureLine(TEXT("login"), Result.GetErrorValue().GetErrorId()));
}

FString USnareOnlineSubsystem::MakeEosFailureLine(const FString& Stage, const FString& ResultCode)
{
	return FString::Printf(TEXT("[Online] EOS 실패: %s %s"),
		Stage.IsEmpty() ? TEXT("unknown") : *Stage, ResultCode.IsEmpty() ? TEXT("unknown") : *ResultCode);
}

void USnareOnlineSubsystem::CreateDeviceIdThenRetry()
{
	IEOSSDKManager* const Manager = IEOSSDKManager::Get();
	const TArray<IEOSPlatformHandlePtr> Platforms = Manager != nullptr ? Manager->GetActivePlatforms() : TArray<IEOSPlatformHandlePtr>();
	if (Platforms.Num() == 0)
	{
		LoginState = ESnareLoginState::Failed;
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] EOS 플랫폼 핸들이 없어 디바이스 ID 를 만들 수 없다"));
		UE_LOG(LogSnarehaven, Warning, TEXT("%s"), *MakeEosFailureLine(TEXT("platform"), TEXT("no_active_platform")));
		return;
	}

	const EOS_HConnect ConnectHandle = EOS_Platform_GetConnectInterface(*Platforms[0]);
	if (ConnectHandle == nullptr)
	{
		LoginState = ESnareLoginState::Failed;
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] EOS Connect 인터페이스를 얻지 못했다"));
		UE_LOG(LogSnarehaven, Warning, TEXT("%s"), *MakeEosFailureLine(TEXT("connect"), TEXT("no_connect_interface")));
		return;
	}

	EOS_Connect_CreateDeviceIdOptions Options = {};
	Options.ApiVersion = EOS_CONNECT_CREATEDEVICEID_API_LATEST;
	Options.DeviceModel = DeviceModelUtf8;

	EOS_Connect_CreateDeviceId(ConnectHandle, &Options, new FWeakSubsystem(this),
		[](const EOS_Connect_CreateDeviceIdCallbackInfo* Data)
		{
			// SDK 틱은 게임 스레드에서 돈다 — 여기서 바로 이어 가도 된다.
			TUniquePtr<FWeakSubsystem> WeakSelf(static_cast<FWeakSubsystem*>(Data->ClientData));
			USnareOnlineSubsystem* const Self = WeakSelf->Get();
			if (Self == nullptr)
			{
				return;
			}

			// 이미 있는 기기면 EOS_DuplicateNotAllowed 가 오는데, 그건 "만들 필요가 없었다" 는 뜻이라 성공과 같다.
			if (Data->ResultCode != EOS_EResult::EOS_Success && Data->ResultCode != EOS_EResult::EOS_DuplicateNotAllowed)
			{
				Self->LoginState = ESnareLoginState::Failed;
				UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 디바이스 ID 만들기 실패 — %hs"),
					EOS_EResult_ToString(Data->ResultCode));
				UE_LOG(LogSnarehaven, Warning, TEXT("%s"), *MakeEosFailureLine(TEXT("device_id"),
					UTF8_TO_TCHAR(EOS_EResult_ToString(Data->ResultCode))));
				return;
			}

			Self->StartLogin();
		});
}

// --- M3-01b 방(세션) --------------------------------------------------------
// `ISessions`(`SessionsEOSGS`) 로 만들기·찾기·참가. 맵을 실제로 여는 것은 M3-01c 다 —
// 여기서는 EOS 쪽 방이 서고, 검색에 걸리고, 들어가지는 것까지만 확인한다(명세 5장).

const FName USnareOnlineSubsystem::RoomSessionName(TEXT("SnareRoom"));
const FName USnareOnlineSubsystem::RoomTagKey(TEXT("SNAREHAVEN_ROOM"));
const TCHAR* USnareOnlineSubsystem::RoomTagValue = TEXT("1");

FCreateSession::Params USnareOnlineSubsystem::MakeCreateRoomParams(const FAccountId& AccountId,
	const FString& HostConnectAddress)
{
	FCreateSession::Params Params;
	Params.LocalAccountId = AccountId;
	Params.SessionName = RoomSessionName;
	// 스키마 이름은 비워 둘 수 없다 — `SessionsCommon.cpp:439` 가 없으면 `invalid_params` 로 바로 되돌린다.
	// EOSGS 는 이 값을 그냥 공개 속성 하나로 실어 보낼 뿐이라(`SessionsEOSGS.cpp:588`) 방 이름과 같아도 된다.
	Params.SessionSettings.SchemaName = RoomSessionName;
	Params.SessionSettings.NumMaxConnections = MaxRoomPlayers;
	Params.SessionSettings.JoinPolicy = ESessionJoinPolicy::Public;
	Params.SessionSettings.CustomSettings.Emplace(
		RoomTagKey, FCustomSessionSetting{ FSchemaVariant(RoomTagValue), ESchemaAttributeVisibility::Public });

	// M3-05: 게스트가 갈 곳. 공개(Public)여야 검색 결과에 실려 온다 — 비공개면 방에 들어간 뒤에도 못 읽는다.
	if (!HostConnectAddress.IsEmpty())
	{
		Params.SessionSettings.CustomSettings.Emplace(
			RoomAddressKey, FCustomSessionSetting{ FSchemaVariant(HostConnectAddress), ESchemaAttributeVisibility::Public });
	}
	return Params;
}

FFindSessions::Params USnareOnlineSubsystem::MakeFindRoomsParams(const FAccountId& AccountId)
{
	FFindSessions::Params Params;
	Params.LocalAccountId = AccountId;
	Params.MaxResults = MaxRoomSearchResults;
	Params.Filters.Add(FFindSessionsSearchFilter{
		RoomTagKey, ESchemaAttributeComparisonOp::Equals, FSchemaVariant(RoomTagValue) });
	return Params;
}

void USnareOnlineSubsystem::RunCommandLineRoomMode()
{
	FString RoomMode;
	if (!SnareCheckSwitch::Value(TEXT("SnareRoom="), RoomMode))
	{
		return;
	}

	if (RoomMode == TEXT("host"))
	{
		CreateRoom();
	}
	else if (RoomMode == TEXT("join"))
	{
		bAutoJoinFoundRoom = true;
		FindRooms();
	}
	else
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] -SnareRoom= 은 host 또는 join 이다 — 받은 값 %s"), *RoomMode);
	}
}

ISessionsPtr USnareOnlineSubsystem::GetSessionsForRoom()
{
	if (LoginState != ESnareLoginState::LoggedIn)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 로그인 전에는 방을 다룰 수 없다 — 지금 상태 %d"), static_cast<int32>(LoginState));
		return nullptr;
	}

	const IOnlineServicesPtr Services = GetServices(EOnlineServices::Epic);
	const ISessionsPtr Sessions = Services.IsValid() ? Services->GetSessionsInterface() : nullptr;
	if (!Sessions.IsValid())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] EOS 세션 인터페이스가 없다 — OnlineServicesEOSGS 플러그인 확인"));
	}
	return Sessions;
}

void USnareOnlineSubsystem::CreateRoom()
{
	if (RoomState == ESnareRoomState::Creating || RoomState == ESnareRoomState::Hosting)
	{
		return;
	}

	const ISessionsPtr Sessions = GetSessionsForRoom();
	if (!Sessions.IsValid())
	{
		RoomState = ESnareRoomState::Failed;
		return;
	}

	RoomState = ESnareRoomState::Creating;
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 방 만들기 시작 — %s, 최대 %u명"), *RoomSessionName.ToString(), MaxRoomPlayers);

	// M3-05: 방을 세우면서 "여기로 접속해라" 를 같이 적는다. 나중에 고쳐 넣으면 그 사이에 들어온
	// 게스트가 빈 표지판을 읽는다.
	const FString HostAddress = MakeHostConnectAddress(ToString(LocalAccountId), GetHostAddressOverride());
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 방 주소 — %s"), HostAddress.IsEmpty() ? TEXT("(없음)") : *HostAddress);

	Sessions->CreateSession(MakeCreateRoomParams(LocalAccountId, HostAddress))
		.OnComplete(this, &USnareOnlineSubsystem::HandleCreateRoomComplete);
}

void USnareOnlineSubsystem::HandleCreateRoomComplete(const TOnlineResult<FCreateSession>& Result)
{
	if (!Result.IsOk())
	{
		RoomState = ESnareRoomState::Failed;
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 방 만들기 실패 — %s"), *Result.GetErrorValue().GetLogString());
		return;
	}

	RoomState = ESnareRoomState::Hosting;
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 방 만들기 성공 — %s"), *RoomSessionName.ToString());

	// M3-05: 방만 세우고 로비에 남으면 게스트가 접속할 서버가 없다.
	TravelToHuntAsServer();
}

void USnareOnlineSubsystem::FindRooms()
{
	const ISessionsPtr Sessions = GetSessionsForRoom();
	if (!Sessions.IsValid())
	{
		RoomState = ESnareRoomState::Failed;
		return;
	}

	FoundRoomIds.Reset();
	LastRejectCode.Reset();
	RoomState = ESnareRoomState::Searching;
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 방 찾기 시작 — 최대 %u개"), MaxRoomSearchResults);

	Sessions->FindSessions(MakeFindRoomsParams(LocalAccountId))
		.OnComplete(this, &USnareOnlineSubsystem::HandleFindRoomsComplete);
}

void USnareOnlineSubsystem::HandleFindRoomsComplete(const TOnlineResult<FFindSessions>& Result)
{
	if (!Result.IsOk())
	{
		RoomState = ESnareRoomState::Failed;
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 방 찾기 실패 — %s"), *Result.GetErrorValue().GetLogString());
		return;
	}

	FoundRoomIds = Result.GetOkValue().FoundSessionIds;
	// 결과 0개는 오류가 아니다(명세 2장) — 아직 아무도 방을 안 열었을 뿐이다.
	RoomState = ESnareRoomState::None;
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 방 찾기 끝 — %d개"), FoundRoomIds.Num());

	if (bAutoJoinFoundRoom && FoundRoomIds.Num() > 0)
	{
		bAutoJoinFoundRoom = false;
		JoinFoundRoom(0);
	}
}

void USnareOnlineSubsystem::JoinFoundRoom(int32 Index)
{
	if (!FoundRoomIds.IsValidIndex(Index))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] %d번 방이 없다 — 찾은 방 %d개"), Index, FoundRoomIds.Num());
		return;
	}

	const ISessionsPtr Sessions = GetSessionsForRoom();
	if (!Sessions.IsValid())
	{
		RoomState = ESnareRoomState::Failed;
		return;
	}

	FJoinSession::Params Params;
	Params.LocalAccountId = LocalAccountId;
	Params.SessionName = RoomSessionName;
	Params.SessionId = FoundRoomIds[Index];

	LastRejectCode.Reset();
	RoomState = ESnareRoomState::Joining;
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 방 참가 시작 — %d번"), Index);

	Sessions->JoinSession(MoveTemp(Params))
		.OnComplete(this, &USnareOnlineSubsystem::HandleJoinRoomComplete);
}

FString USnareOnlineSubsystem::ClassifyJoinError(const UE::Online::FOnlineError& Error)
{
	return Error == UE::Online::Errors::ErrorCode::Common::SessionFull
		? FString(TEXT("room_full")) : FString(TEXT("room_join_failed"));
}

FString USnareOnlineSubsystem::ClassifyNetworkFailure(ENetworkFailure::Type FailureType)
{
	// 엔진이 접속 인사에서 버전(`BuildUniqueId`·네트워크 버전)이 다르면 이 둘로 끊는다.
	return (FailureType == ENetworkFailure::OutdatedClient || FailureType == ENetworkFailure::OutdatedServer)
		? FString(TEXT("version_mismatch")) : FString();
}

void USnareOnlineSubsystem::HandleJoinRoomComplete(const TOnlineResult<FJoinSession>& Result)
{
	if (!Result.IsOk())
	{
		RoomState = ESnareRoomState::Failed;
		// 사유는 로비 상태 줄이 `DT_EntryRejectTable` 문구로 보인다 — "방에 문제가 생겼다" 만으로는 다시 할지 모른다.
		LastRejectCode = ClassifyJoinError(Result.GetErrorValue());
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 방 참가 실패 — %s (거절 코드 %s)"),
			*Result.GetErrorValue().GetLogString(), *LastRejectCode);
		return;
	}

	RoomState = ESnareRoomState::Joined;
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 방 참가 성공 — %s"), *RoomSessionName.ToString());

	// M3-05: EOS 방에 이름만 올린 상태다 — 맵은 호스트에게 접속해야 열린다.
	TravelToJoinedRoom();
}

// --- M3-05 방에 들어가면 사냥 맵으로 ----------------------------------------
// EOS 방은 표지판이고 게임은 언리얼 네트워크가 연다(DESIGN 3.4 P2P 리슨 서버). 그래서 여기서 하는 일은
// 둘뿐이다 — 호스트는 리슨 서버가 되러 떠나고, 게스트는 방에 적힌 주소를 읽어 접속한다.

const FName USnareOnlineSubsystem::RoomAddressKey(TEXT("SNAREHAVEN_ADDR"));

FString USnareOnlineSubsystem::MakeHostConnectAddress(const FString& PuidString, const FString& AddressOverride)
{
	// 명령줄이 준 주소가 이긴다 — 같은 기계 두 프로세스는 PUID 가 같아 P2P 로 서로를 가릴 수 없다(M3-01c2).
	if (!AddressOverride.IsEmpty())
	{
		return AddressOverride;
	}

	if (PuidString.IsEmpty())
	{
		return FString();
	}

	// `EOS:<PUID>` — `FInternetAddrEOS::ToString` 이 만드는 것과 같은 형식이라야 SocketSubsystemEOS 가 푼다.
	return FString::Printf(TEXT("%s%s%s"), EOS_CONNECTION_URL_PREFIX, EOS_URL_SEPARATOR, *PuidString);
}

FString USnareOnlineSubsystem::ReadHostConnectAddress(const FCustomSessionSettingsMap& CustomSettings)
{
	const FCustomSessionSetting* const Setting = CustomSettings.Find(RoomAddressKey);
	if (Setting == nullptr || Setting->Data.GetType() != ESchemaAttributeType::String)
	{
		return FString();
	}
	return Setting->Data.GetString();
}

FString USnareOnlineSubsystem::MakeZoneServerTravelUrl(const FName LevelName)
{
	// 갈 곳이 미정이면 URL 을 지어내지 않는다 — 빈 URL 은 `ServerTravel` 이 거절한다.
	if (LevelName.IsNone())
	{
		return FString();
	}

	// `?listen` 이 빠지면 호스트는 그냥 혼자 노는 사람이 되고 게스트는 붙을 곳이 없다.
	return LevelName.ToString() + TEXT("?listen");
}

FString USnareOnlineSubsystem::MakeHuntServerTravelUrl()
{
	// 존 URL 과 같은 길을 쓴다 (M6-02a) — `?listen` 이 한 군데서만 붙는다.
	return MakeZoneServerTravelUrl(ASnarehavenGameMode::HuntLevelName);
}

FString USnareOnlineSubsystem::GetHostAddressOverride()
{
	FString Override;
	SnareCheckSwitch::Value(TEXT("SnareHostAddr="), Override);
	return Override;
}

void USnareOnlineSubsystem::TravelToHuntAsServer()
{
	UWorld* const World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 월드가 없어 사냥 맵으로 떠나지 못했다"));
		return;
	}

	const FString TravelUrl = MakeHuntServerTravelUrl();
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 호스트가 사냥 맵으로 — %s"), *TravelUrl);
	if (!World->ServerTravel(TravelUrl))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] ServerTravel 이 거절됐다 — %s"), *TravelUrl);
	}
}

void USnareOnlineSubsystem::TravelToJoinedRoom()
{
	const ISessionsPtr Sessions = GetSessionsForRoom();
	if (!Sessions.IsValid())
	{
		return;
	}

	// 참가가 끝나면 방이 이 이름으로 우리 쪽에 들어와 있다 — 검색 결과가 아니라 **들어간 방**을 읽어야
	// 호스트가 사이에 주소를 바꿨어도 최신값을 본다.
	const TOnlineResult<FGetSessionByName> JoinedRoom = Sessions->GetSessionByName({ RoomSessionName });
	if (!JoinedRoom.IsOk())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 들어간 방을 읽지 못했다 — %s"),
			*JoinedRoom.GetErrorValue().GetLogString());
		return;
	}

	const FString HostAddress = ReadHostConnectAddress(JoinedRoom.GetOkValue().Session->GetSessionSettings().CustomSettings);
	if (HostAddress.IsEmpty())
	{
		// 표지판 없는 방 = 갈 곳을 모른다. 로비에 그대로 두는 편이 아무 맵이나 여는 것보다 낫다.
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 방에 접속 주소(%s)가 없다 — 로비에 머문다"), *RoomAddressKey.ToString());
		return;
	}

	UWorld* const World = GetWorld();
	if (World == nullptr || GEngine == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 월드가 없어 호스트에게 접속하지 못했다"));
		return;
	}

	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 게스트가 호스트에게 접속 — %s"), *HostAddress);
	// 절대 여행이라야 로비 맵 이름이 주소 앞에 붙지 않는다.
	GEngine->SetClientTravel(World, *HostAddress, TRAVEL_Absolute);
}

// --- M3-03 방 해산 · 로비 복귀 ----------------------------------------------
// DESIGN 3.4: 호스트가 나가거나 끊기면 방 해산, 호스트 마이그레이션 없음, 남은 사람은 로비로.
// 개인 데이터는 전부 백엔드에 있으므로(DESIGN 4.1) 여기서 지킬 것은 **방 뒷정리와 갈 곳**뿐이다.

const FName USnareOnlineSubsystem::LobbyLevelName(TEXT("L_Title"));

FLeaveSession::Params USnareOnlineSubsystem::MakeLeaveRoomParams(const FAccountId& AccountId, bool bDestroy)
{
	FLeaveSession::Params Params;
	Params.LocalAccountId = AccountId;
	Params.SessionName = RoomSessionName;
	// 호스트는 지우고(해산) 게스트는 나가기만 한다 — 게스트가 지울 수 있으면 남의 방을 닫을 수 있다.
	Params.bDestroySession = bDestroy;
	return Params;
}

bool USnareOnlineSubsystem::ShouldReturnToLobby(ENetMode NetMode)
{
	return NetMode == NM_Client;
}

void USnareOnlineSubsystem::LeaveRoom()
{
	if (RoomState != ESnareRoomState::Hosting && RoomState != ESnareRoomState::Joined)
	{
		// 방에 들어간 적이 없으면 할 일이 없다 — 게임을 끌 때마다 EOS 를 두드리지 않는다.
		return;
	}

	const bool bWasHost = RoomState == ESnareRoomState::Hosting;
	// 먼저 상태를 내린다: 아래에서 실패하고 돌아가도 "아직 방에 있다" 고 남지 않게.
	RoomState = ESnareRoomState::None;

	const ISessionsPtr Sessions = GetSessionsForRoom();
	if (!Sessions.IsValid())
	{
		return;
	}

	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 방 %s — %s"),
		bWasHost ? TEXT("해산") : TEXT("나가기"), *RoomSessionName.ToString());

	// 람다에 `this` 를 담지 않는다 — 게임을 끌 때도 부르는 길이라 콜백이 올 즈음엔 이미 없을 수 있다.
	Sessions->LeaveSession(MakeLeaveRoomParams(LocalAccountId, bWasHost))
		.OnComplete([](const TOnlineResult<FLeaveSession>& Result)
		{
			if (!Result.IsOk())
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 방 떠나기 실패 — %s"), *Result.GetErrorValue().GetLogString());
			}
		});
}

bool USnareOnlineSubsystem::ShouldDisbandAfterBackground(const bool bInRoom, const double AwaySeconds)
{
	return bInRoom && AwaySeconds >= BackgroundGraceSeconds;
}

void USnareOnlineSubsystem::HandleAppWillEnterBackground()
{
	if (!IsInRoom())
	{
		// 혼자 사냥·로비는 남에게 영향이 없다 — 그대로 둔다.
		return;
	}
	ClearBackgroundGrace();
	BackgroundedAtUtc = FDateTime::UtcNow();
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 백그라운드 — %.0f초 안에 돌아오지 않으면 방을 떠난다"), BackgroundGraceSeconds);
	// 1초마다 벽시계로 잰다 — 티커 시간은 앱이 멈춘 동안 흐르지 않을 수 있다.
	BackgroundGraceTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &USnareOnlineSubsystem::HandleBackgroundGraceTick), 1.0f);
}

bool USnareOnlineSubsystem::IsRecvSocketBroken(FSocket* const Socket)
{
	if (Socket == nullptr)
	{
		return true;
	}
	// 엿보기만 한다 — 온 패킷이 있어도 꺼내지 않으니 엔진 수신 루프가 그대로 받는다.
	uint8 PeekByte = 0;
	int32 BytesRead = 0;
	const TSharedRef<FInternetAddr> From = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	if (Socket->RecvFrom(&PeekByte, 1, BytesRead, *From, ESocketReceiveFlags::Peek))
	{
		return false;
	}
	const ESocketErrors Error = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
	// 비차단 소켓에 온 게 없으면 "기다려라" 다. 큰 패킷을 1바이트로 엿보면 "너무 크다" 가 나와도 살아 있는 것이다.
	return Error != SE_EWOULDBLOCK && Error != SE_NO_ERROR && Error != SE_EMSGSIZE;
}

bool USnareOnlineSubsystem::ShouldReopenListenSocket(const ENetMode NetMode, const bool bSocketBroken)
{
	return NetMode == NM_ListenServer && bSocketBroken;
}

bool USnareOnlineSubsystem::ReopenListenSocketIfBroken()
{
	UGameInstance* const GameInstance = GetGameInstance();
	UWorld* const World = GameInstance != nullptr ? GameInstance->GetWorld() : nullptr;
	if (GEngine == nullptr || !IsValid(World))
	{
		return false;
	}
	UIpNetDriver* const IpDriver = Cast<UIpNetDriver>(World->GetNetDriver());
	if (IpDriver == nullptr)
	{
		return false;
	}
	if (!ShouldReopenListenSocket(World->GetNetMode(), IsRecvSocketBroken(IpDriver->GetSocket())))
	{
		return false;
	}
	UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 앱 복귀 — 리슨 서버 소켓이 죽었다(%s, 게스트 %d명). 넷 드라이버를 내리고 다시 연다"),
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(), World->GetNetDriver()->ClientConnections.Num());
	// 엔진 `EnableListenServer` 는 이미 넷 드라이버가 있으면 아무것도 안 한다 — 먼저 내린다. 맵 URL 의 `?listen`·포트는 그대로 남는다.
	GEngine->ShutdownWorldNetDriver(World);
	const bool bListening = GameInstance->EnableListenServer(true);
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 리슨 서버 소켓 다시 열기 %s — 넷 모드 %d"),
		bListening ? TEXT("성공") : TEXT("실패"), static_cast<int32>(World->GetNetMode()));
	return bListening;
}

void USnareOnlineSubsystem::HandleAppEnteredForeground()
{
	// 방 유예보다 먼저 — 이 처리 직후 같은 프레임의 넷 수신이 죽은 소켓에서 멈춘다(로비로 떠나는 길이어도).
	ReopenListenSocketIfBroken();

	if (BackgroundedAtUtc.GetTicks() == 0)
	{
		return;
	}
	DisbandIfBackgroundTooLong();
	if (BackgroundedAtUtc.GetTicks() != 0)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Online] 백그라운드에서 %.1f초 만에 돌아왔다 — 방 유지"),
			(FDateTime::UtcNow() - BackgroundedAtUtc).GetTotalSeconds());
		ClearBackgroundGrace();
	}
}

bool USnareOnlineSubsystem::HandleBackgroundGraceTick(float DeltaTime)
{
	DisbandIfBackgroundTooLong();
	// 떠났거나 돌아왔으면 시각이 지워져 있다 — 그때 티커를 멈춘다.
	const bool bKeepWaiting = BackgroundedAtUtc.GetTicks() != 0;
	if (!bKeepWaiting)
	{
		BackgroundGraceTicker.Reset();
	}
	return bKeepWaiting;
}

void USnareOnlineSubsystem::DisbandIfBackgroundTooLong()
{
	if (BackgroundedAtUtc.GetTicks() == 0)
	{
		return;
	}
	const double AwaySeconds = (FDateTime::UtcNow() - BackgroundedAtUtc).GetTotalSeconds();
	if (!ShouldDisbandAfterBackground(IsInRoom(), AwaySeconds))
	{
		return;
	}
	ClearBackgroundGrace();

	const bool bWasHost = RoomState == ESnareRoomState::Hosting;
	UE_LOG(LogSnarehaven, Log, TEXT("[Online] 백그라운드 %.1f초 — 방 %s 뒤 로비(%s)로"),
		AwaySeconds, bWasHost ? TEXT("해산") : TEXT("나가기"), *LobbyLevelName.ToString());
	LeaveRoom();
	bReturnedFromRoom = true;

	// 호스트가 맵을 떠나면 리슨 서버가 닫혀 게스트는 연결 끊김(M3-03 길)으로 로비에 간다.
	UWorld* const World = GetGameInstance() != nullptr ? GetGameInstance()->GetWorld() : nullptr;
	if (GEngine != nullptr && IsValid(World))
	{
		GEngine->SetClientTravel(World, *LobbyLevelName.ToString(), TRAVEL_Absolute);
	}
}

void USnareOnlineSubsystem::ClearBackgroundGrace()
{
	if (BackgroundGraceTicker.IsValid())
	{
		FTSTicker::RemoveTicker(BackgroundGraceTicker);
		BackgroundGraceTicker.Reset();
	}
	BackgroundedAtUtc = FDateTime(0);
}

void USnareOnlineSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	if (!IsValid(World) || World->GetGameInstance() != GetGameInstance())
	{
		// PIE 두 번째 창처럼 다른 게임 인스턴스에서 난 실패는 내 방 일이 아니다.
		return;
	}

	UE_LOG(LogSnarehaven, Warning, TEXT("[Net] 연결 끊김 — %s: %s"),
		ENetworkFailure::ToString(FailureType), *ErrorString);

	const FString RejectCode = ClassifyNetworkFailure(FailureType);
	if (!RejectCode.IsEmpty())
	{
		LastRejectCode = RejectCode;
		UE_LOG(LogSnarehaven, Log, TEXT("[Net] 입장 거절 코드 %s"), *RejectCode);
	}

	// 넷 모드는 월드가 아니라 **끊긴 드라이버**에게 묻는다: 월드의 드라이버는 이 시점에 이미
	// 떨어져 나가 NM_Standalone 으로 보일 수 있고, 그러면 게스트가 죽은 방에 그대로 남는다.
	const ENetMode NetMode = IsValid(NetDriver) ? NetDriver->GetNetMode() : World->GetNetMode();
	if (!ShouldReturnToLobby(NetMode))
	{
		// 호스트: 게스트 하나가 떨어진 것뿐이다. 남은 사람들과 계속 논다.
		return;
	}

	LeaveRoom();
	bReturnedFromRoom = true;
	UE_LOG(LogSnarehaven, Log, TEXT("[Net] 방이 사라져 로비(%s)로 돌아간다"), *LobbyLevelName.ToString());
	// 여기서 맵을 열지 않는다. 우리 핸들러가 엔진 것보다 **먼저** 불리는데(루프#41 3층 로그 순서),
	// 엔진은 뒤이어 `SetClientTravel("?closed")` 로 기본 맵(`L_Boot`)을 덮어쓰므로 우리가 연 로비는
	// 언제나 진다. 대신 엔진이 내려놓는 `L_Boot` 에서 `ASnarehavenGameMode` 의 자동 진입이 로비까지
	// 이어 달린다 — 위 `bReturnedFromRoom` 이 그 다음 한 걸음(로비 -> 사냥)을 막는 표시다.
}

namespace
{
	/** 콘솔 명령이 부를 곳. 로비 UI(M3-04) 전까지 방을 여는 유일한 손잡이다. */
	USnareOnlineSubsystem* FindOnlineSubsystem(UWorld* World)
	{
		UGameInstance* const GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		if (GameInstance == nullptr)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 게임 인스턴스가 없어 명령을 처리할 수 없다"));
			return nullptr;
		}
		return GameInstance->GetSubsystem<USnareOnlineSubsystem>();
	}

	FAutoConsoleCommandWithWorld CreateRoomCommand(
		TEXT("Snare.Online.CreateRoom"), TEXT("EOS 방을 만든다 (M3-01b)"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (USnareOnlineSubsystem* const Online = FindOnlineSubsystem(World))
			{
				Online->CreateRoom();
			}
		}));

	FAutoConsoleCommandWithWorld FindRoomsCommand(
		TEXT("Snare.Online.FindRooms"), TEXT("EOS 방을 찾는다 (M3-01b)"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (USnareOnlineSubsystem* const Online = FindOnlineSubsystem(World))
			{
				Online->FindRooms();
			}
		}));

	FAutoConsoleCommandWithWorldAndArgs JoinRoomCommand(
		TEXT("Snare.Online.JoinRoom"), TEXT("찾은 방 중 <번호>(기본 0)에 들어간다 (M3-01b)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (USnareOnlineSubsystem* const Online = FindOnlineSubsystem(World))
			{
				Online->JoinFoundRoom(Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0);
			}
		}));

	FAutoConsoleCommandWithWorld LeaveRoomCommand(
		TEXT("Snare.Online.LeaveRoom"), TEXT("방에서 나온다 — 호스트면 해산 (M3-03)"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (USnareOnlineSubsystem* const Online = FindOnlineSubsystem(World))
			{
				Online->LeaveRoom();
			}
		}));

#if !UE_BUILD_SHIPPING
	// 3층 검사용 — iOS 가 멈춘 앱의 리슨 서버 소켓을 거둬 가는 것(INBOX 18:20 폰 `SE_ENOTCONN`)을 맥에서 흉내 낸다:
	// 소켓을 닫고 "앱이 돌아왔다" 알림을 보낸다. `noforeground` 를 주면 닫기만 한다(고치기 전 모습 — 수신 루프가 갇힌다).
	FAutoConsoleCommandWithWorldAndArgs SimulateSocketReclaimCommand(
		TEXT("Snare.Net.SimulateSocketReclaim"), TEXT("리슨 서버 소켓을 닫고 앱 복귀 알림을 보낸다 [noforeground] (검사용)"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UIpNetDriver* const IpDriver = World != nullptr ? Cast<UIpNetDriver>(World->GetNetDriver()) : nullptr;
			if (IpDriver == nullptr || IpDriver->GetSocket() == nullptr)
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("[Online] 검사용 소켓 회수 — 리슨 서버 소켓이 없다(넷 모드 %d)"),
					World != nullptr ? static_cast<int32>(World->GetNetMode()) : -1);
				return;
			}
			IpDriver->GetSocket()->Close();
			const bool bForeground = !Args.Contains(TEXT("noforeground"));
			UE_LOG(LogSnarehaven, Log, TEXT("[Online] 검사용 소켓 회수 — 리슨 서버 소켓을 닫았다, 앱 복귀 알림 %s"),
				bForeground ? TEXT("보냄") : TEXT("안 보냄"));
			if (bForeground)
			{
				FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Broadcast();
			}
		}));
#endif
}
