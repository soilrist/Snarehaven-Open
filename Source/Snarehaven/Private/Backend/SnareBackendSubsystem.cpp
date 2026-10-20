#include "Backend/SnareBackendSubsystem.h"
#include "SnareCheckSwitch.h"

#include "Backend/SnareNickname.h"
#include "Backend/SnarePlayerCacheSave.h"
#include "Backend/SnareServerReply.h"
#include "Core/PlayFabClientAPI.h"
#include "Core/PlayFabClientDataModels.h"
#include "Core/PlayFabError.h"
#include "HAL/PlatformMisc.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PlayFab.h"
#include "PlayFabRuntimeSettings.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Snarehaven.h"
#include "World/SnareSound.h"
#include "Engine/DataTable.h"

namespace
{
	/**
	 * Title ID 가 적힌 자리. 플러그인의 `UPlayFabRuntimeSettings` 는 `config = Engine` 이라 원래
	 * `DefaultEngine.ini` 를 읽지만, 우리는 그 값을 **커밋하지 않는 별도 파일**(`Config/PlayFab.ini`)에 두고
	 * 부팅 때 옮겨 담는다 (BACKEND.md, 원칙 7).
	 */
	const TCHAR* const PlayFabIniName = TEXT("PlayFab.ini");

	/**
	 * 절 이름 후보 둘. 클래스가 사는 모듈은 `PlayFabCommon` 이라 정식 이름은 앞의 것인데,
	 * 사람이 적어 둔 파일에는 플러그인 문서를 따라 뒤의 이름이 쓰여 있다. 둘 다 받는다 —
	 * 안 그러면 사람이 이미 넣어 둔 Title ID 를 못 읽고 조용히 로그인이 꺼진다.
	 */
	const TCHAR* const TitleIdSections[] = {
		TEXT("/Script/PlayFabCommon.PlayFabRuntimeSettings"),
		TEXT("/Script/PlayFab.PlayFabRuntimeSettings")
	};

	/** 콜백이 돌아왔을 때 서브시스템이 이미 사라졌을 수 있다 — 약한 참조로 잡는다. */
	using FWeakSubsystem = TWeakObjectPtr<USnareBackendSubsystem>;

	/**
	 * 시작 장비 응답(`GrantStarterKit`)의 칸 이름들. 원본은 `tools/playfab/cloudscript_main.js` 의
	 * 반환 객체 하나이고 여기는 그것을 읽는 쪽이다 — 이름을 글자로 흩뿌리지 않으려고 모아 둔다.
	 */
	const TCHAR* const StarterKitGrantedField = TEXT("granted");
	const TCHAR* const StarterKitItemsField = TEXT("items");
	const TCHAR* const StarterKitItemIdField = TEXT("itemId");

	// 접속 복구 값 — DESIGN 4.4 「접속 복구」 표가 원본이다.
	constexpr float RetryBaseSeconds = 2.f;
	constexpr float RetryMaxSeconds = 60.f;
	constexpr double SessionRefreshHours = 12.0;

	/** 세션이 없거나 만료돼 거절됐나 — 다시 로그인하면 풀린다. */
	bool IsAuthError(const PlayFab::FPlayFabCppError& Error)
	{
		return Error.HttpCode == 401
			|| Error.ErrorCode == PlayFab::PlayFabErrorNotAuthenticated
			|| Error.ErrorCode == PlayFab::PlayFabErrorInvalidSessionTicket
			|| Error.ErrorCode == PlayFab::PlayFabErrorExpiredAuthToken
			|| Error.ErrorCode == PlayFab::PlayFabErrorAuthTokenExpired
			|| Error.ErrorCode == PlayFab::PlayFabErrorEntityTokenExpired;
	}

	/** 서버까지 못 갔나(끊김·시간 초과·점검). */
	bool IsNetworkError(const PlayFab::FPlayFabCppError& Error)
	{
		return Error.HttpCode == 0 || Error.HttpCode == 408 || Error.HttpCode >= 500
			|| Error.ErrorCode == PlayFab::PlayFabErrorConnectionTimeout
			|| Error.ErrorCode == PlayFab::PlayFabErrorServiceUnavailable
			|| Error.ErrorCode == PlayFab::PlayFabErrorDownstreamServiceUnavailable;
	}

	/** PlayFab 오류를 `SnareServerReply` 표지를 붙인 글로 — 부르는 쪽이 끊김·만료·거절을 가를 수 있게. */
	FString TagPlayFabError(const PlayFab::FPlayFabCppError& Error)
	{
		const FString Report = Error.GenerateErrorReport();
		if (IsAuthError(Error))
		{
			return SnareServerReply::TagLoginError(Report);
		}
		return IsNetworkError(Error) ? SnareServerReply::TagNetworkError(Report) : SnareServerReply::TagRejectedError(Report);
	}
}

TFunction<FString()> USnareBackendSubsystem::TestLoginOverride;

void USnareBackendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (!ShouldAutoLogin(GetGameInstance()))
	{
		return;
	}

#if !UE_BUILD_SHIPPING
	// 개발용: 에디터 PIE 는 PlayFab 에 자동 로그인해 `Snare.Give` 가 스냅샷 규칙(CLAUDE.md 8)에 거절된다.
	// `-SnareOffline` 이면 로그인을 아예 건너뛰어 스냅샷이 백엔드에서 오지 않게 한다. 출시 구성에는 없다.
	if (SnareCheckSwitch::Param(TEXT("SnareOffline")))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 검사용 오프라인 (-SnareOffline) — PlayFab 로그인을 건너뛴다"));
		// 3층 검사용 — 타이틀의 「서버 연결 실패 + 다시 시도」 를 네트워크 없이 띄워 본다.
		if (SnareCheckSwitch::Param(TEXT("SnareLoginFailPreview")))
		{
			LoginState = ESnareBackendLoginState::Failed;
		}
		return;
	}
#endif

	// 로그인보다 먼저 — 응답을 기다리는 동안에도 지난번 화면을 보여 주기 위한 것이 캐시다 (DESIGN 4.5).
	// 아직 계정을 모르므로 주인 검사는 로그인 성공 뒤 `FetchPlayerState` 가 한 번 더 한다.
	LoadLocalCache();

	StartLogin();

	// 폰은 앱을 뒤로 보냈다 돌아오면 몇 시간이 지나 있다 — 세션 티켓·목장 타이머가 낡는다 (DESIGN 4.4 「접속 복구」).
	ForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddUObject(
		this, &USnareBackendSubsystem::HandleEnteredForeground);
}

void USnareBackendSubsystem::Deinitialize()
{
	CancelRetry();
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(ForegroundHandle);
	LoginWaiters.Reset();
	Super::Deinitialize();
}

float USnareBackendSubsystem::GetRetryDelaySeconds(int32 FailureCount)
{
	if (FailureCount <= 0)
	{
		return 0.f;
	}
	const int32 Doublings = FMath::Min(FailureCount, 16) - 1;
	return FMath::Min(RetryBaseSeconds * FMath::Pow(2.f, static_cast<float>(Doublings)), RetryMaxSeconds);
}

void USnareBackendSubsystem::ScheduleRetry()
{
	CancelRetry();
	++ConsecutiveFailures;
	PendingRetryDelay = GetRetryDelaySeconds(ConsecutiveFailures);
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] %d번째 실패 — %.0f초 뒤 다시 붙는다"), ConsecutiveFailures, PendingRetryDelay);
	RetryHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this](float)
		{
			RetryHandle.Reset();
			RetryNow();
			return false;
		}), PendingRetryDelay);
}

void USnareBackendSubsystem::CancelRetry()
{
	if (RetryHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RetryHandle);
		RetryHandle.Reset();
	}
	PendingRetryDelay = 0.f;
}

void USnareBackendSubsystem::RetryNow()
{
	CancelRetry();
	if (LoginState != ESnareBackendLoginState::LoggedIn)
	{
		StartLogin(); // 하는 중이면 StartLogin 이 알아서 안 한다
	}
	else if (FetchState == ESnareBackendFetchState::Failed)
	{
		FetchPlayerState();
	}
	OnConnectionChanged.Broadcast();
}

void USnareBackendSubsystem::Relogin()
{
	if (LoginState == ESnareBackendLoginState::InProgress)
	{
		return;
	}
	CancelRetry();
	LoginState = ESnareBackendLoginState::NotStarted;
	StartLogin();
}

void USnareBackendSubsystem::FlushLoginWaiters(bool bLoggedIn)
{
	// 부르는 동안 새 대기가 붙을 수 있다(재전송이 또 만료) — 떼어 낸 뒤 부른다.
	TArray<TFunction<void(bool)>> Waiters = MoveTemp(LoginWaiters);
	LoginWaiters.Reset();
	for (TFunction<void(bool)>& Waiter : Waiters)
	{
		Waiter(bLoggedIn);
	}
}

