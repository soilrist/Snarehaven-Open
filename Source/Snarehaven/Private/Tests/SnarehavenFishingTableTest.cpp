// ROADMAP M10-01 2층 검사 — 낚시터 어종 표(`DT_FishingTable`)가 DESIGN 8.4 와 같은가.
// 아직 이 표를 읽는 런타임 코드는 없다(낚시 슬롯은 M10-02) — 그래서 이 검사는 **표 자체**만 본다.
// 보는 것 넷: ① 낚시 시간이 DESIGN 8.4 표와 같은가 ② 문서에 있는 어종이 표에도 다 있는가(양방향)
// ③ `ProducedItemID` 가 `DT_ItemTable` 에 있는 아이템인가 ④ `MaxStack` 이 목장 표와 같은 값인가.
//
// **이 검사는 숫자를 하나도 들고 있지 않다.** 15·30·60 을 여기 적으면 사본이 셋이 되어, 검사 자신이
// 막으려던 "한쪽만 고치기" 사고를 저지른다 — 설계서에서 읽어 표와 맞댈 뿐이다
// (작물 대조 `Snarehaven.Crop.Table.MatchesDesign` 과 같은 짜임).
// 상한 5 도 적지 않는다 — DESIGN 8.4 가 "목장·작물과 동일" 이라고 했으니 **목장 표에서 읽어** 비교한다.
//
// 사이클당 코인(5·10·20)은 검사 대상이 아니다 — 어느 표에도 없고 백엔드가 판정한다 (CLAUDE.md 8).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Fishing; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/FishingRow.h"
#include "Data/ItemRow.h"
#include "Data/RanchProductRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* FishingTablePath = TEXT("/Game/Game/Data/Fishing/DT_FishingTable.DT_FishingTable");
	const TCHAR* RanchTablePath = TEXT("/Game/Game/Data/Ranch/DT_RanchProductTable.DT_RanchProductTable");
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	/** `### 8.4 …` 부터 다음 제목 전까지만 잘라 낸다 — 설계서의 다른 표까지 긁으면 엉뚱한 값이 섞인다. */
	FString ExtractSection(const FString& Doc, const FString& HeadingPrefix)
	{
		const int32 Start = Doc.Find(HeadingPrefix, ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return FString();
		}

		const int32 BodyStart = Start + HeadingPrefix.Len();
		const int32 NextHeading = Doc.Find(TEXT("\n#"), ESearchCase::CaseSensitive,
			ESearchDir::FromStart, BodyStart);
		return NextHeading == INDEX_NONE ? Doc.Mid(BodyStart) : Doc.Mid(BodyStart, NextHeading - BodyStart);
	}

	/**
	 * `| 붕어 | `item.crucian_carp` | 15분 | … |` 같은 줄에서 아이템 ID 와 낚시 시간(분)을 꺼낸다.
	 * 아이템 ID 에 역따옴표를 요구해서 머리줄(`| 어종 | 아이템 | …`)이 딸려 들어오지 않는다.
	 */
	TMap<FString, int32> ParseFishingMinutes(const FString& Section)
	{
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);

		const FRegexPattern RowPattern(TEXT("^\\|[^|]+\\|\\s*`([a-z0-9_.]+)`\\s*\\|\\s*([0-9]+)\\s*분"));
		TMap<FString, int32> Minutes;
		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(RowPattern, Line);
			if (Matcher.FindNext())
			{
				Minutes.Add(Matcher.GetCaptureGroup(1), FCString::Atoi(*Matcher.GetCaptureGroup(2)));
			}
		}
		return Minutes;
	}

	// 행들을 훑어 문제를 모은다. 바로 AddError 하지 않고 돌려주는 이유 — 아래 자기검사가
	// "이 검사가 정말 잡는가" 를 에셋·문서 없이 다시 볼 수 있게 (작물 검사와 같은 짜임).
	TArray<FString> CollectFishingIssues(const TMap<FName, FFishingRow>& Rows,
		const TMap<FString, int32>& DesignMinutes,
		const TSet<FName>& KnownItemIDs,
		int32 RanchMaxStack)
	{
		TArray<FString> Issues;

		// 설계서를 한 줄도 못 읽었으면 "비교할 게 없어 전부 통과" 가 된다 — 그게 제일 위험하다.
		if (DesignMinutes.Num() == 0)
		{
			Issues.Add(TEXT("DESIGN 8.4 에서 어종 줄을 하나도 못 읽었다"));
			return Issues;
		}

		// 목장 상한을 못 읽었으면 ④번을 조용히 건너뛰게 된다.
		if (RanchMaxStack <= 0)
		{
			Issues.Add(TEXT("목장 표에서 MaxStack 을 못 읽었다 (DESIGN 8.4 '목장·작물과 동일' 을 맞댈 수 없다)"));
			return Issues;
		}

		TSet<FString> SeenItemIDs;
		for (const TPair<FName, FFishingRow>& RowPair : Rows)
		{
			const FString RowName = RowPair.Key.ToString();
			const FFishingRow& Row = RowPair.Value;

			if (Row.ProducedItemID.IsNone())
			{
				Issues.Add(FString::Printf(TEXT("[%s] ProducedItemID 가 비었다"), *RowName));
				continue;
			}

			const FString ItemID = Row.ProducedItemID.ToString();
			SeenItemIDs.Add(ItemID);

			if (!KnownItemIDs.Contains(Row.ProducedItemID))
			{
				Issues.Add(FString::Printf(TEXT("[%s] ProducedItemID %s 가 아이템 표에 없다"), *RowName, *ItemID));
			}

			const int32* const DesignMinute = DesignMinutes.Find(ItemID);
			if (DesignMinute == nullptr)
			{
				Issues.Add(FString::Printf(TEXT("[%s] %s 는 DESIGN 8.4 표에 없는 어종이다"), *RowName, *ItemID));
			}
			else if (Row.CycleMinutes != *DesignMinute)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 낚시 시간이 DESIGN 8.4 와 다르다 (문서 %d분 / 표 %d분)"),
					*RowName, *DesignMinute, Row.CycleMinutes));
			}

			if (Row.MaxStack != RanchMaxStack)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 미수확 상한이 목장과 다르다 (목장 %d / 표 %d)"),
					*RowName, RanchMaxStack, Row.MaxStack));
			}
		}

		// 반대 방향 — 설계서가 정한 어종이 표에서 빠지면 "적게 만든 것" 이라 위 반복문이 못 본다.
		for (const TPair<FString, int32>& DesignPair : DesignMinutes)
		{
			if (!SeenItemIDs.Contains(DesignPair.Key))
			{
				Issues.Add(FString::Printf(TEXT("DESIGN 8.4 의 어종 %s 가 표에 없다"), *DesignPair.Key));
			}
		}

		return Issues;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFishingTableTest,
	"Snarehaven.Fishing.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFishingTableTest::RunTest(const FString& Parameters)
{
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
	FString Doc;
	if (!TestTrue(*FString::Printf(TEXT("설계서를 읽는다 (%s)"), *DocPath),
		FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}

	const FString Section = ExtractSection(Doc, TEXT("### 8.4"));
	if (!TestTrue(TEXT("DESIGN 8.4 절을 찾았다"), !Section.IsEmpty()))
	{
		return false;
	}

	const TMap<FString, int32> DesignMinutes = ParseFishingMinutes(Section);
	if (!TestTrue(TEXT("DESIGN 8.4 에서 어종 줄을 하나 이상 읽었다"), DesignMinutes.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DESIGN 8.4 에서 어종 %d종을 읽었다"), DesignMinutes.Num()));

	UDataTable* FishingTable = LoadObject<UDataTable>(nullptr, FishingTablePath);
	if (!TestNotNull(TEXT("DT_FishingTable 로드"), FishingTable))
	{
		return false;
	}

	TMap<FName, FFishingRow> Rows;
	for (const FName& RowName : FishingTable->GetRowNames())
	{
		if (const FFishingRow* Row = FishingTable->FindRow<FFishingRow>(RowName, TEXT("FishingTableTest")))
		{
			Rows.Add(RowName, *Row);
		}
	}

	// 행이 0개면 아래 비교가 아무것도 안 보고 조용히 통과한다 (Verification.md — 빈 검사 금지).
	if (!TestTrue(TEXT("어종 행이 하나 이상 있다"), Rows.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DT_FishingTable 행 %d개를 본다"), Rows.Num()));

	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}
	TSet<FName> KnownItemIDs;
	for (const FName& RowName : ItemTable->GetRowNames())
	{
		if (const FItemRow* Row = ItemTable->FindRow<FItemRow>(RowName, TEXT("FishingTableTest")))
		{
			KnownItemIDs.Add(Row->ItemID);
		}
	}

	// 상한은 목장 표에서 읽는다 — DESIGN 8.4 가 "새 경제 밸런스 축을 만들지 않고 목장·작물과 동일" 이라 했다.
	UDataTable* RanchTable = LoadObject<UDataTable>(nullptr, RanchTablePath);
	if (!TestNotNull(TEXT("DT_RanchProductTable 로드"), RanchTable))
	{
		return false;
	}
	int32 RanchMaxStack = 0;
	for (const FName& RowName : RanchTable->GetRowNames())
	{
		if (const FRanchProductRow* Row = RanchTable->FindRow<FRanchProductRow>(RowName, TEXT("FishingTableTest")))
		{
			RanchMaxStack = Row->MaxStack;
			break;
		}
	}
	AddInfo(FString::Printf(TEXT("목장 표의 미수확 상한 %d 을 기준으로 본다"), RanchMaxStack));

	for (const FString& Issue : CollectFishingIssues(Rows, DesignMinutes, KnownItemIDs, RanchMaxStack))
	{
		AddError(Issue);
	}

	return true;
}


// 위 검사가 "표가 멀쩡해서" 통과할 때도 검사 로직이 살아 있는지 본다 (역검증).
// 에셋·설계서를 건드리지 않고 행과 문서 값을 손으로 지어 먹인다 — 검사 전용 값이라
// 진짜 표의 숫자에 기대지 않는다 (Verification.md 6).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFishingTableCheckSelfTest,
	"Snarehaven.Fishing.Table.CheckCatchesBrokenRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFishingTableCheckSelfTest::RunTest(const FString& Parameters)
{
	// 검사용 가짜 설계서 값·가짜 표. 전부 이 검사 안에서만 쓰는 값이다.
	const int32 TestMaxStack = 7;
	TMap<FString, int32> DesignMinutes;
	DesignMinutes.Add(TEXT("item.testfish"), 11);

	const TSet<FName> KnownItemIDs = { FName(TEXT("item.testfish")) };

	FFishingRow SoundRow;
	SoundRow.ProducedItemID = FName(TEXT("item.testfish"));
	SoundRow.CycleMinutes = 11;
	SoundRow.MaxStack = TestMaxStack;

	// 멀쩡한 표까지 물면 검사가 쓸모없어진다 — 반대쪽부터 본다.
	TMap<FName, FFishingRow> SoundRows;
	SoundRows.Add(FName(TEXT("Fishing_Test")), SoundRow);
	TestEqual(TEXT("멀쩡한 한 행은 문제 없음"),
		CollectFishingIssues(SoundRows, DesignMinutes, KnownItemIDs, TestMaxStack).Num(), 0);

	auto IssuesWith = [&](TFunctionRef<void(FFishingRow&)> Break)
	{
		FFishingRow Broken = SoundRow;
		Break(Broken);
		TMap<FName, FFishingRow> BrokenRows;
		BrokenRows.Add(FName(TEXT("Fishing_Test")), Broken);
		return CollectFishingIssues(BrokenRows, DesignMinutes, KnownItemIDs, TestMaxStack).Num();
	};

	// ① 낚시 시간이 설계서와 어긋나면 1건
	TestEqual(TEXT("낚시 시간이 다르면 문제 1건"),
		IssuesWith([](FFishingRow& R) { R.CycleMinutes += 1; }), 1);

	// ② 상한이 목장과 다르면 1건 (설계서 "목장·작물과 동일")
	TestEqual(TEXT("상한이 목장과 다르면 문제 1건"),
		IssuesWith([TestMaxStack](FFishingRow& R) { R.MaxStack = TestMaxStack + 1; }), 1);

	// ③ 자리표시(-1)가 그대로 남으면 — 시간·상한 둘 다 걸려 2건
	TestEqual(TEXT("자리표시 -1 이 남으면 문제 2건"),
		IssuesWith([](FFishingRow& R) { R.CycleMinutes = -1; R.MaxStack = -1; }), 2);

	// ④ 설계서에 없는 어종이면 — "아이템 표에 없다" + "설계서에 없다" + 반대 방향 "표에서 빠졌다" 3건
	TestEqual(TEXT("설계서에 없는 어종이면 문제 3건"),
		IssuesWith([](FFishingRow& R) { R.ProducedItemID = FName(TEXT("item.ghost")); }), 3);

	// ⑤ 아이템 표에 없는 ID 면 — 그 한 줄만 (설계서 값은 맞춰 준다)
	{
		TMap<FString, int32> TwoFish = DesignMinutes;
		TwoFish.Add(TEXT("item.ghost"), 11);
		FFishingRow Ghost = SoundRow;
		Ghost.ProducedItemID = FName(TEXT("item.ghost"));
		TMap<FName, FFishingRow> GhostRows;
		GhostRows.Add(FName(TEXT("Fishing_Ghost")), Ghost);
		// 설계서에는 둘인데 표에는 하나 — "아이템 표에 없다" 1건 + "item.testfish 가 표에 없다" 1건
		TestEqual(TEXT("아이템 표에 없는 ID 는 잡힌다"),
			CollectFishingIssues(GhostRows, TwoFish, KnownItemIDs, TestMaxStack).Num(), 2);
	}

	// ⑥ 설계서를 못 읽었으면 조용히 통과하지 않는다
	TestEqual(TEXT("설계서를 못 읽으면 문제 1건"),
		CollectFishingIssues(SoundRows, TMap<FString, int32>(), KnownItemIDs, TestMaxStack).Num(), 1);

	// ⑦ 목장 상한을 못 읽었어도 조용히 통과하지 않는다
	TestEqual(TEXT("목장 상한을 못 읽으면 문제 1건"),
		CollectFishingIssues(SoundRows, DesignMinutes, KnownItemIDs, 0).Num(), 1);

	// ⑧ 표가 설계서보다 적으면 (행 0개는 본 검사가 따로 막지만, 로직 자체도 잡아야 한다)
	TestEqual(TEXT("표에서 어종이 빠지면 문제 1건"),
		CollectFishingIssues(TMap<FName, FFishingRow>(), DesignMinutes, KnownItemIDs, TestMaxStack).Num(), 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
