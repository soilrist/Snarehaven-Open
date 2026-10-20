#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RanchComponent.generated.h"

// ROADMAP M6-05b: 거점 슬롯 한 칸이 무엇을 기르는가 (DESIGN 8 확장 2 · 8.2 · 결정 005-2).
// **새 컴포넌트를 만들지 않는다** — 목장 칸도 밭도 같은 `URanchComponent` 의 한 칸이고 이 값만 갈린다.
UENUM(BlueprintType)
enum class ERanchSlotKind : uint8
{
	/** 동물 생산 칸. 펫을 넣으면 `DT_RanchProductTable` 의 주기마다 생산물이 쌓인다 (DESIGN 3.2). */
	AnimalProduct,
	/** 작물 칸. 심은 작물이 `DT_CropProductTable` 의 재배 시간마다 쌓인다 (DESIGN 8.2). */
	Crop,
	/** 낚시 칸. 낚이는 어종이 `DT_FishingTable` 의 낚시 시간마다 쌓인다 (DESIGN 8.4 · M10-02). */
	Fishing
};

// ROADMAP M10-02: 이 컴포넌트가 **어느 거점의 칸들**을 들고 있는가 (DESIGN 8.4).
// 낚시터는 새 메커니즘이 아니라 목장과 같은 짜임이라 컴포넌트를 새로 만들지 않았다 — 대신
// 백엔드에서 어느 키·어느 CloudScript 와 짝인지가 이 값으로 갈린다. 한 액터에 둘이 붙는다.
UENUM(BlueprintType)
enum class ERanchStationKind : uint8
{
	/** 목장·밭 — Player Data `ranch` 키, `RegisterPet`/`HarvestRanch` (DESIGN 3.2·8.2 · BACKEND.md). */
	Ranch,
	/** 낚시터 — Player Data `fishing` 키, `RegisterFishingSpot`/`HarvestFishing` 미러 (DESIGN 8.4). */
	Fishing
};

// 목장 슬롯 한 칸. 저장 항목은 DESIGN 4.5 가 적어 둔 그대로다 (`AnimalID`, `OccupiedSinceUtc`, `Stock`).
// **원본은 백엔드에 있다** — 이 구조체는 백엔드가 보내 준 것을 옮겨 담는 그릇이다 (DESIGN 4.1 권위 표).
USTRUCT(BlueprintType)
struct FRanchSlotState
{
	GENERATED_BODY()

	/** 이 칸이 동물 생산 칸인지 작물 칸인지 (M6-05b). 정하는 쪽은 백엔드 스냅샷이다. */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	ERanchSlotKind Kind = ERanchSlotKind::AnimalProduct;

	/** 이 칸에 배치된 동물의 `DT_AnimalTable` ID (예: `animal.pig`). 빈 칸이면 NAME_None. */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	FName AnimalID;

	/**
	 * 작물 칸에 심긴 작물 = `DT_CropProductTable` 의 `ProducedItemID` (예: `item.carrot`). 빈 칸이면 NAME_None.
	 * **`AnimalID` 와 한 칸으로 합치지 않았다** (🟡) — DESIGN 4.5 가 저장 항목 이름을 `AnimalID` 로 적어 뒀고,
	 * 그 칸에 `item.carrot` 을 담으면 이름이 거짓말을 한다. 읽는 쪽은 `GetOccupantID()` 하나로 본다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	FName CropItemID;

	/** 백엔드가 기록한 마지막 사이클 시작 시각(UTC). 남은 시간 표시에만 쓴다 — 계산은 백엔드가 한다. */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	FDateTime OccupiedSinceUtc = FDateTime::MinValue();

	/** 아직 수확하지 않고 쌓인 개수. 상한은 `DT_RanchProductTable.MaxStack`(백엔드가 자른다). */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	int32 Stock = 0;

	/** 색 변종 펫이 든 칸인지 (M21-07c2 · DESIGN 10.6). 서버 칸 `variant: true` 를 그대로 옮긴다 — 주기 ÷1.5 는 서버가 잰다. */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	bool bVariant = false;

