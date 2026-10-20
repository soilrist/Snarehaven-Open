// DESIGN 8.14 잡화점 2층 검사 — 상점 화면(`UShopScreenWidget`)과 상인(`AShopVendorActor`), 시작 코인 표시.
// 이 검사가 잡는 것: ① 사기·팔기 요청 인자(`BuyItem`/`SellItem {itemId, count:1}`)가 서버 규약과 어긋나는 것
// ② **서버 답 전에 코인이 바뀌는 것** — 화면이 먼저 빼 보이면 그게 곧 클라이언트가 만든 잔액이다 (CLAUDE.md 8)
// ③ 파는 값 0 인 것이 팔기 목록에 뜨는 것 ④ 거절 사유를 삼키는 것
// ⑤ 시작 코인 10 을 **지급 응답의 `coins`** 로 캐시에 더하는 것 — 코인은 재조회한 스냅샷에서만 온다.
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신한다. 검사 끝에 반드시 비운다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/ArrangedChildren.h"
#include "Rendering/SlateRenderer.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/SnareUiStyle.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/GachaPoolRow.h"
#include "Data/ItemRow.h"
#include "Data/ShopRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "SnarehavenCharacter.h"
#include "Components/CanvasPanelSlot.h"
#include "UI/QuestScreenWidget.h"
#include "UI/ShopScreenWidget.h"
#include "UI/SnarePromptCardWidget.h"
#include "World/ShopVendorActor.h"
#include "World/ZoneTravelComponent.h"

namespace
{
	const TCHAR* ShopTablePath = TEXT("/Game/Game/Data/Shop/DT_ShopTable.DT_ShopTable");
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr int32 TestCoinBefore = 7;
	constexpr int32 TestCoinAfter = 4;
	constexpr int32 TestSellableCount = 2;
	constexpr int32 TestUnsellableCount = 3;
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	USnareBackendSubsystem* MakeTestBackend()
	{
		UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
		return NewObject<USnareBackendSubsystem>(GameInstance);
	}

	/** 서버가 부른 것 한 건 — 함수 이름·인자와, 답을 나중에 줄 수 있게 받은 콜백. */
	struct FRecordedCall
	{
		FString FunctionName;
		FString ItemId;
		int32 Count = 0;
		USnareBackendSubsystem::FServerCallDone OnDone;
	};

	/** 부르면 적어 두기만 하고 **답은 안 한다** — "응답 전" 을 붙잡아 두려는 가짜 서버. */
	void InstallRecordingServer(TArray<FRecordedCall>& OutCalls)
	{
		USnareBackendSubsystem::TestServerOverride =
			[&OutCalls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args,
				USnareBackendSubsystem::FServerCallDone OnDone)
			{
				FRecordedCall Call;
				Call.FunctionName = FunctionName;
				if (Args.IsValid())
				{
					Args->TryGetStringField(TEXT("itemId"), Call.ItemId);
					Args->TryGetNumberField(TEXT("count"), Call.Count);
				}
				Call.OnDone = MoveTemp(OnDone);
				OutCalls.Add(MoveTemp(Call));
			};
	}

	/** 표에서 파는 값이 있는 것 하나·없는 것 하나를 고른다 — 아이템 ID 를 검사에 박지 않는다. */
	void PickSellableItems(const UDataTable& ItemTable, FName& OutSellable, FName& OutUnsellable)
	{
		for (const TPair<FName, uint8*>& Pair : ItemTable.GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			if (Row == nullptr)
			{
				continue;
			}
			if (Row->SellPrice > 0 && OutSellable.IsNone())
			{
				OutSellable = Row->ItemID;
			}
			if (Row->SellPrice <= 0 && OutUnsellable.IsNone())
			{
				OutUnsellable = Row->ItemID;
			}
		}
	}

