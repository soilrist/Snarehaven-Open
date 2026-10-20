// `DT_ItemTable` 을 **ItemID**(`item.pig_meat`)로 찾는다. 표의 행 이름(`Item_PigMeat`)과 서버·인벤토리가 쓰는
// ItemID 가 달라서, 배낭·지급 대기열처럼 ItemID 만 들고 있는 쪽은 한 줄씩 훑는다(행 수 수백 — 누를 때만 부른다).
#pragma once

#include "CoreMinimal.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"

namespace SnareItemRows
{
	/** 그 ItemID 의 행. 없거나 표가 비면 nullptr. */
	inline const FItemRow* FindByItemID(const UDataTable* ItemTable, FName ItemID)
	{
		if (!IsValid(ItemTable) || ItemID.IsNone())
		{
			return nullptr;
		}
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row != nullptr && Row->ItemID == ItemID)
			{
				return Row;
			}
		}
		return nullptr;
	}

	/** 게임이 쓰는 아이템 표 경로 하나. 생성자에서 `ConstructorHelpers` 로 읽는다. */
	inline const TCHAR* ItemTablePath()
	{
		return TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	}
}
