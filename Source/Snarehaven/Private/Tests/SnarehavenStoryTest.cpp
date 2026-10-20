// 메인 스토리 화면 규칙(`SnareStory`) 2층 검사 — 서버 `GetStory` 답 읽기와 「지금 할 일」 한 줄.
// 잡는 것: ① 25단계 목표 글이 전부 있고 ② 재료 건네기 단계는 `{0}` 자리가 있으며 ③ 답을 읽어 한 줄이 맞게 나오고
// ④ 오류 답은 읽지 않고 ⑤ 끝난 계정은 완료 글이 나온다. 밸런스 숫자에 기대지 않는다(Verification.md 6).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareStory.h"
#include "Engine/Engine.h"
#include "Engine/DataTable.h"
#include "Data/ItemRow.h"
#include "StaticMeshResources.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/EquipmentComponent.h"
#include "Materials/MaterialInterface.h"
#include "UI/SnareNextGoalWidget.h"
#include "World/SnareLighthouseActor.h"
#include "World/SnareCampCageActor.h"
#include "World/SnareHarpoonCampActor.h"
#include "World/SnareHookNpcActor.h"
#include "Player/PlayerBodyAnimInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UI/SnareStoryChapterCardWidget.h"
#include "UI/SnareStoryDialogueWidget.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "UI/SnareStoryGoalWidget.h"
#include "Serialization/JsonReader.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Serialization/JsonSerializer.h"
#include "Data/ZoneRow.h"
#include "World/SnareZoneRules.h"
#include "World/ShopVendorActor.h"
#include "World/QuestGiverActor.h"
#include "SnarehavenCharacter.h"
#include "Combat/MeleeAttackComponent.h"

namespace
{
	TSharedPtr<FJsonObject> Parse(const TCHAR* Json)
	{
		TSharedPtr<FJsonObject> Obj;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Json)), Obj);
		return Obj;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryLineTest, "Snarehaven.Story.GoalLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryLineTest::RunTest(const FString& Parameters)
{
	using namespace SnareStory;
	const auto Name = [](FName Id) { return FText::FromName(Id); };

	for (int32 Step = 1; Step <= TotalSteps; ++Step)
	{
		TestFalse(*FString::Printf(TEXT("%d단계 목표 글"), Step), StepGoal(Step).IsEmpty());
	}
	TestTrue(TEXT("범위 밖은 빈 글"), StepGoal(0).IsEmpty() && StepGoal(TotalSteps + 1).IsEmpty());
	for (int32 Chapter = 1; Chapter <= TotalSteps / StepsPerChapter; ++Chapter)
	{
		TestFalse(*FString::Printf(TEXT("%d장 제목"), Chapter), ChapterTitle(Chapter).IsEmpty());
	}

	FStoryView View;
	TestTrue(TEXT("세기 단계 읽힘"), ParseView(Parse(TEXT("{\"step\":1,\"total\":25,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":2,\"kind\":\"count\",\"have\":0,\"need\":1}}")), View));
	TestEqual(TEXT("지금 단계"), View.CurrentStep, 2);
	TestEqual(TEXT("한 줄(개수 1이면 괄호 없음)"), FormatLine(View, Name).ToString(), FString(TEXT("지금 할 일: 손도끼 만들기 · 1장 2/5")));

	TestTrue(TEXT("세기 2"), ParseView(Parse(TEXT("{\"step\":6,\"total\":25,\"done\":false,\"chapter\":2,\"titles\":[],\"current\":{\"step\":7,\"kind\":\"count\",\"have\":2,\"need\":3}}")), View));
	TestEqual(TEXT("한 줄(진행 개수)"), FormatLine(View, Name).ToString(), FString(TEXT("지금 할 일: 목장 수확 3번 하기 (2/3) · 2장 2/5")));

	TestTrue(TEXT("건네기 읽힘"), ParseView(Parse(TEXT("{\"step\":2,\"total\":25,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":3,\"kind\":\"give\",\"have\":0,\"need\":1,\"needs\":[{\"itemId\":\"item.t_a\",\"count\":30},{\"itemId\":\"item.t_b\",\"count\":10}]}}")), View));
	TestEqual(TEXT("재료 두 줄"), View.Needs.Num(), 2);
	TestEqual(TEXT("한 줄(건네기)"), FormatLine(View, Name).ToString(), FString(TEXT("지금 할 일: 솔방울에게 item.t_a 30개, item.t_b 10개 건네기 · 1장 3/5")));

	// 의뢰인 머리 위 표식: 건넬 재료 모자람 「!」 · 다 모음/말 걸기 「?」 · 세기 단계 없음.
	TestEqual(TEXT("건네기 재료 모자람 = 「!」"), GiverMark(View), EGiverMark::Bang);
	View.Have = View.Need;
	TestEqual(TEXT("건네기 재료 다 모음 = 「?」"), GiverMark(View), EGiverMark::Question);
	TestTrue(TEXT("말 걸기 읽힘"), ParseView(Parse(TEXT("{\"step\":9,\"total\":25,\"done\":false,\"chapter\":2,\"titles\":[],\"current\":{\"step\":10,\"kind\":\"report\",\"have\":0,\"need\":1}}")), View));
	TestEqual(TEXT("말 걸기 = 「?」"), GiverMark(View), EGiverMark::Question);
	TestTrue(TEXT("세기 읽힘"), ParseView(Parse(TEXT("{\"step\":1,\"total\":25,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":2,\"kind\":\"count\",\"have\":0,\"need\":1}}")), View));
	TestEqual(TEXT("세기 단계는 표식 없음"), GiverMark(View), EGiverMark::None);
	TestTrue(TEXT("표식 글자 없음·!·?"), GiverMarkText(EGiverMark::None).IsEmpty() && GiverMarkText(EGiverMark::Bang).ToString() == TEXT("!") && GiverMarkText(EGiverMark::Question).ToString() == TEXT("?"));

	TestFalse(TEXT("오류 답은 안 읽음"), ParseView(Parse(TEXT("{\"error\":\"x\",\"code\":\"y\"}")), View));
	TestFalse(TEXT("빈 답은 안 읽음"), ParseView(nullptr, View));

	TestTrue(TEXT("끝난 계정 읽힘"), ParseView(Parse(TEXT("{\"step\":25,\"total\":25,\"done\":true,\"chapter\":5,\"titles\":[\"올가미지기\"],\"current\":null}")), View));
	TestTrue(TEXT("칭호"), View.Titles.Contains(TEXT("올가미지기")));
	TestEqual(TEXT("끝난 계정은 표식 없음"), GiverMark(View), EGiverMark::None);
	TestFalse(TEXT("완료 글"), FormatLine(View, Name).ToString().StartsWith(TEXT("지금 할 일")));
	return true;
}