void USnareBackendSubsystem::HandleEnteredForeground()
{
	if (LoginState == ESnareBackendLoginState::LoggedIn)
	{
		if (FDateTime::UtcNow() - LoggedInAtUtc > FTimespan::FromHours(SessionRefreshHours))
		{
			UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 앞으로 돌아옴 — 세션이 오래돼 다시 로그인한다"));
			Relogin(); // 성공하면 FinishLoginSucceeded 가 보유 상태를 다시 받는다
			return;
		}
		UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 앞으로 돌아옴 — 보유 상태를 다시 받는다"));
		FetchPlayerState();
		return;
	}
	if (LoginState == ESnareBackendLoginState::Failed)
	{
		RetryNow();
	}
}

bool USnareBackendSubsystem::ShouldAutoLogin(const UGameInstance* GameInstance)
{
	return ShouldAutoLoginFor(MakeAutoLoginContext(GameInstance));
}

FSnareAutoLoginContext USnareBackendSubsystem::MakeAutoLoginContext(const UGameInstance* GameInstance)
{
	// PIE 에서는 게임 인스턴스가 `Init()`(= 서브시스템이 깨어나는 자리) 보다 **먼저** 월드 컨텍스트를
	// 받는다 (엔진 `UGameInstance::InitializeForPlayInEditor`) — 그래서 여기서 이미 읽을 수 있다.
	return MakeAutoLoginContextFor(GameInstance != nullptr ? GameInstance->GetWorldContext() : nullptr);
}

FSnareAutoLoginContext USnareBackendSubsystem::MakeAutoLoginContextFor(const FWorldContext* WorldContext)
{
	FSnareAutoLoginContext Context;
	Context.bRunningGame = IsRunningGame();
	Context.bEditor = GIsEditor;
	Context.bAutomationTest = GIsAutomationTesting;
	Context.bHasTitleId = !ReadTitleIdFromConfig().IsEmpty();

	// PIE 인지는 **월드 종류**로 본다. "에디터니까 PIE 겠지" 로 넘기면 에디터를 띄워만 둔 상태나
	// 커맨드릿이 만든 게임 인스턴스까지 같이 걸린다.
	if (WorldContext != nullptr && WorldContext->WorldType == EWorldType::PIE)
	{
		Context.bPlayInEditor = true;
		// 번호가 안 붙은 PIE 컨텍스트(`INDEX_NONE`)는 창이 하나인 경우다 — 0 번 창으로 센다.
		Context.PlayInEditorInstance = FMath::Max(0, WorldContext->PIEInstance);
	}

	return Context;
}

bool USnareBackendSubsystem::ShouldAutoLoginFor(const FSnareAutoLoginContext& Context)
{
	// 자동화 검사 중에는 안 붙는다 — 검사 한 번에 PlayFab 왕복이 붙으면 네트워크 없는 자리에서
	// 검사가 빨갛게 되고, 검사용 계정이 진짜 계정과 섞인다.
	if (Context.bAutomationTest)
	{
		return false;
	}

	// 실제 게임 실행(`-game`·패키지). 여기는 예전 그대로다.
	if (Context.bRunningGame && !Context.bEditor)
	{
		return true;
	}

	// 에디터 Play(PIE). 사람이 게임을 보는 자리가 여기라 켠다(감독 지시 2026-09-24 00:05 ①).
	// **Title ID 가 적혀 있을 때만** — 저장소를 갓 받아 `Config/PlayFab.ini` 가 없는 사람의 에디터에는
	// 붙을 곳이 없고, 매번 실패 경고만 남는다.
	return Context.bEditor && Context.bPlayInEditor && Context.bHasTitleId;
}

FString USnareBackendSubsystem::ReadTitleIdFromConfig()
{
	const FString IniPath = FPaths::ProjectConfigDir() / PlayFabIniName;
	if (!FPaths::FileExists(IniPath))
	{
		return FString();
	}

	// GConfig 는 이름을 아는 파일만 갖고 있다. 우리 파일은 엔진이 모르는 파일이라 직접 올린다.
	GConfig->LoadFile(IniPath);

	for (const TCHAR* const Section : TitleIdSections)
	{
		FString TitleId;
		if (GConfig->GetString(Section, TEXT("TitleId"), TitleId, IniPath) && !TitleId.IsEmpty())
		{
			return TitleId;
		}
	}

	return FString();
}

FString USnareBackendSubsystem::MakeDeviceId()
{
	// `GetLoginId` 는 기기마다 다르고 재부팅·재설치에도 유지되는 값이다. 게스트 계정을 이 값에 묶으면
	// 로그인 화면 없이도 진행이 이어진다 (DESIGN 4.4).
	return FPlatformMisc::GetLoginId();
}

FString USnareBackendSubsystem::MakeDeviceIdForInstance(const FSnareAutoLoginContext& Context)
{
	FString DeviceId = MakeDeviceId();
	FString AccountTag;
	if (SnareCheckSwitch::Value(TEXT("SnareAccountTag="), AccountTag) && !AccountTag.IsEmpty())  // 검사 스위치: 이 태그로 새 시험 계정 한 칸 (스토리 1장을 새 계정으로 돌릴 때)
	{
		DeviceId += TEXT("_T") + AccountTag;
	}
	if (Context.PlayInEditorInstance == INDEX_NONE)
	{
		return DeviceId;
	}

	return FString::Printf(TEXT("%s_PIE%d"), *DeviceId, Context.PlayInEditorInstance);
}

FString USnareBackendSubsystem::GetDeviceLoginApiName()
{
#if PLATFORM_IOS
	return TEXT("LoginWithIOSDeviceID");
#elif PLATFORM_ANDROID
	return TEXT("LoginWithAndroidDeviceID");
#else
	// 맥·윈도에는 모바일 디바이스 ID 가 없다. 출시 대상은 아니지만 여기서 3층 검사를 돌리므로
	// 같은 기기 ID 를 CustomID 로 보낸다 — PlayFab 쪽에서도 같은 게스트 계정 한 칸이다.
	return TEXT("LoginWithCustomID");
#endif
}

void USnareBackendSubsystem::StartLogin()
{
	if (LoginState == ESnareBackendLoginState::InProgress || LoginState == ESnareBackendLoginState::LoggedIn)
	{
		return;
	}

	if (TestLoginOverride)
	{
		LoginState = ESnareBackendLoginState::InProgress;
		const FString Error = TestLoginOverride();
		if (Error.IsEmpty())
		{
			FinishLoginSucceeded(TEXT("test.player"), /*bNewlyCreated=*/false, TEXT("Tester"));
		}
		else
		{
			FinishLoginFailed(Error);
		}
		return;
	}

	const FString TitleId = ReadTitleIdFromConfig();
	if (TitleId.IsEmpty())
	{
		// 설정이 없는 것은 오류가 아니다 — 저장소를 갓 받은 사람은 백엔드 없이 계속 논다.
		UE_LOG(LogSnarehaven, Warning,
			TEXT("[Backend] PlayFab Title ID 가 없어 로그인을 건너뛴다 — Config/PlayFab.ini 의 TitleId"));
		// 재시도는 잡지 않는다 — 설정 파일이 없으면 몇 번을 다시 해도 같다.
		LoginState = ESnareBackendLoginState::Failed;
		FlushLoginWaiters(false);
		OnConnectionChanged.Broadcast();
		return;
	}

	// 플러그인은 요청 URL 을 설정의 Title ID 로 만든다(`PlayFabCommonSettings::GeneratePfUrl`).
	// 요청 구조체에만 넣으면 주소가 `https://.playfabapi.com` 이 돼서 아예 못 나간다.
	GetMutableDefault<UPlayFabRuntimeSettings>()->TitleId = TitleId;

	const PlayFabClientPtr ClientApi = IPlayFabModuleInterface::Get().GetClientAPI();
	if (!ClientApi.IsValid())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] PlayFab 클라이언트 API 를 얻지 못했다 — PlayFabCpp 모듈 확인"));
		LoginState = ESnareBackendLoginState::Failed;
		FlushLoginWaiters(false);
		OnConnectionChanged.Broadcast();
		return;
	}

	const FSnareAutoLoginContext Context = MakeAutoLoginContext(GetGameInstance());
	const FString DeviceId = MakeDeviceIdForInstance(Context);
	const FString ApiName = GetDeviceLoginApiName();
	LoginState = ESnareBackendLoginState::InProgress;
	OnConnectionChanged.Broadcast();
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] PlayFab 게스트 로그인 시작 — Title %s · %s%s"), *TitleId, *ApiName,
		Context.bPlayInEditor
			? *FString::Printf(TEXT(" · 에디터 Play(PIE #%d) — 기기 ID 에 인스턴스 번호를 붙였다"),
				Context.PlayInEditorInstance)
			: TEXT(""));

	// 성공 델리게이트의 **타입은 API 마다 다르지만**(`FLoginWithCustomIDDelegate` …) 받는 결과는 셋 다
	// `FLoginResult` 다. 그래서 몸통은 람다 하나로 두고, 델리게이트만 갈래마다 만든다.
	const FWeakSubsystem WeakThis(this);
	auto HandleSuccess = [WeakThis](const PlayFab::ClientModels::FLoginResult& Result)
	{
		if (USnareBackendSubsystem* const Self = WeakThis.Get())
		{
			// 닉네임 입력 창을 띄울지는 PlayFab 이 아는 표시 이름이 정한다 — 로그인 답에 함께 받는다.
			FString Name;
			if (Result.InfoResultPayload.IsValid() && Result.InfoResultPayload->PlayerProfile.IsValid())
			{
				Name = Result.InfoResultPayload->PlayerProfile->DisplayName;
			}
			Self->FinishLoginSucceeded(Result.PlayFabId, Result.NewlyCreated, Name);
		}
	};
	const TSharedPtr<PlayFab::ClientModels::FGetPlayerCombinedInfoRequestParams> InfoParams =
		MakeShared<PlayFab::ClientModels::FGetPlayerCombinedInfoRequestParams>();
	InfoParams->GetPlayerProfile = true;
	InfoParams->ProfileConstraints = MakeShared<PlayFab::ClientModels::FPlayerProfileViewConstraints>();
	InfoParams->ProfileConstraints->ShowDisplayName = true;
	auto OnError = PlayFab::FPlayFabErrorDelegate::CreateLambda(
		[WeakThis](const PlayFab::FPlayFabCppError& Error)
		{
			if (USnareBackendSubsystem* const Self = WeakThis.Get())
			{
				Self->FinishLoginFailed(Error.GenerateErrorReport());
			}
		});

	// `CreateAccount` 를 켜야 처음 켠 기기에서 계정이 생긴다 — 게스트 로그인에 가입 화면은 없다.
