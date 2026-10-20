#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/ItemRow.h"
#include "Inventory/ChestLootRules.h"
#include "Inventory/InventoryComponent.h"
#include "World/SnareSound.h"
#include "BackpackComponent.generated.h"

class ADropBagActor;
class ALootChestActor;
class AController;
class UPlayerVitalsComponent;
class UDataTable;
class USnareBackendSubsystem;
class FJsonObject;

/** 서버 판정 하나가 끝났을 때. 화면은 이걸 듣고 상태 줄을 바꾼다 — 보유 칸은 스냅샷으로만 바뀐다. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnBackpackReplySignature, bool, bAccepted, const FString&, Code,
	const FString&, Message);

// 배낭에서 먹기·버리기, 사망 가방, 떨어진 가방 줍기를 **서버에 묻는** 자리 (DESIGN 8.12 · 8.10).
//
// 흐름은 셋 다 같다: `CallServerFunction` → 받아들였을 때만 결과를 반영 → `FetchPlayerState()`.
// **보유 칸은 여기서 한 칸도 안 바뀐다** — 줄고 느는 건 다시 받은 스냅샷으로만 들어온다 (CLAUDE.md 8).
// 가방 액터는 모두가 보게 호스트가 스폰·제거한다(Server RPC). 혼자 할 때는 그 자리에서 돈다.
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API UBackpackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBackpackComponent();

	/** 한 가지 판정이 끝날 때마다. */
	UPROPERTY(BlueprintAssignable, Category = "Backpack")
	FOnBackpackReplySignature OnReply;

	/** 검사·폰 연결용. 비워 두면 부를 때 주인 액터·게임 인스턴스에서 찾는다. */
	void BindTargets(USnareBackendSubsystem* InBackend, UPlayerVitalsComponent* InVitals);

	/** 먹는다 — `ConsumeItem{itemId}`. 성공하면 응답 `heal` 만큼 체력을 채운다. @return 서버에 물었으면 true */
	UFUNCTION(BlueprintCallable, Category = "Backpack")
	bool RequestConsume(FName ItemID);

	/**
	 * 빈 병을 물가에서 채운다 — `FillBottle{itemId}` (DESIGN 13.6). 물가 판정은 **호스트**가 자기 월드의 폰 위치로 한다
	 * (#0203). 멀면 서버에 묻지 않고 `OnReply(false, "not_near_water")`. @return 채우기를 청했으면 true (백엔드 없으면 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "Backpack")
	bool RequestFill(FName ItemID);

	/**
	 * 모자를 쓴다고 서버에 적는다 — `EquipHat{itemId}` (DESIGN 13.5). 수확·판매·뱃삯·나무 효과는 서버가
	 * 이 기록과 가방을 보고 판정한다. 겉모습(머리 칸)은 부르는 쪽이 따로 입힌다. @return 서버에 물었으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Backpack")
	bool RequestEquipHat(FName ItemID);

	/**
	 * 모자를 벗는다 — `EquipHat{itemId:""}` (#0198 · DESIGN 13.5). 서버가 받아들인 **뒤에만** 머리 칸 겉모습을
	 * 뗀다 (CLAUDE.md 8) — 거절되면 효과도 겉모습도 그대로다. @return 서버에 물었으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Backpack")
	bool RequestUnequipHat();

	/**
	 * 꾸미기 한 칸을 입거나 벗는다고 서버에 묻는다 — `SetOutfit{slot, itemId}` (`docs/specs/skin-parts.md` 5장 4).
	 * 서버가 배낭에 그 꾸미기가 있는지 보고 받아들인 **뒤에만** 장비 컴포넌트의 겉모습 덮개를 바꾼다 — 거절이면
	 * 겉모습 그대로다 (CLAUDE.md 8, 안 산 젬 꾸미기를 입어 보이기 막기).
	 * @param ItemID 꾸미기 아이템 ID. NAME_None 이면 그 칸을 벗긴다
	 * @return 서버에 물었으면 true (꾸미기 칸이 아닌 부위·백엔드 없음이면 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "Backpack")
	bool RequestSetOutfit(EEquipSlot Slot, FName ItemID);

	/** 서버 `SetOutfit` 의 칸 이름(`head`·`face`·`top`·`bottom`·`shoes`·`gloves`·`back`). 꾸미기 칸이 아니면 빈 문자열. */
	static FString GetOutfitSlotKey(EEquipSlot Slot);

	/**
	 * 서버가 알려 준 지금 쓴 모자로 맞춘다 — `GetBaseState.hat` (#0199 재접속 복원). 요청이 아니라 서버 값을
	 * 옮겨 적기만 한다(CLAUDE.md 8). 모자가 있으면 머리 칸 겉모습도 시도한다(메시 없는 모자는 조용히 넘긴다).
	 */
	void ApplyBackendHat(FName HatID);

	/**
	 * 서버가 알려 준 입은 꾸미기로 맞춘다 — `GetBaseState.outfit {칸: itemId}` (재접속 복원, `docs/specs/skin-parts.md` 5장 4).
	 * 서버는 지금도 가진 것만 싣는다. 꾸미기 일곱 칸마다 있으면 입히고 없으면 벗긴다 — 요청이 아니라 옮겨 적기만(CLAUDE.md 8).
	 * 모르는 칸 이름은 넘긴다. @return 입힌 칸 수
	 */
	int32 ApplyBackendOutfit(const FJsonObject& Outfit);

	/**
	 * 서버 보유 스냅샷을 받았을 때 — 쓴 모자가 가방에 없으면(죽어서 가방을 두고 옴) 벗긴다 (#0204).
	 * 서버 `wornHatId()` 도 가방에 없는 모자는 효과 0 으로 본다 — 같은 규칙을 옮겨 적기만 한다(CLAUDE.md 8).
	 */
	void ApplyBackendInventory(const TArray<FInventorySlot>& Slots);

	/** 서버가 받아들인 마지막 `EquipHat` 의 모자 (#0198). 벗었거나 아직 이 세션에서 안 썼으면 NAME_None. */
	UFUNCTION(BlueprintPure, Category = "Backpack")
	FName GetWornHatID() const { return WornHatID; }

	/** 버린다 — `DropItem{itemId, count}`. 성공하면 발밑에 가방을 둔다. @return 서버에 물었으면 true */
	UFUNCTION(BlueprintCallable, Category = "Backpack")
	bool RequestDrop(FName ItemID, int32 Count);

	/** 죽었다 — `DropOnDeath{}`. 응답 `dropped` 가 참일 때만 죽은 자리에 가방을 둔다. @return 서버에 물었으면 true */
	bool RequestDropOnDeath();

	/** 줍는다 — `PickupDrop{ownerId, dropId}`. 같은 가방은 답이 오기 전까지 한 번만 묻는다. @return 서버에 물었으면 true */
	bool RequestPickup(ADropBagActor* Bag);

	/** 사거리(근접 공격 사거리) 안 가장 가까운 가방을 줍는다. @return 서버에 물었으면 true */
	bool TryPickupNearest();

	/** 이 위치에서 RangeCm 안 가장 가까운 가방. 없으면 nullptr. */
	ADropBagActor* FindNearestBagInRange(float RangeCm) const;

	/**
	 * 부활 자리 — 이 컨트롤러(또는 그 폰)가 주인인 `SnareHome` 태그 액터(집·텐트). 없으면 nullptr 이고
	 * 부르는 쪽이 PlayerStart 로 간다 (DESIGN 8.12 "사망").
	 */
	static AActor* FindRespawnHome(UWorld* World, const AController* Controller);

	/** 집·텐트 액터가 달아 두는 태그. */
	static FName GetHomeTag();

	/** 성공 응답 뒤 보유 상태를 다시 물은 횟수 — 2층 검사가 "성공하면 다시 받는다" 를 본다. */
	int32 GetRefetchCount() const { return RefetchCount; }

	/** 가방·상자 소리를 낸 수 (M49-03 — 거절·가방 없음은 안 센다). 검사용. */
	int32 GetBagCueCount() const { return BagCueCount; }

	/** 마지막으로 낸 가방·상자 소리 이름 (`BagDrop`·`BagPickup`·`ChestOpen`). 검사용. */
	FName GetLastBagCue() const { return LastBagCue; }

	/** 상자 판정이 끝났을 때 — 상자 화면이 답 전체(`items`·`waitMs`)를 보고 그린다. */
	using FChestDone = TFunction<void(bool bAccepted, const FString& Code, const FString& Message,
		const TSharedPtr<FJsonObject>& Response)>;

	/**
	 * 상자를 연다 — `OpenChest{chest}` (위험구역 상자 1..2) 또는 `OpenChest{treasureId}` (보물 상자, 13.8).
	 * 굴림·재충전은 서버다. 가방이 모자라 답에 `drop` 이 오면 발밑에 가방을 둔다(버리기와 같은 길).
	 * @return 서버에 물었으면 true
	 */
	bool RequestOpenChest(int32 ChestIndex, const FString& TreasureID, FChestDone OnDone);

	/**
	 * 잠긴 금고를 연다 — `OpenVault{ruin}` (폐허 1..3, DESIGN 8.1c). 열쇠패 1장 소모·굴림·24시간은 서버다.
	 * 열쇠패가 없으면 `no_key`, 안 찼으면 `recharging`+`waitMs`. 가방이 모자라 `drop` 이 오면 발밑에 둔다.
	 * @return 서버에 물었으면 true
	 */
	bool RequestOpenVault(int32 RuinIndex, FChestDone OnDone);

	/**
	 * 꾸미기 상자를 연다 — `OpenCrate{crateId}` (DESIGN 10.8). 뽑기·상자·열쇠 차감은 서버다(CLAUDE.md 8).
	 * 답 `{itemId, grade}` 는 OnDone 이 받고, 칸은 이어서 오는 스냅샷이 바꾼다. @return 서버에 물었으면 true
	 */
	bool RequestOpenCrate(FName CrateID, FChestDone OnDone);

	/** 상자 열쇠 1개를 젬으로 산다 — `BuyItem{itemId:item.crate_key, count:1, currency:"GM"}` (10.8). */
	bool RequestBuyCrateKeyWithGems(FChestDone OnDone);

	/** 상자 열쇠 아이템 ID (`item.crate_key`). */
	static FName GetCrateKeyID();

	/**
	 * 보물 지도를 쓴다 — `UseTreasureMap{}`. 받은 자리(또는 `busy` 가 돌려준 이미 연 자리)를 기억하고,
	 * 그 칸이 지금 맵이면 그 자리에 이 기기만 보는 보물 상자를 세운다. @return 서버에 물었으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Backpack")
	bool RequestUseTreasureMap();

	/**
	 * 찾는 보물을 서버 기록에서 다시 받는다 — `GetTreasure{}` (M13-15). 존을 옮기면 캐릭터가 새로 서서 표식을 잃는다.
	 * 답이 보물이면 `RequestUseTreasureMap` 과 같은 길로 상자를 세우고, 없으면(열었거나 지났으면) 지운다.
	 * 알림·다시받기는 하지 않는다(보유가 안 바뀐다). @return 서버에 물었으면 true
	 * @param OnDone 서버 답을 받아들였으면 true, 못 물었거나 거절이면 false (M13-16 — HUD 가 재물음을 정한다)
	 */
	bool RequestRestoreTreasure(TFunction<void(bool bAccepted)> OnDone = nullptr);

	/** 지금 찾는 보물. 없으면 `IsSet()` 이 false. */
	const SnareChestLoot::FTreasure& GetActiveTreasure() const { return ActiveTreasure; }

	/** 이 맵에 세운 보물 상자. 다른 맵 칸이거나 없으면 nullptr. */
	ALootChestActor* GetTreasureChest() const { return TreasureChest.Get(); }

	/** 서버 응답 `items:[{itemId,count}]` 를 칸 목록으로. */
	static TArray<FInventorySlot> ParseItems(const TSharedPtr<FJsonObject>& Response);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable)
	void Server_SpawnDropBag(const FString& OwnerId, const FString& DropId, const TArray<FInventorySlot>& Items,
		const FString& ExpiresAtUtc, FVector Location);

	UFUNCTION(Server, Reliable)
	void Server_RemoveDropBag(ADropBagActor* Bag);

	/**
	 * 서버 응답 하나 → 가방·상자 소리 한 번 (DESIGN 8.12 「가방·상자 소리」, M49-03). 규칙은 `SnareSound::BagCue` —
	 * 거절·오류 코드면 무음. 월드 소리(가방 셋)는 호스트를 거쳐 `Where` 에서 모든 기기, 상자는 이 기기만.
	 */
	void PlayBagCue(SnareSound::EBagAction Action, bool bAccepted, const FString& ErrorCode, const FVector& Where);

	UFUNCTION(Server, Unreliable)
	void Server_PlayBagCue(FName Cue, float Volume, FVector Where);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayBagCue(FName Cue, float Volume, FVector Where);

	/** 먹은 음식의 버프를 호스트 복제본에도 건다 — 이동 속도는 호스트가 판정한다(DESIGN 13.6). 값은 호스트가 표에서 읽는다. */
	UFUNCTION(Server, Reliable)
	void Server_ApplyItemBuff(FName ItemID);

	/** 채우기 청 — 호스트가 물가를 판정한다 (#0203). */
	UFUNCTION(Server, Reliable)
	void Server_RequestFill(FName ItemID);

	/** 호스트가 물가를 인정했다 — 이 기기가 자기 계정으로 `FillBottle` 을 부른다. */
	UFUNCTION(Client, Reliable)
	void Client_BeginFill(FName ItemID);

	/** 호스트가 물가가 아니라고 했다 — 서버 호출 없이 거절을 알린다. */
	UFUNCTION(Client, Reliable)
	void Client_RejectFill();

