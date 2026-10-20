#include "Inventory/InventoryComponent.h"
#include "Snarehaven.h"

UInventoryComponent::UInventoryComponent()
{
	// 인벤토리는 이벤트로만 바뀐다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryComponentTick.bCanEverTick = false;
}

bool UInventoryComponent::ApplyBackendSnapshot(const TArray<FInventorySlot>& NewSlots)
{
	if (SlotCount <= 0)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("인벤토리 칸 수가 미정이라 스냅샷을 반영하지 않는다"));
		return false;
	}

	// 줄 수를 칸 한도와 견주지 않는다 — 스냅샷은 아이템 종류마다 한 줄이라 줄 수 ≠ 가방 칸 수다(펫·가챠는 칸을 안 쓰고,
	// 99개마다 한 칸, 등산 짐가방 +8). 칸 한도는 서버 `capacityFor`·`slotsFor` 가 판정한다 (CLAUDE.md 8, M28-09f —
	// 펫 3 + 가방 20 줄 계정의 23 줄 스냅샷을 통째로 버려 목장 「넣기」 펫 목록·도구 들기가 낡은 캐시를 봤다).
	for (const FInventorySlot& Slot : NewSlots)
	{
		if (Slot.ItemID.IsNone() || Slot.Count <= 0)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("인벤토리 스냅샷에 빈 칸(%s x %d)이 있다 — 반영하지 않는다"),
				*Slot.ItemID.ToString(), Slot.Count);
			return false;
		}
	}

	Slots = NewSlots;
	OnInventoryCacheChanged.Broadcast();
	return true;
}

bool UInventoryComponent::QueuePendingGrant(FName ItemID, int32 Count, int32 ToolUses)
{
	if (ItemID.IsNone() || Count <= 0)
	{
		return false;
	}
	ToolUses = FMath::Clamp(ToolUses, 0, Count);

	// 같은 아이템은 한 줄로 합친다 — 돼지 한 마리에서 고기 3개가 나오면 요청도 한 줄이다.
	for (FInventorySlot& Pending : PendingGrants)
	{
		if (Pending.ItemID == ItemID)
		{
			Pending.Count += Count;
			Pending.ToolUses += ToolUses;
			OnPendingGrantQueued.Broadcast(ItemID, Count);
			return true;
		}
	}

	FInventorySlot NewPending;
	NewPending.ItemID = ItemID;
	NewPending.Count = Count;
	NewPending.ToolUses = ToolUses;
	PendingGrants.Add(NewPending);

	OnPendingGrantQueued.Broadcast(ItemID, Count);
	return true;
}

void UInventoryComponent::ClearPendingGrants()
{
	PendingGrants.Reset();
}

int32 UInventoryComponent::RemovePendingGrant(FName ItemID, int32 Count)
{
	const int32 Index = PendingGrants.IndexOfByPredicate(
		[ItemID](const FInventorySlot& Pending) { return Pending.ItemID == ItemID; });
	if (Index == INDEX_NONE || Count <= 0)
	{
		return 0;
	}

	const int32 Removed = FMath::Min(Count, PendingGrants[Index].Count);
	PendingGrants[Index].Count -= Removed;
	// 보낸 줄의 toolUses 는 min(ToolUses, 보낸 개수) 였다 — 그만큼 같이 뺀다 (ULootClaimComponent).
	PendingGrants[Index].ToolUses = FMath::Max(0, PendingGrants[Index].ToolUses - Removed);
	if (PendingGrants[Index].Count <= 0)
	{
		PendingGrants.RemoveAt(Index);
	}
	return Removed;
}

int32 UInventoryComponent::GetItemCount(FName ItemID) const
{
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.ItemID == ItemID)
		{
			return Slot.Count;
		}
	}

	return 0;
}

int32 UInventoryComponent::GetPendingCount(FName ItemID) const
{
	for (const FInventorySlot& Pending : PendingGrants)
	{
		if (Pending.ItemID == ItemID)
		{
			return Pending.Count;
		}
	}

	return 0;
}