#if PLATFORM_IOS
	PlayFab::ClientModels::FLoginWithIOSDeviceIDRequest Request;
	Request.InfoRequestParameters = InfoParams;
	Request.DeviceId = DeviceId;
	Request.CreateAccount = true;
	ClientApi->LoginWithIOSDeviceID(Request,
		PlayFab::UPlayFabClientAPI::FLoginWithIOSDeviceIDDelegate::CreateLambda(HandleSuccess), OnError);
#elif PLATFORM_ANDROID
	PlayFab::ClientModels::FLoginWithAndroidDeviceIDRequest Request;
	Request.InfoRequestParameters = InfoParams;
	Request.AndroidDeviceId = DeviceId;
	Request.CreateAccount = true;
	ClientApi->LoginWithAndroidDeviceID(Request,
		PlayFab::UPlayFabClientAPI::FLoginWithAndroidDeviceIDDelegate::CreateLambda(HandleSuccess), OnError);
#else
	PlayFab::ClientModels::FLoginWithCustomIDRequest Request;
	Request.InfoRequestParameters = InfoParams;
	Request.CustomId = DeviceId;
	Request.CreateAccount = true;
	ClientApi->LoginWithCustomID(Request,
		PlayFab::UPlayFabClientAPI::FLoginWithCustomIDDelegate::CreateLambda(HandleSuccess), OnError);
#endif
}

void USnareBackendSubsystem::FinishLoginSucceeded(const FString& InPlayFabId, bool bNewlyCreated,
	const FString& InDisplayName)
{
	LoginState = ESnareBackendLoginState::LoggedIn;
	PlayFabId = InPlayFabId;
	DisplayName = InDisplayName;
	LoggedInAtUtc = FDateTime::UtcNow();
	ConsecutiveFailures = 0;
	CancelRetry();
	OnConnectionChanged.Broadcast();
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 표시 이름 — %s"),
		DisplayName.IsEmpty() ? TEXT("없음(닉네임 입력 창을 띄운다)") : *DisplayName);
	OnDisplayNameChanged.Broadcast();
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] PlayFab 로그인 성공 — PlayFabId %s%s"),
		*InPlayFabId, bNewlyCreated ? TEXT(" (계정을 새로 만들었다)") : TEXT(""));

	// 올라와 있던 캐시가 다른 계정 것이면 지금 버린다 — 남의 인벤토리를 한 프레임도 보여 주지 않는다.
	if (PlayerSnapshot.HasOwner() && PlayerSnapshot.PlayFabId != PlayFabId)
	{
		PlayerSnapshot = FSnarePlayerSnapshot();
		OnPlayerSnapshotChanged.Broadcast();
	}

	// 만료로 멈춰 있던 서버 호출을 다시 보낸다 (DESIGN 4.4 「접속 복구」 — 한 번만).
	FlushLoginWaiters(true);

	// 가짜 로그인(2층)은 진짜 세션이 없다 — 분석 이벤트·보유 상태 조회를 PlayFab 으로 띄우지 않는다.
	if (TestLoginOverride)
	{
		return;
	}

	// M8-06: 세션은 **로그인이 성공한 순간** 시작된다 — 앱을 켠 순간이 아니다. 로그인 전 이벤트는
	// 통로가 버리니(`WriteGameEvent`), 여기보다 앞에서 보내면 한 줄도 안 나간다.
	// 계정을 새로 만들었는지는 PlayFab 응답이 말해 준 것뿐이다 — 이 자리가 판정하지 않는다.
	TMap<FName, FString> Payload;
	Payload.Add(TEXT("is_new_account"), bNewlyCreated ? TEXT("true") : TEXT("false"));
	WriteGameEvent(TEXT("session_started"), Payload);

	FetchPlayerState();
}

void USnareBackendSubsystem::FinishLoginFailed(const FString& ErrorReport)
{
	LoginState = ESnareBackendLoginState::Failed;
	UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] PlayFab 로그인 실패 — %s"), *ErrorReport);
	FlushLoginWaiters(false);
	ScheduleRetry();
	OnConnectionChanged.Broadcast();
}

FString USnareBackendSubsystem::GetCoinCurrencyCode()
{
	return TEXT("SC");
}

FString USnareBackendSubsystem::GetGemCurrencyCode()
{
	return TEXT("GM");
}

FString USnareBackendSubsystem::GetPlayerStateApiName()
{
	// 인벤토리와 가상 재화가 이 응답 하나에 같이 온다 — 코인만 따로 묻는 왕복을 만들지 않는다.
	return TEXT("GetUserInventory");
}

bool USnareBackendSubsystem::ApplyPlayerSnapshot(const FSnarePlayerSnapshot& NewSnapshot, bool bFromBackend)
{
	if (!NewSnapshot.HasOwner())
	{
		// 주인 없는 상태를 캐시로 두면 다음 로그인 때 누구 것인지 가릴 수가 없다.
		UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] 주인(PlayFabId) 없는 스냅샷은 반영하지 않는다"));
		return false;
	}

	// 바뀌기 **전** 잔액. 이벤트를 "바뀐 순간" 에만 보내려면 앞뒤를 맞대 볼 수밖에 없다 —
	// 같은 값을 다시 받은 응답까지 세면 그건 잔액 변화가 아니라 응답 횟수가 된다.
	const int32 PreviousCoin = PlayerSnapshot.CoinBalance;
	const int32 PreviousGem = PlayerSnapshot.GemBalance;

	PlayerSnapshot = NewSnapshot;
	if (bFromBackend)
	{
		FetchState = ESnareBackendFetchState::Fetched;
	}

	OnPlayerSnapshotChanged.Broadcast();

	// 캐시가 바뀐 **뒤**에 보낸다 — 이벤트가 보내지든 버려지든(로그인 전) 캐시는 이미 바뀌어 있다.
	WriteBalanceChangedEvent(GetCoinCurrencyCode(), PreviousCoin, PlayerSnapshot.CoinBalance, bFromBackend);
	WriteBalanceChangedEvent(GetGemCurrencyCode(), PreviousGem, PlayerSnapshot.GemBalance, bFromBackend);

	// M9-07b: 시작 장비는 **보유 상태가 백엔드에서 다시 온 뒤**에 손에 건다 (DESIGN 3.5 「받은 뒤」).
	// 로컬 캐시를 올린 것(`bFromBackend == false`)으로는 안 울린다 — 그건 지난번 사본이라
	// 방금 받은 것이 들어 있다는 보장이 없다.
	if (bFromBackend && bStarterKitEquipPending)
	{
		bStarterKitEquipPending = false;
		OnStarterKitGranted.Broadcast();
	}
	return true;
}

