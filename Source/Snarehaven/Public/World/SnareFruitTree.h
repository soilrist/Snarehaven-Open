#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// DESIGN 8.18 과일나무 — 서버 `GetFruitTrees` 답을 나무 몸이 그릴 값으로 옮기는 규칙.
// **보이기만 정한다** — 심기·따기·베기·열매 수는 전부 서버가 판정한다(CLAUDE.md 8). 여기 숫자는 화면 값(🟡)이다.
namespace SnareFruitTree
{
	/** 목장 나무 자리 수 (서버 `FRUIT_SLOTS`). */
	constexpr int32 SlotCount = 4;

	/** 나무 하나의 단계. 서버는 빈 자리를 목록에 안 싣는다 — 목록에 없으면 `Empty`. */
	enum class EStage : uint8
	{
		Empty,
		Sapling,
		Grown,
	};

	/** 서버 `trees[]` 한 줄. */
	struct FTreeView
	{
		int32 Slot = INDEX_NONE;
		FName Fruit;
		EStage Stage = EStage::Empty;
		int32 Fruits = 0;
	};

	/**
	 * `GetFruitTrees` 답 → 자리별 나무. 칸 수는 늘 `SlotCount` 이고 빈 자리는 `Stage == Empty`.
	 * @return `trees` 필드가 있었으면 true — 없는 답(옛 서버·오류)이면 false 라 부르는 쪽이 화면을 안 바꾼다
	 */
	SNAREHAVEN_API bool ParseTrees(const TSharedPtr<FJsonObject>& Reply, TArray<FTreeView>& OutBySlot);

	/** 단계별 나무 키 배율 (다 자란 나무 = 1). 묘목은 작게, 빈 자리는 0. */
	SNAREHAVEN_API float StageScale(EStage Stage);

	/** 가지에 보일 열매 수 — 다 자란 나무만, 서버 열매 수를 0~3 으로 깎는다. */
	SNAREHAVEN_API int32 VisibleFruits(EStage Stage, int32 ServerFruits);

	/** 베기 버튼이 뜨는 때 — 열매를 다 딴 다 자란 나무에 도끼를 들었을 때만(연타로 방금 딴 나무를 실수로 베지 않게). */
	SNAREHAVEN_API bool CanChop(EStage Stage, int32 ShownFruits, bool bAxeInHand);

	/** 과일 5종 아이템 ID — 심을 과일을 가방에서 찾을 때 쓴다(서버 `NATIVE_FRUITS` 와 같은 순서). */
	SNAREHAVEN_API const TArray<FName>& FruitItemIDs();

	/** 과일 색 (5종, 모르는 이름이면 회색). 과일 ID 는 `item.pear` 같은 아이템 ID. */
	SNAREHAVEN_API FLinearColor FruitColor(FName FruitItemID);

	/** 계정 ID → 고유 과일 아이템 ID. 서버 `nativeFruitOf`(FNV-1a + mulberry32 첫 값 × 5)와 같은 계산이라 클라가 방장 과일을 서버 없이 안다. */
	SNAREHAVEN_API FName NativeFruitOf(const FString& PlayFabId);

	/** 과일 아이템 ID → 화면 이름 (「복숭아」). 모르는 이름이면 빈 글자. */
	SNAREHAVEN_API FText FruitName(FName FruitItemID);

	/** 친구 거점 나무 이름표 「○○의 복숭아나무」 (DESIGN 8.18 이름표). 닉네임이 비면 「친구」. */
	SNAREHAVEN_API FText FriendTreeLabel(const FString& OwnerNickname, FName FruitItemID);

	/** 친구 나무를 흔드는 요청 인자 `{hostId}` (서버 `PickFriendFruit`). */
	SNAREHAVEN_API TSharedRef<FJsonObject> MakePickArgs(const FString& HostPlayFabId);
}
