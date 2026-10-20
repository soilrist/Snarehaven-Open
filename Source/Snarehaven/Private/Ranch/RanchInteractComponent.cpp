#include "Ranch/RanchInteractComponent.h"
#include "Snarehaven.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Data/CropProductRow.h"
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Inventory/InventoryComponent.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchPetActor.h"
#include "Ranch/RanchSlotActor.h"
#include "Ranch/RanchSyncComponent.h"
#include "SnarehavenCharacter.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

URanchInteractComponent::URanchInteractComponent()
{
	// 대상 찾기는 타이머가 돌린다 — 매 프레임 액터를 전수 조사하지 않는다
	// (engines/unreal.md 코드 규칙 2).
	PrimaryComponentTick.bCanEverTick = false;

	// 어떤 아이템이 어떤 동물의 펫인지는 코드가 아니라 표가 안다 (DataDriven.md 1).
	static ConstructorHelpers::FObjectFinder<UDataTable> AnimalTableAsset(
		TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable"));
	if (AnimalTableAsset.Succeeded())
	{
		AnimalTable = AnimalTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> CropTableAsset(
		TEXT("/Game/Game/Data/Ranch/DT_CropProductTable.DT_CropProductTable"));
	if (CropTableAsset.Succeeded())
	{
		CropTable = CropTableAsset.Object;
	}

	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(SnareItemRows::ItemTablePath());
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

void URanchInteractComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// M12-05: 확장 칸(6~10번) 슬롯은 서버가 연 만큼만 보인다. 칸 수는 내 목장 캐시가 정하니 내 폰에서만 맞춘다.
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (IsValid(OwnerPawn) && OwnerPawn->IsLocallyControlled())
	{
		WatchedRanch = FindRanch();
		if (IsValid(WatchedRanch))
		{
			WatchedRanch->OnRanchCacheChanged.AddDynamic(this, &URanchInteractComponent::HandleRanchCacheChanged);
			HandleRanchCacheChanged();
		}
	}

	// 하이라이트 갱신 주기도 새로 만들지 않고 공격 간격을 빌린다 (DESIGN 3.3 — 감독 확정).
	const UMeleeAttackComponent* Melee = FindMeleeAttack();
	const float ScanIntervalSeconds = IsValid(Melee) ? Melee->GetAttackIntervalSeconds() : -1.f;
	if (ScanIntervalSeconds <= 0.f)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("목장 슬롯 갱신 주기를 빌릴 곳이 없다 — 버튼을 누를 때만 대상을 찾는다"));
		return;
	}

	World->GetTimerManager().SetTimer(
		TargetScanTimerHandle, this, &URanchInteractComponent::HandleTargetScan, ScanIntervalSeconds, true);
}

void URanchInteractComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머를 남겨 두면 파괴된 컴포넌트를 다시 부른다 (engines/unreal.md 코드 규칙 7).
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TargetScanTimerHandle);
	}

	// 하이라이트를 켠 채로 사라지면 슬롯이 계속 빛난다.
	SetTarget(nullptr);

	if (IsValid(WatchedRanch))
	{
		WatchedRanch->OnRanchCacheChanged.RemoveDynamic(this, &URanchInteractComponent::HandleRanchCacheChanged);
		WatchedRanch = nullptr;
	}

	Super::EndPlay(EndPlayReason);
}

