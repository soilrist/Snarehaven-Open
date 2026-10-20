#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GachaRateRow.generated.h"

// 뽑기 등급. DESIGN 10.2 "등급별 확률" 이 넷으로 못 박아 둔 값이라 데이터가 아니라 타입이다 —
// 등급이 늘면 확률·천장·환급 규칙이 같이 바뀌므로 그때는 설계서부터 고친다.
UENUM(BlueprintType)
enum class EGachaGrade : uint8
{
	Common,
	Rare,
	Epic,
	Legendary
};

// ROADMAP M7-01: 올가미 기계의 등급 하나와 그 확률 (DT_GachaRates 의 행 구조체).
// **원본은 `docs/GACHA_DISCLOSURE.md`(DESIGN 10.2 를 옮겨 적은 공개 문서)다** — 수치를 바꿀 때는
// 그 문서를 먼저 고치고 이 표를 맞춘다. 둘이 어긋나면 M7-02 의 2층 검사가 운다.
// 어긋난 채로 출시하면 확률 오표시라 게임산업법상 2년 이하 징역 또는 2000만원 이하 벌금이다 (DESIGN 10.4).
//
// **이 표는 판정이 아니다.** 확률 롤·천장 카운트·중복 환급은 전부 PlayFab CloudScript 가 계산하고
// (DESIGN 10.5 · 4.1 권위 2단 표 · CLAUDE.md 8), 클라이언트는 이 표를 **화면에 확률을 보여주는 데만**
// 쓴다. 클라이언트가 이 표를 보고 스스로 굴리면 그건 치트 경로다.
USTRUCT(BlueprintType)
struct FGachaRateRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 어느 등급인가. 행 이름 `Gacha_Common` ↔ `EGachaGrade::Common` (DataDriven.md 4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	EGachaGrade Grade = EGachaGrade::Common;

	/** 뽑기 화면에 그대로 뜨는 이름 (`커먼`·`레어`·`에픽`·`레전더리`, DESIGN 10.2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	FText DisplayName;

	/** 이 등급이 나올 확률(백분율). DESIGN 10.2 = 70 / 22 / 6.5 / 1.5. 음수면 미정 취급 (Verification.md 7). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gacha")
	float ProbabilityPct = -1.0f;
};