void USnareBackendSubsystem::WriteBalanceChangedEvent(const FString& CurrencyCode, const int32 PreviousAmount,
	const int32 NewAmount, const bool bFromBackend)
{
	if (PreviousAmount == NewAmount)
	{
		return;
	}

	// 얼마가 **오갔는지**(차액)는 싣지 않는다 — 소비·지급의 원장은 백엔드가 쓴다
	// (`docs/ANALYTICS.md` 표 C). 클라이언트가 말할 수 있는 것은 "지금 보이는 잔액이 이것" 까지다.
	TMap<FName, FString> Payload;
	Payload.Add(TEXT("currency"), CurrencyCode);
	Payload.Add(TEXT("new_balance"), FString::FromInt(NewAmount));
	// 어디서 온 수인지 갈라 두지 않으면 분석에서 백엔드가 확인해 준 잔액과 표시용 캐시가 섞인다.
	// 칸 이름이 `source` 면 PlayFab 이 예약 필드라며 통째로 거절한다 (#0204 — `play_0201.log`).
	Payload.Add(TEXT("balance_source"), bFromBackend ? TEXT("backend") : TEXT("local_cache"));

	WriteGameEvent(TEXT("currency_balance_changed"), Payload);
}

#if !UE_BUILD_SHIPPING
bool USnareBackendSubsystem::DevGrantOffline(FName ItemID, int32 Count)
{
	if (ItemID.IsNone() || Count <= 0)
	{
		return false;
	}
	if (IsSnapshotFromBackend())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] 백엔드 스냅샷이 있어 오프라인 지급을 거절한다 (%s)"), *ItemID.ToString());
		return false;
	}

	FSnarePlayerSnapshot Next = PlayerSnapshot;
	if (!Next.HasOwner())
	{
		// 주인 없는 스냅샷은 반영이 안 된다 — 오프라인 개발 계정 이름을 붙인다. 실제 로그인이 되면 주인이 달라 통째로 비운다.
		Next.PlayFabId = TEXT("offline.dev");
	}
	FInventorySlot* Existing = Next.InventorySlots.FindByPredicate(
		[ItemID](const FInventorySlot& Slot) { return Slot.ItemID == ItemID; });
	if (Existing != nullptr)
	{
		Existing->Count += Count;
	}
	else
	{
		FInventorySlot NewSlot;
		NewSlot.ItemID = ItemID;
		NewSlot.Count = Count;
		Next.InventorySlots.Add(NewSlot);
	}
	return ApplyPlayerSnapshot(Next, /*bFromBackend=*/false);
}
#endif

bool USnareBackendSubsystem::SaveLocalCache() const
{
	if (!PlayerSnapshot.HasOwner())
	{
		return false;
	}

	USnarePlayerCacheSave* const Cache = USnarePlayerCacheSave::MakeFrom(PlayerSnapshot);
	return UGameplayStatics::SaveGameToSlot(Cache, USnarePlayerCacheSave::GetSlotName(), 0);
}

bool USnareBackendSubsystem::LoadLocalCache()
{
	const USnarePlayerCacheSave* const Cache =
		Cast<USnarePlayerCacheSave>(UGameplayStatics::LoadGameFromSlot(USnarePlayerCacheSave::GetSlotName(), 0));

	FSnarePlayerSnapshot Cached;
	if (!USnarePlayerCacheSave::TryReadSnapshot(Cache, PlayFabId, Cached))
	{
		return false;
	}

	// `bFromBackend = false` — 이 값은 표시용이다. 백엔드 응답이 오면 통째로 덮인다.
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 로컬 캐시를 올렸다 — 칸 %d개 · 코인 %d · 젬 %d (표시 전용)"),
		Cached.InventorySlots.Num(), Cached.CoinBalance, Cached.GemBalance);
	return ApplyPlayerSnapshot(Cached, /*bFromBackend=*/false);
}

void USnareBackendSubsystem::FetchPlayerState()
{
	if (LoginState != ESnareBackendLoginState::LoggedIn || FetchState == ESnareBackendFetchState::InProgress)
	{
		return;
	}

	const PlayFabClientPtr ClientApi = IPlayFabModuleInterface::Get().GetClientAPI();
	if (!ClientApi.IsValid())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] PlayFab 클라이언트 API 를 얻지 못했다 — 보유 상태 조회 건너뜀"));
		FetchState = ESnareBackendFetchState::Failed;
		return;
	}

	FetchState = ESnareBackendFetchState::InProgress;
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 보유 상태 조회 시작 — %s"), *GetPlayerStateApiName());

	const FWeakSubsystem WeakThis(this);
	const FString OwnerId = PlayFabId;
	PlayFab::ClientModels::FGetUserInventoryRequest Request;
	ClientApi->GetUserInventory(Request,
		PlayFab::UPlayFabClientAPI::FGetUserInventoryDelegate::CreateLambda(
			[WeakThis, OwnerId](const PlayFab::ClientModels::FGetUserInventoryResult& Result)
			{
				USnareBackendSubsystem* const Self = WeakThis.Get();
				if (!Self)
				{
					return;
				}

				FSnarePlayerSnapshot Snapshot;
				Snapshot.PlayFabId = OwnerId;
				Snapshot.FetchedAtUtc = FDateTime::UtcNow();
				for (const PlayFab::ClientModels::FItemInstance& Item : Result.Inventory)
				{
					// 스택 가능한 소모품은 남은 개수가 `RemainingUses` 에 온다. 없으면 인스턴스 한 줄이 1개다.
					const int32 Count = Item.RemainingUses.notNull() ? Item.RemainingUses.mValue : 1;
					Snapshot.AddItem(FName(*Item.ItemId), Count);
				}
				if (const int32* const Coin = Result.VirtualCurrency.Find(GetCoinCurrencyCode()))
				{
					Snapshot.CoinBalance = *Coin;
				}
				// 젬은 IAP 가 서기 전까지 응답에 아예 안 들어 있다 — 없으면 0 그대로 둔다.
				// 없는 재화를 0 으로 **채워 넣는** 것이 아니라, 스냅샷의 기본값이 0 이라 손대지 않는 것이다.
				if (const int32* const Gem = Result.VirtualCurrency.Find(GetGemCurrencyCode()))
				{
					Snapshot.GemBalance = *Gem;
				}
				// 탈것은 인벤토리 응답에 없다(서버 Internal Data) — 같은 계정이면 지난 서버 응답을 그대로 둔다.
				if (Self->PlayerSnapshot.PlayFabId == OwnerId)
				{
					Snapshot.Mounts = Self->PlayerSnapshot.Mounts;
					Snapshot.MountSaddles = Self->PlayerSnapshot.MountSaddles;
					Snapshot.CodexEntries = Self->PlayerSnapshot.CodexEntries; // 도감도 Internal Data 다
					Snapshot.Achievements = Self->PlayerSnapshot.Achievements; // 업적도(M13-28)
				}

				Self->FinishFetchSucceeded(Snapshot);
			}),
		PlayFab::FPlayFabErrorDelegate::CreateLambda(
			[WeakThis](const PlayFab::FPlayFabCppError& Error)
			{
				if (USnareBackendSubsystem* const Self = WeakThis.Get())
				{
					Self->FinishFetchFailed(Error.GenerateErrorReport(), IsAuthError(Error));
				}
			}));
}

