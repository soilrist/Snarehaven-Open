// ROADMAP M6-01 2층 검사 — 존 표(`DT_ZoneTable`)가 성한지.
// 아직 이 표를 읽는 런타임 코드는 없다(포탈은 M6-02) — 그래서 이 검사는 **표 자체**만 본다.
// 보는 것 둘: ① 모든 행의 `LevelName` 이 실제로 있는 맵인가 ② `ConnectedZoneIDs` 가 표 안의 `ZoneID` 인가.
// 없는 맵 이름이 한 칸이라도 들어가면 포탈이 "이동 중" 에서 멈추고(엔진이 못 찾는다), 없는 존 ID 가
// 들어가면 포탈이 갈 곳 없이 선다 — 둘 다 게임 안에서야 드러나는 종류라 표 단계에서 잡는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Zone; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"

namespace
{
	const TCHAR* ZoneTablePath = TEXT("/Game/Game/Data/World/DT_ZoneTable.DT_ZoneTable");

	// 맵이 디스크에 있는지 보는 기본 판정. `OpenLevel`·`ServerTravel` 이 짧은 이름으로 맵을 찾는 것과
	// 같은 길(`SearchForPackageOnDisk`)이라 폴더 경로를 이 검사에 박지 않아도 된다.
	bool DoesMapExistOnDisk(const FName LevelName)
	{
		FString PackageName = LevelName.ToString();
		return FPackageName::SearchForPackageOnDisk(PackageName, &PackageName);
	}