	/**
	 * 낚시 칸에서 낚이는 어종 = `DT_FishingTable` 의 `ProducedItemID` (예: `item.crucian_carp`). 빈 칸이면 NAME_None.
	 * **`CropItemID` 에 같이 담지 않았다** — 작물 칸에 물고기가 들어 있으면 이름이 거짓말을 한다
	 * (`AnimalID` 와 `CropItemID` 를 가른 것과 같은 이유, 🟡 루프#122). 읽는 쪽은 `GetOccupantID()` 하나로 본다.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	FName FishItemID;

	/** 동물에게 씌운 꾸미기 아이템 ID (DESIGN 10.9, 예: `item.ranch_ribbon`). 없으면 NAME_None. 서버 칸 `cosmetic` 사본. */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	FName CosmeticItemID;

	/** 이 칸이 기르는 것의 ID — 동물 칸이면 `AnimalID`, 작물 칸이면 `CropItemID`, 낚시 칸이면 `FishItemID`. */
	FName GetOccupantID() const
	{
		switch (Kind)
		{
		case ERanchSlotKind::Crop:
			return CropItemID;
		case ERanchSlotKind::Fishing:
			return FishItemID;
		default:
			return AnimalID;
		}
	}

	/**
	 * 칸 종류가 쓰지 않는 ID 칸이 차 있나 (작물 칸에 펫 · 낚시 칸에 작물 …).
	 * 그런 스냅샷은 백엔드와 해석이 어긋난 것이라 `ApplyBackendSnapshot` 이 통째로 거절한다.
	 */
	bool HasForeignOccupantID() const
	{
		return (Kind != ERanchSlotKind::AnimalProduct && !AnimalID.IsNone())
			|| (Kind != ERanchSlotKind::Crop && !CropItemID.IsNone())
			|| (Kind != ERanchSlotKind::Fishing && !FishItemID.IsNone());
	}

	bool IsEmpty() const { return GetOccupantID().IsNone(); }
};

// 백엔드(`BACKEND.md`)의 CloudScript 에 그대로 대응한다. 새 종류를 늘리려면 그 표부터 늘린다.
UENUM(BlueprintType)
enum class ERanchRequestKind : uint8
{
	/** `RegisterPet(animalId, slot)` — 빈 칸에 포획한 펫을 넣어 달라. */
	RegisterPet,
	/** `HarvestRanch(slot)`(작물 칸이면 `HarvestCrop`) — 쌓인 생산물을 달라. 개수·코인은 백엔드가 계산한다. */
	Harvest,
	/** `PlantCrop(slot, seedItemId)` — 빈 칸에 씨앗 1개를 심어 달라 (⑩ · DESIGN 8.2). 씨앗을 빼는 것도 서버다. */
	PlantCrop,
	/** `RemovePet(slot)` — 칸의 펫을 빼 펫 아이템으로 돌려 달라 (#0198 · DESIGN 3.2). 쌓인 생산물은 서버가 먼저 거둔다. */
	RemovePet,
	/** `SetPetCosmetic(slot, itemId)` — 동물에게 꾸미기를 씌우거나(빈 `itemId` = 벗기기) 바꿔 달라 (DESIGN 10.9). */
	SetCosmetic
};

// 백엔드에 보낼 요청 한 건. 인자는 `BACKEND.md` 표의 호출 인자와 같다.
USTRUCT(BlueprintType)
struct FRanchRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	ERanchRequestKind Kind = ERanchRequestKind::RegisterPet;

	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	int32 SlotIndex = INDEX_NONE;

	/** `RegisterPet` 일 때만 채운다. */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	FName AnimalID;

	/** `RegisterPet` 이 같은 종의 변종 펫(`item.pet_<종>_variant`)을 쓰는지 — 서버 인자 `variant: true` (M21-07c2). */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	bool bVariant = false;

	/** `PlantCrop` 일 때만 채운다 (`item.seed_<작물>`). */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	FName SeedItemID;

	/** `SetCosmetic` 일 때 씌울 꾸미기. NAME_None = 벗기기. */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	FName CosmeticItemID;
};

// ROADMAP M12-05: 목장 유지비·명성 (DESIGN 3.2 **유지비·명성** 표). GetBaseState 의 `ranchUpkeep` 을 옮겨 담는다.
// **전부 서버가 판정한 값이다** — 남은 시간도 서버가 응답 시각에 잘라 준 것을 그대로 보여 줄 뿐,
// 클라이언트가 기한까지 시계를 세지 않는다 (CLAUDE.md 8 · DESIGN 4.8).
USTRUCT(BlueprintType)
struct FRanchUpkeepState
{
	GENERATED_BODY()

