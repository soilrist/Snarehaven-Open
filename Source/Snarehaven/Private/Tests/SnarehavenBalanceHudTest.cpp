// ROADMAP M7-03 2층 검사 — 사냥·목장 HUD 의 **재화 잔액 줄**이 백엔드 스냅샷만 비추는지.
// 이 검사가 잡는 것: ① 백엔드에게 못 들었는데 잔액을 `0` 으로 보여 주는 것("0원" 과 "아직 모른다" 를
// 같은 화면으로 만드는 사고 — Verification.md 7) ② 스냅샷이 바뀌었는데 화면 숫자가 안 따라가는 것
// (알림 배선이 빠진 사고) ③ 두 화면이 서로 다른 글을 보여 주는 것 ④ **화면이 잔액을 건드리는 것**
// (CLAUDE.md 8 — 재화 판정은 백엔드 몫이고 클라이언트는 읽기만 한다).
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UI/HuntHudWidget.h"
#include "UI/SnareStoreSettings.h"
#include "UI/RanchHudWidget.h"
#include "UI/SnareBalanceHud.h"

namespace
{
	// 검사 전용 값 — 설계 숫자가 아니다 (Verification.md 6). 1000 미만으로 둔 이유는 화면 글자가
	// `FText::AsNumber` 라 천 단위에 쉼표가 붙어, 기대 문자열을 손으로 적으면 어긋나기 때문이다.
	constexpr int32 TestCoinBalance = 120;
	constexpr int32 TestGemBalance = 34;
	constexpr int32 TestSecondCoinBalance = 7;
	constexpr int32 TestSecondGemBalance = 0;   // 0 은 "모른다" 가 아니라 **0개** 로 보여야 한다

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

	/**
	 * 게임 인스턴스 서브시스템은 `ClassWithin` 이 `UGameInstance` 라 아무 데나 만들 수 없다
	 * (`SnarehavenPlayerCacheTest` 와 같은 방식). `Initialize` 를 부르지 않으므로 로그인은 안 붙는다 —
	 * 검사 한 번에 PlayFab 계정을 만들지 않기 위해서다.
	 */
	USnareBackendSubsystem* MakeTestBackend()
	{
		UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
		return NewObject<USnareBackendSubsystem>(GameInstance);
	}

	/** 백엔드가 보내 준 셈 치는 상태 한 벌. 잔액을 바꾸는 길은 이것 하나뿐이다 (CLAUDE.md 8). */
	FSnarePlayerSnapshot MakeTestSnapshot(int32 Coin, int32 Gem)
	{
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TEXT("PFTESTOWNER");
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		Snapshot.CoinBalance = Coin;
		Snapshot.GemBalance = Gem;
		return Snapshot;
	}

	// 화면 글 원문 — `SnareBalanceHud.cpp` 의 LOCTEXT 와 같아야 한다(글을 바꾸면 여기도).
	const TCHAR* const OfflineLine = TEXT("서버 연결 안 됨");
	const TCHAR* const LoadingLine = TEXT("불러오는 중…");

	FString NumberOnScreen(int32 Value)
	{
		return FText::AsNumber(Value).ToString();
	}