void USnareBackendSubsystem::FinishFetchSucceeded(const FSnarePlayerSnapshot& NewSnapshot)
{
	if (!ApplyPlayerSnapshot(NewSnapshot, /*bFromBackend=*/true))
	{
		FetchState = ESnareBackendFetchState::Failed;
		return;
	}

	ConsecutiveFailures = 0;
	OnConnectionChanged.Broadcast();

	// 캐시는 백엔드가 확정해 준 것만 적는다 — 로컬에서 만든 값이 파일에 남으면 다음 부팅에 진짜처럼 보인다.
	SaveLocalCache();
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 보유 상태 받음 — 칸 %d개 · 코인 %d · 젬 %d"),
		NewSnapshot.InventorySlots.Num(), NewSnapshot.CoinBalance, NewSnapshot.GemBalance);

	// M9-07b: 시작 장비를 묻는 자리는 **여기 하나다**. 보유 상태를 받은 뒤라야 지급분이 캐시에
	// 반영될 다음 왕복과 순서가 맞고, 두 번째부터는 아래 함수가 상태를 보고 스스로 빠진다.
	RequestStarterKit();

	// 도감을 늘리는 판정(포획·수확·낚시·전리품·제작·설계도)은 전부 끝에 보유 상태를 다시 묻는다 — 그 뒤에 도감도 받는다.
	// ponytail: 조회마다 CloudScript 1번 더 — 왕복이 문제 되면 도감을 늘린 응답에만 `entries` 를 싣는다.
	RefreshCodexEntries();

	if (!bMountsAsked)
	{
		bMountsAsked = true;
		const FWeakSubsystem WeakThis(this);
		CallServerFunction(TEXT("GetMounts"), nullptr,
			[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
			{
				USnareBackendSubsystem* const Self = WeakThis.Get();
				if (Self && !(bOk && Self->ApplyMountsReply(Response)))
				{
					Self->bMountsAsked = false; // 다음 보유 상태 조회 때 다시 묻는다
					UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] 탈것 목록 못 받음 — %s"), *Error);
				}
			});
	}

	// 기기 시험용 테스터 키트 — 주는지는 서버가 테스터 목록으로 정한다(클라는 묻기만, CLAUDE.md 8).
	// 출시(TestFlight) 빌드도 묻는다 — 목록 밖 계정은 서버가 거절하니 안전하다.
	// 받았으면 탈것·보유 상태를 다시 묻는다. 목록 밖·이미 받음은 조용히 넘어간다.
	if (!bTesterKitAsked)
	{
		bTesterKitAsked = true;
		const FWeakSubsystem WeakThis(this);
		CallServerFunction(TEXT("GrantTesterKit"), nullptr,
			[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
			{
				USnareBackendSubsystem* const Self = WeakThis.Get();
				bool bGranted = false;
				if (!Self || !bOk || !Response.IsValid() || !Response->TryGetBoolField(TEXT("granted"), bGranted) || !bGranted)
				{
					UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 테스터 키트 안 받음 — %s"), *Error);
					return;
				}
				UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 테스터 키트 받음 — 보유 상태·탈것 다시 묻는다"));
				Self->bMountsAsked = false;
				Self->FetchPlayerState();
			});
	}
}

bool USnareBackendSubsystem::ApplyMountsReply(const TSharedPtr<FJsonObject>& Response)
{
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("mounts"), List))
	{
		return false;
	}
	FSnarePlayerSnapshot Next = PlayerSnapshot;
	Next.Mounts.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		Next.Mounts.AddUnique(FName(*Value->AsString()));
	}
	Next.MountSaddles.Reset();
	const TSharedPtr<FJsonObject>* Saddles = nullptr;
	if (Response->TryGetObjectField(TEXT("saddles"), Saddles))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Saddles)->Values)
		{
			Next.MountSaddles.Add(FName(*Pair.Key), FName(*Pair.Value->AsString()));
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Species = nullptr;
	if (Response->TryGetArrayField(TEXT("species"), Species))
	{
		MountSpeciesChoices.Reset();
		for (const TSharedPtr<FJsonValue>& Value : *Species)
		{
			MountSpeciesChoices.Add(FName(*Value->AsString()));
		}
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 탈것 %d종 · 안장 %d개 받음"), Next.Mounts.Num(), Next.MountSaddles.Num());
	// false: 인벤토리는 지난 사본 그대로라 시작 장비 장착 신호(`bFromBackend`)를 여기서 울리지 않는다.
	// ponytail: 여기선 캐시 파일을 안 쓴다(슬롯 하나라 2층 검사가 실제 캐시를 덮는다) — 다음 보유 상태 조회가 탈것 칸째 저장한다.
	return ApplyPlayerSnapshot(Next, /*bFromBackend=*/false);
}

void USnareBackendSubsystem::RefreshCodexEntries()
{
	const FWeakSubsystem WeakThis(this);
	CallServerFunction(TEXT("GetCodexRewards"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			USnareBackendSubsystem* const Self = WeakThis.Get();
			if (!Self || !bOk || !Self->ApplyCodexReply(Response))
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] 도감 칸 못 받음 — %s"), *Error);
			}
		});
}

bool USnareBackendSubsystem::ApplyCodexReply(const TSharedPtr<FJsonObject>& Response)
{
	const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
	if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("entries"), List))
	{
		return false;
	}
	FSnarePlayerSnapshot Next = PlayerSnapshot;
	Next.CodexEntries.Reset();
	for (const TSharedPtr<FJsonValue>& Value : *List)
	{
		Next.CodexEntries.AddUnique(FName(*Value->AsString()));
	}
	// 업적(M13-28)도 같은 답에 실려 온다 — 서버 `achievements`({counters, done})를 그대로 옮긴다.
	// 칸이 없으면(배포 전 서버) 지난 값을 둔다.
	const TSharedPtr<FJsonObject>* Ach = nullptr;
	if (Response->TryGetObjectField(TEXT("achievements"), Ach))
	{
		Next.Achievements = FSnareAchievementState();
		const TSharedPtr<FJsonObject>* Counters = nullptr;
		if ((*Ach)->TryGetObjectField(TEXT("counters"), Counters))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Counters)->Values)
			{
				Next.Achievements.Counters.Add(FName(*Pair.Key), static_cast<int32>(Pair.Value->AsNumber()));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Done = nullptr;
		if ((*Ach)->TryGetArrayField(TEXT("done"), Done))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Done)
			{
				Next.Achievements.Completed.AddUnique(FName(*Value->AsString()));
			}
		}
	}

	// 계정마다 첫 답은 기준만 — 접속할 때마다 모은 업적·도감이 한꺼번에 터지지 않게 (DESIGN 8.15 「첫 조회 무음」).
	const bool bFirstAnswer = !bCodexBaselineSet || CodexBaselineOwner != PlayerSnapshot.PlayFabId;
	bCodexBaselineSet = true;
	CodexBaselineOwner = PlayerSnapshot.PlayFabId;

	const FSnareAchievementState& Was = PlayerSnapshot.Achievements;
	if (Next.CodexEntries == PlayerSnapshot.CodexEntries && Next.Achievements.Completed == Was.Completed
		&& Next.Achievements.Counters.OrderIndependentCompareEqual(Was.Counters))
	{
		return true; // 같은 답이면 알림을 안 울린다 — 도감 화면이 알림마다 다시 묻지 않게
	}
	const SnareAchievementNotice::FDiff Diff = SnareAchievementNotice::Diff(Was.Completed, Next.Achievements.Completed,
		PlayerSnapshot.CodexEntries, Next.CodexEntries, bFirstAnswer);
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 도감 칸 %d개 · 업적 달성 %d개 받음"),
		Next.CodexEntries.Num(), Next.Achievements.Completed.Num());
	// false: 탈것 응답과 같은 이유 — 인벤토리는 지난 사본이라 시작 장비 장착 신호를 울리지 않는다.
	ApplyPlayerSnapshot(Next, /*bFromBackend=*/false);
	AnnounceAchievementNotice(Diff);
	return true;
}

void USnareBackendSubsystem::AnnounceAchievementNotice(const SnareAchievementNotice::FDiff& Diff)
{
	if (Diff.IsEmpty())
	{
		return;
	}
	SnareAchievementNotice::FNames Names;
	SnareAchievementNotice::AddAchievementNames(
		LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Achievements/DT_AchievementTable.DT_AchievementTable")), Names);
	SnareAchievementNotice::AddCodexNames(
		LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable")),
		LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable")), Names);
	const TArray<SnareAchievementNotice::FCard> Cards = SnareAchievementNotice::MakeCards(Diff, Names);
	const FText Message = SnareAchievementNotice::JoinCards(Cards);
	LastNoticeCardCount = Cards.Num();
	UE_LOG(LogSnarehaven, Log, TEXT("[Notice] 업적 %d · 도감 %d → 카드 %d 「%s」"), Diff.NewAchievements.Num(),
		Diff.NewCodexEntries.Num(), Cards.Num(), *Message.ToString().Replace(TEXT("\n"), TEXT(" / ")));
	// 듣는 HUD 가 없으면(로비 등) 카드를 버린다 — 쌓아 두면 존에 들어서는 순간 한꺼번에 터진다 (DESIGN 8.15 「어느 화면에」).
	if (!OnAchievementNotice.IsBound())
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Notice] 띄울 HUD 없음 — 카드 버림"));
		return;
	}
	OnAchievementNotice.Broadcast(Cards);
	// 한 답에 소리 한 번 — 업적이 있으면 업적 소리 (DESIGN 8.15 「소리 횟수」). 내 계정 일이라 내 기기만.
	LastNoticeCue = SnareAchievementNotice::SoundCue(Diff);
	++NoticeSoundCount;
	USnareSoundSubsystem::Play2D(GetGameInstance(), LastNoticeCue, SnareAchievementNotice::SoundVolume(LastNoticeCue));
}

