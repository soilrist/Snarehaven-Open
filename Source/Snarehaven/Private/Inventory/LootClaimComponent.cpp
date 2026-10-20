#include "Inventory/LootClaimComponent.h"
#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Data/ItemRowLookup.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Inventory/InventoryComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

namespace
{
	// 획득 글에 이름을 적는 최대 종류 수 (DESIGN 3.3 「획득 팝업」) — 넘치면 `외 N종`.
	constexpr int32 GrantPopupMaxKinds = 3;

	// 서버 `GATHER_ITEMS` 의 거울 — 채집물(나무·향나무 8.10 · 산딸기 · 13.4 채집 더미). 위와 같은 서버 계약이다.
	// 전엔 나무·산딸기만 있어 향나무·더미 수확이 대기열에 영영 남았다(M13-05 에서 마체테 +1 을 붙이며 바로잡음).
	const TCHAR* const GatherItemIDs[] = {
		TEXT("item.wood"), TEXT("item.cedar"), TEXT("item.berry"),
		TEXT("item.chanterelle"), TEXT("item.penny_bun"), TEXT("item.yellowleg_bonnet"), TEXT("item.fly_amanita"),
		TEXT("item.resin"), TEXT("item.kingcup"), TEXT("item.windflower"), TEXT("item.hepatica"), TEXT("item.poppy"),
		TEXT("item.fireweed"), TEXT("item.blackberry"), TEXT("item.mezereon"), TEXT("item.cloudberry"),
		TEXT("item.black_lotus"), TEXT("item.blueberry"), TEXT("item.lingonberry"),
		TEXT("item.sugar_beet"), TEXT("item.wheat"),
		TEXT("item.honey_mushroom"), TEXT("item.king_stropharia"), TEXT("item.oyster_mushroom"), TEXT("item.vipers_mane"),
		TEXT("item.bog_flower"), TEXT("item.water_lily"),
		// DESIGN 8.16 광맥 덩어리 — 서버 `ORE_CHUNK_PRICE` 키.
		TEXT("item.coal_chunk"), TEXT("item.copper_chunk"), TEXT("item.iron_chunk"), TEXT("item.titanium_chunk"),
		TEXT("item.silver_chunk"), TEXT("item.gold_chunk")
	};

	/** `^item\.[a-z_]+$` 모양의 뒷부분인가 — 서버 정규식과 같은 글자만 받는다. */
	bool IsLowerSnake(const FString& Text)
	{
		if (Text.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Char : Text)
		{
			if (!((Char >= TEXT('a') && Char <= TEXT('z')) || Char == TEXT('_')))
			{
				return false;
			}
		}
		return true;
	}
}

ULootClaimComponent::ULootClaimComponent()
{
	// 주기는 타이머가 센다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryComponentTick.bCanEverTick = false;

	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(SnareItemRows::ItemTablePath());
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

FString ULootClaimComponent::GetClaimFunctionName()
{
	return TEXT("ClaimLoot");
}

bool ULootClaimComponent::IsKillLine(FName LineID)
{
	const FString Id = LineID.ToString();
	const FString Prefix = TEXT("animal.");
	return Id.StartsWith(Prefix, ESearchCase::CaseSensitive) && IsLowerSnake(Id.Mid(Prefix.Len()));
}

bool ULootClaimComponent::IsClaimableLoot(FName ItemID)
{
	const FString Id = ItemID.ToString();
	const FString Prefix = TEXT("item.");
	const FString Body = Id.Mid(Prefix.Len());
	if (!Id.StartsWith(Prefix, ESearchCase::CaseSensitive) || !IsLowerSnake(Body))
	{
		return false;
	}

	// 포획 · 채집물. 날고기·가죽·전리품은 사냥 드롭이라 `kills` 몫이다 (지시 ① — 아이템으로 보내면 서버가 거절한다).
	if (Body.StartsWith(TEXT("pet_")) && Body.Len() > 4)
	{
		return true;
	}
	for (const TCHAR* const Gathered : GatherItemIDs)
	{
		if (Id == Gathered)
		{
			return true;
		}
	}
	return false;
}

void ULootClaimComponent::BindTargets(UInventoryComponent* InInventory, USnareBackendSubsystem* InBackend)
{
	Inventory = InInventory;
	Backend = InBackend;
}

void ULootClaimComponent::BeginPlay()
{
	Super::BeginPlay();

	// 대기열은 잡은 사람 폰에만 쌓인다 — 남의 복제본에서 돌면 남의 계정으로 보낼 일은 없어도 헛돈다.
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwnerPawn) || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	if (!IsValid(Inventory))
	{
		Inventory = OwnerPawn->FindComponentByClass<UInventoryComponent>();
	}
	if (!IsValid(Backend) && OwnerPawn->GetGameInstance() != nullptr)
	{
		Backend = OwnerPawn->GetGameInstance()->GetSubsystem<USnareBackendSubsystem>();
	}

	if (ClaimIntervalSeconds <= 0.f || MaxItemsPerClaim <= 0)
	{
		UE_LOG(LogSnarehaven, Error, TEXT("[Loot] 보내기 주기·묶음 크기가 미정이다 (%.1f초 · %d개) - 대기열을 안 보낸다"),
			ClaimIntervalSeconds, MaxItemsPerClaim);
		return;
	}

	GetWorld()->GetTimerManager().SetTimer(ClaimTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		TryClaimNow();
	}), ClaimIntervalSeconds, true);
}

void ULootClaimComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld() != nullptr)
	{
		GetWorld()->GetTimerManager().ClearTimer(ClaimTimer);
	}
	Super::EndPlay(EndPlayReason);
}

bool ULootClaimComponent::TryClaimNow()
{
	if (bClaimInFlight || !IsValid(Inventory) || !IsValid(Backend) || MaxItemsPerClaim <= 0 || MaxDropsPerKill <= 0)
	{
		return false;
	}

	// 한 묶음 = 대기열 앞에서부터 합계 MaxItemsPerClaim 개까지 — 처치 한 마리는 MaxDropsPerKill 개로 센다(서버가 굴리기 전에
	// 그렇게 센다). 넘치는 줄은 잘라서 나머지를 다음 주기로 남긴다.
	TArray<FInventorySlot> Batch;
	int32 Total = 0;
	for (const FInventorySlot& Pending : Inventory->GetPendingGrants())
	{
		if (Total >= MaxItemsPerClaim)
		{
			break;
		}
		if (IsKillLine(Pending.ItemID))
		{
			const int32 KillsTaken = FMath::Min(Pending.Count, (MaxItemsPerClaim - Total) / MaxDropsPerKill);
			if (KillsTaken > 0)
			{
				FInventorySlot& Line = Batch.Add_GetRef(Pending);
				Line.Count = KillsTaken;
				Line.ToolUses = 0;
				Total += KillsTaken * MaxDropsPerKill;
			}
			continue;
		}
		if (!IsClaimableLoot(Pending.ItemID))
		{
			if (!WarnedItems.Contains(Pending.ItemID))
			{
				WarnedItems.Add(Pending.ItemID);
				UE_LOG(LogSnarehaven, Warning,
					TEXT("[Loot] %s x %d 은(는) ClaimLoot 허용 목록 밖이라 안 보낸다 - 대기열에 남긴다"),
					*Pending.ItemID.ToString(), Pending.Count);
			}
			continue;
		}
		FInventorySlot& Line = Batch.Add_GetRef(Pending);
		Line.Count = FMath::Min(Pending.Count, MaxItemsPerClaim - Total);
		Line.ToolUses = FMath::Min(Pending.ToolUses, Line.Count);
		const int32 Take = Line.Count;
		Total += Take;
	}
	if (Batch.IsEmpty())
	{
		return false;
	}

	TArray<TSharedPtr<FJsonValue>> Items;
	TArray<TSharedPtr<FJsonValue>> Kills;
	for (const FInventorySlot& Entry : Batch)
	{
		if (IsKillLine(Entry.ItemID))
		{
			const TSharedPtr<FJsonObject> Kill = MakeShared<FJsonObject>();
			Kill->SetStringField(TEXT("animalId"), Entry.ItemID.ToString());
			Kill->SetNumberField(TEXT("count"), Entry.Count);
			Kills.Add(MakeShared<FJsonValueObject>(Kill));
			continue;
		}
		const TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("itemId"), Entry.ItemID.ToString());
		Item->SetNumberField(TEXT("count"), Entry.Count);
		if (Entry.ToolUses > 0)
		{
			Item->SetNumberField(TEXT("toolUses"), Entry.ToolUses);    // DESIGN 13.5 전기톱·마체테
		}
		Items.Add(MakeShared<FJsonValueObject>(Item));
	}
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	// `items` 는 비어도 싣는다 — 받는 쪽(서버·검사의 가짜 서버)이 칸이 있다고 보고 읽는다. `kills` 는 처치가 있을 때만.
	Args->SetArrayField(TEXT("items"), Items);
	if (!Kills.IsEmpty())
	{
		Args->SetArrayField(TEXT("kills"), Kills);
	}

	bClaimInFlight = true;
	UE_LOG(LogSnarehaven, Log, TEXT("[Loot] ClaimLoot 요청 - 아이템 %d줄 · 처치 %d줄 · 셈 %d개"), Items.Num(), Kills.Num(), Total);

	const TWeakObjectPtr<ULootClaimComponent> WeakThis(this);
	Backend->CallServerFunction(GetClaimFunctionName(), Args,
		[WeakThis, Batch, Total](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			ULootClaimComponent* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bClaimInFlight = false;

			FString Code;
			FString Message;
			if (!SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message))
			{
				// 대기열을 그대로 둔다 — 다음 주기에 같은 묶음을 다시 보낸다.
				UE_LOG(LogSnarehaven, Log, TEXT("[Loot] ClaimLoot 거절 %s (%s) - 대기열 유지"), *Code, *Message);
				return;
			}

			// 서버가 받아 준 만큼만 뺀다. 보유 수량은 이어서 오는 스냅샷이 채운다 (CLAUDE.md 8).
			if (IsValid(Self->Inventory))
			{
				for (const FInventorySlot& Entry : Batch)
				{
					Self->Inventory->RemovePendingGrant(Entry.ItemID, Entry.Count);
				}
			}
			++Self->RefetchCount;
			if (IsValid(Self->Backend))
			{
				Self->Backend->FetchPlayerState();
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Loot] ClaimLoot 성공 - %d줄(셈 %d개)을 대기열에서 빼고 보유 상태를 다시 받는다"), Batch.Num(), Total);
			Self->AnnounceGranted(Response);
		});
	return true;
}