	/** 백엔드에서 한 번이라도 받았나. 받기 전에는 화면에 아무것도 쓰지 않는다. */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	bool bReceived = false;

	/** 서버가 준 남은 유지 시간(시간, 내림). -1 = 유지가 아직 시작 전(첫 펫 배치 전). */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	int32 LeftHours = -1;

	/** 유지가 끊겨 목장이 잠겼나 — 배치·수확을 서버가 `locked` 로 거절한다. */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	bool bLocked = false;

	/** 명성 — 슬롯 확장에 쓰는 재화. 서버 Internal Data `ranchMeta.fame`. */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	int32 Fame = 0;

	/** 서버가 연 목장 칸 수 (초기 5 · 확장 상한은 서버 상수). */
	UPROPERTY(BlueprintReadOnly, Category = "Ranch")
	int32 SlotCap = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRanchCacheChangedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRanchRequestQueuedSignature, int32, SlotIndex);

// ROADMAP M2-04a: 플레이어 목장 슬롯의 **로컬 캐시 계층** (명세 `docs/specs/ranch-slot.md`).
//
// `UInventoryComponent` 와 같은 모양을 일부러 그대로 쓴다 — 목장 타이머·수확도 인벤토리와 같은
// "재화가 걸린 판정" 이라 권위가 백엔드에 있기 때문이다 (DESIGN 4.1 표 · CLAUDE.md 8). 길은 둘뿐이다:
//   1) `ApplyBackendSnapshot` — 백엔드가 보내 준 슬롯 상태로 캐시를 덮어쓴다. 칸 내용은 이것만 바꾼다
//   2) `QueueRegisterPet` / `QueueHarvest` — 요청을 대기열에 쌓는다. 칸은 그대로다
// **경과 시간으로 생산량을 계산하는 코드는 여기 없다.** `floor((now - occupiedSinceUtc) / 30분)` 은
// `HarvestRanch` CloudScript 의 일이다 (BACKEND.md). 클라이언트가 같은 계산을 하면 기기 시계로 재화를
// 만들 수 있다. 실제 전송은 M2-01·M2-03 이고, 그때 이 대기열을 읽어 간다.
// 목장은 개인 소유라 복제(Replicated)하지 않는다 (DESIGN 3.2·3.4).
// **M10-02 부터 낚시터도 이 컴포넌트다** — DESIGN 8.4 가 "목장·작물과 같은 짜임(거점 슬롯 ->
// 시간 경과 -> 수확), 새 메커니즘을 만들지 않는다" 라고 못 박았다. 한 플레이어에게 목장 하나·
// 낚시터 하나가 붙고 `StationKind` 로만 갈린다(어느 백엔드 키·어느 CloudScript 짝인지). 찾을 때는
// `FindStation(액터, 종류)` 로 — 같은 클래스라 `FindComponentByClass` 는 먼저 붙은 것을 준다.
// **M6-05b 부터 이 컴포넌트가 거점 슬롯 전부를 맡는다** — 동물 생산 칸과 작물 칸이 `FRanchSlotState::Kind`
// 로만 갈린다 (결정 005-2 · DESIGN 8-2). 작물 칸도 길은 그대로 둘이다: 내용은 `ApplyBackendSnapshot`
// 만 바꾸고, 재배 시간 경과·수확량은 백엔드가 판정한다. **⑩ 부터 심는 요청이 있다** — `QueuePlantCrop` →
// `PlantCrop` CloudScript 가 씨앗 1개를 빼고 빈 칸을 작물 칸으로 만든다. 빈 칸은 종류가 없다: 펫을 넣으면
// 동물 칸, 씨앗을 심으면 작물 칸이 된다 (DESIGN 8.2 〔가정〕).
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API URanchComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URanchComponent();

	/** 칸 수 기본값이 블루프린트에서 바뀌어도 캐시 길이가 따라오게 여기서 맞춘다. */
	virtual void OnRegister() override;

	/**
	 * 백엔드에서 받은 목장 상태로 캐시를 통째로 덮어쓴다 (충돌 시 항상 백엔드 우선 — DESIGN 4.5).
	 * 칸 수가 `SlotCount` 보다 적거나 성하지 않은 칸이 있으면 **하나도 반영하지 않는다** —
	 * 더 긴 것은 받는다: 서버가 명성으로 연 확장 칸이다 (M12-05 `ExpandRanchSlot`).
	 * 반쯤 반영된 목장은 "내 펫이 사라졌다" 로 보인다.
	 * @return 실제로 캐시를 바꿨으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	bool ApplyBackendSnapshot(const TArray<FRanchSlotState>& NewSlots);

	/**
	 * 빈 칸에 펫을 넣어 달라는 요청을 쌓는다. 칸은 바뀌지 않는다 (CLAUDE.md 8).
	 * 캐시에서 그 칸이 비어 있어야 하고, 그 칸에 대기 중인 요청이 이미 있으면 거절한다 —
	 * 백엔드 응답 전에 두 번 누르면 이중 지급으로 보인다.
	 * @param bVariant 같은 종의 변종 펫을 넣는다 (M21-07c2) — 펫이 있는지는 서버가 본다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	bool QueueRegisterPet(int32 SlotIndex, FName AnimalID, bool bVariant = false);

	/**
	 * 빈 칸에 씨앗을 심어 달라는 요청을 쌓는다 (⑩). 칸도 가방도 바뀌지 않는다 — 씨앗을 빼고 칸을 채우는 것은
	 * `PlantCrop` 응답 뒤 다시 받은 값이다 (CLAUDE.md 8). 씨앗을 가졌는지는 부르는 쪽이 가방 캐시로 거른다.
	 * 목장 거점에서만, 빈 칸에만, 칸마다 대기 한 건만.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	bool QueuePlantCrop(int32 SlotIndex, FName SeedItemID);

	/**
	 * 동물 칸의 펫을 빼 달라는 요청을 쌓는다 (#0198). 칸도 가방도 바뀌지 않는다 — 칸이 비고 펫 아이템이
	 * 돌아오는 것은 `RemovePet` 응답 뒤 다시 받은 값이다 (CLAUDE.md 8). 찬 동물 칸에만, 칸마다 대기 한 건만.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	bool QueueRemovePet(int32 SlotIndex);

	/**
	 * 동물 칸에 꾸미기를 씌우거나(`CosmeticItemID`) 벗겨(NAME_None) 달라는 요청을 쌓는다 (DESIGN 10.9).
	 * 칸도 가방도 바뀌지 않는다 — 바뀐 겉모습은 `SetPetCosmetic` 응답 뒤 다시 받은 값이다 (CLAUDE.md 8).
	 * 찬 동물 칸에만, 지금과 다른 값일 때만, 칸마다 대기 한 건만. 꾸미기를 가졌는지는 부르는 쪽이 가방 캐시로 거른다.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	bool QueueSetCosmetic(int32 SlotIndex, FName CosmeticItemID);

	/** 찬 칸의 생산물을 달라는 요청을 쌓는다. 지급 개수·코인은 백엔드가 정한다. */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	bool QueueHarvest(int32 SlotIndex);

	/** 백엔드가 요청을 처리해 새 스냅샷을 준 뒤 비운다 (인벤토리와 같은 이유로 자동으로 안 비운다). */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	void ClearPendingRequests();

	/** 그 칸의 요청만 뺀다 — 전송기(`URanchSyncComponent`)가 서버 답을 받은 한 건만 지운다. */
	void RemovePendingRequest(int32 SlotIndex);

	/** 백엔드가 확인해 준 슬롯들 (캐시). 길이는 `SlotCount` 이상 — 확장 칸만큼 길다. */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	const TArray<FRanchSlotState>& GetSlots() const { return Slots; }

	/** 슬롯 하나. 번호가 범위 밖이면 nullptr. */
	const FRanchSlotState* GetSlot(int32 SlotIndex) const
	{
		return Slots.IsValidIndex(SlotIndex) ? &Slots[SlotIndex] : nullptr;
	}

	/** 그 칸이 비어 있나. 범위 밖이면 false (없는 칸은 "빈 칸" 이 아니다). */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	bool IsSlotEmpty(int32 SlotIndex) const;

	/** 아직 백엔드에 보내지 않은 요청들. */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	const TArray<FRanchRequest>& GetPendingRequests() const { return PendingRequests; }

	/** 그 칸에 대기 중인 요청이 있나. */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	bool HasPendingRequest(int32 SlotIndex) const;

	/** 목장 칸 수. 초기 DESIGN 3.2 = 5, 서버가 확장 칸을 보내 오면 그만큼 (M12-05). 낚시터는 5 그대로. */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	int32 GetSlotCount() const { return FMath::Max(SlotCount, Slots.Num()); }

	/** 확장 전 기본 칸 수 — 다음 확장 비용 k 를 셀 때 쓴다 (서버 `RANCH_SLOT_COUNT` 와 같아야 한다). */
	int32 GetBaseSlotCount() const { return SlotCount; }

	/** 백엔드가 준 목장 유지·명성 상태로 덮어쓴다 (M12-05). 값을 고치지 않고 옮기기만 한다. */
	void ApplyBackendUpkeep(const FRanchUpkeepState& NewUpkeep);

	/** 백엔드가 확인해 준 유지·명성 상태 (M12-05). */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	const FRanchUpkeepState& GetUpkeep() const { return Upkeep; }

	/**
	 * 백엔드가 준 목장 데코(`GetBaseState.ranchDeco` — 종류 `fence`·`path`·`barn`·`sky` → 아이템 ID)로 덮어쓴다 (M35-05c).
	 * 값을 고치지 않고 옮기기만 한다 — 적용·해제 판정은 서버 `SetRanchDeco` 몫 (CLAUDE.md 8).
	 */
	void ApplyBackendDeco(const TMap<FName, FName>& NewDeco);

	/** 백엔드가 확인해 준 목장 데코 (종류 → 아이템 ID). 적용한 것이 없으면 비어 있다. */
	const TMap<FName, FName>& GetDeco() const { return Deco; }

	/** 이 컴포넌트가 목장 칸을 들고 있나, 낚시터 칸을 들고 있나 (M10-02). */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	ERanchStationKind GetStationKind() const { return StationKind; }

	/**
	 * 거점 종류를 정한다. **생성자·부착 시점 전용** — 이미 칸이 찬 뒤에 바꾸면 같은 캐시가
	 * 다른 백엔드 키의 것으로 읽힌다 (`ranch` 스냅샷을 `fishing` 으로 보내는 꼴).
	 */
	void SetStationKind(ERanchStationKind NewStationKind) { StationKind = NewStationKind; }

	/**
	 * 그 액터에 붙은 거점 컴포넌트 중 **종류가 맞는 것**을 돌려준다 (M10-02).
	 * `FindComponentByClass` 를 쓰면 안 된다 — 목장과 낚시터가 같은 클래스라 **먼저 붙은 것**이
	 * 잡히고, 낚시터 칸을 목장 UI 가 그리는 조용한 사고가 난다.
	 */
	static URanchComponent* FindStation(const AActor* Owner, ERanchStationKind Kind);

	/** 캐시가 바뀔 때. 슬롯 액터·UI(M2-04c·M2-06)가 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Ranch")
	FOnRanchCacheChangedSignature OnRanchCacheChanged;

	/** 요청이 쌓일 때. 백엔드 전송기(M2-01)가 이걸 들어 큐를 비운다. */
	UPROPERTY(BlueprintAssignable, Category = "Ranch")
	FOnRanchRequestQueuedSignature OnRanchRequestQueued;

protected:
	/** 초기 칸 수. DESIGN 3.2 "초기 목장 슬롯 수 5칸" — 늘리는 건 설계서부터 (DataDriven.md 3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranch")
	int32 SlotCount = 5;

	/** 이 거점이 목장인지 낚시터인지 (M10-02). 기본은 목장이라 이미 있는 부착·검사는 그대로다. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranch")
	ERanchStationKind StationKind = ERanchStationKind::Ranch;

private:
	/** 칸 내용은 `ApplyBackendSnapshot` 만 바꾼다 (engines/unreal.md 코드 규칙 1 · CLAUDE.md 8). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	TArray<FRanchSlotState> Slots;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	TArray<FRanchRequest> PendingRequests;

	/** 유지·명성 캐시는 `ApplyBackendUpkeep` 만 바꾼다 (칸과 같은 약속). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Ranch", meta = (AllowPrivateAccess = "true"))
	FRanchUpkeepState Upkeep;

	/** 목장 데코 캐시는 `ApplyBackendDeco` 만 바꾼다 (칸과 같은 약속). */
	UPROPERTY(VisibleAnywhere, Category = "Ranch")
	TMap<FName, FName> Deco;
};
