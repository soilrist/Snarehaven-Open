// INBOX 지시 2026-10-11 23:30 2층 검사 — 레퍼런스 게임 고유 꼴 이름이 우리 표시 이름에 0개.
// 목록은 `docs/specs/item-name-diff.md` 표에서 읽는다: `바꿈` 줄의 「지금 이름」(2열)은 표시 이름에 하나도 없어야 하고,
// 「새 이름」(4열)은 그 ID 행의 표시 이름이어야 한다. 표 형식: `| ID | 지금 이름 | 판정 | 새 이름 |`.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Data.ItemNames; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** 띄어쓰기·부호를 지운 글자 — 「에너지 드링크: 순한맛」 과 「에너지드링크:순한맛」 을 같게 본다 (INBOX 보정 2026-10-19). */
	FString StripSpacesAndMarks(const FString& Text)
	{
		FString Out;
		for (const TCHAR Ch : Text)
		{
			if (FChar::IsAlnum(Ch) || Ch > 0x7F)
			{
				Out.AppendChar(Ch);
			}
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenItemNameDiffTest, "Snarehaven.Data.ItemNames.NoReferenceOnlyNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenItemNameDiffTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	FString Doc;
	if (!TestNotNull(TEXT("DT_ItemTable 을 연다"), ItemTable)
		|| !TestTrue(TEXT("item-name-diff.md 읽기"),
			FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/specs/item-name-diff.md")))))
	{
		return false;
	}

	// 레퍼런스 게임 자료의 `"name_ko": "…"` 줄 — 새 이름이 띄어쓰기·부호만 달라도 같으면 안 된다. 자료 파일이 없는 PC 에선 이 칸만 건너뛴다.
	TSet<FString> ReferenceStripped;
	FString ReferenceDoc;
	const bool bHasReference = FFileHelper::LoadFileToString(
		ReferenceDoc, *(FPaths::ProjectDir() / TEXT("_Source/reference/reference/vinternote_items_360.txt")));
	if (bHasReference)
	{
		TArray<FString> LvLines;
		ReferenceDoc.ParseIntoArrayLines(LvLines);
		for (const FString& LvLine : LvLines)
		{
			const FString Key = TEXT("\"name_ko\":");
			int32 At = LvLine.Find(Key);
			FString Rest = At == INDEX_NONE ? FString() : LvLine.Mid(At + Key.Len());
			Rest.TrimStartAndEndInline();
			Rest.TrimCharInline(TEXT(','), nullptr);
			Rest.TrimQuotesInline();
			if (!Rest.IsEmpty())
			{
				ReferenceStripped.Add(StripSpacesAndMarks(Rest));
			}
		}
	}

	TMap<FString, FString> NameByID;
	TSet<FString> AllNames;
	TSet<FString> AllNamesStripped;
	for (const TPair<FName, uint8*>& RowPair : ItemTable->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(RowPair.Value);
		NameByID.Add(Row->ItemID.ToString(), Row->DisplayName.ToString());
		AllNames.Add(Row->DisplayName.ToString());
		AllNamesStripped.Add(StripSpacesAndMarks(Row->DisplayName.ToString()));
	}

	TArray<FString> Lines;
	Doc.ParseIntoArrayLines(Lines);
	int32 Changed = 0;
	for (const FString& Line : Lines)
	{
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), /*InCullEmpty=*/false);
		if (Cells.Num() < 6 || Cells[3].TrimStartAndEnd() != TEXT("바꿈"))
		{
			continue;
		}
		++Changed;
		const FString ID = Cells[1].TrimStartAndEnd();
		const FString Old = Cells[2].TrimStartAndEnd();
		const FString New = Cells[4].TrimStartAndEnd();
		TestFalse(FString::Printf(TEXT("레퍼런스 게임 고유 꼴 「%s」 가 표시 이름에 남았다"), *Old), AllNames.Contains(Old));
		TestFalse(FString::Printf(TEXT("레퍼런스 게임 고유 꼴 「%s」 가 띄어쓰기·부호만 달라진 채 표시 이름에 남았다"), *Old),
			AllNamesStripped.Contains(StripSpacesAndMarks(Old)));
		if (bHasReference)
		{
			TestFalse(FString::Printf(TEXT("새 이름 「%s」 가 레퍼런스 게임 이름과 띄어쓰기·부호만 다르다"), *New),
				ReferenceStripped.Contains(StripSpacesAndMarks(New)));
		}
		const FString* const Now = NameByID.Find(ID);
		if (TestNotNull(FString::Printf(TEXT("%s 행이 있다"), *ID), Now))
		{
			TestEqual(FString::Printf(TEXT("%s 표시 이름 = 표의 새 이름"), *ID), *Now, New);
		}
	}
	// 표가 비어 조용히 통과하지 않게
	TestTrue(TEXT("바꿈 줄이 60개 넘게 있다"), Changed > 60);
	if (bHasReference)
	{
		TestTrue(TEXT("레퍼런스 게임 이름을 읽었다"), ReferenceStripped.Num() > 300);
	}
	AddInfo(FString::Printf(TEXT("[ItemNames] 바꿈 %d줄 확인"), Changed));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
