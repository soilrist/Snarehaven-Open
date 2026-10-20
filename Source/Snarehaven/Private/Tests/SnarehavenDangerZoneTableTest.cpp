// ROADMAP M6-04a 2층 검사 — 위험구역 표(`DT_DangerZoneTable`)가 DESIGN 8.1 과 같은가.
// 아직 이 표를 읽는 런타임 코드는 없다(스포너는 M6-04b) — 그래서 이 검사는 **표 자체**만 본다.
// 보는 것 셋: ① 숫자가 DESIGN 8.1 표와 같은가 ② `ZoneID` 가 `DT_ZoneTable` 에 있는 존인가
// ③ `AnimalRowName` 이 `DT_AnimalTable` 에 있는 행인가.
//
// **이 검사는 숫자를 하나도 들고 있지 않다.** 500·5·300 을 여기 적으면 사본이 셋이 되어,
// 검사 자신이 막으려던 "한쪽만 고치기" 사고를 저지른다 — 설계서에서 읽어 표와 맞댈 뿐이다
// (M7-02 확률 공개 대조 `Snarehaven.Gacha.Disclosure.TableMatchesDocument` 와 같은 짜임).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.DangerZone; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/AnimalRow.h"
#include "Data/DangerZoneRow.h"
#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* DangerTablePath = TEXT("/Game/Game/Data/World/DT_DangerZoneTable.DT_DangerZoneTable");
	const TCHAR* ZoneTablePath = TEXT("/Game/Game/Data/World/DT_ZoneTable.DT_ZoneTable");
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");

	// DESIGN 8.1 표의 왼쪽 칸 그대로. **여기 있는 것은 이름뿐이고 값은 문서에서 읽는다.**
	const TCHAR* RadiusLabel = TEXT("구역 크기 (반지름)");
	const TCHAR* CountLabel = TEXT("구역 안 동물 수");
	const TCHAR* RespawnLabel = TEXT("재생성 간격");

	/** `### 8.1 …` 부터 다음 제목 전까지만 잘라 낸다 — 설계서의 다른 표까지 긁으면 엉뚱한 값이 섞인다. */
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

	/** `| 구역 안 동물 수 | 5마리 | 근거 |` 같은 줄에서 왼쪽 칸 이름과 값 칸의 첫 숫자를 꺼낸다. */
	TMap<FString, double> ParseLabelledNumbers(const FString& Section)
	{
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);

		const FRegexPattern RowPattern(TEXT("^\\|\\s*([^|]+?)\\s*\\|\\s*([0-9]+(?:\\.[0-9]+)?)"));
		TMap<FString, double> Values;
		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(RowPattern, Line);
			if (Matcher.FindNext())
			{
				Values.Add(Matcher.GetCaptureGroup(1), FCString::Atod(*Matcher.GetCaptureGroup(2)));
			}
		}
		return Values;
	}

	// 행들을 훑어 문제를 모은다. 바로 AddError 하지 않고 돌려주는 이유 — 아래 자기검사가
	// "이 검사가 정말 잡는가" 를 에셋·문서 없이 다시 볼 수 있게 (ZoneTable 검사와 같은 짜임).
	TArray<FString> CollectDangerZoneIssues(const TMap<FName, FDangerZoneRow>& Rows,
		const TMap<FString, double>& DesignValues,
		const TSet<FName>& KnownZoneIDs,
		const TSet<FName>& KnownAnimalRows)
	{
		TArray<FString> Issues;

		// 설계서 칸 하나라도 못 읽었으면 "비교할 게 없어 전부 통과" 가 된다 — 그게 제일 위험하다.
		const TCHAR* Labels[] = { RadiusLabel, CountLabel, RespawnLabel };
		for (const TCHAR* Label : Labels)
		{
			if (!DesignValues.Contains(Label))
			{
				Issues.Add(FString::Printf(TEXT("DESIGN 8.1 에서 '%s' 줄을 못 읽었다"), Label));
			}
		}
		if (Issues.Num() > 0)
		{
			return Issues;
		}

		for (const TPair<FName, FDangerZoneRow>& RowPair : Rows)
		{
			const FString RowName = RowPair.Key.ToString();
			const FDangerZoneRow& Row = RowPair.Value;

			if (Row.ZoneID.IsNone())
			{
				Issues.Add(FString::Printf(TEXT("[%s] ZoneID 가 비었다"), *RowName));
			}
			else if (!KnownZoneIDs.Contains(Row.ZoneID))
			{
				Issues.Add(FString::Printf(TEXT("[%s] ZoneID %s 가 존 표에 없다"),
					*RowName, *Row.ZoneID.ToString()));
			}

			if (Row.AnimalRowName.IsNone())
			{
				Issues.Add(FString::Printf(TEXT("[%s] AnimalRowName 이 비었다"), *RowName));
			}
			else if (!KnownAnimalRows.Contains(Row.AnimalRowName))
			{
				Issues.Add(FString::Printf(TEXT("[%s] AnimalRowName %s 가 동물 표에 없다"),
					*RowName, *Row.AnimalRowName.ToString()));
			}

			const double DesignRadius = DesignValues[RadiusLabel];
			if (!FMath::IsNearlyEqual(static_cast<double>(Row.RadiusCm), DesignRadius, 0.01))
			{
				Issues.Add(FString::Printf(TEXT("[%s] 반지름이 DESIGN 8.1 과 다르다 (문서 %.2f / 표 %.2f)"),
					*RowName, DesignRadius, Row.RadiusCm));
			}

			const int32 DesignCount = FMath::RoundToInt(DesignValues[CountLabel]);
			if (Row.AnimalCount != DesignCount)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 동물 수가 DESIGN 8.1 과 다르다 (문서 %d / 표 %d)"),
					*RowName, DesignCount, Row.AnimalCount));
			}

			const int32 DesignRespawn = FMath::RoundToInt(DesignValues[RespawnLabel]);
			if (Row.RespawnSeconds != DesignRespawn)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 재생성 간격이 DESIGN 8.1 과 다르다 (문서 %d / 표 %d)"),
					*RowName, DesignRespawn, Row.RespawnSeconds));
			}
		}

		return Issues;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDangerZoneTableTest,
	"Snarehaven.DangerZone.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDangerZoneTableTest::RunTest(const FString& Parameters)
{
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
	FString Doc;
	if (!TestTrue(*FString::Printf(TEXT("설계서를 읽는다 (%s)"), *DocPath),
		FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}

	const FString Section = ExtractSection(Doc, TEXT("### 8.1"));
	if (!TestTrue(TEXT("DESIGN 8.1 절을 찾았다"), !Section.IsEmpty()))
	{
		return false;
	}

	const TMap<FString, double> DesignValues = ParseLabelledNumbers(Section);
	if (!TestTrue(TEXT("DESIGN 8.1 에서 숫자 줄을 하나 이상 읽었다"), DesignValues.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DESIGN 8.1 에서 숫자 줄 %d개를 읽었다"), DesignValues.Num()));

	UDataTable* DangerTable = LoadObject<UDataTable>(nullptr, DangerTablePath);
	if (!TestNotNull(TEXT("DT_DangerZoneTable 로드"), DangerTable))
	{
		return false;
	}

	TMap<FName, FDangerZoneRow> Rows;
	for (const FName& RowName : DangerTable->GetRowNames())
	{
		const FDangerZoneRow* Row = DangerTable->FindRow<FDangerZoneRow>(RowName, TEXT("DangerZoneTableTest"));
		// 수명 있는 행은 월드 이벤트(8.1b)라 숫자가 8.1 과 다르다 — `Snarehaven.WorldEvent.Table.MatchesDesign` 이 본다.
		if (Row != nullptr && Row->LifetimeSeconds <= 0)
		{
			Rows.Add(RowName, *Row);
		}
	}

	// 행이 0개면 아래 비교가 아무것도 안 보고 조용히 통과한다 (Verification.md — 빈 검사 금지).
	if (!TestTrue(TEXT("위험구역 행이 하나 이상 있다"), Rows.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DT_DangerZoneTable 행 %d개를 본다"), Rows.Num()));

	UDataTable* ZoneTable = LoadObject<UDataTable>(nullptr, ZoneTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}
	TSet<FName> KnownZoneIDs;
	for (const FName& RowName : ZoneTable->GetRowNames())
	{
		if (const FZoneRow* Row = ZoneTable->FindRow<FZoneRow>(RowName, TEXT("DangerZoneTableTest")))
		{
			KnownZoneIDs.Add(Row->ZoneID);
		}
	}

	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable))
	{
		return false;
	}
	const TSet<FName> KnownAnimalRows(AnimalTable->GetRowNames());

	for (const FString& Issue : CollectDangerZoneIssues(Rows, DesignValues, KnownZoneIDs, KnownAnimalRows))
	{
		AddError(Issue);
	}

	return true;
}


// 위 검사가 "표가 멀쩡해서" 통과할 때도 검사 로직이 살아 있는지 본다 (역검증).
// 에셋·설계서를 건드리지 않고 행과 문서 값을 손으로 지어 먹인다 — 검사 전용 값이라
// 진짜 표의 숫자에 기대지 않는다 (Verification.md 6).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDangerZoneTableCheckSelfTest,
	"Snarehaven.DangerZone.Table.CheckCatchesBrokenRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDangerZoneTableCheckSelfTest::RunTest(const FString& Parameters)
{
	// 검사용 가짜 설계서 값·가짜 표. 전부 이 검사 안에서만 쓰는 값이다.
	TMap<FString, double> DesignValues;
	DesignValues.Add(RadiusLabel, 111.0);
	DesignValues.Add(CountLabel, 2.0);
	DesignValues.Add(RespawnLabel, 33.0);

	const TSet<FName> KnownZoneIDs = { FName(TEXT("zone.test")) };
	const TSet<FName> KnownAnimalRows = { FName(TEXT("Animal_Test")) };

	FDangerZoneRow SoundRow;
	SoundRow.ZoneID = FName(TEXT("zone.test"));
	SoundRow.AnimalRowName = FName(TEXT("Animal_Test"));
	SoundRow.RadiusCm = 111.0f;
	SoundRow.AnimalCount = 2;
	SoundRow.RespawnSeconds = 33;

	// 멀쩡한 표까지 물면 검사가 쓸모없어진다 — 반대쪽부터 본다.
	TMap<FName, FDangerZoneRow> SoundRows;
	SoundRows.Add(FName(TEXT("Danger_Test")), SoundRow);
	TestEqual(TEXT("멀쩡한 한 행은 문제 없음"),
		CollectDangerZoneIssues(SoundRows, DesignValues, KnownZoneIDs, KnownAnimalRows).Num(), 0);

	// ① 숫자 셋이 각각 설계서와 어긋나면 각각 1건
	auto IssuesWith = [&](TFunctionRef<void(FDangerZoneRow&)> Break)
	{
		FDangerZoneRow Broken = SoundRow;
		Break(Broken);
		TMap<FName, FDangerZoneRow> BrokenRows;
		BrokenRows.Add(FName(TEXT("Danger_Test")), Broken);
		return CollectDangerZoneIssues(BrokenRows, DesignValues, KnownZoneIDs, KnownAnimalRows).Num();
	};
	TestEqual(TEXT("반지름이 다르면 문제 1건"), IssuesWith([](FDangerZoneRow& R) { R.RadiusCm += 1.0f; }), 1);
	TestEqual(TEXT("동물 수가 다르면 문제 1건"), IssuesWith([](FDangerZoneRow& R) { R.AnimalCount += 1; }), 1);
	TestEqual(TEXT("재생성 간격이 다르면 문제 1건"), IssuesWith([](FDangerZoneRow& R) { R.RespawnSeconds += 1; }), 1);

	// ② 표에 없는 존·동물을 가리키면 각각 1건
	TestEqual(TEXT("없는 존을 가리키면 문제 1건"),
		IssuesWith([](FDangerZoneRow& R) { R.ZoneID = FName(TEXT("zone.nowhere")); }), 1);
	TestEqual(TEXT("없는 동물 행을 가리키면 문제 1건"),
		IssuesWith([](FDangerZoneRow& R) { R.AnimalRowName = FName(TEXT("Animal_NoSuch")); }), 1);

	// ③ 기본값 그대로인 행(미정 -1)은 이름 둘 + 숫자 셋 = 5건
	TMap<FName, FDangerZoneRow> EmptyRows;
	EmptyRows.Add(FName(TEXT("Danger_Empty")), FDangerZoneRow());
	TestEqual(TEXT("빈 행은 문제 5건"),
		CollectDangerZoneIssues(EmptyRows, DesignValues, KnownZoneIDs, KnownAnimalRows).Num(), 5);

	// ④ 설계서 줄을 못 읽으면 조용히 통과하지 않고 그 자리에서 운다
	TMap<FString, double> MissingDesign = DesignValues;
	MissingDesign.Remove(CountLabel);
	TestEqual(TEXT("설계서 줄을 못 읽으면 문제 1건"),
		CollectDangerZoneIssues(SoundRows, MissingDesign, KnownZoneIDs, KnownAnimalRows).Num(), 1);

	// ⑤ 설계서 절 자르기·숫자 읽기 자체도 본다 (문서 모양이 바뀌면 여기서 걸린다).
	const FString FakeDoc = TEXT("### 8.0 앞 절\n| 구역 안 동물 수 | 99마리 |\n\n### 8.1 뒤 절\n")
		TEXT("| 항목 | 값 |\n|---|---|\n| 구역 안 동물 수 | 7마리 | 근거 |\n\n## 9. 다음\n| 구역 안 동물 수 | 1마리 |\n");
	const TMap<FString, double> Parsed = ParseLabelledNumbers(ExtractSection(FakeDoc, TEXT("### 8.1")));
	TestEqual(TEXT("8.1 절 안의 값만 읽는다"), Parsed.Num(), 1);
	TestTrue(TEXT("읽은 값이 8.1 절의 것이다"),
		Parsed.Contains(CountLabel) && FMath::IsNearlyEqual(Parsed[CountLabel], 7.0, 0.01));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
