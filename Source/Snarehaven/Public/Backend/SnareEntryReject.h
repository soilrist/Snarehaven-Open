#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UDataTable;

// 입장 거절 사유 문구 한 곳 — 나룻배·섬 입장료(`PayFerry`)와 멀티 방 참가가 같은 표(`DT_EntryRejectTable`)를 본다.
// 사유 **판정**은 서버·EOS·엔진이 하고, 여기는 그 코드를 한국어 한 줄로 바꿀 뿐이다 (CLAUDE.md 8).
namespace SnareEntryReject
{
	SNAREHAVEN_API extern const TCHAR* const TablePath;

	/** 표에 없는 코드가 쓰는 행 이름. */
	SNAREHAVEN_API extern const FName UnknownCode;

	/** `TablePath` 를 읽는다. 없으면 nullptr. */
	SNAREHAVEN_API const UDataTable* LoadTable();

	/**
	 * 서버 답에서 거절 코드를 읽는다.
	 * @return 받아들였으면 빈 문자열, `{error}` 면 그 `code`(없으면 `unknown`), 닿지 못했으면 `network`
	 */
	SNAREHAVEN_API FString ReadCode(bool bOk, const TSharedPtr<FJsonObject>& Response);

	/**
	 * 거절 코드 → 문구. 행이 없으면 `unknown` 행을 쓴다. `{need}`·`{have}`·`{code}` 는 Response 에서 끼운다.
	 * 표가 통째로 없으면 코드를 그대로 보인다 — 사유를 지어내지 않는다 (Verification.md 7).
	 */
	SNAREHAVEN_API FText MessageFor(const UDataTable* Table, const FString& Code,
		const TSharedPtr<FJsonObject>& Response = nullptr);

	/** `ReadCode` + `MessageFor`. 받아들였으면 빈 FText. */
	SNAREHAVEN_API FText DescribeReply(const UDataTable* Table, bool bOk, const TSharedPtr<FJsonObject>& Response);
}
