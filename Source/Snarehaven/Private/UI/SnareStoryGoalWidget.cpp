#include "UI/SnareStoryGoalWidget.h"

#include "Snarehaven.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Styling/SlateBrush.h"
#include "Components/TextBlock.h"
#include "Data/ItemRow.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "TimerManager.h"
#include "SnareCheckSwitch.h"
#include "UI/SnareUiStyle.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 자리 배치 (🟡 화면 배치 — 게임 규칙 숫자가 아니다). 왼쪽 위 모서리에서 조금 안쪽.
	constexpr float CardLeft = 16.f;
	constexpr float CardTop = 16.f;
	constexpr float FontSize = 20.f;
	constexpr float PipSize = 16.f;
	const FLinearColor PipLit(1.f, 0.82f, 0.28f, 1.f);	// 켜진 등대 불빛
	const FLinearColor PipDim(0.35f, 0.30f, 0.22f, 0.45f);	// 꺼진 고리
}

USnareStoryGoalWidget::USnareStoryGoalWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UDataTable> ItemTableAsset(
		TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (ItemTableAsset.Succeeded())
	{
		ItemTable = ItemTableAsset.Object;
	}
}

FText USnareStoryGoalWidget::GetGoalText() const
{
	return bHasReply
		? SnareStory::FormatLine(View, [this](FName ItemID) { return FindItemDisplayName(ItemID); })
		: FText::GetEmpty();
}

FText USnareStoryGoalWidget::FindItemDisplayName(FName ItemID) const
{
	if (IsValid(ItemTable))
	{
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row != nullptr && Row->ItemID == ItemID && !Row->DisplayName.IsEmpty())
			{
				return Row->DisplayName;
			}
		}
	}
	return FText::FromName(ItemID);
}

void USnareStoryGoalWidget::BindToBackend(USnareBackendSubsystem* Backend)
{
	if (WatchedBackend == Backend)
	{
		return;
	}
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.RemoveDynamic(this, &USnareStoryGoalWidget::HandleBackendSnapshotChanged);
	}
	WatchedBackend = Backend;
	if (IsValid(WatchedBackend))
	{
		WatchedBackend->OnPlayerSnapshotChanged.AddDynamic(this, &USnareStoryGoalWidget::HandleBackendSnapshotChanged);
	}
	// 검사 전용 `-SnareStoryAt=N` — 오프라인엔 서버 답이 없어서, 끝낸 단계 N 인 「report」 답을 직접 만들어 줄과 고리를 그린다.
	int32 FakeStep = 0;
	if (SnareCheckSwitch::Value(TEXT("SnareStoryAt="), FakeStep))
	{
		FakeStep = FMath::Clamp(FakeStep, 0, SnareStory::TotalSteps);
		const TSharedPtr<FJsonObject> Fake = MakeShared<FJsonObject>();
		if (FakeStep >= SnareStory::TotalSteps)
		{
			// `-SnareStoryAt=25` = 25단계를 다 끝낸 계정 (등대 불 켜짐 캡처용, 현재 단계 없음).
			Fake->SetNumberField(TEXT("step"), FakeStep);
			Fake->SetNumberField(TEXT("chapter"), SnareStory::TotalSteps / SnareStory::StepsPerChapter);
			Fake->SetBoolField(TEXT("done"), true);
			ApplyStoryReply(Fake);
			return;
		}
		Fake->SetNumberField(TEXT("step"), FakeStep);
		Fake->SetNumberField(TEXT("chapter"), FakeStep / SnareStory::StepsPerChapter + 1);
		const TSharedPtr<FJsonObject> Current = MakeShared<FJsonObject>();
		Current->SetNumberField(TEXT("step"), FakeStep + 1);
		Current->SetStringField(TEXT("kind"), TEXT("report"));
		// `-SnareStoryGive=아이템id:개수,…` 를 같이 주면 건네기 단계 답으로 만든다 (재료 여러 개 줄의 길이를 보려고).
		FString GiveSpec;
		if (SnareCheckSwitch::Value(TEXT("SnareStoryGive="), GiveSpec, /*bShouldStopOnSeparator=*/false))
		{
			TArray<TSharedPtr<FJsonValue>> Needs;
			TArray<FString> Parts;
			GiveSpec.ParseIntoArray(Parts, TEXT(","));
			for (const FString& Part : Parts)
			{
				FString ItemId;
				FString Count;
				if (Part.Split(TEXT(":"), &ItemId, &Count))
				{
					const TSharedPtr<FJsonObject> Need = MakeShared<FJsonObject>();
					Need->SetStringField(TEXT("itemId"), ItemId);
					Need->SetNumberField(TEXT("count"), FCString::Atoi(*Count));
					Needs.Add(MakeShared<FJsonValueObject>(Need));
				}
			}
			Current->SetStringField(TEXT("kind"), TEXT("give"));
			Current->SetArrayField(TEXT("needs"), Needs);
		}
		Fake->SetObjectField(TEXT("current"), Current);
		// `-SnareStoryAtDelay=초` — `-SnareStandAt=` 순간이동 뒤에 단계 답이 오게 한다 (가기 단계 도착 전 캡처, 루프#1305 3층).
		float FakeDelay = 0.f;
		if (SnareCheckSwitch::Value(TEXT("SnareStoryAtDelay="), FakeDelay) && FakeDelay > 0.f && GetWorld() != nullptr)
		{
			FTimerHandle FakeTimer;
			GetWorld()->GetTimerManager().SetTimer(FakeTimer, FTimerDelegate::CreateWeakLambda(this, [this, Fake]() { ApplyStoryReply(Fake); }),
				FakeDelay, false);
			return;
		}
		ApplyStoryReply(Fake);
		return;
	}
	RequestStory();
	Refresh();
}