void USnareBackendSubsystem::FinishFetchFailed(const FString& ErrorReport, bool bAuthExpired)
{
	FetchState = ESnareBackendFetchState::Failed;
	UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] 보유 상태 조회 실패 — %s"), *ErrorReport);
	OnConnectionChanged.Broadcast();
	if (bAuthExpired)
	{
		Relogin(); // 성공하면 FinishLoginSucceeded 가 보유 상태를 다시 받는다
		return;
	}
	ScheduleRetry();
}

FString USnareBackendSubsystem::GetStarterKitFunctionName()
{
	// 이름의 원본은 `tools/playfab/cloudscript_main.js` 의 `handlers.GrantStarterKit` 이다 —
	// 2층 검사가 그 파일을 읽어 이 글자와 맞댄다(이름이 갈라지면 왕복이 조용히 실패한다).
	return TEXT("GrantStarterKit");
}

void USnareBackendSubsystem::RequestStarterKit()
{
	if (LoginState != ESnareBackendLoginState::LoggedIn
		|| StarterKitState != ESnareBackendStarterKitState::NotStarted)
	{
		return;
	}

	// 3층 검사용 주입 (`-SnareStarterKitInject=<아이템 ID[,ID…]|none>`). 지급 핸들러는 아직 콘솔에
	// 안 올라갔고(BACKEND.md 배포 상태 · M2-03 과 같은 사람 몫) 카탈로그에도 행이 없어, 진짜 왕복은
	// 그 전까지 늘 오류로 돌아온다. **받은 뒤의 길**(캐시 재조회 → 장착)을 눈으로 보려면 응답만
	// 갈아끼울 길이 하나 필요하다 — 아래 `ApplyStarterKitResponseJson` **같은 자리**로 들어간다.
	// 쉼표 목록을 받으려면 네 번째 인자를 꺼야 한다 (루프#158 이 겪은 `FParse::Value` 의 기본값).
	FString InjectedItems;
	if (SnareCheckSwitch::Value(TEXT("SnareStarterKitInject="), InjectedItems,
		/*bShouldStopOnSeparator=*/false))
	{
		StarterKitState = ESnareBackendStarterKitState::InProgress;

		TArray<FString> ItemIds;
		InjectedItems.ParseIntoArray(ItemIds, TEXT(","), /*InCullEmpty=*/true);
		const bool bGranted = !ItemIds.IsEmpty() && ItemIds[0] != TEXT("none");

		// 핸들러가 돌려주는 모양 그대로 만든다 — 읽는 쪽이 진짜 응답과 같은 길을 타야 뜻이 있다.
		FString ItemsJson;
		if (bGranted)
		{
			for (const FString& ItemId : ItemIds)
			{
				ItemsJson += FString::Printf(TEXT("%s{\"%s\":\"%s\",\"count\":1}"),
					ItemsJson.IsEmpty() ? TEXT("") : TEXT(","), StarterKitItemIdField, *ItemId);
			}
		}
		const FString ResponseJson = FString::Printf(
			TEXT("{\"%s\":%s,\"alreadyGranted\":%s,\"%s\":[%s]}"),
			StarterKitGrantedField, bGranted ? TEXT("true") : TEXT("false"),
			bGranted ? TEXT("false") : TEXT("true"), StarterKitItemsField, *ItemsJson);

		UE_LOG(LogSnarehaven, Log,
			TEXT("[Backend] 검사용 시작 장비 응답 주입 (-SnareStarterKitInject=%s) — %s"),
			*InjectedItems, *ResponseJson);
		ApplyStarterKitResponseJson(ResponseJson);
		return;
	}

	const PlayFabClientPtr ClientApi = IPlayFabModuleInterface::Get().GetClientAPI();
	if (!ClientApi.IsValid())
	{
		FinishStarterKitFailed(TEXT("PlayFab 클라이언트 API 를 얻지 못했다"));
		return;
	}

	StarterKitState = ESnareBackendStarterKitState::InProgress;
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 시작 장비 지급을 물어본다 — CloudScript %s"),
		*GetStarterKitFunctionName());

	const FWeakSubsystem WeakThis(this);
	PlayFab::ClientModels::FExecuteCloudScriptRequest Request;
	Request.FunctionName = GetStarterKitFunctionName();
	// 인자는 없다 — **누가** 부르는지는 세션이 말하고, **무엇을** 줄지는 백엔드의 표가 정한다.
	// 아이템 ID 를 클라이언트가 실어 보내면 그게 곧 "아무거나 달라고 하는 길" 이다 (CLAUDE.md 8).
	ClientApi->ExecuteCloudScript(Request,
		PlayFab::UPlayFabClientAPI::FExecuteCloudScriptDelegate::CreateLambda(
			[WeakThis](const PlayFab::ClientModels::FExecuteCloudScriptResult& Result)
			{
				USnareBackendSubsystem* const Self = WeakThis.Get();
				if (Self == nullptr)
				{
					return;
				}

				// HTTP 는 성공했는데 스크립트가 터진 경우다 — 이때 `FunctionResult` 는 비어 있다.
				if (Result.Error.IsValid())
				{
					Self->FinishStarterKitFailed(FString::Printf(TEXT("%s: %s"),
						*Result.Error->Error, *Result.Error->Message));
					return;
				}

				Self->ApplyStarterKitResponseJson(Result.FunctionResult.toJSONString());
			}),
		PlayFab::FPlayFabErrorDelegate::CreateLambda(
			[WeakThis](const PlayFab::FPlayFabCppError& Error)
			{
				if (USnareBackendSubsystem* const Self = WeakThis.Get())
				{
					Self->FinishStarterKitFailed(Error.GenerateErrorReport());
				}
			}));
}

TFunction<void(const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone)>
	USnareBackendSubsystem::TestServerOverride;

void USnareBackendSubsystem::CallServerFunction(const FString& FunctionName, const TSharedPtr<FJsonObject>& Args,
	FServerCallDone OnDone)
{
	SendServerCall(FunctionName, Args, MoveTemp(OnDone), /*bResent=*/false);
}

void USnareBackendSubsystem::SendServerCall(const FString& FunctionName, const TSharedPtr<FJsonObject>& Args,
	FServerCallDone OnDone, bool bResent)
{
	// 세션 만료는 여기 한 곳에서 거른다 — 부르는 쪽(상점·목장·배낭 …)은 만료를 모른 채 답 하나만 받는다.
	const FWeakSubsystem WeakThis(this);
	FServerCallDone Finish = [WeakThis, FunctionName, Args, OnDone, bResent](bool bOk,
		const TSharedPtr<FJsonObject>& Response, const FString& Error)
	{
		USnareBackendSubsystem* const Self = WeakThis.Get();
		if (Self != nullptr && !bResent
			&& SnareServerReply::ClassifyFailure(bOk, Response, Error) == SnareServerReply::EFailureKind::Login)
		{
			UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 세션 만료 — 다시 로그인한 뒤 %s 를 한 번 다시 보낸다"), *FunctionName);
			Self->LoginWaiters.Add([WeakThis, FunctionName, Args, OnDone](bool bLoggedIn)
				{
					USnareBackendSubsystem* const Inner = WeakThis.Get();
					if (bLoggedIn && Inner != nullptr)
					{
						Inner->SendServerCall(FunctionName, Args, OnDone, /*bResent=*/true);
						return;
					}
					OnDone(false, nullptr, SnareServerReply::TagLoginError(TEXT("다시 로그인하지 못했다")));
				});
			Self->Relogin();
			return;
		}
		OnDone(bOk, Response, Error);
	};

	if (TestServerOverride)
	{
		TestServerOverride(FunctionName, Args, MoveTemp(Finish));
		return;
	}

	// 로그인 중·실패 중에는 보내지 않고 바로 「로그인 중」 으로 돌려준다 — 화면이 끊김과 가려 말할 수 있게.
	if (LoginState != ESnareBackendLoginState::LoggedIn)
	{
		OnDone(false, nullptr, SnareServerReply::TagLoginError(TEXT("로그인 전이다")));
		return;
	}
	const PlayFabClientPtr ClientApi = IPlayFabModuleInterface::Get().GetClientAPI();
	if (!ClientApi.IsValid())
	{
		OnDone(false, nullptr, SnareServerReply::TagNetworkError(TEXT("PlayFab 클라이언트 API 가 없다")));
		return;
	}

	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 서버 판정을 묻는다 — CloudScript %s%s"), *FunctionName,
		bResent ? TEXT(" (다시 로그인 뒤 재전송)") : TEXT(""));
	PlayFab::ClientModels::FExecuteCloudScriptRequest Request;
	Request.FunctionName = FunctionName;
	if (Args.IsValid())
	{
		Request.FunctionParameter = PlayFab::FJsonKeeper(Args);
	}

	// 성공·실패 둘 다 같은 Finish 로 모은다. 람다 두 개가 한 콜백을 나눠 쥔다.
	const TSharedRef<FServerCallDone> Done = MakeShared<FServerCallDone>(MoveTemp(Finish));
	ClientApi->ExecuteCloudScript(Request,
		PlayFab::UPlayFabClientAPI::FExecuteCloudScriptDelegate::CreateLambda(
			[Done, FunctionName](const PlayFab::ClientModels::FExecuteCloudScriptResult& Result)
			{
				if (Result.Error.IsValid())
				{
					(*Done)(false, nullptr, SnareServerReply::TagRejectedError(FString::Printf(TEXT("%s: %s: %s"),
						*FunctionName, *Result.Error->Error, *Result.Error->Message)));
					return;
				}
				TSharedPtr<FJsonObject> Root;
				const TSharedRef<TJsonReader<>> Reader =
					TJsonReaderFactory<>::Create(Result.FunctionResult.toJSONString());
				if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
				{
					(*Done)(false, nullptr, SnareServerReply::TagRejectedError(
						FString::Printf(TEXT("%s: 응답을 읽지 못했다"), *FunctionName)));
					return;
				}
				(*Done)(true, Root, FString());
			}),
		PlayFab::FPlayFabErrorDelegate::CreateLambda(
			[Done](const PlayFab::FPlayFabCppError& Error)
			{
				(*Done)(false, nullptr, TagPlayFabError(Error));
			}));
}

