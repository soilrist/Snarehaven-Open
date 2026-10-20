#include "Ranch/RanchSyncComponent.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "Building/SnareBuildRules.h"
#include "Crafting/SnareCraftRules.h"
#include "Data/BaseRuleRow.h"
#include "Data/ItemRowLookup.h"
#include "Data/RanchFailureRow.h"
#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "GameFramework/Pawn.h"
#include "Inventory/BackpackComponent.h"
#include "Ranch/RanchDemoFill.h"
#include "Ranch/SnareLocalNotify.h"
#include "SnarehavenCharacter.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SnareSound.h"
#include "World/WaterTravelComponent.h"

#define LOCTEXT_NAMESPACE "Snarehaven"

URanchSyncComponent::URanchSyncComponent()
{
	// 요청이 쌓일 때·스냅샷이 올 때만 일한다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryComponentTick.bCanEverTick = false;

	// 실패 문구는 데이터다 (DataDriven.md). 표가 없으면 서버 사유 문장을 그대로 쓴다.
	static ConstructorHelpers::FObjectFinder<UDataTable> FailureTableFinder(GetFailureTablePath());
	if (FailureTableFinder.Succeeded())
	{
		FailureMessageTable = FailureTableFinder.Object;
	}
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableFinder(SnareItemRows::ItemTablePath());
	if (ItemTableFinder.Succeeded())
	{
		ItemTable = ItemTableFinder.Object;
	}
}

bool URanchSyncComponent::IsHarvestFunction(const FString& FunctionName)
{
	return FunctionName == TEXT("HarvestRanch") || FunctionName == TEXT("HarvestCrop") || FunctionName == TEXT("HarvestFishing");
}

FText URanchSyncComponent::FormatHarvestPopup(const FText& ItemName, int32 Count, int32 Coins)
{
	if (Count <= 0)
	{
		return FText::GetEmpty();
	}
	if (Coins <= 0)
	{
		return FText::Format(LOCTEXT("HarvestPopupItem", "+{0} {1}"), FText::AsNumber(Count), ItemName);
	}
	return FText::Format(LOCTEXT("HarvestPopupItemCoins", "+{0} {1} / +{2} 코인"),
		FText::AsNumber(Count), ItemName, FText::AsNumber(Coins));
}

FText URanchSyncComponent::FormatHarvestCoinLine(int32 Coins, bool bCoinsCapped)
{
	// 하루 상한(서버 `HARVEST_DAILY_COINS`)에 닿으면 코인이 말없이 빠지지 않게 까닭을 적는다 — 남은 몫은 셈하지 않는다 (CLAUDE.md 8).
	if (bCoinsCapped)
	{
		return Coins > 0
			? FText::Format(LOCTEXT("HarvestPopupCoinLineCappedPart", "+{0} 코인 · 오늘 수확 코인 끝"), FText::AsNumber(Coins))
			: LOCTEXT("HarvestPopupCoinLineCapped", "오늘 수확 코인은 다 받았어요");
	}
	return Coins > 0
		? FText::Format(LOCTEXT("HarvestPopupCoinLine", "+{0} 코인"), FText::AsNumber(Coins))
		: FText::GetEmpty();
}

FName URanchSyncComponent::GetHarvestSoundCue()
{
	return TEXT("Coins");
}

FString URanchSyncComponent::GetFunctionName(ERanchStationKind Station, ERanchRequestKind Kind, ERanchSlotKind SlotKind)
{
	if (Kind == ERanchRequestKind::PlantCrop)
	{
		return TEXT("PlantCrop");
	}
	if (Kind == ERanchRequestKind::RemovePet)
	{
		return TEXT("RemovePet");
	}
	if (Kind == ERanchRequestKind::SetCosmetic)
	{
		return TEXT("SetPetCosmetic");
	}
	if (Kind == ERanchRequestKind::Harvest && SlotKind == ERanchSlotKind::Crop)
	{
		return TEXT("HarvestCrop");
	}
	if (Station == ERanchStationKind::Fishing)
	{
		return Kind == ERanchRequestKind::RegisterPet ? TEXT("RegisterFishingSpot") : TEXT("HarvestFishing");
	}
	return Kind == ERanchRequestKind::RegisterPet ? TEXT("RegisterPet") : TEXT("HarvestRanch");
}

FString URanchSyncComponent::GetBaseStateFunctionName()
{
	return TEXT("GetBaseState");
}