	/**
	 * 두 HUD 가 같은 배선을 쓰므로 검사 몸통도 하나다 — 화면마다 따로 적으면 한쪽만 고쳐지고
	 * 다른 쪽이 조용히 빠진다(이번 조각이 `SnareBalanceHud` 를 한 곳에 둔 것과 같은 이유).
	 */
	template <typename THudWidget>
	void CheckBalanceRow(FAutomationTestBase& Test, UWorld* World, const TCHAR* ScreenName)
	{
		THudWidget* const Hud = CreateWidget<THudWidget>(World, THudWidget::StaticClass());
		if (!Test.TestNotNull(FString::Printf(TEXT("%s HUD 가 만들어진다"), ScreenName), Hud))
		{
			return;
		}

		// Slate 위젯을 실제로 만들어야 RebuildWidget 이 돌고, 그때 자리표시 트리가 세워진다.
		Hud->TakeWidget();

		// 한 번 받아 두는 이유: `Hud` 의 형이 아직 안 정해진 틀 안이라 `Hud->WidgetTree->FindWidget<...>` 를
		// 바로 쓰면 컴파일러가 `<` 를 부등호로 읽는다 (`template` 키워드를 붙이는 대신 형을 먼저 굳힌다).
		UWidgetTree* const Tree = Hud->WidgetTree;
		UTextBlock* const BalanceWidget = Tree->FindWidget<UTextBlock>(TEXT("BalanceText"));
		if (!Test.TestNotNull(FString::Printf(TEXT("%s 자리표시 트리에 잔액 줄이 있다"), ScreenName), BalanceWidget))
		{
			return;
		}

		// ① 아직 아무에게도 안 물어봤다 — 0 도 빈칸 `-` 도 아니라 까닭 한 줄이 떠야 한다(M21-01d).
		Test.TestEqual(FString::Printf(TEXT("%s 백엔드 전에는 `서버 연결 안 됨`"), ScreenName),
			BalanceWidget->GetText().ToString(), FString(OfflineLine));
		Test.TestFalse(FString::Printf(TEXT("%s 백엔드 전에 숫자 0 이 없다"), ScreenName),
			BalanceWidget->GetText().ToString().Contains(TEXT("0")));

		USnareBackendSubsystem* const Backend = MakeTestBackend();
		Test.TestTrue(TEXT("검사용 스냅샷이 반영된다"),
			Backend->ApplyPlayerSnapshot(MakeTestSnapshot(TestCoinBalance, TestGemBalance), /*bFromBackend=*/false));

		Hud->BindToBackend(Backend);

		// ② 붙이는 순간 화면이 백엔드 값을 그대로 비춘다.
		const FString AfterBind = BalanceWidget->GetText().ToString();
		Test.TestTrue(FString::Printf(TEXT("%s 코인 %d 이 화면에 있다 (실제 \"%s\")"),
				ScreenName, TestCoinBalance, *AfterBind),
			AfterBind.Contains(NumberOnScreen(TestCoinBalance)));
		Test.TestTrue(FString::Printf(TEXT("%s 젬 %d 이 화면에 있다 (실제 \"%s\")"),
				ScreenName, TestGemBalance, *AfterBind),
			AfterBind.Contains(NumberOnScreen(TestGemBalance)));
		Test.TestEqual(FString::Printf(TEXT("%s 위젯 글자와 getter 가 같다"), ScreenName),
			AfterBind, Hud->GetBalanceText().ToString());

		// ③ 스냅샷이 바뀌면 화면이 따라간다 — **이번 조각의 합격 조건이다.** 화면을 다시 그리라고
		//    따로 부르지 않는다(알림 배선이 빠졌으면 여기서 운다).
		Test.TestTrue(TEXT("두 번째 스냅샷이 반영된다"),
			Backend->ApplyPlayerSnapshot(
				MakeTestSnapshot(TestSecondCoinBalance, TestSecondGemBalance), /*bFromBackend=*/true));

		const FString AfterChange = BalanceWidget->GetText().ToString();
		Test.TestTrue(FString::Printf(TEXT("%s 바뀐 코인 %d 이 화면에 있다 (실제 \"%s\")"),
				ScreenName, TestSecondCoinBalance, *AfterChange),
			AfterChange.Contains(NumberOnScreen(TestSecondCoinBalance)));
		// 0 은 미정이 아니다 — 백엔드가 "0개" 라고 답한 것이라 `-` 로 바뀌면 안 된다.
		Test.TestFalse(FString::Printf(TEXT("%s 잔액 0 은 `-` 로 바뀌지 않는다 (실제 \"%s\")"),
				ScreenName, *AfterChange),
			AfterChange.Contains(TEXT("젬 -")));
		Test.TestTrue(FString::Printf(TEXT("%s 잔액 0 은 숫자 0 으로 뜬다"), ScreenName),
			AfterChange.Contains(TEXT("젬 0")));

		// ④ 화면은 잔액을 건드리지 않는다 — 몇 번을 다시 그려도 백엔드가 든 값은 그대로다.
		Hud->RefreshDisplay();
		Hud->RefreshDisplay();
		Test.TestEqual(FString::Printf(TEXT("%s 를 다시 그려도 코인 잔액이 그대로다"), ScreenName),
			Backend->GetPlayerSnapshot().CoinBalance, TestSecondCoinBalance);
		Test.TestEqual(FString::Printf(TEXT("%s 를 다시 그려도 젬 잔액이 그대로다"), ScreenName),
			Backend->GetPlayerSnapshot().GemBalance, TestSecondGemBalance);

		// 백엔드를 떼면 다시 "모른다" 로 돌아간다 (남의 계정 잔액이 화면에 남지 않는다 — DESIGN 4.4).
		Hud->BindToBackend(nullptr);
		Test.TestEqual(FString::Printf(TEXT("%s 백엔드를 떼면 `서버 연결 안 됨` 으로 돌아간다"), ScreenName),
			BalanceWidget->GetText().ToString(), FString(OfflineLine));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBalanceHudTest, "Snarehaven.Ui.Balance.FollowsBackendSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBalanceHudTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 젬 화면을 보는 검사 — 1.0 설정(꺼짐)과 무관하게 켠다
	UWorld* const World = CreateTestWorld();

	CheckBalanceRow<UHuntHudWidget>(*this, World, TEXT("사냥"));
	CheckBalanceRow<URanchHudWidget>(*this, World, TEXT("목장"));

	DestroyTestWorld(World);
	return true;
}

// M21-01d — 잔액을 모를 때 `코인 -   젬 -` 빈칸(`captures/240`) 대신 까닭 한 줄. 곧 올 때와 안 올 때를 가른다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBalanceUnknownLineTest, "Snarehaven.Ui.Balance.UnknownSaysWhy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBalanceUnknownLineTest::RunTest(const FString& Parameters)
{
	const FScopedGemsEnabled GemsOn(true);   // 젬 화면을 보는 검사 — 1.0 설정(꺼짐)과 무관하게 켠다
	using L = ESnareBackendLoginState;
	using F = ESnareBackendFetchState;
	TestTrue(TEXT("로그인 중 → 불러오는 중"), SnareBalanceHud::IsBalanceLoading(L::InProgress, F::NotStarted));
	TestTrue(TEXT("로그인 뒤 보유 상태 왕복 전 → 불러오는 중"), SnareBalanceHud::IsBalanceLoading(L::LoggedIn, F::NotStarted));
	TestTrue(TEXT("로그인 뒤 보유 상태 왕복 중 → 불러오는 중"), SnareBalanceHud::IsBalanceLoading(L::LoggedIn, F::InProgress));
	TestFalse(TEXT("로그인 시작 안 함(오프라인) → 연결 안 됨"), SnareBalanceHud::IsBalanceLoading(L::NotStarted, F::NotStarted));
	TestFalse(TEXT("로그인 실패 → 연결 안 됨"), SnareBalanceHud::IsBalanceLoading(L::Failed, F::NotStarted));
	TestFalse(TEXT("보유 상태 왕복 실패 → 연결 안 됨"), SnareBalanceHud::IsBalanceLoading(L::LoggedIn, F::Failed));

	TestEqual(TEXT("백엔드 없음 → 연결 안 됨"), SnareBalanceHud::FormatBalanceText(nullptr).ToString(), FString(OfflineLine));
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	const FString Fresh = SnareBalanceHud::FormatBalanceText(Backend).ToString();
	TestEqual(TEXT("로그인 안 붙은 백엔드(-SnareOffline 과 같은 상태) → 연결 안 됨"), Fresh, FString(OfflineLine));
	TestFalse(TEXT("모를 때 글에 빈칸 `-` 가 없다"), Fresh.Contains(TEXT("-")));
	AddInfo(FString::Printf(TEXT("[Balance] 모름 '%s' · 오는 중 '%s'"), *Fresh, LoadingLine));
	return true;
}

#endif