FString USnareBackendSubsystem::GetDeleteAccountFunctionName()
{
	return TEXT("DeleteMyAccount");
}

void USnareBackendSubsystem::DeleteAccount(FDeleteAccountDone OnDone)
{
	if (bDeletingAccount)
	{
		OnDone(false, TEXT("이미 삭제를 요청하는 중이다"));
		return;
	}
	bDeletingAccount = true;

	// 확인 문자열은 서버가 다시 본다 — 다른 자리에서 잘못 불린 호출 하나로 계정이 사라지지 않게.
	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("confirm"), TEXT("DELETE"));
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 계정 삭제 요청 — CloudScript %s"), *GetDeleteAccountFunctionName());

	const FWeakSubsystem WeakThis(this);
	CallServerFunction(GetDeleteAccountFunctionName(), Args,
		[WeakThis, OnDone = MoveTemp(OnDone)](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			USnareBackendSubsystem* const Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			bool bDeleted = false;
			FString Reason = Error;
			if (bOk && Response.IsValid())
			{
				Response->TryGetBoolField(TEXT("deleted"), bDeleted);
				Response->TryGetStringField(TEXT("error"), Reason); // 서버 fail() 은 bOk=true 로 온다
			}
			if (!bDeleted)
			{
				Self->bDeletingAccount = false;
				UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] 계정 삭제 실패 — 캐시를 그대로 둔다: %s"), *Reason);
				OnDone(false, Reason.IsEmpty() ? FString(TEXT("서버가 삭제를 확인해 주지 않았다")) : Reason);
				return;
			}
			bool bFully = false;
			Response->TryGetBoolField(TEXT("fullyDeleted"), bFully);
			UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 서버가 계정을 지웠다 — %s"),
				bFully ? TEXT("타이틀 계정 삭제") : TEXT("데이터 비우기(남은 빈 계정은 운영자 몫)"));
			Self->UnlinkDeviceThen([WeakThis, OnDone]()
				{
					if (USnareBackendSubsystem* const Inner = WeakThis.Get())
					{
						Inner->FinishAccountDeleted();
						OnDone(true, FString());
						if (ShouldAutoLogin(Inner->GetGameInstance()))
						{
							Inner->StartLogin(); // 새 게스트 계정으로 다시 들어간다
						}
					}
				});
		});
}

void USnareBackendSubsystem::UnlinkDeviceThen(TFunction<void()> Next)
{
	const PlayFabClientPtr ClientApi = IPlayFabModuleInterface::Get().GetClientAPI();
	if (LoginState != ESnareBackendLoginState::LoggedIn || !ClientApi.IsValid())
	{
		Next();
		return;
	}

	// 성공·실패 둘 다 Next 로 간다. 서버가 이미 타이틀 계정을 지웠으면 세션이 끊겨 실패할 수 있다 —
	// 그래도 다시 붙는 계정은 데이터가 빈 계정이다(BACKEND.md 「계정 삭제」).
	const TSharedRef<TFunction<void()>> Shared = MakeShared<TFunction<void()>>(MoveTemp(Next));
	const FString DeviceId = MakeDeviceIdForInstance(MakeAutoLoginContext(GetGameInstance()));
	auto OnError = PlayFab::FPlayFabErrorDelegate::CreateLambda([Shared](const PlayFab::FPlayFabCppError& Error)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] 기기 ID 연결 끊기 실패 — %s"), *Error.GenerateErrorReport());
			(*Shared)();
		});
#if PLATFORM_IOS
	PlayFab::ClientModels::FUnlinkIOSDeviceIDRequest Request;
	Request.DeviceId = DeviceId;
	ClientApi->UnlinkIOSDeviceID(Request, PlayFab::UPlayFabClientAPI::FUnlinkIOSDeviceIDDelegate::CreateLambda(
		[Shared](const PlayFab::ClientModels::FUnlinkIOSDeviceIDResult&) { (*Shared)(); }), OnError);
#elif PLATFORM_ANDROID
	PlayFab::ClientModels::FUnlinkAndroidDeviceIDRequest Request;
	Request.AndroidDeviceId = DeviceId;
	ClientApi->UnlinkAndroidDeviceID(Request, PlayFab::UPlayFabClientAPI::FUnlinkAndroidDeviceIDDelegate::CreateLambda(
		[Shared](const PlayFab::ClientModels::FUnlinkAndroidDeviceIDResult&) { (*Shared)(); }), OnError);
#else
	PlayFab::ClientModels::FUnlinkCustomIDRequest Request;
	Request.CustomId = DeviceId;
	ClientApi->UnlinkCustomID(Request, PlayFab::UPlayFabClientAPI::FUnlinkCustomIDDelegate::CreateLambda(
		[Shared](const PlayFab::ClientModels::FUnlinkCustomIDResult&) { (*Shared)(); }), OnError);
#endif
}

void USnareBackendSubsystem::FinishAccountDeleted()
{
	UGameplayStatics::DeleteGameInSlot(USnarePlayerCacheSave::GetSlotName(), 0);
	LoginState = ESnareBackendLoginState::NotStarted;
	FetchState = ESnareBackendFetchState::NotStarted;
	StarterKitState = ESnareBackendStarterKitState::NotStarted;
	PlayFabId.Reset();
	DisplayName.Reset();
	PlayerSnapshot = FSnarePlayerSnapshot();
	StarterKitGrantedItems.Reset();
	MountSpeciesChoices.Reset();
	bStarterKitEquipPending = false;
	bMountsAsked = false;
	bTesterKitAsked = false;
	bDeletingAccount = false;
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 계정 삭제 끝 — 로컬 캐시를 지우고 로그아웃했다"));
	OnPlayerSnapshotChanged.Broadcast();
}

bool USnareBackendSubsystem::ApplyStarterKitResponseJson(const FString& ResponseJson)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseJson);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		FinishStarterKitFailed(FString::Printf(TEXT("응답을 읽지 못했다 — %s"), *ResponseJson));
		return false;
	}

	bool bGranted = false;
	Root->TryGetBoolField(StarterKitGrantedField, bGranted);

	StarterKitGrantedItems.Reset();
	StarterKitState = ESnareBackendStarterKitState::Answered;

	if (!bGranted)
	{
		// 이미 받은 계정이다. **아무것도 안 한다** — 여기서 한 번 더 주면 몽둥이가 쌓인다
		// (DESIGN 3.5 「지급 횟수」). 판정은 백엔드가 했고 이 자리는 그 답을 따를 뿐이다.
		UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 시작 장비 — 이미 받은 계정이라 지급 0건"));
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (Root->TryGetArrayField(StarterKitItemsField, Items) && Items != nullptr)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Items)
		{
			const TSharedPtr<FJsonObject>* Entry = nullptr;
			FString ItemId;
			if (Value.IsValid() && Value->TryGetObject(Entry) && Entry != nullptr
				&& (*Entry)->TryGetStringField(StarterKitItemIdField, ItemId) && !ItemId.IsEmpty())
			{
				// **개수는 안 읽는다** — 몇 개를 갖게 됐는지는 아래 재조회가 받아 오는 인벤토리가
				// 말한다. 응답의 수를 캐시에 더하면 그게 곧 클라이언트가 만든 보유량이다 (CLAUDE.md 8).
				StarterKitGrantedItems.Add(FName(*ItemId));
			}
		}
	}

	// 받은 것이 실제로 캐시에 들어오는 길은 **백엔드에 다시 묻는 것 하나뿐이다.**
	bStarterKitEquipPending = true;
	UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 시작 장비를 받았다 — %d종 · 보유 상태를 다시 물어본다"),
		StarterKitGrantedItems.Num());
	FetchPlayerState();
	return true;
}

