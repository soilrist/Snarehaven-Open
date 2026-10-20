#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "FishingRow.generated.h"

// ROADMAP M10-01: 낚시터 슬롯에서 낚는 어종 한 종을 적은 데이터 한 줄
// (DT_FishingTable 의 행 구조체). 근거는 **DESIGN 8.4 확정 표**(감독 결정 2026-09-22 —
// 붕어 15분 / 잉어 30분 / 메기 60분, 미수확 상한은 목장과 같은 5).
//
// `FCropProductRow`(거점 작물)·`FRanchProductRow`(동물 생산) 와 **일부러 같은 짜임**이다 —
// DESIGN 8.4 가 "목장(3.2)·작물(8.2)과 같은 짜임(거점 슬롯 -> 시간 경과 -> 수확)" 이라고 못 박았고,
// 새 메커니즘을 만들지 않는다. 칸 이름이 같아야 슬롯 코드(M10-02)가 세 표를 같은 방식으로 읽는다.
//
// **어종 ID 칸이 없다** — 작물과 같은 이유다(`CropProductRow.h` 주석). 낚는 것과 거두는 것이 같은
// 종이라 `fish.*` 라는 새 이름 무리를 만들 자리가 없다. 어종의 정체성은 `ProducedItemID` 다.
//
// **이 표의 숫자는 DESIGN 8.4 의 사본이다.** 한쪽만 고치면 2층
// `Snarehaven.Fishing.Table.MatchesDesign` 이 운다 (작물 표 대조와 같은 짜임).
//
// **사이클당 코인(5·10·20)은 여기 없다.** 목장 코인 10·작물 코인이 표에 없는 것과 같은 이유다 —
// 지급액은 백엔드가 판정한다 (DESIGN 4.1 권위 2단 표 · CLAUDE.md 8). 클라이언트가 코인 숫자를
// 들고 있으면 그게 곧 치트 경로다. 값은 DESIGN 8.4 표에 있고 CloudScript 가 읽는다(M10-03).

// 어종이 사는 물 (DESIGN 8.13 "서식지"). 직접 낚시(`RollFish`)가 그 자리 서식지 어종만 굴린다.
UENUM(BlueprintType)
enum class EFishHabitat : uint8
{
	/** 연못·강. */
	Freshwater,
	/** 바다 해안. */
	Sea,
	/** 먼바다 — 바다 어종 중 낚시 시간 120분 이상 (DESIGN 8.13). 배나 수영으로 간다. */
	DeepSea,
	/** 늪 물웅덩이에서만 (DESIGN 8.13 "늪 1" — 장어). 늪 자리는 민물 어종도 낚인다. */
	Swamp
};

USTRUCT(BlueprintType)
struct FFishingRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 낚이면 나오는 아이템 ID (`DT_ItemTable` 의 `ItemID`, 예: `item.crucian_carp`). 이 행의 정체성이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	FName ProducedItemID;

	/** 한 사이클(낚시) 길이(분). DESIGN 8.4 = 15 / 30 / 60. 음수면 미정 취급 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	int32 CycleMinutes = -1;

	/**
	 * 수확하지 않고 쌓아 둘 수 있는 최대 개수. DESIGN 8.4 = 목장·작물과 같은 5 (새 숫자가 아니다).
	 * `FRanchProductRow::MaxStack` 과 같은 뜻·같은 값이라 2층이 두 표를 맞대어 본다. 음수면 미정.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	int32 MaxStack = -1;

	/** 사는 물 (DESIGN 8.13 민물 12 · 바다 9 · 늪 1, 먼바다는 바다 중 120분 이상). 통발(방치형)은 이 칸을 안 본다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fishing")
	EFishHabitat Habitat = EFishHabitat::Freshwater;
};
