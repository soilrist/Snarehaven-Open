// ROADMAP M2-01 — PlayFab 게스트(디바이스 ID) 로그인 · M2-05 — 인벤토리·코인 원본 읽어 오기.
// 백엔드 결정·데이터 모델은 `docs/BACKEND.md`, 계정 규칙은 `docs/DESIGN.md` 4.4, 저장 규칙은 4.5.
// **이 클래스는 요청과 응답만 나른다.** 개수를 더하거나 경과 시간으로 지급을 계산하는 코드는 여기 없다 —
// 판정은 전부 PlayFab 이다 (DESIGN 4.1 권위 2단 표·4.8 · CLAUDE.md 8).
#pragma once

#include "CoreMinimal.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/SnareAchievementNotice.h"

struct FWorldContext;
class FJsonObject;

#include "SnareBackendSubsystem.generated.h"

/** 백엔드 로그인이 어디까지 갔는지. 지급 대기열(M3-06)이 이 값을 보고 왕복을 시작한다(M2-05). */
UENUM()
enum class ESnareBackendLoginState : uint8
{
	NotStarted,
	InProgress,
	LoggedIn,
	Failed
};

/** 보유 상태(인벤토리·코인)를 받아 오는 왕복이 어디까지 갔는지. */
UENUM()
enum class ESnareBackendFetchState : uint8
{
	NotStarted,
	InProgress,
	Fetched,
	Failed
};

/** 시작 장비 왕복(M9-07b)이 어디까지 갔는지. **한 실행에 한 번만** 나간다. */
UENUM()
enum class ESnareBackendStarterKitState : uint8
{
	NotStarted,
	InProgress,
	/** 답을 받았다 — 방금 받은 계정이든 이미 받았던 계정이든 백엔드가 말해 준 뒤다. */
	Answered,
	Failed
};

/**
 * 자동 로그인을 정할 때 보는 상황 한 묶음.
 * 전역(`GIsEditor`·`IsRunningGame()`…)을 판정 함수 안에서 바로 읽으면 2층이 "PIE 일 때" 를 만들어 볼 수
 * 없다 — 상황을 값으로 빼서 판정(`ShouldAutoLoginFor`)과 수집(`MakeAutoLoginContext`)을 갈랐다.
 */
struct FSnareAutoLoginContext
{
	/** `-game`·패키지 실행인가 (`IsRunningGame()`). */
	bool bRunningGame = false;

	/** 에디터 프로세스인가 (`GIsEditor`). 커맨드릿도 참일 수 있다 — 그래서 이것만으로는 안 켠다. */
	bool bEditor = false;

	/** 이 게임 인스턴스가 **PIE 월드**에 사는가 (`EWorldType::PIE`). */
	bool bPlayInEditor = false;

	/** 자동화 검사가 도는 중인가 (`GIsAutomationTesting`). */
	bool bAutomationTest = false;

	/** `Config/PlayFab.ini` 에 Title ID 가 적혀 있는가. */
	bool bHasTitleId = false;

	/** PIE 인스턴스 번호. PIE 가 아니면 `INDEX_NONE`. 기기 ID 를 창마다 가르는 데 쓴다. */
	int32 PlayInEditorInstance = INDEX_NONE;
};

/** 캐시가 바뀌었을 때. 인벤토리 UI·폰이 이걸 듣고 화면을 다시 그린다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerSnapshotChangedSignature);

/**
 * 업적 달성·도감 새 칸 알림 글 (DESIGN 8.15 「달성 알림」, M48-03). 서버 답의 차이로만 만든다 — 사냥·목장 HUD 가
 * 듣고 왼쪽 위 알림 카드에 띄운다. 카드는 한 답에 최대 2장, 한 장씩 따로 그린다(M48-03c).
 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnSnareAchievementNotice, const TArray<SnareAchievementNotice::FCard>& /*Cards*/);

