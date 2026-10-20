#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LootChestActor.generated.h"

class APawn;
class UChestScreenWidget;
class UStaticMeshComponent;

// ROADMAP M13-12: 위험구역 상자(DESIGN 8.10 「8.1 옆 2개」)와 보물 상자(13.8 「상자 액터는 8.1 것을 쓴다」) 한 종류.
//
// 하는 일은 `AShopVendorActor` 와 같다 — 내 폰이 상호작용 거리 안에 들면 상자 화면(`UChestScreenWidget`)을
// 올리고 멀어지면 내린다. 굴림·재충전·지급은 전부 서버(`OpenChest`) 몫이다 (CLAUDE.md 8).
// - 위험구역 상자: 호스트가 상시 위험구역 옆에 `ChestIndex` 1·2 로 세우고 복제한다. 재충전은 서버가 사람마다 잰다.
// - 보물 상자: 지도를 쓴 사람 기기에만 서는 로컬 액터(`TreasureID`). 남의 보물은 남의 서버 기록이라 복제하지 않는다.
UCLASS()
class SNAREHAVEN_API ALootChestActor : public AActor
{
	GENERATED_BODY()

public:
	ALootChestActor();

	/**
	 * 상시 위험구역 옆에 상자 1·2 를 세운다 — 구역 가운데에서 ±Y 로 **구역 반지름**만큼(구역 경계 위).
	 * 서버만, 이미 서 있으면 다시 안 세운다(상자 번호는 서버 기록 `chests[1..2]` 와 짝이라 맵 전체에 한 벌).
	 * @return 새로 세운 수
	 */
	static int32 SpawnBesideZone(UWorld* World, const FVector& ZoneCenter, float ZoneRadiusCm);

	/**
	 * 잠긴 금고 폐허(DESIGN 8.1c)를 `Where` 에 세운다 — 서버만, 같은 번호가 이미 서 있으면 안 세운다(번호는 서버 기록과 짝).
	 * @return 세웠으면 true
	 */
	static bool SpawnVault(UWorld* World, const FVector& Where, int32 RuinIndex);

	/** 금고 폐허 번호(1..3). 상자면 0. */
	int32 GetVaultRuinIndex() const { return VaultRuinIndex; }
	void SetVaultRuinIndex(int32 InRuinIndex) { VaultRuinIndex = InRuinIndex; }

	/** 위험구역 상자 번호(1..2). 보물 상자면 0. */
	int32 GetChestIndex() const { return ChestIndex; }

	/** 보물 상자의 서버 보물 ID. 위험구역 상자면 빈 문자열. */
	const FString& GetTreasureID() const { return TreasureID; }

	/** 스폰 직후(`FinishSpawning` 전) 한 번. */
	void SetChestIndex(int32 InChestIndex) { ChestIndex = InChestIndex; }
	void SetTreasureID(const FString& InTreasureID) { TreasureID = InTreasureID; }

	/**
	 * 이 폰이 상자 앞에 섰는지 보고 화면을 올리거나 내린다. 타이머가 부르고, 검사도 직접 부른다.
	 * @return 상호작용 거리 안이면 true
	 */
	bool UpdateProximity(APawn* LocalPawn);

	UChestScreenWidget* GetChestScreen() const { return ChestScreen; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	/** 폰이 아직 없으면 다음 틱에 다시 — 상인과 같은 방식(폰의 공격 간격으로 거리 검사). */
	void StartProximityScan();
	void HandleProximityScan();

	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> Body;

	/** 금고 전용 청록 육각 받침 — 일반 상자와 멀리서도 구분된다(열쇠패 아이콘과 같은 색). 금고일 때만 켠다. */
	UPROPERTY(VisibleAnywhere, Category = "Chest")
	TObjectPtr<UStaticMeshComponent> VaultPlinth;

	void ApplyVaultLook();

	/** 금고 둘레 폐허(무너진 담·기둥·돌판)와 철띠·자물쇠 판 — 엔진 기본 도형만 쓴다. 금고일 때만. */
	void BuildVaultRuin();
	void AddRuinPiece(UStaticMesh* Mesh, const FVector& WorldOffset, const FRotator& Rot, const FVector& SizeCm,
		const FLinearColor& Color, bool bBlock);

	UPROPERTY(Replicated)
	int32 ChestIndex = 0;

	/** 맵에 직접 놓는 금고 폐허의 번호(`tools/unreal/setup_vault_ruins.py`). 0 이면 금고가 아니다. */
	UPROPERTY(EditAnywhere, Replicated, Category = "Chest", meta = (ClampMin = "0", ClampMax = "3"))
	int32 VaultRuinIndex = 0;

	FString TreasureID;

	UPROPERTY(Transient)
	TObjectPtr<UChestScreenWidget> ChestScreen;

	FTimerHandle ProximityTimerHandle;
};