FString URanchSyncComponent::GetFeedUpkeepFunctionName()
{
	return TEXT("FeedRanchUpkeep");
}

FString URanchSyncComponent::GetExpandSlotFunctionName()
{
	return TEXT("ExpandRanchSlot");
}

void URanchSyncComponent::BindTargets(URanchComponent* InRanch, URanchComponent* InFishing,
	USnareBackendSubsystem* InBackend, USnareBaseSubsystem* InBase, UWaterTravelComponent* InWaterTravel,
	UBackpackComponent* InBackpack)
{
	Ranch = InRanch;
	Fishing = InFishing;
	Backend = InBackend;
	Base = InBase;
	WaterTravel = InWaterTravel;
	Backpack = InBackpack;

	for (URanchComponent* const Station : { Ranch.Get(), Fishing.Get() })
	{
		if (IsValid(Station))
		{
			Station->OnRanchRequestQueued.AddUniqueDynamic(this, &URanchSyncComponent::HandleRequestQueued);
		}
	}
	if (IsValid(Backend))
	{
		Backend->OnPlayerSnapshotChanged.AddUniqueDynamic(this, &URanchSyncComponent::HandleSnapshotChanged);
	}
}

void URanchSyncComponent::BeginPlay()
{
	Super::BeginPlay();

	// 목장은 개인 소유 — 남의 복제본에서 돌면 내 계정으로 남의 칸을 덮는다.
	const APawn* const OwnerPawn = Cast<APawn>(GetOwner());
	if (!IsValid(OwnerPawn) || !OwnerPawn->IsLocallyControlled())
	{
		return;
	}

	UGameInstance* const GameInstance = OwnerPawn->GetGameInstance();
	BindTargets(URanchComponent::FindStation(OwnerPawn, ERanchStationKind::Ranch),
		URanchComponent::FindStation(OwnerPawn, ERanchStationKind::Fishing),
		GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr,
		GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBaseSubsystem>() : nullptr,
		OwnerPawn->FindComponentByClass<UWaterTravelComponent>(),
		OwnerPawn->FindComponentByClass<UBackpackComponent>());

	// 맵을 옮겨 폰이 새로 섰으면 스냅샷은 이미 와 있다 — 알림을 기다리지 않고 바로 되살린다.
	HandleSnapshotChanged();

#if !UE_BUILD_SHIPPING
	// 3층 캡처 `-SnareRanchDemo` (스토어 스크린샷 ① 목장) — 칸 채우기는 `RanchDemoFill.cpp` (이 파일은 시간 코드 0줄 검사 대상).
	if (SnareCheckSwitch::Param(TEXT("SnareRanchDemo")) && IsValid(Ranch))
	{
		SnareRanchDemoFill::Schedule(Ranch);
	}
#endif

	// 가만히 서 있어도 수확 준비 표시가 켜지게 (M19-06). 존 판정은 매번 한다 — 게스트는 게임 상태가 늦게 올 수 있다.
	StartBaseStatePolling(FTSTicker::GetCoreTicker());
}

#if !UE_BUILD_SHIPPING
void URanchSyncComponent::DebugFeedAcceptedReply(const FString& FunctionName)
{
	HandleServerReply(FunctionName, true, MakeShared<FJsonObject>(), FString());
}
#endif

void URanchSyncComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopBaseStatePolling();
	// 백엔드는 맵을 건너 산다 — 사라진 폰 컴포넌트를 부르지 않게 끊는다.
	if (IsValid(Backend))
	{
		Backend->OnPlayerSnapshotChanged.RemoveAll(this);
	}
	Super::EndPlay(EndPlayReason);
}

float URanchSyncComponent::GetBaseStatePollSeconds()
{
	return GetDefault<URanchSyncComponent>()->BaseStatePollSeconds;
}

void URanchSyncComponent::StartBaseStatePolling(FTSTicker& Ticker)
{
	StopBaseStatePolling();
	const float PollSeconds = GetBaseStatePollSeconds();
	if (PollSeconds <= 0.f)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Ranch] GetBaseState 주기 조회 꺼짐 (BaseStatePollSeconds=%.1f)"), PollSeconds);
		return;
	}
	const TWeakObjectPtr<URanchSyncComponent> WeakThis(this);
	BaseStatePollHandle = Ticker.AddTicker(TEXT("RanchBaseStatePoll"), PollSeconds,
		[WeakThis](float)
		{
			URanchSyncComponent* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return false;
			}
			Self->PollBaseState();
			return true;
		});
}