private:
	/** 비어 있으면 주인·게임 인스턴스에서 찾는다. 남의 복제본(원격 폰)이면 nullptr — 남의 계정으로 묻지 않는다. */
	USnareBackendSubsystem* ResolveBackend();

	/** 호스트에서 — 폰 위치로 물가(`CastRangeCm`)를 보고 부를지·거절할지 정한다. */
	void ApproveFillOnAuthority(FName ItemID);

	/** CloudScript `FillBottle` — 호스트가 인정한 뒤에만 불린다. */
	void CallFillBottle(FName ItemID);

	/** 판정 하나의 끝 — 방송하고, 받아들였으면 보유 상태를 다시 받는다. */
	void Finish(bool bAccepted, const FString& Code, const FString& Message);

	/** 상자·열쇠 서버 호출 공통 — 답을 OnDone 에 넘기고 Finish 로 다시 받는다. */
	bool SendCrateCall(const TCHAR* Function, const TSharedPtr<FJsonObject>& Args, FChestDone OnDone);

	/** 그 아이템 행의 버프(`FItemRow::BuffKind`)를 이 폰의 몸 상태에 건다. 버프 없는 행이면 false. */
	bool ApplyItemBuff(FName ItemID);

	/** 버프 값을 읽을 `DT_ItemTable` (생성자에서 읽는다). */
	UPROPERTY()
	TObjectPtr<UDataTable> ItemTable;

	UPROPERTY()
	TObjectPtr<USnareBackendSubsystem> Backend;

	UPROPERTY()
	TObjectPtr<UPlayerVitalsComponent> Vitals;

	/** 보물을 기억하고 그 자리에 상자를 세운다 (지금 맵의 칸일 때만). */
	void SetTreasure(const SnareChestLoot::FTreasure& Treasure);

	/** 보물이 끝났다(열었다·사라졌다·남의 것) — 기억과 상자를 치운다. */
	void ClearTreasure();

	SnareChestLoot::FTreasure ActiveTreasure;

	TWeakObjectPtr<ALootChestActor> TreasureChest;

	/** 답을 기다리는 가방 ID — 같은 가방을 두 번 묻지 않는다. */
	TSet<FString> PickupsInFlight;

	int32 RefetchCount = 0;

	int32 BagCueCount = 0;
	FName LastBagCue;

	/** `-SnareBagSoundAt` 이 가짜 서버(`TestServerOverride`)를 걸었는가 — 끝날 때 이 컴포넌트가 비운다. */
	bool bInstalledBagSoundServer = false;

	/** 서버가 받아들인 모자 — 답이 온 뒤에만 바뀐다 (CLAUDE.md 8). */
	FName WornHatID;
};
