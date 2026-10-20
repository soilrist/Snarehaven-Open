#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ShopRow.generated.h"

// 🟡 DESIGN 8.14 「가게 여럿」 (감독 2026-09-25) — 레퍼런스 마을처럼 상인이 전문을 나눠 판다.
// 서버 `BuyItem`·`SellItem` 은 전역 목록으로 판정한다 — 이 구분은 **화면에 보이는 줄**만 가른다.
UENUM()
enum class EShopKind : uint8
{
	/** 잡화상 — 아래 셋에 안 드는 물건을 팔고, 파는 값이 있는 것은 전부 산다. */
	General,
	/** 낚시 상인 — 낚싯대·미끼·어부 모자를 팔고 물고기만 산다. */
	Fishing,
	/** 농부 상인 — 씨앗·농부 모자를 팔고 작물만 산다. */
	Farming,
	/** 옷 가게 — 모자·옷·페인트를 판다. 사들이지 않는다. */
	Clothes,
	/** 고구마 장수 — 일요일에만 서서 고구마 묶음을 판다. 줄은 표가 아니라 서버 `GetSweetPotato` 에서 온다(DESIGN 8.17). */
	SweetPotato
};

// DESIGN 8.14 잡화점 "사는 목록" 한 줄 (DT_ShopTable 의 행 구조체). 재고는 무한이라 칸이 없다.
// **이 표의 가격은 DESIGN 8.14 의 사본이다** — 코인 차감·지급은 CloudScript `BuyItem` 이 판정한다 (CLAUDE.md 8).
// 파는 값은 여기가 아니라 `FItemRow::SellPrice` 다 (팔 수 있는 것이 사는 목록보다 훨씬 많다).
USTRUCT(BlueprintType)
struct FShopRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 사는 아이템 (`DT_ItemTable` 의 `ItemID`). 행 이름 `Shop_RodBasic` ↔ `item.rod_basic`. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	FName ItemID;

	/** 한 번 살 때 내는 스네어 코인. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	int32 Price = 0;

	/** 한 번 살 때 받는 개수 (지렁이 팩 ×3). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shop")
	int32 Count = 1;
};