void URanchSyncComponent::StopBaseStatePolling()
{
	FTSTicker::RemoveTicker(BaseStatePollHandle);
	BaseStatePollHandle.Reset();
}

void URanchSyncComponent::PollBaseState()
{
	// 사냥존·로그인 전(로컬 캐시 표시 중)엔 묻지 않는다. 묻는 중이면 `RequestBaseState` 가 알아서 건너뛴다.
	if (IsValid(Backend) && Backend->IsSnapshotFromBackend()
		&& USnareBaseBuildComponent::GetTentZone(GetWorld()) == SnareBaseZones::Ranch)
	{
		RequestBaseState();
	}
}

void URanchSyncComponent::HandleRequestQueued(int32 SlotIndex)
{
	TrySendNext();
}

void URanchSyncComponent::HandleSnapshotChanged()
{
	if (bRestoreRequested || !IsValid(Backend) || !Backend->IsSnapshotFromBackend())
	{
		return;
	}
	bRestoreRequested = RequestBaseState();
}

bool URanchSyncComponent::TrySendNext()
{
	if (bRequestInFlight || !IsValid(Backend))
	{
		return false;
	}
	return SendFrom(Ranch) || SendFrom(Fishing);
}

bool URanchSyncComponent::SendFrom(URanchComponent* Station)
{
	if (!IsValid(Station) || Station->GetPendingRequests().IsEmpty())
	{
		return false;
	}

	const FRanchRequest Request = Station->GetPendingRequests()[0];
	const ERanchStationKind StationKind = Station->GetStationKind();
	const FRanchSlotState* const Slot = Station->GetSlot(Request.SlotIndex);
	const FString FunctionName = GetFunctionName(StationKind, Request.Kind,
		Slot != nullptr ? Slot->Kind : ERanchSlotKind::AnimalProduct);

	// 인자 이름은 서버 핸들러 그대로 (BACKEND.md 표 · cloudscript_main.js).
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetNumberField(TEXT("slot"), Request.SlotIndex);
	if (Request.Kind == ERanchRequestKind::RegisterPet)
	{
		Args->SetStringField(StationKind == ERanchStationKind::Fishing ? TEXT("fishItemId") : TEXT("animalId"),
			Request.AnimalID.ToString());
		// M21-07c2 변종 펫이면 서버가 같은 종의 `item.pet_<종>_variant` 를 뺀다 (`RegisterPet {variant}`).
		if (Request.bVariant && StationKind != ERanchStationKind::Fishing)
		{
			Args->SetBoolField(TEXT("variant"), true);
		}
	}
	else if (Request.Kind == ERanchRequestKind::PlantCrop)
	{
		Args->SetStringField(TEXT("seedItemId"), Request.SeedItemID.ToString());
	}
	else if (Request.Kind == ERanchRequestKind::SetCosmetic)
	{
		// 빈 문자열 = 벗기기 (서버 `SetPetCosmetic` 규약). NAME_None.ToString() 은 "None" 이라 따로 거른다.
		Args->SetStringField(TEXT("itemId"), Request.CosmeticItemID.IsNone() ? FString() : Request.CosmeticItemID.ToString());
	}

	bRequestInFlight = true;
	UE_LOG(LogSnarehaven, Log, TEXT("[Ranch] %s 요청 - %d번 칸"), *FunctionName, Request.SlotIndex);

	const TWeakObjectPtr<URanchSyncComponent> WeakThis(this);
	const TWeakObjectPtr<URanchComponent> WeakStation(Station);
	const int32 SlotIndex = Request.SlotIndex;
	Backend->CallServerFunction(FunctionName, Args,
		[WeakThis, WeakStation, SlotIndex, FunctionName](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			URanchSyncComponent* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bRequestInFlight = false;

			// 누른 사람이 다시 누를 수 있게 이 한 건은 성패와 상관없이 뺀다 — 네트워크가 끊겼을 때 저절로 되풀이하면
			// 같은 칸을 여러 번 수확하려 드는 꼴이 된다(서버가 막아도 안내가 쌓인다).
			if (URanchComponent* const Station = WeakStation.Get())
			{
				Station->RemovePendingRequest(SlotIndex);
			}

			Self->HandleServerReply(FunctionName, bOk, Response, Error);
			Self->TrySendNext();
		});
	return true;
}

