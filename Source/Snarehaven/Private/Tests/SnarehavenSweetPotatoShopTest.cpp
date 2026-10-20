// DESIGN 8.17 고구마 장수 2층 검사 — `UShopScreenWidget`(EShopKind::SweetPotato) + `SnareSweetPotato`.
// 이 검사가 잡는 것: ① 요일 식이 서버 `potatoWeekdayOf` 와 어긋나는 것 ② 열 때 `GetSweetPotato` 를 안 묻는 것
// ③ 사기 줄이 묶음 1·5·10 이 아니거나 값이 `개당 값 × 개수` 가 아닌 것 ④ 남은 상한을 넘는 묶음이 보이는 것
// ⑤ 줄을 누르면 `BuySweetPotato {bundles}` 가 안 가는 것 ⑥ **서버 답 전에** 보유가 바뀌는 것(CLAUDE.md 8).
// 서버는 `TestServerOverride` 가짜로 대신한다. 검사 끝에 반드시 비운다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UI/ShopScreenWidget.h"
#include "World/SnareSweetPotato.h"
#include "World/ShopVendorActor.h"

namespace
{
	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr int32 TestBuyPrice = 7;
	constexpr int32 TestCap = 100;

	struct FPotatoCall
	{
		FString FunctionName;
		int32 Bundles = 0;
		int32 Count = 0;
		USnareBackendSubsystem::FServerCallDone OnDone;
	};