	FSnarePlayerSnapshot MakeSnapshot(int32 Coins, FName Sellable, FName Unsellable)
	{
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TEXT("PFTESTOWNER");
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		Snapshot.CoinBalance = Coins;
		Snapshot.AddItem(Sellable, TestSellableCount);
		Snapshot.AddItem(Unsellable, TestUnsellableCount);
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopListsTest, "Snarehaven.Shop.Screen.ListsFollowTables",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopListsTest::RunTest(const FString& Parameters)
{
	UDataTable* const ShopTable = LoadObject<UDataTable>(nullptr, ShopTablePath);
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ShopTable 로드"), ShopTable) || !TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}
	FName Sellable;
	FName Unsellable;
	PickSellableItems(*ItemTable, Sellable, Unsellable);
	if (!TestFalse(TEXT("파는 값이 있는 아이템이 표에 있다"), Sellable.IsNone())
		|| !TestFalse(TEXT("파는 값 0 인 아이템이 표에 있다"), Unsellable.IsNone()))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상점 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	TSharedPtr<SWidget> Slate = Screen->TakeWidget();

	// ⓪ 모바일 스크롤(M22) — 스크롤 안 버튼은 손가락을 끌면 스크롤로 넘겨야 한다(`PreciseTap`/`PreciseClick`).
	//    기본값(`DownAndUp`)이면 줄 버튼이 터치를 먼저 잡아 목록이 안 움직인다. 스크롤 밖 탭 버튼은 기본값 그대로.
	int32 ScrollButtons = 0;
	Screen->WidgetTree->ForEachWidget([this, &ScrollButtons](UWidget* Widget)
	{
		const UButton* const Button = Cast<UButton>(Widget);
		if (Button == nullptr || !SnareUiStyle::IsInsideScrollBox(*Button))
		{
			return;
		}
		++ScrollButtons;
		TestEqual(*FString::Printf(TEXT("%s 터치는 PreciseTap"), *Button->GetName()),
			static_cast<int32>(Button->GetTouchMethod()), static_cast<int32>(EButtonTouchMethod::PreciseTap));
		TestEqual(*FString::Printf(TEXT("%s 클릭은 PreciseClick"), *Button->GetName()),
			static_cast<int32>(Button->GetClickMethod()), static_cast<int32>(EButtonClickMethod::PreciseClick));
	});
	AddInfo(FString::Printf(TEXT("스크롤 안 버튼 %d개"), ScrollButtons));
	TestNotNull(TEXT("줄 버튼 ShopLineButton_0 이 스크롤 안에 있다"), Screen->WidgetTree->FindWidget<UButton>(TEXT("ShopLineButton_0")));
	TestTrue(TEXT("스크롤 안 버튼이 한 개 이상"), ScrollButtons > 0);
	if (const UButton* const Tab = Screen->WidgetTree->FindWidget<UButton>(TEXT("BuyTabButton")))
	{
		TestEqual(TEXT("스크롤 밖 탭 버튼은 기본 터치"),
			static_cast<int32>(Tab->GetTouchMethod()), static_cast<int32>(EButtonTouchMethod::DownAndUp));
	}

	// ① 백엔드 전 — 코인 숫자 없음(`연결 안 됨`), 팔기 목록 비어 있음 (0 코인으로 보이면 "빈털터리" 로 읽힌다).
	TestTrue(TEXT("백엔드 전 코인은 숫자 대신 `연결 안 됨`"), Screen->GetCoinText().ToString().Contains(TEXT("연결 안 됨")));
	TestEqual(TEXT("백엔드 전 팔기 목록은 비었다"), Screen->BuildSellLines().Num(), 0);

	// ② 사기 목록 — 가게 넷(DESIGN 8.14 「가게 여럿」)이 상점 표를 빠짐없이·겹침 없이 나눈다. 가게마다 한 줄 이상.
	int32 BuyTotal = 0;
	for (const EShopKind Kind : {EShopKind::General, EShopKind::Fishing, EShopKind::Farming, EShopKind::Clothes})
	{
		Screen->SetShopKind(Kind);
		const TArray<FShopLineView> BuyLines = Screen->BuildBuyLines();
		AddInfo(FString::Printf(TEXT("가게 %d 사기 %d줄"), static_cast<int32>(Kind), BuyLines.Num()));
		TestTrue(*FString::Printf(TEXT("가게 %d 진열이 비지 않았다"), static_cast<int32>(Kind)), BuyLines.Num() > 0);
		BuyTotal += BuyLines.Num();
		for (const FShopLineView& Line : BuyLines)
		{
			TestEqual(*FString::Printf(TEXT("사기 줄 %s 는 이 가게 물건"), *Line.ItemID.ToString()),
				UShopScreenWidget::GetSellerKind(Line.ItemID), Kind);
			TestFalse(*FString::Printf(TEXT("사기 줄 %s 에 날 ID 가 안 뜬다"), *Line.ItemID.ToString()),
				Screen->GetLineText(Line, false).ToString().Contains(TEXT("item.")));
		}
	}
	TestEqual(TEXT("가게 넷의 사기 줄 합이 DT_ShopTable 행 수와 같다"), BuyTotal, ShopTable->GetRowMap().Num());
	TestEqual(TEXT("낚싯대는 낚시 상인"), UShopScreenWidget::GetSellerKind(TEXT("item.rod_basic")), EShopKind::Fishing);
	TestEqual(TEXT("씨앗은 농부 상인"), UShopScreenWidget::GetSellerKind(TEXT("item.seed_pumpkin")), EShopKind::Farming);
	TestEqual(TEXT("페인트는 옷 가게"), UShopScreenWidget::GetSellerKind(TEXT("item.paint_red")), EShopKind::Clothes);
	TestEqual(TEXT("텐트는 잡화상"), UShopScreenWidget::GetSellerKind(TEXT("item.tent")), EShopKind::General);
	Screen->SetShopKind(EShopKind::General);

	// ③ 팔기 목록 — 파는 값 있는 것만. 0 인 것은 가지고 있어도 없다.
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeSnapshot(TestCoinBefore, Sellable, Unsellable), /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	const TArray<FShopLineView> SellLines = Screen->BuildSellLines();
	AddInfo(FString::Printf(TEXT("팔기 %d줄 (가진 것 %s x%d 값>0, %s x%d 값 0)"), SellLines.Num(),
		*Sellable.ToString(), TestSellableCount, *Unsellable.ToString(), TestUnsellableCount));
	TestEqual(TEXT("팔기 목록은 파는 값 있는 것 1줄"), SellLines.Num(), 1);
	TestTrue(TEXT("팔기 목록에 값 있는 것이 있다"),
		SellLines.ContainsByPredicate([Sellable](const FShopLineView& Line) { return Line.ItemID == Sellable; }));
	TestFalse(TEXT("SellPrice 0 인 것은 팔기 목록에 없다"),
		SellLines.ContainsByPredicate([Unsellable](const FShopLineView& Line) { return Line.ItemID == Unsellable; }));
	if (SellLines.Num() == 1)
	{
		TestEqual(TEXT("팔기 줄 개수는 스냅샷 개수"), SellLines[0].Count, TestSellableCount);
	}

	// ③-1 내렸다 다시 올림(M22) — 상인에게서 멀어지면 화면이 내려가 `NativeDestruct` 가 돈다.
	//      같은 화면을 다시 올렸을 때 백엔드가 풀린 채면 코인이 `연결 안 됨` 으로 굳는다.
	Slate.Reset();
	TestTrue(TEXT("내린 동안은 백엔드를 풀어 둔다"), Screen->GetCoinText().ToString().Contains(TEXT("연결 안 됨")));
	Slate = Screen->TakeWidget();
	TestFalse(TEXT("다시 올리면 백엔드가 다시 묶인다 — `연결 안 됨` 이 아니다"),
		Screen->GetCoinText().ToString().Contains(TEXT("연결 안 됨")));
	TestEqual(TEXT("다시 올린 뒤에도 팔기 목록이 보인다"), Screen->BuildSellLines().Num(), 1);
	Slate.Reset();
	Slate = Screen->TakeWidget();
	TestFalse(TEXT("두 번째 재진입도 묶여 있다"), Screen->GetCoinText().ToString().Contains(TEXT("연결 안 됨")));

	// ④ 전문 상인은 자기 분야만 산다 — 낚시 = 물고기 태그, 농부 = 씨앗 있는 작물, 옷 가게 = 안 산다.
	const FItemRow* Fish = nullptr;
	const FItemRow* Crop = nullptr;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		Fish = Fish == nullptr && Row->CraftTag == ECraftTag::Fish ? Row : Fish;
		Crop = Crop == nullptr && Row->ItemID == FName(TEXT("item.pumpkin")) ? Row : Crop;
	}
	if (TestNotNull(TEXT("물고기 행"), Fish) && TestNotNull(TEXT("호박 행"), Crop))
	{
		TestTrue(TEXT("낚시 상인은 물고기를 산다"), Screen->DoesBuy(EShopKind::Fishing, *Fish));
		TestFalse(TEXT("낚시 상인은 호박을 안 산다"), Screen->DoesBuy(EShopKind::Fishing, *Crop));
		TestTrue(TEXT("농부 상인은 호박을 산다"), Screen->DoesBuy(EShopKind::Farming, *Crop));
		TestFalse(TEXT("농부 상인은 물고기를 안 산다"), Screen->DoesBuy(EShopKind::Farming, *Fish));
		TestFalse(TEXT("옷 가게는 아무것도 안 산다"), Screen->DoesBuy(EShopKind::Clothes, *Fish));
		TestTrue(TEXT("잡화상은 다 산다"), Screen->DoesBuy(EShopKind::General, *Crop) && Screen->DoesBuy(EShopKind::General, *Fish));
	}

	// ⑤ 펫 판매가(M22 ④, DESIGN 8.14 「펫 판매가」) — 펫 행은 전부 값이 있고 같은 값이며 잡화상만 산다.
	TArray<const FItemRow*> Pets;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row->ItemType == EItemType::Pet)
		{
			Pets.Add(Row);
		}
	}
	AddInfo(FString::Printf(TEXT("펫 행 %d개 · 첫 행 판매가 %d"), Pets.Num(), Pets.IsEmpty() ? 0 : Pets[0]->SellPrice));
	TestTrue(TEXT("펫 행이 있다"), Pets.Num() > 0);
	for (const FItemRow* const Pet : Pets)
	{
		TestTrue(*FString::Printf(TEXT("%s 판매가 > 0"), *Pet->ItemID.ToString()), Pet->SellPrice > 0);
		TestEqual(*FString::Printf(TEXT("%s 판매가는 펫끼리 같다"), *Pet->ItemID.ToString()), Pet->SellPrice, Pets[0]->SellPrice);
		TestTrue(*FString::Printf(TEXT("잡화상은 %s 를 산다"), *Pet->ItemID.ToString()), Screen->DoesBuy(EShopKind::General, *Pet));
		TestFalse(*FString::Printf(TEXT("낚시 상인은 %s 를 안 산다"), *Pet->ItemID.ToString()), Screen->DoesBuy(EShopKind::Fishing, *Pet));
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopClosedHidesPartsTest, "Snarehaven.Shop.Screen.ClosedHidesTitleAndCoins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopClosedHidesPartsTest::RunTest(const FString& Parameters)
{
	// M23-01 — 제목 탭·코인 줄은 판 밖 루트에 선다. 닫힌 상점에서 HUD 위에 남으면 안 된다.
	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상점 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	const TCHAR* const PartNames[] = {TEXT("ShopPanel"), TEXT("TitlePill"), TEXT("CoinPill")};

	const auto ExpectParts = [this, Screen, &PartNames](const TCHAR* Phase, ESlateVisibility Expected)
	{
		for (const TCHAR* const Name : PartNames)
		{
			const UWidget* const Part = Screen->WidgetTree->FindWidget(FName(Name));
			if (!TestNotNull(*FString::Printf(TEXT("%s 가 있다"), Name), Part))
			{
				continue;
			}
			AddInfo(FString::Printf(TEXT("[상점닫힘] %s · %s = %s"), Phase, Name,
				*UEnum::GetValueAsString(Part->GetVisibility())));
			TestEqual(*FString::Printf(TEXT("%s · %s 보임"), Phase, Name),
				static_cast<int32>(Part->GetVisibility()), static_cast<int32>(Expected));
		}
	};

	TestFalse(TEXT("처음엔 닫혀 있다"), Screen->IsOpen());
	Screen->RefreshDisplay();
	ExpectParts(TEXT("처음"), ESlateVisibility::Collapsed);
	Screen->SetOpen(true);
	ExpectParts(TEXT("열림"), ESlateVisibility::Visible);
	Screen->SetOpen(false);
	ExpectParts(TEXT("다시 닫힘"), ESlateVisibility::Collapsed);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopOneCoinPillTest, "Snarehaven.Shop.Screen.OneCoinPillWhileOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopOneCoinPillTest::RunTest(const FString& Parameters)
{
	// M106-09 — 상점 판 머리와 배낭 판 머리에 같은 「코인 N」 이 둘 보이면 「둘이 다른 돈?」 으로 읽힌다. 상점이 열린 동안엔 상점 쪽 하나만.
	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	UBackpackScreenWidget* const Backpack = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상점 화면 위젯"), Screen) || !TestNotNull(TEXT("배낭 화면 위젯"), Backpack))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> ShopSlate = Screen->TakeWidget();
	const TSharedRef<SWidget> BackpackSlate = Backpack->TakeWidget();
	Screen->BindBackpackScreen(Backpack);

	TestFalse(TEXT("처음엔 배낭 코인 칩이 안 접혀 있다"), Backpack->IsBalanceChipHidden());
	Screen->SetOpen(true);
	TestTrue(TEXT("상점이 열리면 배낭 코인 칩이 접힌다"), Backpack->IsBalanceChipHidden());
	Screen->SetOpen(false);
	TestFalse(TEXT("상점이 닫히면 배낭 코인 칩이 다시 보인다"), Backpack->IsBalanceChipHidden());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopOneCloseButtonTest, "Snarehaven.Shop.Screen.OneCloseButtonWhileOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopOneCloseButtonTest::RunTest(const FString& Parameters)
{
	// 출시점검 ③ — 상점과 배낭이 같이 열려 「닫기」 가 둘 보였다. 상점 탭 줄의 「닫기」 하나만 남기고, 상점이 닫히면 배낭 쪽이 되돌아온다.
	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	UBackpackScreenWidget* const Backpack = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상점 화면 위젯"), Screen) || !TestNotNull(TEXT("배낭 화면 위젯"), Backpack))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> ShopSlate = Screen->TakeWidget();
	const TSharedRef<SWidget> BackpackSlate = Backpack->TakeWidget();
	Screen->BindBackpackScreen(Backpack);

	Backpack->SetPanelOpen(true);
	TestFalse(TEXT("상점 없이 배낭만 열면 배낭 「닫기」 가 보인다"), Backpack->IsCloseHidden());
	Backpack->SetPanelOpen(false);
	Screen->SetOpen(true);
	TestTrue(TEXT("상점이 열리면 배낭 「닫기」 가 접힌다"), Backpack->IsCloseHidden());
	Backpack->SetPanelOpen(true);
	TestTrue(TEXT("판을 다시 펴도 접힌 채다"), Backpack->IsCloseHidden());
	Screen->SetOpen(false);
	Backpack->SetPanelOpen(true);
	TestFalse(TEXT("상점이 닫힌 뒤 배낭만 열면 「닫기」 가 되돌아온다"), Backpack->IsCloseHidden());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopTradeTest, "Snarehaven.Shop.Screen.TradeWaitsForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopTradeTest::RunTest(const FString& Parameters)
{
	UDataTable* const ShopTable = LoadObject<UDataTable>(nullptr, ShopTablePath);
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ShopTable 로드"), ShopTable) || !TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}
	FName Sellable;
	FName Unsellable;
	PickSellableItems(*ItemTable, Sellable, Unsellable);

	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상점 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();

	// 백엔드 없이는 거래를 띄우지 않는다.
	TArray<FRecordedCall> Calls;
	InstallRecordingServer(Calls);
	const TArray<FShopLineView> BuyLines = Screen->BuildBuyLines();
	const FName BuyID = BuyLines.Num() > 0 ? BuyLines[0].ItemID : NAME_None;
	TestFalse(TEXT("백엔드 없으면 사기 요청을 안 띄운다"), Screen->RequestBuy(BuyID));
	TestEqual(TEXT("백엔드 없으면 서버를 안 부른다"), Calls.Num(), 0);

	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeSnapshot(TestCoinBefore, Sellable, Unsellable), /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	Screen->SetOpen(true);
	const FString CoinTextBefore = Screen->GetCoinText().ToString();

	// ① 사기 — 인자 {itemId, count:1}. 답 전에는 코인·코인 글자 그대로, 줄은 잠김(두 번째 요청 거절).
	TestTrue(TEXT("사기 요청을 띄운다"), Screen->RequestBuy(BuyID));
	if (!TestEqual(TEXT("서버를 한 번 불렀다"), Calls.Num(), 1))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		DestroyTestWorld(World);
		return false;
	}
	AddInfo(FString::Printf(TEXT("사기 요청: %s itemId=%s count=%d / 답 전 코인 %d [%s]"), *Calls[0].FunctionName,
		*Calls[0].ItemId, Calls[0].Count, Backend->GetPlayerSnapshot().CoinBalance,
		*Screen->GetCoinText().ToString().Replace(TEXT("\n"), TEXT(" "))));
	TestEqual(TEXT("사기 함수 이름"), Calls[0].FunctionName, FString(TEXT("BuyItem")));
	TestEqual(TEXT("사기 itemId 는 상점 표의 ItemID"), Calls[0].ItemId, BuyID.ToString());
	TestEqual(TEXT("사기 count 는 1"), Calls[0].Count, 1);
	TestEqual(TEXT("답 전 캐시 코인 그대로"), Backend->GetPlayerSnapshot().CoinBalance, TestCoinBefore);
	TestEqual(TEXT("답 전 코인 글자 그대로"), Screen->GetCoinText().ToString(), CoinTextBefore);
	TestTrue(TEXT("답 전에는 대기 중"), Screen->IsRequestPending());
	TestFalse(TEXT("답 전에는 다른 거래를 안 띄운다"), Screen->RequestSell(Sellable));
	TestEqual(TEXT("잠긴 동안 서버를 더 안 부른다"), Calls.Num(), 1);

	// 성공 답 — 대기가 풀리고 "샀다". **코인은 여전히 그대로** (바뀌는 길은 재조회 스냅샷뿐).
	const TSharedPtr<FJsonObject> BuyReply = MakeShared<FJsonObject>();
	BuyReply->SetStringField(TEXT("bought"), BuyID.ToString());
	BuyReply->SetNumberField(TEXT("count"), 1);
	BuyReply->SetNumberField(TEXT("cost"), 3);
	Calls[0].OnDone(true, BuyReply, FString());
	TestFalse(TEXT("답 뒤에는 대기가 풀린다"), Screen->IsRequestPending());
	TestTrue(TEXT("성공 문구가 뜬다"), Screen->GetMessage().ToString().Contains(TEXT("샀다")));
	TestEqual(TEXT("성공 답만으로 캐시 코인이 바뀌지 않는다"), Backend->GetPlayerSnapshot().CoinBalance, TestCoinBefore);

	// 재조회 스냅샷이 오면 그때 바뀐다.
	Backend->ApplyPlayerSnapshot(MakeSnapshot(TestCoinAfter, Sellable, Unsellable), /*bFromBackend=*/true);
	AddInfo(FString::Printf(TEXT("재조회 뒤 코인 [%s]"), *Screen->GetCoinText().ToString().Replace(TEXT("\n"), TEXT(" "))));
	TestTrue(TEXT("재조회 스냅샷 뒤 코인 글자가 바뀐다"),
		Screen->GetCoinText().ToString().Contains(FString::Printf(TEXT("코인 %d"), TestCoinAfter)));

	// ② 팔기 — 인자 {itemId, count:1}. 서버 거절이면 사유를 보이고 아무것도 안 바뀐다.
	TestTrue(TEXT("팔기 요청을 띄운다"), Screen->RequestSell(Sellable));
	if (TestEqual(TEXT("서버를 두 번째로 불렀다"), Calls.Num(), 2))
	{
		TestEqual(TEXT("팔기 함수 이름"), Calls[1].FunctionName, FString(TEXT("SellItem")));
		TestEqual(TEXT("팔기 itemId"), Calls[1].ItemId, Sellable.ToString());
		TestEqual(TEXT("팔기 count 는 1"), Calls[1].Count, 1);

		const TSharedPtr<FJsonObject> Refuse = MakeShared<FJsonObject>();
		Refuse->SetStringField(TEXT("error"), TEXT("가진 것보다 많다"));
		Refuse->SetStringField(TEXT("code"), TEXT("not_enough_items"));
		Refuse->SetNumberField(TEXT("need"), 1);
		Refuse->SetNumberField(TEXT("have"), 0);
		Calls[1].OnDone(true, Refuse, FString());
		AddInfo(FString::Printf(TEXT("팔기 거절 문구 [%s]"), *Screen->GetMessage().ToString()));
		TestTrue(TEXT("거절 사유가 뜬다"), Screen->GetMessage().ToString().Contains(TEXT("가진 것보다 많다")));
		TestFalse(TEXT("거절이면 성공 문구가 아니다"), Screen->GetMessage().ToString().Contains(TEXT("팔았다")));
		TestEqual(TEXT("거절 뒤 코인 그대로"), Backend->GetPlayerSnapshot().CoinBalance, TestCoinAfter);
	}

	// ③ 모두 팔기 (INBOX M22 ④) — 팔기 탭에서만 보이는 켬/끔, 켠 뒤 줄 탭 = {itemId, count: 가진 개수}, 코인은 재조회로만.
	const UWidget* const SellAll = Screen->WidgetTree->FindWidget(TEXT("SellAllButton"));
	TestTrue(TEXT("사기 탭에서는 「모두 팔기」 가 안 보인다"), SellAll != nullptr && !SellAll->IsVisible());
	Screen->SetSellTab(true);
	TestTrue(TEXT("팔기 탭에서는 「모두 팔기」 가 보인다"), SellAll != nullptr && SellAll->IsVisible());
	Screen->SetSellAllMode(true);
	UButton* const FirstSellLine = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("ShopLineButton_0")));
	TestNotNull(TEXT("팔기 첫 줄 버튼"), FirstSellLine);
	if (FirstSellLine != nullptr)
	{
		FirstSellLine->OnClicked.Broadcast();   // 사람이 줄을 누른 것과 같은 길
	}
	if (TestEqual(TEXT("서버를 세 번째로 불렀다"), Calls.Num(), 3))
	{
		AddInfo(FString::Printf(TEXT("모두 팔기 요청: %s itemId=%s count=%d"), *Calls[2].FunctionName, *Calls[2].ItemId, Calls[2].Count));
		TestEqual(TEXT("모두 팔기 함수 이름"), Calls[2].FunctionName, FString(TEXT("SellItem")));
		TestEqual(TEXT("모두 팔기 itemId"), Calls[2].ItemId, Sellable.ToString());
		TestEqual(TEXT("모두 팔기 count 는 가진 개수"), Calls[2].Count, TestSellableCount);
		TestFalse(TEXT("답 전에는 모두 팔기를 또 안 띄운다"), Screen->RequestSellAll(Sellable));

		const TSharedPtr<FJsonObject> SellReply = MakeShared<FJsonObject>();
		SellReply->SetStringField(TEXT("sold"), Sellable.ToString());
		SellReply->SetNumberField(TEXT("count"), TestSellableCount);
		SellReply->SetNumberField(TEXT("coins"), TestSellableCount);
		Calls[2].OnDone(true, SellReply, FString());
		AddInfo(FString::Printf(TEXT("모두 팔기 문구 [%s]"), *Screen->GetMessage().ToString()));
		TestTrue(TEXT("모두 팔기 성공 문구에 개수"), Screen->GetMessage().ToString().Contains(FString::Printf(TEXT("x%d"), TestSellableCount)));
		TestEqual(TEXT("모두 팔기 답만으로 코인이 안 바뀐다"), Backend->GetPlayerSnapshot().CoinBalance, TestCoinAfter);
	}
	TestFalse(TEXT("팔기 목록에 없는 것은 모두 팔기를 안 띄운다"), Screen->RequestSellAll(Unsellable));
	TestEqual(TEXT("목록 밖 모두 팔기는 서버를 안 부른다"), Calls.Num(), 3);
	Screen->SetSellAllMode(false);
	if (UButton* const Line = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("ShopLineButton_0"))))
	{
		Line->OnClicked.Broadcast();   // 줄은 화면을 고칠 때마다 다시 서니 새로 찾는다
	}
	if (TestEqual(TEXT("서버를 네 번째로 불렀다"), Calls.Num(), 4))
	{
		TestEqual(TEXT("모두 팔기를 끄면 줄 탭은 다시 하나"), Calls[3].Count, 1);
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopVendorRangeTest, "Snarehaven.Shop.Vendor.OpensOnlyInRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopVendorRangeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AShopVendorActor* const Vendor = World->SpawnActor<AShopVendorActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	const UZoneTravelComponent* const Travel = Player != nullptr ? Player->GetZoneTravel() : nullptr;
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("상인"), Vendor) || !TestNotNull(TEXT("이동 컴포넌트"), Travel))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 상인 대화 거리는 존 이동 거리(150)와 따로다 (M22 · DESIGN 3.1 = 250) — 검사에서는 검사 값을 먹인다.
	Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
	constexpr float TestTalkRangeCm = 250.f;
	Player->GetMeleeAttack()->SetNpcTalkRangeCm(TestTalkRangeCm);
	const float RangeCm = Player->GetMeleeAttack()->GetNpcTalkRangeCm();
	AddInfo(FString::Printf(TEXT("상인 대화 거리 %.0f cm · 존 이동 거리 %.0f cm"), RangeCm, Travel->GetTravelRangeCm()));
	TestEqual(TEXT("상인 거리는 대화 거리 칸을 쓴다"), RangeCm, TestTalkRangeCm);

	// 노점에 막혀 멈추는 184cm 는 대화 거리 안, 존 이동 거리 밖이다.
	Vendor->SetActorLocation(FVector(184.f, 0.f, 0.f));
	TestTrue(TEXT("노점 앞 184cm 에서 상인이 열린다"), Vendor->UpdateProximity(Player));
	Player->GetMeleeAttack()->SetNpcTalkRangeCm(-1.f);
	TestFalse(TEXT("대화 거리 미정이면 상인이 안 열린다"), Vendor->UpdateProximity(Player));
	Player->GetMeleeAttack()->SetNpcTalkRangeCm(TestTalkRangeCm);
	Vendor->SetActorLocation(FVector(RangeCm * 0.5f, 0.f, 0.f));
	TestTrue(TEXT("거리 안이면 상인 앞이다"), Vendor->UpdateProximity(Player));
	Vendor->SetActorLocation(FVector(RangeCm * 2.f, 0.f, 0.f));
	TestFalse(TEXT("거리 밖이면 상인 앞이 아니다"), Vendor->UpdateProximity(Player));
	TestFalse(TEXT("폰이 없으면 상인 앞이 아니다"), Vendor->UpdateProximity(nullptr));
	TestFalse(TEXT("상인 이름이 비지 않았다"), Vendor->GetVendorName().IsEmpty());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStarterCoinsTest, "Snarehaven.StarterKit.Coins.ShownFromSnapshotOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 시작 코인 10 은 서버 `GrantStarterKit` 이 넣는다. 응답에 `coins:10` 가 실려 와도 클라이언트는 그 수를
