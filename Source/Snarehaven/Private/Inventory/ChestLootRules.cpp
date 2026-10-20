#include "Inventory/ChestLootRules.h"

#include "Data/ZoneRow.h"
#include "Dom/JsonObject.h"
#include "Inventory/BackpackComponent.h"

namespace SnareChestLoot
{
namespace
{
	// DESIGN 10.6 = 13.8 「위험구역 상자 전리품 줄」 = 서버 `CHEST_LOOT`. 순서도 같다.
	const FDisclosureLine DisclosureLines[] = {
		{TEXT("item.treasure_map"), 0.10f},
		{TEXT("item.ruby"), 0.04f},
		{TEXT("item.sapphire"), 0.04f},
		{TEXT("item.amethyst"), 0.04f},
		{TEXT("item.aquamarine"), 0.04f},
		{TEXT("item.emerald"), 0.04f},
		{TEXT("item.strawberry_hat"), 0.03f},
		{TEXT("item.looter_hat"), 0.03f},
		{TEXT("item.mystery_bag"), 0.10f},
		{TEXT("item.cash_bundle"), 0.05f},
		{TEXT("item.snackbar"), 0.15f},
		{TEXT("item.water_canister"), 0.15f},
		{TEXT("item.salt"), 0.15f},
		{TEXT("item.black_pepper"), 0.15f},
		{TEXT("item.dill"), 0.15f},
		{TEXT("item.tea_bag"), 0.15f},
		{TEXT("item.solar_kit"), 0.05f},
		{TEXT("item.battery"), 0.05f},
		{TEXT("item.light_bulb"), 0.05f},
		{TEXT("item.boat_motor"), 0.03f},
		{TEXT("item.boat_motor_electric"), 0.02f},
		{TEXT("item.boat_container"), 0.03f},
	};

	// DESIGN 10.6 설계도 줄 = 8.10 「위험구역 상자에서 20%」 = 서버 `CHEST_BLUEPRINT_CHANCE`.
	const FDisclosureLine BlueprintLine = {TEXT("item.blueprint_*"), 0.20f};

	// DESIGN 8.1c 잠긴 금고 폐허 표 = 서버 `VAULT_LOOT`. 순서도 같다 (소총탄 줄은 한 묶음 20발).
	const FDisclosureLine VaultLines[] = {
		{TEXT("item.ruby"), 0.12f},
		{TEXT("item.sapphire"), 0.12f},
		{TEXT("item.amethyst"), 0.12f},
		{TEXT("item.aquamarine"), 0.12f},
		{TEXT("item.emerald"), 0.12f},
		{TEXT("item.strawberry_hat"), 0.08f},
		{TEXT("item.looter_hat"), 0.08f},
		{TEXT("item.boat_motor"), 0.08f},
		{TEXT("item.boat_motor_electric"), 0.06f},
		{TEXT("item.boat_container"), 0.08f},
		{TEXT("item.solar_kit"), 0.12f},
		{TEXT("item.battery"), 0.12f},
		{TEXT("item.light_bulb"), 0.12f},
		{TEXT("item.ammo_rifle"), 0.15f},
		{TEXT("item.treasure_map"), 0.20f},
	};

	const FDisclosureLine VaultBlueprintLine = {TEXT("item.blueprint_*"), 0.30f};
}

TConstArrayView<FDisclosureLine> GetVaultDisclosureLines()
{
	return VaultLines;
}

FDisclosureLine GetVaultBlueprintLine()
{
	return VaultBlueprintLine;
}

TConstArrayView<FDisclosureLine> GetDisclosureLines()
{
	return DisclosureLines;
}

FDisclosureLine GetBlueprintLine()
{
	return BlueprintLine;
}

FName GetTreasureMapItemID()
{
	return DisclosureLines[0].ItemID;
}

FOpenResult ParseOpenChest(const TSharedPtr<FJsonObject>& Response)
{
	FOpenResult Out;
	if (!Response.IsValid())
	{
		return Out;
	}
	Response->TryGetNumberField(TEXT("rolls"), Out.Rolls);
	// 가방이 모자라면 서버는 `items` 를 그대로 주고 `drop` 을 붙인다 — 보일 목록은 같다.
	Out.Items = UBackpackComponent::ParseItems(Response);
	TArray<FString> Blueprints;
	Response->TryGetStringArrayField(TEXT("blueprints"), Blueprints);
	for (const FString& Blueprint : Blueprints)
	{
		Out.Blueprints.Add(FName(*Blueprint));
	}
	Out.bDropped = Response->HasTypedField<EJson::Object>(TEXT("drop"));
	return Out;
}

int32 ParseWaitSeconds(const TSharedPtr<FJsonObject>& Response)
{
	double WaitMs = 0.0;
	if (!Response.IsValid() || !Response->TryGetNumberField(TEXT("waitMs"), WaitMs))
	{
		return -1;
	}
	return FMath::Max(FMath::CeilToInt(WaitMs / 1000.0), 0);
}

FString FormatClock(int32 Seconds)
{
	const int32 Whole = FMath::Max(Seconds, 0);
	return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}

bool ParseTreasure(const TSharedPtr<FJsonObject>& Object, FTreasure& Out)
{
	FString ZoneID;
	FString Expires;
	double U = 0.0;
	double V = 0.0;
	FTreasure Parsed;
	if (!Object.IsValid() || !Object->TryGetStringField(TEXT("treasureId"), Parsed.TreasureID)
		|| !Object->TryGetStringField(TEXT("zoneId"), ZoneID) || !Object->TryGetNumberField(TEXT("u"), U)
		|| !Object->TryGetNumberField(TEXT("v"), V) || !Object->TryGetStringField(TEXT("expiresUtc"), Expires)
		|| !FDateTime::ParseIso8601(*Expires, Parsed.ExpiresUtc))
	{
		return false;
	}
	Parsed.ZoneID = FName(*ZoneID);
	Parsed.U = static_cast<float>(U);
	Parsed.V = static_cast<float>(V);
	Out = MoveTemp(Parsed);
	return true;
}

FVector2D TreasureWorldXY(const FZoneRow& Row, float U, float V)
{
	return Row.RegionCenterCm + FVector2D(U - 0.5f, V - 0.5f) * 2.f * Row.RegionHalfExtentCm;
}
}
