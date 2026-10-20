#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InventoryComponent.generated.h"

// 인벤토리 한 칸. 같은 아이템은 한 칸에 모인다 (슬롯형 — DESIGN 3.3).
// **이 구조체는 백엔드가 보내 준 상태를 옮겨 담는 그릇이지, 원본이 아니다** (DESIGN 4.5).
USTRUCT(BlueprintType)
struct FInventorySlot
{
	GENERATED_BODY()

	/** `DT_ItemTable` 의 `ItemID` (예: `item.pig_meat`). 빈 칸이면 NAME_None. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	FName ItemID;

	/** 이 칸에 들어 있는 개수. 스택 상한은 백엔드가 정한다 (아직 미정 — INBOX 질문). */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 Count = 0;

	/** 지급 대기열에서만 쓴다 — 이 개수 중 보너스 도구로 얻은 번 수(DESIGN 13.5, ClaimLoot `toolUses`). 스냅샷은 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 ToolUses = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryCacheChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPendingGrantQueuedSignature, FName, ItemID, int32, Count);

// ROADMAP M1-04: 플레이어 인벤토리의 **로컬 캐시 계층**.
//
// 왜 "캐시" 인가 — 인벤토리 원본은 백엔드(PlayFab)다 (DESIGN 4.1 권위 표·4.5 · CLAUDE.md 8).
// 그래서 이 컴포넌트에는 "아이템을 준다" 는 함수가 없다. 두 갈래뿐이다:
//   1) `ApplyBackendSnapshot` — 백엔드가 보내 준 보유 상태로 캐시를 덮어쓴다. 이것만이 보유 수량을 바꾼다
//   2) `QueuePendingGrant`   — "이런 걸 얻었으니 반영해 달라" 는 요청을 대기열에 쌓는다. 보유 수량은 안 늘어난다
// 사냥 드롭(M1-04)·포획(M1-05)·채집(M1-06)은 전부 2번으로만 들어온다. 실제 백엔드 왕복은 M2-01·M2-05 이고,
// 그 응답이 와서 스냅샷을 다시 받기 전까지 UI 에 지급을 보여 주지 않는다 (DESIGN 4.5 — 성공 응답 전엔 반영 안 함).
// 멀티 복제(Replicated)는 개인 데이터라 아예 필요 없다 — 목장·인벤토리는 동기화 대상이 아니다 (DESIGN 3.4).
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInventoryComponent();

	/**
	 * 백엔드에서 받은 보유 상태로 캐시를 통째로 덮어쓴다 (충돌 시 항상 백엔드 우선 — DESIGN 4.5).
	 * 개수가 0 이하이거나 ID 가 빈 칸이 있으면 **하나도 반영하지 않고** 경고만 남긴다(칸 한도는 서버가 판정 — 줄 수 ≠ 칸 수) —
	 * 반쯤 반영된 인벤토리는 "내 물건이 사라졌다" 로 보인다.
	 * @param NewSlots 백엔드가 보내 준 칸 목록
	 * @return 실제로 캐시를 바꿨으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool ApplyBackendSnapshot(const TArray<FInventorySlot>& NewSlots);

	/**
	 * 얻은 아이템을 **지급 요청 대기열**에 쌓는다. 보유 수량은 바뀌지 않는다 (CLAUDE.md 8).
	 * 같은 아이템이면 한 줄에 합친다. 백엔드로 보내는 일은 M2-01·M2-05 조각이다.
	 * @param ItemID DT_ItemTable 의 ItemID — 사냥 처치는 `animal.<종>` 줄(한 개 = 한 마리, `ULootClaimComponent::IsKillLine`)
	 * @param Count  개수 (0 이하면 무시)
	 * @param ToolUses 그중 보너스 도구로 얻은 번 수 (DESIGN 13.5 — 서버가 가방을 보고 번마다 +1). 개수를 넘으면 개수까지.
	 * @return 대기열에 쌓았으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool QueuePendingGrant(FName ItemID, int32 Count, int32 ToolUses = 0);

	/**
	 * 백엔드가 지급을 확정해 새 스냅샷을 준 뒤, 반영이 끝난 대기열을 비운다.
	 * 스냅샷 적용이 자동으로 비우지 않는 이유: 스냅샷이 요청보다 먼저 도착할 수 있고,
	 * 그때 지우면 아직 반영 안 된 획득이 조용히 사라진다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ClearPendingGrants();

	/**
	 * 백엔드가 **받아 준 만큼만** 대기열에서 뺀다 (`ClaimLoot` 성공 응답 뒤 — `ULootClaimComponent`).
	 * 보유 수량은 안 바뀐다 — 그건 이어서 오는 스냅샷 몫이다 (CLAUDE.md 8).
	 * @return 뺀 개수 (대기열에 그만큼 없으면 있는 만큼)
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 RemovePendingGrant(FName ItemID, int32 Count);

	/** 백엔드가 확인해 준 보유 칸들 (캐시). */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	const TArray<FInventorySlot>& GetSlots() const { return Slots; }

	/** 백엔드가 확인해 준 보유 개수. 대기열은 안 센다. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetItemCount(FName ItemID) const;

	/** 아직 백엔드에 반영되지 않은 획득 요청들. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	const TArray<FInventorySlot>& GetPendingGrants() const { return PendingGrants; }

	/** 그 아이템의 대기 중인 획득 개수. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetPendingCount(FName ItemID) const;

	/** 인벤토리 칸 수. DESIGN 3.3 = 20. */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetSlotCount() const { return SlotCount; }

	/** 캐시가 바뀔 때. 인벤토리 UI(M1-08 이후)가 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnInventoryCacheChangedSignature OnInventoryCacheChanged;

	/** 지급 요청이 쌓일 때. 백엔드 전송기(M2-01)가 이걸 들어 큐를 비운다. */
	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnPendingGrantQueuedSignature OnPendingGrantQueued;

protected:
	/** 초기 칸 수. DESIGN 3.3 "슬롯형, 초기 20칸" — 늘리는 건 설계서부터 (DataDriven.md 3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Inventory")
	int32 SlotCount = 20;

private:
	// 보유 수량은 `ApplyBackendSnapshot` 만 바꾼다. 밖에서 직접 쓰면 백엔드 권위가 깨져서
	// 읽기 함수만 열어 둔다 (engines/unreal.md 코드 규칙 1 · CLAUDE.md 8).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TArray<FInventorySlot> Slots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TArray<FInventorySlot> PendingGrants;
};
