#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "OutpostRow.generated.h"

// 거점(마을) 한 곳의 이름과 자리 (DT_OutpostTable 의 행 구조체). **행 이름이 곧 거점 id** 다 —
// `tools/unreal/data/world_map.json` 의 `outposts[].id` (`haven_outpost`·`snowbell_post` …).
// 자리(XCm·YCm)는 그 json 에서 `create_outpost_table.py` 가 옮겨 쓴다 — 지도를 다시 굽으면 표도 다시 만든다.
// 큰 지도 마을 이름 글자와 들어설 때 이름 띠가 이 표 하나만 읽는다 (DataDriven.md 1).

USTRUCT(BlueprintType)
struct FOutpostRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 화면에 보이는 마을 이름 (한글, 레퍼런스 게임 이름과 다르게 우리 것 — Reference.md 6절). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outpost")
	FText DisplayName;

	/** 거점 가운데 월드 위치 (cm, 위 = +X · 오른쪽 = +Y). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outpost")
	float XCm = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outpost")
	float YCm = 0.f;

	/** 이 반지름 안에 들어서면 「마을에 들어섰다」 (cm). 근거는 DESIGN 지도 절 마을 이름 표. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Outpost")
	float EnterRadiusCm = 0.f;
};