void URanchSyncComponent::HandleServerReply(const FString& FunctionName, bool bOk,
	const TSharedPtr<FJsonObject>& Response, const FString& Error)
{
	FString Code;
	FString Message;
	const bool bAccepted = SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message);
	LastHarvestPopup = FText::GetEmpty();
	LastHarvestTitle = FText::GetEmpty();
	LastHarvestCoinLine = FText::GetEmpty();
	LastHarvestIcon.Reset();
	LastPlayedCue = NAME_None;
	if (!bAccepted)
	{
		// 칸·유지는 그대로 둔다 — 서버가 안 받았으면 바뀐 것이 없다. 글만 남긴다.
		LastMessage = DescribeRanchFailure(FailureMessageTable, FunctionName, bOk, Response, Error);
		// 라이브에 아직 안 올린 함수 — PlayFab 이 `CloudScriptNotFound` 로 답한다(SnareBackendSubsystem 이 Error 에 싣는다).
		if (!bOk && Error.Contains(TEXT("CloudScriptNotFound")))
		{
			UnavailableFunctions.Add(FunctionName);
			LastMessage = LOCTEXT("RanchServerNotReady", "서버 준비 중");
		}
		UE_LOG(LogSnarehaven, Log, TEXT("[Ranch] %s 거절 %s (%s) - 칸 유지"), *FunctionName, *Code, *Message);
	}
	else
	{
		// 칸·가방·유지·명성은 다시 받은 값으로만 바뀐다 (CLAUDE.md 8).
		LastMessage = FText::GetEmpty();
		++RefetchCount;
		if (IsValid(Backend))
		{
			Backend->FetchPlayerState();
		}
		if (!RequestBaseState())
		{
			bBaseStateAgain = true;
		}
		UE_LOG(LogSnarehaven, Log, TEXT("[Ranch] %s 성공 - 보유·거점 상태를 다시 받는다"), *FunctionName);
		if (IsHarvestFunction(FunctionName))
		{
			CelebrateHarvest(Response);
		}
		else if (FunctionName == TEXT("PlantCrop"))
		{
			// 씨앗 심기도 설치와 같은 「쿵」 한 번, 서버가 받은 뒤에만 (M28-03). 같은 방 사람은 내 자리에서 듣는다.
			LastPlayedCue = SnareBuildRules::PlaceConfirmCue();
			if (ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner()))
			{
				Body->PlayActionSound(LastPlayedCue, Body->GetActorLocation(), 1.f);
			}
		}
		else
		{
			// 넣기·빼기·낚시터 등 목장 소리 한 번, 서버가 받은 뒤에만 (DESIGN 3.2 「목장 소리」, M50-03). 내 기기만.
			const SnareSound::FChangeCue Cue = SnareSound::RanchCue(FunctionName, bAccepted, Code);
			if (!Cue.Cue.IsNone())
			{
				LastPlayedCue = Cue.Cue;
				++RanchCueCount;
				USnareSoundSubsystem::Play2D(this, Cue.Cue, Cue.Volume);
				UE_LOG(LogSnarehaven, Log, TEXT("[Sound] 목장 %s → %s"), *FunctionName, *Cue.Cue.ToString());
			}
		}
	}
	OnRanchRequestAnswered.Broadcast(FunctionName, bAccepted, Code, LastMessage);
}

void URanchSyncComponent::CelebrateHarvest(const TSharedPtr<FJsonObject>& Response)
{
	// 서버가 준 수·아이템·코인 그대로 — 여기서 지급을 셈하지 않는다 (CLAUDE.md 8). 가방 숫자는 다시 받은 스냅샷이 바꾼다.
	int32 Count = 0;
	int32 Coins = 0;
	bool bCoinsCapped = false;
	FString ItemID;
	if (Response.IsValid())
	{
		Response->TryGetNumberField(TEXT("harvested"), Count);
		Response->TryGetNumberField(TEXT("coins"), Coins);
		Response->TryGetBoolField(TEXT("coinsCapped"), bCoinsCapped);
		Response->TryGetStringField(TEXT("itemId"), ItemID);
	}
	if (Count <= 0)
	{
		return;
	}
	// 이름을 모르면 ID 그대로 — 그럴싸한 이름을 지어내지 않는다 (Verification.md 7).
	const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, FName(*ItemID));
	const FText ItemName = Row != nullptr && !Row->DisplayName.IsEmpty() ? Row->DisplayName : FText::FromString(ItemID);
	LastHarvestPopup = FormatHarvestPopup(ItemName, Count, Coins);
	LastHarvestTitle = FormatHarvestPopup(ItemName, Count, 0);
	LastHarvestCoinLine = FormatHarvestCoinLine(Coins, bCoinsCapped);
	LastHarvestIcon = Row != nullptr ? Row->IconRef : TSoftObjectPtr<UTexture2D>();
	LastPlayedCue = GetHarvestSoundCue();
	USnareSoundSubsystem::Play2D(this, LastPlayedCue, 0.8f);
	UE_LOG(LogSnarehaven, Log, TEXT("[Harvest] 팝업 '%s' · 소리 %s"), *LastHarvestPopup.ToString(), *LastPlayedCue.ToString());
}

