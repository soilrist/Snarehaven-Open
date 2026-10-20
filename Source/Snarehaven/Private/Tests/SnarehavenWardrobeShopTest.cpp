// DESIGN 10.7 옷장 꾸미기 상점 2층 검사 — `UWardrobeShopWidget`.
// 이 검사가 잡는 것: ① 진열 칸이 8칸이 아닌 것 ② 사기 요청이 기존 `BuyItem` 길 + `currency:"GM"` 이 아닌 것
// ③ **서버 답 전에 젬·배낭이 바뀌는 것**(CLAUDE.md 8) ④ 성공 답만으로 젬을 손으로 빼는 것 — 젬은 다시 받은 스냅샷에서만
// ⑤ 실패 답 뒤에 「구매 완료」 가 뜨거나 젬이 바뀌는 것.
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신한다. 검사 끝에 반드시 비운다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Data/ItemRow.h"
#include "Data/ItemRowLookup.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UI/WardrobeShopWidget.h"

namespace
{
	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr int32 TestGemsBefore = 500;
	constexpr int32 TestGemsAfter = 200;
	constexpr int32 TestOfferGems = 300;

	UWorld* CreateWardrobeTestWorld()
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyWardrobeTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	struct FWardrobeCall
	{
		FString FunctionName;
		FString ItemId;
		FString Currency;
		int32 Count = 0;
		USnareBackendSubsystem::FServerCallDone OnDone;
	};

