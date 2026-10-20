#include "Inventory/BackpackComponent.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/ZoneRow.h"
#include "Data/ItemRowLookup.h"
#include "Engine/GameInstance.h"
#include "Equipment/EquipmentComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Inventory/DropBagActor.h"
#include "Player/PlayerVitalsComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/ChestScreenWidget.h"
#include "World/LootChestActor.h"
#include "World/SeaActor.h"
#include "World/SnareZoneRules.h"
#include "World/WaterRules.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"

namespace
{
	// 보물 상자를 해안에서 이만큼 안쪽 뭍에 세운다 — 물가 비탈(`make_island_meshes.py` SLOPE_CM 450)보다 안
	constexpr float TreasureCoastMarginCm = 500.f;

	// 가방에서 손으로 하는 일(버리기·줍기·물 채우기·보물지도) — 요청이 나가는 순간 몸을 숙여 손을 뻗는다 (줍기 클립, DESIGN 8.8).
	// 겉모습 전용이라 서버 판정을 기다리지 않는다(채집과 같은 박자, INBOX 2026-10-15 감사 (d)). 먹기는 원본대로 동작 없음(8.8).
	void PlayHandAction(const UActorComponent* Component)
	{
		if (ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(Component->GetOwner()))
		{
			Body->PlayItemAction(EItemAction::Gather);
		}
	}
}

UBackpackComponent::UBackpackComponent()
{
	// 누를 때만 일한다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryComponentTick.bCanEverTick = false;
	// 가방 스폰·제거를 호스트에 부탁하는 Server RPC 가 이 컴포넌트에 있다.
	SetIsReplicatedByDefault(true);

	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(SnareItemRows::ItemTablePath());
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

void UBackpackComponent::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	// `-SnareBagSoundAt=2/3/4` — 3층 소리 확인 전용 (M49-07). 적은 초마다 버리기↔줍기를 번갈아(홀수째 버리기·짝수째 줍기)
	// 사람 길과 같은 `RequestDrop`·`TryPickupNearest` 로 부른다. 오프라인이라 서버 답은 2층 시험과 같은 가짜 서버
	// (`TestServerOverride`)가 성공 모양으로 준다 — 보유 칸은 안 바뀐다(가방 스폰·소리 길만, CLAUDE.md 8).
	FString AtList;
	UWorld* const World = GetWorld();
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (World && OwnerPawn && OwnerPawn->HasAuthority() && !USnareBackendSubsystem::TestServerOverride &&
		SnareCheckSwitch::Value(TEXT("SnareBagSoundAt="), AtList))
	{
		bInstalledBagSoundServer = true;
		USnareBackendSubsystem::TestServerOverride =
			[](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			if (FunctionName == TEXT("DropItem"))
			{
				static int32 DropSerial = 0;
				Response->SetStringField(TEXT("dropId"), FString::Printf(TEXT("bagsound-%d"), ++DropSerial));
				const TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("itemId"), Args->GetStringField(TEXT("itemId")));
				Item->SetNumberField(TEXT("count"), Args->GetNumberField(TEXT("count")));
				Response->SetArrayField(TEXT("items"), { MakeShared<FJsonValueObject>(Item) });
				Response->SetStringField(TEXT("expiresAtUtc"), (FDateTime::UtcNow() + FTimespan::FromMinutes(10)).ToIso8601());
			}
			else if (FunctionName != TEXT("PickupDrop"))
			{
				OnDone(false, nullptr, TEXT("-SnareBagSoundAt 가짜 서버는 DropItem·PickupDrop 만 답한다"));
				return;
			}
			OnDone(true, Response, FString());
		};
		TArray<FString> Seconds;
		AtList.ParseIntoArray(Seconds, TEXT("/")); // 쉼표는 `FParse::Value` 가 값 끝으로 읽는다 (M47-06)
		for (int32 Index = 0; Index < Seconds.Num(); ++Index)
		{
			const bool bDrop = Index % 2 == 0;
			FTimerHandle Unused;
			World->GetTimerManager().SetTimer(Unused, FTimerDelegate::CreateWeakLambda(this, [this, bDrop]()
			{
				const bool bAsked = bDrop ? RequestDrop(TEXT("Item_Club"), 1) : TryPickupNearest();
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 가방 %s (-SnareBagSoundAt) — 물음 %s"), bDrop ? TEXT("버리기") : TEXT("줍기"),
					bAsked ? TEXT("예") : TEXT("아니오"));
			}), FCString::Atof(*Seconds[Index]), false);
		}
	}
#endif
}

void UBackpackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bInstalledBagSoundServer)
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		bInstalledBagSoundServer = false;
	}
	Super::EndPlay(EndPlayReason);
}

bool UBackpackComponent::ApplyItemBuff(FName ItemID)
{
	const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, ItemID);
	if (Row == nullptr || Row->BuffKind == EBuffKind::None)
	{
		return false;
	}
	if (!IsValid(Vitals) && GetOwner() != nullptr)
	{
		Vitals = GetOwner()->FindComponentByClass<UPlayerVitalsComponent>();
	}
	return IsValid(Vitals) && Vitals->ApplyBuff(Row->BuffKind, Row->BuffValue, Row->BuffSeconds);
}

