#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "EntryRejectRow.generated.h"

// 입장 거절 문구 한 줄 (DT_EntryRejectTable 의 행 구조체). **행 이름이 곧 거절 코드**다 —
// 서버 `fail(code, …)` 의 `code`(`not_enough_coins`·`locked`·`bad_args`), 멀티 입장 거절
// (`room_full`·`version_mismatch`·`room_join_failed`), 닿지 못함(`network`), 모르는 코드(`unknown`).
// 문구를 코드 상수로 두지 않는 이유: 문구가 늘거나 바뀔 때 C++ 파일이 안 바뀌게 (DataDriven.md 1).
// 목장 실패 문구는 이 표에 넣지 않는다 — 목장 HUD 가 자기 표를 따로 든다.

USTRUCT(BlueprintType)
struct FEntryRejectRow : public FTableRowBase
{
	GENERATED_BODY()

	/**
	 * 화면 문구. 끼울 수 있는 자리: `{need}`·`{have}`(서버가 준 모자란 수, `not_enough_coins`)·`{code}`(거절 코드).
	 * 판정은 서버다 — 이 문구는 서버가 거절한 **뒤에만** 뜬다 (CLAUDE.md 8).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "EntryReject")
	FText Message;
};
