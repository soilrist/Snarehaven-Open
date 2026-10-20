// ROADMAP M2-05 — 백엔드가 확정해 준 "이 계정이 지금 무엇을 가졌는가" 한 벌.
// 원본은 PlayFab 이고 이 구조체는 그것을 옮겨 담는 그릇이다 (DESIGN 4.5 · CLAUDE.md 8).
// 목장 슬롯(`FRanchSlotState`)은 아직 여기 없다 — 그 값은 `HarvestRanch`/`RegisterPet` CloudScript
// (M2-03, 사람 대기)가 돌려주는 것이라, 그 조각이 열릴 때 같은 구조체에 칸을 더한다.
#pragma once

#include "CoreMinimal.h"
#include "Inventory/InventoryComponent.h"

#include "SnarePlayerSnapshot.generated.h"

/**
 * 업적 진행 (M12-04 · DESIGN 8.15) — 서버 Internal Data `achievements` 를 옮겨 담는 그릇.
 * 달성 여부는 `Completed` 에 있는가 하나다. `Counters` 가 목표를 넘어도 클라이언트가 달성으로 치지 않는다.
 */
USTRUCT(BlueprintType)
struct FSnareAchievementState
{
	GENERATED_BODY()

	/** 서버 카운터 이름 → 현재 값 (`capture` · `hunt` · … · `codexPercent`). 진행 수 표시용. */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	TMap<FName, int32> Counters;

	/** 서버가 달성으로 적은 업적 ID (`ach.*`). 보상은 이미 서버가 줬다. */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	TArray<FName> Completed;
};

/**
 * 백엔드 한 번 왕복의 결과. **로컬에서 만들어 내는 값이 아니다** —
 * 이 구조체를 채우는 자리는 PlayFab 응답 처리(`USnareBackendSubsystem`)와 로컬 캐시 읽기 둘뿐이다.
 */
USTRUCT(BlueprintType)
struct FSnarePlayerSnapshot
{
	GENERATED_BODY()

	/** 백엔드 인벤토리를 우리 칸 모양으로 옮긴 것. 아이템 ID 는 `DT_ItemTable` 의 `ItemID` 와 같은 문자열. */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	TArray<FInventorySlot> InventorySlots;

	/** 스네어 코인 잔액. PlayFab 가상 재화 `SC` (BACKEND.md 데이터 모델 표). */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 CoinBalance = 0;

	/**
	 * 젬 잔액. PlayFab 가상 재화 `GM` (BACKEND.md 데이터 모델 표 - "IAP 단계에서 활성").
	 * IAP 가 서기 전에는 늘 0 이 온다 — 그래도 칸을 비워 두지 않는 이유는, 화면이 코인만 읽다가
	 * 젬이 생기는 날 표시 코드를 다시 짜면 그때 잔액을 만들어 내는 길이 열리기 때문이다 (CLAUDE.md 8).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	int32 GemBalance = 0;

	/**
	 * 도감에 찬 칸들 (M10-05) — 채워진 종·아이템 ID 만 담은 **중복 없는 배열**이다
	 * (`docs/CODEX.md` 표 B · DESIGN 8.5). 사냥 칸은 `animal.*`, 농사·낚시 칸은 `item.*` 로,
	 * 채우는 사건이 다루는 값이 원래 달라 한 종류로 맞추지 않았다(표 A 의 같은 이유).
	 *
	 * **클라이언트가 여기 값을 더하지 않는다.** 칸을 채우는 자리는 CloudScript 성공 판정 셋뿐이고
	 * (`RegisterPet`·`HarvestRanch`·`HarvestFishing` — M10-04, `docs/BACKEND.md` 함수 표),
	 * 이 칸은 그 결과를 받아 오는 그릇이다. 다 모았는지도 **이 배열의 길이**로 판정한다(M10-06) —
	 * 화면이 세어서 "다 찼다" 고 우기면 그것이 곧 탈것을 공짜로 여는 길이다 (DESIGN 4.8 · CLAUDE.md 8).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	TArray<FName> CodexEntries;

	/** 업적 진행 (M12-04). 도감 칸과 같은 약속 — 클라이언트가 채우지 않는다. */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FSnareAchievementState Achievements;

	/**
	 * 가진 탈것 종 (`animal.*`, DESIGN 8.3 · 결정 009) — 서버 `GetMounts`·`ExchangeCollar` 응답을 옮긴다.
	 * 한 종이라도 있으면 탈 수 있다. 도감 칸과 같은 약속 — 클라이언트가 채우지 않는다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	TArray<FName> Mounts;

	/** 종 → 단 안장 (`item.saddle_*`). 모습뿐이다. 서버 `AttachSaddle` 응답을 옮긴다. */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	TMap<FName, FName> MountSaddles;

	/** 이 상태의 주인. 계정이 바뀌면 남의 캐시를 보여 주지 않으려고 같이 들고 다닌다. */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FString PlayFabId;

	/** 백엔드에서 받아온 시각(UTC). 표시용이다 — 경과 시간으로 무언가를 계산하지 않는다 (DESIGN 4.8). */
	UPROPERTY(BlueprintReadOnly, Category = "Backend")
	FDateTime FetchedAtUtc = FDateTime::MinValue();

	/** 주인 없는 스냅샷은 쓸모가 없다 — 어느 계정 것인지 모르면 캐시로 남길 수도 없다. */
	bool HasOwner() const { return !PlayFabId.IsEmpty(); }

	/**
	 * 같은 아이템은 한 칸에 모은다 (슬롯형 — DESIGN 3.3).
	 * PlayFab 클래식 인벤토리는 한 스택을 **인스턴스 여러 줄**로 돌려주기 때문에, 응답을 그대로 옮기면
	 * 돼지고기 3개가 세 칸을 먹는다. 개수가 0 이하이거나 ID 가 비면 무시한다.
	 */
	void AddItem(FName ItemID, int32 Count)
	{
		if (ItemID.IsNone() || Count <= 0)
		{
			return;
		}

		if (FInventorySlot* const Existing = InventorySlots.FindByPredicate(
			[ItemID](const FInventorySlot& Slot) { return Slot.ItemID == ItemID; }))
		{
			Existing->Count += Count;
			return;
		}

		FInventorySlot NewSlot;
		NewSlot.ItemID = ItemID;
		NewSlot.Count = Count;
		InventorySlots.Add(NewSlot);
	}
};
