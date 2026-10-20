#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWorld;

// DESIGN 8.18 과일나무 ③b — 목장 나무 4자리와 서버를 잇는 한 곳.
// 서버 `GetFruitTrees` 답으로 나무를 **그리기만** 하고, 심기·따기·베기는 서버에 **요청**만 보낸다. 열매·과일·나무 수는
// 서버 답이 바꾼다(CLAUDE.md 8) — 요청이 받아들여져도 곧바로 나무를 바꾸지 않고 `GetFruitTrees` 를 다시 묻는다.
namespace FruitTreeSync
{
	enum class EAction : uint8
	{
		Plant,
		Harvest,
		Chop,
	};

	/** 행동 → 서버 함수 이름 (`PlantFruit`·`HarvestTree`·`ChopTree`). */
	SNAREHAVEN_API FString FunctionName(EAction Action);

	/** 요청 인자. 심기만 `itemId` 가 실린다. */
	SNAREHAVEN_API TSharedRef<FJsonObject> MakeArgs(int32 Slot, FName FruitItemID);

	/**
	 * `GetFruitTrees` 답을 월드의 나무 액터에 입힌다. 거절·오류·`trees` 없는 답이면 화면을 안 바꾼다.
	 * @return 입혔으면 true
	 */
	SNAREHAVEN_API bool ApplyReply(UWorld* World, bool bOk, const TSharedPtr<FJsonObject>& Response);

	/** 서버에 나무 상태를 묻고 답을 `ApplyReply` 로 입힌다. 이미 묻는 중이면 아무것도 안 한다. @return 물었으면 true */
	SNAREHAVEN_API bool Refresh(UWorld* World);

	/**
	 * 심기·따기·베기를 서버에 보낸다. 한 번에 하나만 — 답이 오기 전엔 거절(false). 받아들여지면 보유 상태와 나무를 다시 받는다.
	 * @param FruitItemID 심기일 때만 쓰는 과일 아이템 ID
	 * @return 요청을 보냈으면 true
	 */
	SNAREHAVEN_API bool Request(UWorld* World, EAction Action, int32 Slot, FName FruitItemID = NAME_None);

	/** 친구(방장) 나무를 흔드는 서버 함수 이름 `PickFriendFruit`. */
	SNAREHAVEN_API FString PickFriendFunctionName();

	/**
	 * 방장 나무를 흔드는 요청을 서버에 보낸다(DESIGN 8.18 「남의 과일 얻기」). 한 번에 하나만 — 답이 오기 전엔 거절(false).
	 * 받아들여지면 보유 상태만 다시 받는다(내 나무는 안 바뀜). 방장 계정이 비었으면 보내지 않는다.
	 * @return 요청을 보냈으면 true
	 */
	SNAREHAVEN_API bool PickFriend(UWorld* World, const FString& HostPlayFabId);
}