int32 ULootClaimComponent::GetGrantPopupMaxKinds()
{
	return GrantPopupMaxKinds;
}

FText ULootClaimComponent::FormatGrantPopup(const TArray<TPair<FText, int32>>& Lines)
{
	TArray<FText> Parts;
	int32 Hidden = 0;
	for (const TPair<FText, int32>& Line : Lines)
	{
		if (Line.Value <= 0)
		{
			continue;
		}
		if (Parts.Num() >= GrantPopupMaxKinds)
		{
			++Hidden;
			continue;
		}
		Parts.Add(FText::Format(LOCTEXT("LootGrantPopupLine", "+{0} {1}"), FText::AsNumber(Line.Value), Line.Key));
	}
	if (Parts.IsEmpty())
	{
		return FText::GetEmpty();
	}
	const FText Joined = FText::Join(LOCTEXT("LootGrantPopupSeparator", " · "), Parts);
	return Hidden > 0
		? FText::Format(LOCTEXT("LootGrantPopupMore", "{0} 외 {1}종"), Joined, FText::AsNumber(Hidden))
		: Joined;
}

void ULootClaimComponent::AnnounceGranted(const TSharedPtr<FJsonObject>& Response)
{
	// 개수는 서버가 준 그대로다 — 도구 +1·나무꾼 모자 ×2 는 서버만 안다(DESIGN 13.5). 보낸 묶음으로 셈하지 않는다.
	LastGrantPopup = FText::GetEmpty();
	const TArray<TSharedPtr<FJsonValue>>* Granted = nullptr;
	if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("granted"), Granted) || Granted == nullptr)
	{
		return;
	}
	TArray<TPair<FText, int32>> Lines;
	for (const TSharedPtr<FJsonValue>& Value : *Granted)
	{
		const TSharedPtr<FJsonObject> Item = Value.IsValid() ? Value->AsObject() : nullptr;
		FString ItemID;
		int32 Count = 0;
		if (!Item.IsValid() || !Item->TryGetStringField(TEXT("itemId"), ItemID) || !Item->TryGetNumberField(TEXT("count"), Count))
		{
			continue;
		}
		// 이름을 모르면 ID 그대로 — 그럴싸한 이름을 지어내지 않는다 (Verification.md 7).
		const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, FName(*ItemID));
		Lines.Emplace(Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName : FText::FromString(ItemID), Count);
	}
	LastGrantPopup = FormatGrantPopup(Lines);
	if (LastGrantPopup.IsEmpty())
	{
		return;
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[Loot] 획득 팝업 '%s'"), *LastGrantPopup.ToString());
	OnGrantPopup.Broadcast(LastGrantPopup);
}

#undef LOCTEXT_NAMESPACE