// ponytail: 호스트는 "먹었다" 를 믿는다 — 조작 클라이언트는 안 먹고 버프를 부를 수 있다(ClaimLoot 같은 신뢰).
//           버프는 재화가 아닌 몸 상태(최대 이동 +30%)라 PVE 협동에서 받아들인다. 커지면 ConsumeItem 서명 토큰으로.
void UBackpackComponent::Server_ApplyItemBuff_Implementation(FName ItemID)
{
	ApplyItemBuff(ItemID);
}

FName UBackpackComponent::GetHomeTag()
{
	return TEXT("SnareHome");
}

void UBackpackComponent::BindTargets(USnareBackendSubsystem* InBackend, UPlayerVitalsComponent* InVitals)
{
	Backend = InBackend;
	Vitals = InVitals;
}

USnareBackendSubsystem* UBackpackComponent::ResolveBackend()
{
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (IsValid(OwnerPawn) && OwnerPawn->GetController() != nullptr && !OwnerPawn->IsLocallyControlled())
	{
		return nullptr;
	}
	if (!IsValid(Backend) && GetOwner() != nullptr && GetOwner()->GetGameInstance() != nullptr)
	{
		Backend = GetOwner()->GetGameInstance()->GetSubsystem<USnareBackendSubsystem>();
	}
	return Backend;
}

TArray<FInventorySlot> UBackpackComponent::ParseItems(const TSharedPtr<FJsonObject>& Response)
{
	TArray<FInventorySlot> Out;
	const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
	if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("items"), Items))
	{
		return Out;
	}
	for (const TSharedPtr<FJsonValue>& Value : *Items)
	{
		const TSharedPtr<FJsonObject> Item = Value.IsValid() ? Value->AsObject() : nullptr;
		FString ItemId;
		int32 Count = 0;
		if (Item.IsValid() && Item->TryGetStringField(TEXT("itemId"), ItemId) && Item->TryGetNumberField(TEXT("count"), Count))
		{
			FInventorySlot Slot;
			Slot.ItemID = FName(*ItemId);
			Slot.Count = Count;
			Out.Add(Slot);
		}
	}
	return Out;
}

void UBackpackComponent::Finish(bool bAccepted, const FString& Code, const FString& Message)
{
	if (bAccepted)
	{
		// 늘고 준 개수는 이어서 오는 스냅샷이 채운다 (CLAUDE.md 8).
		++RefetchCount;
		if (IsValid(Backend))
		{
			Backend->FetchPlayerState();
		}
	}
	OnReply.Broadcast(bAccepted, Code, Message);
}

bool UBackpackComponent::RequestConsume(FName ItemID)
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr || ItemID.IsNone())
	{
		return false;
	}
	if (!IsValid(Vitals) && GetOwner() != nullptr)
	{
		Vitals = GetOwner()->FindComponentByClass<UPlayerVitalsComponent>();
	}

	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("itemId"), ItemID.ToString());
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("ConsumeItem"), Args,
		[WeakThis, ItemID](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			if (!SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message))
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] ConsumeItem 거절 %s (%s)"), *Code, *Message);
				Self->Finish(false, Code, Message);
				return;
			}
			// 회복량은 서버가 정한 값만 쓴다 (DESIGN 8.10 — 체력은 응답을 받은 쪽이 채운다).
			double Heal = 0.0;
			Response->TryGetNumberField(TEXT("heal"), Heal);
			// 음수는 독 — 광대버섯 −20 (DESIGN 13.4). 체력 0 이면 3.1 쓰러짐 그대로다.
			if (IsValid(Self->Vitals))
			{
				if (Heal < 0.0)
				{
					Self->Vitals->ApplyDamage(static_cast<float>(-Heal));
				}
				else
				{
					Self->Vitals->ApplyHeal(static_cast<float>(Heal));
				}
			}
			// 버프(13.6)는 서버가 먹기를 받아들인 뒤에만 — 내 몸과 호스트 복제본 둘 다에 건다(이동 속도 예측이 안 어긋나게).
			if (Self->ApplyItemBuff(ItemID) && Self->GetOwnerRole() != ROLE_Authority)
			{
				Self->Server_ApplyItemBuff(ItemID);
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] ConsumeItem 성공 - 회복 %.0f"), Heal);
			// 먹은 표시는 받아들여진 뒤에만 — 원본처럼 동작 없이(든 도구 그대로) 머리 위 「+N」 과 씹는 소리만 (DESIGN 8.8 「먹기」 · 2.2).
			if (ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(Self->GetOwner()))
			{
				Body->ShowHealPop(FMath::RoundToInt(Heal));
				Body->PlayEatVoice(static_cast<float>(Heal));
			}
			Self->Finish(true, FString(), FString());
		});
	return true;
}