// 캐시에 더하지 않는다 — 재조회한 스냅샷이 10 이라고 말할 때만 `코인 10` 가 뜬다.
bool FSnarehavenStarterCoinsTest::RunTest(const FString& Parameters)
{
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	FSnarePlayerSnapshot Empty;
	Empty.PlayFabId = TEXT("PFTESTOWNER");
	Empty.FetchedAtUtc = FDateTime::UtcNow();
	Backend->ApplyPlayerSnapshot(Empty, /*bFromBackend=*/true);

	// 서버 응답 모양 그대로 (`handlers.GrantStarterKit` 의 return).
	const FString Granted = TEXT("{\"granted\":true,\"alreadyGranted\":false,\"grantedAtUtc\":\"2026-09-24T00:00:00.000Z\",")
		TEXT("\"items\":[{\"itemId\":\"item.test_starter\",\"count\":1}],\"coins\":10}");
	TestTrue(TEXT("지급 응답을 읽었다"), Backend->ApplyStarterKitResponseJson(Granted));
	AddInfo(FString::Printf(TEXT("지급 응답 직후 캐시 코인 %d"), Backend->GetPlayerSnapshot().CoinBalance));
	TestEqual(TEXT("응답의 coins 는 캐시에 더하지 않는다"), Backend->GetPlayerSnapshot().CoinBalance, 0);

	// 재조회가 서버 잔액 10 을 가져왔다 (검사에서는 스냅샷을 직접 넣는다).
	FSnarePlayerSnapshot Fetched = Empty;
	Fetched.CoinBalance = 10;
	Backend->ApplyPlayerSnapshot(Fetched, /*bFromBackend=*/true);

	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	if (TestNotNull(TEXT("상점 화면 위젯"), Screen))
	{
		Screen->BindToBackend(Backend);
		AddInfo(FString::Printf(TEXT("스냅샷 뒤 코인 [%s]"), *Screen->GetCoinText().ToString().Replace(TEXT("\n"), TEXT(" "))));
		TestTrue(TEXT("스냅샷 뒤 `코인 10` 이 뜬다"), Screen->GetCoinText().ToString().Contains(TEXT("코인 10")));
	}
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopMountExchangeTest, "Snarehaven.Shop.Mount.ExchangeWaitsForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopMountExchangeTest::RunTest(const FString& Parameters)
{
	// DESIGN 8.3 · 결정 009: 목줄을 가졌으면 사기 탭 끝에 "목줄 > 안 가진 종" 줄. 누르면 `ExchangeCollar {collarItemId, speciesId}`,
	// 답 전에는 탈것 칸 그대로, 답의 `mounts` 가 오면 그때 한 종이 는다 (CLAUDE.md 8).
	const FName Collar(TEXT("item.collar_hunting"));
	const FName Wolf(TEXT("animal.wolf"));
	const FName Fox(TEXT("animal.fox"));
	auto MountsReply = [](const TArray<FString>& Mounts, const TArray<FString>& Species)
	{
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> MountValues;
		for (const FString& ID : Mounts) { MountValues.Add(MakeShared<FJsonValueString>(ID)); }
		TArray<TSharedPtr<FJsonValue>> SpeciesValues;
		for (const FString& ID : Species) { SpeciesValues.Add(MakeShared<FJsonValueString>(ID)); }
		Reply->SetArrayField(TEXT("mounts"), MountValues);
		Reply->SetObjectField(TEXT("saddles"), MakeShared<FJsonObject>());
		Reply->SetArrayField(TEXT("species"), SpeciesValues);
		return Reply;
	};

	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상점 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.AddItem(Collar, 1);
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	TestTrue(TEXT("GetMounts 답 반영"), Backend->ApplyMountsReply(MountsReply({}, { Wolf.ToString(), Fox.ToString() })));
	Screen->BindToBackend(Backend);

	TArray<FShopLineView> MountLines = Screen->BuildBuyLines().FilterByPredicate(
		[](const FShopLineView& Line) { return !Line.MountSpecies.IsNone(); });
	TestEqual(TEXT("목줄 1개 · 안 가진 종 2 → 탈것 줄 2"), MountLines.Num(), 2);
	for (const FShopLineView& Line : MountLines)
	{
		const FString Text = Screen->GetLineText(Line, false).ToString();
		AddInfo(FString::Printf(TEXT("탈것 줄 [%s]"), *Text));
		TestFalse(TEXT("탈것 줄에 날 ID 가 안 뜬다"), Text.Contains(TEXT("item.")) || Text.Contains(TEXT("animal.")));
	}

	TArray<FString> Names;
	TArray<TSharedPtr<FJsonObject>> Args;
	TArray<USnareBackendSubsystem::FServerCallDone> Replies;
	USnareBackendSubsystem::TestServerOverride =
		[&Names, &Args, &Replies](const FString& FunctionName, const TSharedPtr<FJsonObject>& InArgs, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			Names.Add(FunctionName);
			Args.Add(InArgs);
			Replies.Add(MoveTemp(OnDone));
		};
	TestTrue(TEXT("교환 요청을 띄운다"), Screen->RequestMountTrade(Collar, Wolf));
	if (TestEqual(TEXT("서버를 한 번 불렀다"), Names.Num(), 1))
	{
		TestEqual(TEXT("함수 = ExchangeCollar"), Names[0], FString(TEXT("ExchangeCollar")));
		TestEqual(TEXT("collarItemId"), Args[0]->GetStringField(TEXT("collarItemId")), Collar.ToString());
		TestEqual(TEXT("speciesId"), Args[0]->GetStringField(TEXT("speciesId")), Wolf.ToString());
		TestEqual(TEXT("답 전에는 탈것 0종 그대로"), Backend->GetPlayerSnapshot().Mounts.Num(), 0);
		Replies[0](true, MountsReply({ Wolf.ToString() }, { Wolf.ToString(), Fox.ToString() }), FString());
		TestEqual(TEXT("답 뒤 탈것 1종"), Backend->GetPlayerSnapshot().Mounts.Num(), 1);
		TestTrue(TEXT("답 뒤 가진 종 = 늑대"), Backend->GetPlayerSnapshot().Mounts.Contains(Wolf));
		const FInventorySlot* const Held = Backend->GetPlayerSnapshot().InventorySlots.FindByPredicate(
			[Collar](const FInventorySlot& Slot) { return Slot.ItemID == Collar; });
		TestTrue(TEXT("목줄 개수는 화면이 안 뺀다(재조회 몫)"), Held && Held->Count == 1);
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopSheetTest, "Snarehaven.Shop.Tree.MatchesReferenceSheet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-18 — 레퍼런스 시트 `docs/reference/shop_vendor.md` 의 「검사 항목(3층)」 다섯 줄을 2층 숫자로 옮긴다.
// 눈으로 캡처를 대조하던 항목이라, 한 번 맞춰 놓아도 다음 조각이 판을 옮기면 조용히 어긋난다
// (배낭 판이 실제로 그렇게 어긋나 있었다 — M15-17).
// 재는 자리는 M15-17 과 같다: **화면 자리**(다른 창과 안 겹치나)는 `GetRenderBoundingRect`(판을 키우는
// 렌더 변환까지 들어간 자리), **시트 px**(원판 52·제목 탭 20·구분선 4)는 `GetLayoutBoundingRect`.
bool FSnarehavenShopSheetTest::RunTest(const FString& Parameters)
{
	// 시트가 적은 값 (검사 전용 사본 — 게임 규칙 숫자가 아니다).
	constexpr float SheetDiscSize = 52.f;
	constexpr float SheetDividerWidth = 4.f;
	constexpr float SheetPillRise = 20.f;
	constexpr float SheetPanelWidth = 600.f;
	// 시트는 600 이지만 판 아래 끝이 왼쪽 아래 체력 알약(시트 y 990~1040)을 덮어 540 으로 줄였다 (M82-04).
	constexpr float SheetPanelHeight = 540.f;
	constexpr float Tolerance = 0.5f;

	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}
	FName Sellable;
	FName Unsellable;
	PickSellableItems(*ItemTable, Sellable, Unsellable);
	if (!TestFalse(TEXT("파는 값이 있는 아이템이 표에 있다"), Sellable.IsNone()))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeSnapshot(TestCoinBefore, Sellable, Unsellable), false);

	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	UBackpackScreenWidget* const Backpack = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상점 화면 위젯"), Screen) || !TestNotNull(TEXT("배낭 화면 위젯"), Backpack))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	const TSharedRef<SWidget> BackpackSlate = Backpack->TakeWidget();
	Backpack->BindTargets(Backend, nullptr, nullptr);
	Screen->BindToBackend(Backend);
	Screen->SetVendorName(FText::FromString(TEXT("잡화상 도토리")));
	Screen->SetVendorZone(FText::FromString(TEXT("기지")));
	Screen->BindBackpackScreen(Backpack);
	Screen->SetOpen(true);

	// ── 재는 도구: 배치된 기하를 걸어 담는다 (위젯 이름이 아니라 **실제로 앉은 자리**로 판정한다) ──
	TMap<TSharedRef<SWidget>, FArrangedWidget> Arranged;
	FVector2D ViewSize(1920.f, 1080.f);
	auto MeasureInto = [&Arranged, &ViewSize](const TSharedRef<SWidget>& Root)
	{
		Root->SlatePrepass(1.f);
		TFunction<void(const FArrangedWidget&)> Walk = [&Walk, &Arranged](const FArrangedWidget& Node)
		{
			Arranged.Add(Node.Widget, Node);
			FArrangedChildren Children(EVisibility::Visible);
			Node.Widget->ArrangeChildren(Node.Geometry, Children);
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				Walk(Children[Index]);
			}
		};
		Walk(FArrangedWidget(Root, FGeometry::MakeRoot(ViewSize, FSlateLayoutTransform())));
	};
	auto Measure = [&Arranged, &MeasureInto, &Slate, &BackpackSlate]()
	{
		Arranged.Reset();
		MeasureInto(Slate);
		MeasureInto(BackpackSlate);
	};
	auto NodeOf = [&Arranged](const UUserWidget& Owner, const TCHAR* Name) -> const FArrangedWidget*
	{
		const UWidget* const Widget = Owner.WidgetTree->FindWidget(Name);
		return Widget != nullptr && Widget->GetCachedWidget().IsValid()
			? Arranged.Find(Widget->GetCachedWidget().ToSharedRef()) : nullptr;
	};
	auto ScreenRect = [&NodeOf](const UUserWidget& Owner, const TCHAR* Name, FSlateRect& Out)
	{
		const FArrangedWidget* const Node = NodeOf(Owner, Name);
		if (Node != nullptr)
		{
			Out = Node->Geometry.GetRenderBoundingRect();
		}
		return Node != nullptr;
	};
	// 상점 판은 폭이 화면에 따라 늘었다 줄었다 해서 **렌더 변환 대신 값마다 배율**이 먹어 있다
	// (배낭과 다른 점 — M15-17 은 판 전체에 1.6배 렌더 변환이라 레이아웃 값이 곧 시트 px 였다).
	// 그래서 시트 px 와 대조하려면 잰 값을 그 배율로 되돌린다. 배율은 위젯이 쓰는 값을 그대로 읽는다.
	const float Scale = UShopScreenWidget::GetPhoneScale();
	auto SheetRect = [&NodeOf, Scale](const UUserWidget& Owner, const TCHAR* Name, FSlateRect& Out)
	{
		const FArrangedWidget* const Node = NodeOf(Owner, Name);
		if (Node != nullptr)
		{
			// 네 변을 모두 배율로 나눈다 — `ScaleBy` 는 가운데를 축으로 **넓히는** 함수라 여기 쓸 것이 아니다.
			const FSlateRect Laid = Node->Geometry.GetLayoutBoundingRect();
			Out = FSlateRect(Laid.Left / Scale, Laid.Top / Scale, Laid.Right / Scale, Laid.Bottom / Scale);
		}
		return Node != nullptr;
	};
	Measure();

	// ── ① 판이 위아래 두 칸 — 가운데 가로 밧줄선(4px) 위가 거래 목록, 아래가 NPC 명함 (M100-01) ──
	FSlateRect Panel;
	FSlateRect Divider;
	FSlateRect List;
	FSlateRect Card;
	const bool bTwoColumns = SheetRect(*Screen, TEXT("ShopPanel"), Panel)
		&& SheetRect(*Screen, TEXT("ColumnDividerBox"), Divider)
		&& SheetRect(*Screen, TEXT("ShopLineScroll"), List)   // 줄이 많으면 LineList 는 스크롤 안에서 길어져 선반을 넘는다
		&& SheetRect(*Screen, TEXT("VendorCard"), Card);
	if (TestTrue(TEXT("두 칸 판 배치를 읽었다"), bTwoColumns))
	{
		AddInfo(FString::Printf(TEXT("[Shop] 판 %.0fx%.0f · 구분선 두께 %.1f (세로 %.0f) · 목록 아래 %.0f · 명함 위 %.0f"),
			Panel.GetSize().X, Panel.GetSize().Y, Divider.GetSize().Y, Divider.Top, List.Bottom, Card.Top));
		TestEqual(TEXT("판 크기 = 시트 600x600"), Panel.GetSize().X, SheetPanelWidth, Tolerance);
		TestEqual(TEXT("판 높이 = 시트 600 에서 체력 알약 자리만큼 줄인 540"), Panel.GetSize().Y, SheetPanelHeight, Tolerance);
		TestEqual(TEXT("구분선 두께 = 4px"), Divider.GetSize().Y, SheetDividerWidth, Tolerance);
		TestTrue(TEXT("거래 목록이 구분선 위"), List.Bottom <= Divider.Top + Tolerance);
		TestTrue(TEXT("NPC 명함이 구분선 아래"), Card.Top >= Divider.Bottom - Tolerance);
		TestTrue(TEXT("명함이 목록 바로 아래라 판 폭을 같이 쓴다(옆 칸 아님)"), Card.Left < List.Right - Tolerance);
		// 감독 캡처 불합격(M100-01 보정): 목록 한눈에 3.5줄 이상, 명함 끝이 닫기 위 끝보다 12px 위.
		FSlateRect Close;
		const float Stride = SnareUiStyle::MenuTouchMin / 1.6f + 8.f;   // 줄 높이(10-16 누르는 칸 121) + 틈, 시트 단위
		// 10-16: 줄이 누르는 칸 121 로 커져 같은 판에 3.3줄 — 끝 줄이 반쯤 잘려 보여 「밀면 더 있다」 는 신호는 그대로다.
		TestTrue(TEXT("목록이 한눈에 3.25줄 이상"), List.GetSize().Y / Stride >= 3.25f);
		if (TestTrue(TEXT("닫기 배치"), SheetRect(*Screen, TEXT("ShopCloseBox"), Close)))
		{
			// 10-16: 닫기는 탭 줄 오른쪽 끝 — 목록보다 위, 누르는 칸 121(시트 75.6).
			AddInfo(FString::Printf(TEXT("[Shop] 목록 %.1f줄 · 닫기 위 %.0f · 목록 위 %.0f"), List.GetSize().Y / Stride, Close.Top, List.Top));
			TestTrue(TEXT("닫기가 탭 줄(목록 위)에 있다"), Close.Bottom <= List.Top + Tolerance);
			TestTrue(TEXT("닫기 높이 ≥ 누르는 칸 121"), Close.GetSize().Y >= SnareUiStyle::MenuTouchMin / 1.6f - Tolerance);
		}
	}

	// ── ② 한 줄은 사기·팔기 모두 「원판·이름 왼쪽 | 가격 오른쪽 끝」 (10-16) ──
	auto RowOrder = [&](bool bSellTab, const TCHAR* What)
	{
		Screen->SetSellTab(bSellTab);
		Measure();
		FSlateRect Price;
		FSlateRect Disc;
		FSlateRect Row;
		if (!TestTrue(*FString::Printf(TEXT("%s 첫 줄 배치"), What),
			SheetRect(*Screen, TEXT("ShopRowPrice_0"), Price) && SheetRect(*Screen, TEXT("ShopRowDiscBox_0"), Disc)
			&& SheetRect(*Screen, TEXT("ShopRow_0"), Row)))
		{
			return;
		}
		AddInfo(FString::Printf(TEXT("[Shop] %s 첫 줄 - 원판 %.0f · 가격 %.0f~%.0f · 줄 오른쪽 %.0f (원판 %.0fx%.0f)"),
			What, Disc.Left, Price.Left, Price.Right, Row.Right, Disc.GetSize().X, Disc.GetSize().Y));
		TestTrue(*FString::Printf(TEXT("%s 줄은 원판이 가격보다 왼쪽"), What), Disc.Right <= Price.Left + Tolerance);
		TestTrue(*FString::Printf(TEXT("%s 줄 가격은 줄 오른쪽 절반"), What), Price.Left >= (Row.Left + Row.Right) * 0.5f);
		TestEqual(TEXT("아이템 원판 = 시트 52px"), Disc.GetSize().X, SheetDiscSize, Tolerance);
		TestEqual(TEXT("아이템 원판이 정사각"), Disc.GetSize().Y, SheetDiscSize, Tolerance);
	};
	RowOrder(false, TEXT("사기"));
	RowOrder(true, TEXT("팔기"));

	// 칸은 색 동그라미가 아니라 둥근 네모(아래 등급 띠)이고 색은 코드 상수가 아니라 표의 등급에서 온다 · 가격 글자는 시트 초록.
	Screen->SetSellTab(false);
	Measure();
	const UBorder* const Disc = Cast<UBorder>(Screen->WidgetTree->FindWidget(TEXT("ShopRowDisc_0")));
	const UTextBlock* const PriceText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("ShopRowPrice_0")));
	const TArray<FShopLineView> BuyLines = Screen->BuildBuyLines();
	if (TestNotNull(TEXT("첫 줄 원판"), Disc) && TestNotNull(TEXT("첫 줄 가격 글"), PriceText)
		&& TestTrue(TEXT("사기 줄이 있다"), BuyLines.Num() > 0))
	{
		const FItemRow* const Row = ItemTable->FindRow<FItemRow>(
			*BuyLines[0].ItemID.ToString().Replace(TEXT("item."), TEXT("")), TEXT("ShopSheetTest"), false);
		const FVector4 Radii = Disc->Background.OutlineSettings.CornerRadii;
		const FLinearColor Tint = Disc->Background.TintColor.GetSpecifiedColor();
		const FLinearColor Green = SnareUiStyle::UiShopPriceGreen();
		const FLinearColor Shown = PriceText->GetColorAndOpacity().GetSpecifiedColor();
		AddInfo(FString::Printf(TEXT("[Shop] 첫 줄 %s - 원판 모서리 %.0f(시트 %.0f) · 색 %s · 가격 글 색 %s (시트 초록 %s)"),
			*BuyLines[0].ItemID.ToString(), Radii.X, Radii.X / Scale, *Tint.ToFColor(true).ToHex(),
			*Shown.ToFColor(true).ToHex(), *Green.ToFColor(true).ToHex()));
		TestTrue(TEXT("칸 모서리는 원이 아니다(M101-04)"), static_cast<float>(Radii.X) < SheetDiscSize * Scale * 0.25f);
		// M97-c-5: 나무 줄 위라 초록 → 크림 글 + 진갈색 외곽선(감독 c-1 검수 「초록 가격이 안 읽힌다」).
		TestEqual(TEXT("가격 글이 크림(나무 위)"), Shown.ToFColor(true).ToHex(), SnareUiStyle::UiPanelCream().ToFColor(true).ToHex());
		TestTrue(TEXT("가격 글에 외곽선"), PriceText->GetFont().OutlineSettings.OutlineSize > 0);
		const UBorder* const Band = Cast<UBorder>(Screen->WidgetTree->FindWidget(TEXT("ShopRowGrade_0")));
		if (TestNotNull(TEXT("첫 줄 등급 띠"), Band) && Row != nullptr)
		{
			TestEqual(TEXT("띠 색이 표의 등급 띠 색"), Band->Background.TintColor.GetSpecifiedColor().ToFColor(true).ToHex(),
				SnareUiStyle::ItemGradeBandColor(Row->Grade).ToFColor(true).ToHex());
		}
	}

	// ── ③ 오른쪽 칸 = 이름 알약(빨강) + 편지체 본문 ──
	const UBorder* const NamePill = Cast<UBorder>(Screen->WidgetTree->FindWidget(TEXT("VendorNamePill")));
	const UTextBlock* const Letter = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("VendorLetterText")));
	FSlateRect PillRect;
	FSlateRect LetterRect;
	if (TestNotNull(TEXT("이름 알약"), NamePill) && TestNotNull(TEXT("명함 본문"), Letter)
		&& TestTrue(TEXT("명함 배치"), SheetRect(*Screen, TEXT("VendorNamePillBox"), PillRect)
			&& SheetRect(*Screen, TEXT("VendorLetterText"), LetterRect)))
	{
		const FLinearColor PillTint = NamePill->Background.TintColor.GetSpecifiedColor();
		const FString Body = Letter->GetText().ToString();
		AddInfo(FString::Printf(TEXT("[Shop] 명함 - 알약 색 %s (시트 빨강 %s) · 본문 %d자 · 첫 줄 '%s'"),
			*PillTint.ToFColor(true).ToHex(), *SnareUiStyle::UiAccentRed().ToFColor(true).ToHex(),
			Body.Len(), *Body.Left(Body.Find(TEXT("\n")) > 0 ? Body.Find(TEXT("\n")) : Body.Len())));
		TestEqual(TEXT("이름 알약이 나무 표찰색(M97-c-5)"), PillTint.ToFColor(true).ToHex(),
			SnareUiStyle::UiWoodButton().ToFColor(true).ToHex());   // M99-06: 밝은 나무 알약
		TestTrue(TEXT("본문이 비어 있지 않다"), Body.Len() > 0);
		TestFalse(TEXT("서명 줄 없음(이름은 알약이 말한다)"), Body.Contains(TEXT("\n- ")));
		TestTrue(TEXT("본문이 이름 알약 아래"), LetterRect.Top >= PillRect.Bottom - Tolerance);
		// 가게마다 다른 글이라야 "누구의 명함" 이 된다 — 넷이 다 같으면 붙여도 뜻이 없다.
		TSet<FString> Letters;
		for (const EShopKind Kind : {EShopKind::General, EShopKind::Fishing, EShopKind::Farming, EShopKind::Clothes})
		{
			Letters.Add(UShopScreenWidget::GetVendorLetter(Kind).ToString());
		}
		TestEqual(TEXT("가게 넷의 명함 글이 서로 다르다"), Letters.Num(), 4);
	}

	// ── ④ 제목 탭이 판 위로 걸침 (배낭과 같은 규격, 색만 초록) ──
	FSlateRect Pill;
	if (TestTrue(TEXT("제목 탭 배치"), SheetRect(*Screen, TEXT("TitlePill"), Pill)))
	{
		const UBorder* const TitlePill = Cast<UBorder>(Screen->WidgetTree->FindWidget(TEXT("TitlePill")));
		AddInfo(FString::Printf(TEXT("[Shop] 제목 탭 %.0fx%.0f · 판 위로 %.1f px · 왼쪽 차 %.1f px · 색 %s"),
			Pill.GetSize().X, Pill.GetSize().Y, Panel.Top - Pill.Top, Pill.Left - Panel.Left,
			TitlePill != nullptr ? *TitlePill->Background.TintColor.GetSpecifiedColor().ToFColor(true).ToHex() : TEXT("-")));
		TestEqual(TEXT("제목 탭이 판보다 시트만큼 위로"), Panel.Top - Pill.Top, SheetPillRise, Tolerance);
		TestTrue(TEXT("제목 탭이 판에 걸쳐 있다(아래가 판 안)"), Pill.Bottom > Panel.Top + Tolerance);
		TestEqual(TEXT("제목 탭이 판 왼쪽 위 모서리에"), Pill.Left, Panel.Left, Tolerance);
	}

	// ── ⑤ 배낭이 오른쪽에 같이 열린다 — 겹치지 않아야 둘 다 쓸 수 있다 (4:3·폰 비율 둘 다) ──
	TestTrue(TEXT("상점을 열면 배낭도 열린다"), Backpack->IsPanelOpen());
	// 1728x1117 은 맥 `-game` 창 기본 크기다 — 3층 캡처가 이 크기로 찍히고, **거기서 판 왼쪽이 잘렸다**
	// (가격 칸·제목 탭이 화면 밖, 이름이 `손도ㄲ` 로 잘림). 시트(1920)보다 좁은 화면을 여기서 같이 본다.
	for (const FVector2D View : {FVector2D(1920.f, 1080.f), FVector2D(2340.f, 1080.f), FVector2D(1728.f, 1117.f)})
	{
		ViewSize = View;
		Measure();
		FSlateRect ShopScreenRect;
		FSlateRect BackpackRect;
		if (!TestTrue(TEXT("두 판의 화면 자리"), ScreenRect(*Screen, TEXT("ShopPanel"), ShopScreenRect)
			&& ScreenRect(*Backpack, TEXT("BackpackPanel"), BackpackRect)))
		{
			continue;
		}
		AddInfo(FString::Printf(TEXT("[Shop] %.0f 폭 - 상점 판 %.0f~%.0f (%.1f%%~%.1f%%) · 배낭 판 %.0f~%.0f · 틈 %.0f"),
			View.X, ShopScreenRect.Left, ShopScreenRect.Right,
			ShopScreenRect.Left / View.X * 100.f, ShopScreenRect.Right / View.X * 100.f,
			BackpackRect.Left, BackpackRect.Right, BackpackRect.Left - ShopScreenRect.Right));
		TestTrue(TEXT("상점 판이 배낭 판 왼쪽에 겹치지 않고 선다"), ShopScreenRect.Right <= BackpackRect.Left + Tolerance);
		TestTrue(TEXT("상점 판이 화면 안에 다 든다"), ShopScreenRect.Left >= -Tolerance && ShopScreenRect.Bottom <= View.Y + Tolerance);
		// 판 안의 것도 같이 본다 — 판만 화면 안이고 제목 탭·가격 글이 밖으로 나가면 캡처에서 잘려 보인다.
		FSlateRect PillScreenRect;
		FSlateRect PriceScreenRect;
		if (ScreenRect(*Screen, TEXT("TitlePill"), PillScreenRect)
			&& ScreenRect(*Screen, TEXT("ShopRowPrice_0"), PriceScreenRect))
		{
			AddInfo(FString::Printf(TEXT("[Shop] %.0f 폭 - 제목 탭 왼쪽 %.0f · 첫 줄 가격 왼쪽 %.0f"),
				View.X, PillScreenRect.Left, PriceScreenRect.Left));
			TestTrue(TEXT("제목 탭이 화면 안"), PillScreenRect.Left >= -Tolerance && PillScreenRect.Top >= -Tolerance);
			TestTrue(TEXT("첫 줄 가격 글이 화면 안"), PriceScreenRect.Left >= -Tolerance);
		}
		// 이름이 제 폭을 못 받으면 글자가 잘린다(캡처에서 `손도ㄲ`) — 받은 폭과 필요한 폭을 같이 적는다.
		if (const FArrangedWidget* const NameNode = NodeOf(*Screen, TEXT("ShopRowName_0")))
		{
			const float Given = NameNode->Geometry.GetLocalSize().X;
			const float Wanted = NameNode->Widget->GetDesiredSize().X;
			FSlateRect RowRect;
			FSlateRect ListRect;
			ScreenRect(*Screen, TEXT("ShopRow_0"), RowRect);
			// 보이는 경계는 목록 세로칸이 아니라 **스크롤 박스**다 — 세로칸은 줄에게 제가 바라는 폭을 그대로
			// 주고(그래서 "받은 폭 = 필요한 폭" 이 된다), 넘친 부분은 스크롤 박스가 잘라 그린다.
			ScreenRect(*Screen, TEXT("ShopLineScroll"), ListRect);
			AddInfo(FString::Printf(TEXT("[Shop] %.0f 폭 - 첫 줄 이름 받은 폭 %.0f · 필요한 폭 %.0f · 줄 오른쪽 %.0f · 목록 오른쪽 %.0f"),
				View.X, Given, Wanted, RowRect.Right, ListRect.Right));
			// 시트 폭(1920)부터는 이름이 온전히 들어가야 한다 — 폰(2340)이 더 넓어 실기기는 여기 든다.
			// 그보다 좁은 창에서는 판이 같이 좁아져 이름이 말줄임으로 끝난다(시트 줄에는 이름 자체가 없다).
			if (View.X >= 1920.f)
			{
				TestTrue(TEXT("첫 줄 이름이 온전히 들어간다"), Given >= Wanted - Tolerance);
				TestTrue(TEXT("첫 줄이 목록 칸 안에 든다"), RowRect.Right <= ListRect.Right + Tolerance);
			}
		}
	}
	// ── ⑥ M22 폰 결함 `손…` — 사기·팔기 **모든 줄** 이름이 앞 6글자(짧으면 전부)를 그릴 폭을 받는다 ──
	// 받은 폭이 글 폭과 딱 같으면 폰 DPI 배율에서 반 픽셀 모자라 말줄임이 걸린다 — 그래서 6글자 폭 + 여유 1글자.
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	constexpr int32 MinVisibleChars = 6;
	for (const FVector2D View : {FVector2D(1920.f, 1080.f), FVector2D(2340.f, 1080.f), FVector2D(1728.f, 1117.f)})
	{
		ViewSize = View;
		for (const bool bSellTab : {false, true})
		{
			Screen->SetSellTab(bSellTab);
			Measure();
			int32 Checked = 0;
			float WorstSlack = TNumericLimits<float>::Max();
			FString WorstName;
			for (int32 Index = 0;; ++Index)
			{
				const FString WidgetName = FString::Printf(TEXT("ShopRowName_%d"), Index);
				const UTextBlock* const NameText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(*WidgetName));
				const FArrangedWidget* const NameNode = NodeOf(*Screen, *WidgetName);
				if (NameText == nullptr || NameNode == nullptr)
				{
					break;
				}
				const FString Label = NameText->GetText().ToString();
				const FSlateFontInfo Font = NameText->GetFont();
				const float Need = FontMeasure->Measure(Label.Left(MinVisibleChars), Font).X
					+ (Label.Len() > MinVisibleChars ? FontMeasure->Measure(Label.Mid(MinVisibleChars, 1), Font).X : 0.f);
				const float Slack = NameNode->Geometry.GetLocalSize().X - Need;
				if (Slack < WorstSlack)
				{
					WorstSlack = Slack;
					WorstName = Label;
				}
				++Checked;
			}
			AddInfo(FString::Printf(TEXT("[Shop] %.0f 폭 %s - 줄 %d개 · 가장 빠듯한 이름 '%s' 여유 %.1f px"),
				View.X, bSellTab ? TEXT("팔기") : TEXT("사기"), Checked, *WorstName, WorstSlack));
			TestTrue(*FString::Printf(TEXT("%.0f 폭 %s 줄이 있다"), View.X, bSellTab ? TEXT("팔기") : TEXT("사기")), Checked > 0);
			// 시트 폭(1920)·폰(2340)만 강제한다. 맥 창(1728)은 판이 같이 좁아져 말줄임이 남는다 — 기록만.
			if (View.X >= 1920.f)
			{
				TestTrue(*FString::Printf(TEXT("%.0f 폭 %s 모든 줄 이름이 6글자 이상 보인다"), View.X, bSellTab ? TEXT("팔기") : TEXT("사기")),
					Checked == 0 || WorstSlack >= 0.f);
			}
			// 「모두 팔기」(INBOX M22 ④)는 팔기 탭 줄에 셋째로 선다 — 좁은 맥 창(1728)까지 글자가 칸에 다 드는지.
			if (bSellTab)
			{
				const UTextBlock* const AllText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("SellAllButtonText")));
				const FArrangedWidget* const AllNode = NodeOf(*Screen, TEXT("SellAllButtonText"));
				if (TestNotNull(TEXT("「모두 팔기」 글자"), AllText) && TestNotNull(TEXT("「모두 팔기」 글자 배치"), AllNode))
				{
					const float Need = FontMeasure->Measure(AllText->GetText().ToString(), AllText->GetFont()).X;
					const float Given = AllNode->Geometry.GetLocalSize().X;
					AddInfo(FString::Printf(TEXT("[Shop] %.0f 폭 「모두 팔기」 받은 폭 %.1f · 필요한 폭 %.1f"), View.X, Given, Need));
					TestTrue(*FString::Printf(TEXT("%.0f 폭 「모두 팔기」 글자가 칸에 든다"), View.X), Given >= Need - Tolerance);
				}
			}
		}
	}
	Screen->SetSellTab(false);
	ViewSize = FVector2D(1920.f, 1080.f);

	// 닫으면 배낭도 같이 접힌다 — 상점만 닫고 배낭이 남으면 화면에 주인 없는 판이 뜬다.
	Screen->SetOpen(false);
	TestFalse(TEXT("상점을 닫으면 배낭도 닫힌다"), Backpack->IsPanelOpen());

	Screen->BindToBackend(nullptr);
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopOpenCardBesideTargetTest, "Snarehaven.Shop.Screen.OpenCardBesideTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopOpenCardBesideTargetTest::RunTest(const FString& Parameters)
{
	// M23-02 — 열기 카드는 화면 가운데에 박힌 큰 네모가 아니라 대상 머리 옆 작은 카드다(시트 「상호작용 프롬프트」).
	// 투영은 화면이 있어야 해 여기선 투영된 자리를 테스트 값으로 넣고, 카드 자리·접힘만 잰다.
	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Shop = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	UQuestScreenWidget* const Quest = CreateWidget<UQuestScreenWidget>(World, UQuestScreenWidget::StaticClass());
	AShopVendorActor* const Vendor = World->SpawnActor<AShopVendorActor>(FVector(100.f, 200.f, 88.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("상점 화면"), Shop) || !TestNotNull(TEXT("의뢰 화면"), Quest) || !TestNotNull(TEXT("상인"), Vendor))
	{
		DestroyTestWorld(World);
		return false;
	}
	Shop->TakeWidget();
	Quest->TakeWidget();

	const FVector Head = Vendor->GetPromptWorldLocation();
	AddInfo(FString::Printf(TEXT("[열기카드] 상인 원점 z %.0f → 카드 대상 z %.0f"), Vendor->GetActorLocation().Z, Head.Z));
	TestEqual(TEXT("대상 점 = 캡슐 중심 + 반높이"), Head.Z - Vendor->GetActorLocation().Z, 88.0);

	const FVector2D Viewport(1920.f, 1080.f);
	const auto Check = [this, &Viewport](const TCHAR* Who, UUserWidget& Screen, const TCHAR* ButtonName,
		TFunctionRef<void(const FVector&)> SetTarget, TFunctionRef<bool(const FVector2D&)> Place, TFunctionRef<void(bool)> SetOpen, TFunctionRef<void(bool)> SetSuppressed)
	{
		const UWidget* const Button = Screen.WidgetTree->FindWidget(FName(ButtonName));
		const UCanvasPanelSlot* const Slot = Button != nullptr ? Cast<UCanvasPanelSlot>(Button->Slot) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s 열기 카드 슬롯"), Who), Slot))
		{
			return;
		}
		const auto Visible = [Button]() { return Button->GetVisibility() == ESlateVisibility::Visible; };
		// 안내 카드에 「!」 없음 · 행동 아이콘 있음 (결함 10-15 16:50).
		const UTextBlock* const Label = Cast<UTextBlock>(Screen.WidgetTree->FindWidget(FName(*(FString(ButtonName) + TEXT("Text")))));
		if (TestNotNull(*FString::Printf(TEXT("%s 카드 글자"), Who), Label))
		{
			TestFalse(*FString::Printf(TEXT("%s 카드 글자에 ! 없음"), Who), Label->GetText().ToString().Contains(TEXT("!")));
		}
		const UImage* const Icon = Cast<UImage>(Screen.WidgetTree->FindWidget(FName(*(FString(ButtonName) + TEXT("Icon")))));
		if (TestNotNull(*FString::Printf(TEXT("%s 카드 행동 아이콘"), Who), Icon) && Label != nullptr)
		{
			// 글꼴 크기는 pt — 화면 px 글자 높이 = pt × 96/72 (Slate 기준 DPI).
			const float TextHeightPx = Label->GetFont().Size * 96.f / 72.f;
			AddInfo(FString::Printf(TEXT("[열기카드] %s 아이콘 %.0f · 글자 높이 %.1f"), Who, Icon->GetBrush().ImageSize.Y, TextHeightPx));
			TestTrue(*FString::Printf(TEXT("%s 아이콘 크기 ≥ 글자 높이"), Who), Icon->GetBrush().ImageSize.Y >= TextHeightPx);
		}
		TestEqual(*FString::Printf(TEXT("%s 카드 크기"), Who), Slot->GetSize(),
			FVector2D(USnarePromptCardWidget::TapCardWidth, USnarePromptCardWidget::TapCardHeight));

		SetTarget(FVector::ZeroVector);
		TestFalse(*FString::Printf(TEXT("%s 대상을 받으면 첫 투영 전엔 접힘"), Who), Visible());

		TestTrue(*FString::Printf(TEXT("%s 화면 안 대상"), Who), Place(FVector2D(900.f, 400.f)));
		AddInfo(FString::Printf(TEXT("[열기카드] %s 대상 (900,400) → 카드 왼쪽 위 (%.0f,%.0f) 크기 %.0fx%.0f 보임 %d"),
			Who, Slot->GetPosition().X, Slot->GetPosition().Y, Slot->GetSize().X, Slot->GetSize().Y, Visible() ? 1 : 0));
		TestEqual(*FString::Printf(TEXT("%s 카드 = 대상 오른쪽 + 틈, 세로 가운데"), Who), Slot->GetPosition(),
			FVector2D(900.f + USnarePromptCardWidget::TapCardGapPx, 400.f - USnarePromptCardWidget::TapCardHeight * 0.5f));
		TestEqual(*FString::Printf(TEXT("%s 앵커 = 왼쪽 위"), Who), Slot->GetAnchors().Minimum, FVector2D::ZeroVector);
		TestTrue(*FString::Printf(TEXT("%s 보인다"), Who), Visible());

		TestTrue(*FString::Printf(TEXT("%s 오른쪽 끝 대상"), Who), Place(FVector2D(1800.f, 400.f)));
		AddInfo(FString::Printf(TEXT("[열기카드] %s 대상 (1800,400) → 카드 왼쪽 위 (%.0f,%.0f)"), Who, Slot->GetPosition().X, Slot->GetPosition().Y));
		TestEqual(*FString::Printf(TEXT("%s 넘치면 왼쪽으로"), Who), static_cast<double>(Slot->GetPosition().X),
			1800.0 - USnarePromptCardWidget::TapCardGapPx - USnarePromptCardWidget::TapCardWidth);

		TestFalse(*FString::Printf(TEXT("%s 화면 밖 대상"), Who), Place(FVector2D(2000.f, 400.f)));
		AddInfo(FString::Printf(TEXT("[열기카드] %s 대상 (2000,400) 화면 밖 → 보임 %d"), Who, Visible() ? 1 : 0));
		TestFalse(*FString::Printf(TEXT("%s 화면 밖이면 접힘"), Who), Visible());
		TestFalse(*FString::Printf(TEXT("%s 카메라 뒤(-1,-1)면 접힘"), Who), Place(FVector2D(-1.f, -1.f)) || Visible());

		Place(FVector2D(900.f, 400.f));
		SetOpen(true);
		TestFalse(*FString::Printf(TEXT("%s 판이 열리면 카드 접힘"), Who), Visible());
		SetOpen(false);
		TestTrue(*FString::Printf(TEXT("%s 판을 닫으면 카드 다시"), Who), Visible());
		SetSuppressed(true);
		TestFalse(*FString::Printf(TEXT("%s 다른 창이 열리면 카드 접힘"), Who), Visible());
		SetSuppressed(false);
		TestTrue(*FString::Printf(TEXT("%s 다른 창이 닫히면 카드 다시"), Who), Visible());
	};

	Check(TEXT("상점"), *Shop, TEXT("OpenButton"),
		[Shop](const FVector& V) { Shop->SetPromptTarget(V); },
		[Shop, &Viewport](const FVector2D& P) { return Shop->PlaceOpenButton(P, Viewport); },
		[Shop](bool b) { Shop->SetOpen(b); }, [Shop](bool b) { Shop->SetPromptSuppressed(b); });
	Check(TEXT("의뢰"), *Quest, TEXT("QuestOpenButton"),
		[Quest](const FVector& V) { Quest->SetPromptTarget(V); },
		[Quest, &Viewport](const FVector2D& P) { return Quest->PlaceOpenButton(P, Viewport); },
		[Quest](bool b) { Quest->SetOpen(b); }, [Quest](bool b) { Quest->SetPromptSuppressed(b); });

	DestroyTestWorld(World);
	return true;
}

