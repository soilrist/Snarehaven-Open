// INBOX 지시 ★4(감독 2026-09-23) 2층 검사 — 맵 전역 분산 스폰.
// 보는 것 셋: ① `DT_AnimalSpreadTable` 의 숫자가 DESIGN 3.1 "동물 맵 전역 분산 스폰" 표와 같은가
// ② `ZoneID` 가 `DT_ZoneTable` 에 있는 존인가 ③ **그 숫자로 실제 자리가 나오는가** — 칸끼리
// 최소 간격 이상 떨어지고, 시작점 금지 반경 안에는 한 칸도 없고, 정원(16)만큼 칸이 남는가.
//
// ③ 이 이 검사의 핵심이다. 표가 설계서와 같아도 **맵이 좁으면 16마리가 물리적으로 안 들어간다** —
// 그때 조용히 12마리만 서면 아무도 모른다. 후보 칸 수를 세어 그 자리에서 운다.
//
// **이 검사는 숫자를 하나도 들고 있지 않다** (`Snarehaven.DangerZone.Table.MatchesDesign` 과 같은 짜임).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.AnimalSpread; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Data/AnimalSpreadRow.h"
#include "Data/DangerZoneRow.h"
#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Misc/Paths.h"
#include "World/AnimalSpreadActor.h"

namespace
{
	const TCHAR* SpreadTablePath = TEXT("/Game/Game/Data/World/DT_AnimalSpreadTable.DT_AnimalSpreadTable");
	const TCHAR* SpreadZoneTablePath = TEXT("/Game/Game/Data/World/DT_ZoneTable.DT_ZoneTable");
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* DangerTablePath = TEXT("/Game/Game/Data/World/DT_DangerZoneTable.DT_DangerZoneTable");

	// DESIGN 3.1 표의 왼쪽 칸 그대로. **여기 있는 것은 이름뿐이고 값은 문서에서 읽는다.**
	const TCHAR* CountLabel = TEXT("동시 서 있는 동물 수");
	const TCHAR* SpacingLabel = TEXT("동물끼리 최소 간격");
	const TCHAR* ClearLabel = TEXT("시작점 금지 반경");
	const TCHAR* RespawnLabel = TEXT("동물 재등장 대기");
	const TCHAR* ExtentLabel = TEXT("흩는 영역 반변");

	/** `### 3.1 …` 부터 다음 제목 전까지만 잘라 낸다 — 다른 절의 표까지 긁으면 엉뚱한 값이 섞인다. */
	FString ExtractSpreadSection(const FString& Doc, const FString& HeadingPrefix)
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

	/** `| 동시 서 있는 동물 수 | 16마리 | 근거 |` 같은 줄에서 이름 칸과 값 칸의 첫 숫자를 꺼낸다. */
	TMap<FString, double> ParseSpreadNumbers(const FString& Section)
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