bool UBackpackComponent::RequestFill(FName ItemID)
{
	// #0203 물가 판정은 호스트 몫이다(낚시 `RollBiteOnAuthority`·채집 `Server_GatherTarget` 과 같은 길).
	// 기기는 "채우기" 만 청하고, 호스트가 자기 월드의 폰 위치로 400cm 안 물가를 본 뒤에만 부르게 한다.
	if (ResolveBackend() == nullptr || ItemID.IsNone())
	{
		return false;
	}
	PlayHandAction(this);
	if (GetOwnerRole() == ROLE_Authority)
	{
		ApproveFillOnAuthority(ItemID);
	}
	else
	{
		Server_RequestFill(ItemID);
	}
	return true;
}

void UBackpackComponent::Server_RequestFill_Implementation(FName ItemID)
{
	ApproveFillOnAuthority(ItemID);
}

void UBackpackComponent::ApproveFillOnAuthority(FName ItemID)
{
	const FWaterActivityRow* const Water = SnareWater::LoadRow();
	const bool bNear = Water != nullptr && GetOwner() != nullptr
		&& SnareWater::IsNearWater(this, GetOwner()->GetActorLocation(), Water->CastRangeCm);
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	// 원격 폰이면 답은 그 기기로 — CloudScript 는 그 기기가 자기 PlayFab 계정으로 부른다(호스트는 남의 계정으로 안 묻는다).
	const bool bRemote = IsValid(OwnerPawn) && !OwnerPawn->IsLocallyControlled() && GetNetMode() != NM_Standalone;
	UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] 물가 판정(호스트) %s - %s"), *ItemID.ToString(), bNear ? TEXT("가까움") : TEXT("멂"));
	if (bRemote)
	{
		if (bNear)
		{
			Client_BeginFill(ItemID);
		}
		else
		{
			Client_RejectFill();
		}
	}
	else if (bNear)
	{
		CallFillBottle(ItemID);
	}
	else
	{
		Client_RejectFill_Implementation();
	}
}

void UBackpackComponent::Client_BeginFill_Implementation(FName ItemID)
{
	CallFillBottle(ItemID);
}

void UBackpackComponent::Client_RejectFill_Implementation()
{
	// 서버에 묻지 않았다 — 보유는 그대로, 사유만 알린다 (CLAUDE.md 8).
	Finish(false, TEXT("not_near_water"),
		NSLOCTEXT("Snarehaven", "BackpackFillFar", "물가에서만 채울 수 있다").ToString());
}

void UBackpackComponent::CallFillBottle(FName ItemID)
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr)
	{
		return;
	}
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("itemId"), ItemID.ToString());
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("FillBottle"), Args,
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			const bool bAccepted = SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
			UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] FillBottle %s %s"), bAccepted ? TEXT("성공") : TEXT("거절"), *Code);
			Self->Finish(bAccepted, Code, Message);
		});
}

bool UBackpackComponent::RequestEquipHat(FName ItemID)
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr || ItemID.IsNone())
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("itemId"), ItemID.ToString());
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("EquipHat"), Args,
		[WeakThis, ItemID](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			const bool bAccepted = SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
			if (!bAccepted)
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] EquipHat 거절 %s (%s)"), *Code, *Message);
				// M13-21 화면이 답 전에 붙인 겉모습을 서버가 인정한 모자(없으면 빈 머리)로 되돌린다.
				Self->ApplyBackendHat(Self->WornHatID);
			}
			else
			{
				Self->WornHatID = ItemID;
			}
			Self->Finish(bAccepted, Code, Message);
		});
	return true;
}

void UBackpackComponent::ApplyBackendHat(FName HatID)
{
	WornHatID = HatID;
	UEquipmentComponent* const Equipment = GetOwner() != nullptr ? GetOwner()->FindComponentByClass<UEquipmentComponent>() : nullptr;
	if (!IsValid(Equipment))
	{
		return;
	}
	// 서버 기록대로 되돌리거나 불러오는 길이다 — 플레이어가 지금 바꾼 것이 아니라 소리 없이 (M47-03).
	const UEquipmentComponent::FQuietChanges Quiet(Equipment);
	if (HatID.IsNone())
	{
		Equipment->UnequipSlot(EEquipSlot::Head); // #0204 서버가 "안 씀" 이라 하면 겉모습도 뗀다
	}
	else
	{
		Equipment->EquipItemByItemID(HatID);
	}
}