// M27-03 — 상점 판 안 버튼(거래 줄·사기/팔기/모두 팔기 탭·닫기) 전부 네 상태 둥근 HudCornerRadius,
// 고른 탭 색·답 기다리는 동안 줄 잠금 그대로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopRoundButtonsTest, "Snarehaven.Shop.Screen.RoundButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopRoundButtonsTest::RunTest(const FString& Parameters)
{
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}
	FName Sellable;
	FName Unsellable;
	PickSellableItems(*ItemTable, Sellable, Unsellable);

	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상점 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	TArray<FRecordedCall> Calls;
	InstallRecordingServer(Calls);
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeSnapshot(TestCoinBefore, Sellable, Unsellable), /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	Screen->SetOpen(true);

	UWidgetTree* const Tree = Screen->WidgetTree;
	UBorder* const Panel = Tree->FindWidget<UBorder>(TEXT("ShopPanel"));
	if (!TestNotNull(TEXT("상점 판"), Panel))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		DestroyTestWorld(World);
		return false;
	}

	const FVector4 ButtonRadius(SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius,
		SnareUiStyle::HudCornerRadius);
	auto IsRound = [&ButtonRadius](const FSlateBrush& Brush)
	{
		return Brush.DrawAs == ESlateBrushDrawType::RoundedBox && Brush.OutlineSettings.CornerRadii == ButtonRadius;
	};
	// 판 안 버튼을 훑어 종류별로 센다 — 종류마다 하나 이상이어야 시험이 그 길을 본 것이다.
	struct FCount
	{
		int32 Lines = 0;
		int32 LockedLines = 0;
		int32 Tabs = 0;
		int32 Close = 0;
		int32 Round = 0;
		int32 All = 0;
	};
	auto Scan = [&](const TCHAR* Stage)
	{
		FCount Count;
		Tree->ForEachWidget([&](UWidget* Widget)
		{
			const UButton* const Button = Cast<UButton>(Widget);
			bool bInPanel = false;
			for (const UWidget* Walk = Button; Walk != nullptr; Walk = Walk->GetParent())
			{
				bInPanel |= Walk == Panel;
			}
			if (Button == nullptr || !bInPanel)
			{
				return;
			}
			const FString Name = Button->GetName();
			const bool bLine = Name.StartsWith(TEXT("ShopLineButton_"));
			Count.Lines += bLine ? 1 : 0;
			Count.LockedLines += bLine && !Button->GetIsEnabled() ? 1 : 0;
			Count.Tabs += Name == TEXT("BuyTabButton") || Name == TEXT("SellTabButton") || Name == TEXT("SellAllButton") ? 1 : 0;
			Count.Close += Name == TEXT("CloseButton") ? 1 : 0;
			const FButtonStyle& Style = Button->GetStyle();
			const bool bRound = IsRound(Style.Normal) && IsRound(Style.Hovered) && IsRound(Style.Pressed) && IsRound(Style.Disabled);
			TestTrue(FString::Printf(TEXT("%s %s 네 상태 둥근 네모 · 반지름 = HudCornerRadius"), Stage, *Name), bRound);
			Count.Round += bRound ? 1 : 0;
			++Count.All;
		});
		return Count;
	};
	auto TintOf = [Tree](const TCHAR* Name)
	{
		const UButton* const Button = Tree->FindWidget<UButton>(Name);
		return Button != nullptr ? Button->GetStyle().Normal.TintColor.GetSpecifiedColor() : FLinearColor::Transparent;
	};

	// ① 사기 탭 — 거래 줄·탭 셋·닫기 전부 둥근, 고른 탭 = 강조색.
	const FCount Buy = Scan(TEXT("사기"));
	TestTrue(TEXT("사기 줄 버튼이 있다"), Buy.Lines > 0);
	TestEqual(TEXT("탭 버튼 3개"), Buy.Tabs, 3);
	TestEqual(TEXT("닫기 버튼 1개"), Buy.Close, 1);
	TestEqual(TEXT("답 전이 아니면 줄 잠김 0"), Buy.LockedLines, 0);
	TestEqual(TEXT("고른 사기 탭 = UiButtonHighlight"), TintOf(TEXT("BuyTabButton")), SnareUiStyle::UiButtonHighlight());
	TestEqual(TEXT("안 고른 팔기 탭 = UiWoodButton"), TintOf(TEXT("SellTabButton")), SnareUiStyle::UiWoodButton());

	// ② 팔기 탭 — 다시 세운 줄도 둥글고 강조가 옮겨 간다.
	Screen->SetSellTab(true);
	const FCount Sell = Scan(TEXT("팔기"));
	TestTrue(TEXT("팔기 줄 버튼이 있다"), Sell.Lines > 0);
	TestEqual(TEXT("고른 팔기 탭 = UiButtonHighlight"), TintOf(TEXT("SellTabButton")), SnareUiStyle::UiButtonHighlight());

	// ③ 답을 기다리는 동안 — 줄 전부 잠김 그대로, 모양도 그대로.
	TestTrue(TEXT("팔기 요청을 띄운다"), Screen->RequestSell(Sellable));
	const FCount Pending = Scan(TEXT("대기"));
	TestTrue(TEXT("대기 중"), Screen->IsRequestPending());
	TestEqual(TEXT("대기 중 줄 전부 잠김"), Pending.LockedLines, Pending.Lines);

	AddInfo(FString::Printf(TEXT("[ShopRound] 둥근 버튼 사기 %d/%d (줄 %d·탭 %d·닫기 %d) · 팔기 %d/%d (줄 %d) · 대기 %d/%d (잠김 %d/%d)"),
		Buy.Round, Buy.All, Buy.Lines, Buy.Tabs, Buy.Close, Sell.Round, Sell.All, Sell.Lines, Pending.Round, Pending.All,
		Pending.LockedLines, Pending.Lines));

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}