bool URanchSyncComponent::RequestFeedUpkeep(int32 Count)
{
	if (Count <= 0)
	{
		return false;
	}
	// 가진 나무 전부를 보낸다 — 여유보다 많으면 서버가 `min(count, 여유)` 로 자른다 (DESIGN 3.2 🟡 나무 넣기 개수).
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetNumberField(TEXT("count"), Count);
	return CallUpkeepAction(GetFeedUpkeepFunctionName(), Args);
}

bool URanchSyncComponent::RequestExpandSlot()
{
	return CallUpkeepAction(GetExpandSlotFunctionName(), MakeShared<FJsonObject>());
}

FString URanchSyncComponent::GetSetRanchDecoFunctionName()
{
	return TEXT("SetRanchDeco");
}

bool URanchSyncComponent::RequestSetRanchDeco(FName ItemID, FName Kind)
{
	if (ItemID.IsNone() && Kind.IsNone())
	{
		return false;
	}
	// 빈 `itemId` + `kind` = 해제 (서버 `SetRanchDeco` 규약). NAME_None.ToString() 은 "None" 이라 따로 거른다.
	const TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("itemId"), ItemID.IsNone() ? FString() : ItemID.ToString());
	if (!Kind.IsNone())
	{
		Args->SetStringField(TEXT("kind"), Kind.ToString());
	}
	return CallUpkeepAction(GetSetRanchDecoFunctionName(), Args);
}

bool URanchSyncComponent::CallUpkeepAction(const FString& FunctionName, const TSharedPtr<FJsonObject>& Args)
{
	if (bUpkeepActionInFlight || !IsValid(Backend))
	{
		return false;
	}
	bUpkeepActionInFlight = true;
	UE_LOG(LogSnarehaven, Log, TEXT("[Ranch] %s 요청"), *FunctionName);
	// 손으로 넣는·놓는 몸짓 — 겉모습뿐, 결과는 서버 답 뒤 (INBOX 2026-10-15 감사 (e)).
	if (ASnarehavenCharacter* const Body = Cast<ASnarehavenCharacter>(GetOwner()))
	{
		Body->PlayItemAction(EItemAction::Gather);
	}

	const TWeakObjectPtr<URanchSyncComponent> WeakThis(this);
	Backend->CallServerFunction(FunctionName, Args,
		[WeakThis, FunctionName](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			URanchSyncComponent* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bUpkeepActionInFlight = false;
			Self->HandleServerReply(FunctionName, bOk, Response, Error);
		});
	return true;
}

const FBaseRuleRow* URanchSyncComponent::GetRanchRules() const
{
	const FBaseRuleRow* const Rules = IsValid(Base) ? Base->GetRules() : nullptr;
	return Rules != nullptr && Rules->HasRanchRules() ? Rules : nullptr;
}

int32 URanchSyncComponent::GetUpkeepItemCount() const
{
	const FBaseRuleRow* const Rules = GetRanchRules();
	if (Rules == nullptr || !IsValid(Backend) || !Backend->IsSnapshotFromBackend())
	{
		return -1;
	}
	return SnareCraftRules::CountItem(Backend->GetPlayerSnapshot().InventorySlots, Rules->UpkeepItemID);
}

bool URanchSyncComponent::GetNextExpandCost(int32& OutFame, int32& OutCoins) const
{
	const FBaseRuleRow* const Rules = GetRanchRules();
	if (Rules == nullptr || !IsValid(Ranch) || !Ranch->GetUpkeep().bReceived || IsSlotCapAtMax())
	{
		return false;
	}
	const int32 Step = Ranch->GetUpkeep().SlotCap - Ranch->GetBaseSlotCount() + 1;
	if (Step <= 0)
	{
		return false;
	}
	OutFame = Rules->RanchExpandFameStep * Step;
	OutCoins = Rules->RanchExpandCoinStep * Step;
	return true;
}

