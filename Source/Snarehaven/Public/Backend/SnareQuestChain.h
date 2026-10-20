#pragma once

#include "CoreMinimal.h"

class UDataTable;
struct FQuestChainRow;

// DESIGN 8.14 「의뢰 줄기」 규칙 함수 — 「지금 몇 단계·목표를 채웠나·받을 수 있나」를 표(`DT_QuestChain`)에서 읽는다.
// 단계 번호는 서버 user data `questChain` 이 든 **지금 단계**(1부터, 끝나면 단계 수 + 1)이고,
// 여기는 판정이 아니라 서버 답을 화면에 옮길 때 쓰는 순수 함수다 (CLAUDE.md 8).
namespace SnareQuestChain
{
	SNAREHAVEN_API extern const TCHAR* const TablePath;

	/** 서버 실패 코드 (DESIGN 「실패 코드」). */
	SNAREHAVEN_API extern const TCHAR* const CodeLocked;
	SNAREHAVEN_API extern const TCHAR* const CodeDone;
	SNAREHAVEN_API extern const TCHAR* const CodeShort;

	enum class EResult : uint8
	{
		Ok,
		Locked,	// 지금 단계가 아닌 단계를 받으려 함 (건너뛰기·되돌리기)
		Done,	// 이미 끝까지 받음
		Short	// 재료 모자람
	};

	/** `TablePath` 를 읽는다. 없으면 nullptr. */
	SNAREHAVEN_API const UDataTable* LoadTable();

	/** 행 이름 `chain.<단계>`. */
	SNAREHAVEN_API FName RowNameFor(int32 Step);

	/** 표에 `chain.1` 부터 끊김 없이 이어진 행 수. */
	SNAREHAVEN_API int32 StepCount(const UDataTable* Table);

	/** 단계 행. 범위 밖이면 nullptr. */
	SNAREHAVEN_API const FQuestChainRow* FindStep(const UDataTable* Table, int32 Step);

	/** 줄기를 다 받았나 (지금 단계가 단계 수를 넘음). 표가 비면 true — 보일 게 없다. */
	SNAREHAVEN_API bool IsFinished(const UDataTable* Table, int32 CurrentStep);

	/** 재료 한 줄의 모은 개수 (Have 에서 읽는다, 없으면 0). 목표 개수를 넘겨도 Count 로 자르지 않는다. */
	SNAREHAVEN_API int32 HaveCount(const TMap<FName, int32>& Have, FName ItemId);

	/** 목표를 전부 채웠나. */
	SNAREHAVEN_API bool IsGoalMet(const FQuestChainRow& Row, const TMap<FName, int32>& Have);

	/**
	 * 단계 `Requested` 를 받을 수 있나 — 서버 판정과 같은 순서: 끝났으면 Done, 지금 단계가 아니면 Locked,
	 * 재료가 모자라면 Short. 실패하면 아무것도 안 바뀐다.
	 */
	SNAREHAVEN_API EResult CanComplete(const UDataTable* Table, int32 CurrentStep, int32 Requested,
		const TMap<FName, int32>& Have);

	/** 결과 → 서버 실패 코드. Ok 면 빈 문자열. */
	SNAREHAVEN_API FString CodeFor(EResult Result);

	/** 받은 뒤 지금 단계 (= CurrentStep + 1). 받을 수 없으면 CurrentStep 그대로. */
	SNAREHAVEN_API int32 StepAfter(const UDataTable* Table, int32 CurrentStep, int32 Requested,
		const TMap<FName, int32>& Have);
}
