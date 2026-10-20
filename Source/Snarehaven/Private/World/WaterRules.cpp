#include "World/WaterRules.h"
#include "Snarehaven.h"
#include "Data/FishingRow.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "World/FishingSpotActor.h"
#include "World/SeaActor.h"

namespace SnareWater
{
	const TCHAR* const TablePath = TEXT("/Game/Game/Data/Balance/DT_WaterActivityTable.DT_WaterActivityTable");
	const TCHAR* const FishingTablePath = TEXT("/Game/Game/Data/Fishing/DT_FishingTable.DT_FishingTable");

	bool IsRowComplete(const FWaterActivityRow& Row)
	{
		const float Values[] = {
			Row.SeaWidthCm, Row.SwimSpeedMul, Row.SwimStaminaPerSecond, Row.DrownHealthPerSecond,
			Row.BoatSpeedMul, Row.BoatMotorMul, Row.BoatElectricMotorMul, static_cast<float>(Row.BoatSeats),
			Row.CastRangeCm, Row.BiteMinSeconds, Row.BiteMaxSeconds, Row.WormBiteMul,
			Row.TapGoal, Row.TapBasePresses, Row.TapMinutesDivisor, Row.TapDecayPerSecond, Row.TapFailSeconds,
			static_cast<float>(Row.SpotCatchesBeforeGone), Row.SpotRespawnSeconds,
			static_cast<float>(Row.FarSeaMinCycleMinutes), Row.MountSpeedMul, Row.MountLengthPerRiderWidth,
			Row.MountSeatBackOfLength, Row.MountWalkClipSpeedCmPerSecond, Row.MountRunClipSpeedCmPerSecond };
		for (const float Value : Values)
		{
			if (Value < 0.f)
			{
				return false;
			}
		}
		// 나누개·배율이 0 이면 식이 0 으로 나눈다 — 미정과 같게 본다.
		return Row.TapMinutesDivisor > 0.f && Row.TapBasePresses > 0.f && Row.SwimSpeedMul > 0.f
			&& Row.BoatSpeedMul > 0.f && Row.MountSpeedMul > 0.f && Row.MountLengthPerRiderWidth > 0.f
			&& Row.MountWalkClipSpeedCmPerSecond > 0.f && Row.MountRunClipSpeedCmPerSecond > 0.f && Row.BiteMaxSeconds >= Row.BiteMinSeconds;
	}

	const FWaterActivityRow* FindRow(const UDataTable* Table)
	{
		if (Table == nullptr)
		{
			return nullptr;
		}
		const FWaterActivityRow* const Row =
			Table->FindRow<FWaterActivityRow>(TEXT("Default"), TEXT("SnareWater::FindRow"));
		if (Row == nullptr || !IsRowComplete(*Row))
		{
			UE_LOG(LogSnarehaven, Warning,
				TEXT("DT_WaterActivityTable 의 Default 행이 없거나 미정 칸이 있다 — 수영·배·낚시를 끈다"));
			return nullptr;
		}
		return Row;
	}

	const FWaterActivityRow* LoadRow()
	{
		return FindRow(LoadObject<UDataTable>(nullptr, TablePath));
	}

	float BoatSpeedMul(const FWaterActivityRow& Row, int32 MotorTier)
	{
		const float MotorMul = MotorTier >= 2 ? Row.BoatElectricMotorMul : MotorTier == 1 ? Row.BoatMotorMul : 1.f;
		return Row.BoatSpeedMul * MotorMul;
	}

	float ProgressPerPress(const FWaterActivityRow& Row, float CycleMinutes)
	{
		return Row.TapGoal / (Row.TapBasePresses + CycleMinutes / Row.TapMinutesDivisor);
	}

	int32 PressesNeeded(const FWaterActivityRow& Row, float CycleMinutes)
	{
		// 부동소수 오차로 13.0000001 이 14 가 되지 않게 조금 깎고 올린다.
		return FMath::CeilToInt(Row.TapBasePresses + CycleMinutes / Row.TapMinutesDivisor - KINDA_SMALL_NUMBER);
	}

	float BiteWaitSeconds(const FWaterActivityRow& Row, float Alpha, float GearSpeedMul, bool bWormBait)
	{
		const float Base = FMath::Lerp(Row.BiteMinSeconds, Row.BiteMaxSeconds, FMath::Clamp(Alpha, 0.f, 1.f));
		const float Gear = GearSpeedMul > 0.f ? GearSpeedMul : 1.f;
		return Base / Gear * (bWormBait ? Row.WormBiteMul : 1.f);
	}