	/** 부르면 적어 두기만 하고 답은 안 한다 — "응답 전" 을 붙잡아 둔다. */
	void InstallWardrobeServer(TArray<FWardrobeCall>& OutCalls)
	{
		USnareBackendSubsystem::TestServerOverride =
			[&OutCalls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
			{
				FWardrobeCall Call;
				Call.FunctionName = FunctionName;
				if (Args.IsValid())
				{
					Args->TryGetStringField(TEXT("itemId"), Call.ItemId);
					Args->TryGetStringField(TEXT("currency"), Call.Currency);
					Args->TryGetNumberField(TEXT("count"), Call.Count);
				}
				Call.OnDone = MoveTemp(OnDone);
				OutCalls.Add(MoveTemp(Call));
			};
	}

	FSnarePlayerSnapshot MakeGemSnapshot(int32 Gems, FName OwnedItem)
	{
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TEXT("PFTESTOWNER");
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		Snapshot.GemBalance = Gems;
		if (!OwnedItem.IsNone())
		{
			Snapshot.AddItem(OwnedItem, 1);
		}
		return Snapshot;
	}

	/** 서버 `GetWardrobeShop` 꼴 답 — 아이템 ID 는 검사용 가짜(이름은 ID 로 그린다). */
	TSharedPtr<FJsonObject> MakeCatalogReply(int32 Count)
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("itemId"), FString::Printf(TEXT("item.test_cosmetic_%d"), Index));
			Entry->SetNumberField(TEXT("gems"), TestOfferGems);
			Items.Add(MakeShared<FJsonValueObject>(Entry));
		}
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetNumberField(TEXT("week"), 1);
		Reply->SetArrayField(TEXT("items"), Items);
		return Reply;
	}

	int32 OwnedCount(const USnareBackendSubsystem& Backend, FName ItemID)
	{
		int32 Total = 0;
		for (const FInventorySlot& Entry : Backend.GetPlayerSnapshot().InventorySlots)
		{
			Total += Entry.ItemID == ItemID ? Entry.Count : 0;
		}
		return Total;
	}

	void PressButton(UWardrobeShopWidget& Screen, const TCHAR* Name)
	{
		if (UButton* const Button = Cast<UButton>(Screen.WidgetTree->FindWidget(FName(Name))))
		{
			Button->OnClicked.Broadcast();
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWardrobeShopFlowTest, "Snarehaven.Shop.Wardrobe.BuyWaitsForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWardrobeShopFlowTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateWardrobeTestWorld();
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	TArray<FWardrobeCall> Calls;
	InstallWardrobeServer(Calls);
	Backend->ApplyPlayerSnapshot(MakeGemSnapshot(TestGemsBefore, NAME_None), true);

	UWardrobeShopWidget* const Screen = CreateWidget<UWardrobeShopWidget>(World, UWardrobeShopWidget::StaticClass());
	Screen->TakeWidget();
	Screen->BindToBackend(Backend);
	Screen->SetOpen(true);

	// ① 열면 진열을 서버에 묻고, 칸은 8칸이다.
	TestEqual(TEXT("열면 GetWardrobeShop 한 번"), Calls.Num(), 1);
	TestEqual(TEXT("진열 함수"), Calls.Num() > 0 ? Calls[0].FunctionName : FString(), FString(TEXT("GetWardrobeShop")));
	int32 TileWidgets = 0;
	while (Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("WardrobeTile_%d"), TileWidgets))) != nullptr)
	{
		++TileWidgets;
	}
	TestEqual(TEXT("진열 칸 8"), TileWidgets, 8);
	if (Calls.Num() > 0)
	{
		Calls[0].OnDone(true, MakeCatalogReply(8), FString());
	}
	TestEqual(TEXT("서버 진열 8개가 8칸에"), Screen->GetOffers().Num(), 8);
	const UButton* const LastTile = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("WardrobeTile_7")));
	TestTrue(TEXT("8번째 칸도 누를 수 있다"), LastTile != nullptr && LastTile->GetIsEnabled());

	// ② 칸 → 구매 확인 창 → 「구매」 = 기존 BuyItem 길에 currency GM.
	PressButton(*Screen, TEXT("WardrobeTile_0"));
	TestTrue(TEXT("구매 확인 창"), Screen->IsBuyWindowOpen());
	PressButton(*Screen, TEXT("WardrobeConfirmButton"));
	TestEqual(TEXT("구매 요청 한 번"), Calls.Num(), 2);
	const FName Bought = Screen->GetOffers()[0].ItemID;
	if (Calls.Num() == 2)
	{
		TestEqual(TEXT("BuyItem 재사용"), Calls[1].FunctionName, FString(TEXT("BuyItem")));
		TestEqual(TEXT("itemId"), Calls[1].ItemId, Bought.ToString());
		TestEqual(TEXT("count 1"), Calls[1].Count, 1);
		TestEqual(TEXT("currency GM"), Calls[1].Currency, USnareBackendSubsystem::GetGemCurrencyCode());
	}

	// ③ 답 전 — 젬·배낭 그대로, 완료 창 없음.
	const FText GemBefore = Screen->GetGemText();
	TestTrue(TEXT("답 전 보내는 중"), Screen->IsPending());
	TestEqual(TEXT("답 전 젬 글"), Screen->GetGemText().ToString(), GemBefore.ToString());
	TestEqual(TEXT("답 전 젬 캐시"), Backend->GetPlayerSnapshot().GemBalance, TestGemsBefore);
	TestEqual(TEXT("답 전 배낭"), OwnedCount(*Backend, Bought), 0);
	TestFalse(TEXT("답 전 완료 창 없음"), Screen->IsCompleteWindowOpen());

	// ④ 성공 답 — 완료 창은 뜨지만 젬은 손으로 안 뺀다. 다시 받은 스냅샷이 오면 그때 바뀐다.
	if (Calls.Num() == 2)
	{
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetStringField(TEXT("bought"), Bought.ToString());
		Reply->SetNumberField(TEXT("count"), 1);
		Reply->SetNumberField(TEXT("cost"), TestOfferGems);
		Reply->SetStringField(TEXT("currency"), TEXT("GM"));
		Calls[1].OnDone(true, Reply, FString());
	}
	TestTrue(TEXT("성공 뒤 구매 완료 창"), Screen->IsCompleteWindowOpen());
	TestEqual(TEXT("성공 답만으로는 젬 그대로"), Backend->GetPlayerSnapshot().GemBalance, TestGemsBefore);
	Backend->ApplyPlayerSnapshot(MakeGemSnapshot(TestGemsAfter, Bought), true);
	TestEqual(TEXT("스냅샷 뒤 젬 글"), Screen->GetGemText().ToString(),
		FText::Format(FText::FromString(TEXT("젬 {0}")), FText::AsNumber(TestGemsAfter)).ToString());
	TestEqual(TEXT("스냅샷 뒤 배낭"), OwnedCount(*Backend, Bought), 1);
	PressButton(*Screen, TEXT("WardrobeCompleteOkButton"));
	TestFalse(TEXT("확인으로 완료 창 닫힘"), Screen->IsCompleteWindowOpen());

	// ⑤ 실패 답(서버 거절·네트워크) — 젬·배낭 그대로, 완료 창 없음.
	const FText GemAfter = Screen->GetGemText();
	const FName Second = Screen->GetOffers()[1].ItemID;
	for (int32 Attempt = 0; Attempt < 2; ++Attempt)
	{
		Screen->SelectOffer(1);
		const int32 Before = Calls.Num();
		TestTrue(TEXT("실패 경우 보냄"), Screen->ConfirmBuy());
		if (Calls.Num() == Before + 1)
		{
			if (Attempt == 0)
			{
				const TSharedPtr<FJsonObject> Rejected = MakeShared<FJsonObject>();
				Rejected->SetStringField(TEXT("error"), TEXT("젬이 모자란다"));
				Rejected->SetStringField(TEXT("code"), TEXT("not_enough_gems"));
				Calls.Last().OnDone(true, Rejected, FString());
			}
			else
			{
				Calls.Last().OnDone(false, nullptr, TEXT("network down"));
			}
		}
		TestFalse(TEXT("실패 뒤 완료 창 없음"), Screen->IsCompleteWindowOpen());
		TestFalse(TEXT("실패 뒤 보내는 중 풀림"), Screen->IsPending());
		TestEqual(TEXT("실패 뒤 젬 글"), Screen->GetGemText().ToString(), GemAfter.ToString());
		TestEqual(TEXT("실패 뒤 젬 캐시"), Backend->GetPlayerSnapshot().GemBalance, TestGemsAfter);
		TestEqual(TEXT("실패 뒤 배낭"), OwnedCount(*Backend, Second), 0);
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyWardrobeTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWardrobeShopIconTest, "Snarehaven.Shop.Wardrobe.TilesShowIcons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWardrobeShopIconTest::RunTest(const FString& Parameters)
{
	// 진열 8칸 모두 아이콘 그림이 비지 않는다 — 진짜 아이템 표의 꾸미기(`Cosmetic`) 행 앞 8개로 채운다.
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	if (!TestNotNull(TEXT("아이템 표"), ItemTable))
	{
		return false;
	}
	TArray<FName> CosmeticIds;
	int32 CosmeticRows = 0;
	int32 CosmeticRowsWithoutIcon = 0;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row->ItemType == EItemType::Cosmetic)
		{
			++CosmeticRows;
			CosmeticRowsWithoutIcon += Row->IconRef.IsNull() ? 1 : 0;
			if (CosmeticIds.Num() < UWardrobeShopWidget::TileCount)
			{
				CosmeticIds.Add(Row->ItemID);
			}
		}
	}
	TestTrue(TEXT("꾸미기 행이 8개 이상"), CosmeticIds.Num() == UWardrobeShopWidget::TileCount);
	TestEqual(TEXT("아이콘 없는 꾸미기 행"), CosmeticRowsWithoutIcon, 0);

	UWorld* const World = CreateWardrobeTestWorld();
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	TArray<FWardrobeCall> Calls;
	InstallWardrobeServer(Calls);
	UWardrobeShopWidget* const Screen = CreateWidget<UWardrobeShopWidget>(World, UWardrobeShopWidget::StaticClass());
	Screen->TakeWidget();
	Screen->BindToBackend(Backend);
	Screen->SetOpen(true);
	TArray<TSharedPtr<FJsonValue>> Items;
	for (const FName Id : CosmeticIds)
	{
		const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("itemId"), Id.ToString());
		Entry->SetNumberField(TEXT("gems"), TestOfferGems);
		Items.Add(MakeShared<FJsonValueObject>(Entry));
	}
	const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetArrayField(TEXT("items"), Items);
	if (TestEqual(TEXT("진열 요청"), Calls.Num(), 1))
	{
		Calls[0].OnDone(true, Reply, FString());
	}
	for (int32 Index = 0; Index < UWardrobeShopWidget::TileCount; ++Index)
	{
		TestNotNull(*FString::Printf(TEXT("칸 %d 아이콘 텍스처"), Index), Screen->GetTileIconTexture(Index));
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyWardrobeTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWardrobeShopRetryTest, "Snarehaven.Shop.Wardrobe.Retry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWardrobeShopRetryTest::RunTest(const FString& Parameters)
{
	// 진열을 못 받으면 이유 곁에 「다시 시도」 — 누르면 `GetWardrobeShop` 을 다시 묻는다.
	UWorld* const World = CreateWardrobeTestWorld();
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	TArray<FWardrobeCall> Calls;
	InstallWardrobeServer(Calls);
	Backend->ApplyPlayerSnapshot(MakeGemSnapshot(TestGemsBefore, NAME_None), true);
	UWardrobeShopWidget* const Screen = CreateWidget<UWardrobeShopWidget>(World, UWardrobeShopWidget::StaticClass());
	Screen->TakeWidget();
	Screen->BindToBackend(Backend);
	Screen->SetOpen(true);

	const UWidget* const RetryBox = Screen->WidgetTree->FindWidget(TEXT("WardrobeRetryBox"));
	TestNotNull(TEXT("다시 시도 단추가 있다"), Screen->WidgetTree->FindWidget(TEXT("WardrobeRetryButton")));
	if (RetryBox != nullptr && Calls.Num() == 1)
	{
		TestFalse(TEXT("묻는 중엔 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		Calls[0].OnDone(false, nullptr, TEXT("net"));
		TestTrue(TEXT("진열 실패 → 다시 시도 보임"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		PressButton(*Screen, TEXT("WardrobeRetryButton"));
		TestEqual(TEXT("다시 시도 → GetWardrobeShop 다시"), Calls.Num() > 1 ? Calls.Last().FunctionName : FString(), FString(TEXT("GetWardrobeShop")));
		TestFalse(TEXT("누른 뒤 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		if (Calls.Num() > 1)
		{
			Calls.Last().OnDone(true, MakeCatalogReply(8), FString());
		}
		TestFalse(TEXT("성공 뒤 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyWardrobeTestWorld(World);
	return true;
}

#endif
