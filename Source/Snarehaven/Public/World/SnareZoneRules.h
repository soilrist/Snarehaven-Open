#pragma once

#include "CoreMinimal.h"
#include "Data/ZoneRow.h"

class ASeaActor;
class UDataTable;

// M12-00·01: 맵 안 성격 존(설원·늪·섬) 판정을 한곳에 모은다 — 분산 스포너·건축·존 효과 컴포넌트와
// 2층 검사가 **같은 함수**를 부른다 (`SnareWater` 와 같은 짜임). 숫자는 전부 `DT_ZoneTable` 행에서 온다.
namespace SnareZones
{
	SNAREHAVEN_API extern const TCHAR* const TablePath;

	/** 맵 안 정사각형 칸을 가진 행인가 (반변 > 0). 아니면 맵 전체 행(`zone.hunt` 꼴)이다. */
	SNAREHAVEN_API bool IsRegionRow(const FZoneRow& Row);

	/** 이 자리(X·Y)가 행의 칸 안인가. 칸이 없는 행이면 false. 경계 위는 안으로 친다. */
	SNAREHAVEN_API bool IsInsideRegion(const FZoneRow& Row, const FVector& Location);

	/** 표에서 `ZoneID` 가 같은 행. 없으면 nullptr. */
	SNAREHAVEN_API const FZoneRow* FindZoneRow(const UDataTable* Table, FName ZoneID);

	/**
	 * 이 맵의 이 자리를 덮는 성격 존 행. 칸이 겹치면 표의 첫 행. 없으면 nullptr.
	 * `Sea` 가 새 지도 거리장을 들고 있으면 정사각형 대신 존 칸 코드(`FieldCode`)로 고른다 — 뭍일 때만. 바다는 nullptr.
	 */
	SNAREHAVEN_API const FZoneRow* FindRegionAt(const UDataTable* Table, FName LevelName, const FVector& Location,
		const ASeaActor* Sea = nullptr);

	/** 월드의 맵 이름(PIE 접두어 뺀 것)으로 `FindRegionAt`. 표는 `TablePath` 에서 읽는다. */
	SNAREHAVEN_API const FZoneRow* FindRegionAt(const UObject* WorldContext, const FVector& Location);

	/**
	 * 월드 판 `IsInsideRegion` — 바다가 새 지도 거리장을 들고 행에 `FieldCode` 가 있으면 존 칸 코드와 뭍으로,
	 * 아니면 정사각형으로 가른다. 게임 쪽 판정(동물 분산·위험구역·보물)은 이것을 부른다.
	 */
	SNAREHAVEN_API bool IsInsideRegion(const UObject* WorldContext, const FZoneRow& Row, const FVector& Location);

	/** 행의 틀(정사각형) 안에서 무작위로 골라 그 존 안인 첫 점. `MaxTries` 번 다 빗나가면 false. */
	SNAREHAVEN_API bool RandomPointInRegion(const UObject* WorldContext, const FZoneRow& Row, float InsetCm, FVector2D& OutPoint);

	/** 이 자리가 건축 불가 존(늪) 안인가. */
	SNAREHAVEN_API bool IsBuildBlockedAt(const UObject* WorldContext, const FVector& Location);
}