	bool FishFitsSpot(const FWaterActivityRow& Row, EFishingSpotKind Kind, const FFishingRow& Fish)
	{
		const bool bSeaFish = Fish.Habitat == EFishHabitat::Sea || Fish.Habitat == EFishHabitat::DeepSea;
		switch (Kind)
		{
		case EFishingSpotKind::Pond:
		case EFishingSpotKind::River:
			return Fish.Habitat == EFishHabitat::Freshwater;
		case EFishingSpotKind::Swamp:
			return Fish.Habitat == EFishHabitat::Freshwater || Fish.Habitat == EFishHabitat::Swamp;
		case EFishingSpotKind::Coast:
			return bSeaFish;
		case EFishingSpotKind::FarSea:
			return bSeaFish && Fish.CycleMinutes >= Row.FarSeaMinCycleMinutes;
		}
		return false;
	}

	const FFishingRow* FindFishByItemId(FName FishItemId)
	{
		const UDataTable* const Table = LoadObject<UDataTable>(nullptr, FishingTablePath);
		if (Table == nullptr || FishItemId.IsNone())
		{
			return nullptr;
		}
		const FFishingRow* Found = nullptr;
		Table->ForeachRow<FFishingRow>(TEXT("SnareWater::FindFishByItemId"),
			[&Found, FishItemId](const FName&, const FFishingRow& Fish)
			{
				if (Found == nullptr && Fish.ProducedItemID == FishItemId)
				{
					Found = &Fish;
				}
			});
		return Found;
	}

	bool SpotKindFromId(const FString& SpotId, EFishingSpotKind& OutKind)
	{
		if (SpotId.StartsWith(TEXT("pond_"))) { OutKind = EFishingSpotKind::Pond; return true; }
		if (SpotId.StartsWith(TEXT("river_"))) { OutKind = EFishingSpotKind::River; return true; }
		if (SpotId.StartsWith(TEXT("coast_"))) { OutKind = EFishingSpotKind::Coast; return true; }
		if (SpotId.StartsWith(TEXT("far_"))) { OutKind = EFishingSpotKind::FarSea; return true; }
		if (SpotId.StartsWith(TEXT("swamp_"))) { OutKind = EFishingSpotKind::Swamp; return true; }
		return false;
	}

	USnareBackendSubsystem* FindBackend(const UObject* WorldContext)
	{
		const UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
		UGameInstance* const GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	}

	void CallServer(const UObject* WorldContext, const FString& FunctionName,
		const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone Done)
	{
		// 서버 `fail()` 은 스크립트 오류가 아니라 `{error, code}` 를 **성공 응답으로** 돌려준다 — 여기서 실패로 바꾼다.
		USnareBackendSubsystem::FServerCallDone Wrapped =
			[Done = MoveTemp(Done), FunctionName](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
			{
				FString Code;
				if (bOk && Response.IsValid() && Response->TryGetStringField(TEXT("code"), Code))
				{
					UE_LOG(LogSnarehaven, Log, TEXT("[Water] %s 거절: %s"), *FunctionName, *Code);
					Done(false, Response, Code);
					return;
				}
				Done(bOk, Response, Error);
			};

		if (USnareBackendSubsystem* const Backend = FindBackend(WorldContext))
		{
			Backend->CallServerFunction(FunctionName, Args, MoveTemp(Wrapped));
			return;
		}
		if (USnareBackendSubsystem::TestServerOverride)
		{
			USnareBackendSubsystem::TestServerOverride(FunctionName, Args, MoveTemp(Wrapped));
			return;
		}
		Wrapped(false, nullptr, TEXT("백엔드가 없다"));
	}

	void RefreshPlayerState(const UObject* WorldContext)
	{
		if (USnareBackendSubsystem* const Backend = FindBackend(WorldContext))
		{
			Backend->FetchPlayerState();
		}
	}

	int32 CountOwned(const UObject* WorldContext, FName ItemID)
	{
		const USnareBackendSubsystem* const Backend = FindBackend(WorldContext);
		if (Backend == nullptr)
		{
			return 0;
		}
		for (const FInventorySlot& Slot : Backend->GetPlayerSnapshot().InventorySlots)
		{
			if (Slot.ItemID == ItemID)
			{
				return Slot.Count;
			}
		}
		return 0;
	}

	bool IsNearWater(const UObject* WorldContext, const FVector& From, float RangeCm)
	{
		// 해안까지 거리가 RangeCm 안이면 물가다(여러 섬이면 가장 가까운 섬 해안).
		if (const ASeaActor* const Sea = ASeaActor::FindSea(WorldContext))
		{
			if (Sea->BuiltSeaWidthCm > 0.f && Sea->CoastDistance(From) >= -RangeCm)
			{
				return true;
			}
		}
		UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (World == nullptr)
		{
			return false;
		}
		// 물고기가 다 잡혀도 물은 있다 — 남은 수는 보지 않는다.
		for (TActorIterator<AFishingSpotActor> It(World); It; ++It)
		{
			if (FVector::Dist2D(It->GetActorLocation(), From) <= RangeCm)
			{
				return true;
			}
		}
		return false;
	}
}