void USnareStoryGoalWidget::HandleBackendSnapshotChanged()
{
	// 단계·개수는 서버만 아니까 보유 상태가 바뀌면 다시 묻는다.
	if (bPending)
	{
		bRefreshQueued = true;
		return;
	}
	const double Wait = MinRefreshSeconds - (FPlatformTime::Seconds() - LastRequestSeconds);
	UWorld* const World = GetWorld();
	if (Wait <= 0.0 || World == nullptr)
	{
		RequestStory();
		return;
	}
	if (!World->GetTimerManager().IsTimerActive(RefreshTimer))
	{
		const TWeakObjectPtr<USnareStoryGoalWidget> WeakThis(this);
		World->GetTimerManager().SetTimer(RefreshTimer, FTimerDelegate::CreateLambda([WeakThis]()
		{
			if (USnareStoryGoalWidget* const Self = WeakThis.Get())
			{
				Self->RequestStory();
			}
		}), static_cast<float>(Wait), false);
	}
}

bool USnareStoryGoalWidget::RequestStory()
{
	if (bPending || !IsValid(WatchedBackend))
	{
		return false;
	}
	bPending = true;
	bRefreshQueued = false;
	LastRequestSeconds = FPlatformTime::Seconds();
	const TWeakObjectPtr<USnareStoryGoalWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("GetStory"), MakeShared<FJsonObject>(),
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			USnareStoryGoalWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bPending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				UE_LOG(LogSnarehaven, Log, TEXT("[Story] 지금 할 일 줄 — GetStory 거절: %s"), *Failure.ToString());
			}
			else
			{
				Self->ApplyStoryReply(Response);
			}
			if (Self->bRefreshQueued)
			{
				Self->HandleBackendSnapshotChanged();
			}
		});
	return true;
}

bool USnareStoryGoalWidget::RequestReport(int32 ReadStep)
{
	if (ReadStep == View.CurrentStep && SnareStory::ShouldGive(View))
	{
		GiveReadStep = ReadStep;  // 재료가 모자라 거절돼도, 나중에 모으면 다시 건넨다
	}
	return ReadStep == View.CurrentStep && (SnareStory::ShouldGive(View) ? RequestAdvance(false, /*bGive=*/true) : RequestAdvance(/*bReport=*/true));
}