int32 URanchInteractComponent::ApplySlotVisibility(UWorld* World, int32 SlotCount)
{
	if (World == nullptr)
	{
		return 0;
	}

	int32 VisibleCount = 0;
	for (TActorIterator<ARanchSlotActor> It(World); It; ++It)
	{
		ARanchSlotActor* const Slot = *It;
		if (!IsValid(Slot))
		{
			continue;
		}
		// 번호를 안 받은 슬롯(INDEX_NONE)은 건드리지 않는다 — 숨길지 말지 근거가 없다.
		if (Slot->GetSlotIndex() == INDEX_NONE)
		{
			continue;
		}
		const bool bVisible = Slot->GetSlotIndex() < SlotCount;
		// 칸에 붙은 발판·난간(`setup_ranch_buildings.py` 가 슬롯에 붙인다)도 같이 — 잠긴 칸은 통째로 안 보인다.
		TArray<AActor*> Pieces;
		Slot->GetAttachedActors(Pieces);
		Pieces.Add(Slot);
		for (AActor* const Piece : Pieces)
		{
			Piece->SetActorHiddenInGame(!bVisible);
			Piece->SetActorEnableCollision(bVisible);
		}
		VisibleCount += bVisible ? 1 : 0;
	}
	return VisibleCount;
}

namespace SnareRanchDeco
{
	/** 데코가 세운 겉모습 메시 — 벗길 때 이 태그로 찾아 지운다. */
	const FName OverlayTag(TEXT("RanchDeco.Overlay"));
	/** 데코가 숨긴 원래 메시 — 벗길 때 다시 보인다. 처음부터 숨은 메시와 섞이지 않게 따로 표시한다. */
	const FName HiddenBaseTag(TEXT("RanchDeco.HiddenBase"));

	/** 레벨 태그(`setup_ranch_buildings.py`) → 서버 종류 이름(`RANCH_DECO_KINDS`). */
	FName KindOf(const AActor* Actor)
	{
		static const TPair<FName, FName> Kinds[] = {
			{ TEXT("RanchDeco.Fence"), TEXT("fence") }, { TEXT("RanchDeco.Path"), TEXT("path") },
			{ TEXT("RanchDeco.Barn"), TEXT("barn") }, { TEXT("RanchDeco.Sky"), TEXT("sky") } };
		for (const TPair<FName, FName>& Kind : Kinds)
		{
			if (Actor->Tags.Contains(Kind.Key))
			{
				return Kind.Value;
			}
		}
		return NAME_None;
	}

	struct FLightOriginal
	{
		float Intensity = 0.f;
		FLinearColor Color = FLinearColor::White;
	};

	/** 처음 빛 값 — 해제하면 되돌린다. 빛 컴포넌트가 사라지면 약한 포인터라 저절로 무효가 된다. */
	TMap<TWeakObjectPtr<ULightComponentBase>, FLightOriginal>& LightOriginals()
	{
		static TMap<TWeakObjectPtr<ULightComponentBase>, FLightOriginal> Originals;
		return Originals;
	}

	/** 지난번에 입힌 것을 벗긴다 — 겉모습 메시 지우기 · 숨긴 원래 메시 보이기 · 빛 처음 값. */
	void Strip(AActor* Actor)
	{
		TArray<UStaticMeshComponent*> Meshes;
		Actor->GetComponents(Meshes);
		for (UStaticMeshComponent* const Mesh : Meshes)
		{
			if (Mesh->ComponentHasTag(OverlayTag))
			{
				Mesh->DestroyComponent();
			}
			else if (Mesh->ComponentHasTag(HiddenBaseTag))
			{
				Mesh->ComponentTags.Remove(HiddenBaseTag);
				Mesh->SetHiddenInGame(false);
			}
		}
		TArray<ULightComponentBase*> Lights;
		Actor->GetComponents(Lights);
		for (ULightComponentBase* const Light : Lights)
		{
			FLightOriginal Original;
			if (!LightOriginals().RemoveAndCopyValue(Light, Original))
			{
				continue;
			}
			if (ULightComponent* const Direct = Cast<ULightComponent>(Light))
			{
				Direct->SetIntensity(Original.Intensity);
				Direct->SetLightColor(Original.Color);
			}
			else if (USkyLightComponent* const Sky = Cast<USkyLightComponent>(Light))
			{
				Sky->SetIntensity(Original.Intensity);
			}
		}
	}

