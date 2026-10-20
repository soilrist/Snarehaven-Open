#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "LootClaimComponent.generated.h"

class UInventoryComponent;
class USnareBackendSubsystem;

/** 서버가 받아 준 묶음의 획득 글(`+5 나무 · +2 산딸기`) — 사냥 HUD 알림 카드가 듣는다 (DESIGN 3.3 「획득 팝업」). */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLootGrantPopup, const FText& /*Popup*/);

// 지급 대기열(`UInventoryComponent::PendingGrants`)을 서버 판정 함수 `ClaimLoot` 로 비운다 (DESIGN 8.12 "지급 대기열 보내기").
//
// 사냥 처치·채집·포획은 호스트가 판정해 **잡은 사람 폰의 대기열**에만 쌓인다. 사냥은 아이템이 아니라 `animal.<종>` 줄
// (한 마리)로 쌓여 `kills` 로 나가고, 드롭은 서버가 그 종의 표로 굴린다 (지시 감독 10-20 19:30 ①). 이 컴포넌트가 주기마다 한 묶음을
// 서버에 보내고, 서버가 받아 준 뒤에야 대기열에서 빼고 보유 상태를 다시 받는다. **보유 수량은 여기서 안 바뀐다** —
// 늘어난 개수는 `FetchPlayerState` 가 돌려준 스냅샷으로만 들어온다 (CLAUDE.md 8 · DESIGN 4.5).
// 실패(`too_soon`·`inventory_full`·네트워크)는 대기열을 그대로 두고 다음 주기에 다시 보낸다.
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API ULootClaimComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULootClaimComponent();

	/** 부를 서버 함수 이름. 로그·2층 검사가 같은 말을 쓰게 따로 뺐다. */
	static FString GetClaimFunctionName();

	/**
	 * `items` 로 보낼 수 있는 아이템인가 — 펫·채집물(서버 `GATHER_ITEMS`)뿐. 사냥 드롭(고기·가죽·전리품)은 더는 아이템으로
	 * 안 보낸다 — 처치는 `animal.<종>` 줄로 쌓여 `kills` 로 나간다 (`IsKillLine`, 지시 ①).
	 * 목록 밖 하나라도 섞이면 서버가 **묶음 통째로** 거절하므로 애초에 안 싣는다.
	 */
	static bool IsClaimableLoot(FName ItemID);

	/** 대기열의 사냥 처치 줄(`animal.<종>`, 한 개 = 한 마리)인가 — `kills:[{animalId, count}]` 로 나간다. */
	static bool IsKillLine(FName LineID);

	/** 검사·폰 연결용. 비워 두면 BeginPlay 가 주인 액터·게임 인스턴스에서 찾는다. */
	void BindTargets(UInventoryComponent* InInventory, USnareBackendSubsystem* InBackend);

	/**
	 * 한 묶음을 보낸다. 이미 보내는 중이거나 보낼 것이 없으면 아무것도 안 한다.
	 * @return 서버에 물었으면 true
	 */
	bool TryClaimNow();

	/** 성공 응답 뒤 보유 상태를 다시 물은 횟수 — 2층 검사가 "성공하면 다시 받는다" 를 본다. */
	int32 GetRefetchCount() const { return RefetchCount; }

	/** 서버에 묻는 중인가. */
	bool IsClaimInFlight() const { return bClaimInFlight; }

	/**
	 * 획득 글 한 줄 — `+N 이름` 을 ` · ` 로 잇고, 넘치는 종류는 `외 N종` 으로 접는다. 0개 줄은 뺀다.
	 * @param Lines 이름·개수 (서버 `granted` 순서 그대로)
	 * @return 보일 것이 없으면 빈 글
	 */
	static FText FormatGrantPopup(const TArray<TPair<FText, int32>>& Lines);

	/** 한 글에 이름을 적는 최대 종류 수. DESIGN 3.3 「획득 팝업」. */
	static int32 GetGrantPopupMaxKinds();

	/** 마지막으로 받아 준 묶음의 획득 글 — 거절·0개면 빈 글. 2층 검사가 글자를 본다. */
	const FText& GetLastGrantPopup() const { return LastGrantPopup; }

	/** 서버가 받아 준 뒤(획득 글이 있을 때만) 한 번 울린다. */
	FOnLootGrantPopup OnGrantPopup;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 한 번에 싣는 최대 개수(합계). DESIGN 8.12 "지급 대기열 보내기" = 서버 `LOOT_MAX_PER_CALL` 과 같게. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot")
	int32 MaxItemsPerClaim = 20;

	/**
	 * 처치 한 마리를 한 묶음에서 몇 개로 치나 = 서버 `KILL_DROP_MAX`(동물 행 드롭 개수 최대 3, DESIGN 4.8 「사냥 드롭 서버 굴림」).
	 * 서버가 굴리기 전에 마리 × 이 값으로 한도를 보므로 같게 세야 묶음이 `too_many` 로 통째 거절되지 않는다.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot")
	int32 MaxDropsPerKill = 3;

	/** 보내는 주기(초). DESIGN 8.12 "지급 대기열 보내기" = 서버 `LOOT_MIN_GAP_MS` 와 같게. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Loot")
	float ClaimIntervalSeconds = 10.f;

private:
	/** 서버 답 `granted` 그대로 획득 글을 만들고 알린다 — 여기서 수를 셈하지 않는다 (CLAUDE.md 8). */
	void AnnounceGranted(const TSharedPtr<FJsonObject>& Response);

	UPROPERTY()
	TObjectPtr<UInventoryComponent> Inventory;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> Backend;

	UPROPERTY()
	TObjectPtr<class UDataTable> ItemTable;

	/** 목록 밖이라 안 보낸 아이템 — 같은 경고를 주기마다 되풀이하지 않으려고 한 번만 적는다. */
	TSet<FName> WarnedItems;

	FTimerHandle ClaimTimer;
	bool bClaimInFlight = false;
	int32 RefetchCount = 0;
	FText LastGrantPopup;
};
