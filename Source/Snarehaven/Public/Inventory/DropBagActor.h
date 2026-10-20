#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Inventory/InventoryComponent.h"
#include "DropBagActor.generated.h"

class UStaticMeshComponent;

// 땅에 떨어진 가방 (DESIGN 8.12 "버리기·사망"). 버리기(`DropItem`)·사망(`DropOnDeath`) 응답으로만 생긴다.
//
// **이 액터는 원본이 아니다.** 가방 속 목록·만료 시각·"이미 주웠다" 판정은 서버 `PickupDrop` 이 한다 (CLAUDE.md 8).
// 여기 들고 있는 값은 서버 응답을 옮긴 표시용이고, 줍기는 `UBackpackComponent::RequestPickup` 이 서버에 묻는다.
// 모두가 같은 자리에서 보게 호스트가 스폰하고 복제한다.
UCLASS()
class SNAREHAVEN_API ADropBagActor : public AActor
{
	GENERATED_BODY()

public:
	ADropBagActor();

	/**
	 * 서버 응답으로 채운다. 호스트에서만 부른다 — 수명은 서버 `expiresAtUtc` 에서 계산한다(클라이언트 시계로 정하지 않는다).
	 * @param InOwnerId      가방 주인 PlayFabId (`PickupDrop` 의 `ownerId`)
	 * @param InDropId       서버가 준 가방 ID
	 * @param InItems        서버가 담았다고 한 목록(표시용)
	 * @param InExpiresAtUtc 서버가 정한 사라지는 시각(ISO 8601)
	 */
	void InitFromServer(const FString& InOwnerId, const FString& InDropId, const TArray<FInventorySlot>& InItems,
		const FString& InExpiresAtUtc);

	/** 서버 만료 시각까지 남은 초. 못 읽으면 0. 표시·수명 계산에만 쓴다. */
	static float SecondsUntil(const FString& ExpiresAtUtc, const FDateTime& NowUtc);

	const FString& GetOwnerPlayFabId() const { return OwnerPlayFabId; }
	const FString& GetDropId() const { return DropId; }
	const TArray<FInventorySlot>& GetItems() const { return Items; }
	const FString& GetExpiresAtUtc() const { return ExpiresAtUtc; }

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Drop")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Drop")
	FString OwnerPlayFabId;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Drop")
	FString DropId;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Drop")
	TArray<FInventorySlot> Items;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Drop")
	FString ExpiresAtUtc;
};