	/** 해 세기 × `RanchDecoSunScale`·색, 하늘빛 세기 × `RanchDecoSkyScale`. 바꾸기 전 값을 기억한다. */
	bool DressLights(AActor* Actor, const FItemRow& Row)
	{
		bool bChanged = false;
		TArray<ULightComponentBase*> Lights;
		Actor->GetComponents(Lights);
		for (ULightComponentBase* const Light : Lights)
		{
			if (UDirectionalLightComponent* const Sun = Cast<UDirectionalLightComponent>(Light); Sun != nullptr && Row.RanchDecoSunScale > 0.f)
			{
				LightOriginals().Add(Sun, { Sun->Intensity, Sun->GetLightColor() });
				Sun->SetIntensity(Sun->Intensity * Row.RanchDecoSunScale);
				Sun->SetLightColor(Row.RanchDecoSunColor);
				bChanged = true;
			}
			else if (USkyLightComponent* const Sky = Cast<USkyLightComponent>(Light); Sky != nullptr && Row.RanchDecoSkyScale > 0.f)
			{
				LightOriginals().Add(Sky, { Sky->Intensity, FLinearColor::White });
				Sky->SetIntensity(Sky->Intensity * Row.RanchDecoSkyScale);
				bChanged = true;
			}
		}
		return bChanged;
	}

	/** 메시가 있는 원래 컴포넌트마다 충돌 없는 겉모습 메시를 겹치고 원래 것은 숨긴다(충돌은 그대로). */
	bool DressMeshes(AActor* Actor, UStaticMesh* DecoMesh, UMaterialInterface* DecoMaterial, FName MaterialSlot)
	{
		bool bChanged = false;
		TArray<UStaticMeshComponent*> Bases;
		Actor->GetComponents(Bases);
		for (UStaticMeshComponent* const Base : Bases)
		{
			UStaticMesh* const BaseMesh = Base->GetStaticMesh();
			if (BaseMesh == nullptr || Base->ComponentHasTag(OverlayTag))
			{
				continue;
			}
			UStaticMesh* const Mesh = DecoMesh != nullptr ? DecoMesh : BaseMesh;
			UStaticMeshComponent* const Overlay = NewObject<UStaticMeshComponent>(Actor, NAME_None, RF_Transient);
			Overlay->ComponentTags.Add(OverlayTag);
			Overlay->SetMobility(EComponentMobility::Movable);
			Overlay->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Overlay->SetCanEverAffectNavigation(false);
			Overlay->SetStaticMesh(Mesh);
			Overlay->SetupAttachment(Base);
			if (Mesh != BaseMesh)
			{
				// 가로로 가장 긴 변을 원래 메시에 맞춘다 — 울타리는 길이, 헛간은 닭장 폭.
				const FVector BaseExtent = BaseMesh->GetBounds().BoxExtent;
				const FVector DecoExtent = Mesh->GetBounds().BoxExtent;
				const double DecoWidth = FMath::Max(DecoExtent.X, DecoExtent.Y);
				if (DecoWidth > KINDA_SMALL_NUMBER)
				{
					Overlay->SetRelativeScale3D(FVector(FMath::Max(BaseExtent.X, BaseExtent.Y) / DecoWidth));
				}
			}
			else
			{
				// 같은 메시면 레벨이 씌운 재질부터 그대로 옮기고 그 위에 데코 재질을 얹는다.
				for (int32 Index = 0; Index < Base->GetNumMaterials(); ++Index)
				{
					Overlay->SetMaterial(Index, Base->GetMaterial(Index));
				}
			}
			if (DecoMaterial != nullptr)
			{
				if (MaterialSlot.IsNone())
				{
					for (int32 Index = 0; Index < Overlay->GetNumMaterials(); ++Index)
					{
						Overlay->SetMaterial(Index, DecoMaterial);
					}
				}
				else
				{
					Overlay->SetMaterialByName(MaterialSlot, DecoMaterial);
				}
			}
			Overlay->RegisterComponent();
			Actor->AddInstanceComponent(Overlay);
			Base->SetHiddenInGame(true);
			Base->ComponentTags.AddUnique(HiddenBaseTag);
			bChanged = true;
		}
		return bChanged;
	}
}