int32 UBackpackComponent::ApplyBackendOutfit(const FJsonObject& Outfit)
{
	UEquipmentComponent* const Equipment = GetOwner() != nullptr ? GetOwner()->FindComponentByClass<UEquipmentComponent>() : nullptr;
	if (!IsValid(Equipment))
	{
		return 0;
	}
	int32 Worn = 0;
	// 불러오기 복원 — 소리 없이 (M47-03 무음 길).
	const UEquipmentComponent::FQuietChanges Quiet(Equipment);
	for (const EEquipSlot Slot : { EEquipSlot::Head, EEquipSlot::FaceAccessory, EEquipSlot::OutfitTop,
			 EEquipSlot::OutfitBottom, EEquipSlot::Shoes, EEquipSlot::Gloves, EEquipSlot::Backpack })
	{
		FString ItemID;
		const bool bWorn = Outfit.TryGetStringField(GetOutfitSlotKey(Slot), ItemID) && !ItemID.IsEmpty();
		// 서버 답에 없는 칸 = 안 입은 것(또는 이제 안 가진 것) — 남아 있던 덮개를 벗긴다.
		Equipment->SetCosmeticByItemID(Slot, bWorn ? FName(*ItemID) : NAME_None);
		Worn += bWorn && !Equipment->GetCosmeticItem(Slot).IsNone() ? 1 : 0;
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] 서버 꾸미기 복원 %d칸"), Worn);
	return Worn;
}

void UBackpackComponent::ApplyBackendInventory(const TArray<FInventorySlot>& Slots)
{
	const bool bHeld = Slots.ContainsByPredicate([this](const FInventorySlot& Slot)
		{
			return Slot.ItemID == WornHatID && Slot.Count > 0;
		});
	if (!WornHatID.IsNone() && !bHeld)
	{
		ApplyBackendHat(NAME_None);
	}
}

bool UBackpackComponent::RequestUnequipHat()
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("itemId"), FString());
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("EquipHat"), Args,
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			const bool bAccepted = SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
			UEquipmentComponent* const Equipment =
				Self->GetOwner() != nullptr ? Self->GetOwner()->FindComponentByClass<UEquipmentComponent>() : nullptr;
			if (bAccepted)
			{
				Self->WornHatID = NAME_None;
				if (IsValid(Equipment))
				{
					Equipment->UnequipSlot(EEquipSlot::Head);
				}
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] EquipHat 벗기 %s %s"), bAccepted ? TEXT("성공") : TEXT("거절"), *Code);
			Self->Finish(bAccepted, Code, Message);
		});
	return true;
}

FString UBackpackComponent::GetOutfitSlotKey(EEquipSlot Slot)
{
	// 서버 `OUTFIT_SLOTS` 와 한 줄씩 맞는다 — 손에 드는 칸(무기·도구)은 꾸미기 칸이 아니다.
	switch (Slot)
	{
	case EEquipSlot::Head:          return TEXT("head");
	case EEquipSlot::FaceAccessory: return TEXT("face");
	case EEquipSlot::OutfitTop:     return TEXT("top");
	case EEquipSlot::OutfitBottom:  return TEXT("bottom");
	case EEquipSlot::Shoes:         return TEXT("shoes");
	case EEquipSlot::Gloves:        return TEXT("gloves");
	case EEquipSlot::Backpack:      return TEXT("back");
	default:                        return FString();
	}
}

bool UBackpackComponent::RequestSetOutfit(EEquipSlot Slot, FName ItemID)
{
	const FString SlotKey = GetOutfitSlotKey(Slot);
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr || SlotKey.IsEmpty())
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("slot"), SlotKey);
	// 빈 문자열 = 벗기 (서버 규약). NAME_None.ToString() 은 "None" 이라 따로 거른다.
	Args->SetStringField(TEXT("itemId"), ItemID.IsNone() ? FString() : ItemID.ToString());
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("SetOutfit"), Args,
		[WeakThis, Slot, ItemID](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			const bool bAccepted = SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
			if (bAccepted)
			{
				UEquipmentComponent* const Equipment =
					Self->GetOwner() != nullptr ? Self->GetOwner()->FindComponentByClass<UEquipmentComponent>() : nullptr;
				if (IsValid(Equipment))
				{
					Equipment->SetCosmeticByItemID(Slot, ItemID);
				}
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] SetOutfit %s %s %s"), *ItemID.ToString(),
				bAccepted ? TEXT("성공") : TEXT("거절"), *Code);
			Self->Finish(bAccepted, Code, Message);
		});
	return true;
}

bool UBackpackComponent::RequestDrop(FName ItemID, int32 Count)
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr || ItemID.IsNone() || Count <= 0 || GetOwner() == nullptr)
	{
		return false;
	}
	PlayHandAction(this);

	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("itemId"), ItemID.ToString());
	Args->SetNumberField(TEXT("count"), Count);
	const FVector Where = GetOwner()->GetActorLocation();
	const FString OwnerId = Server->GetPlayerSnapshot().PlayFabId;
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("DropItem"), Args,
		[WeakThis, Where, OwnerId](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			if (!SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message))
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] DropItem 거절 %s (%s)"), *Code, *Message);
				Self->PlayBagCue(SnareSound::EBagAction::Drop, false, Code, Where);
				Self->Finish(false, Code, Message);
				return;
			}
			Self->Server_SpawnDropBag(OwnerId, Response->GetStringField(TEXT("dropId")), ParseItems(Response),
				Response->GetStringField(TEXT("expiresAtUtc")), Where);
			Self->PlayBagCue(SnareSound::EBagAction::Drop, true, FString(), Where);
			Self->Finish(true, FString(), FString());
		});
	return true;
}