bool URanchSyncComponent::IsSlotCapAtMax() const
{
	const FBaseRuleRow* const Rules = GetRanchRules();
	return Rules != nullptr && IsValid(Ranch) && Ranch->GetUpkeep().bReceived
		&& Ranch->GetUpkeep().SlotCap >= Rules->RanchSlotMax;
}

namespace
{
	/** `<함수>_<code>` 행 → `<code>` 행. 문구가 빈 행은 없는 것으로 본다. */
	const FRanchFailureRow* FindFailureRow(const UDataTable* Table, const FString& FunctionName, const FString& Code,
		bool bUseCodeRows)
	{
		if (!IsValid(Table) || Code.IsEmpty())
		{
			return nullptr;
		}
		const FRanchFailureRow* Row = nullptr;
		if (!FunctionName.IsEmpty())
		{
			Row = Table->FindRow<FRanchFailureRow>(FName(*(FunctionName + TEXT("_") + Code)), TEXT("RanchFailure"), false);
		}
		if (Row == nullptr && (bUseCodeRows || Code == TEXT("network")))
		{
			Row = Table->FindRow<FRanchFailureRow>(FName(*Code), TEXT("RanchFailure"), false);
		}
		return Row != nullptr && !Row->Message.IsEmpty() ? Row : nullptr;
	}

	/** 표 문구 + 모자란 수(`need`/`have`)가 오면 꼬리. */
	FText FormatFailureRow(const FRanchFailureRow& Row, const TSharedPtr<FJsonObject>& Response)
	{
		double Need = 0.0;
		double Have = 0.0;
		if (Response.IsValid() && Response->TryGetNumberField(TEXT("need"), Need) && Response->TryGetNumberField(TEXT("have"), Have))
		{
			return FText::Format(LOCTEXT("ServerFailNeedHave", "{0} ({1} 필요, {2} 있음)"), Row.Message,
				FText::AsNumber(static_cast<int32>(Need)), FText::AsNumber(static_cast<int32>(Have)));
		}
		return Row.Message;
	}
}

FText URanchSyncComponent::DescribeRanchFailure(const UDataTable* Table, const FString& FunctionName, bool bOk,
	const TSharedPtr<FJsonObject>& Response, const FString& Error)
{
	FString Code;
	FString Message;
	if (SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message))
	{
		return FText::GetEmpty();
	}
	const FRanchFailureRow* const Row = FindFailureRow(Table, FunctionName, Code, true);
	return Row != nullptr ? FormatFailureRow(*Row, Response) : SnareServerReply::DescribeFailure(bOk, Response, Error);
}

FText URanchSyncComponent::DescribeFailureCode(const UDataTable* Table, const FString& FunctionName, const FString& Code,
	const FString& Message, const TSharedPtr<FJsonObject>& Response, bool bUseCodeRows)
{
	const FRanchFailureRow* const Row = FindFailureRow(Table, FunctionName, Code, bUseCodeRows);
	return Row != nullptr ? FormatFailureRow(*Row, Response) : FText::FromString(Message.IsEmpty() ? Code : Message);
}

const TCHAR* URanchSyncComponent::GetFailureTablePath()
{
	return TEXT("/Game/Game/Data/Ranch/DT_RanchFailureTable.DT_RanchFailureTable");
}

bool URanchSyncComponent::RequestBaseState()
{
	if (bBaseStateInFlight || !IsValid(Backend))
	{
		return false;
	}
	bBaseStateInFlight = true;

	const TWeakObjectPtr<URanchSyncComponent> WeakThis(this);
	Backend->CallServerFunction(GetBaseStateFunctionName(), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			URanchSyncComponent* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bBaseStateInFlight = false;

			FString Code;
			FString Message;
			if (!SnareServerReply::IsAccepted(bOk, Response, Error, Code, Message))
			{
				UE_LOG(LogSnarehaven, Warning, TEXT("[Ranch] GetBaseState 실패 %s (%s) - 거점 칸을 그대로 둔다"), *Code, *Message);
				// 목장 화면이 이유와 「다시 시도」 를 보이게 알린다. 성공은 캐시 갱신으로 충분하다.
				Self->OnRanchRequestAnswered.Broadcast(GetBaseStateFunctionName(), false, Code,
					DescribeRanchFailure(Self->FailureMessageTable, GetBaseStateFunctionName(), bOk, Response, Error));
			}
			else
			{
				Self->ApplyBaseState(Response);
			}

			// 이 조회 도중 다른 요청이 성공했다 — 방금 값은 그 전 값일 수 있으니 한 번 더 묻는다.
			if (Self->bBaseStateAgain)
			{
				Self->bBaseStateAgain = false;
				Self->RequestBaseState();
			}
		});
	return true;
}