bool USnareStoryGoalWidget::RequestAdvance(bool bReport, bool bGive)
{
	if (bAdvancePending || !IsValid(WatchedBackend) || !(bGive ? SnareStory::ShouldGive(View) : bReport ? SnareStory::ShouldReport(View) : SnareStory::ShouldAutoAdvance(View)))
	{
		return false;
	}
	bAdvancePending = true;
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetNumberField(TEXT("step"), View.CurrentStep);
	if (bReport)
	{
		Args->SetBoolField(TEXT("report"), true);
	}
	const TWeakObjectPtr<USnareStoryGoalWidget> WeakThis(this);
	WatchedBackend->CallServerFunction(TEXT("AdvanceStory"), Args,
		[WeakThis](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
		{
			USnareStoryGoalWidget* const Self = WeakThis.Get();
			if (Self == nullptr)
			{
				return;
			}
			Self->bAdvancePending = false;
			const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
			if (!Failure.IsEmpty())
			{
				FString Raw;
				if (Response.IsValid())
				{
					const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Raw);
					FJsonSerializer::Serialize(Response.ToSharedRef(), Writer); // 거절한 재료 id(itemId) 를 로그에서 바로 읽게
				}
				UE_LOG(LogSnarehaven, Log, TEXT("[Story] 단계 넘기기 — AdvanceStory 거절: %s · 서버 답 %s"), *Failure.ToString(), *Raw);
				return;
			}
			UE_LOG(LogSnarehaven, Log, TEXT("[Story] 단계를 넘겼다 — 서버 답으로 줄을 고친다"));
			Self->ApplyStoryReply(Response);
			Self->RequestStory(); // 코인·꾸미기 지급이 있었으면 보유 상태도 서버 답으로
		});
	return true;
}

