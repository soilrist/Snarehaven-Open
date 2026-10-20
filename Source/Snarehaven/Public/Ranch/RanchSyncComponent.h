#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Ranch/RanchComponent.h"
#include "UObject/SoftObjectPtr.h"
#include "RanchSyncComponent.generated.h"

class UDataTable;
class UTexture2D;
class USnareBackendSubsystem;
class USnareBaseSubsystem;
class UWaterTravelComponent;
class UBackpackComponent;
struct FBaseRuleRow;

/** 목장 서버 함수 하나의 답이 왔다. 실패면 `Message` 가 `DT_RanchFailureTable` 문구다 (M12-05 실패 HUD). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnRanchRequestAnsweredSignature, const FString&, FunctionName,
	bool, bAccepted, const FString&, FailureCode, const FText&, Message);

// 목장·낚시터 요청 대기열을 서버로 보내고, 로그인 뒤 거점 저장값을 한 번 받아 되살린다 (BACKEND.md 함수 표).
//
// 보내기: `URanchComponent::OnRanchRequestQueued` 를 들어 한 건씩 `RegisterPet`·`HarvestRanch`
// (낚시터면 `RegisterFishingSpot`·`HarvestFishing`, 작물이면 `PlantCrop`·`HarvestCrop`) 을 부른다. **칸은 여기서 안 바뀐다** — 서버가 받아 준 뒤
// `FetchPlayerState` + `GetBaseState` 로 다시 받은 값으로만 바뀐다 (CLAUDE.md 8). 거절이면 칸은 그대로, 글만 남긴다.
// 되살리기: 스냅샷이 백엔드에서 처음 온 순간 `GetBaseState` 를 한 번 불러 목장·낚시터 칸, 텐트 사본, 배에 적는다.
// 목장 존에 있는 동안은 `GetBaseState` 를 주기적으로 다시 묻는다 (M19-06, 주기는 DefaultGame.ini · DESIGN 4.8).
// `ULootClaimComponent` 와 같은 짜임이다 (잡은 사람 폰에서만 돈다).
UCLASS(Config = Game, ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API URanchSyncComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URanchSyncComponent();

	/**
	 * 거점 종류 × 요청 종류 → 서버 함수 이름. 로그·2층 검사가 같은 말을 쓰게 따로 뺐다.
	 * 수확은 칸 종류로 갈린다 — 작물 칸이면 `HarvestCrop` (⑩).
	 */
	static FString GetFunctionName(ERanchStationKind Station, ERanchRequestKind Kind,
		ERanchSlotKind SlotKind = ERanchSlotKind::AnimalProduct);

	/** 되살리기 조회 함수 이름. */
	static FString GetBaseStateFunctionName();

	/** 검사·폰 연결용. 대기열·스냅샷 알림도 여기서 듣기 시작한다. 없는 대상은 nullptr. */
	void BindTargets(URanchComponent* InRanch, URanchComponent* InFishing, USnareBackendSubsystem* InBackend,
		USnareBaseSubsystem* InBase, UWaterTravelComponent* InWaterTravel, UBackpackComponent* InBackpack = nullptr);

	/**
	 * 대기열 맨 앞 한 건을 보낸다(목장 먼저, 다음 낚시터). 이미 보내는 중이거나 보낼 것이 없으면 아무것도 안 한다.
	 * @return 서버에 물었으면 true
	 */
	bool TrySendNext();

	/** `GetBaseState` 를 부른다. 이미 묻는 중이면 false. */
	bool RequestBaseState();

	bool IsRequestInFlight() const { return bRequestInFlight; }

	/** 목장 존 주기 조회 간격(초). 0 이하면 꺼짐. 값은 `DefaultGame.ini` (DESIGN 4.8 표). */
	static float GetBaseStatePollSeconds();

	/**
	 * 주기 조회를 건다 — 간격마다 목장 존이고 로그인돼 있으면 `GetBaseState` 를 묻는다.
	 * 받은 값은 다른 조회와 같은 `ApplyBaseState` 로만 들어간다 (CLAUDE.md 8). 검사는 자기 `FTSTicker` 를 넘긴다.
	 */
	void StartBaseStatePolling(FTSTicker& Ticker);
	void StopBaseStatePolling();

	/** 성공 응답 뒤 보유 상태를 다시 물은 횟수 — 2층 검사가 "성공하면 다시 받는다" 를 본다. */
	int32 GetRefetchCount() const { return RefetchCount; }

	/** 마지막 거절 사유 (`DT_RanchFailureTable` 문구, 없으면 서버 사유 문장). 성공하면 빈 글로 돌아간다. */
	const FText& GetLastMessage() const { return LastMessage; }

	/**
	 * 라이브 CloudScript 에 그 함수가 아직 없다고 답을 받았나 (`CloudScriptNotFound`). 한 번 받으면 이 세션 동안
	 * 그 요청을 다시 쌓지 않고 "서버 준비 중" 으로 둔다 — 새 함수를 코드보다 늦게 올리는 동안 헛누름을 막는다.
	 */
	bool IsFunctionUnavailable(const FString& FunctionName) const { return UnavailableFunctions.Contains(FunctionName); }

	// --- M12-05 목장 유지·칸 확장 ---

	static FString GetFeedUpkeepFunctionName();
	static FString GetExpandSlotFunctionName();

	/**
	 * `FeedRanchUpkeep {count}` 를 부른다. 유지·명성·칸은 **여기서 안 바뀐다** — 성공 뒤 다시 받은 값으로만 (CLAUDE.md 8).
	 * @return 서버에 물었으면 true (Count<=0 · 이미 묻는 중 · 백엔드 없음이면 false)
	 */
	bool RequestFeedUpkeep(int32 Count);

	/** `ExpandRanchSlot` 을 부른다. 반환 뜻은 `RequestFeedUpkeep` 과 같다. */
	bool RequestExpandSlot();

	/** 목장 데코 적용·해제 함수 이름 (M35-05d). */
	static FString GetSetRanchDecoFunctionName();

	/**
	 * `SetRanchDeco {itemId[, kind]}` 를 부른다 (M35-05d · `docs/specs/gacha-deco.md` 5장 4). `ItemID` 가 NAME_None 이면
	 * 그 `Kind` 칸을 푼다. 데코 캐시·목장 겉모습은 **여기서 안 바뀐다** — 성공 뒤 다시 받은 `GetBaseState.ranchDeco` 로만 (CLAUDE.md 8).
	 * 유지 넣기·칸 확장과 같은 「한 번에 하나」 길을 탄다. 반환 뜻은 `RequestFeedUpkeep` 과 같다.
	 */
	bool RequestSetRanchDeco(FName ItemID, FName Kind);

	/** 목장 칸 캐시 (데코 포함). 묶기 전이면 nullptr. */
	URanchComponent* GetRanch() const { return Ranch; }

	/** 유지 넣기·칸 확장(·데코 적용) 중 하나를 묻는 중인가 — 그동안 HUD 두 버튼이 꺼진다. */
	bool IsUpkeepActionInFlight() const { return bUpkeepActionInFlight; }

	/** 목장 숫자가 채워진 규칙 행. 없거나 미정이면 nullptr. */
	const FBaseRuleRow* GetRanchRules() const;

	/** 가방(백엔드 스냅샷) 속 유지비 아이템 수. 규칙·스냅샷이 없으면 -1. */
	int32 GetUpkeepItemCount() const;

	/**
	 * 다음 확장 비용 (k = 지금 칸 수 - 기본 칸 수 + 1, 명성·코인 = 단계값 × k). 서버 `ExpandRanchSlot` 과 같은 식.
	 * @return 유지 상태를 아직 못 받았거나 규칙이 없거나 이미 최대면 false
	 */
	bool GetNextExpandCost(int32& OutFame, int32& OutCoins) const;

	/** 서버가 준 칸 수가 상한에 닿았나. 모르면 false. */
	bool IsSlotCapAtMax() const;

	/**
	 * 실패 응답 → 화면 문구. `<함수>_<code>` 행 → `<code>` 행 → 서버 사유 문장 순으로 찾고, `need`/`have` 가 오면 꼬리로 붙인다.
	 * 성공 응답이면 빈 글.
	 */
	static FText DescribeRanchFailure(const UDataTable* Table, const FString& FunctionName, bool bOk,
		const TSharedPtr<FJsonObject>& Response, const FString& Error);

	/**
	 * 이미 가른 code·사유 문장 → 화면 문구 (M26-02 — 상자·뽑기·배낭도 이 표를 쓴다).
	 * `<함수>_<code>` 행 → `<code>` 행 → 서버 사유 문장(비면 code) 순, `need`/`have` 가 오면 표 문구 꼬리로 붙인다.
	 * @param bUseCodeRows false 면 `<code>` 행은 `network` 만 본다 — 공통 행 글귀(`locked` 목장이 잠겼다 …)가 목장 것이라
	 */
	static FText DescribeFailureCode(const UDataTable* Table, const FString& FunctionName, const FString& Code,
		const FString& Message, const TSharedPtr<FJsonObject>& Response, bool bUseCodeRows = true);

	/** 실패 문구 표 `DT_RanchFailureTable` 경로 — 목장 밖 판도 생성자에서 같은 표를 찾는다. */
	static const TCHAR* GetFailureTablePath();

	// --- M21-03 수확 순간 ---

	/**
	 * `GetBaseState` 답의 `ranchDeco`(M35-05a, `{ "fence": "gacha.deco_wood_fence", ... }`)를 종류 → 아이템 ID 로 옮긴다.
	 * @return 필드가 있었으면 true — 없는 옛 서버 답이면 false 라 캐시를 건드리지 않는다
	 */
	static bool ParseRanchDeco(const TSharedPtr<FJsonObject>& Response, TMap<FName, FName>& OutDeco);

	/** 수확 함수(`HarvestRanch`·`HarvestCrop`·`HarvestFishing`)인가. */
	static bool IsHarvestFunction(const FString& FunctionName);

	/** 수확 팝업 글 — `+1 송로버섯 / +10 코인`. 코인이 0 이면 앞 절만, 개수가 0 이하면 빈 글. */
	static FText FormatHarvestPopup(const FText& ItemName, int32 Count, int32 Coins);

	/** 수확 카드 코인 줄 (M109-04c) — 서버 답 `coins`·`coinsCapped` 그대로. 상한이면 「오늘 수확 코인은 다 받았어요」(생산물은 제목 줄이 보여 준다 — 카드 폭 300 에 한 줄), 코인 0·상한 아님이면 빈 글. */
	static FText FormatHarvestCoinLine(int32 Coins, bool bCoinsCapped);

	/** 수확이 확정될 때 트는 소리 이름 (`SFX_Coins_*`). */
	static FName GetHarvestSoundCue();

	/** 마지막 답이 서버가 확정한 수확이면 그 팝업 글. 다른 답이 오면 빈 글로 돌아간다 — 서버 응답 뒤에만 찬다 (CLAUDE.md 8). */
	const FText& GetLastHarvestPopup() const { return LastHarvestPopup; }

	/** 알림 카드 제목·설명으로 가른 같은 글 (M23-03) — `+1 송로버섯` / `+10 코인`(코인 0 이면 빈 글). */
	const FText& GetLastHarvestTitle() const { return LastHarvestTitle; }
	const FText& GetLastHarvestCoinLine() const { return LastHarvestCoinLine; }

	/** 마지막 수확 아이템의 아이콘 (`DT_ItemTable` `IconRef`). 표에 없거나 아이콘이 없으면 빈 참조. */
	const TSoftObjectPtr<UTexture2D>& GetLastHarvestIcon() const { return LastHarvestIcon; }

	/** 마지막 답에서 튼 소리 이름(수확·씨앗 심기·목장 소리) — 2층 검사가 "어떤 소리를 틀었나" 를 본다. 안 틀었으면 `NAME_None`. */
	FName GetLastPlayedCue() const { return LastPlayedCue; }

	/** 서버가 받아들인 목장 답에서 튼 목장 소리(`SnareSound::RanchCue`) 수 — 2층 검사가 「받아들임 n → 소리 n · 거절 0」 을 본다. */
	int32 GetRanchCueCount() const { return RanchCueCount; }

	UPROPERTY(BlueprintAssignable, Category = "Ranch")
	FOnRanchRequestAnsweredSignature OnRanchRequestAnswered;