	// 문제를 모아 돌려준다 — 아래 자기검사가 에셋·문서 없이 "이 검사가 정말 잡는가" 를 다시 본다.
	TArray<FString> CollectSpreadIssues(const TMap<FName, FAnimalSpreadRow>& Rows,
		const TMap<FString, double>& DesignValues,
		const TSet<FName>& KnownZoneIDs)
	{
		TArray<FString> Issues;

		// 설계서 칸 하나라도 못 읽었으면 "비교할 게 없어 전부 통과" 가 된다 — 그게 제일 위험하다.
		const TCHAR* Labels[] = { CountLabel, SpacingLabel, ClearLabel, RespawnLabel, ExtentLabel };
		for (const TCHAR* Label : Labels)
		{
			if (!DesignValues.Contains(Label))
			{
				Issues.Add(FString::Printf(TEXT("DESIGN 3.1 에서 '%s' 줄을 못 읽었다"), Label));
			}
		}
		if (Issues.Num() > 0)
		{
			return Issues;
		}

		for (const TPair<FName, FAnimalSpreadRow>& RowPair : Rows)
		{
			const FString RowName = RowPair.Key.ToString();
			const FAnimalSpreadRow& Row = RowPair.Value;

			if (Row.ZoneID.IsNone())
			{
				Issues.Add(FString::Printf(TEXT("[%s] ZoneID 가 비었다"), *RowName));
			}
			else if (!KnownZoneIDs.Contains(Row.ZoneID))
			{
				Issues.Add(FString::Printf(TEXT("[%s] ZoneID %s 가 존 표에 없다"),
					*RowName, *Row.ZoneID.ToString()));
			}

			const int32 DesignCount = FMath::RoundToInt(DesignValues[CountLabel]);
			if (Row.AnimalCount != DesignCount)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 동물 수가 DESIGN 3.1 과 다르다 (문서 %d / 표 %d)"),
					*RowName, DesignCount, Row.AnimalCount));
			}

			const int32 DesignRespawn = FMath::RoundToInt(DesignValues[RespawnLabel]);
			if (Row.RespawnSeconds != DesignRespawn)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 재등장 대기가 DESIGN 3.1 과 다르다 (문서 %d / 표 %d)"),
					*RowName, DesignRespawn, Row.RespawnSeconds));
			}

			const TPair<const TCHAR*, float> Distances[] = {
				{ SpacingLabel, Row.MinSpacingCm },
				{ ClearLabel, Row.PlayerStartClearCm },
				{ ExtentLabel, Row.AreaHalfExtentCm },
			};
			for (const TPair<const TCHAR*, float>& Pair : Distances)
			{
				const double Design = DesignValues[Pair.Key];
				if (!FMath::IsNearlyEqual(static_cast<double>(Pair.Value), Design, 0.01))
				{
					Issues.Add(FString::Printf(TEXT("[%s] '%s' 가 DESIGN 3.1 과 다르다 (문서 %.2f / 표 %.2f)"),
						*RowName, Pair.Key, Design, Pair.Value));
				}
			}

			// **숫자가 맞아도 자리가 안 나오면 소용없다** — 그 행의 값으로 칸을 깔아 본다.
			// 시작점은 영역 한가운데 하나로 본다(`L_Hunt` 의 `PlayerStart` 는 맵 중심 (0,0)).
			const TArray<FVector2D> Cells = AAnimalSpreadActor::MakeSpreadCells(
				Row.AreaHalfExtentCm, Row.MinSpacingCm, Row.PlayerStartClearCm, { FVector2D::ZeroVector });
			if (Cells.Num() < Row.AnimalCount)
			{
				Issues.Add(FString::Printf(
					TEXT("[%s] 정원 %d 마리인데 설 수 있는 칸이 %d개뿐이다 (반변 %.0f · 간격 %.0f · 금지 %.0f)"),
					*RowName, Row.AnimalCount, Cells.Num(), Row.AreaHalfExtentCm, Row.MinSpacingCm,
					Row.PlayerStartClearCm));
			}
		}

		return Issues;
	}

	/** 칸끼리 최소 간격을 지키는지 · 금지 반경 안에 든 칸이 있는지. 어긋난 칸 수를 돌려준다. */
	int32 CountSpacingViolations(const TArray<FVector2D>& Cells, float MinSpacingCm)
	{
		int32 Violations = 0;
		for (int32 A = 0; A < Cells.Num(); ++A)
		{
			for (int32 B = A + 1; B < Cells.Num(); ++B)
			{
				// 격자 칸 사이 거리는 최소 간격과 **똑같다** — 실수 오차만큼 여유를 준다.
				if (FVector2D::Distance(Cells[A], Cells[B]) < MinSpacingCm - 1.f)
				{
					++Violations;
				}
			}
		}
		return Violations;
	}

	int32 CountClearanceViolations(const TArray<FVector2D>& Cells, const FVector2D& Center, float ClearCm)
	{
		int32 Violations = 0;
		for (const FVector2D& Cell : Cells)
		{
			if (FVector2D::Distance(Center, Cell) < ClearCm)
			{
				++Violations;
			}
		}
		return Violations;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalSpreadTableTest,
	"Snarehaven.AnimalSpread.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalSpreadTableTest::RunTest(const FString& Parameters)
{
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
	FString Doc;
	if (!TestTrue(*FString::Printf(TEXT("설계서를 읽는다 (%s)"), *DocPath),
		FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}

	const FString Section = ExtractSpreadSection(Doc, TEXT("### 3.1"));
	if (!TestTrue(TEXT("DESIGN 3.1 절을 찾았다"), !Section.IsEmpty()))
	{
		return false;
	}

	const TMap<FString, double> DesignValues = ParseSpreadNumbers(Section);
	if (!TestTrue(TEXT("DESIGN 3.1 에서 숫자 줄을 하나 이상 읽었다"), DesignValues.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DESIGN 3.1 에서 숫자 줄 %d개를 읽었다"), DesignValues.Num()));

	UDataTable* SpreadTable = LoadObject<UDataTable>(nullptr, SpreadTablePath);
	if (!TestNotNull(TEXT("DT_AnimalSpreadTable 로드"), SpreadTable))
	{
		return false;
	}

	TMap<FName, FAnimalSpreadRow> Rows;
	for (const FName& RowName : SpreadTable->GetRowNames())
	{
		if (const FAnimalSpreadRow* Row =
				SpreadTable->FindRow<FAnimalSpreadRow>(RowName, TEXT("AnimalSpreadTableTest")))
		{
			Rows.Add(RowName, *Row);
		}
	}

	// 행이 0개면 아래 비교가 아무것도 안 보고 조용히 통과한다 (Verification.md — 빈 검사 금지).
	if (!TestTrue(TEXT("분산 스폰 행이 하나 이상 있다"), Rows.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DT_AnimalSpreadTable 행 %d개를 본다"), Rows.Num()));

	UDataTable* ZoneTable = LoadObject<UDataTable>(nullptr, SpreadZoneTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}
	TSet<FName> KnownZoneIDs;
	for (const FName& RowName : ZoneTable->GetRowNames())
	{
		if (const FZoneRow* Row = ZoneTable->FindRow<FZoneRow>(RowName, TEXT("AnimalSpreadTableTest")))
		{
			KnownZoneIDs.Add(Row->ZoneID);
		}
	}

	for (const FString& Issue : CollectSpreadIssues(Rows, DesignValues, KnownZoneIDs))
	{
		AddError(Issue);
	}

	// 실제 표 값으로 깐 칸이 규칙을 지키는지도 여기서 한 번 더 본다 (숫자가 아니라 결과로 판단 — PROMPT ③-12).
	for (const TPair<FName, FAnimalSpreadRow>& RowPair : Rows)
	{
		const FAnimalSpreadRow& Row = RowPair.Value;
		const TArray<FVector2D> Cells = AAnimalSpreadActor::MakeSpreadCells(
			Row.AreaHalfExtentCm, Row.MinSpacingCm, Row.PlayerStartClearCm, { FVector2D::ZeroVector });
		AddInfo(FString::Printf(TEXT("[%s] 설 수 있는 칸 %d개 (정원 %d)"),
			*RowPair.Key.ToString(), Cells.Num(), Row.AnimalCount));
		TestEqual(TEXT("칸끼리 최소 간격을 어긴 짝이 없다"),
			CountSpacingViolations(Cells, Row.MinSpacingCm), 0);
		TestEqual(TEXT("시작점 금지 반경 안에 든 칸이 없다"),
			CountClearanceViolations(Cells, FVector2D::ZeroVector, Row.PlayerStartClearCm), 0);
	}

	return true;
}


// 위 검사가 "표가 멀쩡해서" 통과할 때도 검사 로직이 살아 있는지 본다 (역검증).
// 에셋·설계서를 건드리지 않고 행과 문서 값을 손으로 지어 먹인다 — 전부 검사 전용 값이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalSpreadCheckSelfTest,
	"Snarehaven.AnimalSpread.Cells.CheckCatchesBrokenRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalSpreadCheckSelfTest::RunTest(const FString& Parameters)
{
	// 검사용 가짜 설계서 값·가짜 표 (진짜 숫자 16·1200·1500·90·2500 과 일부러 다르게 잡는다).
	TMap<FString, double> DesignValues;
	DesignValues.Add(CountLabel, 4.0);
	DesignValues.Add(SpacingLabel, 100.0);
	DesignValues.Add(ClearLabel, 50.0);
	DesignValues.Add(RespawnLabel, 7.0);
	DesignValues.Add(ExtentLabel, 200.0);

	const TSet<FName> KnownZoneIDs = { FName(TEXT("zone.test")) };

	FAnimalSpreadRow SoundRow;
	SoundRow.ZoneID = FName(TEXT("zone.test"));
	SoundRow.AnimalCount = 4;
	SoundRow.MinSpacingCm = 100.0f;
	SoundRow.PlayerStartClearCm = 50.0f;
	SoundRow.AreaHalfExtentCm = 200.0f;
	SoundRow.RespawnSeconds = 7;

	TMap<FName, FAnimalSpreadRow> SoundRows;
	SoundRows.Add(FName(TEXT("Spread_Test")), SoundRow);
	TestEqual(TEXT("멀쩡한 한 행은 문제 없음"),
		CollectSpreadIssues(SoundRows, DesignValues, KnownZoneIDs).Num(), 0);

	auto IssuesWith = [&](TFunctionRef<void(FAnimalSpreadRow&)> Break)
	{
		FAnimalSpreadRow Broken = SoundRow;
		Break(Broken);
		TMap<FName, FAnimalSpreadRow> BrokenRows;
		BrokenRows.Add(FName(TEXT("Spread_Test")), Broken);
		return CollectSpreadIssues(BrokenRows, DesignValues, KnownZoneIDs).Num();
	};
	TestEqual(TEXT("마리수가 다르면 문제 1건"), IssuesWith([](FAnimalSpreadRow& R) { R.AnimalCount -= 1; }), 1);
	TestEqual(TEXT("재등장 대기가 다르면 문제 1건"), IssuesWith([](FAnimalSpreadRow& R) { R.RespawnSeconds += 1; }), 1);
	TestEqual(TEXT("없는 존을 가리키면 문제 1건"),
		IssuesWith([](FAnimalSpreadRow& R) { R.ZoneID = FName(TEXT("zone.nowhere")); }), 1);

	// 간격을 영역보다 넓게 잡으면(반변 200 인데 간격 500 → 칸이 한가운데 하나뿐) 설계서와 어긋나는 것 1건 +
	// 칸이 모자라는 것 1건 둘 다 잡혀야 한다 — **"맵이 좁아 16마리가 안 들어간다" 를 잡는 자리가 이 줄이다.**
	TestEqual(TEXT("간격을 영역보다 넓히면 문서 불일치 + 칸 부족 2건"),
		IssuesWith([](FAnimalSpreadRow& R) { R.MinSpacingCm = 500.0f; }), 2);

	// 기본값 그대로인 행(미정 -1)은 존 이름 1 + 숫자 5 = 6건.
	// 칸 부족은 안 센다 — 정원도 -1 이라 "0칸인데 -1마리 세우라" 가 되어 모자랄 것이 없다.
	// 미정 값으로 게임이 도는 것은 여기가 아니라 `SetSpreadRow` 의 거름망이 막는다(음수면 통째로 거절).
	TMap<FName, FAnimalSpreadRow> EmptyRows;
	EmptyRows.Add(FName(TEXT("Spread_Empty")), FAnimalSpreadRow());
	TestEqual(TEXT("빈 행은 문제 6건"), CollectSpreadIssues(EmptyRows, DesignValues, KnownZoneIDs).Num(), 6);

	// 설계서 줄을 못 읽으면 조용히 통과하지 않고 그 자리에서 운다
	TMap<FString, double> MissingDesign = DesignValues;
	MissingDesign.Remove(SpacingLabel);
	TestEqual(TEXT("설계서 줄을 못 읽으면 문제 1건"),
		CollectSpreadIssues(SoundRows, MissingDesign, KnownZoneIDs).Num(), 1);

	// 칸 까는 함수 자체도 본다 — 금지 반경이 0 이면 격자 전부, 크면 가운데가 빈다.
	const TArray<FVector2D> AllCells =
		AAnimalSpreadActor::MakeSpreadCells(200.f, 100.f, 0.f, { FVector2D::ZeroVector });
	TestEqual(TEXT("반변 200 · 간격 100 이면 5×5 = 25칸"), AllCells.Num(), 25);
	TestEqual(TEXT("그 25칸은 서로 간격을 지킨다"), CountSpacingViolations(AllCells, 100.f), 0);

	const TArray<FVector2D> ClearedCells =
		AAnimalSpreadActor::MakeSpreadCells(200.f, 100.f, 150.f, { FVector2D::ZeroVector });
	TestEqual(TEXT("금지 반경 150 이면 가운데 9칸이 빠져 16칸"), ClearedCells.Num(), 16);
	TestEqual(TEXT("금지 반경 안에 남은 칸이 없다"),
		CountClearanceViolations(ClearedCells, FVector2D::ZeroVector, 150.f), 0);

	// 미정(음수) 값이면 한 칸도 안 깐다 (Verification.md 7 — 미정 숫자로 게임을 돌리지 않는다).
	TestEqual(TEXT("반변이 미정이면 0칸"),
		AAnimalSpreadActor::MakeSpreadCells(-1.f, 100.f, 0.f, {}).Num(), 0);
	TestEqual(TEXT("간격이 미정이면 0칸"),
		AAnimalSpreadActor::MakeSpreadCells(200.f, -1.f, 0.f, {}).Num(), 0);

	// 설계서 절 자르기·숫자 읽기 자체도 본다 (문서 모양이 바뀌면 여기서 걸린다).
	const FString FakeDoc = TEXT("### 3.0 앞 절\n| 동시 서 있는 동물 수 | 99마리 |\n\n### 3.1 뒤 절\n")
		TEXT("| 항목 | 값 |\n|---|---|\n| 동시 서 있는 동물 수 | 8마리 | 근거 |\n\n## 4. 다음\n| 동시 서 있는 동물 수 | 1마리 |\n");
	const TMap<FString, double> Parsed = ParseSpreadNumbers(ExtractSpreadSection(FakeDoc, TEXT("### 3.1")));
	TestEqual(TEXT("3.1 절 안의 값만 읽는다"), Parsed.Num(), 1);
	TestTrue(TEXT("읽은 값이 3.1 절의 것이다"),
		Parsed.Contains(CountLabel) && FMath::IsNearlyEqual(Parsed[CountLabel], 8.0, 0.01));

	return true;
}


// 사용자 지시 2026-09-23 16:55 ② — **기본 플레이에 서는 종**.
// 보는 것 셋: ① 스폰 풀에 자리표시 종(돼지·소·양)이 한 종도 없다 ② 검사 스위치를 주면 그만큼 늘어난다
// ③ 위험구역이 가리키는 종도 자리표시가 아니다 (그 다섯 마리도 기본 플레이에 서는 동물이다).
//
// **종 이름을 하나도 적지 않는다** — 자리표시인지는 행의 칸(`bPlaceholderSpecies`)이 말한다.
// 이름으로 세면 종이 바뀔 때 검사가 조용히 거짓 통과가 된다 (PROMPT ③-12).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalSpreadPoolTest,
	"Snarehaven.AnimalSpread.Pool.ExcludesPlaceholders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalSpreadPoolTest::RunTest(const FString& Parameters)
{
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable))
	{
		return false;
	}

	// 사냥터 분산 스포너의 존 — 서식 존(M5-07e)으로 걸러진 뒤의 풀을 본다. 존 이름을 박지 않고 표에서 읽는다.
	UDataTable* SpreadTable = LoadObject<UDataTable>(nullptr, SpreadTablePath);
	if (!TestNotNull(TEXT("DT_AnimalSpreadTable 로드"), SpreadTable) || SpreadTable->GetRowNames().Num() == 0)
	{
		return false;
	}
	const FName SpreadZone = SpreadTable->FindRow<FAnimalSpreadRow>(
		SpreadTable->GetRowNames()[0], TEXT("AnimalSpreadPoolTest"))->ZoneID;

	TSet<FName> PlaceholderRows;
	int32 ZoneRowCount = 0;
	for (const FName& RowName : AnimalTable->GetRowNames())
	{
		if (const FAnimalRow* Row = AnimalTable->FindRow<FAnimalRow>(RowName, TEXT("AnimalSpreadPoolTest")))
		{
			if (Row->HabitatZoneID != SpreadZone)
			{
				continue;
			}
			if (Row->bStorySpecies)
			{
				continue;   // 이야기 종은 무작위 풀에 안 든다 — 행 수에도 안 센다
			}
			++ZoneRowCount;
			if (Row->bPlaceholderSpecies)
			{
				PlaceholderRows.Add(RowName);
			}
		}
	}

	const TArray<FName> DefaultPool = AAnimalSpreadActor::MakeSpawnPool(AnimalTable, SpreadZone, false);
	const TArray<FName> TestPool = AAnimalSpreadActor::MakeSpawnPool(AnimalTable, SpreadZone, true);
	AddInfo(FString::Printf(TEXT("존 %s: 동물 %d행 · 자리표시 %d행 · 기본 플레이 스폰 풀 %d종 · 검사 스위치 %d종"),
		*SpreadZone.ToString(), ZoneRowCount, PlaceholderRows.Num(), DefaultPool.Num(), TestPool.Num()));

	// 풀이 비면 아래 for 가 한 번도 안 돌아 조용히 통과한다 (Verification.md — 빈 검사 금지).
	if (!TestTrue(TEXT("기본 플레이 스폰 풀에 종이 하나 이상 있다"), DefaultPool.Num() > 0))
	{
		return false;
	}

	for (const FName& RowName : DefaultPool)
	{
		TestFalse(*FString::Printf(TEXT("기본 풀의 %s 는 자리표시가 아니다"), *RowName.ToString()),
			PlaceholderRows.Contains(RowName));
	}

	TestEqual(TEXT("기본 풀 = 이 존의 행 - 자리표시 행"), DefaultPool.Num(), ZoneRowCount - PlaceholderRows.Num());
	TestEqual(TEXT("검사 스위치를 주면 이 존의 모든 행이 선다"), TestPool.Num(), ZoneRowCount);

	// 자리표시 행이 아직 남아 있는 동안에는 두 풀이 실제로 달라야 한다 — 같으면 거름망이 죽은 것이다.
	// (M5-07 끝에 행이 빠지면 자리표시 0행이 되고, 그때는 두 풀이 같은 게 맞다.)
	if (PlaceholderRows.Num() > 0)
	{
		TestTrue(TEXT("자리표시 행이 있는 동안 두 풀은 다르다"), TestPool.Num() > DefaultPool.Num());
	}

	// 이름순 고정 — 같은 자리에 같은 종이 서는 근거다.
	for (int32 Index = 1; Index < DefaultPool.Num(); ++Index)
	{
		TestTrue(TEXT("스폰 풀은 이름순이다"), DefaultPool[Index - 1].LexicalLess(DefaultPool[Index]));
	}

	// 검사 스위치가 없는 자동화 프로세스에서는 자리표시를 세우지 않는다고 답해야 한다.
	TestFalse(TEXT("-SnareStageAnimals 없이 돌면 자리표시를 안 세운다"),
		AAnimalCharacter::ShouldStandPlaceholderSpecies());

	TestFalse(TEXT("-SnareFreezeAnimals 없이 돌면 동물이 움직인다"), AAnimalCharacter::ShouldFreezeAnimals());
	TestFalse(TEXT("-SnareHuntHerd 없이 돌면 동물을 안 옮긴다"), AAnimalCharacter::ShouldHuntHerd());

	// 모으는 자리 — 0번은 사거리 안쪽(때릴 수 있다), 나머지는 앞쪽 350~450cm 에서 서로 다른 자리.
	const FVector HerdOrigin(1000.f, 2000.f, 90.f);
	const float HerdReachCm = 300.f;
	const FVector HerdNear = AAnimalCharacter::HuntHerdSpot(HerdOrigin, FVector::ForwardVector, 0, HerdReachCm);
	TestTrue(TEXT("모으기 0번은 사거리 안쪽"), FVector::Dist2D(HerdOrigin, HerdNear) < HerdReachCm);
	TestEqual(TEXT("모으기 높이는 폰 그대로"), HerdNear.Z, HerdOrigin.Z);
	TArray<FVector> HerdSpots;
	HerdSpots.Add(HerdNear);
	for (int32 Index = 1; Index <= 3; ++Index)
	{
		const FVector Spot = AAnimalCharacter::HuntHerdSpot(HerdOrigin, FVector::ForwardVector, Index, HerdReachCm);
		const float DistanceCm = FVector::Dist2D(HerdOrigin, Spot);
		TestTrue(*FString::Printf(TEXT("모으기 %d번은 350~450cm"), Index), DistanceCm >= 350.f && DistanceCm <= 450.f);
		TestTrue(*FString::Printf(TEXT("모으기 %d번은 앞쪽"), Index), Spot.X > HerdOrigin.X);
		for (const FVector& Other : HerdSpots)
		{
			TestTrue(TEXT("모으기 자리는 서로 200cm 넘게 떨어진다"), FVector::Dist2D(Other, Spot) > 200.f);
		}
		HerdSpots.Add(Spot);
	}

	// 위험구역이 세우는 종도 기본 플레이의 동물이어야 한다 (자리표시면 구역이 텅 빈다).
	UDataTable* DangerTable = LoadObject<UDataTable>(nullptr, DangerTablePath);
	if (!TestNotNull(TEXT("DT_DangerZoneTable 로드"), DangerTable))
	{
		return false;
	}
	int32 CheckedZones = 0;
	for (const FName& RowName : DangerTable->GetRowNames())
	{
		const FDangerZoneRow* Row = DangerTable->FindRow<FDangerZoneRow>(RowName, TEXT("AnimalSpreadPoolTest"));
		if (Row == nullptr)
		{
			continue;
		}

		++CheckedZones;
		TestFalse(*FString::Printf(TEXT("[%s] 위험구역 종 %s 는 자리표시가 아니다"),
			*RowName.ToString(), *Row->AnimalRowName.ToString()),
			AAnimalCharacter::IsPlaceholderSpeciesRow(AnimalTable, Row->AnimalRowName));
	}
	TestTrue(TEXT("위험구역 행을 하나 이상 봤다"), CheckedZones > 0);

	return true;
}


// M5-07e — **종은 자기 서식 존에만 선다.**
// ① 임시 표(존 둘)로 `MakeSpawnPool` 이 존마다 제 종만 돌려주는지 — 실제 표는 지금 존이 하나(`zone.hunt`)뿐이라
//    거름망이 죽어도 통과한다. 그래서 가짜 존 둘을 세운 표로 따로 본다(역검증 겸).
// ② 실제 `DT_AnimalTable` 의 모든 행이 `DT_ZoneTable` 에 **있는** 존을 가리키는지 — 지어낸 존 금지.
// ③ 분산 스포너가 선 존들의 풀을 합치면 기본 플레이 종(자리표시 아님)이 한 종도 빠지지 않는지 — 어느 존에도 못 서는 종 금지.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalSpreadHabitatTest,
	"Snarehaven.AnimalSpread.Pool.HabitatZonesExist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalSpreadHabitatTest::RunTest(const FString& Parameters)
{
	// ① 임시 표 — 이름은 검사 안에서만 쓰는 가짜다(실제 종·존이 아니다).
	UDataTable* FakeTable = NewObject<UDataTable>(GetTransientPackage());
	FakeTable->RowStruct = FAnimalRow::StaticStruct();
	const FName ZoneA(TEXT("zone.test_a"));
	const FName ZoneB(TEXT("zone.test_b"));
	auto AddFake = [FakeTable](const TCHAR* Name, FName Zone, bool bPlaceholder)
	{
		FAnimalRow Row;
		Row.AnimalID = FName(Name);
		Row.HabitatZoneID = Zone;
		Row.bPlaceholderSpecies = bPlaceholder;
		FakeTable->AddRow(FName(Name), Row);
	};
	AddFake(TEXT("Test_A1"), ZoneA, false);
	AddFake(TEXT("Test_A2"), ZoneA, false);
	AddFake(TEXT("Test_B1"), ZoneB, false);
	AddFake(TEXT("Test_B2"), ZoneB, true);
	{
		// 이야기 종은 스위치를 줘도 무작위 풀에 안 든다 (안개뿔 2a).
		FAnimalRow StoryRow;
		StoryRow.AnimalID = FName(TEXT("Test_Story"));
		StoryRow.HabitatZoneID = ZoneB;
		StoryRow.bStorySpecies = true;
		FakeTable->AddRow(FName(TEXT("Test_Story")), StoryRow);
	}

	const TArray<FName> PoolA = AAnimalSpreadActor::MakeSpawnPool(FakeTable, ZoneA, false);
	const TArray<FName> PoolB = AAnimalSpreadActor::MakeSpawnPool(FakeTable, ZoneB, false);
	TestEqual(TEXT("존 A 풀은 A 종 둘"), PoolA, TArray<FName>{FName(TEXT("Test_A1")), FName(TEXT("Test_A2"))});
	TestEqual(TEXT("존 B 풀은 자리표시를 뺀 B 종 하나"), PoolB, TArray<FName>{FName(TEXT("Test_B1"))});
	TestEqual(TEXT("존 B + 검사 스위치면 B 종 둘"), AAnimalSpreadActor::MakeSpawnPool(FakeTable, ZoneB, true).Num(), 2);
	TestFalse(TEXT("이야기 종은 풀에 없다"), AAnimalSpreadActor::MakeSpawnPool(FakeTable, ZoneB, true).Contains(FName(TEXT("Test_Story"))));
	TestEqual(TEXT("존 B 의 이야기 종을 칸으로 찾는다"), AAnimalSpreadActor::FindStorySpecies(FakeTable, ZoneB), FName(TEXT("Test_Story")));
	TestTrue(TEXT("이야기 종이 없는 존은 없음"), AAnimalSpreadActor::FindStorySpecies(FakeTable, ZoneA).IsNone());
	TestEqual(TEXT("아무도 안 사는 존은 빈 풀"), AAnimalSpreadActor::MakeSpawnPool(FakeTable, FName(TEXT("zone.test_none")), true).Num(), 0);

	// ② 실제 표의 서식 존이 존 표에 있는가.
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	UDataTable* ZoneTable = LoadObject<UDataTable>(nullptr, SpreadZoneTablePath);
	UDataTable* SpreadTable = LoadObject<UDataTable>(nullptr, SpreadTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable) || !TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable)
		|| !TestNotNull(TEXT("DT_AnimalSpreadTable 로드"), SpreadTable))
	{
		return false;
	}

	TSet<FName> KnownZones;
	for (const FName& RowName : ZoneTable->GetRowNames())
	{
		KnownZones.Add(ZoneTable->FindRow<FZoneRow>(RowName, TEXT("AnimalSpreadHabitatTest"))->ZoneID);
	}

	TSet<FName> PlayableRows;
	for (const FName& RowName : AnimalTable->GetRowNames())
	{
		const FAnimalRow* Row = AnimalTable->FindRow<FAnimalRow>(RowName, TEXT("AnimalSpreadHabitatTest"));
		TestTrue(*FString::Printf(TEXT("%s 의 서식 존 '%s' 가 DT_ZoneTable 에 있다"),
			*RowName.ToString(), *Row->HabitatZoneID.ToString()), KnownZones.Contains(Row->HabitatZoneID));
		if (!Row->bPlaceholderSpecies && !Row->bStorySpecies)
		{
			PlayableRows.Add(RowName);
		}
	}

	// ③ 스포너 존들의 풀 합 = 기본 플레이 종 전부. 한 종이 두 존 풀에 동시에 들면 안 된다(존은 종마다 하나).
	TSet<FName> Stood;
	for (const FName& RowName : SpreadTable->GetRowNames())
	{
		const FName Zone = SpreadTable->FindRow<FAnimalSpreadRow>(RowName, TEXT("AnimalSpreadHabitatTest"))->ZoneID;
		for (const FName& Species : AAnimalSpreadActor::MakeSpawnPool(AnimalTable, Zone, false))
		{
			const FAnimalRow* Row = AnimalTable->FindRow<FAnimalRow>(Species, TEXT("AnimalSpreadHabitatTest"));
			TestEqual(*FString::Printf(TEXT("%s 는 제 서식 존(%s)에만 선다"), *Species.ToString(), *Zone.ToString()),
				Row->HabitatZoneID, Zone);
			Stood.Add(Species);
		}
	}
	AddInfo(FString::Printf(TEXT("존 표 %d개 · 기본 플레이 종 %d · 스포너 존에서 서는 종 %d"),
		KnownZones.Num(), PlayableRows.Num(), Stood.Num()));
	TestTrue(TEXT("기본 플레이 종이 하나 이상"), PlayableRows.Num() > 0);
	TestTrue(TEXT("모든 기본 플레이 종이 어느 스포너 존엔가 선다"),
		Stood.Num() == PlayableRows.Num() && Stood.Includes(PlayableRows));

	return true;
}

// M21-01 — **시작점 15m 안 순한 동물 2마리.**
// ① 표의 시작점 동물 수·거리가 DESIGN 3.1 과 같고, 그 수를 가진 행은 **하나**(시작점이 있는 존)뿐이다
// ② 거리 < 시작점 금지 반경(1500 = 15m) — 흩는 동물이 못 서는 자리 안쪽이어야 "시작점 코앞" 이다
// ③ 그 존의 시작점 풀이 비지 않고 전부 도주만 하는 종(1타 0)이다
// ④ 자리 함수가 거리·간격을 지키고, 막힌 방향(바다)을 돌아 뭍을 고른다 — 검사 전용 값으로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalSpreadStarterTest,
	"Snarehaven.AnimalSpread.Starter.NearStartDocile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalSpreadStarterTest::RunTest(const FString& Parameters)
{
	const TCHAR* StarterCountLabel = TEXT("시작점 순한 동물 수");
	const TCHAR* StarterRingLabel = TEXT("시작점 순한 동물 거리");

	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"),
		FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	const TMap<FString, double> DesignValues = ParseSpreadNumbers(ExtractSpreadSection(Doc, TEXT("### 3.1")));
	if (!TestTrue(TEXT("DESIGN 3.1 에서 시작점 동물 수·거리·금지 반경 줄을 읽었다"),
		DesignValues.Contains(StarterCountLabel) && DesignValues.Contains(StarterRingLabel)
		&& DesignValues.Contains(ClearLabel)))
	{
		return false;
	}
	const int32 DesignCount = FMath::RoundToInt(DesignValues[StarterCountLabel]);
	const double DesignRing = DesignValues[StarterRingLabel];
	const double DesignClear = DesignValues[ClearLabel];
	TestTrue(TEXT("설계서: 시작점 동물 거리 < 시작점 금지 반경 (15m 안)"), DesignRing < DesignClear);

	UDataTable* SpreadTable = LoadObject<UDataTable>(nullptr, SpreadTablePath);
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalSpreadTable 로드"), SpreadTable) || !TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable))
	{
		return false;
	}

	const FAnimalSpreadRow* StarterRow = nullptr;
	int32 StarterRows = 0;
	for (const FName& RowName : SpreadTable->GetRowNames())
	{
		const FAnimalSpreadRow* Row = SpreadTable->FindRow<FAnimalSpreadRow>(RowName, TEXT("AnimalSpreadStarterTest"));
		if (Row == nullptr)
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("[%s] 시작점 동물 거리 = DESIGN 3.1 (문서 %.0f / 표 %.0f)"),
			*RowName.ToString(), DesignRing, Row->StarterRingCm),
			FMath::IsNearlyEqual(static_cast<double>(Row->StarterRingCm), DesignRing, 0.01));
		TestTrue(*FString::Printf(TEXT("[%s] 시작점 동물 수는 0 또는 DESIGN 3.1 값 (표 %d)"),
			*RowName.ToString(), Row->StarterAnimalCount),
			Row->StarterAnimalCount == 0 || Row->StarterAnimalCount == DesignCount);
		if (Row->StarterAnimalCount > 0)
		{
			StarterRow = Row;
			++StarterRows;
		}
	}
	if (!TestEqual(TEXT("시작점 동물을 세우는 행은 하나 (시작점이 있는 존)"), StarterRows, 1) || StarterRow == nullptr)
	{
		return false;
	}

	const TArray<FName> Pool = AAnimalSpreadActor::MakeStarterPool(AnimalTable, StarterRow->ZoneID);
	FString PoolNames;
	for (const FName& RowName : Pool)
	{
		PoolNames += RowName.ToString() + TEXT(" ");
		const FAnimalRow* Row = AnimalTable->FindRow<FAnimalRow>(RowName, TEXT("AnimalSpreadStarterTest"));
		if (TestNotNull(*FString::Printf(TEXT("%s 행"), *RowName.ToString()), Row))
		{
			TestTrue(*FString::Printf(TEXT("%s 는 도주만 한다"), *RowName.ToString()),
				Row->BehaviorKind == EAnimalBehaviorKind::Flee);
			TestTrue(*FString::Printf(TEXT("%s 는 사람을 안 친다 (1타 %.0f)"), *RowName.ToString(), Row->AttackDamage),
				Row->AttackDamage == 0.f);
			TestFalse(*FString::Printf(TEXT("%s 는 자리표시가 아니다"), *RowName.ToString()), Row->bPlaceholderSpecies);
		}
	}
	AddInfo(FString::Printf(TEXT("[Starter] 존 %s · %d마리 · 거리 %.0fcm · 종 %s"),
		*StarterRow->ZoneID.ToString(), StarterRow->StarterAnimalCount, StarterRow->StarterRingCm, *PoolNames));
	if (!TestTrue(TEXT("시작점 풀이 비지 않았다"), Pool.Num() > 0))
	{
		return false;
	}

	// ④ 자리 함수 — 표 값(거리·간격)과 검사 전용 시작점·방향으로.
	const FVector2D Start(1000.f, -2000.f);
	const float Ring = StarterRow->StarterRingCm;
	const float Spacing = StarterRow->MinSpacingCm;
	auto CheckSpots = [&](const TCHAR* Case, const TArray<FVector2D>& Spots, int32 Expected)
	{
		TestEqual(*FString::Printf(TEXT("%s: 자리 %d곳"), Case, Expected), Spots.Num(), Expected);
		for (const FVector2D& Spot : Spots)
		{
			TestTrue(*FString::Printf(TEXT("%s: 시작점에서 %.0fcm (거리 %.0f)"), Case, FVector2D::Distance(Spot, Start), Ring),
				FMath::IsNearlyEqual(FVector2D::Distance(Spot, Start), Ring, 1.f));
		}
		TestEqual(*FString::Printf(TEXT("%s: 둘 사이 최소 간격을 어긴 짝 없음"), Case), CountSpacingViolations(Spots, Spacing), 0);
	};

	const TArray<FVector2D> Open = AAnimalSpreadActor::MakeStarterSpots(Start, 0.f, StarterRow->StarterAnimalCount,
		Ring, Spacing, [](const FVector2D&) { return true; });
	CheckSpots(TEXT("사방이 뭍"), Open, StarterRow->StarterAnimalCount);
	TestTrue(TEXT("사방이 뭍이면 첫 자리는 바라보는 쪽(+X)"), Open.Num() > 0 && Open[0].X > Start.X + Ring - 1.f);

	// 앞(+X) 반쪽이 바다 — 뒤쪽 반원에서 고른다.
	const TArray<FVector2D> Coast = AAnimalSpreadActor::MakeStarterSpots(Start, 0.f, StarterRow->StarterAnimalCount,
		Ring, Spacing, [&Start](const FVector2D& Spot) { return Spot.X < Start.X - 1.f; });
	CheckSpots(TEXT("앞이 바다"), Coast, StarterRow->StarterAnimalCount);
	for (const FVector2D& Spot : Coast)
	{
		TestTrue(TEXT("앞이 바다면 뭍 쪽에만 선다"), Spot.X < Start.X);
	}

	TestEqual(TEXT("사방이 바다면 0곳 (조용히 바다에 세우지 않는다)"),
		AAnimalSpreadActor::MakeStarterSpots(Start, 0.f, 2, Ring, Spacing, [](const FVector2D&) { return false; }).Num(), 0);
	TestEqual(TEXT("미정 거리(-1)면 0곳"),
		AAnimalSpreadActor::MakeStarterSpots(Start, 0.f, 2, -1.f, Spacing, [](const FVector2D&) { return true; }).Num(), 0);

	return true;
}

