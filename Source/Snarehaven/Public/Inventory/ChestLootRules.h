#pragma once

#include "CoreMinimal.h"
#include "Inventory/InventoryComponent.h"

class FJsonObject;
struct FZoneRow;

// ROADMAP M13-12: 위험구역·보물 상자(DESIGN 8.10 · 13.8)의 **서버 답 읽기와 확률 고지 표** 한 곳.
// 굴림은 서버(`OpenChest`)만 한다 — 여기는 답을 화면이 쓸 모양으로 바꾸기만 한다 (CLAUDE.md 8).
namespace SnareChestLoot
{
	/** 확률 고지 표 한 줄 (DESIGN 10.6). */
	struct FDisclosureLine
	{
		const TCHAR* ItemID;
		float Chance;
	};

	/**
	 * 10.6 고지 표 18줄 — 서버 `CHEST_LOOT` 와 같은 순서·값. 고지는 보여 주기만 한다(굴림은 서버).
	 * 모바일 빌드엔 설계서가 없어 값을 코드에 적는다 — `Snarehaven.Chest.Disclosure.MatchesDesign` 이
	 * DESIGN 10.6 과 줄마다 맞대고, 서버 쪽은 `test_cloudscript.js` 가 10.6 과 맞댄다.
	 */
	SNAREHAVEN_API TConstArrayView<FDisclosureLine> GetDisclosureLines();

	/** 10.6 마지막 줄 — 설계도(아직 없는 것 중 하나). 서버 `CHEST_BLUEPRINT_CHANCE`. */
	SNAREHAVEN_API FDisclosureLine GetBlueprintLine();

	/**
	 * 8.1c 잠긴 금고 폐허 고지 표 15줄 — 서버 `VAULT_LOOT` 와 같은 순서·값. `Snarehaven.Vault.Disclosure.MatchesDesign` 이
	 * DESIGN 8.1c 와 줄마다 맞댄다. 굴림은 서버(`OpenVault`)만 한다.
	 */
	SNAREHAVEN_API TConstArrayView<FDisclosureLine> GetVaultDisclosureLines();

	/** 8.1c 마지막 줄 — 설계도(아직 없는 것 중 하나) 30%. 서버 `VAULT_BLUEPRINT_CHANCE`. */
	SNAREHAVEN_API FDisclosureLine GetVaultBlueprintLine();

	/** 가방에서 "사용" 하면 `UseTreasureMap` 을 부르는 아이템 — 서버 `TREASURE_ITEM`. */
	SNAREHAVEN_API FName GetTreasureMapItemID();

	/** `OpenChest` 성공 답. */
	struct FOpenResult
	{
		int32 Rolls = 0;
		TArray<FInventorySlot> Items;
		TArray<FName> Blueprints;
		/** 가방이 모자라 전리품이 발밑 가방(`drop`)으로 갔다. */
		bool bDropped = false;
	};

	SNAREHAVEN_API FOpenResult ParseOpenChest(const TSharedPtr<FJsonObject>& Response);

	/** `recharging` 거절의 `waitMs` → 남은 초(올림). 칸이 없으면 -1. */
	SNAREHAVEN_API int32 ParseWaitSeconds(const TSharedPtr<FJsonObject>& Response);

	/** `m:ss` — 미니맵 남은 시간 줄과 같은 꼴. */
	SNAREHAVEN_API FString FormatClock(int32 Seconds);

	/** 서버가 연 보물 한 곳 (`UseTreasureMap` 답, 또는 `busy` 거절의 `treasure`). */
	struct FTreasure
	{
		FString TreasureID;
		FName ZoneID;
		/** 칸 안 0~1 비율. */
		float U = 0.f;
		float V = 0.f;
		FDateTime ExpiresUtc;

		bool IsSet() const { return !TreasureID.IsEmpty(); }
		/** 남은 초. 지났으면 0 이하. */
		double GetRemainingSeconds() const { return (ExpiresUtc - FDateTime::UtcNow()).GetTotalSeconds(); }
	};

	/** 답 객체에서 보물을 읽는다. 칸이 모자라면 false. */
	SNAREHAVEN_API bool ParseTreasure(const TSharedPtr<FJsonObject>& Object, FTreasure& Out);

	/**
	 * 보물 자리의 월드 X·Y. 🟡 u → 월드 X(북), v → 월드 Y(동) — 서버는 비율만 주고 축을 정하지 않았다(13.8 "서버는 위치를 모른다").
	 * 칸 가운데 ± 반변 안이다.
	 */
	SNAREHAVEN_API FVector2D TreasureWorldXY(const FZoneRow& Row, float U, float V);
}