// 루프#539 가챠 희귀 소재 — 아이템 표 행이 없어 `DT_GachaPool.SellPrice` 로 잡화점 팔기 탭에 줄이 선다(DESIGN 10.2).
// 값 있는 풀 행·값 0 풀 행을 표에서 고른다(ID 를 박지 않는다). 다른 가게는 안 산다 · 이름에 날 ID·묶음 수가 안 보인다 ·
// 「모두 팔기」 = {itemId, count: 가진 개수} · 답만으로 코인이 안 바뀐다(CLAUDE.md 8).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopGachaMaterialSellTest, "Snarehaven.Shop.Sell.GachaMaterialLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopGachaMaterialSellTest::RunTest(const FString& Parameters)
{
	const UDataTable* const PoolTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Gacha/DT_GachaPool.DT_GachaPool"));
	if (!TestNotNull(TEXT("DT_GachaPool 로드"), PoolTable))
	{
		return false;
	}
	const FGachaPoolRow* Material = nullptr;
	const FGachaPoolRow* NotSold = nullptr;
	int32 PricedRows = 0;
	int32 MaterialRows = 0;
	for (const TPair<FName, uint8*>& Pair : PoolTable->GetRowMap())
	{
		const FGachaPoolRow* const Row = reinterpret_cast<const FGachaPoolRow*>(Pair.Value);
		PricedRows += Row->SellPrice > 0 ? 1 : 0;
		MaterialRows += Row->Category.ToString() == TEXT("희귀 소재") ? 1 : 0;
		if (Row->SellPrice > 0 && Material == nullptr)
		{
			Material = Row;
		}
		if (Row->SellPrice <= 0 && NotSold == nullptr && UBackpackScreenWidget::IsShownGachaPrize(Row->ItemID))
		{
			NotSold = Row;
		}
	}
	AddInfo(FString::Printf(TEXT("풀 %d행 중 값 있는 것 %d"), PoolTable->GetRowMap().Num(), PricedRows));
	TestTrue(TEXT("희귀 소재 행이 있다(빈 통과 방지)"), MaterialRows > 0);
	TestEqual(TEXT("값 있는 풀 행 = 공개 문서 「희귀 소재」 행"), PricedRows, MaterialRows);
	if (!TestNotNull(TEXT("값 있는 풀 행"), Material) || !TestNotNull(TEXT("값 0 풀 행"), NotSold))
	{
		return false;
	}
	const FName MaterialID = Material->ItemID;
	constexpr int32 TestMaterialCount = 4;

	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상점 화면 위젯"), Screen))
	{
		DestroyTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	TArray<FRecordedCall> Calls;
	InstallRecordingServer(Calls);

	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.CoinBalance = TestCoinBefore;
	Snapshot.AddItem(MaterialID, TestMaterialCount);
	Snapshot.AddItem(NotSold->ItemID, 1);
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	Screen->SetOpen(true);

	Screen->SetShopKind(EShopKind::General);
	const TArray<FShopLineView> Lines = Screen->BuildSellLines();
	const FShopLineView* const Line = Lines.FindByPredicate([MaterialID](const FShopLineView& L) { return L.ItemID == MaterialID; });
	if (TestNotNull(TEXT("잡화점 팔기 탭에 소재 줄"), Line))
	{
		const FString Text = Screen->GetLineText(*Line, true).ToString();
		AddInfo(FString::Printf(TEXT("소재 줄 [%s] (풀 이름 %s · 값 %d)"), *Text, *Material->DisplayName.ToString(), Material->SellPrice));
		TestEqual(TEXT("줄 값 = 풀 SellPrice"), Line->Price, Material->SellPrice);
		TestEqual(TEXT("줄 개수 = 가진 개수"), Line->Count, TestMaterialCount);
		TestFalse(TEXT("줄에 날 ID 가 안 보인다"), Text.Contains(TEXT("gacha.")));
		TestTrue(TEXT("줄 이름은 풀 이름 앞부분"), Material->DisplayName.ToString().StartsWith(Line->DisplayName.ToString()));
		TestFalse(TEXT("이름 끝의 묶음 수(x5 등)는 뗀다"), Line->DisplayName.ToString().Contains(TEXT(" x")));
	}
	TestFalse(TEXT("값 0 인 가챠 경품(의상·데코)은 팔기 줄이 없다"),
		Lines.ContainsByPredicate([NotSold](const FShopLineView& L) { return L.ItemID == NotSold->ItemID; }));
	for (const EShopKind Kind : {EShopKind::Fishing, EShopKind::Farming, EShopKind::Clothes})
	{
		Screen->SetShopKind(Kind);
		TestFalse(*FString::Printf(TEXT("가게 %d 는 소재를 안 산다"), static_cast<int32>(Kind)),
			Screen->BuildSellLines().ContainsByPredicate([MaterialID](const FShopLineView& L) { return L.ItemID == MaterialID; }));
	}
	Screen->SetShopKind(EShopKind::General);

	TestTrue(TEXT("모두 팔기 요청을 띄운다"), Screen->RequestSellAll(MaterialID));
	if (TestEqual(TEXT("서버를 한 번 불렀다"), Calls.Num(), 1))
	{
		TestEqual(TEXT("함수 이름"), Calls[0].FunctionName, FString(TEXT("SellItem")));
		TestEqual(TEXT("itemId"), Calls[0].ItemId, MaterialID.ToString());
		TestEqual(TEXT("count 는 가진 개수"), Calls[0].Count, TestMaterialCount);
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetStringField(TEXT("sold"), MaterialID.ToString());
		Reply->SetNumberField(TEXT("count"), TestMaterialCount);
		Reply->SetNumberField(TEXT("coins"), Material->SellPrice * TestMaterialCount);
		Calls[0].OnDone(true, Reply, FString());
		const FString Done = Screen->GetMessage().ToString();
		AddInfo(FString::Printf(TEXT("팔기 문구 [%s]"), *Done));
		TestTrue(TEXT("성공 문구"), Done.Contains(TEXT("팔았다")));
		TestFalse(TEXT("성공 문구에 날 ID 가 안 보인다"), Done.Contains(TEXT("gacha.")));
		TestEqual(TEXT("답만으로 코인이 안 바뀐다"), Backend->GetPlayerSnapshot().CoinBalance, TestCoinBefore);
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShopRetryTest, "Snarehaven.Shop.Screen.Retry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShopRetryTest::RunTest(const FString& Parameters)
{
	// 서버에 닿지 못한 거래만 이유 곁에 「다시 시도」 — 누르면 같은 거래를 다시. 서버 거절엔 안 보인다.
	UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}
	FName Sellable;
	FName Unsellable;
	PickSellableItems(*ItemTable, Sellable, Unsellable);

	UWorld* World = CreateTestWorld();
	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	TArray<FRecordedCall> Calls;
	InstallRecordingServer(Calls);
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeSnapshot(TestCoinBefore, Sellable, Unsellable), /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	Screen->SetOpen(true);

	const UWidget* const RetryBox = Screen->WidgetTree->FindWidget(TEXT("ShopRetryBox"));
	UButton* const RetryButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("ShopRetryButton")));
	const TArray<FShopLineView> BuyLines = Screen->BuildBuyLines();
	const FName BuyID = BuyLines.Num() > 0 ? BuyLines[0].ItemID : NAME_None;
	if (!TestNotNull(TEXT("다시 시도 단추가 있다"), RetryButton) || RetryBox == nullptr || !TestTrue(TEXT("사기 요청"), Screen->RequestBuy(BuyID)))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		DestroyTestWorld(World);
		return false;
	}
	TestFalse(TEXT("처음엔 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	Calls.Last().OnDone(false, nullptr, TEXT("net"));
	TestTrue(TEXT("네트워크 실패 → 다시 시도 보임"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	TestFalse(TEXT("이유를 보인다"), Screen->GetMessage().IsEmpty());
	const int32 Before = Calls.Num();
	RetryButton->OnClicked.Broadcast();
	TestEqual(TEXT("다시 시도 → BuyItem 다시"), Calls.Num() > Before ? Calls.Last().FunctionName : FString(), FString(TEXT("BuyItem")));
	TestEqual(TEXT("같은 아이템"), Calls.Last().ItemId, BuyID.ToString());
	TestFalse(TEXT("보내는 중엔 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	TSharedPtr<FJsonObject> Rejected = MakeShared<FJsonObject>();
	Rejected->SetStringField(TEXT("error"), TEXT("x"));
	Rejected->SetStringField(TEXT("code"), TEXT("not_enough_coins"));
	Calls.Last().OnDone(true, Rejected, FString());
	TestFalse(TEXT("서버 거절엔 다시 시도 없음"), RetryBox->GetVisibility() == ESlateVisibility::Visible);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