void URanchSyncComponent::ApplyBaseState(const TSharedPtr<FJsonObject>& Response)
{
	// M12-05: 목장 유지·명성. 서버가 계산한 값을 그대로 옮긴다 — 기한까지 세는 일은 여기 없다.
	const TSharedPtr<FJsonObject>* UpkeepObject = nullptr;
	if (IsValid(Ranch) && Response->TryGetObjectField(TEXT("ranchUpkeep"), UpkeepObject) && UpkeepObject != nullptr)
	{
		FRanchUpkeepState Upkeep;
		if (!(*UpkeepObject)->TryGetNumberField(TEXT("leftHours"), Upkeep.LeftHours))
		{
			Upkeep.LeftHours = -1; // null = 유지 시작 전
		}
		(*UpkeepObject)->TryGetBoolField(TEXT("locked"), Upkeep.bLocked);
		(*UpkeepObject)->TryGetNumberField(TEXT("fame"), Upkeep.Fame);
		(*UpkeepObject)->TryGetNumberField(TEXT("slotCap"), Upkeep.SlotCap);
		Ranch->ApplyBackendUpkeep(Upkeep);
	}

	ApplyStation(Ranch, Response, TEXT("ranch"));
	ApplyStation(Fishing, Response, TEXT("fishing"));

	// M35-05c 목장 데코 — 서버가 「배낭에 아직 있는 것만」 보낸다. 겉모습은 목장 칸 듣는 쪽(`URanchInteractComponent`)이 입힌다.
	TMap<FName, FName> Deco;
	if (IsValid(Ranch) && ParseRanchDeco(Response, Deco))
	{
		Ranch->ApplyBackendDeco(Deco);
	}

	// M21-04b: 앱 밖 알림은 목장 칸·유지를 새로 받을 때마다 지우고 다시 건다 (DESIGN 3.2 「앱 밖 알림」).
	if (IsValid(Ranch) && Ranch->GetUpkeep().bReceived)
	{
		SnareLocalNotify::RescheduleFromRanch(Ranch->GetSlots(), Ranch->GetUpkeep());
	}

	const TArray<TSharedPtr<FJsonValue>>* TentValues = nullptr;
	if (IsValid(Base) && Response->TryGetArrayField(TEXT("tents"), TentValues) && TentValues != nullptr)
	{
		// 발전기 연료 끝 시각을 서버 시계로 재려면 시각이 텐트보다 먼저 (M108-07c — 가구를 세우며 남은 초를 옮긴다).
		FString NowUtc;
		Response->TryGetStringField(TEXT("nowUtc"), NowUtc);
		Base->ApplyBackendNow(NowUtc);
		Base->ApplyBackendTents(*TentValues);
	}

	// M32-02c 배 보관함 속 — 필드가 없는 옛 서버 답이면 사본 그대로.
	const TSharedPtr<FJsonObject>* BoatStorage = nullptr;
	if (IsValid(Base) && Response->TryGetObjectField(TEXT("boatStorage"), BoatStorage) && BoatStorage != nullptr)
	{
		Base->ApplyBackendBoatStorage(*BoatStorage);
	}

	const TSharedPtr<FJsonObject>* Boat = nullptr;
	bool bLaunched = false;
	if (IsValid(WaterTravel) && Response->TryGetObjectField(TEXT("boat"), Boat) && Boat != nullptr)
	{
		(*Boat)->TryGetBoolField(TEXT("launched"), bLaunched);
		WaterTravel->ApplyBackendBoat(bLaunched);
	}

	// #0199 재접속 모자 복원 — 필드가 없는 옛 서버 답이면 건드리지 않는다. "" 는 안 쓴 것.
	// 3층 검사 `-SnareEquip=` 이 머리에 건 것은 두고 본다 — 검사 계정은 모자를 안 써서 "" 가 기본 모자로 덮는다.
	FString HatID;
	FString EquipSwitch;
	if (IsValid(Backpack) && Response->TryGetStringField(TEXT("hat"), HatID)
		&& !SnareCheckSwitch::Value(TEXT("SnareEquip="), EquipSwitch))
	{
		Backpack->ApplyBackendHat(HatID.IsEmpty() ? NAME_None : FName(*HatID));
	}

	// 스킨 조각 e2 재접속 꾸미기 복원 — `outfit` 이 없는 옛 서버 답이면 건드리지 않는다.
	const TSharedPtr<FJsonObject>* Outfit = nullptr;
	if (IsValid(Backpack) && Response->TryGetObjectField(TEXT("outfit"), Outfit) && Outfit != nullptr && Outfit->IsValid())
	{
		Backpack->ApplyBackendOutfit(**Outfit);
	}
}