int32 URanchInteractComponent::ApplyRanchDeco(UWorld* World, const TMap<FName, FName>& DecoMap, const UDataTable* InItemTable)
{
	if (World == nullptr)
	{
		return 0;
	}

	// 종류마다 입힐 행 — 표에 없거나 종류가 다른 ID 는 건너뛴다(겉모습만이라 틀린 칸에 입히는 것보다 안 입히는 게 낫다).
	TMap<FName, const FItemRow*> RowByKind;
	for (const TPair<FName, FName>& Entry : DecoMap)
	{
		const FItemRow* const Row = SnareItemRows::FindByItemID(InItemTable, Entry.Value);
		if (Row != nullptr && Row->ItemType == EItemType::RanchDeco && Row->RanchDecoKind == Entry.Key)
		{
			RowByKind.Add(Entry.Key, Row);
		}
		else
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("[RanchDeco] %s 칸의 %s 는 표의 목장 데코가 아니다 - 건너뜀"),
				*Entry.Key.ToString(), *Entry.Value.ToString());
		}
	}

	int32 Changed = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* const Actor = *It;
		const FName Kind = IsValid(Actor) ? SnareRanchDeco::KindOf(Actor) : NAME_None;
		if (Kind.IsNone())
		{
			continue;
		}
		SnareRanchDeco::Strip(Actor);
		const FItemRow* const* const Row = RowByKind.Find(Kind);
		if (Row == nullptr)
		{
			continue;
		}
		const bool bChanged = Kind == TEXT("sky")
			? SnareRanchDeco::DressLights(Actor, **Row)
			: SnareRanchDeco::DressMeshes(Actor, (*Row)->RanchDecoMeshRef.LoadSynchronous(),
				(*Row)->RanchDecoMaterialRef.LoadSynchronous(), (*Row)->RanchDecoMaterialSlot);
		Changed += bChanged ? 1 : 0;
	}
	return Changed;
}

void URanchInteractComponent::ApplyDecoIfChanged()
{
	if (!IsValid(WatchedRanch) || WatchedRanch->GetStationKind() != ERanchStationKind::Ranch
		|| WatchedRanch->GetDeco().OrderIndependentCompareEqual(AppliedDeco))
	{
		return;
	}
	AppliedDeco = WatchedRanch->GetDeco();
	ApplyRanchDeco(GetWorld(), AppliedDeco, ItemTable);
}

void URanchInteractComponent::HandleRanchCacheChanged()
{
	if (IsValid(WatchedRanch))
	{
		ApplySlotVisibility(GetWorld(), WatchedRanch->GetSlotCount());
		ApplySlotPets();
		ApplyDecoIfChanged();
	}
}

void URanchInteractComponent::ApplySlotPets()
{
	// 맡긴 펫이 말뚝 곁에 보인다 (DESIGN 3.2 「목장 펫 모습」) — 칸 캐시를 비추기만 한다(CLAUDE.md 8).
	if (!IsValid(WatchedRanch) || WatchedRanch->GetStationKind() != ERanchStationKind::Ranch)
	{
		return;
	}
	for (TActorIterator<ARanchSlotActor> It(GetWorld()); It; ++It)
	{
		const FRanchSlotState* const State = WatchedRanch->GetSlot(It->GetSlotIndex());
		const bool bPet = State != nullptr && !It->IsHidden() && State->Kind == ERanchSlotKind::AnimalProduct
			&& !State->AnimalID.IsNone();
		It->ShowPet(bPet ? FindAnimalRow(State->AnimalID) : nullptr);
		// 10.9 씌운 꾸미기 — 몸이 선 칸만. 같은 꾸미기면 펫 쪽이 그대로 둔다.
		if (bPet && IsValid(It->GetPet()))
		{
			It->GetPet()->SetCosmetic(SnareItemRows::FindByItemID(ItemTable, State->CosmeticItemID));
		}
	}
}

