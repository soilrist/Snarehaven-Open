#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "RanchFailureRow.generated.h"

// 목장 서버 함수(RegisterPet·HarvestRanch·FeedRanchUpkeep·ExpandRanchSlot)가 거절한 `code` → 화면 문구
// (`DT_RanchFailureTable` 의 행 구조체, M12-05). 문구를 코드 상수로 두지 않는다 (DataDriven.md).
//
// 행 이름은 서버 `code` 그대로다(`locked`, `not_enough_fame` …). 같은 코드가 함수마다 뜻이 다르면
// (`full` = 유지가 가득 / 칸이 최대) `<함수 이름>_<code>` 행이 먼저 잡힌다. 표에 없는 코드는
// 서버 사유 문장(`SnareServerReply::DescribeFailure`)을 그대로 쓴다.
USTRUCT(BlueprintType)
struct FRanchFailureRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 화면에 잠깐 띄울 한 줄. 모자란 수(`need`/`have`)가 오면 뒤에 붙는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ranch")
	FText Message;
};
