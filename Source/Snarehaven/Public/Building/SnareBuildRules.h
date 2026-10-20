#pragma once

#include "CoreMinimal.h"
#include "SnareBuildRules.generated.h"

struct FBaseRuleRow;

/**
 * 놓기 미리보기 색 (DESIGN 8.11 "초록 = 내 땅 · 파랑 = 땅 밖 · 빨강 = 겹침").
 * 판정은 서버(`PlaceTent`·`PlaceStructure`)가 하고, 이 색은 누르기 전에 보여 주는 안내일 뿐이다.
 */
UENUM(BlueprintType)
enum class ESnarePlacementVerdict : uint8
{
	/** 내 땅 안, 빈 칸 — 놓을 수 있다. */
	Green,
	/** 내 땅 밖(또는 텐트가 없음·거점 존이 아님·한도) — 못 놓는다. */
	Blue,
	/** 다른 것과 겹친다 — 못 놓는다. */
	Red
};

/** 이미 선 물건을 서버가 바꿔 준 일 — 철거·칠하기 확정 소리를 고르는 칸 (M45-02, DESIGN 2.2 「거점·상자 소리」). */
enum class ESnareStructureEdit : uint8
{
	Remove,
	Paint
};

// DESIGN 8.11 칸 격자 규칙. 서버와 같은 좌표를 쓴다 — 서버 `x`,`y` 는 **칸 번호**(월드 cm ÷ 칸 크기, 내림)이고
// 땅은 텐트 칸을 가운데로 [-LandCells/2, LandCells/2) 이다 (cloudscript `PlaceStructure` 의 `dx < -half || dx >= half`).
// 순수 함수만 둔다 — 2층 검사가 월드 없이 부른다.
namespace SnareBuildRules
{
	/** 월드 위치 → 칸 번호 (내림 — 음수 칸도 칸 경계가 같다). */
	SNAREHAVEN_API FIntPoint WorldToCell(const FVector& WorldLocation, float CellSizeCm);

	/** 칸 발자국(앵커 칸이 왼쪽 아래)의 월드 가운데. */
	SNAREHAVEN_API FVector FootprintCenter(const FIntPoint& AnchorCell, const FIntPoint& Size, float CellSizeCm, float Z);

	/**
	 * 캐릭터 앞에 놓을 발자국의 앵커 칸 — 발자국 가운데가 바라보는 방향 줄 위에 오고, 발자국에서 가장 가까운 점이
	 * 캐릭터 중심에서 `ClearanceCm` 이상 떨어진다. 앵커(왼쪽 아래)를 앞 칸에 그대로 두면 뒤·옆을 볼 때 발자국이
	 * 몸 쪽으로 펼쳐져 텐트(3×3)가 캐릭터를 덮는다 (INBOX M22 ②).
	 */
	SNAREHAVEN_API FIntPoint AnchorAhead(const FVector2D& Origin, const FVector2D& Forward, const FIntPoint& Size,
		float CellSizeCm, float ClearanceCm);

	/**
	 * 세밀 격자(반 칸 = 50cm) — 좌표를 **반 칸 단위**(`Units`, 칸 번호 ×2 + 반 칸 오프셋)로 다룬다.
	 * 서버는 칸 번호 `x`,`y` 와 반 칸 오프셋 `hx`,`hy`(0 또는 1)를 따로 저장한다 (옛 가구는 오프셋 0).
	 */
	SNAREHAVEN_API FIntPoint AnchorAheadHalf(const FVector2D& Origin, const FVector2D& Forward, const FIntPoint& Size,
		float CellSizeCm, float ClearanceCm);

	/** 반 칸 단위 앵커 → 칸 번호 + 반 칸 오프셋(0/1). 음수도 내림 기준. */
	SNAREHAVEN_API void SplitHalfUnits(const FIntPoint& Units, FIntPoint& OutCell, FIntPoint& OutHalf);

	/** 칸 번호 + 반 칸 오프셋 → 반 칸 단위 앵커. */
	SNAREHAVEN_API FIntPoint JoinHalfUnits(const FIntPoint& Cell, const FIntPoint& Half);

	/** 반 칸 단위 앵커의 발자국 월드 가운데. */
	SNAREHAVEN_API FVector FootprintCenterHalf(const FIntPoint& AnchorUnits, const FIntPoint& Size, float CellSizeCm, float Z);

	/** 반 칸 단위 앵커부터 발자국(칸 수 ×2)이 덮는 반 칸 전부. */
	SNAREHAVEN_API void FootprintHalfUnits(const FIntPoint& AnchorUnits, const FIntPoint& Size, TArray<FIntPoint>& OutUnits);