const FAnimalRow* URanchInteractComponent::FindAnimalRow(FName AnimalID) const
{
	if (IsValid(AnimalTable))
	{
		for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
		{
			const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
			if (Row != nullptr && Row->AnimalID == AnimalID)
			{
				return Row;
			}
		}
	}
	return nullptr;
}

void URanchInteractComponent::HandleTargetScan()
{
	RefreshTarget();
}

UMeleeAttackComponent* URanchInteractComponent::FindMeleeAttack() const
{
	const AActor* Owner = GetOwner();
	return IsValid(Owner) ? Owner->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
}

URanchComponent* URanchInteractComponent::FindRanch() const
{
	// M10-02: 낚시터가 같은 클래스로 하나 더 붙어 있다 — 종류로 골라야 목장 칸을 만진다.
	return URanchComponent::FindStation(GetOwner(), ERanchStationKind::Ranch);
}

UInventoryComponent* URanchInteractComponent::FindInventory() const
{
	const AActor* Owner = GetOwner();
	return IsValid(Owner) ? Owner->FindComponentByClass<UInventoryComponent>() : nullptr;
}

float URanchInteractComponent::GetInteractRangeCm() const
{
	const UMeleeAttackComponent* Melee = FindMeleeAttack();
	if (!IsValid(Melee))
	{
		// 사거리를 여기서 지어내지 않는다 — 자동 상호작용 거리는 DESIGN 3.1 확정값 하나뿐이다
		// (DataDriven.md 3 · Verification.md 7).
		UE_LOG(LogSnarehaven, Warning, TEXT("근접 공격 컴포넌트가 없어 목장 상호작용 사거리를 알 수 없다"));
		return -1.f;
	}

	return Melee->GetAttackRangeCm();
}

ARanchSlotActor* URanchInteractComponent::RefreshTarget()
{
	ARanchSlotActor* NewTarget = FindNearestSlotInRange();
	SetTarget(NewTarget);
	return NewTarget;
}

TArray<FName> URanchInteractComponent::GetOwnedPetAnimalIDs() const
{
	TArray<FName> OwnedPets;

	const UInventoryComponent* Inventory = FindInventory();
	if (!IsValid(Inventory) || !IsValid(AnimalTable))
	{
		return OwnedPets;
	}

	// 표를 훑어 "내가 가진 펫 아이템" 에 해당하는 동물을 모은다. 아이템 → 동물 방향의 역표를 따로
	// 만들지 않는 이유는 동물 종이 한 화면에 들어갈 만큼 적고(DESIGN 3.1), 버튼을 누른 순간과
	// HUD 갱신 때만 도는 길이기 때문이다. 종이 수백이 되면 그때 캐시를 만든다.
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (Row == nullptr || Row->PetItemID.IsNone())
		{
			continue;
		}

		// 보유는 **백엔드가 확인해 준 캐시**만 센다 — 아직 응답이 안 온 지급 요청으로 펫을 배치하면
		// 백엔드가 거절했을 때 목장에 없는 펫이 서 있게 된다 (DESIGN 4.5 · CLAUDE.md 8).
		if (Inventory->GetItemCount(Row->PetItemID) > 0)
		{
			OwnedPets.Add(Row->AnimalID);
		}
	}

	return OwnedPets;
}

TArray<FName> URanchInteractComponent::GetOwnedSeedItemIDs() const
{
	TArray<FName> OwnedSeeds;

	const UInventoryComponent* Inventory = FindInventory();
	if (!IsValid(Inventory) || !IsValid(CropTable))
	{
		return OwnedSeeds;
	}

	for (const TPair<FName, uint8*>& Pair : CropTable->GetRowMap())
	{
		// 씨앗 ID 는 표 칸이 든다(차·커피·참나무는 작물 이름과 다르다, DESIGN 13.3) — 서버 `seedOf` 와 같은 짝.
		const FCropProductRow* Row = reinterpret_cast<const FCropProductRow*>(Pair.Value);
		if (Row != nullptr && !Row->SeedItemID.IsNone() && Inventory->GetItemCount(Row->SeedItemID) > 0)
		{
			OwnedSeeds.Add(Row->SeedItemID);
		}
	}

	return OwnedSeeds;
}