	// 행들을 훑어 문제를 모은다. 바로 AddError 하지 않고 돌려주는 이유 — 아래 자기검사가
	// "이 검사가 정말 잡는가" 를 에셋 없이 다시 볼 수 있게 (SnarehavenDataIntegrityTest 와 같은 짜임).
	// MapExists 를 인자로 받는 것도 같은 이유다: 자기검사가 디스크를 안 건드리고 없는 맵을 흉내낸다.
	TArray<FString> CollectZoneIssues(const TMap<FName, FZoneRow>& Rows,
		TFunctionRef<bool(FName)> MapExists)
	{
		TArray<FString> Issues;

		TSet<FName> KnownZoneIDs;
		for (const TPair<FName, FZoneRow>& RowPair : Rows)
		{
			if (!RowPair.Value.ZoneID.IsNone())
			{
				KnownZoneIDs.Add(RowPair.Value.ZoneID);
			}
		}

		for (const TPair<FName, FZoneRow>& RowPair : Rows)
		{
			const FString RowName = RowPair.Key.ToString();
			const FZoneRow& Row = RowPair.Value;

			if (Row.ZoneID.IsNone())
			{
				Issues.Add(FString::Printf(TEXT("[%s] ZoneID 가 비었다"), *RowName));
			}

			if (Row.LevelName.IsNone())
			{
				Issues.Add(FString::Printf(TEXT("[%s] LevelName 이 비었다"), *RowName));
			}
			else if (!MapExists(Row.LevelName))
			{
				Issues.Add(FString::Printf(TEXT("[%s] LevelName 이 없는 맵을 가리킨다: %s"),
					*RowName, *Row.LevelName.ToString()));
			}

			for (const FName& ConnectedID : Row.ConnectedZoneIDs)
			{
				if (!KnownZoneIDs.Contains(ConnectedID))
				{
					Issues.Add(FString::Printf(TEXT("[%s] 이어지는 존 %s 가 표에 없다"),
						*RowName, *ConnectedID.ToString()));
				}
			}
		}

		return Issues;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneTableTest, "Snarehaven.Zone.Table.RowsPointSomewhereReal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneTableTest::RunTest(const FString& Parameters)
{
	UDataTable* ZoneTable = LoadObject<UDataTable>(nullptr, ZoneTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}

	TMap<FName, FZoneRow> Rows;
	for (const FName& RowName : ZoneTable->GetRowNames())
	{
		if (const FZoneRow* Row = ZoneTable->FindRow<FZoneRow>(RowName, TEXT("ZoneTableTest")))
		{
			Rows.Add(RowName, *Row);
		}
	}

	// 행이 0개면 위의 두 검사가 아무것도 안 보고 조용히 통과한다 (Verification.md — 빈 검사 금지).
	if (!TestTrue(TEXT("존 행이 하나 이상 있다"), Rows.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DT_ZoneTable 행 %d개를 본다"), Rows.Num()));

	for (const FString& Issue : CollectZoneIssues(Rows, &DoesMapExistOnDisk))
	{
		AddError(Issue);
	}

	return true;
}


// 위 검사가 "표가 멀쩡해서" 통과할 때도 검사 로직이 살아 있는지 본다 (역검증).
// 에셋을 건드리지 않고 행을 손으로 지어 먹인다 — 검사 전용 값이라 표의 숫자·이름에 기대지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneTableCheckSelfTest,
	"Snarehaven.Zone.Table.CheckCatchesBrokenRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneTableCheckSelfTest::RunTest(const FString& Parameters)
{
	// 검사용 가짜 맵 둘. 이 이름만 "있는 맵" 으로 쳐 준다.
	const FName ExistingMap(TEXT("L_TestZoneA"));
	const FName MissingMap(TEXT("L_TestZoneNoSuchMap"));
	auto MapExists = [&ExistingMap](const FName LevelName) { return LevelName == ExistingMap; };

	FZoneRow SoundRow;
	SoundRow.ZoneID = FName(TEXT("zone.test_a"));
	SoundRow.LevelName = ExistingMap;
	SoundRow.DisplayName = FText::FromString(TEXT("검사용 존 A"));

	// 멀쩡한 표까지 물면 검사가 쓸모없어진다 — 반대쪽부터 본다.
	TMap<FName, FZoneRow> SoundRows;
	SoundRows.Add(FName(TEXT("Zone_TestA")), SoundRow);
	TestEqual(TEXT("멀쩡한 한 행은 문제 없음"), CollectZoneIssues(SoundRows, MapExists).Num(), 0);

	// 서로 가리키는 두 행도 통과해야 한다 (실제 표가 이 모양이다).
	FZoneRow SecondRow = SoundRow;
	SecondRow.ZoneID = FName(TEXT("zone.test_b"));
	SoundRows[FName(TEXT("Zone_TestA"))].ConnectedZoneIDs.Add(SecondRow.ZoneID);
	SecondRow.ConnectedZoneIDs.Add(SoundRow.ZoneID);
	SoundRows.Add(FName(TEXT("Zone_TestB")), SecondRow);
	TestEqual(TEXT("서로 이어진 두 행은 문제 없음"), CollectZoneIssues(SoundRows, MapExists).Num(), 0);

	// ① 없는 맵을 가리키는 행
	TMap<FName, FZoneRow> BrokenMapRows;
	FZoneRow BrokenMapRow = SoundRow;
	BrokenMapRow.LevelName = MissingMap;
	BrokenMapRows.Add(FName(TEXT("Zone_TestA")), BrokenMapRow);
	TestEqual(TEXT("없는 맵은 문제 1건"), CollectZoneIssues(BrokenMapRows, MapExists).Num(), 1);

	// ② 표에 없는 존으로 이어진 행
	TMap<FName, FZoneRow> DanglingRows;
	FZoneRow DanglingRow = SoundRow;
	DanglingRow.ConnectedZoneIDs.Add(FName(TEXT("zone.test_nowhere")));
	DanglingRows.Add(FName(TEXT("Zone_TestA")), DanglingRow);
	TestEqual(TEXT("표에 없는 존으로 이어지면 문제 1건"), CollectZoneIssues(DanglingRows, MapExists).Num(), 1);

	// ③ 칸이 빈 행
	TMap<FName, FZoneRow> EmptyRows;
	EmptyRows.Add(FName(TEXT("Zone_TestEmpty")), FZoneRow());
	TestEqual(TEXT("ZoneID·LevelName 이 둘 다 비면 문제 2건"), CollectZoneIssues(EmptyRows, MapExists).Num(), 2);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
