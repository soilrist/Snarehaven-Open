#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RanchInteractComponent.generated.h"

class ARanchSlotActor;
class UDataTable;
class UInventoryComponent;
class UMeleeAttackComponent;
class URanchComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRanchSlotTargetChangedSignature, ARanchSlotActor*, NewTarget);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedPlacementChangedSignature, FName, NewPlacementID);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRanchNothingToPlaceSignature);

// ROADMAP M2-04c: 목장 슬롯 상호작용 (명세 `docs/specs/ranch-slot.md` 5장 3번).
//
// `UGatherComponent` 와 **같은 길**을 그대로 쓴다 — 사거리 안 가장 가까운 것 하나를 타이머로 골라
// 하이라이트하고, 같은 자동 상호작용 버튼(`IA_Attack`)이 그것을 집는다 (DESIGN 3.3). 사거리·갱신 주기를
// 여기서 새로 만들지 않고 `UMeleeAttackComponent` 에서 빌리는 이유도 같다: 버튼이 하나인 이상
// 거리가 두 군데로 갈리면 "동물은 닿는데 슬롯은 안 닿는" 차이가 생긴다.
//
// 권위: **칸 내용을 여기서 바꾸지 않는다.** 누른 결과는 `URanchComponent` 의 요청 대기열에만 쌓이고,
// 칸이 실제로 차는 것은 백엔드 스냅샷이 왔을 때뿐이다 (CLAUDE.md 8 · DESIGN 4.1 권위 표).
// 실제 전송은 M2-01·M2-03(보류 — 사람 몫).
UCLASS(ClassGroup = (Snarehaven), meta = (BlueprintSpawnableComponent))
class SNAREHAVEN_API URanchInteractComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URanchInteractComponent();

	/**
	 * 사거리 안 가장 가까운 슬롯을 다시 찾아 하이라이트를 옮긴다.
	 * @return 새 대상 (없으면 nullptr)
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	ARanchSlotActor* RefreshTarget();

	/**
	 * 상호작용 버튼을 누른 순간. 대상 칸이 비어 있으면 배치 **요청**을, 차 있으면 수확 **요청**을 쌓는다.
	 * 2층 검사도 이 함수를 직접 부른다 (입력 없이 같은 길을 타려고).
	 * 빈 칸에 넣는 것은 고른 것에 따라 갈린다 — 펫이면 `RegisterPet`, 씨앗이면 `PlantCrop`(⑩).
	 * 요청이 쌓이면 몸에 줍기 몸짓(`EItemAction::Gather`)을 한 번 튼다 (M28-04).
	 * @return 요청을 쌓았으면 true (대상이 없거나, 넣을 것이 없거나, 서버에 `PlantCrop` 이 아직 없으면 false)
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	bool TryInteractNearest();

	/** 지금 하이라이트된 슬롯. 목장 HUD(M2-06)의 버튼 표시가 이걸 본다. */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	ARanchSlotActor* GetSlotTarget() const { return CurrentTarget; }

	/** `OnNothingToPlace` 를 몇 번 알렸나 — 2층 검사가 읽는다 (`GetItemActionCount` 와 같은 취지). */
	int32 GetNothingToPlaceCount() const { return NothingToPlaceCount; }

	/**
	 * 지금 가진 펫들의 동물 ID (`DT_AnimalTable` 순서, `PetItemID` ↔ 보유 아이템).
	 * 보유는 백엔드 스냅샷 기준이라 아직 확정 안 난 지급 요청은 세지 않는다 (DESIGN 4.5) —
	 * 요청만 쌓인 펫을 목록에 넣으면 백엔드가 거절했을 때 없는 펫을 배치하게 된다.
	 * @return 가진 펫의 동물 ID 목록 (없으면 빈 배열)
	 */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	TArray<FName> GetOwnedPetAnimalIDs() const;

	/**
	 * 지금 가진 씨앗 아이템 ID (⑩, `DT_CropProductTable` 순서). 씨앗 ID 는 표 칸 `SeedItemID` —
	 * 제작 표(`create_data_tables.py`)·서버 `PlantCrop` 과 같은 규칙이다. 보유는 펫과 같이 백엔드 캐시만 센다.
	 * @return 가진 씨앗 ID 목록 (없으면 빈 배열)
	 */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	TArray<FName> GetOwnedSeedItemIDs() const;

	/**
	 * 지금 가진 색 변종 펫 아이템 ID (M21-07c2, `DT_AnimalTable` 순서의 `VariantPetItemID`). 동물 ID 로 담지 않는다 —
	 * 같은 종의 일반 펫과 선택지가 겹치면 어느 쪽을 넣을지 모른다. 보유는 펫과 같이 백엔드 캐시만 센다.
	 * @return 가진 변종 펫 아이템 ID 목록 (없으면 빈 배열)
	 */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	TArray<FName> GetOwnedVariantPetItemIDs() const;

	/**
	 * 지금 배낭에 있는 목장 꾸미기 아이템 ID (DESIGN 10.9, `DT_ItemTable` 의 `PetCosmetic` 행 순서). 보유는 백엔드 캐시만 센다.
	 * @return 가진 꾸미기 ID 목록 (없으면 빈 배열)
	 */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	TArray<FName> GetOwnedPetCosmeticIDs() const;

	/**
	 * 변종 펫 아이템 ID 의 동물 ID (`item.pet_fox_variant` → `animal.fox`). 서버 `RegisterPet` 은 동물 ID + `variant` 로 받는다.
	 * @return 동물 ID. 변종 펫이 아니면 NAME_None
	 */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	FName FindVariantAnimalID(FName PlacementID) const;

	/** 빈 칸에 넣을 수 있는 것 전부 — 가진 펫(동물 ID) 뒤에 가진 변종 펫(아이템 ID), 그 뒤에 가진 씨앗(아이템 ID). HUD 고르기 줄이 센다. */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	TArray<FName> GetPlacementOptions() const;

	/**
	 * 빈 칸에 넣으려고 고른 것 (M2-06b · ⑩). 고른 적이 없거나 고른 것이 목록에서 빠졌으면 **첫 선택지** —
	 * 펫이 앞에 오므로 씨앗이 없으면 고르기가 없던 때와 답이 같다 (DESIGN 3.2).
	 * @return 동물 ID 또는 씨앗 아이템 ID. 넣을 것이 없으면 NAME_None
	 */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	FName GetSelectedPlacementID() const;

	/**
	 * 넣기 판 탭 하나의 선택지 (M28-08). 씨앗 탭이면 가진 씨앗, 동물 탭이면 가진 펫 + 변종 펫 — 둘을 이으면
	 * `GetPlacementOptions` 와 같은 순서다.
	 * @param bSeedTab true 면 씨앗 탭
	 * @return 그 탭의 선택지 (없으면 빈 배열)
	 */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	TArray<FName> GetPlacementTabOptions(bool bSeedTab) const;

	/** 동물과 씨앗을 둘 다 가졌는지 — 그때만 넣기 판에 「동물 / 씨앗」 탭이 선다 (한쪽뿐이면 탭 없이). */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	bool HasPlacementTabs() const;

	/** 지금 고른 것이 씨앗 탭에 있는지. 탭은 따로 상태를 들지 않고 고른 것이 정한다 — 둘이 어긋날 일이 없다. */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	bool IsSeedTabSelected() const;

	/**
	 * 탭을 바꾼다 — 이미 그 탭이면 그대로, 아니면 그 탭의 첫 선택지를 고른다. 칸은 건드리지 않는다.
	 * @param bSeedTab true 면 씨앗 탭
	 * @return 그 탭에 고를 것이 있으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	bool SelectPlacementTab(bool bSeedTab);

	/**
	 * 선택지에서 **다음 것**으로 돌린다 (목장 HUD 의 고르기 버튼이 부른다). **고른 탭 안에서만** 돈다(M28-08) —
	 * 씨앗이 동물 사이에 섞여 넘겨야 나오지 않게. 한쪽만 가졌으면 그 한쪽 = 전부라 예전과 같다.
	 * 고르기는 표시 상태일 뿐이라 칸을 바꾸지 않는다 — 바뀌는 건 다음 요청에 실릴 것뿐이다.
	 * @return 새로 고른 ID. 넣을 것이 없으면 NAME_None
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	FName SelectNextPlacement();

	/**
	 * 선택지에서 **지정한 것**을 고른다 (배낭 펫 줄의 `풀어놓기` 가 부른다 — 순환 없이 그 종으로 바로).
	 * 고르기는 표시 상태일 뿐이라 칸을 바꾸지 않는다. 지금 가진 선택지에 없으면 아무것도 바꾸지 않는다.
	 * @param PlacementID 동물 ID 또는 씨앗 아이템 ID
	 * @return 골라졌으면 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Ranch")
	bool SelectPlacement(FName PlacementID);

	/**
	 * 화면에 보이는 이름. 동물 표에 있으면 동물 이름(변종 펫이면 `변종 ○○`), 아니면 아이템 표 이름(씨앗). HUD 가 표를 따로 읽지 않으려고
	 * 이 함수를 쓴다 (DataDriven.md 1).
	 * @return 표에 적힌 이름. 그런 행이 없으면 빈 FText
	 */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	FText GetPlacementDisplayName(FName PlacementID) const;

	/**
	 * 지금 쓰는 사거리(cm). 근접 공격 컴포넌트에서 빌려 온 값이라 여기 기본값이 없다.
	 * @return 사거리 (빌릴 곳이 없으면 -1 — 미정 취급이라 아무것도 고르지 않는다)
	 */
	UFUNCTION(BlueprintPure, Category = "Ranch")
	float GetInteractRangeCm() const;

	/**
	 * 월드의 슬롯 액터를 칸 수만큼만 보이게 한다 (M12-05) — `SlotIndex < SlotCount` 만 보이고 나머지는 숨기고
	 * 충돌을 끈다 — 슬롯에 붙은 액터(발판·난간)도 같이. 칸 수는 서버가 준 스냅샷 길이(`URanchComponent::GetSlotCount`)다. 확장 응답 뒤 다시 받은
	 * 스냅샷이 길어지면 새 칸이 나타난다.
	 * @return 보이게 둔 슬롯 수
	 */
	static int32 ApplySlotVisibility(UWorld* World, int32 SlotCount);

	/**
	 * 목장 데코(M35-05c · `docs/specs/gacha-deco.md` 5장 3)를 월드의 `RanchDeco.<종류>` 태그 액터에 입힌다.
	 * 먼저 지난번에 입힌 것을 전부 벗기고(처음 값으로) `DecoMap`(종류 → 아이템 ID)에 있는 종류만 다시 입힌다 — 비우면 해제다.
	 * 메시·재질은 **원래 메시를 숨기고 충돌 없는 겉모습 메시를 겹쳐** 바꾼다: 원래 메시의 충돌·길은 그대로다(갇힘 0).
	 * 메시가 바뀌면 겉모습 메시를 원래 메시의 가로 폭에 맞춰 줄인다(헛간 `Barn_02` → 닭장 `Coop_01` 폭). `sky` 는 해·하늘빛 세기·색.
	 * 표에 없거나 종류가 안 맞는 ID 는 건너뛴다.
	 * @return 겉모습을 바꾼 액터 수
	 */
	static int32 ApplyRanchDeco(UWorld* World, const TMap<FName, FName>& DecoMap, const UDataTable* InItemTable);

	/** 하이라이트 대상이 바뀔 때. 목장 HUD(M2-06)가 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Ranch")
	FOnRanchSlotTargetChangedSignature OnRanchSlotTargetChanged;

	/** 고른 것이 바뀔 때 (M2-06b). 목장 HUD 의 고르기 줄이 이걸 듣는다. */
	UPROPERTY(BlueprintAssignable, Category = "Ranch")
	FOnSelectedPlacementChangedSignature OnSelectedPlacementChanged;

	/**
	 * 빈 칸에서 눌렀는데 넣을 펫·씨앗이 하나도 없을 때 (전수 표 실측 #0512 — 「넣기」 라 해 놓고 아무 일도 없었다).
	 * 목장 HUD 가 거절 줄에 무엇이 없는지 한 줄을 띄운다. 요청은 쌓지 않는다.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Ranch")
	FOnRanchNothingToPlaceSignature OnNothingToPlace;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 어떤 아이템이 어떤 동물의 펫인지 적힌 표 (M1-01). 기본값은 `DT_AnimalTable` 하드 참조. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranch")
	TObjectPtr<UDataTable> AnimalTable;

	/** 심을 수 있는 작물 표 (⑩ — 씨앗 목록의 근거). 기본값은 `DT_CropProductTable` 하드 참조. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranch")
	TObjectPtr<UDataTable> CropTable;

	/** 씨앗 표시 이름을 읽는 아이템 표. 기본값은 `DT_ItemTable`. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Ranch")
	TObjectPtr<UDataTable> ItemTable;

private:
	/**
	 * 같은 액터에 붙은 형제 컴포넌트들. 캐싱하지 않고 쓸 때마다 찾는 이유는 `UGatherComponent` 와 같다 —
	 * 컴포넌트 등록 순서에 기대지 않고, 런타임에 떼었다 붙여도 낡은 포인터를 보지 않기 위해서다.
	 */
	UMeleeAttackComponent* FindMeleeAttack() const;
	URanchComponent* FindRanch() const;
	UInventoryComponent* FindInventory() const;

	ARanchSlotActor* FindNearestSlotInRange() const;

	/** `TryInteractNearest` 의 본체 — 대상 칸에 맞는 요청 하나를 쌓는다. 몸짓은 부르는 쪽이 튼다. */
	bool QueueNearestRequest();

	void SetTarget(ARanchSlotActor* NewTarget);
	void HandleTargetScan();

	/** 목장 캐시가 바뀌었다 — 칸 수가 늘었으면 새 슬롯을 드러낸다. */
	UFUNCTION()
	void HandleRanchCacheChanged();

	/** 드러난 목장 칸마다 맡긴 펫의 몸을 세우거나 치운다 (DESIGN 3.2 「목장 펫 모습」). */
	void ApplySlotPets();

	/** 목장 캐시의 데코가 지난번에 입힌 것과 다를 때만 `ApplyRanchDeco` 를 부른다 — 폴링마다 겉모습 메시를 새로 세우지 않게. */
	void ApplyDecoIfChanged();

	/** `AnimalID`(`animal.pig`) 로 `AnimalTable` 행을 찾는다. 없으면 nullptr. */
	const struct FAnimalRow* FindAnimalRow(FName AnimalID) const;

	/** 칸 수를 듣는 목장 (EndPlay 에서 끊으려고 든다). 로컬 폰에서만 채워진다. */
	UPROPERTY(Transient)
	TObjectPtr<URanchComponent> WatchedRanch;

	// 액터를 원시 포인터로 들고 있으면 파괴돼도 눈치채지 못한다 — GC 가 아는 포인터로 든다.
	UPROPERTY(Transient)
	TObjectPtr<ARanchSlotActor> CurrentTarget;

	/** `GetNothingToPlaceCount` 가 읽는다. */
	int32 NothingToPlaceCount = 0;

	/**
	 * 플레이어가 고른 펫·씨앗. **저장하지 않는다** — 고르기는 화면 상태고, 진짜 보유는 백엔드 캐시에 있다.
	 * 목록에 없는 ID 가 남아 있어도 `GetSelectedPlacementID` 가 첫 선택지로 되돌린다.
	 */
	UPROPERTY(Transient)
	FName SelectedPlacementID;

	FTimerHandle TargetScanTimerHandle;

	/** 마지막으로 월드에 입힌 데코 (종류 → 아이템 ID). 처음은 비어 있다 = 레벨 그대로. */
	TMap<FName, FName> AppliedDeco;
};
