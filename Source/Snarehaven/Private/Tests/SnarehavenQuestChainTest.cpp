// DESIGN 8.14 「의뢰 줄기」 2층 검사 — 표(`DT_QuestChain`)와 규칙 함수(`SnareQuestChain`).
// 잡는 것: ① 표가 5단계 순서대로 있고 미정(-1)·빈 칸이 없다 ② 숫자가 DESIGN 줄과 같다
// ③ 단계 건너뛰기·되돌리기·끝난 뒤 재수령·재료 모자람이 각자 맞는 실패 코드로 막힌다.
// ③ 은 검사 전용 표를 메모리에서 만들어 본다 (밸런스 숫자가 바뀌어도 안 깨지게, Verification.md 6).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareQuestChain.h"
#include "Data/QuestChainRow.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	UDataTable* MakeTestTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		Table->RowStruct = FQuestChainRow::StaticStruct();
		for (int32 Step = 1; Step <= 3; ++Step)
		{
			FQuestChainRow Row;
			Row.Needs.Add({FName(TEXT("item.t_a")), Step});
			Row.RewardCoins = Step;
			Table->AddRow(SnareQuestChain::RowNameFor(Step), Row);
		}
		return Table;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenQuestChainRulesTest, "Snarehaven.Quest.Chain.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenQuestChainRulesTest::RunTest(const FString& Parameters)
{
	using namespace SnareQuestChain;
	const UDataTable* Table = MakeTestTable();
	TMap<FName, int32> Rich;
	Rich.Add(FName(TEXT("item.t_a")), 99);
	TMap<FName, int32> Poor;
	Poor.Add(FName(TEXT("item.t_a")), 1);

	TestEqual(TEXT("단계 수"), StepCount(Table), 3);
	TestFalse(TEXT("1단계는 끝 아님"), IsFinished(Table, 1));
	TestTrue(TEXT("단계 수 + 1 이면 끝"), IsFinished(Table, 4));
	TestTrue(TEXT("재료 충분 + 지금 단계 = Ok"), CanComplete(Table, 2, 2, Rich) == EResult::Ok);
	TestTrue(TEXT("앞 단계로 되돌리기 = Locked"), CanComplete(Table, 2, 1, Rich) == EResult::Locked);
	TestTrue(TEXT("건너뛰기 = Locked"), CanComplete(Table, 1, 3, Rich) == EResult::Locked);
	TestTrue(TEXT("없는 단계 = Locked"), CanComplete(Table, 1, 9, Rich) == EResult::Locked);
	TestTrue(TEXT("재료 모자람 = Short"), CanComplete(Table, 2, 2, Poor) == EResult::Short);
	TestTrue(TEXT("끝난 뒤 재수령 = Done"), CanComplete(Table, 4, 3, Rich) == EResult::Done);
	TestEqual(TEXT("Locked 코드"), CodeFor(EResult::Locked), FString(CodeLocked));
	TestEqual(TEXT("Done 코드"), CodeFor(EResult::Done), FString(CodeDone));
	TestEqual(TEXT("Short 코드"), CodeFor(EResult::Short), FString(CodeShort));
	TestTrue(TEXT("Ok 는 코드 없음"), CodeFor(EResult::Ok).IsEmpty());
	TestEqual(TEXT("받으면 +1"), StepAfter(Table, 2, 2, Rich), 3);
	TestEqual(TEXT("실패하면 그대로 (Short)"), StepAfter(Table, 2, 2, Poor), 2);
	TestEqual(TEXT("실패하면 그대로 (Locked)"), StepAfter(Table, 2, 3, Rich), 2);
	TestEqual(TEXT("끝까지 받으면 단계 수 + 1"), StepAfter(Table, 3, 3, Rich), 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenQuestChainDesignTest, "Snarehaven.Quest.Chain.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenQuestChainDesignTest::RunTest(const FString& Parameters)
{
	using namespace SnareQuestChain;
	const UDataTable* Table = LoadTable();
	if (!TestNotNull(TEXT("DT_QuestChain 로드"), Table))
	{
		return false;
	}
	TestEqual(TEXT("5단계"), StepCount(Table), 5);
	TestEqual(TEXT("6단계 행은 없다"), Table->GetRowNames().Num(), 5);

	FString Design;
	FFileHelper::LoadFileToString(Design, *FPaths::Combine(FPaths::ProjectDir(), TEXT("docs/DESIGN.md")));
	TArray<FString> Lines;
	Design.ParseIntoArrayLines(Lines);

	int32 CoinSum = 0;
	for (int32 Step = 1; Step <= 5; ++Step)
	{
		const FQuestChainRow* Row = FindStep(Table, Step);
		if (!TestNotNull(*FString::Printf(TEXT("chain.%d 행"), Step), Row))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("chain.%d 재료가 있다"), Step), Row->Needs.Num() > 0);
		TestTrue(*FString::Printf(TEXT("chain.%d 코인 정해짐"), Step), Row->RewardCoins >= 0);
		CoinSum += Row->RewardCoins;

		const FString Prefix = FString::Printf(TEXT("| %d단계 「"), Step);
		const FString* DesignLine = Lines.FindByPredicate([&Prefix](const FString& L) { return L.StartsWith(Prefix); });
		if (!TestNotNull(*FString::Printf(TEXT("DESIGN %d단계 줄"), Step), DesignLine))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%d단계 이름 `%s` 이 DESIGN 줄에"), Step, *Row->Title.ToString()),
			DesignLine->Contains(Row->Title.ToString()));
		for (const FQuestChainNeed& Need : Row->Needs)
		{
			TestTrue(*FString::Printf(TEXT("%d단계 재료 %s 정해짐"), Step, *Need.ItemId.ToString()),
				Need.Count > 0 && !Need.ItemId.IsNone());
			TestTrue(*FString::Printf(TEXT("%d단계 DESIGN 줄에 개수 **%d**"), Step, Need.Count),
				DesignLine->Contains(FString::Printf(TEXT("**%d**"), Need.Count)));
		}
		TestTrue(*FString::Printf(TEXT("%d단계 DESIGN 줄에 코인 **%d**"), Step, Row->RewardCoins),
			DesignLine->Contains(FString::Printf(TEXT("**%d코인"), Row->RewardCoins)));
		if (Step == 5)
		{
			TestEqual(TEXT("마지막 보상은 열쇠패"), Row->RewardItemId, FName(TEXT("item.vault_pass")));
			TestEqual(TEXT("열쇠패 1장"), Row->RewardItemCount, 1);
		}
		else
		{
			TestTrue(*FString::Printf(TEXT("%d단계는 아이템 보상 없음"), Step), Row->RewardItemId.IsNone());
		}
	}
	const FString SumText = FString::Printf(TEXT("코인 **%d**"), CoinSum);
	TestTrue(*FString::Printf(TEXT("DESIGN 보상 합계 줄에 %s"), *SumText), Design.Contains(SumText));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
