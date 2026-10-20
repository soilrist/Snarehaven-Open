#include "Building/SnareBuildRules.h"
#include "Data/BaseRuleRow.h"

namespace SnareBuildRules
{
	FIntPoint WorldToCell(const FVector& WorldLocation, float CellSizeCm)
	{
		if (CellSizeCm <= 0.f)
		{
			return FIntPoint::ZeroValue;
		}
		return FIntPoint(FMath::FloorToInt32(WorldLocation.X / CellSizeCm), FMath::FloorToInt32(WorldLocation.Y / CellSizeCm));
	}

	FVector FootprintCenter(const FIntPoint& AnchorCell, const FIntPoint& Size, float CellSizeCm, float Z)
	{
		return FVector((AnchorCell.X + Size.X * 0.5f) * CellSizeCm, (AnchorCell.Y + Size.Y * 0.5f) * CellSizeCm, Z);
	}

	bool PickWallSlot(const FVector2D& Point, const FIntPoint& TentCell, int32 HouseCells, float CellSizeCm,
		FIntPoint& OutCell, int32& OutSide)
	{
		if (CellSizeCm <= 0.f || HouseCells < 1)
		{
			return false;
		}
		const float MinX = TentCell.X * CellSizeCm;
		const float MinY = TentCell.Y * CellSizeCm;
		const float MaxX = (TentCell.X + HouseCells) * CellSizeCm;
		const float MaxY = (TentCell.Y + HouseCells) * CellSizeCm;
		// 네 변까지 거리 — 가장 가까운 변이 이긴다 (같으면 +Y, +X, -Y, -X 순).
		const float Dist[4] = { FMath::Abs(Point.Y - MaxY), FMath::Abs(Point.X - MaxX), FMath::Abs(Point.Y - MinY), FMath::Abs(Point.X - MinX) };
		int32 Best = 0;
		for (int32 Side = 1; Side < 4; ++Side)
		{
			if (Dist[Side] < Dist[Best])
			{
				Best = Side;
			}
		}
		OutSide = Best;
		const bool bAlongX = (Best == 0 || Best == 2);
		const int32 Along = FMath::Clamp(FMath::FloorToInt32((bAlongX ? Point.X : Point.Y) / CellSizeCm - (bAlongX ? TentCell.X : TentCell.Y)), 0, HouseCells - 1);
		const int32 Edge = (Best == 0 || Best == 1) ? HouseCells - 1 : 0;
		OutCell = bAlongX ? FIntPoint(TentCell.X + Along, TentCell.Y + Edge) : FIntPoint(TentCell.X + Edge, TentCell.Y + Along);
		return true;
	}

	FVector WallSlotLocation(const FIntPoint& Cell, int32 Side, float CellSizeCm, float HeightCm, float& OutYawDegrees)
	{
		const float CX = (Cell.X + 0.5f) * CellSizeCm;
		const float CY = (Cell.Y + 0.5f) * CellSizeCm;
		const float Half = CellSizeCm * 0.5f;
		switch (Side)
		{
		case 0: OutYawDegrees = 90.f; return FVector(CX, CY + Half, HeightCm);
		case 1: OutYawDegrees = 0.f; return FVector(CX + Half, CY, HeightCm);
		case 2: OutYawDegrees = -90.f; return FVector(CX, CY - Half, HeightCm);
		default: OutYawDegrees = 180.f; return FVector(CX - Half, CY, HeightCm);
		}
	}

	int32 NextWallHeight(int32 HeightCm)
	{
		for (const int32 Stop : WallHeightStops)
		{
			if (Stop > HeightCm)
			{
				return Stop;
			}
		}
		return WallHeightStops[0];
	}

	int32 WallDecorRotation(int32 Side)
	{
		float OutwardYaw = 0.f;
		WallSlotLocation(FIntPoint::ZeroValue, Side, 1.f, 0.f, OutwardYaw);
		return SnapRotation(FMath::RoundToInt32(OutwardYaw) + 90);
	}

	ESnarePlacementVerdict EvaluateWallHang(const FIntPoint& Cell, int32 Side, int32 HeightCm,
		const FIntPoint& TentCell, int32 HouseCells, bool bHouse, const TArray<FWallHung>& Hung)
	{
		const int32 OX = Cell.X - TentCell.X;
		const int32 OY = Cell.Y - TentCell.Y;
		const bool bOnEdge = (Side == 0 && OY == HouseCells - 1) || (Side == 2 && OY == 0)
			|| (Side == 1 && OX == HouseCells - 1) || (Side == 3 && OX == 0);
		const bool bInRange = HeightCm >= WallHeightStops[0] && HeightCm <= WallHeightStops[UE_ARRAY_COUNT(WallHeightStops) - 1];
		if (!bHouse || !bInRange || OX < 0 || OY < 0 || OX >= HouseCells || OY >= HouseCells || !bOnEdge)
		{
			return ESnarePlacementVerdict::Blue;
		}
		for (const FWallHung& Other : Hung)
		{
			if (Other.Cell == Cell && Other.Side == Side && FMath::Abs(Other.HeightCm - HeightCm) < WallItemGapCm)
			{
				return ESnarePlacementVerdict::Red;
			}
		}
		return ESnarePlacementVerdict::Green;
	}

