#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "QuestChainRow.generated.h"

// DESIGN 8.14 「의뢰 줄기」: 의뢰인 솔방울이 한 번만 거는 5단계 부탁 (DT_QuestChain 의 행 구조체).
// 행 이름은 `chain.1`~`chain.5` — 이름의 숫자가 단계 번호다. 숫자 원본은 DESIGN 줄이고,
// 서버 사본(M105-03)과 이 표는 시험이 DESIGN 줄과 맞대어 본다. **판정은 서버 몫이다** (CLAUDE.md 8) —
// 이 표는 글자와 진행 막대에 쓸 뿐 배낭·코인을 직접 바꾸지 않는다.

/** 건네야 하는 재료 한 줄. */
USTRUCT(BlueprintType)
struct FQuestChainNeed
{
	GENERATED_BODY()

	/** 아이템 ID (`item.wood` 꼴). 비어 있으면 미정. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "QuestChain")
	FName ItemId;

	/** 개수. 음수면 미정 취급 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "QuestChain")
	int32 Count = -1;
};

USTRUCT(BlueprintType)
struct FQuestChainRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 단계 이름 (「땔감 부탁」). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "QuestChain")
	FText Title;

	/** 건네야 하는 재료들. 전부 모아야 받을 수 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "QuestChain")
	TArray<FQuestChainNeed> Needs;

	/** 보상 코인. 음수면 미정 취급. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "QuestChain")
	int32 RewardCoins = -1;

	/** 보상 아이템 (없으면 None). 마지막 단계의 금고 열쇠패 `item.vault_pass`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "QuestChain")
	FName RewardItemId;

	/** 보상 아이템 개수. `RewardItemId` 가 None 이면 0. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "QuestChain")
	int32 RewardItemCount = 0;
};
