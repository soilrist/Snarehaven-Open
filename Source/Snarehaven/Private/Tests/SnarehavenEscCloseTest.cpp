// INBOX 결함(2026-10-04 13:50) 2층 검사 — ESC·안드로이드 뒤로 버튼으로 열린 창 닫기 (`FSnareScreenCloser`).
// 이 검사가 잡는 것: ① 화면을 열고 ESC 를 눌러도 안 닫히는 것(화면 하나라도 빠지면 실패)
// ② 상점+배낭 중 하나만 닫히는 것 ③ 열린 창이 없는데 ESC 가 키를 먹어 일시정지 메뉴 길을 막는 것
// ④ 업적 위 ESC 가 도감까지 한 번에 날리는 것 ⑤ 구매 응답 대기 중 닫으면 응답 처리가 사라지는 것
// ⑥ 옷 상점(M37-01)에서 ESC 가 판을 남긴 채 일시정지 메뉴를 띄우거나, 작은 창 대신 판째 닫거나, 확인 창 닫기가 구매를 보내는 것.
// ⑦ 옷장(M37-02)에서 ESC 가 판을 남기거나, 옷 상점보다 옷장을 먼저 닫거나, 옷장과 함께 밑의 배낭까지 닫는 것.
// ⑧ 상자 열기(M37-03)에서 ESC 가 판을 남기거나, 응답 대기 중 닫은 판을 응답이 다시 붙이거나, 닫기가 열기 요청·지급을 바꾸는 것.
// 키는 실제 Slate 전처리기 함수(`HandleKeyDownEvent`)에 넣는다 — 사람이 누르는 길과 같은 함수다.
// 실행: UnrealEditor <uproject> -ExecCmds="Automation RunTests Snarehaven.UI.EscClose; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Components/CanvasPanel.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Inventory/BackpackComponent.h"
#include "UI/AchievementScreenWidget.h"
#include "UI/AttendanceScreenWidget.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/ChestScreenWidget.h"
#include "UI/CodexScreenWidget.h"
#include "UI/CraftingScreenWidget.h"
#include "UI/CrateScreenWidget.h"
#include "UI/GachaScreenWidget.h"
#include "UI/PauseMenuWidget.h"
#include "UI/QuestScreenWidget.h"
#include "UI/ShopScreenWidget.h"
#include "UI/SnareScreenCloser.h"
#include "UI/WardrobeShopWidget.h"
#include "UI/WardrobeWidget.h"
#include "UI/WorldMapScreenWidget.h"

namespace
{
	UWorld* CreateEscTestWorld()
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyEscTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** 키 한 번 — 전처리기가 먹었으면 true. */
	bool PressKey(FSnareScreenCloser& Closer, const FKey& Key)
	{
		const FKeyEvent Event(Key, FModifierKeysState(), /*UserIndex=*/0, /*bIsRepeat=*/false, 0, 0);
		return Closer.HandleKeyDownEvent(FSlateApplication::Get(), Event);
	}