/**
 * 시작 장비를 **방금 받았고 보유 상태까지 다시 받아 온** 순간. 폰이 이걸 듣고 받은 것을 손에 건다
 * (DESIGN 3.5 「받은 뒤」). 이미 받은 계정에서는 한 번도 안 울린다.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnStarterKitGrantedSignature);

/**
 * 게임이 시작될 때 PlayFab 에 게스트로 로그인한다.
 * EOS 로그인(`USnareOnlineSubsystem`)과는 **별개다** — EOS 는 세션·P2P 전용이고 계정의 원본은 PlayFab 이다.
 * MVP 는 둘을 연결하지 않는다 (DESIGN 4.4).
 * 화면에 보이는 것은 없다 — 확인은 로그(`LogSnarehaven` 의 `[Backend]` 줄)로 한다.
 */
UCLASS()
class SNAREHAVEN_API USnareBackendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 로그인을 시작한다. 이미 되어 있거나 하는 중이면 아무것도 안 한다. */
	void StartLogin();

	ESnareBackendLoginState GetLoginState() const { return LoginState; }

	// ── 접속 복구 (DESIGN 4.4 「접속 복구」) ─────────────────────────────────────────
	/**
	 * 로그인·보유 상태 조회가 실패해 서버와 끊긴 상태인가. 화면이 「다시 시도」 단추를 펼지 이걸로 본다.
	 * Title ID 가 없는 개발 실행도 참이다 — 그때는 다시 시도해도 같은 자리에서 실패한다.
	 */
	bool HasConnectionProblem() const
	{
		return LoginState == ESnareBackendLoginState::Failed || FetchState == ESnareBackendFetchState::Failed;
	}

	/**
	 * 「다시 시도」. 기다리던 자동 재시도를 지우고 지금 바로 다시 붙는다 — 로그인이 안 됐으면 로그인부터,
	 * 로그인은 됐는데 보유 상태 조회가 실패했으면 조회만. 화면 단추는 전부 이것 하나를 부른다.
	 */
	void RetryNow();

	/** 접속 상태가 바뀌었을 때(로그인 성공·실패·조회 실패·재시도 시작). 화면이 `AddUObject` 로 듣고 단추를 켜고 끈다. */
	FSimpleMulticastDelegate OnConnectionChanged;

	/** 실패를 연달아 몇 번째 겪었을 때 몇 초 뒤 다시 붙나. 2·4·8·…·60초 (DESIGN 4.4 「접속 복구」 표). */
	static float GetRetryDelaySeconds(int32 FailureCount);

	/** 지금 잡혀 있는 자동 재시도의 간격(초). 잡혀 있지 않으면 0 — 검사용. */
	float GetPendingRetryDelay() const { return PendingRetryDelay; }

	/** 앱이 앞으로 돌아왔을 때 — 오래된 세션이면 다시 로그인, 아니면 보유 상태만 다시 받는다. */
	void HandleEnteredForeground();

	/**
	 * 2층 검사용 가짜 로그인. 설정돼 있으면 `StartLogin` 이 PlayFab 대신 이것을 부른다 — 빈 문자열이면 성공,
	 * 아니면 그 글이 실패 사유다. 가짜 로그인은 분석 이벤트·보유 상태 조회를 띄우지 않는다. 검사가 끝나면 비운다.
	 */
	static TFunction<FString()> TestLoginOverride;

	/** 로그인에 성공했을 때만 채워진다. 이 사람의 PlayFab 계정 ID. */
	const FString& GetPlayFabId() const { return PlayFabId; }

	/**
	 * 시작하자마자 알아서 로그인할 상황인가.
	 * 참인 자리는 둘이다 — **실제 게임 실행(`-game`·패키지)** 과 **에디터 Play(PIE)**.
	 * PIE 를 켠 것은 사람이 게임을 보는 자리가 거기라서다(감독 지시 2026-09-24 00:05 ①) — 끄고 두면
	 * 백엔드가 주는 것(시작 장비·인벤토리)이 에디터에서만 통째로 안 보인다. 자동화 검사·커맨드릿·
	 * 띄워만 둔 에디터에서는 거짓 — 검사 한 번 돌릴 때마다 PlayFab 에 요청을 띄우지 않기 위해서다.
	 * @param GameInstance 이 서브시스템이 사는 게임 인스턴스 (PIE 인지 여기 월드로 본다, 없으면 PIE 가 아니다)
	 */
	static bool ShouldAutoLogin(const UGameInstance* GameInstance);

	/** 지금 프로세스·이 게임 인스턴스의 상황을 모은다. 전역을 읽는 자리는 여기 하나뿐이다. */
	static FSnareAutoLoginContext MakeAutoLoginContext(const UGameInstance* GameInstance);

	/**
	 * 월드 컨텍스트 하나를 받아 상황을 모은다 — PIE 인지 "에디터니까" 가 아니라 **월드 종류**로 본다.
	 * 2층이 PIE 컨텍스트를 만들어 넣을 수 있게 따로 뺐다 (`MakeAutoLoginContext` 의 몸통이다).
	 */
	static FSnareAutoLoginContext MakeAutoLoginContextFor(const FWorldContext* WorldContext);

	/** 상황 하나를 받아 붙을지 정한다. **전역을 안 읽는다** — 2층이 상황을 만들어 넣고 답을 본다. */
	static bool ShouldAutoLoginFor(const FSnareAutoLoginContext& Context);

	/**
	 * `Config/PlayFab.ini` 에서 Title ID 를 읽는다. 없으면 빈 문자열.
	 * 이 파일은 gitignore 된다(.gitignore 102행) — 저장소를 갓 받은 사람에게는 없는 게 정상이다.
	 */
	static FString ReadTitleIdFromConfig();

	/**
	 * 이 기기를 가리키는 게스트 ID. **같은 기기에서 늘 같은 값**이어야 계정이 이어진다.
	 * 앱을 지웠다 깔아도 같은 값이라 게스트 진행이 살아남는다.
	 */
	static FString MakeDeviceId();

	/**
	 * 이 게임 인스턴스가 쓸 게스트 ID. 실제 게임에서는 `MakeDeviceId()` 와 **글자 하나 안 다르고**,
	 * PIE 에서만 인스턴스 번호가 뒤에 붙는다 — PIE 창을 여럿 띄우면 기기 값이 같아 한 계정에 여럿이
	 * 붙어 버린다(감독 지시 2026-09-24 00:05 ①). 덤으로 PIE 계정이 패키지 계정과 갈린다:
	 * 에디터에서 "첫 접속 1회" 를 써 버려 실기기에서 시작 장비를 못 받는 일이 없다.
	 */
	static FString MakeDeviceIdForInstance(const FSnareAutoLoginContext& Context);

	/**
	 * 이 플랫폼에서 부를 PlayFab 로그인 API 이름. 로그와 2층 검사가 같은 말을 쓰게 하려고 따로 뺐다.
	 * iOS/Android 는 DESIGN 4.4 가 지정한 디바이스 ID 로그인, 그 밖(맥·윈도 — 개발·검사 기기)은 CustomID.
	 */
	static FString GetDeviceLoginApiName();

	// ── M2-05: 인벤토리·코인 (DESIGN 4.5) ─────────────────────────────────────────────
	// 길은 `UInventoryComponent`·`URanchComponent` 와 같은 모양이다 — 밖에서 값을 더하는 함수는 없고,
	// 캐시를 바꾸는 것은 `ApplyPlayerSnapshot` 하나뿐이다.

	/**
	 * 지금 보여 줄 보유 상태. 로그인 전에는 로컬 캐시에서 올라온 지난번 값이고,
	 * 백엔드 응답이 오면 그것으로 통째로 바뀐다 (충돌 시 항상 백엔드 우선 — DESIGN 4.5).
	 */
	const FSnarePlayerSnapshot& GetPlayerSnapshot() const { return PlayerSnapshot; }

	/** 지금 들고 있는 스냅샷이 **이번 실행에서 백엔드가 준 것**인가. 거짓이면 로컬 캐시(표시 전용)다. */
	bool IsSnapshotFromBackend() const { return FetchState == ESnareBackendFetchState::Fetched; }

	ESnareBackendFetchState GetFetchState() const { return FetchState; }

	/**
	 * 캐시를 이 스냅샷으로 **통째로** 바꾼다. 칸을 하나씩 고치는 길은 일부러 두지 않았다 —
	 * 부분 갱신을 허용하면 클라이언트가 자기 인벤토리를 조금씩 만들어 낼 수 있다 (CLAUDE.md 8).
	 * 주인(`PlayFabId`) 없는 스냅샷은 거절한다.
	 * @param bFromBackend 백엔드 응답이면 true, 로컬 캐시를 올린 것이면 false
	 * @return 실제로 캐시를 바꿨으면 true
	 */
	bool ApplyPlayerSnapshot(const FSnarePlayerSnapshot& NewSnapshot, bool bFromBackend);

