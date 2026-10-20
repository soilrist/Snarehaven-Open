#include "World/SnareZoneRules.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "World/SeaActor.h"

namespace SnareZones
{
	const TCHAR* const TablePath = TEXT("/Game/Game/Data/World/DT_ZoneTable.DT_ZoneTable");

	namespace
	{
		// 틀 안 무작위 점이 존에 들 때까지 — 늪처럼 틀의 일부만 차는 존도 32번이면 거의 늘 맞는다
		constexpr int32 MaxRandomTries = 32;

		const ASeaActor* FieldSea(const UObject* WorldContext)
		{
			const ASeaActor* const Sea = ASeaActor::FindSea(WorldContext);
			return Sea != nullptr && Sea->HasField() ? Sea : nullptr;
		}
	}

	bool IsRegionRow(const FZoneRow& Row)
	{
		return Row.RegionHalfExtentCm > 0.f;
	}

	bool IsInsideRegion(const FZoneRow& Row, const FVector& Location)
	{
		return IsRegionRow(Row)
			&& FMath::Abs(Location.X - Row.RegionCenterCm.X) <= Row.RegionHalfExtentCm
			&& FMath::Abs(Location.Y - Row.RegionCenterCm.Y) <= Row.RegionHalfExtentCm;
	}

	const FZoneRow* FindZoneRow(const UDataTable* Table, FName ZoneID)
	{
		if (Table == nullptr || ZoneID.IsNone())
		{
			return nullptr;
		}
		const FZoneRow* Found = nullptr;
		Table->ForeachRow<FZoneRow>(TEXT("SnareZones::FindZoneRow"),
			[&Found, ZoneID](const FName&, const FZoneRow& Row)
			{
				if (Found == nullptr && Row.ZoneID == ZoneID)
				{
					Found = &Row;
				}
			});
		return Found;
	}

	const FZoneRow* FindRegionAt(const UDataTable* Table, FName LevelName, const FVector& Location, const ASeaActor* Sea)
	{
		if (Table == nullptr)
		{
			return nullptr;
		}
		// 새 지도 — 존 칸 코드가 같은 행. 바다(코드 0)는 어느 존도 아니다
		// 바다 칸에도 가장 가까운 뭍 존 코드가 붙어 있어(`make_world_map.py` 채움) 코드만 보면 설원 앞바다가 설원이다 —
		// DESIGN 1.3 "칸 경계": 뭍(해안 거리 < 0)일 때만 그 존, 바다 = 특성 없음 (`IsInsideRegion` 과 같은 식)
		const bool bField = Sea != nullptr && Sea->HasField();
		const int32 Code = bField && Sea->CoastDistance(Location) < 0.f ? Sea->ZoneCodeAt(Location) & 0x0F : 0;
		const FZoneRow* Found = nullptr;
		Table->ForeachRow<FZoneRow>(TEXT("SnareZones::FindRegionAt"),
			[&Found, LevelName, &Location, bField, Code](const FName&, const FZoneRow& Row)
			{
				const bool bInside = bField && Row.FieldCode > 0 ? Code == Row.FieldCode : IsInsideRegion(Row, Location);
				if (Found == nullptr && Row.LevelName == LevelName && bInside)
				{
					Found = &Row;
				}
			});
		return Found;
	}

	const FZoneRow* FindRegionAt(const UObject* WorldContext, const FVector& Location)
	{
		const UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (World == nullptr)
		{
			return nullptr;
		}
		// PIE 는 맵 이름 앞에 `UEDPIE_0_` 를 붙인다 (`AZonePortalActor::FindCurrentZoneID` 와 같은 함정).
		const FName LevelName(*UWorld::RemovePIEPrefix(World->GetMapName()));
		return FindRegionAt(LoadObject<UDataTable>(nullptr, TablePath), LevelName, Location, FieldSea(WorldContext));
	}

	bool IsInsideRegion(const UObject* WorldContext, const FZoneRow& Row, const FVector& Location)
	{
		const ASeaActor* const Sea = Row.FieldCode > 0 ? FieldSea(WorldContext) : nullptr;
		if (Sea == nullptr)
		{
			return IsInsideRegion(Row, Location);
		}
		return (Sea->ZoneCodeAt(Location) & 0x0F) == Row.FieldCode && Sea->CoastDistance(Location) < 0.f;
	}

	bool RandomPointInRegion(const UObject* WorldContext, const FZoneRow& Row, float InsetCm, FVector2D& OutPoint)
	{
		const float Reach = FMath::Max(Row.RegionHalfExtentCm - InsetCm, 0.f);
		for (int32 Try = 0; Try < MaxRandomTries; ++Try)
		{
			OutPoint = Row.RegionCenterCm + FVector2D(FMath::FRandRange(-Reach, Reach), FMath::FRandRange(-Reach, Reach));
			if (IsInsideRegion(WorldContext, Row, FVector(OutPoint, 0.0)))
			{
				return true;
			}
		}
		return false;
	}

	bool IsBuildBlockedAt(const UObject* WorldContext, const FVector& Location)
	{
		const FZoneRow* const Row = FindRegionAt(WorldContext, Location);
		return Row != nullptr && Row->bBuildBlocked;
	}
}