	/** 도감·업적·뽑기·메뉴는 "화면에 붙어 있음" 이 열림이다 — 검사 월드엔 뷰포트가 없어 판 하나에 붙인다. */
	UCanvasPanel* MakeHolder()
	{
		return NewObject<UCanvasPanel>(GetTransientPackage());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEscCloseEachScreenTest, "Snarehaven.UI.EscClose.EachScreenCloses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEscCloseEachScreenTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate 앱이 있다"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	UWorld* const World = CreateEscTestWorld();
	FSnareScreenCloser Closer(World);

	// 열린 창이 없어도 키를 먹는다 — 흘려보내면 에디터 PIE 가 ESC 를 「플레이 중지」로 받아 게임이 꺼진다(INBOX 15:40).
	TestTrue(TEXT("빈 상태 ESC 도 먹는다(게임이 안 꺼진다)"), PressKey(Closer, EKeys::Escape));
	TestTrue(TEXT("빈 상태 뒤로 버튼도 먹는다"), PressKey(Closer, EKeys::Android_Back));

	UBackpackScreenWidget* const Backpack = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	Backpack->SetPanelOpen(true);
	TestFalse(TEXT("ESC·뒤로 말고 다른 키는 안 닫는다"), PressKey(Closer, EKeys::A));
	TestTrue(TEXT("배낭: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestFalse(TEXT("배낭: 닫혔다"), Backpack->IsPanelOpen());

	UChestScreenWidget* const Chest = CreateWidget<UChestScreenWidget>(World, UChestScreenWidget::StaticClass());
	Chest->SetOpen(true);
	TestTrue(TEXT("보관함: 뒤로 버튼을 먹는다"), PressKey(Closer, EKeys::Android_Back));
	TestFalse(TEXT("보관함: 닫혔다"), Chest->IsOpen());

	UQuestScreenWidget* const Quest = CreateWidget<UQuestScreenWidget>(World, UQuestScreenWidget::StaticClass());
	Quest->SetOpen(true);
	TestTrue(TEXT("퀘스트: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestFalse(TEXT("퀘스트: 닫혔다"), Quest->IsOpen());

	UAttendanceScreenWidget* const Attendance =
		CreateWidget<UAttendanceScreenWidget>(World, UAttendanceScreenWidget::StaticClass());
	Attendance->SetOpen(true);
	TestTrue(TEXT("출석: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestFalse(TEXT("출석: 닫혔다"), Attendance->IsOpen());

	UCraftingScreenWidget* const Crafting = CreateWidget<UCraftingScreenWidget>(World, UCraftingScreenWidget::StaticClass());
	Crafting->OpenBackpack();
	TestTrue(TEXT("제작: 열렸다"), Crafting->IsOpen());
	TestTrue(TEXT("제작: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestFalse(TEXT("제작: 닫혔다"), Crafting->IsOpen());

	UWorldMapScreenWidget* const Map = CreateWidget<UWorldMapScreenWidget>(World, UWorldMapScreenWidget::StaticClass());
	Map->SetMapOpen(true);
	TestTrue(TEXT("지도: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestFalse(TEXT("지도: 닫혔다"), Map->IsMapOpen());

	UCanvasPanel* const Holder = MakeHolder();
	UGachaScreenWidget* const Gacha = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
	Holder->AddChild(Gacha);
	TestTrue(TEXT("뽑기: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestNull(TEXT("뽑기: 떼어 냈다"), Gacha->GetParent());

	UPauseMenuWidget* const Pause = CreateWidget<UPauseMenuWidget>(World, UPauseMenuWidget::StaticClass());
	Holder->AddChild(Pause);
	TestTrue(TEXT("일시정지 메뉴: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestNull(TEXT("일시정지 메뉴: 떼어 냈다"), Pause->GetParent());

	// 도감 → 업적: 업적이 도감을 접고 그 위에 뜬다. ESC 한 번 = 맨 위(업적)만, 두 번째 = 도감.
	UCodexScreenWidget* const Codex = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	UAchievementScreenWidget* const Achievement =
		CreateWidget<UAchievementScreenWidget>(World, UAchievementScreenWidget::StaticClass());
	Holder->AddChild(Codex);
	Holder->AddChild(Achievement);
	Achievement->SetReturnScreen(Codex);
	Codex->SetVisibility(ESlateVisibility::Collapsed);
	TestTrue(TEXT("업적: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestNull(TEXT("업적: 떼어 냈다"), Achievement->GetParent());
	TestNotNull(TEXT("업적 닫아도 도감은 남는다"), Codex->GetParent());
	TestEqual(TEXT("업적 닫으면 도감이 다시 보인다"), Codex->GetVisibility(), ESlateVisibility::Visible);
	TestTrue(TEXT("도감: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestNull(TEXT("도감: 떼어 냈다"), Codex->GetParent());

	TestTrue(TEXT("다 닫은 뒤 ESC 도 먹는다(게임이 안 꺼진다)"), PressKey(Closer, EKeys::Escape));

	DestroyEscTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEscCloseShopTest, "Snarehaven.UI.EscClose.ShopAndBackpackTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEscCloseShopTest::RunTest(const FString& Parameters)
{
	constexpr int32 TestCoins = 999;   // 검사 전용 값 — 무엇이든 살 수 있게 (Verification.md 6)
	if (!TestTrue(TEXT("Slate 앱이 있다"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	UWorld* const World = CreateEscTestWorld();
	FSnareScreenCloser Closer(World);

	// 응답을 붙잡아 두는 가짜 서버 — "대기 중에 닫기" 를 만든다.
	USnareBackendSubsystem::FServerCallDone PendingDone;
	int32 CallCount = 0;
	USnareBackendSubsystem::TestServerOverride =
		[&PendingDone, &CallCount](const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			++CallCount;
			PendingDone = MoveTemp(OnDone);
		};

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.CoinBalance = TestCoins;
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);

	UShopScreenWidget* const Shop = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	UBackpackScreenWidget* const Backpack = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	Shop->BindToBackend(Backend);
	Shop->BindBackpackScreen(Backpack);
	Shop->SetOpen(true);
	TestTrue(TEXT("상점과 배낭이 같이 열린다"), Shop->IsOpen() && Backpack->IsPanelOpen());

	const TArray<FShopLineView> BuyLines = Shop->BuildBuyLines();
	const bool bAsked = BuyLines.Num() > 0 && Shop->RequestBuy(BuyLines[0].ItemID);
	TestTrue(TEXT("사기 요청을 띄웠다"), bAsked);
	TestTrue(TEXT("응답 대기 중"), Shop->IsRequestPending());

	TestTrue(TEXT("상점+배낭: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestFalse(TEXT("상점이 닫혔다"), Shop->IsOpen());
	TestFalse(TEXT("배낭도 같이 닫혔다"), Backpack->IsPanelOpen());
	TestTrue(TEXT("닫아도 응답 대기는 남는다(응답을 버리지 않는다)"), Shop->IsRequestPending());

	// 닫힌 뒤 도착한 응답도 처리된다 — 대기가 풀린다. 재화는 화면이 아니라 재조회 스냅샷만 바꾼다(CLAUDE.md 8).
	if (TestEqual(TEXT("서버를 한 번 불렀다"), CallCount, 1) && PendingDone)
	{
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetStringField(TEXT("bought"), BuyLines[0].ItemID.ToString());
		Reply->SetNumberField(TEXT("count"), 1);
		PendingDone(true, Reply, FString());
		TestFalse(TEXT("닫힌 뒤 온 응답도 처리돼 대기가 풀린다"), Shop->IsRequestPending());
	}
	TestEqual(TEXT("응답만으로 캐시 코인은 그대로"), Backend->GetPlayerSnapshot().CoinBalance, TestCoins);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyEscTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEscCloseWardrobeShopTest, "Snarehaven.UI.EscClose.WardrobeShopTopWindowFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEscCloseWardrobeShopTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr int32 TestGems = 999;
	constexpr int32 TestOfferGems = 1;
	if (!TestTrue(TEXT("Slate 앱이 있다"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	UWorld* const World = CreateEscTestWorld();
	FSnareScreenCloser Closer(World);

	// 부르면 적어 두기만 한다 — 「구매 요청 0」 과 「응답 대기 중」 을 본다.
	TArray<FString> CallNames;
	TArray<USnareBackendSubsystem::FServerCallDone> CallDones;
	USnareBackendSubsystem::TestServerOverride =
		[&CallNames, &CallDones](const FString& FunctionName, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			CallNames.Add(FunctionName);
			CallDones.Add(MoveTemp(OnDone));
		};
	const auto BuyCount = [&CallNames]() { return CallNames.FilterByPredicate([](const FString& Name) { return Name == TEXT("BuyItem"); }).Num(); };

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.GemBalance = TestGems;
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);

	UCanvasPanel* const Holder = MakeHolder();
	UWardrobeShopWidget* const WardrobeShop = CreateWidget<UWardrobeShopWidget>(World, UWardrobeShopWidget::StaticClass());
	WardrobeShop->TakeWidget();
	WardrobeShop->BindToBackend(Backend);
	Holder->AddChild(WardrobeShop);
	WardrobeShop->SetOpen(true);
	if (CallDones.Num() == 1)
	{
		TArray<TSharedPtr<FJsonValue>> Items;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("itemId"), FString::Printf(TEXT("item.test_cosmetic_%d"), Index));
			Entry->SetNumberField(TEXT("gems"), TestOfferGems);
			Items.Add(MakeShared<FJsonValueObject>(Entry));
		}
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetNumberField(TEXT("week"), 1);
		Reply->SetArrayField(TEXT("items"), Items);
		CallDones[0](true, Reply, FString());
	}
	if (!TestEqual(TEXT("진열 2칸을 받았다"), WardrobeShop->GetOffers().Num(), 2))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		DestroyEscTestWorld(World);
		return false;
	}
	TestTrue(TEXT("옷 상점: 판 열림으로 센다"), FSnareScreenCloser::HasOpenScreen(World));

	// ① 구매 확인 창 → ESC 한 번 = 확인 창만. 판은 남고 구매 요청은 안 나간다.
	WardrobeShop->SelectOffer(0);
	TestTrue(TEXT("확인 창: 닫을 게 있다(일시정지 메뉴 안 뜸)"), FSnareScreenCloser::CloseOpenScreens(World));
	TestFalse(TEXT("확인 창: 닫혔다"), WardrobeShop->IsBuyWindowOpen());
	TestNotNull(TEXT("확인 창: 판은 남는다"), WardrobeShop->GetParent());
	TestEqual(TEXT("확인 창: 구매 요청 0"), BuyCount(), 0);

	// ② 구매 완료 창 → ESC 한 번 = 완료 창만.
	WardrobeShop->SelectOffer(0);
	TestTrue(TEXT("구매를 보냈다"), WardrobeShop->ConfirmBuy());
	if (TestEqual(TEXT("구매 요청 한 번"), BuyCount(), 1))
	{
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetStringField(TEXT("bought"), WardrobeShop->GetOffers()[0].ItemID.ToString());
		Reply->SetNumberField(TEXT("count"), 1);
		CallDones[CallNames.IndexOfByKey(FString(TEXT("BuyItem")))](true, Reply, FString());
	}
	TestTrue(TEXT("완료 창이 떴다"), WardrobeShop->IsCompleteWindowOpen());
	TestTrue(TEXT("완료 창: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestFalse(TEXT("완료 창: 닫혔다"), WardrobeShop->IsCompleteWindowOpen());
	TestNotNull(TEXT("완료 창: 판은 남는다"), WardrobeShop->GetParent());
	TestEqual(TEXT("완료 창 닫기가 젬을 안 바꾼다(CLAUDE.md 8)"), Backend->GetPlayerSnapshot().GemBalance, TestGems);

	// ③ 응답 대기 중 확인 창 → 키만 먹고 판·대기는 그대로(응답을 버리지 않는다).
	WardrobeShop->SelectOffer(1);
	TestTrue(TEXT("두 번째 구매를 보냈다"), WardrobeShop->ConfirmBuy());
	TestTrue(TEXT("대기 중: 닫을 게 있다(일시정지 메뉴 안 뜸)"), FSnareScreenCloser::CloseOpenScreens(World));
	TestTrue(TEXT("대기 중: 응답 대기는 남는다"), WardrobeShop->IsPending());
	TestNotNull(TEXT("대기 중: 판은 남는다"), WardrobeShop->GetParent());
	TestEqual(TEXT("대기 중: 구매 요청은 그대로 2"), BuyCount(), 2);
	CallDones.Last()(false, nullptr, TEXT("test_network"));
	TestFalse(TEXT("실패 응답 뒤 대기 풀림"), WardrobeShop->IsPending());

	// ④ 작은 창 없음 → ESC 한 번 = 판 닫힘. 그다음 ESC 는 닫을 게 없다(= 일시정지 메뉴 차례).
	TestTrue(TEXT("판: 뒤로 버튼을 먹는다"), PressKey(Closer, EKeys::Android_Back));
	TestNull(TEXT("판: 떼어 냈다"), WardrobeShop->GetParent());
	TestFalse(TEXT("판 닫힌 뒤 열린 판 없음"), FSnareScreenCloser::HasOpenScreen(World));
	TestFalse(TEXT("판 닫힌 뒤 ESC 는 닫을 게 없다(일시정지 메뉴 차례)"), FSnareScreenCloser::CloseOpenScreens(World));

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyEscTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEscCloseWardrobeTest, "Snarehaven.UI.EscClose.WardrobeBackToBackpack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEscCloseWardrobeTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate 앱이 있다"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	UWorld* const World = CreateEscTestWorld();
	FSnareScreenCloser Closer(World);

	// 배낭 「장비」 탭 → 「옷장」 → 「상점 열기」 순으로 쌓인 상태(판 셋).
	UBackpackScreenWidget* const Backpack = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	Backpack->SetPanelOpen(true);
	UCanvasPanel* const Holder = MakeHolder();
	UWardrobeWidget* const Wardrobe = CreateWidget<UWardrobeWidget>(World, UWardrobeWidget::StaticClass());
	Wardrobe->TakeWidget();
	Holder->AddChild(Wardrobe);
	UWardrobeShopWidget* const WardrobeShop = CreateWidget<UWardrobeShopWidget>(World, UWardrobeShopWidget::StaticClass());
	WardrobeShop->TakeWidget();
	Holder->AddChild(WardrobeShop);

	// ① 옷 상점이 위에 있으면 상점 먼저 — 옷장·배낭은 남는다.
	TestTrue(TEXT("상점 위: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestNull(TEXT("상점 위: 옷 상점 떼어 냈다"), WardrobeShop->GetParent());
	TestNotNull(TEXT("상점 위: 옷장은 남는다"), Wardrobe->GetParent());
	TestTrue(TEXT("상점 위: 배낭은 남는다"), Backpack->IsPanelOpen());

	// ② 옷장만 열린 판 → ESC 한 번 = 옷장만 닫혀 배낭으로 돌아간다.
	TestTrue(TEXT("옷장: 판 열림으로 센다"), FSnareScreenCloser::HasOpenScreen(World));
	TestTrue(TEXT("옷장: 뒤로 버튼을 먹는다"), PressKey(Closer, EKeys::Android_Back));
	TestNull(TEXT("옷장: 떼어 냈다"), Wardrobe->GetParent());
	TestTrue(TEXT("옷장: 밑의 배낭은 남는다"), Backpack->IsPanelOpen());

	// ③ 다음 ESC 는 배낭을 닫고, 그다음은 닫을 게 없다(= 일시정지 메뉴 차례).
	TestTrue(TEXT("배낭: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestFalse(TEXT("배낭: 닫혔다"), Backpack->IsPanelOpen());
	TestFalse(TEXT("판 셋 닫힌 뒤 열린 판 없음"), FSnareScreenCloser::HasOpenScreen(World));
	TestFalse(TEXT("판 셋 닫힌 뒤 ESC 는 닫을 게 없다(일시정지 메뉴 차례)"), FSnareScreenCloser::CloseOpenScreens(World));

	DestroyEscTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEscCloseCrateTest, "Snarehaven.UI.EscClose.CrateBackToBackpack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEscCloseCrateTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	const FName TestCrateID(TEXT("item.crate_autumn26"));
	constexpr int32 TestKeys = 1;
	if (!TestTrue(TEXT("Slate 앱이 있다"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	UWorld* const World = CreateEscTestWorld();
	FSnareScreenCloser Closer(World);

	// 부르면 적어 두기만 한다 — 「열기 요청 한 번」 과 「응답 대기 중」 을 본다.
	TArray<FString> CallNames;
	TArray<USnareBackendSubsystem::FServerCallDone> CallDones;
	USnareBackendSubsystem::TestServerOverride =
		[&CallNames, &CallDones](const FString& FunctionName, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			CallNames.Add(FunctionName);
			CallDones.Add(MoveTemp(OnDone));
		};
	const auto OpenCount = [&CallNames]() { return CallNames.FilterByPredicate([](const FString& Name) { return Name == TEXT("OpenCrate"); }).Num(); };

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
	AActor* const Owner = World->SpawnActor<AActor>();
	USceneComponent* const Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
	Owner->SetRootComponent(Root);
	Root->RegisterComponent();
	UBackpackComponent* const BackpackComponent = NewObject<UBackpackComponent>(Owner, TEXT("Backpack"));
	BackpackComponent->RegisterComponent();
	BackpackComponent->BindTargets(Backend, nullptr);
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	FInventorySlot CrateSlot;
	CrateSlot.ItemID = TestCrateID;
	CrateSlot.Count = 1;
	FInventorySlot KeySlot;
	KeySlot.ItemID = UBackpackComponent::GetCrateKeyID();
	KeySlot.Count = TestKeys;
	Snapshot.InventorySlots = {CrateSlot, KeySlot};
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/false);

	// 배낭에서 상자를 눌러 상자 판이 그 위에 뜬 상태(판 둘).
	UBackpackScreenWidget* const Backpack = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	Backpack->SetPanelOpen(true);
	UCanvasPanel* const Holder = MakeHolder();
	UCrateScreenWidget* const Crate = CreateWidget<UCrateScreenWidget>(World, UCrateScreenWidget::StaticClass());
	Crate->TakeWidget();
	Crate->BindToCrate(Backend, BackpackComponent, TestCrateID);
	Holder->AddChild(Crate);

	// ① 열린 상자 판 → ESC 한 번 = 상자 판만 닫혀 배낭으로 돌아간다.
	TestTrue(TEXT("상자: 판 열림으로 센다"), FSnareScreenCloser::HasOpenScreen(World));
	TestTrue(TEXT("상자: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestNull(TEXT("상자: 떼어 냈다"), Crate->GetParent());
	TestTrue(TEXT("상자: 밑의 배낭은 남는다"), Backpack->IsPanelOpen());
	TestEqual(TEXT("상자: 닫기는 열기 요청을 안 보낸다"), OpenCount(), 0);

	// ② 열기 응답 대기 중 → 뒤로 한 번 = 닫힘. 늦게 온 응답은 판을 다시 붙이지 않는다.
	Holder->AddChild(Crate);
	TestTrue(TEXT("대기: 열기를 보냈다"), Crate->PressOpen());
	TestTrue(TEXT("대기: 응답을 기다린다"), Crate->IsRequestPending());
	TestTrue(TEXT("대기: 뒤로 버튼을 먹는다"), PressKey(Closer, EKeys::Android_Back));
	TestNull(TEXT("대기: 떼어 냈다"), Crate->GetParent());
	TestEqual(TEXT("대기: 열기 요청은 그대로 한 번"), OpenCount(), 1);
	if (CallDones.Num() >= 1)
	{
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetStringField(TEXT("crateId"), TestCrateID.ToString());
		Reply->SetStringField(TEXT("itemId"), TEXT("item.harbor_whale"));
		Reply->SetStringField(TEXT("grade"), TEXT("epic"));
		CallDones[CallNames.IndexOfByKey(FString(TEXT("OpenCrate")))](true, Reply, FString());
	}
	TestFalse(TEXT("응답 뒤: 대기 풀림"), Crate->IsRequestPending());
	TestFalse(TEXT("응답 뒤: 결과는 서버 답으로 그렸다"), Crate->GetResultText().IsEmpty());
	TestNull(TEXT("응답 뒤: 판은 다시 안 붙는다"), Crate->GetParent());
	TestFalse(TEXT("응답 뒤: 상자 판 열림 아님"), Crate->IsInViewport());
	TestEqual(TEXT("응답 뒤: 열쇠 칸은 스냅샷 그대로(닫기가 지급을 안 바꾼다)"), Crate->GetKeyCount(), TestKeys);

	// ③ 다음 ESC 는 배낭을 닫고, 그다음은 닫을 게 없다(= 일시정지 메뉴 차례).
	TestTrue(TEXT("배낭: ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestFalse(TEXT("배낭: 닫혔다"), Backpack->IsPanelOpen());
	TestFalse(TEXT("판 둘 닫힌 뒤 열린 판 없음"), FSnareScreenCloser::HasOpenScreen(World));
	TestFalse(TEXT("판 둘 닫힌 뒤 ESC 는 닫을 게 없다(일시정지 메뉴 차례)"), FSnareScreenCloser::CloseOpenScreens(World));

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyEscTestWorld(World);
	return true;
}

// M45-05 판 열기·닫기 소리 — `FSnareScreenCloser::UpdatePanelSound` 한 곳이 열린 창 수를 견줘 `Ui_Open`·`Ui_Close` 를 한 번씩.
// 잡는 것: 판을 열어도 소리 0 · 열린 채 가만히 있는데 또 남 · ESC 로 닫아도 소리 0 · 상자 판을 배낭 위에 열 때 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPanelSoundTest, "Snarehaven.UI.PanelSound.OpenAndEscClose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPanelSoundTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate 앱이 있다"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	UWorld* const World = CreateEscTestWorld();
	FSnareScreenCloser Closer(World);
	const FName Open(TEXT("Ui_Open"));
	const FName Close(TEXT("Ui_Close"));
	int32 Opens = 0;
	int32 Closes = 0;
	auto Count = [&](const FName Cue)
	{
		Opens += Cue == Open ? 1 : 0;
		Closes += Cue == Close ? 1 : 0;
	};

	TestTrue(TEXT("빈 화면 = 소리 없음"), Closer.UpdatePanelSound(World).IsNone());

	UBackpackScreenWidget* const Backpack = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	Backpack->SetPanelOpen(true);
	Count(Closer.UpdatePanelSound(World));
	TestEqual(TEXT("배낭 열기 → Open 1"), Opens, 1);
	Count(Closer.UpdatePanelSound(World));
	TestEqual(TEXT("열린 채 그대로 → Open 그대로 1"), Opens, 1);

	UCanvasPanel* const Holder = MakeHolder();
	UCrateScreenWidget* const Crate = CreateWidget<UCrateScreenWidget>(World, UCrateScreenWidget::StaticClass());
	Crate->TakeWidget();
	Holder->AddChild(Crate);
	Count(Closer.UpdatePanelSound(World));
	TestEqual(TEXT("배낭 위 상자 판 → Open 2"), Opens, 2);

	TestTrue(TEXT("ESC 를 먹는다"), PressKey(Closer, EKeys::Escape));
	TestEqual(TEXT("ESC 가 상자 판만 닫았다(배낭 남음)"), FSnareScreenCloser::CountOpenScreens(World), 1);
	Count(Closer.UpdatePanelSound(World));
	TestEqual(TEXT("ESC 로 상자 닫기 → Close 1"), Closes, 1);

	TestTrue(TEXT("뒤로 버튼을 먹는다"), PressKey(Closer, EKeys::Android_Back));
	Count(Closer.UpdatePanelSound(World));
	TestEqual(TEXT("뒤로 버튼으로 배낭 닫기 → Close 2"), Closes, 2);
	Count(Closer.UpdatePanelSound(World));
	TestEqual(TEXT("닫힌 채 그대로 → Open 2 · Close 2"), Opens * 10 + Closes, 22);
	TestEqual(TEXT("소리 크기 0.5 (DESIGN 2.2)"), FSnareScreenCloser::PanelSoundVolume(), 0.5f);
	AddInfo(FString::Printf(TEXT("[PanelSoundTest] 배낭 열기 → 상자 열기 → ESC → 뒤로: Open %d · Close %d"), Opens, Closes));

	DestroyEscTestWorld(World);
	return true;
}

#endif
