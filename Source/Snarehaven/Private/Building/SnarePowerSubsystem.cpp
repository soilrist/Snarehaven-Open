#include "Building/SnarePowerSubsystem.h"

#include "Building/SnareBaseSubsystem.h"
#include "Building/SnareBuildRules.h"
#include "Building/SnarePlacedStructureActor.h"
#include "Data/ItemRow.h"
#include "Data/BaseRuleRow.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Snarehaven.h"
#include "World/SnareDayNight.h"

namespace
{
	/** 놓기·치우기·해질녘을 몇 초 안에 따라가면 충분하다 — 매 프레임 세지 않는다 (engines/unreal.md 코드 규칙 2). */
	constexpr float CheckIntervalSeconds = 0.5f;

	struct FLandCount
	{
		int32 SolarKits = 0;
		int32 Batteries = 0;
		int32 RunningGenerators = 0;
		int32 ElectricUse = 0;
		TArray<ASnarePlacedStructureActor*> Lamps;
		TArray<ASnarePlacedStructureActor*> Flames;
		TArray<ASnarePlacedStructureActor*> Electrics;
	};

	/** 상한 안에서 누가 켜질지 모든 기기가 같게 — 서버가 준 가구 ID 순. */
	void SortByStructureId(TArray<ASnarePlacedStructureActor*>& Actors)
	{
		Actors.Sort([](const ASnarePlacedStructureActor& A, const ASnarePlacedStructureActor& B)
		{
			return A.GetStructureId() < B.GetStructureId();
		});
	}

	/** 불꽃 색 = DESIGN 3.3 「밤 불빛」 모닥불 (255,120,40). 전구 주황(255,160,80)보다 붉다. */
	const FLinearColor FlameLightColor(FColor(255, 120, 40));
}

bool USnarePowerSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId USnarePowerSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USnarePowerSubsystem, STATGROUP_Tickables);
}

void USnarePowerSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	SinceLastCheck += DeltaTime;
	if (SinceLastCheck < CheckIntervalSeconds)
	{
		return;
	}
	SinceLastCheck = 0.f;

	UWorld* const World = GetWorld();
	const UGameInstance* const GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
	const USnareBaseSubsystem* const Base = GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBaseSubsystem>() : nullptr;
	const FBaseRuleRow* const Rules = Base != nullptr ? Base->GetRules() : nullptr;
	const USnareDayNightSubsystem* const DayNight = World != nullptr ? World->GetSubsystem<USnareDayNightSubsystem>() : nullptr;
	if (Rules == nullptr || DayNight == nullptr)
	{
		return;
	}
	// 거리 불빛과 같은 문턱 — 밤 0.5 를 넘을 때 켠다 (DESIGN 3.3 「밤 불빛」).
	const int32 Lit = ApplyPower(World, *Rules, DayNight->GetNightAmount() >= 0.5f);
	if (Lit != LastLitCount)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Power] 켜진 전등·불꽃 %d"), Lit);
		LastLitCount = Lit;
	}
}