bool UBackpackComponent::RequestOpenChest(int32 ChestIndex, const FString& TreasureID, FChestDone OnDone)
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr || GetOwner() == nullptr || (ChestIndex <= 0 && TreasureID.IsEmpty()))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	if (TreasureID.IsEmpty())
	{
		Args->SetNumberField(TEXT("chest"), ChestIndex);
	}
	else
	{
		Args->SetStringField(TEXT("treasureId"), TreasureID);
	}
	const FVector Where = GetOwner()->GetActorLocation();
	const FString OwnerId = Server->GetPlayerSnapshot().PlayFabId;
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("OpenChest"), Args,
		[WeakThis, Where, OwnerId, TreasureID, OnDone = MoveTemp(OnDone)](
			bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			const bool bAccepted = SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
			UE_LOG(LogSnarehaven, Log, TEXT("[Chest] OpenChest %s %s"), bAccepted ? TEXT("성공") : TEXT("거절"), *Code);
			// 보물은 한 번 열면(또는 사라졌거나 남의 것이면) 끝이다 — 서버 기록도 비었다.
			if (!TreasureID.IsEmpty() && (bAccepted || Code == TEXT("no_treasure") || Code == TEXT("expired")))
			{
				Self->ClearTreasure();
			}
			const TSharedPtr<FJsonObject>* Drop = nullptr;
			if (bAccepted && Response->TryGetObjectField(TEXT("drop"), Drop))
			{
				// 가방이 모자랐다 — 서버가 적은 가방을 발밑에 둔다 (`RequestDrop` 과 같은 모양).
				Self->Server_SpawnDropBag(OwnerId, (*Drop)->GetStringField(TEXT("dropId")), ParseItems(*Drop),
					(*Drop)->GetStringField(TEXT("expiresAtUtc")), Where);
			}
			// 위험구역 상자·보물 상자 같은 길 — 받아들인 답 한 번에 소리 한 번, 거절(`recharging` 등)은 무음.
			Self->PlayBagCue(SnareSound::EBagAction::OpenChest, bAccepted, Code, Where);
			if (OnDone)
			{
				OnDone(bAccepted, Code, Message, Response);
			}
			Self->Finish(bAccepted, Code, Message);
		});
	return true;
}

bool UBackpackComponent::RequestOpenVault(int32 RuinIndex, FChestDone OnDone)
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr || GetOwner() == nullptr || RuinIndex <= 0)
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetNumberField(TEXT("ruin"), RuinIndex);
	const FVector Where = GetOwner()->GetActorLocation();
	const FString OwnerId = Server->GetPlayerSnapshot().PlayFabId;
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("OpenVault"), Args,
		[WeakThis, Where, OwnerId, OnDone = MoveTemp(OnDone)](
			bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			const bool bAccepted = SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
			UE_LOG(LogSnarehaven, Log, TEXT("[Chest] OpenVault %s %s"), bAccepted ? TEXT("성공") : TEXT("거절"), *Code);
			const TSharedPtr<FJsonObject>* Drop = nullptr;
			if (bAccepted && Response->TryGetObjectField(TEXT("drop"), Drop))
			{
				Self->Server_SpawnDropBag(OwnerId, (*Drop)->GetStringField(TEXT("dropId")), ParseItems(*Drop),
					(*Drop)->GetStringField(TEXT("expiresAtUtc")), Where);
			}
			// 금고도 상자 소리 — 받아들인 답 한 번에 한 번, 거절(`no_key`·`recharging`)은 무음.
			Self->PlayBagCue(SnareSound::EBagAction::OpenChest, bAccepted, Code, Where);
			if (OnDone)
			{
				OnDone(bAccepted, Code, Message, Response);
			}
			Self->Finish(bAccepted, Code, Message);
		});
	return true;
}

bool UBackpackComponent::SendCrateCall(const TCHAR* Function, const TSharedPtr<FJsonObject>& Args, FChestDone OnDone)
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr)
	{
		return false;
	}
	const FString Name(Function);
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(Name, Args,
		[WeakThis, Name, OnDone = MoveTemp(OnDone)](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			FString Code;
			FString Message;
			const bool bAccepted = SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
			UE_LOG(LogSnarehaven, Log, TEXT("[Crate] %s %s %s"), *Name, bAccepted ? TEXT("성공") : TEXT("거절"), *Code);
			if (OnDone)
			{
				OnDone(bAccepted, Code, Message, Response);
			}
			Self->Finish(bAccepted, Code, Message);
		});
	return true;
}

bool UBackpackComponent::RequestOpenCrate(FName CrateID, FChestDone OnDone)
{
	if (CrateID.IsNone())
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("crateId"), CrateID.ToString());
	return SendCrateCall(TEXT("OpenCrate"), Args, MoveTemp(OnDone));
}

