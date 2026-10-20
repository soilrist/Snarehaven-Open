// ROADMAP M7-05 — 뽑기 한 번의 결과 한 줄. **백엔드(CloudScript `RollGacha`, M7-06)가 확정해 준 것**을
// 옮겨 담는 그릇이다 (`FSnarePlayerSnapshot` 과 같은 취지 — DESIGN 4.5 · 4.1 권위 2단 표).
//
// **여기 있는 값은 클라이언트가 만들어 내지 않는다.** 확률 롤·천장 카운트·중복 환급 계산은 전부
// CloudScript 몫이고(DESIGN 10.5 · CLAUDE.md 8), 클라이언트는 받은 것을 **보여주기만** 한다.
// 클라이언트가 스스로 굴려 이 구조체를 채우면 그건 치트 경로이자 화면에 적힌 확률과 다른 결과를
// 주는 길이다 — 확률 오표시는 게임산업법상 형사처벌 대상이다 (DESIGN 10.4).
// 그래서 이 파일에는 **확률도 비용도 환급 계산식도 없다.** 전부 응답이 들고 온다.
#pragma once

#include "CoreMinimal.h"
#include "Data/GachaRateRow.h"

#include "GachaPullResult.generated.h"

/**
 * 뽑기 한 번의 결과. 10연차면 이런 줄이 열 개 온다.
 * 미정(모른다)은 0 이 아니라 음수·빈 값으로 둔다 — 화면이 `-` 로 쓴다 (Verification.md 7).
 */
USTRUCT(BlueprintType)
struct FGachaPullResult
{
	GENERATED_BODY()

	/** 백엔드가 굴려 나온 등급. 화면에 뜨는 이름은 이 등급으로 `DT_GachaRates` 에서 찾는다. */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha")
	EGachaGrade Grade = EGachaGrade::Common;

	/** 받은 아이템 ID — `DT_ItemTable` 의 `ItemID` 와 같은 문자열 (M7-07 풀이 서면 그 표의 행이 된다). */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha")
	FName ItemID;

	/** 화면에 그대로 뜨는 아이템 이름. 비어 있으면 화면이 `-` 로 쓴다. */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha")
	FText ItemName;

	/**
	 * 이미 가진 아이템이라 재화 환급으로 바뀌었는가 (DESIGN 10.2 "중복 처리").
	 * **보유 여부를 판정하는 것도 백엔드다** — 클라이언트 인벤토리 캐시로 다시 따지지 않는다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha")
	bool bDuplicate = false;

	/** 환급으로 받은 재화 양. 백엔드가 계산한 값이고, 못 들었으면 음수다(화면은 `-`). */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha")
	int32 RefundAmount = -1;

	/** 환급 재화 코드 — `SC`(스네어 코인)·`GM`(젬), BACKEND.md 데이터 모델 표. */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha")
	FString RefundCurrency;

	/**
	 * 재화 환급 경품(공개 문서 「스네어 코인 xN」)으로 받은 코인 양 — 응답의 `coins`, M35-02.
	 * 아이템이 아니라 돈이라 중복이 없다. 코인 경품이 아니면 0 이다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Gacha")
	int32 PrizeCoins = 0;
};
