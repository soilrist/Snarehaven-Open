#include "Ranch/RanchComponent.h"
#include "Snarehaven.h"
#include "GameFramework/Actor.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	// M8-04: 분석 통로를 찾는 곳. 못 찾으면 **이벤트만 빠지고 요청은 그대로 쌓인다** — 분석이
	// 게임을 막지 않는다 (`docs/ANALYTICS.md` 표 B · 루프#111 과 같은 짜임).
	USnareBackendSubsystem* FindBackendForAnalytics(const UObject* WorldContext)
	{
		UGameInstance* const GameInstance = UGameplayStatics::GetGameInstance(WorldContext);
		return IsValid(GameInstance) ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
	}
}

URanchComponent* URanchComponent::FindStation(const AActor* Owner, ERanchStationKind Kind)
{
	if (!IsValid(Owner))
	{
		return nullptr;
	}

	// 같은 클래스가 둘 붙어 있으므로(목장·낚시터) 전부 훑어 종류로 고른다 — M10-02.
	for (UActorComponent* const Component : Owner->GetComponents())
	{
		URanchComponent* const Station = Cast<URanchComponent>(Component);
		if (Station != nullptr && Station->GetStationKind() == Kind)
		{
			return Station;
		}
	}

	return nullptr;
}

URanchComponent::URanchComponent()
{
	// 목장은 알림으로만 바뀐다 — 남은 시간조차 백엔드 값의 표시라 매 프레임 볼 것이 없다.
	PrimaryComponentTick.bCanEverTick = false;
}

void URanchComponent::OnRegister()
{
	Super::OnRegister();

	// 칸 수를 블루프린트에서 바꿔도 캐시 길이가 따라오게 여기서 맞춘다 —
	// 생성자에서 맞추면 기본값(5)으로 굳어 `SlotCount` 와 어긋난다.
	if (SlotCount > 0 && Slots.Num() != SlotCount)
	{
		Slots.SetNum(SlotCount);
	}
}

bool URanchComponent::ApplyBackendSnapshot(const TArray<FRanchSlotState>& NewSlots)
{
	if (SlotCount <= 0)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("목장 칸 수가 미정이라 스냅샷을 반영하지 않는다"));
		return false;
	}

	// 더 긴 스냅샷은 서버가 명성으로 연 확장 칸이다 (M12-05). 짧은 것만 칸이 사라진 것으로 보고 거절한다.
	if (NewSlots.Num() < SlotCount)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("목장 스냅샷이 %d 칸인데 목장은 %d 칸이다 — 반영하지 않는다"),
			NewSlots.Num(), SlotCount);
		return false;
	}

	for (int32 SlotIndex = 0; SlotIndex < NewSlots.Num(); ++SlotIndex)
	{
		const FRanchSlotState& Slot = NewSlots[SlotIndex];

		if (Slot.Stock < 0)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("목장 스냅샷에 미수확 개수가 음수(%d)인 칸이 있다 — 반영하지 않는다"),
				Slot.Stock);
			return false;
		}

		// M6-05b·M10-02: 칸 종류와 안 맞는 ID 가 들어 있으면 백엔드와 해석이 어긋난 것이다
		// (작물 칸에 펫 · 낚시 칸에 작물). 어느 쪽을 믿을지 여기서 정하지 않고 통째로 거절한다.
		if (Slot.HasForeignOccupantID())
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("목장 스냅샷 %d 번 칸의 종류와 ID 가 어긋난다 — 반영하지 않는다"),
				SlotIndex);
			return false;
		}

		// 빈 칸인데 시각·재고가 들어 있으면 백엔드와 해석이 어긋난 것이다. 반쯤 믿지 않는다.
		if (Slot.IsEmpty() && (Slot.Stock > 0 || Slot.OccupiedSinceUtc != FDateTime::MinValue()))
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("목장 스냅샷의 빈 칸에 재고·시각이 들어 있다 — 반영하지 않는다"));
			return false;
		}
	}

	Slots = NewSlots;
	OnRanchCacheChanged.Broadcast();
	return true;
}

