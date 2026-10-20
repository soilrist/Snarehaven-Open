// ROADMAP M6-05a·M13-01 2층 검사 — 거점 작물 표(`DT_CropProductTable`)가 DESIGN 8.2 + 13.3 과 같은가.
// 이 검사는 **표 자체**만 본다. 보는 것 넷: ① 재배 시간이 설계서 표와 같은가 ② 문서에 있는 작물이 표에도
// 다 있는가(양방향) ③ `ProducedItemID`·`SeedItemID` 가 `DT_ItemTable` 에 있는 아이템인가 ④ 씨앗이 설계서와
// 같은가(8.2 는 8.10 `item.seed_<작물>`, 13.3 은 씨앗 열). 미수확 상한은 없다 — ⑩ 부터 한 번 거두면 칸이 빈다.
//
// **이 검사는 숫자를 하나도 들고 있지 않다.** 15·30·60 을 여기 적으면 사본이 셋이 되어, 검사 자신이
// 막으려던 "한쪽만 고치기" 사고를 저지른다 — 설계서에서 읽어 표와 맞댈 뿐이다
// (M6-04a 위험구역 대조 `Snarehaven.DangerZone.Table.MatchesDesign` 과 같은 짜임).
//
// 사이클당 코인(5·10·20)은 검사 대상이 아니다 — 어느 표에도 없고 백엔드가 판정한다 (CLAUDE.md 8).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Crop; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/CropProductRow.h"
#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* CropTablePath = TEXT("/Game/Game/Data/Ranch/DT_CropProductTable.DT_CropProductTable");
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	/** `### 8.2 …` 부터 다음 제목 전까지만 잘라 낸다 — 설계서의 다른 표까지 긁으면 엉뚱한 값이 섞인다. */
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

	/** 설계서 한 줄이 정한 작물 — 재배 시간(분)과 씨앗 아이템 ID. */
	struct FDesignCrop
	{
		int32 Minutes = 0;
		FString SeedID;
	};

	/**
	 * 8.2 `| 당근 | `item.carrot` | 15분 | … |` 줄에서 아이템 ID 와 재배 시간(분)을 꺼낸다. 8.2 에는 씨앗 열이
	 * 없어 8.10 규칙 `item.seed_<작물>` 로 적는다. 아이템 ID 에 역따옴표를 요구해서 머리줄이 딸려 들어오지 않는다.
	 */
	void ParseCrops82(const FString& Section, TMap<FString, FDesignCrop>& Out)
	{
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);

		const FRegexPattern RowPattern(TEXT("^\\|[^|]+\\|\\s*`(item\\.([a-z0-9_]+))`\\s*\\|\\s*([0-9]+)\\s*분"));
		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(RowPattern, Line);
			if (Matcher.FindNext())
			{
				Out.Add(Matcher.GetCaptureGroup(1),
					{ FCString::Atoi(*Matcher.GetCaptureGroup(3)), TEXT("item.seed_") + Matcher.GetCaptureGroup(2) });
			}
		}
	}

	/** 13.3 `| 작물 | 원본 | `item.x`(있음) | `item.seed_y` | 15분 | 5 | … |` 줄 — 씨앗은 열에서 그대로 읽는다. */
	void ParseCrops133(const FString& Section, TMap<FString, FDesignCrop>& Out)
	{
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);

		const FRegexPattern RowPattern(TEXT(
			"^\\|[^|]+\\|[^|]+\\|\\s*`(item\\.[a-z0-9_]+)`[^|]*\\|\\s*`(item\\.seed_[a-z0-9_]+)`\\s*\\|\\s*([0-9]+)분"));
		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(RowPattern, Line);
			if (Matcher.FindNext())
			{
				Out.Add(Matcher.GetCaptureGroup(1),
					{ FCString::Atoi(*Matcher.GetCaptureGroup(3)), Matcher.GetCaptureGroup(2) });
			}
		}
	}

	// 행들을 훑어 문제를 모은다. 바로 AddError 하지 않고 돌려주는 이유 — 아래 자기검사가
	// "이 검사가 정말 잡는가" 를 에셋·문서 없이 다시 볼 수 있게 (DangerZone 검사와 같은 짜임).
	TArray<FString> CollectCropIssues(const TMap<FName, FCropProductRow>& Rows,
		const TMap<FString, FDesignCrop>& DesignCrops,
		const TSet<FName>& KnownItemIDs)
	{
		TArray<FString> Issues;

		// 설계서를 한 줄도 못 읽었으면 "비교할 게 없어 전부 통과" 가 된다 — 그게 제일 위험하다.
		if (DesignCrops.Num() == 0)
		{
			Issues.Add(TEXT("DESIGN 8.2·13.3 에서 작물 줄을 하나도 못 읽었다"));
			return Issues;
		}

		TSet<FString> SeenItemIDs;
		for (const TPair<FName, FCropProductRow>& RowPair : Rows)
		{
			const FString RowName = RowPair.Key.ToString();
			const FCropProductRow& Row = RowPair.Value;

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

			if (!KnownItemIDs.Contains(Row.SeedItemID))
			{
				Issues.Add(FString::Printf(TEXT("[%s] SeedItemID %s 가 아이템 표에 없다"), *RowName, *Row.SeedItemID.ToString()));
			}

			const FDesignCrop* const Design = DesignCrops.Find(ItemID);
			if (Design == nullptr)
			{
				Issues.Add(FString::Printf(TEXT("[%s] %s 는 DESIGN 8.2·13.3 표에 없는 작물이다"), *RowName, *ItemID));
				continue;
			}
			if (Row.CycleMinutes != Design->Minutes)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 재배 시간이 설계서와 다르다 (문서 %d분 / 표 %d분)"),
					*RowName, Design->Minutes, Row.CycleMinutes));
			}
			if (Row.SeedItemID.ToString() != Design->SeedID)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 씨앗이 설계서와 다르다 (문서 %s / 표 %s)"),
					*RowName, *Design->SeedID, *Row.SeedItemID.ToString()));
			}
		}

		// 반대 방향 — 설계서가 정한 작물이 표에서 빠지면 "적게 만든 것" 이라 위 반복문이 못 본다.
		for (const TPair<FString, FDesignCrop>& DesignPair : DesignCrops)
		{
			if (!SeenItemIDs.Contains(DesignPair.Key))
			{
				Issues.Add(FString::Printf(TEXT("설계서의 작물 %s 가 표에 없다"), *DesignPair.Key));
			}
		}

		return Issues;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCropTableTest,
	"Snarehaven.Crop.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCropTableTest::RunTest(const FString& Parameters)
{
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
	FString Doc;
	if (!TestTrue(*FString::Printf(TEXT("설계서를 읽는다 (%s)"), *DocPath),
		FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}

	const FString Section82 = ExtractSection(Doc, TEXT("### 8.2"));
	const FString Section133 = ExtractSection(Doc, TEXT("### 13.3"));
	if (!TestTrue(TEXT("DESIGN 8.2·13.3 절을 찾았다"), !Section82.IsEmpty() && !Section133.IsEmpty()))
	{
		return false;
	}

	TMap<FString, FDesignCrop> DesignCrops;
	ParseCrops82(Section82, DesignCrops);
	const int32 Count82 = DesignCrops.Num();
	ParseCrops133(Section133, DesignCrops);
	if (!TestTrue(TEXT("8.2·13.3 에서 각각 작물 줄을 하나 이상 읽었다"), Count82 > 0 && DesignCrops.Num() > Count82))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DESIGN 8.2 에서 %d종 + 13.3 에서 %d종을 읽었다"), Count82, DesignCrops.Num() - Count82));

	UDataTable* CropTable = LoadObject<UDataTable>(nullptr, CropTablePath);
	if (!TestNotNull(TEXT("DT_CropProductTable 로드"), CropTable))
	{
		return false;
	}

	TMap<FName, FCropProductRow> Rows;
	for (const FName& RowName : CropTable->GetRowNames())
	{
		if (const FCropProductRow* Row = CropTable->FindRow<FCropProductRow>(RowName, TEXT("CropTableTest")))
		{
			Rows.Add(RowName, *Row);
		}
	}

	// 행이 0개면 아래 비교가 아무것도 안 보고 조용히 통과한다 (Verification.md — 빈 검사 금지).
	if (!TestTrue(TEXT("작물 행이 하나 이상 있다"), Rows.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DT_CropProductTable 행 %d개를 본다"), Rows.Num()));

	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("DT_ItemTable 로드"), ItemTable))
	{
		return false;
	}
	TSet<FName> KnownItemIDs;
	for (const FName& RowName : ItemTable->GetRowNames())
	{
		if (const FItemRow* Row = ItemTable->FindRow<FItemRow>(RowName, TEXT("CropTableTest")))
		{
			KnownItemIDs.Add(Row->ItemID);
		}
	}

	for (const FString& Issue : CollectCropIssues(Rows, DesignCrops, KnownItemIDs))
	{
		AddError(Issue);
	}

	return true;
}


// 위 검사가 "표가 멀쩡해서" 통과할 때도 검사 로직이 살아 있는지 본다 (역검증).
// 에셋·설계서를 건드리지 않고 행과 문서 값을 손으로 지어 먹인다 — 검사 전용 값이라
// 진짜 표의 숫자에 기대지 않는다 (Verification.md 6).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCropTableCheckSelfTest,
	"Snarehaven.Crop.Table.CheckCatchesBrokenRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCropTableCheckSelfTest::RunTest(const FString& Parameters)
{
	// 검사용 가짜 설계서 값·가짜 표. 전부 이 검사 안에서만 쓰는 값이다.
	TMap<FString, FDesignCrop> DesignMinutes;
	DesignMinutes.Add(TEXT("item.testcrop"), { 11, TEXT("item.seed_testcrop") });

	const TSet<FName> KnownItemIDs = { FName(TEXT("item.testcrop")), FName(TEXT("item.seed_testcrop")) };

	FCropProductRow SoundRow;
	SoundRow.ProducedItemID = FName(TEXT("item.testcrop"));
	SoundRow.SeedItemID = FName(TEXT("item.seed_testcrop"));
	SoundRow.CycleMinutes = 11;

	// 멀쩡한 표까지 물면 검사가 쓸모없어진다 — 반대쪽부터 본다.
	TMap<FName, FCropProductRow> SoundRows;
	SoundRows.Add(FName(TEXT("Crop_Test")), SoundRow);
	TestEqual(TEXT("멀쩡한 한 행은 문제 없음"),
		CollectCropIssues(SoundRows, DesignMinutes, KnownItemIDs).Num(), 0);

	auto IssuesWith = [&](TFunctionRef<void(FCropProductRow&)> Break)
	{
		FCropProductRow Broken = SoundRow;
		Break(Broken);
		TMap<FName, FCropProductRow> BrokenRows;
		BrokenRows.Add(FName(TEXT("Crop_Test")), Broken);
		return CollectCropIssues(BrokenRows, DesignMinutes, KnownItemIDs).Num();
	};

	// ① 재배 시간이 설계서와 어긋나면 1건
	TestEqual(TEXT("재배 시간이 다르면 문제 1건"),
		IssuesWith([](FCropProductRow& R) { R.CycleMinutes += 1; }), 1);

	// ② 자리표시(-1)가 그대로 남으면 1건
	TestEqual(TEXT("자리표시 -1 이 남으면 문제 1건"),
		IssuesWith([](FCropProductRow& R) { R.CycleMinutes = -1; }), 1);

	// ③ 설계서에 없는 작물이면 — "아이템 표에 없다" + "설계서에 없다" + 반대 방향 "표에서 빠졌다" 3건
	TestEqual(TEXT("설계서에 없는 작물이면 문제 3건"),
		IssuesWith([](FCropProductRow& R) { R.ProducedItemID = FName(TEXT("item.ghost")); }), 3);

	// ④ 아이템 표에 없는 ID 면 — 그 한 줄만 (설계서 값은 맞춰 준다)
	{
		TMap<FString, FDesignCrop> TwoCrops = DesignMinutes;
		TwoCrops.Add(TEXT("item.ghost"), { 11, TEXT("item.seed_testcrop") });
		FCropProductRow Ghost = SoundRow;
		Ghost.ProducedItemID = FName(TEXT("item.ghost"));
		TMap<FName, FCropProductRow> GhostRows;
		GhostRows.Add(FName(TEXT("Crop_Ghost")), Ghost);
		// 설계서에는 둘인데 표에는 하나 — "아이템 표에 없다" 1건 + "item.testcrop 이 표에 없다" 1건
		TestEqual(TEXT("아이템 표에 없는 ID 는 잡힌다"),
			CollectCropIssues(GhostRows, TwoCrops, KnownItemIDs).Num(), 2);
	}

	// ⑤ 설계서를 못 읽었으면 조용히 통과하지 않는다
	TestEqual(TEXT("설계서를 못 읽으면 문제 1건"),
		CollectCropIssues(SoundRows, TMap<FString, FDesignCrop>(), KnownItemIDs).Num(), 1);

	// ⑦ 씨앗이 설계서와 다르고 아이템 표에도 없으면 — "씨앗 다르다" + "아이템 표에 없다" 2건
	TestEqual(TEXT("씨앗이 다르면 잡힌다"),
		IssuesWith([](FCropProductRow& R) { R.SeedItemID = FName(TEXT("item.seed_ghost")); }), 2);

	// ⑥ 표가 설계서보다 적으면 (행 0개는 본 검사가 따로 막지만, 로직 자체도 잡아야 한다)
	TestEqual(TEXT("표에서 작물이 빠지면 문제 1건"),
		CollectCropIssues(TMap<FName, FCropProductRow>(), DesignMinutes, KnownItemIDs).Num(), 1);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