bool URanchSyncComponent::ParseRanchDeco(const TSharedPtr<FJsonObject>& Response, TMap<FName, FName>& OutDeco)
{
	OutDeco.Reset();
	const TSharedPtr<FJsonObject>* DecoObject = nullptr;
	if (!Response.IsValid() || !Response->TryGetObjectField(TEXT("ranchDeco"), DecoObject) || DecoObject == nullptr
		|| !DecoObject->IsValid())
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*DecoObject)->Values)
	{
		FString ItemID;
		if (Entry.Value.IsValid() && Entry.Value->TryGetString(ItemID) && !ItemID.IsEmpty())
		{
			OutDeco.Add(FName(*Entry.Key), FName(*ItemID));
		}
	}
	return true;
}

void URanchSyncComponent::ApplyStation(URanchComponent* Station, const TSharedPtr<FJsonObject>& Response, const TCHAR* Field)
{
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!IsValid(Station) || !Response->TryGetArrayField(Field, Entries) || Entries == nullptr)
	{
		return;
	}

	// 서버 배열의 자리 = 칸 번호, 빈 칸은 null. 칸 수가 다르면 `ApplyBackendSnapshot` 이 통째로 거절한다.
	const bool bFishing = Station->GetStationKind() == ERanchStationKind::Fishing;
	TArray<FRanchSlotState> Slots;
	for (const TSharedPtr<FJsonValue>& Entry : *Entries)
	{
		FRanchSlotState& Slot = Slots.AddDefaulted_GetRef();
		Slot.Kind = bFishing ? ERanchSlotKind::Fishing : ERanchSlotKind::AnimalProduct;
		const TSharedPtr<FJsonObject> Object = Entry.IsValid() && Entry->Type == EJson::Object ? Entry->AsObject() : nullptr;
		if (!Object.IsValid())
		{
			continue;
		}
		FString Id;
		// ⑩ 작물 항목은 같은 `ranch` 배열에 `cropItemId` 로 온다 (`PlantCrop`) — 있으면 작물 칸이다.
		if (!bFishing && Object->TryGetStringField(TEXT("cropItemId"), Id) && !Id.IsEmpty())
		{
			Slot.Kind = ERanchSlotKind::Crop;
			Slot.CropItemID = FName(*Id);
		}
		else
		{
			Object->TryGetStringField(bFishing ? TEXT("fishItemId") : TEXT("animalId"), Id);
			(bFishing ? Slot.FishItemID : Slot.AnimalID) = FName(*Id);
		}
		FString Since;
		if (Object->TryGetStringField(TEXT("occupiedSinceUtc"), Since))
		{
			FDateTime::ParseIso8601(*Since, Slot.OccupiedSinceUtc);
		}
		int32 Stock = 0;
		Object->TryGetNumberField(TEXT("stock"), Stock);
		Slot.Stock = Stock;
		// M21-07c2 변종 칸 — 서버 칸에 `variant: true` 가 있을 때만 (없으면 일반 칸).
		bool bVariant = false;
		Object->TryGetBoolField(TEXT("variant"), bVariant);
		Slot.bVariant = bVariant && Slot.Kind == ERanchSlotKind::AnimalProduct;
		// 10.9 동물 꾸미기 — 서버 칸 `cosmetic` 이 있을 때만 (없으면 맨몸).
		FString Cosmetic;
		if (Slot.Kind == ERanchSlotKind::AnimalProduct && Object->TryGetStringField(TEXT("cosmetic"), Cosmetic))
		{
			Slot.CosmeticItemID = FName(*Cosmetic);
		}
	}
	Station->ApplyBackendSnapshot(Slots);
}

#undef LOCTEXT_NAMESPACE