// INBOX 결함(감독 2026-10-07 14:40) 2층 — **사냥터 공작이 오두막 지붕 위를 걷는다.** 시작점 순한 동물 자리를
// 위에서 내려 그은 첫 바닥(지붕)에 찍어 공작이 Z=395 지붕에 섰다. 보는 것: ① 지붕·울타리 윗면 칸은 땅이 아니라
// 버리고 빈 땅 칸만 고른다 ② 이미 지붕에 올라 쉬는 동물은 땅으로 내린다 ③ 땅에 선 동물은 안 옮긴다.
// 숫자는 검사 전용(큐브 배율) — 배회 반경이 바뀌어도 「지붕 < 배회 원 < 땅」 이면 그대로 맞다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalSpreadGroundOnlyTest, "Snarehaven.AnimalSpread.GroundOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalSpreadGroundOnlyTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	UStaticMesh* const Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("큐브"), Cube) || !TestNotNull(TEXT("동물 표"), AnimalTable))
	{
		return false;
	}
	auto SpawnBox = [World, Cube](const FVector& Center, const FVector& Scale)
	{
		AStaticMeshActor* Box = World->SpawnActor<AStaticMeshActor>(Center, FRotator::ZeroRotator);
		Box->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Box->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Box->GetStaticMeshComponent()->SetWorldScale3D(Scale);
		return Box;
	};

	// 큐브 = 100cm. 땅 200m(윗면 Z 0) · 오두막 지붕 8m(윗면 Z 350) · 울타리 4m × 20cm(윗면 Z 120).
	const float WanderHalf = AAnimalCharacter::GetWanderRadiusCm();
	AStaticMeshActor* const Ground = SpawnBox(FVector(0.f, 0.f, -50.f), FVector(200.f, 200.f, 1.f));
	AStaticMeshActor* const Roof = SpawnBox(FVector(0.f, 0.f, 300.f), FVector(8.f, 8.f, 1.f));
	SpawnBox(FVector(3000.f, 0.f, 60.f), FVector(4.f, 0.2f, 1.2f));
	TestTrue(FString::Printf(TEXT("검사 지붕 반폭 400 < 배회 반경 %.0f < 땅 반폭 10000"), WanderHalf),
		400.f < WanderHalf && WanderHalf < 10000.f);

	TestTrue(TEXT("땅 큐브는 동물이 설 땅"), AAnimalSpreadActor::IsAnimalGroundFloor(Ground->GetStaticMeshComponent()));
	TestFalse(TEXT("지붕 큐브는 땅이 아니다"), AAnimalSpreadActor::IsAnimalGroundFloor(Roof->GetStaticMeshComponent()));
	TestFalse(TEXT("바닥 없음은 땅이 아니다"), AAnimalSpreadActor::IsAnimalGroundFloor(nullptr));

	// ① 칸 고르기 — 지붕·울타리 위 칸은 버리고(그 아래 집 안·울타리 속도 아님), 빈 땅 칸은 Z 0 에 찍는다.
	const FVector Up(0.f, 0.f, 1000.f);
	FVector Impact;
	TestFalse(TEXT("오두막 지붕 위 칸은 버린다"),
		AAnimalSpreadActor::FindAnimalGround(World, FVector(0.f, 0.f, 0.f) + Up, FVector(0.f, 0.f, 0.f) - Up, nullptr, Impact));
	TestFalse(TEXT("울타리 위 칸은 버린다"),
		AAnimalSpreadActor::FindAnimalGround(World, FVector(3000.f, 0.f, 0.f) + Up, FVector(3000.f, 0.f, 0.f) - Up, nullptr, Impact));
	const bool bOpenGround = AAnimalSpreadActor::FindAnimalGround(World, FVector(5000.f, 0.f, 0.f) + Up,
		FVector(5000.f, 0.f, 0.f) - Up, nullptr, Impact);
	TestTrue(TEXT("빈 땅 칸은 고른다"), bOpenGround);
	TestTrue(FString::Printf(TEXT("빈 땅 칸은 땅 높이에 찍힌다 (Z %.1f)"), Impact.Z), bOpenGround && FMath::Abs(Impact.Z) < 1.f);

	// ② 이미 지붕에 올라간 동물 — 내려앉힌 뒤 쉬는 채로 지붕을 딛고 있으면 땅으로 옮긴다.
	const float HalfHeight = GetDefault<AAnimalCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	AAnimalCharacter* const Animal = World->SpawnActor<AAnimalCharacter>(FVector(0.f, 0.f, 350.f + HalfHeight + 5.f),
		FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("동물"), Animal))
	{
		return false;
	}
	Animal->SetAnimalData(AnimalTable, FName(TEXT("Animal_Peacock")));
	TestTrue(TEXT("공작 행으로 초기화"), Animal->InitializeFromTable());
	UCharacterMovementComponent* const Move = Animal->GetCharacterMovement();
	auto Settle = [Move]()
	{
		for (int32 Frame = 0; Frame < 30; ++Frame)
		{
			Move->TickComponent(1.f / 60.f, LEVELTICK_All, nullptr);
		}
	};
	Settle();
	TestTrue(FString::Printf(TEXT("공작이 지붕 위에 섰다 (Z %.1f)"), Animal->GetActorLocation().Z),
		Animal->GetActorLocation().Z > 300.f && Move->IsMovingOnGround());
	TestTrue(TEXT("지붕 위 쉬는 공작은 땅 아닌 바닥에 갇힌 것"), AAnimalSpreadActor::IsStrandedOffGround(Animal));

	const FVector GroundSpot = Impact + FVector(0.f, 0.f, HalfHeight);
	TestTrue(TEXT("지붕 위 공작을 땅으로 내렸다"), AAnimalSpreadActor::ReturnToGroundIfStranded(Animal, GroundSpot));
	Settle();
	TestTrue(FString::Printf(TEXT("내린 뒤 땅 높이 (Z %.1f)"), Animal->GetActorLocation().Z),
		Animal->GetActorLocation().Z < 100.f && Move->IsMovingOnGround());
	TestFalse(TEXT("내린 뒤엔 갇힌 것 아님"), AAnimalSpreadActor::IsStrandedOffGround(Animal));

	// ③ 땅에 선 동물은 안 옮긴다 — 다른 자리를 줘도 그대로.
	const FVector Before = Animal->GetActorLocation();
	TestFalse(TEXT("땅 위 공작은 안 옮긴다"),
		AAnimalSpreadActor::ReturnToGroundIfStranded(Animal, GroundSpot + FVector(2000.f, 0.f, 0.f)));
	TestTrue(TEXT("땅 위 공작 자리 그대로"), Animal->GetActorLocation().Equals(Before, 1.f));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