TArray<FName> URanchInteractComponent::GetOwnedPetCosmeticIDs() const
{
	TArray<FName> Owned;
	const UInventoryComponent* Inventory = FindInventory();
	if (!IsValid(Inventory) || !IsValid(ItemTable))
	{
		return Owned;
	}
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row != nullptr && Row->ItemType == EItemType::PetCosmetic && Inventory->GetItemCount(Row->ItemID) > 0)
		{
			Owned.Add(Row->ItemID);
		}
	}
	return Owned;
}

TArray<FName> URanchInteractComponent::GetOwnedVariantPetItemIDs() const
{
	TArray<FName> OwnedVariants;

	const UInventoryComponent* Inventory = FindInventory();
	if (!IsValid(Inventory) || !IsValid(AnimalTable))
	{
		return OwnedVariants;
	}

	// 일반 펫과 같은 길 — 백엔드가 확인해 준 캐시만 센다 (CLAUDE.md 8).
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (Row != nullptr && !Row->VariantPetItemID.IsNone() && Inventory->GetItemCount(Row->VariantPetItemID) > 0)
		{
			OwnedVariants.Add(Row->VariantPetItemID);
		}
	}

	return OwnedVariants;
}

FName URanchInteractComponent::FindVariantAnimalID(FName PlacementID) const
{
	if (PlacementID.IsNone() || !IsValid(AnimalTable))
	{
		return NAME_None;
	}

	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (Row != nullptr && Row->VariantPetItemID == PlacementID)
		{
			return Row->AnimalID;
		}
	}
	return NAME_None;
}

TArray<FName> URanchInteractComponent::GetPlacementOptions() const
{
	TArray<FName> Options = GetOwnedPetAnimalIDs();
	Options.Append(GetOwnedVariantPetItemIDs());
	Options.Append(GetOwnedSeedItemIDs());
	return Options;
}

FName URanchInteractComponent::GetSelectedPlacementID() const
{
	const TArray<FName> Options = GetPlacementOptions();
	if (Options.Num() == 0)
	{
		return NAME_None;
	}

	// 고른 것을 다 써서 목록에서 빠질 수 있다(백엔드가 새 스냅샷을 보내는 순간). 그때 옛 선택을 그대로
	// 들고 있으면 없는 펫·씨앗을 요청에 실어 보낸다 — 첫 선택지로 되돌린다.
	return Options.Contains(SelectedPlacementID) ? SelectedPlacementID : Options[0];
}

TArray<FName> URanchInteractComponent::GetPlacementTabOptions(bool bSeedTab) const
{
	if (bSeedTab)
	{
		return GetOwnedSeedItemIDs();
	}
	TArray<FName> Options = GetOwnedPetAnimalIDs();
	Options.Append(GetOwnedVariantPetItemIDs());
	return Options;
}

bool URanchInteractComponent::HasPlacementTabs() const
{
	return GetPlacementTabOptions(false).Num() > 0 && GetPlacementTabOptions(true).Num() > 0;
}

bool URanchInteractComponent::IsSeedTabSelected() const
{
	const FName SelectedID = GetSelectedPlacementID();
	return !SelectedID.IsNone() && GetOwnedSeedItemIDs().Contains(SelectedID);
}

bool URanchInteractComponent::SelectPlacementTab(bool bSeedTab)
{
	const TArray<FName> TabOptions = GetPlacementTabOptions(bSeedTab);
	if (TabOptions.Num() == 0)
	{
		return false;
	}
	if (IsSeedTabSelected() == bSeedTab)
	{
		return true;
	}
	return SelectPlacement(TabOptions[0]);
}

