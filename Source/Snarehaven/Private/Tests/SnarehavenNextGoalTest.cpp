// INBOX M22 ⑤ · DESIGN 8.14 「다음 목표 줄」 2층 검사 — 첫 실행 안내 뒤 HUD 다음 목표 한 줄(`USnareNextGoalWidget`).
// 잡는 것: ① 묶으면 `GetQuests` 를 한 번 묻는다 ② 줄 = 안 끝낸 첫 의뢰(모으는 중 / 건네기 / 전부 끝)
// ③ 보유 상태가 바뀌면 다시 묻는다 · 거절 답엔 앞 줄을 지우지 않는다 ④ 코인·배낭을 안 바꾼다 ⑤ 안내가 끝나면 `OnFinished`.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.Quest.NextGoal; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Backend/SnareQuestChain.h"
#include "Engine/DataTable.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/SnareFirstRunGuideWidget.h"
#include "UI/SnareNextGoalWidget.h"

namespace
{
	// 검사 전용 값 (Verification.md 6).
	const TCHAR* GoalReplyJson = TEXT("{\"day\":1,\"quests\":["
		"{\"id\":\"item.wood\",\"itemId\":\"item.wood\",\"count\":10,\"reward\":5,\"done\":false},"
		"{\"id\":\"item.carrot\",\"itemId\":\"item.carrot\",\"count\":3,\"reward\":5,\"done\":false}]}");
	const TCHAR* GoalWoodDoneJson = TEXT("{\"day\":1,\"quests\":["
		"{\"id\":\"item.wood\",\"itemId\":\"item.wood\",\"count\":10,\"reward\":5,\"done\":true},"
		"{\"id\":\"item.carrot\",\"itemId\":\"item.carrot\",\"count\":3,\"reward\":5,\"done\":false}]}");
	const TCHAR* GoalAllDoneJson = TEXT("{\"day\":1,\"quests\":["
		"{\"id\":\"item.wood\",\"itemId\":\"item.wood\",\"count\":10,\"reward\":5,\"done\":true}]}");
	constexpr int32 TestCoins = 7;
	constexpr int32 TestWood = 12;
	constexpr int32 TestCarrot = 2;