bool UBackpackComponent::RequestBuyCrateKeyWithGems(FChestDone OnDone)
{
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("itemId"), GetCrateKeyID().ToString());
	Args->SetNumberField(TEXT("count"), 1);
	Args->SetStringField(TEXT("currency"), TEXT("GM"));
	return SendCrateCall(TEXT("BuyItem"), Args, MoveTemp(OnDone));
}

FName UBackpackComponent::GetCrateKeyID()
{
	return FName(TEXT("item.crate_key"));
}

bool UBackpackComponent::RequestUseTreasureMap()
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr)
	{
		return false;
	}
	PlayHandAction(this);
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("UseTreasureMap"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			const bool bAccepted = SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
			// `busy` 는 이미 연 보물을 돌려준다 — 맵을 옮겨 잃은 표식을 되찾는 길이다.
			const TSharedPtr<FJsonObject>* Busy = nullptr;
			const TSharedPtr<FJsonObject> Source = bAccepted ? Response
				: (Code == TEXT("busy") && Response.IsValid() && Response->TryGetObjectField(TEXT("treasure"), Busy)) ? *Busy
				: nullptr;
			SnareChestLoot::FTreasure Treasure;
			if (SnareChestLoot::ParseTreasure(Source, Treasure))
			{
				Self->SetTreasure(Treasure);
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Chest] UseTreasureMap %s %s - 보물 %s %s u=%.2f v=%.2f"),
				bAccepted ? TEXT("성공") : TEXT("거절"), *Code, *Treasure.TreasureID, *Treasure.ZoneID.ToString(),
				Treasure.U, Treasure.V);
			Self->Finish(bAccepted, Code, Message);
		});
	return true;
}

bool UBackpackComponent::RequestRestoreTreasure(TFunction<void(bool bAccepted)> OnDone)
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr)
	{
		return false;
	}
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("GetTreasure"), MakeShared<FJsonObject>(),
		[WeakThis, OnDone = MoveTemp(OnDone)](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			const bool bAccepted = Self != nullptr && SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
			if (OnDone)
			{
				OnDone(bAccepted);
			}
			if (!bAccepted)
			{
				// 못 물었으면 아는 것을 그대로 둔다 — 지우는 것은 서버가 "없다" 고 답했을 때만.
				return;
			}
			const TSharedPtr<FJsonObject>* Found = nullptr;
			SnareChestLoot::FTreasure Treasure;
			if (Response->TryGetObjectField(TEXT("treasure"), Found) && SnareChestLoot::ParseTreasure(*Found, Treasure))
			{
				Self->SetTreasure(Treasure);
			}
			else
			{
				Self->ClearTreasure();
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Chest] GetTreasure - 보물 %s %s u=%.2f v=%.2f 상자 %s"),
				Treasure.IsSet() ? *Treasure.TreasureID : TEXT("없음"), *Treasure.ZoneID.ToString(), Treasure.U, Treasure.V,
				Self->GetTreasureChest() != nullptr ? TEXT("섬") : TEXT("안 섬(다른 맵)"));
		});
	return true;
}

void UBackpackComponent::SetTreasure(const SnareChestLoot::FTreasure& Treasure)
{
	ClearTreasure();
	ActiveTreasure = Treasure;

	UWorld* const World = GetWorld();
	const UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	const FZoneRow* const Zone = SnareZones::FindZoneRow(ZoneTable, Treasure.ZoneID);
	if (World == nullptr || Zone == nullptr || !SnareZones::IsRegionRow(*Zone)
		|| Zone->LevelName != FName(*UWorld::RemovePIEPrefix(World->GetMapName())))
	{
		// 다른 맵의 칸이다 — 기억만 한다. 그 맵에서 지도를 다시 쓰면 `busy` 가 자리를 돌려준다.
		return;
	}

	// 바닥을 찾는다 — 길이는 칸 반변 (`ADangerZoneActor::SpawnWorldEvent` 와 같은 방식).
	FVector2D XY = SnareChestLoot::TreasureWorldXY(*Zone, Treasure.U, Treasure.V);
	// 새 지도 존은 정사각형 틀의 일부만 찬다 — 틀 속 점이 바다·다른 존이면 가장 가까운 그 존 뭍으로 옮긴다(같은 u·v 면 늘 같은 자리)
	const ASeaActor* const Sea = ASeaActor::FindSea(this);
	if (Sea != nullptr && Sea->HasField() && Zone->FieldCode > 0 && !SnareZones::IsInsideRegion(this, *Zone, FVector(XY, 0.0)))
	{
		Sea->FindZonePoint(static_cast<uint8>(Zone->FieldCode), XY, TreasureCoastMarginCm, 2.f * Zone->RegionHalfExtentCm, XY);
	}
	const FVector Column(XY.X, XY.Y, 0.f);
	const FVector Up(0.f, 0.f, Zone->RegionHalfExtentCm);
	FHitResult Hit;
	const FVector Ground = World->LineTraceSingleByChannel(Hit, Column + Up, Column - Up, ECC_WorldStatic,
		FCollisionQueryParams(SCENE_QUERY_STAT(TreasureGround), false)) ? Hit.ImpactPoint : Column;

	// 이 사람만의 보물이라 복제하지 않는다 — 남의 기기엔 안 선다.
	const FTransform SpawnAt(Ground);
	ALootChestActor* const Chest = World->SpawnActorDeferred<ALootChestActor>(ALootChestActor::StaticClass(), SpawnAt,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Chest != nullptr)
	{
		Chest->SetReplicates(false);
		Chest->SetTreasureID(Treasure.TreasureID);
		Chest->FinishSpawning(SpawnAt);
		TreasureChest = Chest;
	}
}