void URanchComponent::ApplyBackendUpkeep(const FRanchUpkeepState& NewUpkeep)
{
	Upkeep = NewUpkeep;
	Upkeep.bReceived = true;
	OnRanchCacheChanged.Broadcast();
}

void URanchComponent::ApplyBackendDeco(const TMap<FName, FName>& NewDeco)
{
	// 폴링마다 같은 값이 오므로 바뀌었을 때만 알린다 — 듣는 쪽이 목장 겉모습을 다시 입힌다.
	if (NewDeco.OrderIndependentCompareEqual(Deco))
	{
		return;
	}
	Deco = NewDeco;
	OnRanchCacheChanged.Broadcast();
}

bool URanchComponent::QueueRegisterPet(int32 SlotIndex, FName AnimalID, bool bVariant)
{
	if (!Slots.IsValidIndex(SlotIndex) || AnimalID.IsNone())
	{
		return false;
	}

	// M6-05b·M10-02: 밭에도 낚시터에도 펫을 넣지 않는다. 밭은 `QueuePlantCrop` 으로 심는다(⑩).
	if (Slots[SlotIndex].Kind != ERanchSlotKind::AnimalProduct)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%d 번 칸은 동물 생산 칸이 아니다 — 펫 배치 요청을 쌓지 않는다"), SlotIndex);
		return false;
	}

	if (!Slots[SlotIndex].IsEmpty())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%d 번 칸에 이미 %s 가 있다 — 배치 요청을 쌓지 않는다"),
			SlotIndex, *Slots[SlotIndex].GetOccupantID().ToString());
		return false;
	}

	if (HasPendingRequest(SlotIndex))
	{
		return false;
	}

	FRanchRequest Request;
	Request.Kind = ERanchRequestKind::RegisterPet;
	Request.SlotIndex = SlotIndex;
	Request.AnimalID = AnimalID;
	Request.bVariant = bVariant;
	PendingRequests.Add(Request);

	// M8-04: **요청을 쌓은 것까지만** 센다. 배치가 실제로 성립했는지는 `RegisterPet` CloudScript 가
	// 답하고, 그 기록은 백엔드 원장이다 (`docs/ANALYTICS.md` 표 C · CLAUDE.md 8).
	if (USnareBackendSubsystem* const Backend = FindBackendForAnalytics(this))
	{
		TMap<FName, FString> Payload;
		Payload.Add(TEXT("slot_index"), FString::FromInt(SlotIndex));
		Payload.Add(TEXT("animal_id"), AnimalID.ToString());
		Backend->WriteGameEvent(TEXT("ranch_pet_register_requested"), Payload);
	}

	OnRanchRequestQueued.Broadcast(SlotIndex);
	return true;
}

bool URanchComponent::QueuePlantCrop(int32 SlotIndex, FName SeedItemID)
{
	// 낚시터 칸에는 심지 않는다. 빈 칸 확인·중복 대기 거절은 펫 배치와 같은 이유다.
	if (StationKind != ERanchStationKind::Ranch || !Slots.IsValidIndex(SlotIndex) || SeedItemID.IsNone()
		|| !Slots[SlotIndex].IsEmpty() || HasPendingRequest(SlotIndex))
	{
		return false;
	}

	FRanchRequest Request;
	Request.Kind = ERanchRequestKind::PlantCrop;
	Request.SlotIndex = SlotIndex;
	Request.SeedItemID = SeedItemID;
	PendingRequests.Add(Request);

	OnRanchRequestQueued.Broadcast(SlotIndex);
	return true;
}