	TSharedPtr<FJsonObject> ParseGoalJson(const TCHAR* Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenNextGoalFormatTest, "Snarehaven.Quest.NextGoal.FormatRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenNextGoalFormatTest::RunTest(const FString& Parameters)
{
	TArray<FQuestLineView> Lines;
	FQuestLineView Done;
	Done.ItemID = TEXT("item.pike");
	Done.Count = 1;
	Done.bDone = true;
	FQuestLineView Carrot;
	Carrot.ItemID = TEXT("item.carrot");
	Carrot.Count = 3;
	Carrot.Reward = 5;
	Lines = { Done, Carrot };

	int32 Owned = TestCarrot;
	auto Count = [&Owned](FName) { return Owned; };
	auto Name = [](FName ItemID) { return FText::FromString(ItemID == TEXT("item.carrot") ? TEXT("당근") : TEXT("?")); };

	const FString NoReply = USnareNextGoalWidget::FormatGoal(false, Lines, Count, Name).ToString();
	TestEqual(TEXT("답 전"), NoReply, FString(TEXT("다음 목표: 의뢰인 솔방울에게 오늘 의뢰를 받아요")));
	const FString Collect = USnareNextGoalWidget::FormatGoal(true, Lines, Count, Name).ToString();
	TestEqual(TEXT("끝낸 줄은 건너뛰고 모으는 중"), Collect, FString(TEXT("다음 목표: 당근 3개 모으기 (2/3)")));
	Owned = 3;
	const FString HandIn = USnareNextGoalWidget::FormatGoal(true, Lines, Count, Name).ToString();
	TestEqual(TEXT("다 모으면 건네기"), HandIn, FString(TEXT("다음 목표: 의뢰인 솔방울에게 당근 3개 건네기 > 5코인")));
	const FString AllDone = USnareNextGoalWidget::FormatGoal(true, { Done }, Count, Name).ToString();
	TestEqual(TEXT("전부 끝"), AllDone, FString(TEXT("다음 목표: 오늘 의뢰 끝! 내일 새 의뢰가 와요")));
	AddInfo(FString::Printf(TEXT("줄: %s / %s / %s / %s"), *NoReply, *Collect, *HandIn, *AllDone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenNextGoalServerTest, "Snarehaven.Quest.NextGoal.FollowsServerList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenNextGoalServerTest::RunTest(const FString& Parameters)
{
	TArray<FString> Calls;
	TArray<USnareBackendSubsystem::FServerCallDone> Pending;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls, &Pending](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			Calls.Add(FunctionName);
			Pending.Add(MoveTemp(OnDone));
		};
	auto Answer = [&Pending](const TCHAR* Json)
	{
		if (Pending.Num() > 0)
		{
			USnareBackendSubsystem::FServerCallDone Done = MoveTemp(Pending[0]);
			Pending.RemoveAt(0);
			Done(true, ParseGoalJson(Json), FString());
		}
	};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	USnareNextGoalWidget* const Goal = CreateWidget<USnareNextGoalWidget>(World, USnareNextGoalWidget::StaticClass());
	Goal->MinRefreshSeconds = 0.f;
	TSharedPtr<SWidget> Slate = Goal->TakeWidget();

	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.CoinBalance = TestCoins;
	Snapshot.AddItem(TEXT("item.wood"), TestWood);
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);

	// ① 묶으면 한 번 묻는다. 답 전엔 의뢰인을 찾아가라는 줄.
	Goal->BindToBackend(Backend);
	TestEqual(TEXT("묶으면 서버를 한 번 부른다"), Calls.Num(), 1);
	TestEqual(TEXT("목록 함수"), Calls.Num() > 0 ? Calls[0] : FString(), FString(TEXT("GetQuests")));
	TestFalse(TEXT("답 전"), Goal->HasReply());
	TestTrue(TEXT("답 전 줄"), Goal->GetGoalText().ToString().Contains(TEXT("오늘 의뢰를 받아요")));

	// ② 나무 10 의뢰 — 가진 12 라 건네기 줄.
	Answer(GoalReplyJson);
	const FString HandIn = Goal->GetGoalText().ToString();
	AddInfo(FString::Printf(TEXT("답 뒤 줄: %s"), *HandIn));
	TestTrue(TEXT("가진 게 넉넉하면 건네기"), HandIn.Contains(TEXT("건네기")) && HandIn.Contains(TEXT("5코인")));

	// ③ 서버가 나무 의뢰를 끝냈다 → 보유 상태가 바뀜 → 다시 묻는다. 당근 0/3 모으는 중.
	FSnarePlayerSnapshot After = Snapshot;
	After.InventorySlots.Reset();
	After.AddItem(TEXT("item.wood"), TestWood - 10);
	Backend->ApplyPlayerSnapshot(After, /*bFromBackend=*/true);
	TestEqual(TEXT("보유 상태가 바뀌면 다시 묻는다"), Calls.Num(), 2);
	Answer(GoalWoodDoneJson);
	const FString Collect = Goal->GetGoalText().ToString();
	AddInfo(FString::Printf(TEXT("나무 끝낸 뒤 줄: %s"), *Collect));
	TestTrue(TEXT("다음 안 끝낸 줄 = 당근 모으기"), Collect.Contains(TEXT("(0/3)")));

	// 거절 답은 앞 줄을 지우지 않는다.
	Backend->ApplyPlayerSnapshot(After, /*bFromBackend=*/true);
	if (Pending.Num() > 0)
	{
		USnareBackendSubsystem::FServerCallDone Done = MoveTemp(Pending[0]);
		Pending.RemoveAt(0);
		Done(true, ParseGoalJson(TEXT("{\"error\":\"잠시 뒤에\",\"code\":\"busy\"}")), FString());
	}
	TestEqual(TEXT("거절 답엔 줄 그대로"), Goal->GetGoalText().ToString(), Collect);
	TestFalse(TEXT("거절 뒤 대기 풀림"), Goal->IsRequestPending());

	// 전부 끝.
	Backend->ApplyPlayerSnapshot(After, /*bFromBackend=*/true);
	Answer(GoalAllDoneJson);
	TestTrue(TEXT("전부 끝"), Goal->GetGoalText().ToString().Contains(TEXT("오늘 의뢰 끝")));

	// ④ 줄은 코인·배낭을 바꾸지 않는다 · 의뢰 완료를 부르지 않는다.
	TestEqual(TEXT("코인 그대로"), Backend->GetPlayerSnapshot().CoinBalance, TestCoins);
	TestFalse(TEXT("CompleteQuest 를 부르지 않는다"), Calls.Contains(TEXT("CompleteQuest")));

	// ⑤ 안내를 건너뛰면 끝 알림이 온다(캐릭터가 이걸 듣고 줄을 띄운다).
	USnareFirstRunGuideWidget* const Guide = CreateWidget<USnareFirstRunGuideWidget>(World, USnareFirstRunGuideWidget::StaticClass());
	Guide->bPersistSeen = false;
	int32 Finished = 0;
	Guide->OnFinished.AddLambda([&Finished]() { ++Finished; });
	Guide->Advance();
	TestEqual(TEXT("중간 장에선 끝 알림 없음"), Finished, 0);
	Guide->Skip();
	TestEqual(TEXT("건너뛰면 끝 알림 한 번"), Finished, 1);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	Slate.Reset();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenNextGoalChainTest, "Snarehaven.Quest.NextGoal.ChainLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M105-05: 하루 의뢰 남음 → 기존 줄 그대로 · 다 끝남 → 줄기 글 · 줄기도 끝 → 줄 접힘(빈 글).
bool FSnarehavenNextGoalChainTest::RunTest(const FString& Parameters)
{
	FQuestLineView Open;
	Open.ItemID = TEXT("item.carrot");
	Open.Count = 3;
	Open.Reward = 5;
	FQuestLineView Done = Open;
	Done.bDone = true;
	int32 Owned = 0;
	auto Count = [&Owned](FName) { return Owned; };
	auto Name = [](FName ItemID) { return FText::FromName(ItemID); };
	const UDataTable* const Table = SnareQuestChain::LoadTable();
	if (!TestNotNull(TEXT("DT_QuestChain"), Table))
	{
		return false;
	}
	const int32 Steps = SnareQuestChain::StepCount(Table);

	const FString Daily = USnareNextGoalWidget::FormatGoal(true, { Open }, Count, Name, 0).ToString();
	TestTrue(TEXT("하루 의뢰가 남으면 기존 줄 그대로"), Daily.Contains(TEXT("모으기 (0/3)")) && !Daily.Contains(TEXT("부탁")));

	const FString Collect = USnareNextGoalWidget::FormatGoal(true, { Done }, Count, Name, 0).ToString();
	TestTrue(TEXT("다 끝나면 줄기 1단계 모으기"), Collect.Contains(TEXT("다음 목표: ")) && Collect.Contains(TEXT("모으기")) && !Collect.Contains(TEXT("오늘 의뢰 끝")));

	Owned = 100000;
	const FString HandIn = USnareNextGoalWidget::FormatGoal(true, { Done }, Count, Name, 0).ToString();
	TestTrue(TEXT("재료를 다 모으면 건네기"), HandIn.Contains(TEXT("건네기")) && HandIn.Contains(TEXT("코인")));

	const FString Unknown = USnareNextGoalWidget::FormatGoal(true, { Done }, Count, Name, -1).ToString();
	TestTrue(TEXT("줄기 단계를 모르면 옛 글"), Unknown.Contains(TEXT("오늘 의뢰 끝")));

	const FText Finished = USnareNextGoalWidget::FormatGoal(true, { Done }, Count, Name, Steps);
	TestTrue(TEXT("줄기도 끝이면 접힘(빈 글)"), Finished.IsEmpty());
	AddInfo(FString::Printf(TEXT("줄: %s / %s / %s"), *Daily, *Collect, *HandIn));
	return true;
}

#endif