void UBackpackComponent::ClearTreasure()
{
	ActiveTreasure = SnareChestLoot::FTreasure();
	// 열기 결과를 보는 중이면 상자는 남긴다 — 멀어질 때 상자가 스스로 치운다(`ALootChestActor::UpdateProximity`).
	ALootChestActor* const Chest = TreasureChest.Get();
	if (Chest != nullptr && !(IsValid(Chest->GetChestScreen()) && Chest->GetChestScreen()->IsInViewport()))
	{
		Chest->Destroy();
	}
	TreasureChest.Reset();
}

bool UBackpackComponent::RequestDropOnDeath()
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr || GetOwner() == nullptr)
	{
		return false;
	}

	// 죽은 자리를 **지금** 잡는다 — 응답이 올 때쯤엔 이미 부활 자리로 옮겨졌을 수 있다.
	const FVector Where = GetOwner()->GetActorLocation();
	const FString OwnerId = Server->GetPlayerSnapshot().PlayFabId;
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	Server->CallServerFunction(TEXT("DropOnDeath"), MakeShared<FJsonObject>(),
		[WeakThis, Where, OwnerId](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			if (!SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message))
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] DropOnDeath 거절 %s (%s)"), *Code, *Message);
				Self->PlayBagCue(SnareSound::EBagAction::DeathDrop, false, Code, Where);
				Self->Finish(false, Code, Message);
				return;
			}
			// 서버가 떨군 게 없다고 하면(배낭이 비었거나 전부 지키는 것) 가방을 두지 않는다.
			bool bDropped = false;
			Response->TryGetBoolField(TEXT("dropped"), bDropped);
			if (bDropped)
			{
				Self->Server_SpawnDropBag(OwnerId, Response->GetStringField(TEXT("dropId")), ParseItems(Response),
					Response->GetStringField(TEXT("expiresAtUtc")), Where);
			}
			// 가방을 두지 않았으면(빈 배낭) 떨어지는 게 없으니 무음 — 받아들임 = 가방을 둠.
			Self->PlayBagCue(SnareSound::EBagAction::DeathDrop, bDropped, FString(), Where);
			UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] DropOnDeath 성공 - 가방 %s"), bDropped ? TEXT("둠") : TEXT("없음"));
			Self->Finish(true, FString(), FString());
		});
	return true;
}

bool UBackpackComponent::RequestPickup(ADropBagActor* Bag)
{
	USnareBackendSubsystem* const Server = ResolveBackend();
	if (Server == nullptr || !IsValid(Bag) || Bag->GetDropId().IsEmpty() || PickupsInFlight.Contains(Bag->GetDropId()))
	{
		return false;
	}

	const FString DropId = Bag->GetDropId();
	PickupsInFlight.Add(DropId);
	PlayHandAction(this);
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("ownerId"), Bag->GetOwnerPlayFabId());
	Args->SetStringField(TEXT("dropId"), DropId);
	const TWeakObjectPtr<UBackpackComponent> WeakThis(this);
	const TWeakObjectPtr<ADropBagActor> WeakBag(Bag);
	// 답이 오기 전에 가방이 치워질 수 있어 소리 자리는 지금 잡는다.
	const FVector Where = Bag->GetActorLocation();
	Server->CallServerFunction(TEXT("PickupDrop"), Args,
		[WeakThis, WeakBag, DropId, Where](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			UBackpackComponent* const Self = WeakThis.Get();
			FString Code;
			FString Message;
			if (Self == nullptr)
			{
				return;
			}
			Self->PickupsInFlight.Remove(DropId);
			const bool bAccepted = SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
			// 주웠거나, 서버가 이미 없다고 한 가방은 모두의 화면에서 치운다. 가득 참·네트워크 실패는 그대로 둔다.
			if (bAccepted || Code == TEXT("already_taken") || Code == TEXT("expired"))
			{
				if (WeakBag.IsValid())
				{
					Self->Server_RemoveDropBag(WeakBag.Get());
				}
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Backpack] PickupDrop %s %s %s"), *DropId,
				bAccepted ? TEXT("성공") : *Code, *Message);
			// 가득 참·already_taken·expired 는 규칙이 무음으로 — 주운 응답에만 한 번.
			Self->PlayBagCue(SnareSound::EBagAction::Pickup, bAccepted, Code, Where);
			Self->Finish(bAccepted, Code, Message);
		});
	return true;
}

