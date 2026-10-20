#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "AchievementRow.generated.h"

// ROADMAP M12-04: 업적 한 개를 적은 데이터 한 줄 (DT_AchievementTable 의 행 구조체).
// 근거는 **DESIGN 8.15 업적 목록 표**. 서버 `ACHIEVEMENTS`(cloudscript_main.js)와 ID·카운터·목표가
// 같아야 한다 — 2층 `Snarehaven.Achievement.Table.*` 이 셋을 맞댄다.
//
// `RewardCoins` 는 **표시 전용**이다(M48-03b) — 달성 알림 카드 「+N 코인」 글에만 쓴다. 지급액은 백엔드가
// 판정하고(CLAUDE.md 8) 2층 `Achievement.Table.MatchesServer` 가 이 값을 서버 `coins` 와 맞댄다.
// 달성 여부도 여기서 계산하지 않는다. 화면은 스냅샷의 `Achievements.Completed` 만 본다.

USTRUCT(BlueprintType)
struct FAchievementRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 서버가 `done` 에 적는 ID (예: `ach.capture_1`). 이 행의 정체성이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement")
	FName AchievementID;

	/** 화면에 보이는 이름 (DESIGN 8.15 "이름" 칸). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement")
	FText DisplayName;

	/** 서버 카운터 이름 (`capture` · `hunt` · `fish` · `harvest` · `craft` · `house` · `codexPercent`). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement")
	FName Counter;

	/** 목표 값 — 진행 수 `현재/목표` 를 그릴 때만 쓴다. 음수면 미정 취급 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement")
	int32 Target = -1;

	/** 보상 코인 — **표시 전용**(DESIGN 8.15 "코인" 칸). 음수면 미정 취급, 카드에 코인 글을 안 붙인다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement")
	int32 RewardCoins = -1;
};