#if !UE_BUILD_SHIPPING
	/** 3층 소리 확인 전용 — 서버가 받아들인 답을 사람 길과 같은 `HandleServerReply` 에 그대로 넣는다 (스위치는 캐릭터 쪽, M50-07). */
	void DebugFeedAcceptedReply(const FString& FunctionName);
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleRequestQueued(int32 SlotIndex);

	UFUNCTION()
	void HandleSnapshotChanged();

	bool SendFrom(URanchComponent* Station);
	bool CallUpkeepAction(const FString& FunctionName, const TSharedPtr<FJsonObject>& Args);
	/** 목장 서버 함수 답 공통 처리 — 실패는 문구·알림만, 성공은 보유·거점 상태 다시 받기. */
	void HandleServerReply(const FString& FunctionName, bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error);
	/** 확정된 수확 답 → 팝업 글·코인 소리. 받은 수가 0 이면 아무것도 안 한다. */
	void CelebrateHarvest(const TSharedPtr<FJsonObject>& Response);
	void ApplyBaseState(const TSharedPtr<FJsonObject>& Response);
	void PollBaseState();
	static void ApplyStation(URanchComponent* Station, const TSharedPtr<FJsonObject>& Response, const TCHAR* Field);

	UPROPERTY()
	TObjectPtr<URanchComponent> Ranch;

	UPROPERTY()
	TObjectPtr<URanchComponent> Fishing;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> Backend;

	UPROPERTY()
	TObjectPtr<USnareBaseSubsystem> Base;

	UPROPERTY()
	TObjectPtr<UWaterTravelComponent> WaterTravel;

	/** `GetBaseState.hat` 을 옮겨 적을 배낭 (#0199 재접속 모자 복원). */
	UPROPERTY()
	TObjectPtr<UBackpackComponent> Backpack;

	/** 실패 code → 문구 (`DT_RanchFailureTable`, 행 구조체 `FRanchFailureRow`). */
	UPROPERTY(EditDefaultsOnly, Category = "Ranch")
	TObjectPtr<UDataTable> FailureMessageTable;

	/** 수확 팝업에 쓸 아이템 이름 (`DT_ItemTable`). 없으면 ID 그대로. */
	UPROPERTY(EditDefaultsOnly, Category = "Ranch")
	TObjectPtr<UDataTable> ItemTable;

	/** 목장 존 `GetBaseState` 주기(초) — CDO 값만 쓴다 (`GetBaseStatePollSeconds`). 0 = 꺼짐. */
	UPROPERTY(Config)
	float BaseStatePollSeconds = 0.f;

	FTSTicker::FDelegateHandle BaseStatePollHandle;
	FText LastMessage;
	FText LastHarvestPopup;
	FText LastHarvestTitle;
	FText LastHarvestCoinLine;
	TSoftObjectPtr<UTexture2D> LastHarvestIcon;
	FName LastPlayedCue;
	int32 RanchCueCount = 0;
	TSet<FString> UnavailableFunctions;
	bool bRequestInFlight = false;
	bool bBaseStateInFlight = false;
	bool bUpkeepActionInFlight = false;
	/** 거점 조회가 도는 중에 또 성공이 왔다 — 끝나면 한 번 더 묻는다(앞 조회는 성공 전 값일 수 있다). */
	bool bBaseStateAgain = false;
	/** 로그인 뒤 되살리기를 이미 불렀나 — 스냅샷 알림은 매 조회마다 오니 한 번만 부른다. */
	bool bRestoreRequested = false;
	int32 RefetchCount = 0;
};