	float TopSlotAlong(int32 Slot, int32 SlotCount)
	{
		if (SlotCount <= 0 || Slot < 0 || Slot >= SlotCount)
		{
			return 0.f;
		}
		return (static_cast<float>(Slot) + 0.5f) / static_cast<float>(SlotCount) - 0.5f;
	}

	int32 NextTopSlot(int32 Slot, int32 SlotCount)
	{
		return SlotCount > 0 ? (FMath::Max(Slot, -1) + 1) % SlotCount : -1;
	}

	ESnarePlacementVerdict EvaluateTopPlace(int32 Slot, int32 SlotCount, const TArray<int32>& Taken)
	{
		if (SlotCount <= 0 || Slot < 0 || Slot >= SlotCount)
		{
			return ESnarePlacementVerdict::Blue;
		}
		return Taken.Contains(Slot) ? ESnarePlacementVerdict::Red : ESnarePlacementVerdict::Green;
	}

	FIntPoint AnchorAhead(const FVector2D& Origin, const FVector2D& Forward, const FIntPoint& Size,
		float CellSizeCm, float ClearanceCm)
	{
		if (CellSizeCm <= 0.f)
		{
			return FIntPoint::ZeroValue;
		}
		const FVector2D Dir = Forward.GetSafeNormal().IsNearlyZero() ? FVector2D(1.f, 0.f) : Forward.GetSafeNormal();
		const FVector2D Half = FVector2D(FMath::Max(1, Size.X), FMath::Max(1, Size.Y)) * (CellSizeCm * 0.5f);
		// 바라보는 방향으로 잰 발자국 반 폭(상자의 지지 거리)만큼 더 밀어야 가까운 변이 ClearanceCm 에 온다.
		float Distance = ClearanceCm + FMath::Abs(Dir.X) * Half.X + FMath::Abs(Dir.Y) * Half.Y;
		FIntPoint Anchor = FIntPoint::ZeroValue;
		// 칸에 맞추면 가운데가 반 칸까지 몸 쪽으로 밀릴 수 있다 — 떨어진 거리를 재서 모자라면 반 칸씩 더 민다.
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const FVector2D Center = Origin + Dir * Distance;
			Anchor = FIntPoint(FMath::RoundToInt32(Center.X / CellSizeCm - Half.X / CellSizeCm),
				FMath::RoundToInt32(Center.Y / CellSizeCm - Half.Y / CellSizeCm));
			const FVector2D Min = FVector2D(Anchor) * CellSizeCm;
			const FVector2D Max = Min + Half * 2.f;
			const FVector2D Nearest(FMath::Clamp(Origin.X, Min.X, Max.X), FMath::Clamp(Origin.Y, Min.Y, Max.Y));
			if (FVector2D::Distance(Nearest, Origin) >= ClearanceCm)
			{
				break;
			}
			Distance += CellSizeCm * 0.5f;
		}
		return Anchor;
	}

	FIntPoint AnchorAheadHalf(const FVector2D& Origin, const FVector2D& Forward, const FIntPoint& Size,
		float CellSizeCm, float ClearanceCm)
	{
		if (CellSizeCm <= 0.f)
		{
			return FIntPoint::ZeroValue;
		}
		const float Unit = CellSizeCm * 0.5f;
		const FVector2D Dir = Forward.GetSafeNormal().IsNearlyZero() ? FVector2D(1.f, 0.f) : Forward.GetSafeNormal();
		const FVector2D Half = FVector2D(FMath::Max(1, Size.X), FMath::Max(1, Size.Y)) * (CellSizeCm * 0.5f);
		float Distance = ClearanceCm + FMath::Abs(Dir.X) * Half.X + FMath::Abs(Dir.Y) * Half.Y;
		FIntPoint Anchor = FIntPoint::ZeroValue;
		// 반 칸 단위로 맞추고, 가까운 변이 몸에서 ClearanceCm 밖이 될 때까지 반 칸씩 더 민다.
		for (int32 Step = 0; Step < 16; ++Step)
		{
			const FVector2D Center = Origin + Dir * Distance;
			Anchor = FIntPoint(FMath::RoundToInt32((Center.X - Half.X) / Unit), FMath::RoundToInt32((Center.Y - Half.Y) / Unit));
			const FVector2D Min = FVector2D(Anchor) * Unit;
			const FVector2D Max = Min + Half * 2.f;
			const FVector2D Nearest(FMath::Clamp(Origin.X, Min.X, Max.X), FMath::Clamp(Origin.Y, Min.Y, Max.Y));
			if (FVector2D::Distance(Nearest, Origin) >= ClearanceCm)
			{
				break;
			}
			Distance += Unit;
		}
		return Anchor;
	}

	void SplitHalfUnits(const FIntPoint& Units, FIntPoint& OutCell, FIntPoint& OutHalf)
	{
		OutCell = FIntPoint(FMath::FloorToInt32(Units.X * 0.5f), FMath::FloorToInt32(Units.Y * 0.5f));
		OutHalf = Units - OutCell * 2;
	}

	FIntPoint JoinHalfUnits(const FIntPoint& Cell, const FIntPoint& Half)
	{
		return Cell * 2 + FIntPoint(FMath::Clamp(Half.X, 0, 1), FMath::Clamp(Half.Y, 0, 1));
	}

	FVector FootprintCenterHalf(const FIntPoint& AnchorUnits, const FIntPoint& Size, float CellSizeCm, float Z)
	{
		const float Unit = CellSizeCm * 0.5f;
		return FVector((AnchorUnits.X + Size.X) * Unit, (AnchorUnits.Y + Size.Y) * Unit, Z);
	}

	void FootprintHalfUnits(const FIntPoint& AnchorUnits, const FIntPoint& Size, TArray<FIntPoint>& OutUnits)
	{
		OutUnits.Reset();
		for (int32 X = 0; X < Size.X * 2; ++X)
		{
			for (int32 Y = 0; Y < Size.Y * 2; ++Y)
			{
				OutUnits.Add(AnchorUnits + FIntPoint(X, Y));
			}
		}
	}

	ESnarePlacementVerdict EvaluateStructureHalf(const FIntPoint& AnchorUnits, const FIntPoint& Size,
		const FIntPoint* TentCell, int32 LandCells, const TSet<FIntPoint>& OccupiedUnits)
	{
		TArray<FIntPoint> Units;
		FootprintHalfUnits(AnchorUnits, Size, Units);
		for (const FIntPoint& Unit : Units)
		{
			if (OccupiedUnits.Contains(Unit))
			{
				return ESnarePlacementVerdict::Red;
			}
		}
		if (TentCell == nullptr || LandCells <= 0)
		{
			return ESnarePlacementVerdict::Blue;
		}
		// 땅은 칸 기준 [-Half, Half) — 반 칸 단위로는 ×2.
		const int32 HalfUnits = LandCells;
		for (const FIntPoint& Unit : Units)
		{
			const FIntPoint Delta = Unit - *TentCell * 2;
			if (Delta.X < -HalfUnits || Delta.X >= HalfUnits || Delta.Y < -HalfUnits || Delta.Y >= HalfUnits)
			{
				return ESnarePlacementVerdict::Blue;
			}
		}
		return ESnarePlacementVerdict::Green;
	}

	int32 SnapRotation(int32 RotationDegrees)
	{
		return SnapFineRotation(RotationDegrees);
	}

	int32 SnapFineRotation(int32 RotationDegrees)
	{
		const int32 Wrapped = ((RotationDegrees % 360) + 360) % 360;
		return (FMath::RoundToInt32(Wrapped / static_cast<float>(FineRotationStepDegrees)) % FineRotationSteps)
			* FineRotationStepDegrees;
	}

	int32 SnapQuarterRotation(int32 RotationDegrees)
	{
		const int32 Wrapped = ((RotationDegrees % 360) + 360) % 360;
		return (FMath::RoundToInt32(Wrapped / 90.f) % 4) * 90;
	}

	FIntPoint RotatedFootprint(int32 FootprintX, int32 FootprintY, int32 RotationDegrees)
	{
		const FIntPoint Size(FMath::Max(1, FootprintX), FMath::Max(1, FootprintY));
		const int32 Rotation = SnapQuarterRotation(RotationDegrees);
		return (Rotation == 90 || Rotation == 270) ? FIntPoint(Size.Y, Size.X) : Size;
	}

	void FootprintCells(const FIntPoint& AnchorCell, const FIntPoint& Size, TArray<FIntPoint>& OutCells)
	{
		OutCells.Reset();
		for (int32 X = 0; X < Size.X; ++X)
		{
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				OutCells.Add(AnchorCell + FIntPoint(X, Y));
			}
		}
	}

	bool IsInsideLand(const FIntPoint& Cell, const FIntPoint& TentCell, int32 LandCells)
	{
		// 서버와 같은 반열린 구간 — 16칸이면 텐트 칸 기준 -8 ~ +7.
		const int32 Half = LandCells / 2;
		const FIntPoint Delta = Cell - TentCell;
		return Delta.X >= -Half && Delta.X < Half && Delta.Y >= -Half && Delta.Y < Half;
	}

	ESnarePlacementVerdict EvaluateStructure(const FIntPoint& AnchorCell, const FIntPoint& Size,
		const FIntPoint* TentCell, int32 LandCells, const TSet<FIntPoint>& Occupied)
	{
		TArray<FIntPoint> Cells;
		FootprintCells(AnchorCell, Size, Cells);

		// ponytail: 서버는 앵커 칸만 겹침으로 본다 — 미리보기는 발자국 전체로 더 엄격하게 본다(빨강이 더 잦다).
		for (const FIntPoint& Cell : Cells)
		{
			if (Occupied.Contains(Cell))
			{
				return ESnarePlacementVerdict::Red;
			}
		}

		if (TentCell == nullptr || LandCells <= 0)
		{
			return ESnarePlacementVerdict::Blue;
		}

		for (const FIntPoint& Cell : Cells)
		{
			if (!IsInsideLand(Cell, *TentCell, LandCells))
			{
				return ESnarePlacementVerdict::Blue;
			}
		}

		return ESnarePlacementVerdict::Green;
	}

	ESnarePlacementVerdict EvaluateTent(const FIntPoint& Cell, bool bInBaseZone,
		const TArray<FIntPoint>& OwnTentCells, int32 TentMax, int32 LandCells)
	{
		if (!bInBaseZone || TentMax <= 0 || OwnTentCells.Num() >= TentMax)
		{
			return ESnarePlacementVerdict::Blue;
		}

		for (const FIntPoint& Other : OwnTentCells)
		{
			if (FMath::Abs(Other.X - Cell.X) < LandCells && FMath::Abs(Other.Y - Cell.Y) < LandCells)
			{
				return ESnarePlacementVerdict::Red;
			}
		}

		return ESnarePlacementVerdict::Green;
	}

	int32 FurnitureMaxFor(const FBaseRuleRow& Rules, int32 Floors)
	{
		return Floors >= 2 && Rules.HasFloor2Rules() ? Rules.FurnitureMaxFloor2 : Rules.FurnitureMax;
	}

	float FurnitureLoad(int32 FloorCount, int32 HungOrTopCount)
	{
		return FMath::Max(FloorCount, 0) + 0.5f * FMath::Max(HungOrTopCount, 0);
	}

	bool HasFurnitureRoom(float Load, const FBaseRuleRow& Rules, int32 Floors)
	{
		const int32 Max = FurnitureMaxFor(Rules, Floors);
		return Max > 0 && Load < Max;
	}

	int32 PowerSupply(const FBaseRuleRow& Rules, int32 SolarKits, int32 Batteries, int32 RunningGenerators)
	{
		if (!Rules.HasPowerRules())
		{
			return 0;
		}
		// 발전기는 연료로 돌아 키트와 상관없다. 키트가 없으면 배터리는 0 — 낮에 채워 줄 것이 없다.
		const int32 Generators = Rules.HasGeneratorRules() ? FMath::Max(RunningGenerators, 0) * Rules.GeneratorPower : 0;
		const int32 Solar = SolarKits > 0 ? SolarKits * Rules.SolarKitPower + FMath::Max(Batteries, 0) * Rules.BatteryPower : 0;
		return Solar + Generators;
	}

	int32 LandPowerUse(const FBaseRuleRow& Rules, int32 Lamps, int32 ElectricUse)
	{
		return Rules.HasPowerRules() ? FMath::Max(Lamps, 0) * Rules.LampPowerUse + FMath::Max(ElectricUse, 0) : 0;
	}

	bool IsLandPowered(const FBaseRuleRow& Rules, int32 Supply, int32 Use)
	{
		// 켤 것이 없는 땅은 「꺼짐」 — 옛 전등 판정(`Lamps > 0`)을 소모 합으로 넓힌 것.
		return Rules.HasPowerRules() && Use > 0 && Supply >= Use;
	}

	bool IsLandPowered(const FBaseRuleRow& Rules, int32 SolarKits, int32 Batteries, int32 Lamps, int32 RunningGenerators)
	{
		return IsLandPowered(Rules, PowerSupply(Rules, SolarKits, Batteries, RunningGenerators), LandPowerUse(Rules, Lamps));
	}

	FName PlaceConfirmCue()
	{
		return FName(TEXT("Chop"));
	}

	FName EditConfirmCue(ESnareStructureEdit Edit)
	{
		return Edit == ESnareStructureEdit::Remove ? FName(TEXT("BuildRemove")) : FName(TEXT("Paint"));
	}

	float EditConfirmVolume(ESnareStructureEdit Edit)
	{
		return Edit == ESnareStructureEdit::Remove ? 1.f : 0.8f;
	}

	FName StorageConfirmCue()
	{
		return FName(TEXT("Storage"));
	}

	float StorageConfirmVolume()
	{
		return 0.7f;
	}
}