bool USnareStoryGoalWidget::ApplyStoryReply(const TSharedPtr<FJsonObject>& Response)
{
	SnareStory::FStoryView Parsed;
	if (!SnareStory::ParseView(Response, Parsed))
	{
		return false;
	}
	View = Parsed;
	UE_LOG(LogSnarehaven, Log, TEXT("[Story] 서버 답 — %d단계 %s %d/%d"), View.CurrentStep, *View.Kind, View.Have, View.Need);
	SnareStory::SetKnownCurrentStep(View.CurrentStep);  // 이야기 동물 스포너가 읽는다
	SnareStory::SetKnownDone(View.bDone);  // 등대 액터가 읽는다
	bHasReply = true;
	Refresh();
	OnViewChanged.Broadcast(View);
	if (SnareStory::ShouldGive(View) && GiveReadStep == View.CurrentStep && View.Have >= View.Need)
	{
		RequestAdvance(false, /*bGive=*/true);  // 대사를 이미 읽었고 재료를 다 모았다 — 다시 건네 달라고 한다
	}
	RequestAdvance();  // 세기·목장 단계가 이미 채워졌으면 서버에 넘겨 달라고 한다
	if (SnareCheckSwitch::Param(TEXT("SnareStoryAutoRead")) && (SnareStory::ShouldReport(View) || SnareStory::ShouldGive(View)))
	{
		RequestReport(View.CurrentStep);  // 검사 전용: 대사를 다 읽은 것으로 쳐서 서버에 알린다 (판정은 서버가 다시 한다)
	}
	static bool bCheckBuyUsed = false;
	FString BuyItemID;
	if (!bCheckBuyUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStoryBuy="), BuyItemID) && !BuyItemID.IsEmpty())
	{
		bCheckBuyUsed = true;  // 검사 전용: 상점 줄을 누른 것처럼 서버 BuyItem 을 한 번 묻는다 (코인이 모자라면 서버가 거절)
		TSharedPtr<FJsonObject> BuyArgs = MakeShared<FJsonObject>();
		BuyArgs->SetStringField(TEXT("itemId"), BuyItemID);
		BuyArgs->SetNumberField(TEXT("count"), 1);
		WatchedBackend->CallServerFunction(TEXT("BuyItem"), BuyArgs,
			[BuyItemID](bool bOk, const TSharedPtr<FJsonObject>&, const FString& Error)
			{
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 사기 (-SnareStoryBuy=%s) — 서버 답 %s %s"), *BuyItemID, bOk ? TEXT("성공") : TEXT("거절"), *Error);
			});
	}
	static bool bCheckSellUsed = false;
	FString SellSpec;
	if (!bCheckSellUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStorySell="), SellSpec) && !SellSpec.IsEmpty())
	{
		bCheckSellUsed = true;  // 검사 전용: 가방 칸을 비우려고 서버 SellItem 을 한 번 묻는다 (`아이템id:개수`, 개수를 안 주면 1)
		FString SellItemID = SellSpec;
		FString SellCountText;
		SellSpec.Split(TEXT(":"), &SellItemID, &SellCountText);
		TSharedPtr<FJsonObject> SellArgs = MakeShared<FJsonObject>();
		SellArgs->SetStringField(TEXT("itemId"), SellItemID);
		SellArgs->SetNumberField(TEXT("count"), SellCountText.IsEmpty() ? 1 : FCString::Atoi(*SellCountText));
		WatchedBackend->CallServerFunction(TEXT("SellItem"), SellArgs,
			[SellItemID](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
			{
				const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);  // 서버가 일을 거절해도 통신은 성공일 수 있다
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 팔기 (-SnareStorySell=%s) — 서버 답 %s %s"), *SellItemID, Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString());
			});
	}
	static bool bCheckCraftUsed = false;
	FString CraftRecipeID;
	if (!bCheckCraftUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStoryCraft="), CraftRecipeID) && !CraftRecipeID.IsEmpty())
	{
		bCheckCraftUsed = true;  // 검사 전용: 제작 단추를 누른 것처럼 서버 CraftItem 을 한 번 묻는다 (재료가 모자라면 서버가 거절)
		TSharedPtr<FJsonObject> CraftArgs = MakeShared<FJsonObject>();
		CraftArgs->SetStringField(TEXT("recipeId"), CraftRecipeID);
		WatchedBackend->CallServerFunction(TEXT("CraftItem"), CraftArgs,
			[CraftRecipeID](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
			{
				const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);  // 서버가 일을 거절해도 통신은 성공일 수 있다
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 제작 (-SnareStoryCraft=%s) — 서버 답 %s %s"), *CraftRecipeID, Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString());
			});
	}
	static bool bCheckRegisterUsed = false;
	FString RegisterSpec;
	if (!bCheckRegisterUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStoryRegisterPet="), RegisterSpec, /*bShouldStopOnSeparator=*/false) && !RegisterSpec.IsEmpty())
	{
		bCheckRegisterUsed = true;  // 검사 전용: 목장 칸에 펫을 넣는 것처럼 서버 RegisterPet 을 묻는다 (펫이 없으면 서버가 거절). `동물id[:칸],…` — 칸을 안 주면 0
		TArray<FString> Parts;
		RegisterSpec.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			FString AnimalID = Part;
			FString SlotText;
			Part.Split(TEXT(":"), &AnimalID, &SlotText);
			const int32 SlotIndex = FCString::Atoi(*SlotText);
			TSharedPtr<FJsonObject> RegisterArgs = MakeShared<FJsonObject>();
			RegisterArgs->SetStringField(TEXT("animalId"), AnimalID);
			RegisterArgs->SetNumberField(TEXT("slot"), SlotIndex);
			WatchedBackend->CallServerFunction(TEXT("RegisterPet"), RegisterArgs,
				[AnimalID, SlotIndex](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
				{
					const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);  // 서버가 일을 거절해도 통신은 성공일 수 있다
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 목장에 두기 (-SnareStoryRegisterPet=%s 칸 %d) — 서버 답 %s %s"), *AnimalID, SlotIndex, Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString());
				});
		}
	}
	static bool bCheckRemoveUsed = false;
	FString RemoveSpec;
	if (!bCheckRemoveUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStoryRemovePet="), RemoveSpec, /*bShouldStopOnSeparator=*/false) && !RemoveSpec.IsEmpty())
	{
		bCheckRemoveUsed = true;  // 검사 전용: 목장 칸의 펫을 빼는 것처럼 서버 RemovePet 을 묻는다 (칸이 5/5 로 차서 안개뿔 자리가 없을 때)
		const int32 SlotIndex = FCString::Atoi(*RemoveSpec);
		TSharedPtr<FJsonObject> RemoveArgs = MakeShared<FJsonObject>();
		RemoveArgs->SetNumberField(TEXT("slot"), SlotIndex);
		WatchedBackend->CallServerFunction(TEXT("RemovePet"), RemoveArgs,
			[SlotIndex](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
			{
				const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);  // 서버가 일을 거절해도 통신은 성공일 수 있다
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 목장 펫 빼기 (-SnareStoryRemovePet= 칸 %d) — 서버 답 %s %s"), SlotIndex, Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString());
			});
	}
	static bool bCheckBagDumpUsed = false;
	if (!bCheckBagDumpUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Param(TEXT("SnareStoryDumpBag")) && WatchedBackend->GetPlayerSnapshot().InventorySlots.Num() > 0)
	{
		bCheckBagDumpUsed = true;  // 검사 전용: 서버가 준 보유 상태를 한 줄씩 적는다 — 가방이 왜 가득인지 보려고
		for (const FInventorySlot& BagSlot : WatchedBackend->GetPlayerSnapshot().InventorySlots)
		{
			UE_LOG(LogSnarehaven, Log, TEXT("검사용 가방 — %s x%d"), *BagSlot.ItemID.ToString(), BagSlot.Count);
		}
	}
	static bool bCheckFeedUsed = false;
	FString FeedSpec;
	if (!bCheckFeedUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStoryFeedUpkeep="), FeedSpec, /*bShouldStopOnSeparator=*/false) && !FeedSpec.IsEmpty())
	{
		bCheckFeedUsed = true;  // 검사 전용: 목장 유지에 나무를 넣는 것처럼 서버 FeedRanchUpkeep 을 묻는다 (유지 끊김이 풀려야 HarvestRanch 가 된다)
		TSharedPtr<FJsonObject> FeedArgs = MakeShared<FJsonObject>();
		FeedArgs->SetNumberField(TEXT("count"), FCString::Atoi(*FeedSpec));
		WatchedBackend->CallServerFunction(TEXT("FeedRanchUpkeep"), FeedArgs,
			[](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
			{
				const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);  // 서버가 일을 거절해도 통신은 성공일 수 있다
				UE_LOG(LogSnarehaven, Log, TEXT("검사용 목장 유지 (-SnareStoryFeedUpkeep=) — 서버 답 %s %s"), Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString());
			});
	}
	static bool bCheckHarvestUsed = false;
	FString HarvestSpec;
	if (!bCheckHarvestUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStoryHarvest="), HarvestSpec, /*bShouldStopOnSeparator=*/false) && !HarvestSpec.IsEmpty())
	{
		bCheckHarvestUsed = true;  // 검사 전용: 목장 칸을 거두는 것처럼 서버 HarvestRanch 를 묻는다 (쌓인 게 없으면 서버가 거절). `칸,칸,…`
		TArray<FString> Parts;
		HarvestSpec.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			const int32 SlotIndex = FCString::Atoi(*Part);
			TSharedPtr<FJsonObject> HarvestArgs = MakeShared<FJsonObject>();
			HarvestArgs->SetNumberField(TEXT("slot"), SlotIndex);
			WatchedBackend->CallServerFunction(TEXT("HarvestRanch"), HarvestArgs,
				[SlotIndex](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
				{
					const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);  // 서버가 일을 거절해도 통신은 성공일 수 있다
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 목장 수확 (-SnareStoryHarvest= 칸 %d) — 서버 답 %s %s"), SlotIndex, Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString());
				});
		}
	}
	static bool bCheckSeedUsed = false;
	FString SeedSpec;
	if (!bCheckSeedUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStorySeeds="), SeedSpec, /*bShouldStopOnSeparator=*/false) && !SeedSpec.IsEmpty())
	{
		bCheckSeedUsed = true;  // 검사 전용: 씨앗을 사서(BuyItem) 칸에 심는다(PlantCrop) — 둘 다 서버가 판정. `씨앗id:칸[:사는개수],…` (사는개수 기본 1, 0 이면 사지 않고 심기만)
		TArray<FString> Parts;
		SeedSpec.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			TArray<FString> Fields;
			Part.ParseIntoArray(Fields, TEXT(":"));
			if (Fields.Num() < 2)
			{
				continue;
			}
			const FString SeedID = Fields[0];
			const int32 SlotIndex = FCString::Atoi(*Fields[1]);
			const int32 BuyCount = Fields.Num() > 2 ? FCString::Atoi(*Fields[2]) : 1;
			TSharedPtr<FJsonObject> PlantArgs = MakeShared<FJsonObject>();
			PlantArgs->SetStringField(TEXT("seedItemId"), SeedID);
			PlantArgs->SetNumberField(TEXT("slot"), SlotIndex);
			const TWeakObjectPtr<USnareBackendSubsystem> WeakBackend(WatchedBackend);
			auto Plant = [WeakBackend, SeedID, SlotIndex, PlantArgs]()
			{
				if (USnareBackendSubsystem* const Backend = WeakBackend.Get())
				{
					Backend->CallServerFunction(TEXT("PlantCrop"), PlantArgs,
						[SeedID, SlotIndex](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
						{
							const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
							UE_LOG(LogSnarehaven, Log, TEXT("검사용 심기 (-SnareStorySeeds=%s 칸 %d) — 서버 답 %s %s"), *SeedID, SlotIndex, Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString());
						});
				}
			};
			if (BuyCount <= 0)
			{
				Plant();
				continue;
			}
			TSharedPtr<FJsonObject> BuyArgs = MakeShared<FJsonObject>();
			BuyArgs->SetStringField(TEXT("itemId"), SeedID);
			BuyArgs->SetNumberField(TEXT("count"), BuyCount);
			WatchedBackend->CallServerFunction(TEXT("BuyItem"), BuyArgs,
				[Plant, SeedID, BuyCount](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
				{
					const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 씨앗 사기 (-SnareStorySeeds=%s x%d) — 서버 답 %s %s"), *SeedID, BuyCount, Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString());
					if (Failure.IsEmpty())
					{
						Plant();  // 산 뒤에만 심는다 — 지급은 서버 답 뒤
					}
				});
		}
	}
	static bool bCheckCropUsed = false;
	FString CropSpec;
	if (!bCheckCropUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStoryHarvestCrop="), CropSpec, /*bShouldStopOnSeparator=*/false) && !CropSpec.IsEmpty())
	{
		bCheckCropUsed = true;  // 검사 전용: 작물 칸을 거두는 것처럼 서버 HarvestCrop 을 묻는다 (덜 자랐으면 서버가 0개로 답). `칸,칸,…`
		TArray<FString> Parts;
		CropSpec.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			const int32 SlotIndex = FCString::Atoi(*Part);
			TSharedPtr<FJsonObject> CropArgs = MakeShared<FJsonObject>();
			CropArgs->SetNumberField(TEXT("slot"), SlotIndex);
			WatchedBackend->CallServerFunction(TEXT("HarvestCrop"), CropArgs,
				[SlotIndex](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
				{
					const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
					double Harvested = -1.0;
					if (Response.IsValid())
					{
						Response->TryGetNumberField(TEXT("harvested"), Harvested);
					}
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 작물 수확 (-SnareStoryHarvestCrop= 칸 %d) — 서버 답 %s %s harvested=%.0f"), SlotIndex, Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString(), Harvested);
				});
		}
	}
	static bool bCheckFishSpotUsed = false;
	FString FishSpotSpec;
	if (!bCheckFishSpotUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStoryFishSpots="), FishSpotSpec, /*bShouldStopOnSeparator=*/false) && !FishSpotSpec.IsEmpty())
	{
		bCheckFishSpotUsed = true;  // 검사 전용: 낚시 칸에 물고기를 등록한다(RegisterFishingSpot, 서버가 판정). `물고기id:칸,…`
		TArray<FString> Parts;
		FishSpotSpec.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			TArray<FString> Fields;
			Part.ParseIntoArray(Fields, TEXT(":"));
			if (Fields.Num() < 2)
			{
				continue;
			}
			const FString FishID = Fields[0];
			const int32 SlotIndex = FCString::Atoi(*Fields[1]);
			TSharedPtr<FJsonObject> SpotArgs = MakeShared<FJsonObject>();
			SpotArgs->SetStringField(TEXT("fishItemId"), FishID);
			SpotArgs->SetNumberField(TEXT("slot"), SlotIndex);
			WatchedBackend->CallServerFunction(TEXT("RegisterFishingSpot"), SpotArgs,
				[FishID, SlotIndex](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
				{
					const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 낚시 칸 등록 (-SnareStoryFishSpots=%s 칸 %d) — 서버 답 %s %s"), *FishID, SlotIndex, Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString());
				});
		}
	}
	static bool bCheckFishHarvestUsed = false;
	FString FishHarvestSpec;
	if (!bCheckFishHarvestUsed && IsValid(WatchedBackend) && SnareCheckSwitch::Value(TEXT("SnareStoryHarvestFish="), FishHarvestSpec, /*bShouldStopOnSeparator=*/false) && !FishHarvestSpec.IsEmpty())
	{
		bCheckFishHarvestUsed = true;  // 검사 전용: 낚시 칸을 거두는 것처럼 서버 HarvestFishing 을 묻는다 (덜 지났으면 서버가 0마리로 답). `칸,칸,…`
		TArray<FString> Parts;
		FishHarvestSpec.ParseIntoArray(Parts, TEXT(","));
		for (const FString& Part : Parts)
		{
			const int32 SlotIndex = FCString::Atoi(*Part);
			TSharedPtr<FJsonObject> FishArgs = MakeShared<FJsonObject>();
			FishArgs->SetNumberField(TEXT("slot"), SlotIndex);
			WatchedBackend->CallServerFunction(TEXT("HarvestFishing"), FishArgs,
				[SlotIndex](bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error)
				{
					const FText Failure = SnareServerReply::DescribeFailure(bOk, Response, Error);
					double Harvested = -1.0;
					if (Response.IsValid())
					{
						Response->TryGetNumberField(TEXT("harvested"), Harvested);
					}
					UE_LOG(LogSnarehaven, Log, TEXT("검사용 낚시 수확 (-SnareStoryHarvestFish= 칸 %d) — 서버 답 %s %s harvested=%.0f"), SlotIndex, Failure.IsEmpty() ? TEXT("성공") : TEXT("거절"), *Failure.ToString(), Harvested);
				});
		}
	}
	return true;
}

void USnareStoryGoalWidget::Refresh()
{
	OnLineShown.Broadcast(!GetGoalText().IsEmpty());
	if (GoalText != nullptr)
	{
		const FText Text = GetGoalText();
		GoalText->SetText(Text);
		const int32 Lit = SnareStory::LighthouseRingsLit(View);
		for (int32 Index = 0; Index < RingPips.Num(); ++Index)
		{
			if (RingPips[Index] != nullptr)
			{
				FSlateColorBrush PipBrush(Index < Lit && bHasReply ? PipLit : PipDim);
				PipBrush.ImageSize = FVector2D(PipSize, PipSize);  // 색 브러시는 크기가 0 이라 칸이 안 보였다
				RingPips[Index]->SetBrush(PipBrush);
			}
		}
		if (Card != nullptr)
		{
			Card->SetVisibility(Text.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
		}
	}
}

int32 USnareStoryGoalWidget::CountLitRingPips() const
{
	int32 Count = 0;
	for (const TObjectPtr<UImage>& Pip : RingPips)
	{
		if (Pip != nullptr && Pip->GetBrush().TintColor.GetSpecifiedColor().Equals(PipLit))
		{
			++Count;
		}
	}
	return Count;
}

bool USnareStoryGoalWidget::IsTappable() const
{
	return Card != nullptr && Card->GetVisibility() == ESlateVisibility::Visible;
}

bool USnareStoryGoalWidget::NotifyTapped()
{
	if (!IsTappable())
	{
		return false;
	}
	OnTapped.Broadcast();
	return true;
}

FReply USnareStoryGoalWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	return NotifyTapped() ? FReply::Handled() : Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply USnareStoryGoalWidget::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	return NotifyTapped() ? FReply::Handled() : Super::NativeOnTouchStarted(InGeometry, InGestureEvent);
}

void USnareStoryGoalWidget::NativeDestruct()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}
	Super::NativeDestruct();
}

TSharedRef<SWidget> USnareStoryGoalWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		UCanvasPanel* const Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("StoryGoalRoot"));
		WidgetTree->RootWidget = Root;

		Card = SnareUiStyle::MakeCreamCard(*WidgetTree, TEXT("StoryGoalCard"));
		Card->SetVisibility(ESlateVisibility::Collapsed);
		Card->SetPadding(FMargin(16.f, 6.f));
		if (UCanvasPanelSlot* const CardSlot = Cast<UCanvasPanelSlot>(Root->AddChild(Card)))
		{
			CardSlot->SetAnchors(FAnchors(0.f, 0.f, 0.f, 0.f));
			CardSlot->SetAlignment(FVector2D(0.f, 0.f));
			CardSlot->SetPosition(FVector2D(CardLeft, CardTop));
			CardSlot->SetAutoSize(true);
		}

		GoalText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("StoryGoalText"));
		GoalText->SetColorAndOpacity(FSlateColor(SnareUiStyle::UiTextOnButton()));
		UVerticalBox* const Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("StoryGoalColumn"));
		Card->SetContent(Column);
		Column->AddChild(GoalText);
		UHorizontalBox* const Rings = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("StoryGoalRings"));
		Column->AddChild(Rings);
		RingPips.Reset();
		for (int32 Index = 0; Index < SnareStory::TotalSteps / SnareStory::StepsPerChapter; ++Index)
		{
			UImage* const Pip = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), *FString::Printf(TEXT("StoryRingPip%d"), Index));
			FSlateColorBrush InitialBrush(PipDim);
			InitialBrush.ImageSize = FVector2D(PipSize, PipSize);
			Pip->SetBrush(InitialBrush);
			Pip->SetDesiredSizeOverride(FVector2D(PipSize, PipSize));
			if (UHorizontalBoxSlot* const PipSlot = Cast<UHorizontalBoxSlot>(Rings->AddChild(Pip)))
			{
				PipSlot->SetPadding(FMargin(0.f, 4.f, 6.f, 0.f));
			}
			RingPips.Add(Pip);
		}
	}

	TSharedRef<SWidget> Result = Super::RebuildWidget();
	SnareUiStyle::ApplyUiStyle(*this);
	if (GoalText != nullptr)
	{
		SnareUiStyle::SetFontSize(*GoalText, FontSize);
	}
	Refresh();
	return Result;
}