int32 USnarePowerSubsystem::ApplyPower(UWorld* World, const FBaseRuleRow& Rules, bool bNight)
{
	const bool bPowerRules = Rules.HasPowerRules();
	const bool bFlameRules = Rules.HasFlameRules();
	if (World == nullptr || (!bPowerRules && !bFlameRules))
	{
		return 0;
	}
	// 땅 = 주인 + 텐트 ID — 같은 방의 두 사람 텐트 ID 가 우연히 같아도 섞이지 않게.
	TMap<FString, FLandCount> Lands;
	for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
	{
		if (It->IsPreview() || It->GetTentId().IsEmpty())
		{
			continue;
		}
		const FName ItemID = It->GetItemID();
		const bool bFlame = It->GetPlaceableFunction() == EPlaceableFunction::FlameLight;
		// 발전기는 연료가 남은 것만 센다 — 끝 시각은 서버 `fuelUntilUtc` 를 게임 서버 시계로 옮긴 복제 값 (M108-07c).
		const bool bGenerator = bPowerRules && Rules.HasGeneratorRules() && ItemID == Rules.GeneratorItemID;
		if (bGenerator && !It->IsGeneratorRunning())
		{
			continue;
		}
		// 전기 가구(행 `PowerUse` > 0, M108-08c) — 라디오·간판은 소품·벽 장식이면서 전기라 행 기능이 아닌 소모 칸으로 가른다.
		const bool bElectric = bPowerRules && It->GetPowerUse() > 0;
		const bool bPowerItem = bPowerRules
			&& (ItemID == Rules.SolarKitItemID || ItemID == Rules.BatteryItemID || ItemID == Rules.LampItemID || bGenerator || bElectric);
		if (!bFlame && !bPowerItem)
		{
			continue;
		}
		FLandCount& Land = Lands.FindOrAdd(It->GetOwnerPlayFabId() + TEXT("/") + It->GetTentId());
		if (bFlame)
		{
			Land.Flames.Add(*It);
		}
		else if (bElectric)
		{
			Land.ElectricUse += It->GetPowerUse();
			Land.Electrics.Add(*It);
		}
		else if (ItemID == Rules.SolarKitItemID)
		{
			++Land.SolarKits;
		}
		else if (ItemID == Rules.BatteryItemID)
		{
			++Land.Batteries;
		}
		else if (bGenerator)
		{
			++Land.RunningGenerators;
		}
		else
		{
			Land.Lamps.Add(*It);
		}
	}

	int32 Lit = 0;
	for (TPair<FString, FLandCount>& Pair : Lands)
	{
		FLandCount& Land = Pair.Value;
		// 땅 하나 점광 상한(8.11 「비전기 조명」 모바일 동적 빛 예산) — 전등이 먼저(전력을 들여 켠 것), 남은 자리에 불꽃.
		int32 Left = bFlameRules ? Rules.LandLightMax : MAX_int32;
		// 한 줄 판정(8.11) — 공급 ≥ 전등 + 전기 가구 소모 합이면 땅 전체 켜짐, 모자라면 전등까지 전부 꺼짐.
		const bool bLandOn = bPowerRules && SnareBuildRules::IsLandPowered(Rules,
			SnareBuildRules::PowerSupply(Rules, Land.SolarKits, Land.Batteries, Land.RunningGenerators),
			SnareBuildRules::LandPowerUse(Rules, Land.Lamps.Num(), Land.ElectricUse));
		const bool bLampsOn = bLandOn && bNight;
		// 전기 가구는 낮밤 없이(「🟡 전기 가구」) — 점광이 없어 상한 `Left` 를 쓰지 않는다.
		for (ASnarePlacedStructureActor* const Electric : Land.Electrics)
		{
			Electric->SetElectricOn(bLandOn);
		}
		SortByStructureId(Land.Lamps);
		for (ASnarePlacedStructureActor* const Lamp : Land.Lamps)
		{
			const bool bOn = bLampsOn && Left > 0;
			if (Lamp->IsLampLit() != bOn)
			{
				Lamp->SetLampLit(bOn, Rules.LampLightCandela, Rules.LampLightRadiusCm);
			}
			Left -= bOn ? 1 : 0;
			Lit += bOn ? 1 : 0;
		}
		SortByStructureId(Land.Flames);
		for (ASnarePlacedStructureActor* const Flame : Land.Flames)
		{
			const bool bOn = bFlameRules && bNight && Left > 0;
			if (Flame->IsLampLit() != bOn)
			{
				Flame->SetLampLit(bOn, Rules.FlameLightCandela, Rules.FlameLightRadiusCm, FlameLightColor);
			}
			Left -= bOn ? 1 : 0;
			Lit += bOn ? 1 : 0;
		}
	}
	return Lit;
}

void USnarePowerSubsystem::MeasureLand(UWorld* World, const FBaseRuleRow& Rules, const FString& OwnerPlayFabId, const FString& TentId,
	int32& OutSupply, int32& OutUse)
{
	OutSupply = 0;
	OutUse = 0;
	if (World == nullptr || !Rules.HasPowerRules() || TentId.IsEmpty())
	{
		return;
	}
	const bool bGeneratorRules = Rules.HasGeneratorRules();
	int32 SolarKits = 0;
	int32 Batteries = 0;
	int32 RunningGenerators = 0;
	int32 Lamps = 0;
	int32 ElectricUse = 0;
	for (TActorIterator<ASnarePlacedStructureActor> It(World); It; ++It)
	{
		if (It->IsPreview() || It->GetTentId() != TentId || It->GetOwnerPlayFabId() != OwnerPlayFabId)
		{
			continue;
		}
		const FName ItemID = It->GetItemID();
		SolarKits += ItemID == Rules.SolarKitItemID ? 1 : 0;
		Batteries += ItemID == Rules.BatteryItemID ? 1 : 0;
		Lamps += ItemID == Rules.LampItemID ? 1 : 0;
		RunningGenerators += bGeneratorRules && ItemID == Rules.GeneratorItemID && It->IsGeneratorRunning() ? 1 : 0;
		ElectricUse += It->GetPowerUse();
	}
	OutSupply = SnareBuildRules::PowerSupply(Rules, SolarKits, Batteries, RunningGenerators);
	OutUse = SnareBuildRules::LandPowerUse(Rules, Lamps, ElectricUse);
}