#if !UE_BUILD_SHIPPING
	/**
	 * 개발용 오프라인 지급 (`Snare.Give`). **출시 구성에는 이 함수가 컴파일되지 않는다.**
	 * 백엔드에 로그인하지 못한 에디터·개발 빌드에서 배낭이 텅 비어 도구·무기를 못 들어 보는 것을 푸는 길이다.
	 * 이번 실행에서 백엔드가 스냅샷을 준 적이 있으면(`IsSnapshotFromBackend`) **거절한다** — 재화·인벤토리 판정은
	 * 백엔드 몫이다 (CLAUDE.md 8). 캐시 파일에도 쓰지 않아 다음 실행에 남지 않는다.
	 * @return 칸에 넣었으면 true
	 */
	bool DevGrantOffline(FName ItemID, int32 Count);
#endif

	/** 백엔드에 보유 상태를 물어본다. 로그인 전에는 아무것도 안 한다. */
	void FetchPlayerState();

	/** 지금 캐시를 로컬 파일에 적는다. 백엔드 응답이 왔을 때만 부른다 — 캐시는 백엔드의 사본이다. */
	bool SaveLocalCache() const;

	/**
	 * 로컬 캐시 파일을 올린다. 형식·주인이 안 맞으면 버린다(`USnarePlayerCacheSave::TryReadSnapshot`).
	 * @return 올렸으면 true
	 */
	bool LoadLocalCache();

	/** 스네어 코인의 PlayFab 가상 재화 코드. BACKEND.md 데이터 모델 표의 `SC`. */
	static FString GetCoinCurrencyCode();

	/** 젬의 PlayFab 가상 재화 코드. BACKEND.md 데이터 모델 표의 `GM`. */
	static FString GetGemCurrencyCode();

	/** 보유 상태를 물을 때 부르는 PlayFab API 이름. 로그와 2층 검사가 같은 말을 쓰게 따로 뺐다. */
	static FString GetPlayerStateApiName();

	// ── M8-02: 분석 이벤트 통로 (docs/ANALYTICS.md) ─────────────────────────────────
	// **나가는 길은 이 함수 하나뿐이다.** 다른 곳에서 `WritePlayerEvent` 를 부르면 2층
	// `Snarehaven.Analytics.Channel.SinglePath` 가 운다 — 길이 둘이 되면 "무엇을 보내는가" 를
	// 한 곳에서 볼 수 없게 되고, 이름표(`DT_AnalyticsEventTable`) 대조도 새는 길이 생긴다.

	/**
	 * 분석 이벤트 하나를 PlayFab 에 보낸다.
	 * `EventID` 는 `DT_AnalyticsEventTable` 에 있는 이름이어야 한다 — 표에 없는 이름을 보내면 2층이 운다.
	 * **로그인 전이거나 Title ID 가 없으면 조용히 버린다** — 큐에 쌓지 않는다. 분석은 지급이 아니라 관찰이라
	 * 놓친 한 건보다 나중에 몰아 보내 시각이 어긋난 한 건이 더 나쁘다 (docs/ANALYTICS.md 표 B).
	 * **이 함수는 아무 값도 바꾸지 않는다** — 스냅샷·인벤토리·잔액을 건드리는 줄이 0 이다 (CLAUDE.md 8).
	 * @param Payload 칸 이름 → 값. 값은 전부 문자열로 실린다 (칸 이름의 원본은 표의 `PayloadKeys`)
	 * @return 실제로 요청을 띄웠으면 true (버렸으면 false)
	 */
	bool WriteGameEvent(FName EventID, const TMap<FName, FString>& Payload = TMap<FName, FString>());

	// ── M9-07b: 시작 장비 왕복 (DESIGN 3.5 「시작 장비 지급」 표) ──────────────────────
	// **"처음인가" 를 여기서 재지 않는다.** 받았는지는 백엔드 플레이어 데이터 표시가 알고, 개수도
	// 백엔드 인벤토리가 말한다 — 이 클래스는 물어보고 받은 답을 옮길 뿐이다 (CLAUDE.md 8 · DESIGN 4.1).

	/** 지급 판정을 맡은 CloudScript 함수 이름. 로그·2층 검사가 같은 말을 쓰게 따로 뺐다. */
	static FString GetStarterKitFunctionName();

	/**
	 * 시작 장비를 달라고 **한 번** 묻는다. 보유 상태 조회가 성공한 뒤 자동으로 불린다 — 먼저 묻는
	 * 순서로 두면 지급 전 인벤토리를 받아 캐시가 한 박자 낡는다.
	 * 이미 물었으면(상태가 `NotStarted` 가 아니면) 아무것도 안 한다. 두 번 불러도 백엔드가 막지만
	 * 왕복을 두 번 띄울 이유가 없다.
	 */
	void RequestStarterKit();

	// ── 8.9~8.14 판정 함수 공통 왕복 (감독 2026-09-24) ──────────────────────────────
	/** 응답 한 덩이. 실패면 bOk=false 이고 Error 에 사유(스크립트 오류·HTTP 오류·JSON 을 못 읽음). */
	using FServerCallDone = TFunction<void(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)>;

	/**
	 * `tools/playfab/cloudscript_main.js` 의 `handlers.<FunctionName>` 을 부른다. 제작·건축·배낭·낚시·상점·배
	 * 기능은 **이것 하나로만** 서버에 묻는다 — 기능마다 이 파일에 호출을 늘리지 않는다.
	 * 받은 뒤 보유 상태가 바뀌었으면 부른 쪽이 `FetchPlayerState()` 로 다시 받는다(백엔드 판정 → 캐시, CLAUDE.md 8).
	 */
	void CallServerFunction(const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, FServerCallDone OnDone);

	/**
	 * 2층 검사용 가짜 서버. 설정돼 있으면 `CallServerFunction` 이 네트워크 대신 이것을 부른다 —
	 * 받는 쪽은 진짜 응답과 **같은 길**을 탄다. 검사가 끝나면 비운다.
	 */
	static TFunction<void(const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, FServerCallDone OnDone)> TestServerOverride;

	/**
	 * 서버 `GetMounts`·`ExchangeCollar`·`AttachSaddle` 응답(`mounts`·`saddles`·`species`)을 스냅샷에 옮긴다 (DESIGN 8.3).
	 * 탈것 칸을 바꾸는 자리는 여기 하나다 — 클라이언트가 종을 더하지 않는다(CLAUDE.md 8).
	 * @return `mounts` 배열이 있어 반영했으면 true
	 */
	bool ApplyMountsReply(const TSharedPtr<FJsonObject>& Response);

	/**
	 * 서버 `GetCodexRewards` 의 `entries`(찬 칸 목록)를 물어 스냅샷 `CodexEntries` 에 **그대로** 옮긴다 (M13-26, DESIGN 4.8).
	 * 도감은 서버 Internal Data 라 인벤토리 응답에 없다 — 보유 상태를 받을 때마다 여기로 다시 받는다.
	 * 도감 칸을 바꾸는 자리는 여기 하나다(클라이언트가 세어 채우지 않는다, CLAUDE.md 8).
	 * 같은 답의 `achievements`({counters, done})도 스냅샷 `Achievements` 로 그대로 옮긴다 (M13-28 — 왕복을 늘리지 않으려고 한 호출).
	 */
	void RefreshCodexEntries();

	/**
	 * `GetCodexRewards` 답 하나를 스냅샷에 옮기고, 지난 답과의 차이로 달성 알림을 낸다 (M48-03 — 2층 검사가 답을 주입하는 자리).
	 * 계정마다 첫 답은 기준만 잡는다(카드 0 · 소리 0). 알림은 `OnAchievementNotice` + 소리 한 번 —
	 * 듣는 HUD 가 없으면(로비) 로그만 남기고 버린다.
	 * @return `entries` 배열이 있어 받아들였으면 true
	 */
	bool ApplyCodexReply(const TSharedPtr<FJsonObject>& Response);

	/** 달성 알림 소리를 낸 횟수·마지막 소리·마지막 답의 카드 수 — 검사용. */
	int32 GetNoticeSoundCount() const { return NoticeSoundCount; }
	FName GetLastNoticeCue() const { return LastNoticeCue; }
	int32 GetLastNoticeCardCount() const { return LastNoticeCardCount; }

	/** 달성 알림 카드. 네이티브 델리게이트 — HUD 가 `AddUObject` 로 듣는다. */
	FOnSnareAchievementNotice OnAchievementNotice;

	/** 목줄로 바꿀 수 있는 종(서버 `MOUNT_SPECIES`). `GetMounts` 응답이 오기 전엔 비어 있다. */
	const TArray<FName>& GetMountSpeciesChoices() const { return MountSpeciesChoices; }

	ESnareBackendStarterKitState GetStarterKitState() const { return StarterKitState; }

	/** 이번 왕복에서 **방금 받은** 아이템 ID 들(`item.club`). 이미 받은 계정은 비어 있다. */
	const TArray<FName>& GetStarterKitGrantedItems() const { return StarterKitGrantedItems; }

	/**
	 * 받은 것을 아직 손에 걸지 않았는가 — 지급 응답은 왔지만 **보유 상태가 백엔드에서 다시 오기 전**이면 참.
	 * 장착 신호는 그 응답이 캐시에 들어간 뒤에야 나간다 (DESIGN 3.5 「받은 뒤」).
	 */
	bool IsStarterKitEquipPending() const { return bStarterKitEquipPending; }

	/**
	 * CloudScript `GrantStarterKit` 이 돌려준 JSON 한 덩이를 반영한다.
	 * **실제 왕복도 검사 주입도 이 자리 하나로 들어온다** — 검사용 사본을 따로 두면 사본만 맞고
	 * 진짜 응답을 읽는 길은 한 번도 안 돌아 본 채로 남는다 (루프#168 의 모의 검사와 같은 판단).
	 * `granted` 가 참이면 보유 상태를 **백엔드에 다시 물어** 캐시를 채운 뒤 `OnStarterKitGranted` 를 쏜다 —
	 * 응답의 개수를 캐시에 더하지 않는다 (CLAUDE.md 8).
	 * @return 형식이 맞아 반영했으면 true
	 */
	bool ApplyStarterKitResponseJson(const FString& ResponseJson);

	// ── 계정 삭제 (DESIGN 4.4 「계정 삭제」 · BACKEND.md 「계정 삭제」) ──────────────────
	/** 끝났을 때. bDeleted 가 참일 때만 캐시를 지우고 로그아웃한 뒤다. 거짓이면 아무것도 안 바뀌었다. */
	using FDeleteAccountDone = TFunction<void(bool bDeleted, const FString& Error)>;

	/** 삭제를 맡은 CloudScript 함수 이름. 로그·2층 검사가 같은 말을 쓰게 따로 뺐다. */
	static FString GetDeleteAccountFunctionName();

	/**
	 * 서버에 계정 삭제를 요청한다. **서버가 `deleted:true` 로 답한 뒤에만** 기기 ID 연결을 끊고 로컬 캐시
	 * 파일을 지우고 로그아웃 상태로 돌린다 (CLAUDE.md 8 — 백엔드 응답 전에 아무것도 바꾸지 않는다).
	 * 실패면 캐시·로그인 상태를 그대로 둔다. 이미 진행 중이면 바로 실패로 답한다.
	 */
	void DeleteAccount(FDeleteAccountDone OnDone);

	bool IsDeletingAccount() const { return bDeletingAccount; }

	// ── 닉네임 (DESIGN 2.2 「이름표」, INBOX 지시 10-06 13:40) ──────────────────────────────
	/** 끝났을 때. 받아들였으면 bAccepted=true, 아니면 Code 가 거절 코드(`bad_length`·`network`·`display_name` …), Message 가 알약에 띄울 문구. */
	using FNicknameDone = TFunction<void(bool bAccepted, const FString& Code, const FString& Message)>;

	static FString GetSetNicknameFunctionName();

	/** PlayFab 이 확인해 준 표시 이름. 로그인 응답·`SubmitNickname` 성공에서만 바뀐다. 아직 없으면 빈 문자열. */
	const FString& GetDisplayName() const { return DisplayName; }

	/** 로그인했는데 표시 이름이 없다 — 닉네임 입력 창을 띄워야 한다. */
	bool NeedsNickname() const { return LoginState == ESnareBackendLoginState::LoggedIn && DisplayName.IsEmpty(); }

	bool IsSubmittingNickname() const { return bSubmittingNickname; }

	/** 2층 검사용 — 로그인 왕복 없이 「로그인함 + 이 표시 이름」 으로 둔다 (로비 입력 줄이 막는지 보려고). */
	void SetLoggedInForTest(const FString& InDisplayName)
	{
		LoginState = ESnareBackendLoginState::LoggedIn;
		DisplayName = InDisplayName;
	}

	/**
	 * 닉네임을 정한다. 앞뒤 공백을 자르고 `SnareNickname::ProblemCode` 로 미리 막은 뒤 서버 `SetNickname` 에 묻고,
	 * **서버가 받아 준 이름 그대로** PlayFab 표시 이름(`UpdateUserTitleDisplayName`)에 올린다. 둘 다 받아들여진 뒤에만
	 * `DisplayName` 을 바꾸고 `OnDisplayNameChanged` 를 쏜다 (CLAUDE.md 8). 겹치는 이름은 PlayFab 거절 문구를 그대로 돌려준다.
	 * 이미 진행 중이면 바로 `busy` 로 답한다.
	 */
	void SubmitNickname(const FString& RawName, FNicknameDone OnDone);

	/** 2층 검사용 가짜 표시 이름 API. 설정돼 있으면 PlayFab 대신 이것을 부른다 — 성공이면 Error 가 빈 문자열. */
	static TFunction<void(const FString& Name, TFunction<void(const FString& AcceptedName, const FString& Error)> OnDone)> TestDisplayNameOverride;

	UPROPERTY(BlueprintAssignable, Category = "Backend")
	FOnPlayerSnapshotChangedSignature OnDisplayNameChanged;

	UPROPERTY(BlueprintAssignable, Category = "Backend")
	FOnPlayerSnapshotChangedSignature OnPlayerSnapshotChanged;

	UPROPERTY(BlueprintAssignable, Category = "Backend")
	FOnStarterKitGrantedSignature OnStarterKitGranted;

