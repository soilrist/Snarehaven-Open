#pragma once

#include "CoreMinimal.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Data/WaterActivityRow.h"

class UDataTable;
struct FFishingRow;

// DESIGN 8.9·8.13 의 식을 한곳에 모은다 — 수영·배·낚시 컴포넌트와 2층 검사가 **같은 함수**를 부른다.
// 숫자는 전부 `FWaterActivityRow`(DT_WaterActivityTable) 에서 온다. 여기엔 식만 있다.
namespace SnareWater
{
	SNAREHAVEN_API extern const TCHAR* const TablePath;
	SNAREHAVEN_API extern const TCHAR* const FishingTablePath;

	/** 음수(미정) 칸이 하나라도 있거나 나누개가 0 이면 false. */
	SNAREHAVEN_API bool IsRowComplete(const FWaterActivityRow& Row);

	/** 표의 `Default` 행. 없거나 미정이 있으면 nullptr. */
	SNAREHAVEN_API const FWaterActivityRow* FindRow(const UDataTable* Table);

	/** `TablePath` 를 읽어 `FindRow`. */
	SNAREHAVEN_API const FWaterActivityRow* LoadRow();

	/** 배 속도 배율 = 배 × 모터 배율. MotorTier 0 = 모터 없음, 1 = 보트 모터, 2 = 전기 보트 모터. */
	SNAREHAVEN_API float BoatSpeedMul(const FWaterActivityRow& Row, int32 MotorTier);

	/** 한 번 누를 때 진행 = 목표 ÷ (기본 + 낚시 시간(분) ÷ 나누개). */
	SNAREHAVEN_API float ProgressPerPress(const FWaterActivityRow& Row, float CycleMinutes);

	/** 목표까지 필요한 누름 수 (농어 30분 = 13 · 악상어 180분 = 28). */
	SNAREHAVEN_API int32 PressesNeeded(const FWaterActivityRow& Row, float CycleMinutes);

	/** 입질 대기(초). Alpha 0~1 은 호스트가 굴린 값. GearSpeedMul = 낚싯대 × 모자 배율. */
	SNAREHAVEN_API float BiteWaitSeconds(const FWaterActivityRow& Row, float Alpha, float GearSpeedMul, bool bWormBait);

	/** 이 어종이 이 자리에서 나와도 되는지 (DESIGN 8.13 "서식지"). */
	SNAREHAVEN_API bool FishFitsSpot(const FWaterActivityRow& Row, EFishingSpotKind Kind, const FFishingRow& Fish);

	/** `DT_FishingTable` 에서 ProducedItemID 가 같은 행. 없으면 nullptr. */
	SNAREHAVEN_API const FFishingRow* FindFishByItemId(FName FishItemId);

	/** 서버 spotId 문자열의 머리(`pond`·`river`·`coast`·`far`) 로 종류를 읽는다. 모르면 false. */
	SNAREHAVEN_API bool SpotKindFromId(const FString& SpotId, EFishingSpotKind& OutKind);

	/** 이 월드의 백엔드 서브시스템 (GameInstance 가 없는 2층 월드면 nullptr). */
	SNAREHAVEN_API USnareBackendSubsystem* FindBackend(const UObject* WorldContext);

	/**
	 * 서버 판정 함수 한 번. 서브시스템 → (없으면) `TestServerOverride` → (그것도 없으면) 실패 순서.
	 * 응답에 `code` 가 있으면(서버 `fail()`) bOk=false, Error = code 로 돌려준다.
	 */
	SNAREHAVEN_API void CallServer(const UObject* WorldContext, const FString& FunctionName,
		const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone Done);

	/** 보유 상태가 바뀌었으면 다시 받는다 (서브시스템이 없으면 아무것도 안 한다). */
	SNAREHAVEN_API void RefreshPlayerState(const UObject* WorldContext);

	/** 백엔드 스냅샷 기준 보유 개수 (서브시스템이 없으면 0). ItemID 는 `item.worm_bait` 꼴. */
	SNAREHAVEN_API int32 CountOwned(const UObject* WorldContext, FName ItemID);

	/** 물가인가 (DESIGN 13.6 물 병 채우기) — 바다 또는 낚시 포인트가 RangeCm 안. */
	SNAREHAVEN_API bool IsNearWater(const UObject* WorldContext, const FVector& From, float RangeCm);
}