bool URanchComponent::QueueRemovePet(int32 SlotIndex)
{
	// 펫이 선 동물 칸만 — 작물 칸·낚시 칸·빈 칸에는 뺄 펫이 없다.
	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].Kind != ERanchSlotKind::AnimalProduct
		|| Slots[SlotIndex].IsEmpty() || HasPendingRequest(SlotIndex))
	{
		return false;
	}

	FRanchRequest Request;
	Request.Kind = ERanchRequestKind::RemovePet;
	Request.SlotIndex = SlotIndex;
	PendingRequests.Add(Request);
	OnRanchRequestQueued.Broadcast(SlotIndex);
	return true;
}

bool URanchComponent::QueueSetCosmetic(int32 SlotIndex, FName CosmeticItemID)
{
	// 펫이 선 동물 칸만, 지금과 다른 값만 — 같은 값이면 서버가 `already` 로 거절할 요청이다.
	if (!Slots.IsValidIndex(SlotIndex) || Slots[SlotIndex].Kind != ERanchSlotKind::AnimalProduct
		|| Slots[SlotIndex].IsEmpty() || Slots[SlotIndex].CosmeticItemID == CosmeticItemID
		|| HasPendingRequest(SlotIndex))
	{
		return false;
	}

	FRanchRequest Request;
	Request.Kind = ERanchRequestKind::SetCosmetic;
	Request.SlotIndex = SlotIndex;
	Request.CosmeticItemID = CosmeticItemID;
	PendingRequests.Add(Request);
	OnRanchRequestQueued.Broadcast(SlotIndex);
	return true;
}

bool URanchComponent::QueueHarvest(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		return false;
	}

	if (Slots[SlotIndex].IsEmpty())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%d 번 칸이 비어 있다 — 수확 요청을 쌓지 않는다"), SlotIndex);
		return false;
	}

	if (HasPendingRequest(SlotIndex))
	{
		return false;
	}

	FRanchRequest Request;
	Request.Kind = ERanchRequestKind::Harvest;
	Request.SlotIndex = SlotIndex;
	PendingRequests.Add(Request);

	// M8-04: 칸은 **슬롯 번호 하나뿐**이다. 쌓인 재고(`Slots[SlotIndex].Stock`)를 여기 실으면
	// 클라이언트 캐시가 수확량의 출처가 된다 — 그 수는 `HarvestRanch` CloudScript 가 쓴다.
	// **낚시터 수확은 이 줄을 보내지 않는다**(M10-02) — `ranch_harvest_requested` 는 `docs/ANALYTICS.md`
	// 표 A 가 "목장 수확 요청" 이라고 적어 둔 이름이라, 낚시터가 같은 이름으로 보내면 표와 값이 어긋난다.
	// 낚시터 줄을 표에 더하는 것은 분석 조각(M8) 몫이지 이 조각 몫이 아니다.
	if (StationKind == ERanchStationKind::Ranch)
	{
		if (USnareBackendSubsystem* const Backend = FindBackendForAnalytics(this))
		{
			TMap<FName, FString> Payload;
			Payload.Add(TEXT("slot_index"), FString::FromInt(SlotIndex));
			Backend->WriteGameEvent(TEXT("ranch_harvest_requested"), Payload);
		}
	}

	OnRanchRequestQueued.Broadcast(SlotIndex);
	return true;
}

void URanchComponent::ClearPendingRequests()
{
	PendingRequests.Reset();
}

void URanchComponent::RemovePendingRequest(int32 SlotIndex)
{
	PendingRequests.RemoveAll([SlotIndex](const FRanchRequest& Request) { return Request.SlotIndex == SlotIndex; });
}

bool URanchComponent::IsSlotEmpty(int32 SlotIndex) const
{
	const FRanchSlotState* Slot = GetSlot(SlotIndex);
	return Slot != nullptr && Slot->IsEmpty();
}

bool URanchComponent::HasPendingRequest(int32 SlotIndex) const
{
	for (const FRanchRequest& Request : PendingRequests)
	{
		if (Request.SlotIndex == SlotIndex)
		{
			return true;
		}
	}

	return false;
}