private:
	/** PlayFab 응답이 돌아왔을 때. PlayFab 타입을 헤더로 끌고 오지 않으려고 문자열만 받는다. */
	void FinishLoginSucceeded(const FString& InPlayFabId, bool bNewlyCreated, const FString& InDisplayName = FString());
	void FinishLoginFailed(const FString& ErrorReport);

	/** 보유 상태 응답이 왔을 때. 로그인과 같은 이유로 PlayFab 타입을 헤더에 들이지 않는다. */
	void FinishFetchSucceeded(const FSnarePlayerSnapshot& NewSnapshot);
	/** @param bAuthExpired 세션이 만료돼 거절됐으면 true — 조회를 다시 하지 않고 다시 로그인부터 한다 */
	void FinishFetchFailed(const FString& ErrorReport, bool bAuthExpired = false);

	/** `CallServerFunction` 의 몸통. bResent 면 이미 한 번 다시 보낸 요청이라 만료여도 또 보내지 않는다. */
	void SendServerCall(const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, FServerCallDone OnDone,
		bool bResent);

	/** 로그인 상태를 버리고 다시 로그인한다(세션 만료·오래된 세션). 이미 하는 중이면 아무것도 안 한다. */
	void Relogin();

	/** 실패 횟수를 하나 올리고 그 간격 뒤 `RetryNow` 를 잡는다. */
	void ScheduleRetry();
	void CancelRetry();

	/** 다시 로그인을 기다리는 요청들. 로그인이 끝나면 성공 여부를 넘겨 한꺼번에 부른다. */
	TArray<TFunction<void(bool bLoggedIn)>> LoginWaiters;
	void FlushLoginWaiters(bool bLoggedIn);

	FTSTicker::FDelegateHandle RetryHandle;
	FDelegateHandle ForegroundHandle;
	int32 ConsecutiveFailures = 0;
	float PendingRetryDelay = 0.f;

	/** 마지막으로 로그인에 성공한 때(UTC). 앞으로 돌아왔을 때 세션이 오래됐는지 본다. */
	FDateTime LoggedInAtUtc;

	/**
	 * 재화 하나가 **실제로 바뀌었을 때만** `currency_balance_changed` 를 보낸다 (M8-05a).
	 * **값을 인자로 받는 이유:** 이 함수가 스냅샷을 직접 들여다보면 "이벤트가 잔액을 읽는 자리" 가 하나
	 * 더 생긴다. 백엔드가 준 수를 그대로 옮기는 관찰자로 두려고 캐시를 건드리는 낱말을 몸통에서 뺐다
	 * (`docs/ANALYTICS.md` 표 A 의 보내는 쪽 = `Observer` · CLAUDE.md 8).
	 * @param bFromBackend 백엔드 응답으로 바뀐 것이면 true, 로컬 캐시를 올려 바뀐 것이면 false
	 */
	void WriteBalanceChangedEvent(const FString& CurrencyCode, int32 PreviousAmount, int32 NewAmount,
		bool bFromBackend);

	ESnareBackendLoginState LoginState = ESnareBackendLoginState::NotStarted;

	ESnareBackendFetchState FetchState = ESnareBackendFetchState::NotStarted;

	FString PlayFabId;

	/** 백엔드가 확인해 준 보유 상태의 사본. 원본은 PlayFab 에 있다 (DESIGN 4.5). */
	FSnarePlayerSnapshot PlayerSnapshot;

	/** 지급 왕복이 실패했을 때. 오류는 로그로만 남는다 — 시작 장비가 없어도 게임은 돈다. */
	void FinishStarterKitFailed(const FString& ErrorReport);

	ESnareBackendStarterKitState StarterKitState = ESnareBackendStarterKitState::NotStarted;

	/** 방금 받은 아이템 ID 들. **개수는 안 들고 있다** — 몇 개인지는 백엔드 인벤토리가 말한다. */
	TArray<FName> StarterKitGrantedItems;

	/** 받았는데 아직 캐시가 안 돌아왔다 — 보유 상태가 백엔드에서 오는 순간 장착 신호로 바뀐다. */
	bool bStarterKitEquipPending = false;

	/** 차이를 카드 글로 바꿔 `OnAchievementNotice` + 소리 한 번. 빈 차이면 아무것도 안 한다. */
	void AnnounceAchievementNotice(const SnareAchievementNotice::FDiff& Diff);

	/** 이 계정의 도감·업적 첫 답을 받았나 — 첫 답은 알림 기준만 잡는다 (DESIGN 8.15 「첫 조회 무음」). */
	bool bCodexBaselineSet = false;
	FString CodexBaselineOwner;

	int32 NoticeSoundCount = 0;
	FName LastNoticeCue;
	int32 LastNoticeCardCount = 0;

	/** 로그인 뒤 보유 상태를 처음 받은 때 한 번 `GetMounts` 를 묻는다. */
	bool bMountsAsked = false;

	/** 로그인 뒤 한 번 `GrantTesterKit` 을 묻는다(출시 빌드도 — 서버가 테스터 목록으로 판정). */
	bool bTesterKitAsked = false;

	TArray<FName> MountSpeciesChoices;

	/**
	 * 이 기기 ID 와 계정의 연결을 끊고 Next 를 부른다(성공·실패 모두). 그래야 다음 게스트 로그인이 지운 계정에
	 * 다시 붙지 않는다. 로그인 상태가 아니면 바로 Next 다.
	 */
	void UnlinkDeviceThen(TFunction<void()> Next);

	/** 서버가 지웠다고 답한 뒤 — 캐시 파일 삭제·로그아웃·상태 초기화. */
	void FinishAccountDeleted();

	bool bDeletingAccount = false;

	/** 표시 이름 올리기 — 가짜가 있으면 가짜로. 성공이면 Error 빈 문자열. */
	void UpdateDisplayName(const FString& Name, TFunction<void(const FString& AcceptedName, const FString& Error)> OnDone);

	FString DisplayName;

	bool bSubmittingNickname = false;
};