ADropBagActor* UBackpackComponent::FindNearestBagInRange(float RangeCm) const
{
	const AActor* const Owner = GetOwner();
	UWorld* const World = GetWorld();
	if (!IsValid(Owner) || World == nullptr || RangeCm < 0.f)
	{
		return nullptr;
	}
	// 가방은 드물고 누를 때만 찾는다 — `UGatherComponent::FindNearestGatherableInRange` 와 같은 훑기.
	ADropBagActor* Nearest = nullptr;
	float NearestSquared = RangeCm * RangeCm;
	for (TActorIterator<ADropBagActor> It(World); It; ++It)
	{
		if (!IsValid(*It))
		{
			continue;
		}
		const float DistanceSquared = FVector::DistSquared(Owner->GetActorLocation(), It->GetActorLocation());
		if (DistanceSquared <= NearestSquared)
		{
			Nearest = *It;
			NearestSquared = DistanceSquared;
		}
	}
	return Nearest;
}

bool UBackpackComponent::TryPickupNearest()
{
	// 줍는 거리 = 자동 상호작용 거리(근접 공격 사거리, DESIGN 3.1) — 새 숫자를 만들지 않는다.
	const UMeleeAttackComponent* const Melee =
		GetOwner() != nullptr ? GetOwner()->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
	ADropBagActor* const Bag = IsValid(Melee) ? FindNearestBagInRange(Melee->GetAttackRangeCm()) : nullptr;
	return Bag != nullptr && RequestPickup(Bag);
}

AActor* UBackpackComponent::FindRespawnHome(UWorld* World, const AController* Controller)
{
	if (World == nullptr || Controller == nullptr)
	{
		return nullptr;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(GetHomeTag()) &&
			(It->GetOwner() == Controller || (Controller->GetPawn() != nullptr && It->GetOwner() == Controller->GetPawn())))
		{
			return *It;
		}
	}
	return nullptr;
}

void UBackpackComponent::Server_SpawnDropBag_Implementation(const FString& OwnerId, const FString& DropId,
	const TArray<FInventorySlot>& Items, const FString& ExpiresAtUtc, FVector Location)
{
	UWorld* const World = GetWorld();
	if (World == nullptr || DropId.IsEmpty())
	{
		return;
	}
	// 받은 자리는 폰 캡슐 가운데(바닥 위 약 88cm)다 — 가방 메시는 받침점이 바닥이라 바닥을 쏴서 내려놓는다 (M13-38).
	// **물체 종류**로 쏜다(`ASnarePlacedStructureActor::CellToWorld` 와 같은 이유 — 채널이면 캐릭터 캡슐에 맞는다).
	// 가방은 충돌이 없어 안 맞는다. 못 맞으면(바다 위 등) 받은 자리 그대로.
	constexpr float TraceUpCm = 50.f;
	constexpr float TraceDownCm = 500.f;
	FHitResult Hit;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(DropBagGround), false, GetOwner());
	if (World->LineTraceSingleByObjectType(Hit, Location + FVector(0.f, 0.f, TraceUpCm), Location - FVector(0.f, 0.f, TraceDownCm),
		FCollisionObjectQueryParams(ECC_WorldStatic), TraceParams))
	{
		Location.Z = Hit.ImpactPoint.Z;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (ADropBagActor* const Bag = World->SpawnActor<ADropBagActor>(Location, FRotator::ZeroRotator, Params))
	{
		Bag->InitFromServer(OwnerId, DropId, Items, ExpiresAtUtc);
	}
}

void UBackpackComponent::PlayBagCue(SnareSound::EBagAction Action, bool bAccepted, const FString& ErrorCode, const FVector& Where)
{
	const SnareSound::FChangeCue Cue = SnareSound::BagCue(Action, bAccepted, ErrorCode);
	if (Cue.Cue.IsNone())
	{
		return;
	}
	++BagCueCount;
	LastBagCue = Cue.Cue;
	UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 가방 %s — 크기 %.2f · %s"), *Cue.Cue.ToString(), Cue.Volume,
		Cue.bWorld ? TEXT("모든 기기") : TEXT("내 기기만"));
	if (Cue.bWorld)
	{
		Server_PlayBagCue(Cue.Cue, Cue.Volume, Where);
		return;
	}
	USnareSoundSubsystem::Play2D(this, Cue.Cue, Cue.Volume);
}

void UBackpackComponent::Server_PlayBagCue_Implementation(FName Cue, float Volume, FVector Where)
{
	Multicast_PlayBagCue(Cue, Volume, Where);
}

void UBackpackComponent::Multicast_PlayBagCue_Implementation(FName Cue, float Volume, FVector Where)
{
	USnareSoundSubsystem::PlayAt(this, Cue, Where, Volume);
}

void UBackpackComponent::Server_RemoveDropBag_Implementation(ADropBagActor* Bag)
{
	if (IsValid(Bag))
	{
		Bag->Destroy();
	}
}
