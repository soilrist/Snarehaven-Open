#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "UsageHintRow.generated.h"

// 처음 들기·처음 다가가기 안내 한 줄 (DT_UsageHints 의 행 구조체, `docs/specs/onboarding-usage-hints.md`).
// **행 이름이 곧 안내 키**다 — 도구 `tool.<대표 ItemID>`(`tool.item.hatchet`) · 놓인 것 `place.<EPlaceableFunction>`
// (`place.Workbench`) · 목장 칸 `ranch.slot` · 상인 `vendor.shop`. 본 키는 이 기기에 저장한다(`SnareUsageHints`).
// 글을 코드 상수로 두지 않는 이유: 줄이 늘거나 글이 바뀔 때 C++ 파일이 안 바뀌게 (DataDriven.md 1).
// `Guide*` 칸은 M28-06 안내서가 같은 표를 읽으려고 미리 둔 자리다 — 글은 그 조각이 채운다.

USTRUCT(BlueprintType)
struct FUsageHintRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 상황 버튼 위에 뜨는 한 줄 (「나무 앞에서 「베기」 — 한 번에 원목 3~5개」). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UsageHint")
	FText Line;

	/** 한 줄이 떠 있는 시간(초). 값과 근거는 명세 3절 「한 줄 떠 있는 시간」. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UsageHint")
	float ShowSeconds = 0.f;

	/**
	 * 이 줄을 같이 쓰는 다른 도구 ItemID — 낚싯대 넷은 `tool.item.rod_basic` 한 줄, 곡괭이 셋은 `tool.item.pickaxe_basic` 한 줄.
	 * 대표 ItemID 자체는 행 이름에 있으니 여기 다시 적지 않는다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UsageHint")
	TArray<FName> AlsoItemIDs;

	/** M28-06 안내서 — 쪽 제목. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FText GuideTitle;

	/** M28-06 안내서 — 순서대로 몇 줄. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	TArray<FText> GuideSteps;

	/** M28-06 안내서 — 아이콘을 빌려 올 `DT_ItemTable` 의 ItemID. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	FName GuideIconItemID;

	/** M28-06 안내서 — 쪽 순서(작을수록 앞). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Guide")
	int32 GuideOrder = 0;
};