	/** `EvaluateStructure` 의 반 칸 판. `Occupied` 와 땅 안 판정이 모두 반 칸 단위. */
	SNAREHAVEN_API ESnarePlacementVerdict EvaluateStructureHalf(const FIntPoint& AnchorUnits, const FIntPoint& Size,
		const FIntPoint* TentCell, int32 LandCells, const TSet<FIntPoint>& OccupiedUnits);

	/** 벽 장식이 걸릴 집 발자국 한 변 — 서버 `wall.side` 와 같은 번호 (0=+Y · 1=+X · 2=-Y · 3=-X). 집 = 텐트 칸이 앵커인 `HouseCells`×`HouseCells`. */
	SNAREHAVEN_API bool PickWallSlot(const FVector2D& Point, const FIntPoint& TentCell, int32 HouseCells, float CellSizeCm,
		FIntPoint& OutCell, int32& OutSide);

	/** 벽 칸 변의 가운데 월드 위치(높이 `HeightCm`)와 벽이 바라보는 바깥 방향 yaw(도). 장식은 이 위치에서 바깥 쪽을 본다. */
	SNAREHAVEN_API FVector WallSlotLocation(const FIntPoint& Cell, int32 Side, float CellSizeCm, float HeightCm, float& OutYawDegrees);

	/** 벽 장식 높이 단 (cm, 낮은 순) — 놓기 바 「높이」가 이 순서로 돈다. 서버 `WALL_HEIGHT_MIN`~`MAX` 안 (DESIGN 8.11 벽 걸기). */
	constexpr int32 WallHeightStops[] = { 120, 150, 200 };
	/** 놓기를 켤 때 높이 — 눈높이 150 (DESIGN 8.11 벽 걸기 근거). */
	constexpr int32 WallHeightDefault = 150;
	/** 같은 칸·같은 변 두 장식의 높이 간격 하한 — 서버 `WALL_ITEM_GAP`. */
	constexpr int32 WallItemGapCm = 50;

	/**
	 * 2층 바닥 윗면 높이 — 집 바닥 + 이만큼 (M108-06c, DESIGN 8.11 「계단」 🟡). 1층 벽 260 + 판 두께 15 — 집 메시
	 * (`make_house_meshes.py` `TENT_HOUSE_DIMS`·`UPPER_FLOOR_T`)와 묶인 치수라 규칙 표가 아니라 여기 둔다(메시만 바꾸면 어긋난다).
	 */
	constexpr float UpperFloorZCm = 275.f;

	/**
	 * 층 가름 문턱 — 발 높이가 집 바닥 + 이만큼 이상이면 2층에 섰다 (M108-06e, DESIGN 8.11 「계단·위층」 🟡). 1층(0)과
	 * 2층(275) 사이, 계단 중간(137)보다 높아 오르는 도중엔 1층 보기 — 꼭대기 근처에서 2층 보기로 바뀐다.
	 */
	constexpr float UpperFloorSplitCm = 200.f;

	inline bool IsOnUpperFloor(float FeetAboveHouseCm)
	{
		return FeetAboveHouseCm >= UpperFloorSplitCm;
	}

	/**
	 * 문 앞 디딤 자리 (M108-06f-3) — 집 발자국 가운데에서 앞(-X, 문 쪽) 변 한 칸 밖의 집 공간 X. 계단이 치워진 순간
	 * 2층에 섰던 폰을 여기로 옮긴다(갇힘 없음 — 호스트 판정). 앞벽 바깥면(165)·캡슐 반지름(약 42)을 비키는 자리 —
	 * 3×3·칸 100 이면 -250 (메시 치수에 묶인 모양 값, 규칙 숫자 아님).
	 */
	inline float HouseDoorStepX(int32 HouseCellsX, float CellSizeCm)
	{
		return -(HouseCellsX * 0.5f + 1.f) * CellSizeCm;
	}