FName URanchInteractComponent::SelectNextPlacement()
{
	// 고른 탭 안에서만 돈다 (M28-08) — 한쪽만 가졌으면 그 탭이 곧 전부다.
	const TArray<FName> Options = GetPlacementTabOptions(IsSeedTabSelected());
	if (Options.Num() == 0)
	{
		SelectedPlacementID = NAME_None;
		return NAME_None;
	}

	// 지금 고른 것(= 없으면 첫 선택지)의 다음 칸으로. 목록 끝에서 처음으로 돌아온다 — 버튼 하나로
	// 그 탭을 다 볼 수 있어야 하고, 하나뿐이면 같은 것이 다시 나온다.
	const int32 CurrentIndex = Options.IndexOfByKey(GetSelectedPlacementID());
	SelectedPlacementID = Options[(CurrentIndex + 1) % Options.Num()];

	OnSelectedPlacementChanged.Broadcast(SelectedPlacementID);
	return SelectedPlacementID;
}

bool URanchInteractComponent::SelectPlacement(FName PlacementID)
{
	// 없는 펫·씨앗을 고르면 요청에 실려 나간다 — 지금 가진 선택지만 받는다.
	if (!GetPlacementOptions().Contains(PlacementID))
	{
		return false;
	}

	SelectedPlacementID = PlacementID;
	OnSelectedPlacementChanged.Broadcast(SelectedPlacementID);
	return true;
}

FText URanchInteractComponent::GetPlacementDisplayName(FName PlacementID) const
{
	if (PlacementID.IsNone())
	{
		return FText::GetEmpty();
	}

	if (IsValid(AnimalTable))
	{
		for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
		{
			const FAnimalRow* Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
			if (Row != nullptr && Row->AnimalID == PlacementID)
			{
				return Row->DisplayName;
			}
			// M21-07c2 변종 펫 — 사냥 맵 이름 카드(`UAnimalStateComponent::GetCardName`)·도감과 같은 `변종 ○○`.
			if (Row != nullptr && Row->VariantPetItemID == PlacementID)
			{
				return FText::Format(LOCTEXT("RanchVariantPlacementName", "변종 {0}"), Row->DisplayName);
			}
		}
	}

	// 동물이 아니면 씨앗이다 — 이름은 아이템 표에 있다.
	const FItemRow* const ItemRow = SnareItemRows::FindByItemID(ItemTable, PlacementID);
	return ItemRow != nullptr ? ItemRow->DisplayName : FText::GetEmpty();
}

bool URanchInteractComponent::TryInteractNearest()
{
	// 요청이 쌓인 누름에만 줍기 몸짓 (M28-04) — 채집과 같은 클립을 모두에게. 그림뿐이라 칸·재고·보유는 그대로 서버 뒤다.
	const bool bQueued = QueueNearestRequest();
	if (bQueued)
	{
		if (ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner()))
		{
			Body->PlayItemAction(EItemAction::Gather);
		}
	}
	return bQueued;
}