// 왼쪽 위 「지금 할 일」 위젯 — 묶으면 `GetStory` 를 한 번 묻고, 답으로 줄이 나오며, 거절 답엔 앞 줄이 그대로고,
// 보유 상태가 바뀌면 다시 묻고, 코인을 바꾸지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryWidgetTest, "Snarehaven.Story.GoalWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryWidgetTest::RunTest(const FString& Parameters)
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
			Done(true, Parse(Json), FString());
		}
	};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	USnareStoryGoalWidget* const Goal = CreateWidget<USnareStoryGoalWidget>(World, USnareStoryGoalWidget::StaticClass());
	Goal->MinRefreshSeconds = 0.f;
	TSharedPtr<SWidget> Slate = Goal->TakeWidget();

	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.CoinBalance = 7;
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);

	TArray<bool> Shown;
	Goal->OnLineShown.AddLambda([&Shown](bool bShown) { Shown.Add(bShown); });
	Goal->BindToBackend(Backend);
	TestEqual(TEXT("묶으면 서버를 한 번 부른다"), Calls.Num(), 1);
	TestEqual(TEXT("함수 이름"), Calls.Num() > 0 ? Calls[0] : FString(), FString(TEXT("GetStory")));
	TestTrue(TEXT("답 전엔 줄이 접혀 있다"), Goal->GetGoalText().IsEmpty());
	TestFalse(TEXT("접힌 줄은 누를 수 없다"), Goal->NotifyTapped());

	int32 ViewCount = 0;
	Goal->OnViewChanged.AddLambda([&ViewCount](const SnareStory::FStoryView&) { ++ViewCount; });
	Answer(TEXT("{\"step\":1,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":2,\"kind\":\"count\",\"have\":0,\"need\":1}}"));
	const FString Line = Goal->GetGoalText().ToString();
	AddInfo(FString::Printf(TEXT("답 뒤 줄: %s"), *Line));
	TestTrue(TEXT("줄이 나온다"), Line.StartsWith(TEXT("지금 할 일")) && Line.Contains(TEXT("1장")));
	TestEqual(TEXT("답을 읽으면 표식 쪽에 알린다"), ViewCount, 1);
	TestTrue(TEXT("줄이 뜨면 알린다(옛 다음 목표 줄을 접는 신호)"), Shown.Num() > 0 && Shown.Last());

	int32 TapCount = 0;
	Goal->OnTapped.AddLambda([&TapCount]() { ++TapCount; });
	TestTrue(TEXT("줄이 펴지면 누를 수 있다"), Goal->IsTappable());
	TestTrue(TEXT("누르면 알린다"), Goal->NotifyTapped());
	TestEqual(TEXT("누름 1번"), TapCount, 1);

	USnareNextGoalWidget* const Old = CreateWidget<USnareNextGoalWidget>(World, USnareNextGoalWidget::StaticClass());
	Old->SetSuppressed(true);
	TestTrue(TEXT("옛 줄을 접을 수 있다"), Old->IsSuppressed());

	// 보유 상태가 바뀌면 다시 묻는다. 거절 답엔 앞 줄을 지우지 않는다.
	FSnarePlayerSnapshot After = Snapshot;
	After.CoinBalance = 9;
	Backend->ApplyPlayerSnapshot(After, /*bFromBackend=*/true);
	TestEqual(TEXT("보유 상태가 바뀌면 다시 묻는다"), Calls.Num(), 2);
	Answer(TEXT("{\"error\":\"잠시 뒤에\",\"code\":\"busy\"}"));
	TestEqual(TEXT("거절 답엔 줄 그대로"), Goal->GetGoalText().ToString(), Line);
	TestEqual(TEXT("거절 답엔 표식 쪽에 알리지 않는다"), ViewCount, 1);
	TestFalse(TEXT("거절 뒤 대기 풀림"), Goal->IsRequestPending());

	TestEqual(TEXT("코인 그대로"), Backend->GetPlayerSnapshot().CoinBalance, 9);
	TestFalse(TEXT("AdvanceStory 를 부르지 않는다"), Calls.Contains(TEXT("AdvanceStory")));

	USnareBackendSubsystem::TestServerOverride = nullptr;
	Slate.Reset();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryDialogueTest, "Snarehaven.Story.Dialogue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryDialogueTest::RunTest(const FString& Parameters)
{
	using namespace SnareStory;
	for (int32 Step = 1; Step <= TotalSteps; ++Step)
	{
		const TArray<FDialogueLine> Lines = StepDialogue(Step);
		TestTrue(*FString::Printf(TEXT("%d단계 대사 1~3줄"), Step), Lines.Num() >= 1 && Lines.Num() <= 3);
		for (const FDialogueLine& Line : Lines)
		{
			TestFalse(*FString::Printf(TEXT("%d단계 말하는 사람 있음"), Step), Line.Speaker.IsEmpty());
			TestFalse(*FString::Printf(TEXT("%d단계 말 있음"), Step), Line.Text.IsEmpty());
		}
	}
	TestEqual(TEXT("0단계는 빈 배열"), StepDialogue(0).Num(), 0);
	TestEqual(TEXT("26단계는 빈 배열"), StepDialogue(TotalSteps + 1).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryDialogueWidgetTest, "Snarehaven.Story.DialogueWidget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryDialogueWidgetTest::RunTest(const FString& Parameters)
{
	const int32 OriginalSeen = USnareStoryDialogueWidget::GetSeenStep();
	USnareStoryDialogueWidget::SetSeenStep(3);
	TestFalse(TEXT("본 단계는 다시 안 띄운다"), USnareStoryDialogueWidget::ShouldShow(3));
	TestTrue(TEXT("새 단계는 띄운다"), USnareStoryDialogueWidget::ShouldShow(4) || FParse::Param(FCommandLine::Get(), TEXT("SnareNoGuide")));
	TestFalse(TEXT("0단계는 안 띄운다"), USnareStoryDialogueWidget::ShouldShow(0));

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	{
		USnareStoryDialogueWidget* const Panel = CreateWidget<USnareStoryDialogueWidget>(World, USnareStoryDialogueWidget::StaticClass());
		if (TestNotNull(TEXT("대사 판"), Panel))
		{
			Panel->bPersistSeen = false;
			TestFalse(TEXT("범위 밖 단계는 못 연다"), Panel->ShowStep(0));
			const int32 Count = SnareStory::StepDialogue(4).Num();
			TestTrue(TEXT("4단계를 연다"), Panel->ShowStep(4));
			TestEqual(TEXT("줄 수가 대사 함수와 같다"), Panel->GetLineCount(), Count);
			bool bFinished = false;
			Panel->OnFinished.AddLambda([&bFinished]() { bFinished = true; });
			for (int32 i = 0; i < Count - 1; ++i)
			{
				Panel->Advance();
				TestFalse(TEXT("마지막 줄 전엔 안 끝난다"), bFinished);
			}
			Panel->Advance();
			TestTrue(TEXT("마지막 줄에서 누르면 끝난다"), bFinished && Panel->IsFinished());
			TestEqual(TEXT("저장 끈 모드는 본 단계를 안 남긴다"), USnareStoryDialogueWidget::GetSeenStep(), 3);

			USnareStoryDialogueWidget* const Saved = CreateWidget<USnareStoryDialogueWidget>(World, USnareStoryDialogueWidget::StaticClass());
			if (TestNotNull(TEXT("저장할 판"), Saved) && TestTrue(TEXT("5단계를 연다"), Saved->ShowStep(5)))
			{
				const int32 SavedCount = Saved->GetLineCount();
				for (int32 i = 0; i < SavedCount; ++i)
				{
					Saved->Advance();
				}
				TestEqual(TEXT("끝나면 본 단계를 남긴다"), USnareStoryDialogueWidget::GetSeenStep(), 5);
			}
		}
	}
	USnareStoryDialogueWidget::SetSeenStep(OriginalSeen);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryChapterCardTest, "Snarehaven.Story.ChapterCard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryChapterCardTest::RunTest(const FString& Parameters)
{
	using namespace SnareStory;
	FStoryView View;
	View.Step = 4;
	TestEqual(TEXT("4단계는 아직 끝낸 장 없음"), CompletedChapter(View), 0);
	View.Step = 5;
	TestEqual(TEXT("5단계면 1장"), CompletedChapter(View), 1);
	View.Step = 24;
	TestEqual(TEXT("24단계면 4장"), CompletedChapter(View), 4);
	View.Step = TotalSteps;
	TestEqual(TEXT("25단계면 5장"), CompletedChapter(View), 5);
	int32 Sum = 0;
	for (int32 Chapter = 1; Chapter <= 5; ++Chapter)
	{
		Sum += ChapterCoins(Chapter);
		TestFalse(*FString::Printf(TEXT("%d장 보상 이름"), Chapter), ChapterRewardItemName(Chapter).IsEmpty());
	}
	TestEqual(TEXT("장 끝 코인 합계 200 (STORY.md 5절)"), Sum, 200);
	TestEqual(TEXT("범위 밖 코인 0"), ChapterCoins(6), 0);

	const int32 OriginalSeen = USnareStoryChapterCardWidget::GetSeenChapter();
	USnareStoryChapterCardWidget::SetSeenChapter(1);
	TestFalse(TEXT("본 장은 다시 안 띄운다"), USnareStoryChapterCardWidget::ShouldShow(1));
	TestTrue(TEXT("새 장은 띄운다"), USnareStoryChapterCardWidget::ShouldShow(2) || FParse::Param(FCommandLine::Get(), TEXT("SnareNoGuide")));
	TestFalse(TEXT("끝낸 장 없으면 안 띄운다"), USnareStoryChapterCardWidget::ShouldShow(0));

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	{
		USnareStoryChapterCardWidget* const Card = CreateWidget<USnareStoryChapterCardWidget>(World, USnareStoryChapterCardWidget::StaticClass());
		if (TestNotNull(TEXT("장 끝 카드"), Card))
		{
			Card->bPersistSeen = false;
			TestFalse(TEXT("범위 밖 장은 못 연다"), Card->ShowChapter(0));
			TestTrue(TEXT("2장을 연다"), Card->ShowChapter(2));
			TestTrue(TEXT("보상 글에 코인 30"), Card->GetRewardText().ToString().Contains(TEXT("30")));
			Card->Close();
			TestTrue(TEXT("누르면 닫힌다"), Card->IsFinished());
			TestEqual(TEXT("저장 끈 모드는 본 장을 안 남긴다"), USnareStoryChapterCardWidget::GetSeenChapter(), 1);
		}
	}
	USnareStoryChapterCardWidget::SetSeenChapter(OriginalSeen);
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryAutoAdvanceTest, "Snarehaven.Story.AutoAdvance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryAutoAdvanceTest::RunTest(const FString& Parameters)
{
	using namespace SnareStory;
	FStoryView View;
	View.CurrentStep = 4;
	View.Kind = TEXT("count");
	View.Need = 1;
	View.Have = 0;
	TestFalse(TEXT("덜 채운 세기 단계는 안 넘김"), ShouldAutoAdvance(View));
	View.Have = 1;
	TestTrue(TEXT("채운 세기 단계는 넘김"), ShouldAutoAdvance(View));
	View.Kind = TEXT("ranch");
	TestTrue(TEXT("채운 목장 단계는 넘김"), ShouldAutoAdvance(View));
	View.Kind = TEXT("give");
	TestFalse(TEXT("건네기는 사람이 건넨다"), ShouldAutoAdvance(View));
	View.Kind = TEXT("report");
	TestFalse(TEXT("보고 단계는 안 넘김"), ShouldAutoAdvance(View));
	TestTrue(TEXT("보고 단계는 대사를 읽으면 알림"), ShouldReport(View));
	View.Kind = TEXT("count");
	TestFalse(TEXT("세기 단계는 보고 아님"), ShouldReport(View));
	View.Kind = TEXT("report");
	View.Kind = TEXT("give");
	TestTrue(TEXT("건네기 단계는 대사를 읽으면 건넴"), ShouldGive(View));
	TestFalse(TEXT("건네기는 보고 아님"), ShouldReport(View));
	View.Kind = TEXT("count");
	TestFalse(TEXT("세기는 건네기 아님"), ShouldGive(View));
	View.bDone = true;
	TestFalse(TEXT("끝난 계정은 안 넘김"), ShouldAutoAdvance(View));

	TArray<FString> Calls;
	double SentStep = -1.0;
	bool bSentReport = false;
	TArray<USnareBackendSubsystem::FServerCallDone> Pending;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls, &Pending, &SentStep, &bSentReport](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			Calls.Add(FunctionName);
			if (FunctionName == TEXT("AdvanceStory") && Args.IsValid())
			{
				Args->TryGetNumberField(TEXT("step"), SentStep);
				Args->TryGetBoolField(TEXT("report"), bSentReport);
			}
			Pending.Add(MoveTemp(OnDone));
		};
	auto Answer = [&Pending](const TCHAR* Json)
	{
		if (Pending.Num() > 0)
		{
			USnareBackendSubsystem::FServerCallDone Done = MoveTemp(Pending[0]);
			Pending.RemoveAt(0);
			Done(true, Parse(Json), FString());
		}
	};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	USnareStoryGoalWidget* const Goal = CreateWidget<USnareStoryGoalWidget>(World, USnareStoryGoalWidget::StaticClass());
	Goal->MinRefreshSeconds = 0.f;
	TSharedPtr<SWidget> Slate = Goal->TakeWidget();
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	Goal->BindToBackend(Backend);
	Answer(TEXT("{\"step\":3,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":4,\"kind\":\"count\",\"have\":0,\"need\":1}}"));
	TestFalse(TEXT("덜 채웠으면 AdvanceStory 안 부름"), Calls.Contains(TEXT("AdvanceStory")));

	Goal->RequestStory();
	Answer(TEXT("{\"step\":3,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":4,\"kind\":\"count\",\"have\":1,\"need\":1}}"));
	TestTrue(TEXT("채웠으면 AdvanceStory 를 부른다"), Calls.Contains(TEXT("AdvanceStory")));
	TestEqual(TEXT("지금 단계 번호를 같이 보낸다"), SentStep, 4.0);
	TestTrue(TEXT("답 기다리는 중"), Goal->IsAdvancePending());
	TestFalse(TEXT("기다리는 동안 또 부르지 않는다"), Goal->RequestAdvance());
	Answer(TEXT("{\"step\":4,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":5,\"kind\":\"ranch\",\"have\":0,\"need\":1}}"));
	TestFalse(TEXT("답이 오면 대기 풀림"), Goal->IsAdvancePending());
	TestEqual(TEXT("답으로 줄이 다음 단계가 된다"), Goal->GetView().CurrentStep, 5);

	// 보고 단계: 읽은 단계가 같을 때만 report:true 로 부른다
	Goal->RequestStory();
	Answer(TEXT("{\"step\":0,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":1,\"kind\":\"report\",\"have\":0,\"need\":1}}"));
	TestFalse(TEXT("보고 단계는 저절로 안 넘김"), Goal->IsAdvancePending());
	TestFalse(TEXT("다른 단계 대사를 읽은 건 무시"), Goal->RequestReport(2));
	TestTrue(TEXT("같은 단계 대사를 읽으면 부른다"), Goal->RequestReport(1));
	TestTrue(TEXT("report:true 를 보낸다"), bSentReport);
	Answer(TEXT("{\"step\":1,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":2,\"kind\":\"count\",\"have\":0,\"need\":1}}"));
	TestEqual(TEXT("보고 뒤 줄이 다음 단계"), Goal->GetView().CurrentStep, 2);

	// 건네기 단계: 읽으면 report 없이 부른다
	Goal->RequestStory();
	Answer(TEXT("{\"step\":2,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":3,\"kind\":\"give\",\"have\":0,\"need\":1}}"));
	bSentReport = false;
	TestTrue(TEXT("건네기 단계 대사를 읽으면 부른다"), Goal->RequestReport(3));
	TestFalse(TEXT("건네기는 report 를 안 보낸다"), bSentReport);
	Answer(TEXT("{\"error\":\"story_short\"}"));
	TestEqual(TEXT("재료가 모자라 거절되면 줄 그대로"), Goal->GetView().CurrentStep, 3);
	Calls.Reset();
	Goal->RequestStory();
	Answer(TEXT("{\"step\":2,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":3,\"kind\":\"give\",\"have\":0,\"need\":1}}"));
	TestFalse(TEXT("아직 모자라면 다시 안 건넨다"), Calls.Contains(TEXT("AdvanceStory")));
	Goal->RequestStory();
	Answer(TEXT("{\"step\":2,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":3,\"kind\":\"give\",\"have\":1,\"need\":1}}"));
	TestTrue(TEXT("대사를 읽은 뒤 재료를 모으면 다시 건넨다"), Calls.Contains(TEXT("AdvanceStory")));
	Answer(TEXT("{\"step\":3,\"done\":false,\"chapter\":1,\"titles\":[],\"current\":{\"step\":4,\"kind\":\"count\",\"have\":0,\"need\":1}}"));
	TestEqual(TEXT("건넨 뒤 줄이 다음 단계"), Goal->GetView().CurrentStep, 4);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	Slate.Reset();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryFogbuckStandTest, "Snarehaven.Story.FogbuckStand",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryFogbuckStandTest::RunTest(const FString& Parameters)
{
	// 안개뿔은 22(언덕 오르기)·23(포획) 단계에서만 서고, 잡은 뒤(24~)와 그 전엔 안 선다.
	TestFalse(TEXT("21단계엔 안 선다"), SnareStory::ShouldStandFogbuck(21));
	TestTrue(TEXT("22단계엔 선다"), SnareStory::ShouldStandFogbuck(22));
	TestTrue(TEXT("23단계엔 선다"), SnareStory::ShouldStandFogbuck(23));
	TestFalse(TEXT("24단계엔 안 선다"), SnareStory::ShouldStandFogbuck(24));
	TestFalse(TEXT("답 전(0)엔 안 선다"), SnareStory::ShouldStandFogbuck(0));

	const int32 Before = SnareStory::GetKnownCurrentStep();
	SnareStory::SetKnownCurrentStep(23);
	TestEqual(TEXT("받은 단계를 기억한다"), SnareStory::GetKnownCurrentStep(), 23);
	SnareStory::SetKnownCurrentStep(Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryLighthouseRingTest, "Snarehaven.Story.LighthouseRings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryLighthouseRingTest::RunTest(const FString& Parameters)
{
	// 등대 불빛 고리 = 끝낸 장 수 (0~5). 끝낸 계정은 5.
	SnareStory::FStoryView View;
	View.Step = 4;
	TestEqual(TEXT("4단계 — 고리 0"), SnareStory::LighthouseRingsLit(View), 0);
	View.Step = 10;
	TestEqual(TEXT("10단계 — 고리 2"), SnareStory::LighthouseRingsLit(View), 2);
	View.Step = SnareStory::TotalSteps;
	View.bDone = true;
	TestEqual(TEXT("끝냄 — 고리 5"), SnareStory::LighthouseRingsLit(View), 5);

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	USnareStoryGoalWidget* const Goal = CreateWidget<USnareStoryGoalWidget>(World, USnareStoryGoalWidget::StaticClass());
	TSharedPtr<SWidget> Slate = Goal->TakeWidget();
	TestEqual(TEXT("답 전엔 켜진 칸 0"), Goal->CountLitRingPips(), 0);
	TestTrue(TEXT("답 받음"), Goal->ApplyStoryReply(Parse(TEXT("{\"step\":10,\"done\":false,\"chapter\":3,\"current\":{\"step\":11,\"kind\":\"count\",\"have\":0,\"need\":1}}"))));
	TestEqual(TEXT("10단계면 켜진 칸 2"), Goal->CountLitRingPips(), 2);

	Slate.Reset();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryLighthouseLightTest, "Snarehaven.Story.Lighthouse.Light",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryLighthouseLightTest::RunTest(const FString& Parameters)
{
	// 25단계를 못 끝낸 계정은 밤에도 불이 없다 (빈 길 0)
	TestEqual(TEXT("5장 전 + 한밤 = 0"), SnareStory::LighthouseBrightness(false, 1.f), 0.f);
	TestEqual(TEXT("5장 전 + 낮 = 0"), SnareStory::LighthouseBrightness(false, 0.f), 0.f);
	TestEqual(TEXT("끝냄 + 낮 = 0"), SnareStory::LighthouseBrightness(true, 0.f), 0.f);
	TestEqual(TEXT("끝냄 + 한밤 = 1"), SnareStory::LighthouseBrightness(true, 1.f), 1.f);
	TestTrue(TEXT("끝냄 + 해질녘 = 반쯤"), FMath::IsNearlyEqual(SnareStory::LighthouseBrightness(true, 0.5f), 0.5f));
	TestEqual(TEXT("범위 밖 밤 값은 접는다"), SnareStory::LighthouseBrightness(true, 3.f), 1.f);

	// 한 바퀴 주기 — 반 주기에 180도, 한 주기에 다시 0, 음수 시각도 접힌다
	const double Turn = SnareStory::LighthouseTurnSeconds;
	TestEqual(TEXT("0초 = 0도"), SnareStory::LighthouseBeamYaw(0.0), 0.f);
	TestTrue(TEXT("반 주기 = 180도"), FMath::IsNearlyEqual(SnareStory::LighthouseBeamYaw(Turn * 0.5), 180.f, 0.01f));
	TestTrue(TEXT("한 주기 = 0도"), FMath::IsNearlyEqual(SnareStory::LighthouseBeamYaw(Turn * 7.0), 0.f, 0.01f));
	TestTrue(TEXT("음수 반 주기 = 180도"), FMath::IsNearlyEqual(SnareStory::LighthouseBeamYaw(-Turn * 0.5), 180.f, 0.01f));

	// 답에서 읽은 「끝냄」을 기억한다
	const bool Before = SnareStory::GetKnownDone();
	SnareStory::SetKnownDone(true);
	TestTrue(TEXT("끝냄을 기억한다"), SnareStory::GetKnownDone());
	SnareStory::SetKnownDone(Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryLighthouseActorTest, "Snarehaven.Story.Lighthouse.Actor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryLighthouseActorTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ASnareLighthouseActor* const First = ASnareLighthouseActor::SpawnAtSpot(World);
	TestNotNull(TEXT("탑이 선다"), First);
	TestTrue(TEXT("두 번째 부름은 같은 탑"), ASnareLighthouseActor::SpawnAtSpot(World) == First);
	if (First != nullptr)
	{
		First->ApplyLight(SnareStory::LighthouseBrightness(false, 1.f), 0.f);
		TestEqual(TEXT("5장 전엔 빛줄기 세기 0"), First->GetBeamIntensity(), 0.f);
		First->ApplyLight(SnareStory::LighthouseBrightness(true, 1.f), 90.f);
		TestTrue(TEXT("끝낸 계정 한밤엔 빛줄기가 켜진다"), First->GetBeamIntensity() > 0.f);
		First->ApplyLight(SnareStory::LighthouseBrightness(true, 0.f), 90.f);
		TestEqual(TEXT("낮엔 다시 0"), First->GetBeamIntensity(), 0.f);

		// 빈 길 0 — 5장 전 계정은 낮밤 어느 값에서도 두 불이 한 개도 안 켜진다.
		for (int32 Step = 0; Step <= 10; ++Step)
		{
			First->ApplyLight(SnareStory::LighthouseBrightness(false, Step / 10.f), Step * 36.f);
			TestEqual(*FString::Printf(TEXT("5장 전 밤 %d/10 — 빛줄기 0"), Step), First->GetBeamIntensity(), 0.f);
			TestEqual(*FString::Printf(TEXT("5장 전 밤 %d/10 — 랜턴 0"), Step), First->GetLanternIntensity(), 0.f);
		}
		First->ApplyLight(SnareStory::LighthouseBrightness(true, 1.f), 0.f);
		TestTrue(TEXT("끝낸 계정 한밤엔 랜턴도 켜진다"), First->GetLanternIntensity() > 0.f);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryLighthouseGlanceTest, "Snarehaven.Story.Lighthouse.Glance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryLighthouseGlanceTest::RunTest(const FString& Parameters)
{
	// 장 끝 연출 카메라 — 탑에서 플레이어 쪽으로 띄워 탑을 본다. 숫자는 표에서 읽어 비교한다.
	const FVector Tower(1000.f, 2000.f, 45.f);
	const FTransform East = SnareStory::LighthouseGlancePose(Tower, Tower + FVector(30000.f, 0.f, 900.f));
	TestTrue(TEXT("플레이어 쪽(+X)에 선다"), East.GetLocation().X > Tower.X);
	TestTrue(TEXT("탑까지 수평 거리 = 표 값"), FMath::IsNearlyEqual(FVector::Dist2D(East.GetLocation(), Tower), SnareStory::LighthouseGlanceDistanceCm, 0.5f));
	TestTrue(TEXT("눈높이 = 탑 밑동 + 표 값 (플레이어 높이 무관)"), FMath::IsNearlyEqual(East.GetLocation().Z, Tower.Z + SnareStory::LighthouseGlanceEyeHeightCm, 0.5f));
	const FVector Forward = East.GetRotation().GetForwardVector();
	TestTrue(TEXT("탑 쪽(-X)을 본다"), Forward.GetSafeNormal2D().X < -0.99f);
	// 보는 줄이 탑 축에서 표의 높이를 지난다
	const float Along = (Tower.X - East.GetLocation().X) / Forward.X;
	TestTrue(TEXT("탑 중간 높이를 본다"), FMath::IsNearlyEqual(East.GetLocation().Z + Forward.Z * Along, Tower.Z + SnareStory::LighthouseGlanceLookHeightCm, 1.f));
	TestTrue(TEXT("내려다본다 (하늘을 안 비춘다)"), East.Rotator().Pitch < -20.f);
	// 탑 밑동과 꼭대기(시험 전용 키 1337cm — 메시 원본 값)가 게임 카메라 화각 30도 · 폰 가로 1334×616 의 세로 반각 안에 든다
	const float HalfVerticalDeg = FMath::RadiansToDegrees(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(15.f)) * 616.f / 1334.f));
	const float Pitch = East.Rotator().Pitch;
	const float Dist = SnareStory::LighthouseGlanceDistanceCm;
	const float BottomDeg = FMath::RadiansToDegrees(FMath::Atan2(-SnareStory::LighthouseGlanceEyeHeightCm, Dist));
	const float TopDeg = FMath::RadiansToDegrees(FMath::Atan2(1337.f - SnareStory::LighthouseGlanceEyeHeightCm, Dist));
	TestTrue(TEXT("탑 밑동이 화면 안"), FMath::Abs(BottomDeg - Pitch) < HalfVerticalDeg);
	TestTrue(TEXT("탑 꼭대기가 화면 안"), FMath::Abs(TopDeg - Pitch) < HalfVerticalDeg);

	const FTransform North = SnareStory::LighthouseGlancePose(Tower, Tower + FVector(0.f, 500.f, 0.f));
	TestTrue(TEXT("플레이어가 +Y 면 +Y 쪽"), North.GetLocation().Y > Tower.Y && FMath::IsNearlyEqual(North.GetLocation().X, Tower.X, 0.5f));
	const FTransform Same = SnareStory::LighthouseGlancePose(Tower, Tower);
	TestTrue(TEXT("같은 자리면 -X 쪽으로 물러선다"), Same.GetLocation().X < Tower.X);
	TestFalse(TEXT("같은 자리여도 NaN 없음"), Same.ContainsNaN());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryLighthouseIgnitionTest, "Snarehaven.Story.Lighthouse.Ignition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryLighthouseIgnitionTest::RunTest(const FString& Parameters)
{
	// 5장 끝 점화 — 숫자는 표에서 읽어 비교한다.
	const float Blend = SnareStory::LighthouseGlanceBlendSeconds;
	const float Hold = SnareStory::LighthouseGlanceHoldSeconds;
	const float Ramp = SnareStory::LighthouseIgnitionRampSeconds;
	const float Total = SnareStory::LighthouseIgnitionSeconds();
	TestTrue(TEXT("점화 길이 = 비추기 길이 (섞기 둘 + 머물기)"), FMath::IsNearlyEqual(Total, Blend * 2.f + Hold));
	TestTrue(TEXT("불이 다 붙는 건 카메라가 머무는 동안"), Ramp < Hold);
	TestEqual(TEXT("시작 전 0"), SnareStory::LighthouseIgnitionBrightness(-0.1f), 0.f);
	TestEqual(TEXT("카메라가 가는 동안 0"), SnareStory::LighthouseIgnitionBrightness(Blend * 0.5f), 0.f);
	TestTrue(TEXT("닿고 반쯤 붙음"), FMath::IsNearlyEqual(SnareStory::LighthouseIgnitionBrightness(Blend + Ramp * 0.5f), 0.5f, 0.001f));
	TestEqual(TEXT("다 붙음"), SnareStory::LighthouseIgnitionBrightness(Blend + Ramp), 1.f);
	TestEqual(TEXT("돌아오는 동안도 켜짐"), SnareStory::LighthouseIgnitionBrightness(Total - 0.01f), 1.f);
	TestEqual(TEXT("연출 끝 = 0 (밤 규칙에 넘김)"), SnareStory::LighthouseIgnitionBrightness(Total), 0.f);
	TestEqual(TEXT("돌림 — 머물기 전 0"), SnareStory::LighthouseIgnitionExtraYaw(Blend), 0.f);
	TestTrue(TEXT("돌림 — 머물기 반 = 반 바퀴"), FMath::IsNearlyEqual(SnareStory::LighthouseIgnitionExtraYaw(Blend + Hold * 0.5f), 180.f, 0.01f));
	TestEqual(TEXT("돌림 — 머물기 끝 = 한 바퀴"), SnareStory::LighthouseIgnitionExtraYaw(Blend + Hold), 360.f);
	TestEqual(TEXT("돌림 — 그 뒤 한 바퀴 그대로"), SnareStory::LighthouseIgnitionExtraYaw(Total + 5.f), 360.f);

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ASnareLighthouseActor* const Tower = ASnareLighthouseActor::SpawnAtSpot(World);
	if (TestNotNull(TEXT("탑이 선다"), Tower))
	{
		const double Start = 100.0;
		const double BeamSeconds = SnareStory::LighthouseTurnSeconds * 0.25;	// 밤 규칙 방향 90도
		Tower->UpdateLight(true, 0.f, BeamSeconds, Start);
		TestEqual(TEXT("점화 전 끝낸 계정 낮 — 빛줄기 0"), Tower->GetBeamIntensity(), 0.f);
		TestFalse(TEXT("점화 전엔 점화 중 아님"), Tower->IsIgniting(Start));
		Tower->StartIgnition(Start);
		TestTrue(TEXT("시작하면 점화 중"), Tower->IsIgniting(Start));
		Tower->UpdateLight(true, 0.f, BeamSeconds, Start + Blend * 0.5);
		TestEqual(TEXT("카메라가 가는 동안 낮엔 아직 0"), Tower->GetBeamIntensity(), 0.f);
		TestEqual(TEXT("카메라가 가는 동안 창도 어두움"), Tower->GetWindowGlow(), 0.f);
		Tower->UpdateLight(true, 0.f, BeamSeconds, Start + Blend + Ramp);
		const float Full = Tower->GetBeamIntensity();
		TestTrue(TEXT("낮이어도 점화로 다 켜짐"), Full > 0.f);
		TestTrue(TEXT("랜턴도 켜짐"), Tower->GetLanternIntensity() > 0.f);
		TestEqual(TEXT("랜턴 방 창도 다 켜짐"), Tower->GetWindowGlow(), 1.f);
		Tower->UpdateLight(true, 0.f, BeamSeconds, Start + Blend + Hold * 0.5);
		TestTrue(TEXT("머물기 반 — 빛줄기 90+180 도"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(Tower->GetBeamYaw() - 270.f), 0.f, 0.1f));
		Tower->UpdateLight(true, 1.f, BeamSeconds, Start + Blend + Ramp);
		TestTrue(TEXT("한밤엔 밤 규칙과 같은 세기 (더 세지 않음)"), FMath::IsNearlyEqual(Tower->GetBeamIntensity(), Full));
		Tower->UpdateLight(true, 0.f, BeamSeconds, Start + Total + 0.01);
		TestFalse(TEXT("연출 끝 — 점화 중 아님"), Tower->IsIgniting(Start + Total + 0.01));
		TestEqual(TEXT("연출 끝 낮 — 다시 0"), Tower->GetBeamIntensity(), 0.f);
		TestEqual(TEXT("연출 끝 낮 — 창도 어두운 재질로"), Tower->GetWindowGlow(), 0.f);
		TestTrue(TEXT("연출 끝 방향 = 밤 규칙 방향 (끊김 없음)"), FMath::IsNearlyEqual(FRotator::NormalizeAxis(Tower->GetBeamYaw() - 90.f), 0.f, 0.1f));
		Tower->UpdateLight(true, 1.f, BeamSeconds, Start + Total + 1.0);
		TestTrue(TEXT("연출 뒤 밤엔 켜짐"), Tower->GetBeamIntensity() > 0.f);
		// 5장 전 계정은 점화를 안 부른다 — 부르지 않은 탑은 낮밤 어디서도 0 (빈 길 0 은 Actor 시험)
		Tower->UpdateLight(false, 1.f, BeamSeconds, Start + Total + 2.0);
		TestEqual(TEXT("5장 전 + 점화 없음 = 0"), Tower->GetBeamIntensity(), 0.f);
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryLighthouseBeamMeshTest, "Snarehaven.Story.Lighthouse.BeamMesh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryLighthouseBeamMeshTest::RunTest(const FString& Parameters)
{
	// 낮에도 보이는 빛줄기 메시 (INBOX 감독 10-20 16:20) — 시험 전용 값: 탑 키 1337cm(메시 원본), 랜턴 높이 1105cm.
	const float TowerHeight = 1337.f;
	const float LanternHeight = 1105.f;
	const float Blend = SnareStory::LighthouseGlanceBlendSeconds;
	const float Hold = SnareStory::LighthouseGlanceHoldSeconds;
	const float Ramp = SnareStory::LighthouseIgnitionRampSeconds;
	const float Total = SnareStory::LighthouseIgnitionSeconds();

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ASnareLighthouseActor* const Tower = ASnareLighthouseActor::SpawnAtSpot(World);
	const UStaticMeshComponent* const Mesh = Tower ? Tower->GetBeamMesh() : nullptr;
	if (TestNotNull(TEXT("탑이 선다"), Tower) && TestNotNull(TEXT("빛줄기 메시가 있다"), Mesh)
		&& TestNotNull(TEXT("원뿔 메시가 실렸다"), Mesh->GetStaticMesh().Get()))
	{
		// 원뿔 꼭짓점(+Z 50)·바닥 가운데(-Z 50)·바닥 가장자리를 월드로
		const auto Apex = [Mesh]() { return Mesh->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, 50.f)); };
		const auto End = [Mesh]() { return Mesh->GetComponentTransform().TransformPosition(FVector(0.f, 0.f, -50.f)); };
		const auto EndRadius = [Mesh]() { return Mesh->GetComponentTransform().TransformVector(FVector(50.f, 0.f, 0.f)).Size(); };
		const FVector Lantern = Tower->GetActorLocation() + FVector(0.f, 0.f, LanternHeight);

		const double Start = 100.0;
		const double BeamSeconds = SnareStory::LighthouseTurnSeconds * 0.25;	// 밤 규칙 방향 90도
		Tower->UpdateLight(true, 0.f, BeamSeconds, Start);
		TestFalse(TEXT("점화 전 낮 — 숨음"), Mesh->IsVisible());
		TestEqual(TEXT("점화 전 낮 — 세기 0"), Tower->GetBeamMeshStrength(), 0.f);
		Tower->StartIgnition(Start);
		Tower->UpdateLight(true, 0.f, BeamSeconds, Start + Blend * 0.5);
		TestFalse(TEXT("카메라가 가는 동안 — 아직 숨음"), Mesh->IsVisible());

		Tower->UpdateLight(true, 0.f, BeamSeconds, Start + Blend + Ramp);
		TestTrue(TEXT("낮 점화 — 보임"), Mesh->IsVisible());
		const float Full = Tower->GetBeamMeshStrength();
		TestTrue(TEXT("낮 점화 — 세기 > 0"), Full > 0.f);
		const UMaterialInterface* const Material = Mesh->GetMaterial(0);
		TestTrue(TEXT("가산 섞기 재질"), Material != nullptr && Material->GetBlendMode() == BLEND_Additive);
		TestTrue(TEXT("꼭짓점 = 랜턴 방"), FVector::Dist(Apex(), Lantern) < 1.f);
		const float Length = FVector::Dist(Apex(), End());
		TestTrue(*FString::Printf(TEXT("길이 %.0fcm ≥ 탑 키 2배 %.0fcm"), Length, TowerHeight * 2.f), Length >= TowerHeight * 2.f);
		TestTrue(TEXT("끝이 넓어진다 (띠)"), EndRadius() > 100.f);
		TestTrue(TEXT("끝이 땅에 안 박힌다"), End().Z - EndRadius() > Tower->GetActorLocation().Z);

		// 방향 다른 두 순간 — 빛줄기 끝이 스포트 빛 방향(요)을 따라 돈다
		Tower->UpdateLight(true, 0.f, BeamSeconds, Start + Blend + Hold * 0.25);
		const FVector Quarter = End();
		const FVector QuarterDir = (Quarter - Apex()).GetSafeNormal2D();
		const float QuarterYaw = Tower->GetBeamYaw();
		TestTrue(TEXT("머물기 1/4 — 끝 방향 = 빛 방향"), FVector::DotProduct(QuarterDir, FRotator(0.f, QuarterYaw, 0.f).Vector()) > 0.999f);
		Tower->UpdateLight(true, 0.f, BeamSeconds, Start + Blend + Hold * 0.75);
		const FVector ThreeQuarter = End();
		TestTrue(TEXT("머물기 3/4 — 끝이 반대쪽 (반 바퀴)"), FVector::DotProduct(QuarterDir, (ThreeQuarter - Apex()).GetSafeNormal2D()) < -0.99f);

		Tower->UpdateLight(true, 1.f, BeamSeconds, Start + Total + 1.0);
		TestTrue(TEXT("연출 뒤 한밤 — 점광과 같이 보임"), Mesh->IsVisible());
		TestTrue(TEXT("한밤 세기 = 점화 세기 (더 세지 않음)"), FMath::IsNearlyEqual(Tower->GetBeamMeshStrength(), Full));
		Tower->UpdateLight(true, 0.5f, BeamSeconds, Start + Total + 1.0);
		TestTrue(TEXT("반쯤 밤 — 세기 반"), FMath::IsNearlyEqual(Tower->GetBeamMeshStrength(), Full * 0.5f, 0.001f));
		Tower->UpdateLight(true, 0.f, BeamSeconds, Start + Total + 1.0);
		TestFalse(TEXT("연출 뒤 낮 — 숨음"), Mesh->IsVisible());
		// 빈 길 0 — 5장 전 계정은 낮밤 어느 값에서도 안 보인다
		for (int32 Step = 0; Step <= 10; ++Step)
		{
			Tower->UpdateLight(false, Step / 10.f, BeamSeconds, Start + Total + 2.0);
			TestFalse(*FString::Printf(TEXT("5장 전 밤 %d/10 — 숨음"), Step), Mesh->IsVisible());
		}
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryArrivalTest, "Snarehaven.Story.ArrivalGate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenStoryArrivalTest::RunTest(const FString& Parameters)
{
	using namespace SnareStory;
	const float Far = 1.0e9f;
	const FArrival Hub = ArrivalFor(1);
	TestTrue(TEXT("1단계는 거점 도착 조건"), Hub.Kind == EArrival::NearGiver);
	TestFalse(TEXT("해변(멀리)에선 대사 안 뜸"), IsArrived(Hub, Hub.RadiusCm + 1.f, NAME_None, Far));
	TestFalse(TEXT("시작 자리(550cm)에선 미도착"), IsArrived(Hub, 550.f, NAME_None, Far));
	TestTrue(TEXT("불빛 앞에 닿으면 뜸"), IsArrived(Hub, Hub.RadiusCm, NAME_None, Far));
	const FArrival Island = ArrivalFor(14);
	TestFalse(TEXT("이웃 섬 밖"), IsArrived(Island, 0.f, FName(TEXT("Zone_Plains")), 0.f));
	TestFalse(TEXT("존 없음(바다)"), IsArrived(Island, 0.f, NAME_None, 0.f));
	TestTrue(TEXT("이웃 섬 안"), IsArrived(Island, Far, Island.RegionId, Far));
	// 도착 조건의 존 이름이 존 표의 실제 `ZoneID` 여야 한다 — 행 이름(`Zone_Forest`)을 적어 14단계가 영영 안 끝났다(루프#1325)
	const UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	if (TestNotNull(TEXT("존 표를 읽음"), ZoneTable))
	{
		const FZoneRow* const ForestRow = ZoneTable->FindRow<FZoneRow>(FName(TEXT("Zone_Forest")), TEXT("StoryArrival"));
		if (TestNotNull(TEXT("숲 존 행"), ForestRow))
		{
			TestEqual(TEXT("14단계 도착 존 = 표의 숲 ZoneID"), Island.RegionId, ForestRow->ZoneID);
		}
	}
	const FArrival Hill = ArrivalFor(22);
	TestFalse(TEXT("등대 언덕 밖"), IsArrived(Hill, 0.f, NAME_None, Hill.RadiusCm + 1.f));
	TestTrue(TEXT("등대 언덕 안"), IsArrived(Hill, Far, NAME_None, Hill.RadiusCm));
	const FArrival Camp = ArrivalFor(16);
	TestTrue(TEXT("16단계는 캠프 도착 조건"), Camp.Kind == EArrival::AtCamp);
	TestFalse(TEXT("캠프 밖"), IsArrived(Camp, Far, NAME_None, Far, Camp.RadiusCm + 1.f));
	TestTrue(TEXT("캠프 안"), IsArrived(Camp, Far, NAME_None, Far, Camp.RadiusCm));
	TestEqual(TEXT("캠프 도착 반경은 화면 반폭 1000cm"), Camp.RadiusCm, 1000.f);
	TestFalse(TEXT("캠프 1500cm 밖에선 미도착"), IsArrived(Camp, Far, NAME_None, Far, 1500.f));
	TestTrue(TEXT("16단계는 발견 대사가 따로 있다"), ArrivalDialogue(16).Num() > 0);
	TestTrue(TEXT("발견 대사는 「가 보게」와 다르다"), ArrivalDialogue(16)[0].Text.ToString() != StepDialogue(16)[0].Text.ToString());
	TestEqual(TEXT("1단계는 발견 대사 없음(만남이 곧 대사)"), ArrivalDialogue(1).Num(), 0);
	TestFalse(TEXT("캠프 거리를 안 주면 미도착"), IsArrived(Camp, 0.f, FName(TEXT("Zone_Swamp")), 0.f));
	TestTrue(TEXT("조건 없는 단계는 늘 참"), IsArrived(ArrivalFor(2), Far, NAME_None, Far));
	const FArrival Cages = ArrivalFor(18);
	TestTrue(TEXT("18단계는 우리 부수기 조건"), Cages.Kind == EArrival::CagesBroken);
	TestFalse(TEXT("우리를 안 부쉈으면 캠프 안이라도 미도착"), IsArrived(Cages, 0.f, NAME_None, 0.f, 0.f, false));
	TestTrue(TEXT("우리를 다 부수면 도착"), IsArrived(Cages, Far, NAME_None, Far, Far, true));
	const FArrival Hook = ArrivalFor(20);
	TestTrue(TEXT("20단계(갈고리 말 걸기)는 갈고리 곁 조건"), Hook.Kind == EArrival::NearHook);
	TestFalse(TEXT("갈고리에게서 멀면 캠프 안이라도 대사 안 뜸"), IsArrived(Hook, 0.f, NAME_None, 0.f, 0.f, true, 0.f, true, Hook.RadiusCm + 1.f));
	TestTrue(TEXT("갈고리 곁이면 뜸"), IsArrived(Hook, Far, NAME_None, Far, Far, false, Far, false, Hook.RadiusCm));
	TestFalse(TEXT("갈고리 거리를 안 주면 미도착"), IsArrived(Hook, 0.f, NAME_None, 0.f, 0.f));
	const FArrival Sand = ArrivalFor(11);
	TestTrue(TEXT("11단계(모래 말 걸기)는 모래 곁 조건"), Sand.Kind == EArrival::NearMollae);
	TestFalse(TEXT("모래에게서 멀면 대사 안 뜸"), IsArrived(Sand, 0.f, NAME_None, 0.f, Far, false, Sand.RadiusCm + 1.f));
	TestTrue(TEXT("모래 곁이면 뜸"), IsArrived(Sand, Far, NAME_None, Far, Far, false, Sand.RadiusCm));
	TestFalse(TEXT("솔방울 곁이어도 모래가 멀면 미도착"), IsArrived(Sand, 0.f, NAME_None, Far));
	const FArrival Journal = ArrivalFor(15);
	TestTrue(TEXT("15단계(일지 건네기)는 솔방울 곁 조건"), Journal.Kind == EArrival::NearGiver);
	TestFalse(TEXT("솔방울에게서 멀면 대사 안 뜸"), IsArrived(Journal, Journal.RadiusCm + 1.f, NAME_None, Far));
	TestTrue(TEXT("솔방울 곁이면 뜸"), IsArrived(Journal, Journal.RadiusCm, NAME_None, Far));
	const FArrival Top = ArrivalFor(25);
	TestTrue(TEXT("25단계(불 켜기)는 누르기 도착 조건"), Top.Kind == EArrival::LightPressed);
	TestFalse(TEXT("탑 곁에 서기만 해선 불 켜기 대사 안 뜸"), IsArrived(Top, 0.f, NAME_None, 0.f, 0.f, true, 0.f, false));
	TestTrue(TEXT("「불 켜기」 를 누르면 뜸"), IsArrived(Top, Far, NAME_None, Far, Far, false, Far, true));
	TestTrue(TEXT("탑 반경 안·안 누름이면 버튼"), CanPressLighthouseLight(25, Top.RadiusCm, false));
	TestFalse(TEXT("탑 반경 밖이면 버튼 없음"), CanPressLighthouseLight(25, Top.RadiusCm + 1.f, false));
	TestFalse(TEXT("이미 눌렀으면 버튼 없음"), CanPressLighthouseLight(25, 0.f, true));
	TestFalse(TEXT("다른 단계(22 등대 언덕)엔 버튼 없음"), CanPressLighthouseLight(22, 0.f, false));
	TestFalse(TEXT("다 끝낸 계정(지금 단계 0)엔 버튼 없음"), CanPressLighthouseLight(0, 0.f, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryMarkTargetTest, "Snarehaven.Story.MarkTarget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenStoryMarkTargetTest::RunTest(const FString& Parameters)
{
	using namespace SnareStory;
	// 표식 상대는 단계 대사의 첫 말하는 사람과 같아야 한다 (결함 10-19 「!」「?」 가 의뢰인 전부에 뜬다).
	for (int32 Step = 1; Step <= TotalSteps; ++Step)
	{
		const TArray<FDialogueLine> Lines = StepDialogue(Step);
		const FString Speaker = Lines.Num() > 0 ? Lines[0].Speaker.ToString() : FString();
		const EStoryNpc Want = Speaker == TEXT("모래") ? EStoryNpc::Mollae : Speaker == TEXT("갈고리") ? EStoryNpc::Hook : EStoryNpc::Solbangul;
		TestEqual(*FString::Printf(TEXT("%d단계 표식 상대 (%s)"), Step, *Speaker), TargetNpc(Step), Want);
	}
	TestEqual(TEXT("3장 1단계는 모래"), TargetNpc(11), EStoryNpc::Mollae);
	TestEqual(TEXT("1장 3단계는 솔방울"), TargetNpc(3), EStoryNpc::Solbangul);
	TestEqual(TEXT("범위 밖은 없음"), TargetNpc(0), EStoryNpc::None);
	TestEqual(TEXT("범위 밖은 없음 2"), TargetNpc(TotalSteps + 1), EStoryNpc::None);
	SetCurrentTargetNpc(EStoryNpc::Mollae);
	TestEqual(TEXT("지도 표식 상대를 적고 읽는다"), GetCurrentTargetNpc(), EStoryNpc::Mollae);
	SetCurrentTargetNpc(EStoryNpc::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryDialogueFitTest, "Snarehaven.Story.DialogueFit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenStoryDialogueFitTest::RunTest(const FString& Parameters)
{
	// 대사 판은 오른쪽 아래 기력 막대(화면 오른쪽에서 약 490 안쪽까지)를 덮지 않는다.
	for (const float Screen : {1280.f, 1600.f, 1920.f, 2400.f})
	{
		const float Width = USnareStoryDialogueWidget::FitPanelWidth(Screen);
		TestTrue(*FString::Printf(TEXT("화면 %.0f: 판 오른쪽 끝이 막대 앞"), Screen), Screen * 0.5f + Width * 0.5f <= Screen - 490.f || Width <= 360.f);
		TestTrue(TEXT("최대 900"), Width <= 900.f);
	}
	TestEqual(TEXT("넓은 화면은 900"), USnareStoryDialogueWidget::FitPanelWidth(2400.f), 900.f);

	// 계산 함수만이 아니라 실제 위젯 — 긴 줄(42자)을 싣고 화면 폭을 알려 준 뒤 상자가 받은 너비를 Slate 에서 읽는다.
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	if (USnareStoryDialogueWidget* const Widget = CreateWidget<USnareStoryDialogueWidget>(World, USnareStoryDialogueWidget::StaticClass()))
	{
		Widget->bPersistSeen = false;
		Widget->ShowStep(15);
		const TSharedRef<SWidget> Slate = Widget->TakeWidget();
		for (const float Screen : {1280.f, 1334.f, 1920.f})
		{
			Widget->FitToScreen(Screen);
			Slate->SlatePrepass(1.f);
			const UWidget* const Box = Widget->GetWidgetFromName(TEXT("DialoguePanelBox"));
			if (TestNotNull(TEXT("판 상자"), Box) && Box->GetCachedWidget().IsValid())
			{
				const float Got = Box->GetCachedWidget()->GetDesiredSize().X;
				TestTrue(*FString::Printf(TEXT("화면 %.0f: 실제 판 너비 %.0f 가 막대 앞"), Screen, Got),
					Screen * 0.5f + Got * 0.5f <= Screen - 490.f || Got <= 360.f);
			}
		}
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryCampTest, "Snarehaven.Story.CampActor", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenStoryCampTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ASnareHarpoonCampActor* const First = ASnareHarpoonCampActor::SpawnAtSpot(World);
	TestNotNull(TEXT("캠프가 선다"), First);
	TestTrue(TEXT("두 번째 부름은 같은 캠프"), ASnareHarpoonCampActor::SpawnAtSpot(World) == First);
	if (First != nullptr)
	{
		const UStaticMeshComponent* const Mesh = First->FindComponentByClass<UStaticMeshComponent>();
		TestTrue(TEXT("캠프 모양(메시)이 붙어 있다 — 엔진 기본 도형이 아니라 반입한 에셋"),
			Mesh != nullptr && Mesh->GetStaticMesh() != nullptr && Mesh->GetStaticMesh()->GetPathName().Contains(TEXT("SM_Prop_HarpoonCamp_01")));
		TestTrue(TEXT("캠프는 정해진 자리 근처"), FVector2D::Distance(FVector2D(First->GetActorLocation()), FVector2D(SnareStory::CampSpotX, SnareStory::CampSpotY)) < 1.f);
	}
	TestEqual(TEXT("우리 셋이 선다"), ASnareCampCageActor::CountAll(World), 3);
	TestFalse(TEXT("아직 아무것도 안 부쉈다"), ASnareCampCageActor::AllBroken(World));
	const FVector Near = First != nullptr ? First->GetActorLocation() + FVector(0.0, -450.0, 0.0) : FVector::ZeroVector;
	const FVector Far = Near + FVector(5000.0, 0.0, 0.0);
	TestFalse(TEXT("멀리선 못 친다"), ASnareCampCageActor::StrikeNearest(World, Far));
	for (int32 Hit = 0; Hit < ASnareCampCageActor::HitsToBreak; ++Hit)
	{
		TestEqual(TEXT("부서지기 전엔 1개도 안 부서짐"), ASnareCampCageActor::CountBroken(World), 0);
		TestTrue(TEXT("곁에서 치면 맞는다"), ASnareCampCageActor::StrikeNearest(World, Near));
	}
	TestEqual(TEXT("정해진 타수에 하나가 부서진다"), ASnareCampCageActor::CountBroken(World), 1);
	TestFalse(TEXT("우리 하나만으론 미도착"), ASnareCampCageActor::AllBroken(World));
	for (const FVector2D& Offset : ASnareCampCageActor::Offsets())
	{
		const FVector At = First->GetActorLocation() + FVector(Offset.X, Offset.Y, 0.0);
		for (int32 Hit = 0; Hit < ASnareCampCageActor::HitsToBreak; ++Hit)
		{
			ASnareCampCageActor::StrikeNearest(World, At);
		}
	}
	TestEqual(TEXT("셋 다 부서졌다"), ASnareCampCageActor::CountBroken(World), 3);
	TestTrue(TEXT("전부 부서지면 AllBroken"), ASnareCampCageActor::AllBroken(World));
	TestFalse(TEXT("다 부서진 뒤엔 칠 게 없다"), ASnareCampCageActor::StrikeNearest(World, Near));
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryCampLayoutTest, "Snarehaven.Story.CampLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenStoryCampLayoutTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	ASnareHarpoonCampActor* const Camp = ASnareHarpoonCampActor::SpawnAtSpot(World);
	if (Camp != nullptr)
	{
		const FVector Center = Camp->GetActorLocation();
		TArray<FBox2D> Cages;
		for (TActorIterator<ASnareCampCageActor> It(World); It; ++It)
		{
			const UStaticMeshComponent* const Mesh = It->FindComponentByClass<UStaticMeshComponent>();
			if (Mesh == nullptr || Mesh->GetStaticMesh() == nullptr)
			{
				AddError(TEXT("우리에 메시가 없다"));
				continue;
			}
			const FBox Box = Mesh->Bounds.GetBox();
			UE_LOG(LogTemp, Display, TEXT("[CampLayout] 우리 상자 x %.0f~%.0f y %.0f~%.0f (캠프 기준)"), Box.Min.X - Center.X, Box.Max.X - Center.X, Box.Min.Y - Center.Y, Box.Max.Y - Center.Y);
			Cages.Add(FBox2D(FVector2D(Box.Min), FVector2D(Box.Max)));
		}
		TestEqual(TEXT("우리 셋"), Cages.Num(), 3);
		// 천막 자리 상수가 실제 메시와 맞는지(반입이 Y 를 뒤집어 한 번 어긋났다) — 천막 상자가 캠프 메시 월드 상자 안에 들어야 한다.
		const UStaticMeshComponent* const CampMesh = Camp->FindComponentByClass<UStaticMeshComponent>();
		if (TestNotNull(TEXT("캠프 메시"), CampMesh))
		{
			const FBox CampBox = CampMesh->Bounds.GetBox();
			for (const FVector2D& Tent : ASnareHarpoonCampActor::TentSpots())
			{
				const FVector2D At = FVector2D(Center) + Tent;
				TestTrue(FString::Printf(TEXT("천막(%.0f,%.0f)이 캠프 메시 안에 든다"), Tent.X, Tent.Y),
					At.X - ASnareHarpoonCampActor::TentRadiusCm * 0.9f >= CampBox.Min.X && At.X + ASnareHarpoonCampActor::TentRadiusCm * 0.9f <= CampBox.Max.X
					&& At.Y - ASnareHarpoonCampActor::TentRadiusCm * 0.9f >= CampBox.Min.Y && At.Y + ASnareHarpoonCampActor::TentRadiusCm * 0.9f <= CampBox.Max.Y);
			}
		}
		for (int32 A = 0; A < Cages.Num(); ++A)
		{
			for (int32 B = A + 1; B < Cages.Num(); ++B)
			{
				TestFalse(FString::Printf(TEXT("우리 %d·%d 가 겹친다"), A, B), Cages[A].Intersect(Cages[B]));
			}
			for (const FVector2D& Tent : ASnareHarpoonCampActor::TentSpots())
			{
				const FVector2D At = FVector2D(Center) + Tent;
				const FBox2D TentBox(At - FVector2D(ASnareHarpoonCampActor::TentRadiusCm), At + FVector2D(ASnareHarpoonCampActor::TentRadiusCm));
				TestFalse(FString::Printf(TEXT("우리 %d 가 천막(%.0f,%.0f)과 겹친다"), A, Tent.X, Tent.Y), Cages[A].Intersect(TentBox));
			}
		}
	}
	World->DestroyWorld(false);
	return true;
}

// NPC(상인·의뢰인)는 놓인 요가 어떻든 처음엔 카메라 쪽(월드 −X)을 보고, 말 걸기 반경 안에 폰이 오면 폰 쪽을 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryNpcFacingTest, "Snarehaven.Story.NpcFacing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryNpcFacingTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	const float TestTalkRangeCm = 300.f;
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector(0.f, 100.f, 0.f), FRotator::ZeroRotator);
	AShopVendorActor* const Vendor = World->SpawnActor<AShopVendorActor>(FVector::ZeroVector, FRotator(0.f, 37.f, 0.f));
	AQuestGiverActor* const Giver = World->SpawnActor<AQuestGiverActor>(FVector(5000.f, 0.f, 0.f), FRotator(0.f, 211.f, 0.f));
	if (TestNotNull(TEXT("플레이어"), Player) && TestNotNull(TEXT("상인"), Vendor) && TestNotNull(TEXT("의뢰인"), Giver))
	{
		Player->DispatchBeginPlay();
		Player->GetMeleeAttack()->SetNpcTalkRangeCm(TestTalkRangeCm);
		Vendor->DispatchBeginPlay();
		Giver->DispatchBeginPlay();

		// 월드 −X 쪽이 카메라 쪽 (카메라는 +X 를 본다) — 요 ±180.
		TestTrue(TEXT("상인 기본 방향 = 카메라 쪽"), FMath::Abs(FRotator::NormalizeAxis(Vendor->GetFacingYaw() - 180.f)) < 1.f);
		TestTrue(TEXT("의뢰인 기본 방향 = 카메라 쪽"), FMath::Abs(FRotator::NormalizeAxis(Giver->GetFacingYaw() - 180.f)) < 1.f);

		// 몸은 플레이어와 같은 고개 들기 — 없으면 내려다보는 카메라에 모자 윗면만 보인다(루프#1426).
		const UPlayerBodyAnimInstance* const VendorAnim = Cast<UPlayerBodyAnimInstance>(Vendor->GetBody()->GetAnimInstance());
		if (TestNotNull(TEXT("상인 몸 = 플레이어 몸 애님"), VendorAnim))
		{
			const float PlayerLift = GetDefault<ASnarehavenCharacter>()->GetHeadLiftDegrees();
			TestTrue(TEXT("플레이어 고개 들기가 0 보다 큼"), PlayerLift > 0.f);
			TestEqual(TEXT("상인 고개 들기 = 플레이어"), VendorAnim->GetHeadLiftDegrees(), PlayerLift);
			TestNotNull(TEXT("상인 대기 클립이 돔"), VendorAnim->GetBaseClip());
		}

		// 폰이 반경 안(+Y 100cm, 요 90)이면 폰 쪽으로 돌되 카메라 쪽에서 한도까지만(180 − 한도), 반경 밖이면 카메라 쪽.
		const float Max = AShopVendorActor::MaxTalkTurnDegrees;
		TestTrue(TEXT("한도는 0~90 사이(옆모습 전에 멈춤)"), Max > 0.f && Max < 90.f);
		TestTrue(TEXT("반경 안 옆 → 폰 쪽으로 한도까지"), FMath::Abs(FRotator::NormalizeAxis(Vendor->ComputeFacingTargetYaw(Player) - (180.f - Max))) < 1.f);
		TestTrue(TEXT("반경 밖 → 카메라 쪽"), FMath::Abs(FRotator::NormalizeAxis(Giver->ComputeFacingTargetYaw(Player) - 180.f)) < 1.f);
		Vendor->SnapFacing(Vendor->ComputeFacingTargetYaw(Player));
		TestTrue(TEXT("돌아본 몸이 폰 쪽 한도"), FMath::Abs(FRotator::NormalizeAxis(Vendor->GetFacingYaw() - (180.f - Max))) < 1.f);
		// 한도 안(카메라 쪽에서 절반만 비킨 앞)이면 폰을 똑바로 본다.
		const float InsideYaw = 180.f - Max * 0.5f;
		Player->SetActorLocation(FRotator(0.f, InsideYaw, 0.f).Vector() * 100.f);
		TestTrue(TEXT("한도 안 앞 → 폰을 똑바로"), FMath::Abs(FRotator::NormalizeAxis(Vendor->ComputeFacingTargetYaw(Player) - InsideYaw)) < 1.f);
		// 등 뒤(+X, 요 0)여도 등을 돌리지 않는다 — 카메라 쪽에서 한도 안.
		Player->SetActorLocation(FVector(100.f, 0.f, 0.f));
		TestTrue(TEXT("등 뒤 폰 → 카메라에서 한도 안"), FMath::Abs(FRotator::NormalizeAxis(Vendor->ComputeFacingTargetYaw(Player) - 180.f)) <= Max + 0.01f);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// 4장 갈고리 — 캠프가 모닥불 앞에 세우고, 스토리 상대(18·20단계)로 찾히며, 좌판·판이 없어 상황 버튼 「열기」 대상이 아니다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryHookNpcTest, "Snarehaven.Story.HookNpc", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryHookNpcTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	const float TestTalkRangeCm = 300.f;
	ASnareHarpoonCampActor* const Camp = ASnareHarpoonCampActor::SpawnAtSpot(World);
	TActorIterator<ASnareHookNpcActor> Found(World);
	ASnareHookNpcActor* const Hook = Found ? *Found : nullptr;
	if (TestNotNull(TEXT("캠프"), Camp) && TestNotNull(TEXT("캠프가 갈고리를 세움"), Hook))
	{
		int32 HookCount = 0;
		for (TActorIterator<ASnareHookNpcActor> It(World); It; ++It)
		{
			++HookCount;
		}
		ASnareHookNpcActor::SpawnAtCamp(World, Camp->GetActorLocation());
		int32 AfterAgain = 0;
		for (TActorIterator<ASnareHookNpcActor> It(World); It; ++It)
		{
			++AfterAgain;
		}
		TestEqual(TEXT("갈고리는 하나"), HookCount, 1);
		TestEqual(TEXT("다시 불러도 하나"), AfterAgain, 1);
		Hook->DispatchBeginPlay();

		const FVector Offset = Hook->GetActorLocation() - Camp->GetActorLocation();
		TestTrue(TEXT("모닥불 앞(카메라 쪽 −X)"), Offset.X < -100.0 && FMath::Abs(Offset.Y) < 1.0);
		TestTrue(TEXT("캡슐 중심이 바닥 위"), Offset.Z > 50.0);
		for (const FVector2D& Cage : ASnareCampCageActor::Offsets())
		{
			TestTrue(TEXT("우리와 겹치지 않음(2m 넘게 떨어짐)"), FVector2D::Distance(FVector2D(Offset), Cage) > 200.0);
		}
		for (const FVector2D& Tent : ASnareHarpoonCampActor::TentSpots())
		{
			TestTrue(TEXT("천막 밖"), FVector2D::Distance(FVector2D(Offset), Tent) > ASnareHarpoonCampActor::TentRadiusCm);
		}
		TestTrue(TEXT("스토리 상대 = 갈고리"), Hook->GetStoryNpc() == SnareStory::EStoryNpc::Hook);
		TestTrue(TEXT("FindStoryNpc(갈고리)"), AShopVendorActor::FindStoryNpc(World, SnareStory::EStoryNpc::Hook) == Hook);
		TestTrue(TEXT("18·20단계 상대가 갈고리"), SnareStory::TargetNpc(18) == SnareStory::EStoryNpc::Hook && SnareStory::TargetNpc(20) == SnareStory::EStoryNpc::Hook);
		TestEqual(TEXT("이름"), Hook->GetVendorName().ToString(), FString(TEXT("갈고리")));
		TestFalse(TEXT("좌판 숨김"), Hook->GetStall()->IsVisible());
		TestEqual(TEXT("좌판 충돌 없음"), Hook->GetStall()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
		TestFalse(TEXT("복제 안 함(캠프와 같이 각자 화면)"), Hook->GetIsReplicated());
		const FName ChiefHatRow = Hook->GetEquipment()->FindEquipmentRowByItemID(TEXT("item.chief_hat"));
		TestFalse(TEXT("우두머리 모자 행이 표에 있음"), ChiefHatRow.IsNone());
		TestEqual(TEXT("우두머리 모자를 씀"), Hook->GetEquipment()->GetEquippedItem(EEquipSlot::Head), ChiefHatRow);
		TestTrue(TEXT("몸이 보임"), Hook->GetBody()->IsVisible());
		TestTrue(TEXT("기본 방향 = 카메라 쪽"), FMath::Abs(FRotator::NormalizeAxis(Hook->GetFacingYaw() - 180.f)) < 1.f);

		Hook->SetStoryMark(SnareStory::EGiverMark::Bang);
		TestTrue(TEXT("머리 위 「!」 가 켜짐"), Hook->IsStoryMarkShown());

		// 곁에 선 폰 — 대화 거리 안이지만 「열기」 대상이 아니고 화면도 안 올린다.
		ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(Hook->GetActorLocation() + FVector(0.f, 100.f, 0.f), FRotator::ZeroRotator);
		if (TestNotNull(TEXT("플레이어"), Player))
		{
			Player->DispatchBeginPlay();
			Player->GetMeleeAttack()->SetNpcTalkRangeCm(TestTalkRangeCm);
			TestTrue(TEXT("대화 거리 안"), Hook->IsPawnInTalkRange(Player));
			TestNull(TEXT("상황 버튼 「열기」 대상 아님"), AShopVendorActor::FindInTalkRange(Player));
			TestTrue(TEXT("곁 검사는 참"), Hook->UpdateProximity(Player));
			TestNull(TEXT("열 판 없음"), Hook->GetScreen());
			TestTrue(TEXT("곁이면 폰 쪽을 돌아봄(옆 요 90 → 카메라 쪽 한도까지)"), FMath::Abs(FRotator::NormalizeAxis(Hook->ComputeFacingTargetYaw(Player) - (180.f - AShopVendorActor::MaxTalkTurnDegrees))) < 1.f);
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// 상인 5종 — 덮는 모자를 쓰면 머리카락이 아랫단 밑만 남는다(플레이어와 같은 `UpdateHair` 규칙, INBOX 보정 10-19 22:45 ②).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenVendorHairUnderHatTest, "Snarehaven.Hat.Vendor.HairUnderHat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenVendorHairUnderHatTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	for (const EShopKind Kind : {EShopKind::General, EShopKind::Fishing, EShopKind::Farming, EShopKind::Clothes, EShopKind::SweetPotato})
	{
		AShopVendorActor* const Vendor = World->SpawnActor<AShopVendorActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("상인"), Vendor))
		{
			continue;
		}
		Vendor->SetShopKind(Kind);
		Vendor->DispatchBeginPlay();
		const UStaticMeshComponent* const Hat = Vendor->GetEquipment()->GetEquippedMesh(EEquipSlot::Head);
		const UStaticMeshComponent* const Hair = Vendor->GetHairMesh();
		if (TestNotNull(TEXT("모자"), Hat) && TestNotNull(TEXT("머리카락"), Hair))
		{
			// 플레이어와 같은 규칙 — 덮는 모자 밑으로는 머리카락 아랫단만 남고(통째로 숨기면 뒤통수가 맨살), 색은 종류마다.
			// 모자 폭은 행의 설계 폭이다(상인만 다른 규칙 금지).
			UEquipmentComponent* const Equip = Vendor->GetEquipment();
			TestTrue(FString::Printf(TEXT("[종류 %d] 모자 밑 머리카락은 아랫단 아래만 (자르는 높이 %.1f)"), static_cast<int32>(Kind), Equip->GetHairTopZ()),
				Equip->GetHairTopZ() < TNumericLimits<float>::Max());
			// 고구마 장수는 일요일 말고는 몸째 숨는다(`ApplySundayOnly`) — 머리카락은 몸과 같이 보이고 숨는다.
			TestEqual(FString::Printf(TEXT("[종류 %d] 머리카락 보임 = 몸 보임"), static_cast<int32>(Kind)), Hair->IsVisible(), Vendor->GetBody()->IsVisible());
			TestTrue(FString::Printf(TEXT("[종류 %d] 머리카락 컴포넌트가 장비 컴포넌트의 것"), static_cast<int32>(Kind)), Equip->GetHairMesh() == Hair);
			FLinearColor Color;
			const UMaterialInterface* const HairMaterial = Hair->GetMaterial(0);
			TestTrue(FString::Printf(TEXT("[종류 %d] 머리카락 색 = 종류 색"), static_cast<int32>(Kind)), HairMaterial != nullptr
				&& HairMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("DiffuseColor")), Color)
				&& Color.Equals(AShopVendorActor::GetKindHair(Kind).Color));
			const FVector Extent = Hat->GetStaticMesh()->GetBounds().BoxExtent * 2.f * Hat->GetComponentScale();
			const double Width = FMath::Max(Extent.X, Extent.Y);
			const double WantWidth = Vendor->GetEquipment()->GetDefaultHatWidthCm();
			AddInfo(FString::Printf(TEXT("[종류 %d] 모자 폭 %.1f (기본 %.1f)"), static_cast<int32>(Kind), Width, WantWidth));
			TestTrue(FString::Printf(TEXT("[종류 %d] 모자 폭 > 0"), static_cast<int32>(Kind)), Width > 1.0);
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// 머리 칸 모든 모자(장비·꾸미기 70) — 실제 정점 기준으로 머리 공을 뚫지 않고(머리 공 안쪽 정점 0) 정수리를 덮는다(떠 있지 않음).
// 경계상자가 아니라 메시 정점이라 오목한 챙·운두도 걸린다. 머리 공은 머리카락 껍질 크기로 잰다(상인 몸 = 플레이어와 같은 소켓 규칙).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAllHatsVertexTest, "Snarehaven.Hat.Vendor.AllHatsVertex", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAllHatsVertexTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	AShopVendorActor* const Vendor = World->SpawnActor<AShopVendorActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (TestNotNull(TEXT("상인"), Vendor))
	{
		Vendor->SetShopKind(EShopKind::General);
		Vendor->DispatchBeginPlay();
		UEquipmentComponent* const Equip = Vendor->GetEquipment();
		const UStaticMeshComponent* const Hair = Vendor->GetHairMesh();
		const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
		if (TestNotNull(TEXT("장비"), Equip) && TestNotNull(TEXT("머리카락"), Hair) && TestNotNull(TEXT("표"), ItemTable))
		{
			// 머리 공: 머리 ×1.04 (머리카락 메시 = 머리 ×1.07 — 예전 상인 껍질 ×0.97 크기 그대로, 시험 전용 값), 정수리(소켓 원점)에서 반지름만큼 아래가 중심.
			// 상인 머리카락이 플레이어와 같은 ×1.0 으로 바뀌어도(INBOX 보정 10-19 22:45 ②) 공 크기는 머리에서 잰다.
			const FBoxSphereBounds HairBounds = Hair->CalcBounds(Hair->GetComponentTransform());
			const double Radius = FMath::Max(HairBounds.BoxExtent.X, HairBounds.BoxExtent.Y) / 1.07 * 1.04;
			int32 HatCount = 0;
			for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
			{
				const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
				if (Row == nullptr || Row->EquipSlot != EEquipSlot::Head)
				{
					continue;
				}
				// INBOX 결함 10-20 00:10 ⑤ — 장비 13종만이 아니라 꾸미기 57종까지 머리 70종 전부(꾸미기는 겉모습 덮개 길).
				const bool bCosmetic = Row->ItemType == EItemType::Cosmetic;
				Equip->SetCosmetic(EEquipSlot::Head, NAME_None);
				if (!TestTrue(FString::Printf(TEXT("[%s] 상인 머리에 썼다"), *Pair.Key.ToString()),
					bCosmetic ? Equip->SetCosmetic(EEquipSlot::Head, Pair.Key) : Equip->EquipItem(Pair.Key)))
				{
					continue;
				}
				const UStaticMeshComponent* const Hat = Equip->GetEquippedMesh(EEquipSlot::Head);
				if (Hat == nullptr || Hat->GetStaticMesh() == nullptr || !Hat->GetStaticMesh()->GetRenderData() || Hat->GetStaticMesh()->GetRenderData()->LODResources.Num() == 0)
				{
					continue;
				}
				++HatCount;
				const FVector Crown = Hat->GetComponentLocation();
				const FVector Center = Crown - FVector(0, 0, Radius);
				const FPositionVertexBuffer& Positions = Hat->GetStaticMesh()->GetRenderData()->LODResources[0].VertexBuffers.PositionVertexBuffer;
				const FTransform Xf = Hat->GetComponentTransform();
				int32 Inside = 0;
				double MinZ = TNumericLimits<double>::Max();
				for (uint32 i = 0; i < Positions.GetNumVertices(); ++i)
				{
					const FVector P = Xf.TransformPosition(FVector(Positions.VertexPosition(i)));
					Inside += (P - Center).Size() < Radius * 0.9 ? 1 : 0;
					MinZ = FMath::Min(MinZ, P.Z);
				}
				const double Drop = Crown.Z - MinZ;
				AddInfo(FString::Printf(TEXT("[%s] 머리공 안 정점 %d · 정수리 아래로 %.1fcm (반지름 %.1f)"), *Pair.Key.ToString(), Inside, Drop, Radius));
				TestEqual(FString::Printf(TEXT("[%s] 머리 관통 정점 0"), *Pair.Key.ToString()), Inside, 0);
				TestTrue(FString::Printf(TEXT("[%s] 정수리를 덮는다(떠 있지 않음)"), *Pair.Key.ToString()), Drop >= Radius * 0.25);
			}
			Equip->SetCosmetic(EEquipSlot::Head, NAME_None);
			TestEqual(TEXT("머리 70종 전부 검사 (장비 13 + 꾸미기 57)"), HatCount, 70);
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoryPopupOrderTest, "Snarehaven.Story.PopupOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoryPopupOrderTest::RunTest(const FString& Parameters)
{
	// 장 끝(등대 → 카드)과 대사 판이 한 화면에 하나씩 (감독 보정 10-20 17:15). 캐릭터가 부르는 차례 그대로 장 1~5 를 같은 길로 돌린다.
	const int32 ChapterCount = SnareStory::TotalSteps / SnareStory::StepsPerChapter;
	for (int32 Chapter = 1; Chapter <= ChapterCount; ++Chapter)
	{
		SnareStory::FStoryView View;
		View.Step = Chapter * SnareStory::StepsPerChapter;
		TestEqual(FString::Printf(TEXT("%d장 — 끝낸 장"), Chapter), SnareStory::CompletedChapter(View), Chapter);

		SnareStory::FPopupQueue Queue;
		// ① 같은 답에 둘 다 오면 캐릭터는 대사부터 청한다
		TestTrue(FString::Printf(TEXT("%d장 — 대사가 먼저 뜬다"), Chapter), Queue.RequestDialogue(/*bChapterEndOpen=*/false));
		TestFalse(FString::Printf(TEXT("%d장 — 대사가 열려 있으면 장 끝은 안 연다"), Chapter), Queue.RequestChapterEnd(Chapter, true, true, false));
		TestEqual(FString::Printf(TEXT("%d장 — 미룬 장"), Chapter), Queue.WaitingChapter, Chapter);
		TestFalse(FString::Printf(TEXT("%d장 — 대사 중 같은 답이 또 와도 안 연다"), Chapter), Queue.RequestChapterEnd(Chapter, true, true, false));

		// ② 마지막 대사를 닫으면 등대 → 카드
		bool bPersist = false;
		TestEqual(FString::Printf(TEXT("%d장 — 대사를 닫으면 미룬 장을 꺼낸다"), Chapter), Queue.TakeChapterAfterDialogue(bPersist), Chapter);
		TestTrue(FString::Printf(TEXT("%d장 — 본 장 저장 그대로"), Chapter), bPersist);
		TestTrue(FString::Printf(TEXT("%d장 — 꺼낸 장 끝은 바로 연다"), Chapter), Queue.RequestChapterEnd(Chapter, bPersist, false, false));
		TestEqual(FString::Printf(TEXT("%d장 — 두 번 꺼내지 않는다"), Chapter), Queue.TakeChapterAfterDialogue(bPersist), 0);

		// ③ 장 끝 동안 오는 대사(다음 단계 답)는 카드 닫을 때까지 미룬다 · 같은 장 끝은 또 안 연다
		TestFalse(FString::Printf(TEXT("%d장 — 장 끝 동안 대사 안 뜸"), Chapter), Queue.RequestDialogue(/*bChapterEndOpen=*/true));
		TestFalse(FString::Printf(TEXT("%d장 — 장 끝 동안 장 끝 또 안 열림"), Chapter), Queue.RequestChapterEnd(Chapter, true, false, true));
		TestEqual(FString::Printf(TEXT("%d장 — 열린 장 끝을 미룬 줄에 또 안 쌓는다"), Chapter), Queue.WaitingChapter, 0);

		// ④ 카드를 닫으면 미룬 대사
		TestTrue(FString::Printf(TEXT("%d장 — 카드를 닫으면 미룬 대사"), Chapter), Queue.TakeDialogueAfterChapterEnd());
		TestTrue(FString::Printf(TEXT("%d장 — 그 대사는 이제 뜬다"), Chapter), Queue.RequestDialogue(/*bChapterEndOpen=*/false));
		TestFalse(FString::Printf(TEXT("%d장 — 미룬 대사는 한 번만"), Chapter), Queue.TakeDialogueAfterChapterEnd());
	}

	SnareStory::FPopupQueue Alone;
	TestTrue(TEXT("대사 없이 장 끝만 오면 바로 연다"), Alone.RequestChapterEnd(2, true, false, false));
	TestFalse(TEXT("끝낸 장 0 은 안 연다"), Alone.RequestChapterEnd(0, true, false, false));
	TestFalse(TEXT("미룬 대사 없으면 카드 닫아도 대사 없음"), Alone.TakeDialogueAfterChapterEnd());

	SnareStory::FPopupQueue Preview;
	Preview.RequestChapterEnd(3, /*bPersist=*/false, true, false);
	bool bPreviewPersist = true;
	TestEqual(TEXT("검사 스위치 장 끝도 같은 줄"), Preview.TakeChapterAfterDialogue(bPreviewPersist), 3);
	TestFalse(TEXT("검사 스위치는 본 장 저장 끔이 이어진다"), bPreviewPersist);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