	/**
	 * 계단 비탈(도) — 오르막 `UpperFloorZCm` ÷ 수평 `RunCm`(계단 발자국 긴 변). 3칸(300)이면 42.5°
	 * (M108-06d, DESIGN 8.11 「계단·위층」 🟡). 캐릭터 걸을 수 있는 비탈(엔진 기본 44.77°)보다 낮아야 걸어 오른다.
	 */
	inline float StairsSlopeDegrees(float RunCm)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(UpperFloorZCm, FMath::Max(KINDA_SMALL_NUMBER, RunCm)));
	}

	/** 다음 높이 단 (마지막 다음은 처음). 단에 없는 값이면 그보다 높은 첫 단. */
	SNAREHAVEN_API int32 NextWallHeight(int32 HeightCm);

	/**
	 * 벽 장식 액터 돌림(도, 0~345) — 메시 앞(+Y)이 **방 안쪽**을 본다(바깥 yaw + 180 − 90). 방 안 장식이라 지붕을 숨긴
	 * 집 안 보기(DESIGN 8.11 「집 실내」)에서 뒷벽(+X) 장식이 카메라 쪽을 본다.
	 */
	SNAREHAVEN_API int32 WallDecorRotation(int32 Side);

	/** 이미 걸린 장식 하나 (같은 집의 칸·변·높이). */
	struct FWallHung
	{
		FIntPoint Cell = FIntPoint::ZeroValue;
		int32 Side = 0;
		int32 HeightCm = 0;
	};

	/**
	 * 벽 장식 미리보기 색 — 서버 `wallHangFail` 과 같은 줄. 집이 아니거나 벽 칸·높이 범위 밖이면 파랑,
	 * 같은 칸·같은 변에 높이 차가 `WallItemGapCm` 보다 작은 장식이 있으면 빨강.
	 */
	SNAREHAVEN_API ESnarePlacementVerdict EvaluateWallHang(const FIntPoint& Cell, int32 Side, int32 HeightCm,
		const FIntPoint& TentCell, int32 HouseCells, bool bHouse, const TArray<FWallHung>& Hung);

	/**
	 * 위에 올리기 슬롯 가운데 (M108-03) — 받침 메시 긴 변을 `SlotCount` 같은 폭으로 나눈 칸 가운데를 긴 변 길이에 대한 비로
	 * 돌려준다(가운데 0, -0.5~0.5). 탁자 3 → -1/3·0·1/3, 보관함 2 → -1/4·1/4. 범위 밖 슬롯은 0.
	 */
	SNAREHAVEN_API float TopSlotAlong(int32 Slot, int32 SlotCount);

	/** 놓기 바 「자리」 다음 슬롯 (마지막 다음은 처음). 슬롯이 없으면 -1. */
	SNAREHAVEN_API int32 NextTopSlot(int32 Slot, int32 SlotCount);

	/**
	 * 소품 미리보기 색 — 서버 `topPlaceFail` 과 같은 줄. 받침이 없거나 슬롯 범위 밖이면 파랑, 그 슬롯에 이미 소품이 있으면 빨강.
	 * @param Taken 같은 받침에 이미 올라간 슬롯들.
	 */
	SNAREHAVEN_API ESnarePlacementVerdict EvaluateTopPlace(int32 Slot, int32 SlotCount, const TArray<int32>& Taken);

	/** 90° 단위 회전이 발자국 가로·세로를 바꾼다 (90·270 이면 뒤바뀜). 0 이하 칸은 1 로 본다. */
	SNAREHAVEN_API FIntPoint RotatedFootprint(int32 FootprintX, int32 FootprintY, int32 RotationDegrees);

	/** 세밀 회전 한 칸 (DESIGN 8.11 「건축가 꾸미기」 — 15°, 24단계). 서버 `PlaceStructure` 가 15 의 배수 0~345 만 받는다. */
	constexpr int32 FineRotationStepDegrees = 15;
	constexpr int32 FineRotationSteps = 24;

	/** 15° 단위로 맞춘다 (0~345). 90° 돌리기도 이 안의 값이다. */
	SNAREHAVEN_API int32 SnapRotation(int32 RotationDegrees);
	SNAREHAVEN_API int32 SnapFineRotation(int32 RotationDegrees);

	/** 90° 단위로 맞춘다 (0·90·180·270) — 발자국 가로·세로 바꿈 판정용(세밀 회전 중에도 칸 판정은 가까운 90°). */
	SNAREHAVEN_API int32 SnapQuarterRotation(int32 RotationDegrees);

	/** 앵커부터 발자국이 덮는 칸 전부. */
	SNAREHAVEN_API void FootprintCells(const FIntPoint& AnchorCell, const FIntPoint& Size, TArray<FIntPoint>& OutCells);

	/** 이 칸이 이 텐트의 땅 안인가. */
	SNAREHAVEN_API bool IsInsideLand(const FIntPoint& Cell, const FIntPoint& TentCell, int32 LandCells);

	/**
	 * 가구 미리보기 색. 빨강(겹침)이 파랑(땅 밖)보다 먼저다 — 겹치면 땅 안팎을 따질 것도 없이 못 놓는다.
	 * @param TentCell 가구가 들어갈 내 텐트 칸 (없으면 nullptr → 파랑)
	 * @param Occupied 이미 차 있는 칸 (다른 가구·텐트 발자국)
	 */
	SNAREHAVEN_API ESnarePlacementVerdict EvaluateStructure(const FIntPoint& AnchorCell, const FIntPoint& Size,
		const FIntPoint* TentCell, int32 LandCells, const TSet<FIntPoint>& Occupied);

	/**
	 * 텐트 미리보기 색. 거점 존이 아니거나 한도면 파랑, 내 다른 텐트 땅과 겹치면 빨강 (서버 `|dx| < LandCells`).
	 */
	SNAREHAVEN_API ESnarePlacementVerdict EvaluateTent(const FIntPoint& Cell, bool bInBaseZone,
		const TArray<FIntPoint>& OwnTentCells, int32 TentMax, int32 LandCells);

	/** 땅 하나 가구 한도 — 2층 집이면 `FurnitureMaxFloor2`(60), 아니면 `FurnitureMax`(40). 서버 `furnitureMaxOf` 와 같은 줄. */
	SNAREHAVEN_API int32 FurnitureMaxFor(const FBaseRuleRow& Rules, int32 Floors);

	/** 땅 하나 가구 몫 — 바닥 가구 1개 = 1, 벽 장식·소품 1개 = 0.5 (DESIGN 8.11 「가구 한도」, 서버 `furnitureLoad` 와 같은 줄). */
	SNAREHAVEN_API float FurnitureLoad(int32 FloorCount, int32 HungOrTopCount);

	/** 땅 하나에 가구를 하나 더 놓을 수 있는가 — 몫(`FurnitureLoad`) < 한도 (DESIGN 8.11 = 40, 2층 60 — 숫자는 표). */
	SNAREHAVEN_API bool HasFurnitureRoom(float Load, const FBaseRuleRow& Rules, int32 Floors = 1);

	/**
	 * 땅 하나 공급 합 (DESIGN 8.11 「집 2층·전력」) — 키트마다 `SolarKitPower`, 배터리는 키트가 1개 이상일 때만 `BatteryPower`,
	 * 연료가 남은 발전기마다 `GeneratorPower`(M108-07c — 키트 없이도, 발전기 줄이 비면 0).
	 */
	SNAREHAVEN_API int32 PowerSupply(const FBaseRuleRow& Rules, int32 SolarKits, int32 Batteries, int32 RunningGenerators = 0);

	/** 땅 하나 소모 합 (M108-08c, DESIGN 8.11 「🟡 전기 가구」) — 전등 × `LampPowerUse` + 전기 가구 `PowerUse` 합. 전력 줄이 비면 0. */
	SNAREHAVEN_API int32 LandPowerUse(const FBaseRuleRow& Rules, int32 Lamps, int32 ElectricUse = 0);

	/** 땅 하나가 켜질 만한가 — 소모가 있고 공급 합 ≥ 소모 합 (8.11 한 줄 판정, 모자라면 전등·전기 가구 전부 꺼짐). 전력 줄이 비면 false. */
	SNAREHAVEN_API bool IsLandPowered(const FBaseRuleRow& Rules, int32 Supply, int32 Use);

	/** 전등만 있는 땅의 옛 꼴 — `IsLandPowered(Rules, PowerSupply(…), LandPowerUse(Rules, Lamps))`. */
	SNAREHAVEN_API bool IsLandPowered(const FBaseRuleRow& Rules, int32 SolarKits, int32 Batteries, int32 Lamps,
		int32 RunningGenerators = 0);

	/**
	 * 서버가 설치를 확정했을 때 그 자리에서 한 번 나는 「쿵」 (M28-03, DESIGN 8.11 「설치 확정 소리」).
	 * 텐트·가구 놓기·집 올리기·씨앗 심기가 같은 소리 — 쿵 소리 큐가 아직 없어 나무 두드림 `Chop` 을 빌렸다(🟡).
	 */
	SNAREHAVEN_API FName PlaceConfirmCue();

	/** 서버가 철거·칠하기를 확정했을 때 그 물건 자리에서 한 번 나는 소리 — 철거 `BuildRemove` · 칠하기 `Paint` (M45-02). */
	SNAREHAVEN_API FName EditConfirmCue(ESnareStructureEdit Edit);

	/** 그 소리 크기 — 철거 1.0(설치 확인과 같은 무게) · 칠하기 0.8 (DESIGN 2.2 「거점·상자 소리」 크기 줄). */
	SNAREHAVEN_API float EditConfirmVolume(ESnareStructureEdit Edit);

	/** 보관함 넣기·빼기(집·배)·유지 나무 넣기를 서버가 받아 준 뒤 내 기기에서 한 번 — 나무 뚜껑 덜컥 `Storage` (M45-03). */
	SNAREHAVEN_API FName StorageConfirmCue();

	/** 그 소리 크기 — 0.7 = 판 안 일이라 줍기 `Pickup` 과 같게 (DESIGN 2.2 「거점·상자 소리」 크기 줄). */
	SNAREHAVEN_API float StorageConfirmVolume();
}