void USnareBackendSubsystem::FinishStarterKitFailed(const FString& ErrorReport)
{
	// 시작 장비가 없어도 게임은 돈다(맨손) — 실패를 막지 않고 로그로만 남긴다. 다시 묻는 길은
	// 다음 실행이다: 계정에 표시가 안 찍혔으면 그때 받는다 (핸들러가 지급 먼저·표시 나중이다).
	StarterKitState = ESnareBackendStarterKitState::Failed;
	bStarterKitEquipPending = false;
	UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] 시작 장비 지급 실패 — %s"), *ErrorReport);
}

bool USnareBackendSubsystem::WriteGameEvent(const FName EventID, const TMap<FName, FString>& Payload)
{
	// 이름 없는 이벤트는 PlayFab 에서 이름 없는 한 줄로 쌓인다 — 세어도 뜻이 없으니 여기서 막는다.
	if (EventID.IsNone())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Analytics] 이름 없는 이벤트는 보내지 않는다"));
		return false;
	}

	// 로그인 전(Title ID 가 없어 건너뛴 경우 포함)은 오류가 아니다 — `StartLogin` 이 조용히 넘어가는 것과 같은 자리다.
	// 큐에 쌓지 않는 이유: 분석은 지급이 아니라 관찰이라, 나중에 몰아 보내면 시각만 어긋난다 (docs/ANALYTICS.md 표 B).
	if (LoginState != ESnareBackendLoginState::LoggedIn)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Analytics] 로그인 전이라 '%s' 이벤트를 버린다"), *EventID.ToString());
		return false;
	}

	const PlayFabClientPtr ClientApi = IPlayFabModuleInterface::Get().GetClientAPI();
	if (!ClientApi.IsValid())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Analytics] PlayFab 클라이언트 API 를 얻지 못했다 — '%s' 버림"),
			*EventID.ToString());
		return false;
	}

	PlayFab::ClientModels::FWriteClientPlayerEventRequest Request;
	Request.EventName = EventID.ToString();
	for (const TPair<FName, FString>& Entry : Payload)
	{
		// 값은 전부 문자열로 싣는다. 숫자 칸을 따로 두지 않는 이유는 보내는 쪽이 칸마다 타입을 고르게 되면
		// 같은 칸이 어떤 자리에서는 수, 어떤 자리에서는 글자로 나가기 때문이다 — 칸 이름의 원본은 표 하나다.
		Request.Body.Add(Entry.Key.ToString(), PlayFab::FJsonKeeper(Entry.Value));
	}

	// 실패해도 다시 보내지 않는다 — 재전송을 넣으면 한 사건이 두 번 세어질 수 있다 (DESIGN 4.1 과 같은 이유).
	const FString EventName = Request.EventName;
	ClientApi->WritePlayerEvent(Request,
		PlayFab::UPlayFabClientAPI::FWritePlayerEventDelegate::CreateLambda(
			[EventName](const PlayFab::ClientModels::FWriteEventResponse& Result)
			{
				UE_LOG(LogSnarehaven, Verbose, TEXT("[Analytics] '%s' 받아들여짐 — %s"), *EventName, *Result.EventId);
			}),
		PlayFab::FPlayFabErrorDelegate::CreateLambda(
			[EventName](const PlayFab::FPlayFabCppError& Error)
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("[Analytics] '%s' 보내기 실패 — %s"),
					*EventName, *Error.GenerateErrorReport());
			}));

	UE_LOG(LogSnarehaven, Log, TEXT("[Analytics] '%s' 보냄 — 칸 %d개"), *EventID.ToString(), Payload.Num());
	return true;
}

TFunction<void(const FString& Name, TFunction<void(const FString& AcceptedName, const FString& Error)> OnDone)>
	USnareBackendSubsystem::TestDisplayNameOverride;

FString USnareBackendSubsystem::GetSetNicknameFunctionName()
{
	return TEXT("SetNickname");
}

void USnareBackendSubsystem::SubmitNickname(const FString& RawName, FNicknameDone OnDone)
{
	if (bSubmittingNickname)
	{
		OnDone(false, TEXT("busy"), TEXT("닉네임을 올리는 중이다"));
		return;
	}
	const FString Name = SnareNickname::Trim(RawName);
	const FString LocalProblem = SnareNickname::ProblemCode(Name);
	if (!LocalProblem.IsEmpty())
	{
		// 서버가 어차피 거절할 이름 — 왕복 없이 같은 문구로 막는다. 판정 원본은 서버다.
		OnDone(false, LocalProblem, SnareNickname::MessageFor(LocalProblem));
		return;
	}
	bSubmittingNickname = true;

	const TSharedRef<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("name"), Name);
	const FWeakSubsystem WeakThis(this);
	CallServerFunction(GetSetNicknameFunctionName(), Args,
		[WeakThis, OnDone = MoveTemp(OnDone)](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			USnareBackendSubsystem* const Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			FString Code;
			FString Message;
			FString Accepted;
			if (!SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message)
				|| !Response->TryGetStringField(TEXT("name"), Accepted) || Accepted.IsEmpty())
			{
				Self->bSubmittingNickname = false;
				if (Code.IsEmpty())
				{
					Code = TEXT("bad_reply");
					Message = TEXT("서버 답에 이름이 없다");
				}
				UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] 닉네임 거절 — %s: %s"), *Code, *Message);
				OnDone(false, Code, Code == TEXT("network") ? SnareServerReply::DescribeFailure(bOk, Response, Error).ToString() : Message);
				return;
			}
			// 서버가 받아 준 이름 그대로 올린다 — 클라가 고친 글자가 표시 이름이 되지 않게.
			Self->UpdateDisplayName(Accepted,
				[WeakThis, OnDone](const FString& FinalName, const FString& DisplayError)
				{
					USnareBackendSubsystem* const Inner = WeakThis.Get();
					if (!Inner)
					{
						return;
					}
					Inner->bSubmittingNickname = false;
					if (!DisplayError.IsEmpty())
					{
						UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] 표시 이름 거절 — %s"), *DisplayError);
						OnDone(false, TEXT("display_name"), DisplayError);
						return;
					}
					Inner->DisplayName = FinalName;
					UE_LOG(LogSnarehaven, Log, TEXT("[Backend] 닉네임을 정했다 — %s"), *FinalName);
					Inner->OnDisplayNameChanged.Broadcast();
					OnDone(true, FString(), FString());
				});
		});
}

void USnareBackendSubsystem::UpdateDisplayName(const FString& Name,
	TFunction<void(const FString& AcceptedName, const FString& Error)> OnDone)
{
	if (TestDisplayNameOverride)
	{
		TestDisplayNameOverride(Name, MoveTemp(OnDone));
		return;
	}
	const PlayFabClientPtr ClientApi = IPlayFabModuleInterface::Get().GetClientAPI();
	if (LoginState != ESnareBackendLoginState::LoggedIn || !ClientApi.IsValid())
	{
		OnDone(FString(), TEXT("로그인 전이라 표시 이름을 못 올렸다"));
		return;
	}
	const TSharedRef<TFunction<void(const FString&, const FString&)>> Done =
		MakeShared<TFunction<void(const FString&, const FString&)>>(MoveTemp(OnDone));
	PlayFab::ClientModels::FUpdateUserTitleDisplayNameRequest Request;
	Request.DisplayName = Name;
	ClientApi->UpdateUserTitleDisplayName(Request,
		PlayFab::UPlayFabClientAPI::FUpdateUserTitleDisplayNameDelegate::CreateLambda(
			[Done, Name](const PlayFab::ClientModels::FUpdateUserTitleDisplayNameResult& Result)
			{
				(*Done)(Result.DisplayName.IsEmpty() ? Name : Result.DisplayName, FString());
			}),
		PlayFab::FPlayFabErrorDelegate::CreateLambda(
			[Done](const PlayFab::FPlayFabCppError& Error)
			{
				// 겹치는 이름(NameNotAvailable) 등 — 알약엔 한국어 문구, 로그엔 PlayFab 원문.
				UE_LOG(LogSnarehaven, Warning, TEXT("[Backend] PlayFab 표시 이름 오류 — %s: %s"), *Error.ErrorName, *Error.ErrorMessage);
				(*Done)(FString(), SnareNickname::DisplayNameMessage(Error.ErrorName));
			}));
}