bool URanchInteractComponent::QueueNearestRequest()
{
	ARanchSlotActor* Target = RefreshTarget();
	URanchComponent* Ranch = FindRanch();
	if (!IsValid(Target) || !IsValid(Ranch))
	{
		return false;
	}

	const int32 SlotIndex = Target->GetSlotIndex();
	if (Ranch->GetSlot(SlotIndex) == nullptr)
	{
		// 맵에 깔면서 칸 번호를 안 줬거나 0~4 밖이다 — 무시하고 경고만 (명세 2장).
		UE_LOG(LogSnarehaven, Warning, TEXT("목장 슬롯 번호가 범위 밖이다: %d"), SlotIndex);
		return false;
	}

	if (!Ranch->IsSlotEmpty(SlotIndex))
	{
		// 찬 칸 → 수확 요청. 몇 개를 주는지·코인이 얼마인지는 백엔드가 정한다 (DESIGN 3.2).
		return Ranch->QueueHarvest(SlotIndex);
	}

	// 무엇을 넣을지는 플레이어가 고른다 (M2-06b · ⑩). 고르지 않았으면 첫 선택지(펫 먼저)다.
	const FName PlacementID = GetSelectedPlacementID();
	if (PlacementID.IsNone())
	{
		// 넣을 펫도 씨앗도 없으면 요청을 쌓지 않는다 (명세 2장 실패·예외) — 씨앗 없이 심기 거부.
		UE_LOG(LogSnarehaven, Log, TEXT("인벤토리 캐시에 넣을 펫·씨앗이 없어 요청을 쌓지 않는다"));
		++NothingToPlaceCount;
		OnNothingToPlace.Broadcast();
		return false;
	}

	// M21-07c2 변종 펫은 아이템 ID 로 골라져 있다 — 서버엔 같은 종 동물 ID + `variant` 로 보낸다.
	const FName VariantAnimalID = FindVariantAnimalID(PlacementID);
	if (!VariantAnimalID.IsNone())
	{
		return Ranch->QueueRegisterPet(SlotIndex, VariantAnimalID, true);
	}

	if (!GetOwnedSeedItemIDs().Contains(PlacementID))
	{
		return Ranch->QueueRegisterPet(SlotIndex, PlacementID);
	}

	// ⑩ 씨앗이다. 라이브에 `PlantCrop` 이 아직 없다고 한 번 들었으면 더 보내지 않는다 — HUD 는 "서버 준비 중".
	const URanchSyncComponent* const Sync = GetOwner()->FindComponentByClass<URanchSyncComponent>();
	if (IsValid(Sync) && Sync->IsFunctionUnavailable(TEXT("PlantCrop")))
	{
		return false;
	}

	// 씨앗을 빼는 것도 칸을 채우는 것도 서버다 — 여기선 요청만 쌓는다 (CLAUDE.md 8).
	return Ranch->QueuePlantCrop(SlotIndex, PlacementID);
}

ARanchSlotActor* URanchInteractComponent::FindNearestSlotInRange() const
{
	const AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	const float RangeCm = GetInteractRangeCm();
	if (!IsValid(Owner) || World == nullptr || RangeCm < 0.f)
	{
		return nullptr;
	}

	const FVector OwnerLocation = Owner->GetActorLocation();
	const float RangeSquared = RangeCm * RangeCm;

	// 슬롯은 맵에 깔린 뒤 사라지지 않지만, BeginPlay 캐싱 대신 훑는 쪽을 고른다 —
	// 5칸뿐이고 갱신 주기 타이머에서만 돌기 때문이다 (`UGatherComponent` 와 같은 취지).
	ARanchSlotActor* Nearest = nullptr;
	float NearestDistanceSquared = 0.f;
	for (TActorIterator<ARanchSlotActor> It(World); It; ++It)
	{
		ARanchSlotActor* Slot = *It;
		// 숨은 슬롯은 아직 서버가 열지 않은 확장 칸이다 (M12-05) — 고르면 없는 칸에 요청이 쌓인다.
		if (!IsValid(Slot) || Slot->IsHidden())
		{
			continue;
		}

		const float DistanceSquared = FVector::DistSquared(OwnerLocation, Slot->GetActorLocation());
		if (DistanceSquared > RangeSquared)
		{
			continue;
		}

		if (Nearest == nullptr || DistanceSquared < NearestDistanceSquared)
		{
			Nearest = Slot;
			NearestDistanceSquared = DistanceSquared;
		}
	}

	return Nearest;
}

void URanchInteractComponent::SetTarget(ARanchSlotActor* NewTarget)
{
	if (CurrentTarget == NewTarget)
	{
		return;
	}

	if (IsValid(CurrentTarget))
	{
		CurrentTarget->SetHighlighted(false);
	}

	CurrentTarget = NewTarget;

	if (IsValid(CurrentTarget))
	{
		CurrentTarget->SetHighlighted(true);
	}

	OnRanchSlotTargetChanged.Broadcast(CurrentTarget);
}

#undef LOCTEXT_NAMESPACE