	TSharedPtr<FJsonObject> MakeStateReply(int32 Bought)
	{
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetNumberField(TEXT("weekday"), 0);
		Reply->SetNumberField(TEXT("buyPrice"), TestBuyPrice);
		Reply->SetNumberField(TEXT("boughtThisWeek"), Bought);
		Reply->SetNumberField(TEXT("cap"), TestCap);
		return Reply;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSweetPotatoWeekdayTest, "Snarehaven.Shop.SweetPotato.Weekday",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSweetPotatoWeekdayTest::RunTest(const FString& Parameters)
{
	// 1970-01-01 = 목요일(4), 1970-01-04 = 일요일(0), 2026-10-11 = 일요일, 2026-10-10 = 토요일(6).
	TestEqual(TEXT("1970-01-01 목"), SnareSweetPotato::WeekdayOf(FDateTime(1970, 1, 1)), 4);
	TestEqual(TEXT("1970-01-04 일"), SnareSweetPotato::WeekdayOf(FDateTime(1970, 1, 4)), 0);
	TestEqual(TEXT("2026-10-11 일"), SnareSweetPotato::WeekdayOf(FDateTime(2026, 10, 11, 23, 59, 59)), 0);
	TestEqual(TEXT("2026-10-10 토"), SnareSweetPotato::WeekdayOf(FDateTime(2026, 10, 10)), 6);
	TestEqual(TEXT("2026-10-12 월"), SnareSweetPotato::WeekdayOf(FDateTime(2026, 10, 12)), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSweetPotatoBuyTest, "Snarehaven.Shop.SweetPotato.BuyBundles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSweetPotatoBuyTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	TArray<FPotatoCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FPotatoCall Call;
			Call.FunctionName = FunctionName;
			double Value = 0.0;
			if (Args.IsValid() && Args->TryGetNumberField(TEXT("bundles"), Value))
			{
				Call.Bundles = static_cast<int32>(Value);
			}
			if (Args.IsValid() && Args->TryGetNumberField(TEXT("count"), Value))
			{
				Call.Count = static_cast<int32>(Value);
			}
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	Screen->TakeWidget();
	Screen->BindToBackend(Backend);
	Screen->SetShopKind(EShopKind::SweetPotato);
	TestEqual(TEXT("답 전에는 줄이 없다"), Screen->BuildBuyLines().Num(), 0);

	// ② 열면 GetSweetPotato 를 묻는다.
	Screen->SetOpen(true);
	TestEqual(TEXT("열면 서버 한 번"), Calls.Num(), 1);
	TestEqual(TEXT("함수 이름"), Calls.Num() > 0 ? Calls[0].FunctionName : FString(), FString(TEXT("GetSweetPotato")));
	if (Calls.Num() > 0)
	{
		Calls[0].OnDone(true, MakeStateReply(0), FString());
	}

	// ③ 묶음 1·5·10, 값 = 개당 값 × 개수.
	TArray<FShopLineView> Lines = Screen->BuildBuyLines();
	TestEqual(TEXT("줄 3개"), Lines.Num(), 3);
	if (Lines.Num() == 3)
	{
		const int32 Bundles[3] = {1, 5, 10};
		for (int32 Index = 0; Index < 3; ++Index)
		{
			TestEqual(TEXT("묶음 수"), Lines[Index].Bundles, Bundles[Index]);
			TestEqual(TEXT("개수"), Lines[Index].Count, Bundles[Index] * SnareSweetPotato::BundleSize);
			TestEqual(TEXT("값 = 개당 × 개수"), Lines[Index].Price, Bundles[Index] * SnareSweetPotato::BundleSize * TestBuyPrice);
		}
	}

	// ④ 이미 산 개수가 많으면 남은 상한을 넘는 묶음은 안 보인다 (남은 30 → 1·... 5는 50>30 이라 숨김).
	Screen->ApplySweetPotatoReply(MakeStateReply(70));
	Lines = Screen->BuildBuyLines();
	TestEqual(TEXT("남은 30개면 1묶음 줄만"), Lines.Num(), 1);
	Screen->ApplySweetPotatoReply(MakeStateReply(0));

	// ⑤⑥ 줄을 누르면 BuySweetPotato {bundles}, 답 전에는 잠긴다.
	TestTrue(TEXT("5묶음 사기 요청"), Screen->RequestBuySweetPotato(5));
	TestEqual(TEXT("서버 두 번째 호출"), Calls.Num(), 2);
	if (Calls.Num() == 2)
	{
		TestEqual(TEXT("함수 이름"), Calls[1].FunctionName, FString(TEXT("BuySweetPotato")));
		TestEqual(TEXT("bundles"), Calls[1].Bundles, 5);
	}
	TestTrue(TEXT("답 전 보내는 중"), Screen->IsRequestPending());
	TestFalse(TEXT("답 전 두 번째 요청은 거절"), Screen->RequestBuySweetPotato(1));
	TestEqual(TEXT("답 전 서버 호출 그대로"), Calls.Num(), 2);

	// 거절 답이면 산 개수가 안 바뀐다.
	if (Calls.Num() == 2)
	{
		const TSharedPtr<FJsonObject> Fail = MakeShared<FJsonObject>();
		Fail->SetBoolField(TEXT("ok"), false);
		Fail->SetStringField(TEXT("error"), TEXT("not_enough_coins"));
		Calls[1].OnDone(true, Fail, FString());
	}
	TestFalse(TEXT("거절 뒤 대기 풀림"), Screen->IsRequestPending());
	TestEqual(TEXT("거절 뒤에도 줄 3개"), Screen->BuildBuyLines().Num(), 3);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSweetPotatoSellTest, "Snarehaven.Shop.SweetPotato.SellLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSweetPotatoSellTest::RunTest(const FString& Parameters)
{
	// 잡화상 팔기 줄(8.17 ④): 고구마가 없으면 서버를 안 부르고 줄도 없다 · 있으면 열 때 GetSweetPotato · 월~토에만 줄 ·
	// ▲/▼·산 값 · 누르면 SellSweetPotato {count} · 답 전에는 배낭이 안 바뀐다.
	constexpr int32 TestHeld = 12;
	constexpr int32 TestPrice = 14;
	constexpr int32 TestPrev = 10;
	constexpr int32 TestBought = 70;
	constexpr int32 TestPaid = 700;

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	TArray<FPotatoCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FPotatoCall Call;
			Call.FunctionName = FunctionName;
			double Value = 0.0;
			if (Args.IsValid() && Args->TryGetNumberField(TEXT("count"), Value))
			{
				Call.Count = static_cast<int32>(Value);
			}
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UShopScreenWidget* const Screen = CreateWidget<UShopScreenWidget>(World, UShopScreenWidget::StaticClass());
	Screen->TakeWidget();
	Screen->BindToBackend(Backend);
	Screen->SetShopKind(EShopKind::General);

	// 고구마를 안 가졌으면 서버를 안 부른다.
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->SetSellTab(true);
	TestEqual(TEXT("고구마 없으면 서버 안 부름"), Calls.Num(), 0);
	TestEqual(TEXT("고구마 없으면 줄 없음"), Screen->BuildSellLines().Num(), 0);

	// 가졌으면 팔기 탭을 열 때 묻는다. 답 전에는 값을 몰라 줄이 없다.
	Snapshot.AddItem(FName(TEXT("item.sweet_potato")), TestHeld);
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->SetSellTab(false);
	Screen->SetSellTab(true);
	TestEqual(TEXT("가졌으면 서버 한 번"), Calls.Num(), 1);
	TestEqual(TEXT("함수 이름"), Calls.Num() > 0 ? Calls[0].FunctionName : FString(), FString(TEXT("GetSweetPotato")));
	TestEqual(TEXT("답 전에는 줄 없음"), Screen->BuildSellLines().Num(), 0);

	const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
	Reply->SetNumberField(TEXT("weekday"), 3);
	Reply->SetNumberField(TEXT("price"), TestPrice);
	Reply->SetNumberField(TEXT("prevPrice"), TestPrev);
	Reply->SetNumberField(TEXT("paid"), TestPaid);
	Reply->SetNumberField(TEXT("boughtThisWeek"), TestBought);
	Reply->SetNumberField(TEXT("cap"), TestCap);
	if (Calls.Num() > 0)
	{
		Calls[0].OnDone(true, Reply, FString());
	}
	const TArray<FShopLineView> Lines = Screen->BuildSellLines();
	if (TestEqual(TEXT("월~토 답이면 고구마 줄 1개"), Lines.Num(), 1))
	{
		TestTrue(TEXT("고구마 팔기 줄"), Lines[0].bPotatoSell);
		TestEqual(TEXT("개당 값"), Lines[0].Price, TestPrice);
		TestEqual(TEXT("가진 개수"), Lines[0].Count, TestHeld);
		TestEqual(TEXT("지난 칸보다 올랐다 ▲"), Lines[0].PotatoTrend, 1);
		TestEqual(TEXT("산 값 = 합 ÷ 개수"), Lines[0].PotatoPaidEach, TestPaid / TestBought);
	}

	// 누르면 SellSweetPotato {count}. 답 전에는 배낭이 그대로다.
	TestTrue(TEXT("1개 팔기 요청"), Screen->RequestSellSweetPotato(1));
	TestEqual(TEXT("서버 두 번째 호출"), Calls.Num(), 2);
	if (Calls.Num() == 2)
	{
		TestEqual(TEXT("함수 이름"), Calls[1].FunctionName, FString(TEXT("SellSweetPotato")));
		TestEqual(TEXT("count"), Calls[1].Count, 1);
	}
	TestEqual(TEXT("답 전 배낭 그대로"), Screen->BuildSellLines().Num() == 1 ? Screen->BuildSellLines()[0].Count : -1, TestHeld);
	TestFalse(TEXT("답 전 두 번째 요청은 거절"), Screen->RequestSellSweetPotato(1));

	// 일요일 답이면 사 주는 값이 없어 줄이 사라진다.
	if (Calls.Num() == 2)
	{
		const TSharedPtr<FJsonObject> Fail = MakeShared<FJsonObject>();
		Fail->SetBoolField(TEXT("ok"), false);
		Fail->SetStringField(TEXT("error"), TEXT("closed"));
		Calls[1].OnDone(true, Fail, FString());
	}
	TestFalse(TEXT("거절 뒤 대기 풀림"), Screen->IsRequestPending());
	Screen->ApplySweetPotatoReply(MakeStateReply(0));
	TestEqual(TEXT("일요일 답이면 줄 없음"), Screen->BuildSellLines().Num(), 0);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSweetPotatoClosedSignTest, "Snarehaven.Shop.SweetPotato.ClosedSign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSweetPotatoClosedSignTest::RunTest(const FString& Parameters)
{
	// 평일 = 몸 숨김 + 팻말 「일요일에 와요」 + 말 걸기 막힘, 일요일 = 팻말 없음. 다른 가게는 팻말이 안 뜬다.
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("시험 월드"), World))
	{
		return false;
	}
	AShopVendorActor* const Potato = World->SpawnActor<AShopVendorActor>();
	AShopVendorActor* const Other = World->SpawnActor<AShopVendorActor>();
	if (TestNotNull(TEXT("장수"), Potato) && TestNotNull(TEXT("다른 상인"), Other))
	{
		Potato->SetShopKind(EShopKind::SweetPotato);
		Potato->ApplyOpenState(false);
		TestTrue(TEXT("평일엔 닫힘"), Potato->IsClosedToday());
		TestTrue(TEXT("평일엔 팻말이 켜진다"), Potato->IsSignShown());
		TestEqual(TEXT("팻말 글자"), Potato->GetShownSignText().ToString(), SnareSweetPotato::ClosedSignText().ToString());
		TestTrue(TEXT("팻말에 「일요일」"), Potato->GetShownSignText().ToString().Contains(TEXT("일요일")));
		Potato->ApplyOpenState(true);
		TestFalse(TEXT("일요일엔 열림"), Potato->IsClosedToday());
		TestTrue(TEXT("일요일엔 이름이 켜진다"), Potato->IsSignShown());
		TestEqual(TEXT("일요일 이름 글자"), Potato->GetShownSignText().ToString(), SnareSweetPotato::OpenSignText().ToString());
		TestFalse(TEXT("일요일 이름에 「일요일에 와요」 없음"), Potato->GetShownSignText().ToString().Contains(TEXT("일요일")));
		Other->ApplyOpenState(false);
		TestFalse(TEXT("다른 가게는 닫지 않는다"), Other->IsClosedToday());
		TestFalse(TEXT("다른 가게는 팻말이 없다"), Other->IsSignShown());
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSweetPotatoArrivalNoticeTest, "Snarehaven.Shop.SweetPotato.ArrivalNotice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSweetPotatoArrivalNoticeTest::RunTest(const FString& Parameters)
{
	// 일요일 알림: 글자에 「고구마 장수」·「왔어요」, 폰·목장 HUD 가 없으면 띄운 걸로 치지 않고, 닫힌 날·다른 가게는 아예 안 띄운다.
	TestTrue(TEXT("알림에 「고구마 장수」"), SnareSweetPotato::ArrivalNoticeText().ToString().Contains(TEXT("고구마 장수")));
	TestTrue(TEXT("알림에 「왔어요」"), SnareSweetPotato::ArrivalNoticeText().ToString().Contains(TEXT("왔어요")));
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("시험 월드"), World))
	{
		return false;
	}
	AShopVendorActor* const Potato = World->SpawnActor<AShopVendorActor>();
	AShopVendorActor* const Other = World->SpawnActor<AShopVendorActor>();
	if (TestNotNull(TEXT("장수"), Potato) && TestNotNull(TEXT("다른 상인"), Other))
	{
		Potato->SetShopKind(EShopKind::SweetPotato);
		Potato->ApplyOpenState(true);
		TestFalse(TEXT("폰이 없으면 못 띄움"), Potato->TryShowArrivalNotice(nullptr));
		TestFalse(TEXT("못 띄웠으니 띄운 표시 없음"), Potato->HasShownArrivalNotice());
		Potato->ApplyOpenState(false);
		TestFalse(TEXT("닫힌 날은 안 띄움"), Potato->TryShowArrivalNotice(nullptr));
		Other->ApplyOpenState(true);
		TestFalse(TEXT("다른 가게는 안 띄움"), Other->TryShowArrivalNotice(nullptr));
	}
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSweetPotatoCalendarHintTest, "Snarehaven.Shop.SweetPotato.CalendarHint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSweetPotatoCalendarHintTest::RunTest(const FString& Parameters)
{
	// 달력 안내: 언제(일요일)·어디서(장터 줄 끝)·누구(고구마 장수)가 다 들어 있어야 처음 하는 사람이 만난다.
	const FString Hint = SnareSweetPotato::CalendarHintText().ToString();
	TestTrue(TEXT("안내에 「일요일」"), Hint.Contains(TEXT("일요일")));
	TestTrue(TEXT("안내에 「장터」"), Hint.Contains(TEXT("장터")));
	TestTrue(TEXT("안내에 「고구마 장수」"), Hint.Contains(TEXT("고구마 장수")));
	return true;
}

#endif
