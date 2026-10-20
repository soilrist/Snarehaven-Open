// ROADMAP M12-00·01 2층 검사 — `L_Hunt` 안의 존 칸(평원·설원·숲·늪)과 그 특성.
// 보는 것 넷: ① `DT_ZoneTable` 의 칸 행이 DESIGN 1.3 "맵 안 존" 표와 같은가 ② 분산 스포너가 존 칸에 맞춰
// 깐 자리가 **전부 제 존 칸 안**인가(종은 제 존 스포너에만 서므로 = 종은 제 존 안에만 선다) ③ 늪 칸에서
// 건축이 막히는가 ④ 설원 칸의 체력 감소가 표 값만큼인가.
//
// **이 검사는 숫자를 하나도 들고 있지 않다** — 설계 값은 문서에서, 칸은 표에서 읽는다
// (`Snarehaven.AnimalSpread.Table.MatchesDesign` 과 같은 짜임).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Zone.Region; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/AnimalRow.h"
#include "Data/AnimalSpreadRow.h"
#include "Data/PlayerVitalsRow.h"
#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/PlayerVitalsComponent.h"
#include "World/AnimalSpreadActor.h"
#include "World/SeaActor.h"
#include "GameFramework/PlayerStart.h"
#include "World/SnareZoneRules.h"
#include "World/ZoneTravelComponent.h"

namespace
{
	const TCHAR* RegionSpreadTablePath = TEXT("/Game/Game/Data/World/DT_AnimalSpreadTable.DT_AnimalSpreadTable");
	const TCHAR* RegionAnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* RegionVitalsTablePath = TEXT("/Game/Game/Data/Balance/DT_PlayerVitalsTable.DT_PlayerVitalsTable");
	const FName RegionLevel(TEXT("L_Hunt"));

	/** DESIGN 1.3 "맵 안 존" 표 한 줄에서 읽은 것. */
	struct FDesignRegion
	{
		FVector2D Center = FVector2D::ZeroVector;
		float HalfExtent = 0.f;
		float HealthDrain = 0.f;
		bool bBuildBlocked = false;
		bool bEntryFee = false;
	};

	FString ExtractRegionSection(const FString& Doc)
	{
		const FString Heading = TEXT("### 1.3");
		const int32 Start = Doc.Find(Heading, ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return FString();
		}
		const int32 BodyStart = Start + Heading.Len();
		const int32 Next = Doc.Find(TEXT("\n#"), ESearchCase::CaseSensitive, ESearchDir::FromStart, BodyStart);
		return Next == INDEX_NONE ? Doc.Mid(BodyStart) : Doc.Mid(BodyStart, Next - BodyStart);
	}

	/** `| 설원 (`zone.snow`) | 중심 (5000, 0) cm · 반변 2500cm · 체력 감소 2 / 초 | 근거 |` 꼴의 줄을 존 ID 별로. */
	TMap<FName, FDesignRegion> ParseDesignRegions(const FString& Section)
	{
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);
		const FRegexPattern Pattern(TEXT("^\\|[^|]*\\(`(zone\\.[a-z_]+)`\\)\\s*\\|\\s*중심 \\((-?[0-9.]+), (-?[0-9.]+)\\) cm · 반변 ([0-9.]+)cm · ([^|]+)\\|"));
		const FRegexPattern DrainPattern(TEXT("체력 감소 ([0-9.]+)"));
		TMap<FName, FDesignRegion> Out;
		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(Pattern, Line);
			if (!Matcher.FindNext())
			{
				continue;
			}
			FDesignRegion Region;
			Region.Center = FVector2D(FCString::Atod(*Matcher.GetCaptureGroup(2)), FCString::Atod(*Matcher.GetCaptureGroup(3)));
			Region.HalfExtent = FCString::Atof(*Matcher.GetCaptureGroup(4));
			const FString Traits = Matcher.GetCaptureGroup(5);
			FRegexMatcher Drain(DrainPattern, Traits);
			Region.HealthDrain = Drain.FindNext() ? FCString::Atof(*Drain.GetCaptureGroup(1)) : 0.f;
			Region.bBuildBlocked = Traits.Contains(TEXT("건축 불가"));
			Region.bEntryFee = Traits.Contains(TEXT("입장료"));
			Out.Add(FName(*Matcher.GetCaptureGroup(1)), Region);
		}
		return Out;
	}

	/** 표의 칸 행과 문서 줄을 맞댄다. 자기검사가 에셋 없이 다시 부를 수 있게 문제를 모아 돌려준다. */
	TArray<FString> CollectRegionIssues(const TArray<FZoneRow>& TableRows, const TMap<FName, FDesignRegion>& Design)
	{
		TArray<FString> Issues;
		if (Design.Num() == 0)
		{
			Issues.Add(TEXT("DESIGN 1.3 에서 존 칸 줄을 하나도 못 읽었다"));
			return Issues;
		}
		TSet<FName> Seen;
		for (const FZoneRow& Row : TableRows)
		{
			if (!SnareZones::IsRegionRow(Row))
			{
				continue;
			}
			Seen.Add(Row.ZoneID);
			const FDesignRegion* const Want = Design.Find(Row.ZoneID);
			if (Want == nullptr)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 표엔 칸이 있는데 DESIGN 1.3 에 없다 (지어낸 존)"), *Row.ZoneID.ToString()));
				continue;
			}
			if (!Row.RegionCenterCm.Equals(Want->Center, 0.01) || !FMath::IsNearlyEqual(Row.RegionHalfExtentCm, Want->HalfExtent, 0.01f))
			{
				Issues.Add(FString::Printf(TEXT("[%s] 칸 자리가 다르다 (문서 %s ±%.0f / 표 %s ±%.0f)"), *Row.ZoneID.ToString(),
					*Want->Center.ToString(), Want->HalfExtent, *Row.RegionCenterCm.ToString(), Row.RegionHalfExtentCm));
			}
			if (!FMath::IsNearlyEqual(Row.HealthDrainPerSecond, Want->HealthDrain, 0.01f))
			{
				Issues.Add(FString::Printf(TEXT("[%s] 체력 감소가 다르다 (문서 %.2f / 표 %.2f)"), *Row.ZoneID.ToString(),
					Want->HealthDrain, Row.HealthDrainPerSecond));
			}
			if (Row.bBuildBlocked != Want->bBuildBlocked || Row.bChargesEntryFee != Want->bEntryFee)
			{
				Issues.Add(FString::Printf(TEXT("[%s] 건축 불가·입장료 칸이 문서와 다르다"), *Row.ZoneID.ToString()));
			}
		}
		for (const TPair<FName, FDesignRegion>& Pair : Design)
		{
			if (!Seen.Contains(Pair.Key))
			{
				Issues.Add(FString::Printf(TEXT("[%s] DESIGN 1.3 에 있는데 표에 칸 행이 없다"), *Pair.Key.ToString()));
			}
		}
		return Issues;
	}

	TArray<FZoneRow> ReadAllZoneRows(const UDataTable* Table)
	{
		TArray<FZoneRow> Rows;
		Table->ForeachRow<FZoneRow>(TEXT("ZoneRegionTest"), [&Rows](const FName&, const FZoneRow& Row) { Rows.Add(Row); });
		return Rows;
	}

	/** `L_Hunt` 의 바다(새 지도 거리장·존 칸을 든다) — 레벨을 띄우지 않고 액터 목록에서 찾는다(`Snarehaven.Mine.Level` 과 같은 길). */
	const ASeaActor* LoadHuntSea(const UWorld*& OutLevel)
	{
		OutLevel = LoadObject<UWorld>(nullptr, TEXT("/Game/Game/Maps/L_Hunt.L_Hunt"));
		if (OutLevel == nullptr || OutLevel->PersistentLevel == nullptr)
		{
			return nullptr;
		}
		for (const AActor* const Actor : OutLevel->PersistentLevel->Actors)
		{
			if (const ASeaActor* const Sea = Cast<ASeaActor>(Actor); Sea != nullptr && Sea->HasField())
			{
				return Sea;
			}
		}
		return nullptr;
	}

	/** 새 지도 존 행이면 `FieldCode` 칸 + 뭍, 아니면 정사각형 — `SnareZones::IsInsideRegion(WorldContext, …)` 과 같은 식. */
	bool IsInsideOnMap(const ASeaActor* Sea, const FZoneRow& Row, const FVector2D& Point)
	{
		if (Sea == nullptr || Row.FieldCode <= 0)
		{
			return SnareZones::IsInsideRegion(Row, FVector(Point, 0.0));
		}
		return (Sea->ZoneCodeAt(FVector(Point, 0.0)) & 0x0F) == Row.FieldCode && Sea->CoastDistance(FVector(Point, 0.0)) < 0.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneRegionDesignTest,
	"Snarehaven.Zone.Region.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneRegionDesignTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	const TMap<FName, FDesignRegion> Design = ParseDesignRegions(ExtractRegionSection(Doc));
	AddInfo(FString::Printf(TEXT("DESIGN 1.3 존 칸 %d줄"), Design.Num()));

	const UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}
	for (const FString& Issue : CollectRegionIssues(ReadAllZoneRows(ZoneTable), Design))
	{
		AddError(Issue);
	}

	// 칸은 전부 `L_Hunt` 안이다 — 다른 맵의 칸이면 사냥존 스포너·효과가 못 본다.
	for (const FZoneRow& Row : ReadAllZoneRows(ZoneTable))
	{
		if (SnareZones::IsRegionRow(Row))
		{
			TestEqual(*FString::Printf(TEXT("[%s] 칸은 L_Hunt 안이다"), *Row.ZoneID.ToString()), Row.LevelName, RegionLevel);
		}
	}
	return true;
}

// 위 검사가 "표가 멀쩡해서" 통과할 때도 살아 있는지 (역검증). 값은 전부 검사 전용이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneRegionSelfTest,
	"Snarehaven.Zone.Region.CheckCatchesBrokenRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneRegionSelfTest::RunTest(const FString& Parameters)
{
	const FString FakeDoc = TEXT("### 1.3 가짜\n| 항목 | 값 | 근거 |\n|---|---|---|\n")
		TEXT("| 가 (`zone.test_a`) | 중심 (10, -20) cm · 반변 30cm · 체력 감소 4 / 초 | 근거 |\n")
		TEXT("| 나 (`zone.test_b`) | 중심 (0, 0) cm · 반변 5cm · 건축 불가 · 입장료(섬) | |\n\n### 1.4 다음\n")
		TEXT("| 다 (`zone.test_c`) | 중심 (0, 0) cm · 반변 5cm · 특성 없음 | |\n");
	const TMap<FName, FDesignRegion> Design = ParseDesignRegions(ExtractRegionSection(FakeDoc));
	TestEqual(TEXT("1.3 절 안의 두 줄만 읽는다"), Design.Num(), 2);

	FZoneRow A;
	A.ZoneID = TEXT("zone.test_a");
	A.RegionCenterCm = FVector2D(10.0, -20.0);
	A.RegionHalfExtentCm = 30.f;
	A.HealthDrainPerSecond = 4.f;
	FZoneRow B;
	B.ZoneID = TEXT("zone.test_b");
	B.RegionHalfExtentCm = 5.f;
	B.bBuildBlocked = true;
	B.bChargesEntryFee = true;
	FZoneRow Whole;
	Whole.ZoneID = TEXT("zone.test_whole");

	TestEqual(TEXT("맞는 행은 문제 0건 (칸 없는 행은 안 본다)"), CollectRegionIssues({ A, B, Whole }, Design).Num(), 0);

	auto IssuesWith = [&](TFunctionRef<void(FZoneRow&)> Break)
	{
		FZoneRow Broken = A;
		Break(Broken);
		return CollectRegionIssues({ Broken, B }, Design).Num();
	};
	TestEqual(TEXT("중심이 틀리면 잡는다"), IssuesWith([](FZoneRow& R) { R.RegionCenterCm.X += 1.0; }), 1);
	TestEqual(TEXT("체력 감소가 틀리면 잡는다"), IssuesWith([](FZoneRow& R) { R.HealthDrainPerSecond = 0.f; }), 1);
	TestEqual(TEXT("건축 불가가 틀리면 잡는다"), IssuesWith([](FZoneRow& R) { R.bBuildBlocked = true; }), 1);
	TestEqual(TEXT("지어낸 존은 잡는다 (+ 빠진 존)"), IssuesWith([](FZoneRow& R) { R.ZoneID = TEXT("zone.test_nowhere"); }), 2);
	TestEqual(TEXT("문서를 못 읽으면 통과시키지 않는다"), CollectRegionIssues({ A }, {}).Num(), 1);

	// 칸 판정: 경계는 안, 한 발 밖은 밖, 칸 없는 행은 늘 밖.
	TestTrue(TEXT("경계 위는 안"), SnareZones::IsInsideRegion(A, FVector(40.0, -50.0, 0.0)));
	TestFalse(TEXT("경계 한 발 밖은 밖"), SnareZones::IsInsideRegion(A, FVector(40.1, -20.0, 0.0)));
	TestFalse(TEXT("칸 없는 행은 늘 밖"), SnareZones::IsInsideRegion(Whole, FVector::ZeroVector));
	FAnimalSpreadRow Spread;
	Spread.AreaHalfExtentCm = 100.f;
	FVector2D Center;
	TestFalse(TEXT("칸 없는 존엔 스포너를 안 옮긴다"), AAnimalSpreadActor::FitToZone(Whole, Spread, Center));
	TestTrue(TEXT("칸 있는 존엔 맞춘다"), AAnimalSpreadActor::FitToZone(A, Spread, Center));
	TestEqual(TEXT("반변은 칸 반변 이하로 준다"), Spread.AreaHalfExtentCm, 30.f);
	TestEqual(TEXT("중심은 칸 중심"), Center, A.RegionCenterCm);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneRegionSpawnTest,
	"Snarehaven.Zone.Region.SpeciesSpawnInsideZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneRegionSpawnTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	const UDataTable* const SpreadTable = LoadObject<UDataTable>(nullptr, RegionSpreadTablePath);
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, RegionAnimalTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable) || !TestNotNull(TEXT("DT_AnimalSpreadTable 로드"), SpreadTable)
		|| !TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable))
	{
		return false;
	}

	// 새 지도(2026-09-25): 존이 정사각형이 아니라 스포너는 배치 스크립트가 놓은 자리에 서고, 칸마다 제 존 뭍인지 거른다
	// (`AAnimalSpreadActor::CollectFreeCells`). 레벨의 스포너를 하나씩 — 걸러진 칸이 정원 이상이고 전부 제 존인가.
	const UWorld* Level = nullptr;
	const ASeaActor* const Sea = LoadHuntSea(Level);
	if (!TestNotNull(TEXT("L_Hunt 바다(거리장)"), Sea))
	{
		return false;
	}
	FVector2D Start = FVector2D::ZeroVector;
	TArray<const AAnimalSpreadActor*> Spreaders;
	for (const AActor* const Actor : Level->PersistentLevel->Actors)
	{
		if (const APlayerStart* const PlayerStart = Cast<APlayerStart>(Actor))
		{
			Start = FVector2D(PlayerStart->GetActorLocation());
		}
		if (const AAnimalSpreadActor* const Spreader = Cast<AAnimalSpreadActor>(Actor))
		{
			Spreaders.Add(Spreader);
		}
	}
	TSet<FName> ZonesWithSpawner;
	int32 CheckedCells = 0;
	for (const AAnimalSpreadActor* const Spreader : Spreaders)
	{
		const FString Label = Spreader->GetActorNameOrLabel();
		const FAnimalSpreadRow* const Row = SpreadTable->FindRow<FAnimalSpreadRow>(Spreader->GetSpreadRowName(), TEXT("ZoneRegionTest"));
		const FZoneRow* const Zone = Row ? SnareZones::FindZoneRow(ZoneTable, Row->ZoneID) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 행 %s 와 그 존"), *Label, *Spreader->GetSpreadRowName().ToString()), Zone))
		{
			continue;
		}
		const FVector2D Origin(Spreader->GetActorLocation());
		int32 Inside = 0;
		for (const FVector2D& Offset : AAnimalSpreadActor::MakeSpreadCells(Row->AreaHalfExtentCm, Row->MinSpacingCm,
			Row->PlayerStartClearCm, { Start - Origin }))
		{
			const FVector2D Cell = Origin + Offset;
			if (!IsInsideOnMap(Sea, *Zone, Cell))
			{
				continue;   // 스포너가 버리는 칸
			}
			++Inside;
			++CheckedCells;
			const FZoneRow* const At = SnareZones::FindRegionAt(ZoneTable, RegionLevel, FVector(Cell, 0.0), Sea);
			if (At == nullptr || At->ZoneID != Row->ZoneID)
			{
				AddError(FString::Printf(TEXT("[%s] 자리 %s 가 %s 칸 밖이다 (%s)"), *Label, *Cell.ToString(),
					*Row->ZoneID.ToString(), At ? *At->ZoneID.ToString() : TEXT("칸 없음")));
			}
		}
		TestTrue(*FString::Printf(TEXT("[%s] 제 존 칸이 정원 %d 이상 (%d칸)"), *Label, Row->AnimalCount, Inside), Inside >= Row->AnimalCount);
		ZonesWithSpawner.Add(Row->ZoneID);
	}
	AddInfo(FString::Printf(TEXT("스포너 존 %d개 · 본 자리 %d칸"), ZonesWithSpawner.Num(), CheckedCells));
	TestTrue(TEXT("자리를 하나 이상 봤다"), CheckedCells > 0);

	// 기본 플레이 종은 전부 맵 안 칸 존에 살고, 그 존엔 스포너가 있다 — 종이 제 존 스포너에서만 서므로 제 존 안에만 선다.
	int32 Species = 0;
	AnimalTable->ForeachRow<FAnimalRow>(TEXT("ZoneRegionTest"), [&](const FName& RowName, const FAnimalRow& Row)
	{
		if (Row.bPlaceholderSpecies)
		{
			return;
		}
		++Species;
		const FZoneRow* const Zone = SnareZones::FindZoneRow(ZoneTable, Row.HabitatZoneID);
		TestTrue(*FString::Printf(TEXT("%s 의 서식 존 %s 는 맵 안 칸이다"), *RowName.ToString(), *Row.HabitatZoneID.ToString()),
			Zone != nullptr && SnareZones::IsRegionRow(*Zone));
		TestTrue(*FString::Printf(TEXT("%s 의 서식 존 %s 에 스포너가 있다"), *RowName.ToString(), *Row.HabitatZoneID.ToString()),
			ZonesWithSpawner.Contains(Row.HabitatZoneID));
	});
	TestTrue(TEXT("기본 플레이 종을 하나 이상 봤다"), Species > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneRegionTraitsTest,
	"Snarehaven.Zone.Region.TraitsApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneRegionTraitsTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	const UDataTable* const VitalsTable = LoadObject<UDataTable>(nullptr, RegionVitalsTablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable) || !TestNotNull(TEXT("DT_PlayerVitalsTable 로드"), VitalsTable))
	{
		return false;
	}

	const UWorld* Level = nullptr;
	const ASeaActor* const Sea = LoadHuntSea(Level);
	if (!TestNotNull(TEXT("L_Hunt 바다(거리장)"), Sea))
	{
		return false;
	}

	// ── 늪: 건축 불가 칸 가운데에 서면 막히고, 다른 칸 가운데에선 안 막힌다 ──
	int32 BlockedZones = 0;
	int32 DrainZones = 0;
	for (const FZoneRow& Row : ReadAllZoneRows(ZoneTable))
	{
		if (!SnareZones::IsRegionRow(Row))
		{
			continue;
		}
		// 새 지도 존은 틀 가운데가 다른 존일 수 있다 — 틀 가운데에서 가장 가까운 제 존 뭍 칸을 쓴다
		FVector2D Probe = Row.RegionCenterCm;
		if (Row.FieldCode > 0 && !TestTrue(*FString::Printf(TEXT("[%s] 지도에 제 존 뭍 칸이 있다"), *Row.ZoneID.ToString()),
			Sea->FindZonePoint(Row.FieldCode, Row.RegionCenterCm, 400.f, Row.RegionHalfExtentCm * 2.f, Probe)))
		{
			continue;
		}
		const FZoneRow* const At = SnareZones::FindRegionAt(ZoneTable, RegionLevel, FVector(Probe, 0.0), Sea);
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 칸 가운데는 그 칸이다"), *Row.ZoneID.ToString()), At))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("[%s] 칸 가운데 판정"), *Row.ZoneID.ToString()), At->ZoneID, Row.ZoneID);
		TestEqual(*FString::Printf(TEXT("[%s] 가운데서 건축 %s"), *Row.ZoneID.ToString(), Row.bBuildBlocked ? TEXT("불가") : TEXT("가능")),
			At->bBuildBlocked, Row.bBuildBlocked);
		BlockedZones += Row.bBuildBlocked ? 1 : 0;

		// ── 설원: 칸 값을 받은 몸은 같은 1초에 표 값만큼 더 닳는다 ──
		if (Row.HealthDrainPerSecond > 0.f)
		{
			++DrainZones;
			const FPlayerVitalsRow* const Vitals = VitalsTable->FindRow<FPlayerVitalsRow>(TEXT("Player"), TEXT("ZoneRegionTest"));
			if (!TestNotNull(TEXT("`Player` 행"), Vitals))
			{
				return false;
			}
			UPlayerVitalsComponent* const Plain = NewObject<UPlayerVitalsComponent>();
			UPlayerVitalsComponent* const Cold = NewObject<UPlayerVitalsComponent>();
			Plain->InitializeFromRow(*Vitals);
			Cold->InitializeFromRow(*Vitals);
			// 가득 차 있으면 회복이 잘려 차이가 안 보인다 — 절반을 깎고 잰다(자연 회복이 도는 쪽).
			Plain->ApplyDamage(Vitals->MaxHealth * 0.5f);
			Cold->ApplyDamage(Vitals->MaxHealth * 0.5f);
			Cold->SetZoneHealthDrain(At->HealthDrainPerSecond);
			Plain->AdvanceTime(1.f);
			Cold->AdvanceTime(1.f);
			TestEqual(*FString::Printf(TEXT("[%s] 1초 체력 차 = 표의 체력 감소"), *Row.ZoneID.ToString()),
				Plain->GetHealth() - Cold->GetHealth(), Row.HealthDrainPerSecond, 0.01f);
			Cold->SetZoneHealthDrain(0.f);
			const float PlainBefore = Plain->GetHealth();
			const float ColdBefore = Cold->GetHealth();
			Plain->AdvanceTime(1.f);
			Cold->AdvanceTime(1.f);
			TestEqual(TEXT("칸을 나오면(0) 보통 몸과 같이 흐른다"),
				Cold->GetHealth() - ColdBefore, Plain->GetHealth() - PlainBefore, 0.01f);
		}
	}
	TestTrue(TEXT("건축 불가 칸(늪)이 하나 이상"), BlockedZones > 0);
	TestTrue(TEXT("체력 감소 칸(설원)이 하나 이상"), DrainZones > 0);

	// ── 섬 입장료: 요금을 받는 칸은 전부 서버 `PayFerry` 가 아는 이름표가 있다 (없으면 공짜로 들어간다) ──
	int32 FeeZones = 0;
	for (const FZoneRow& Row : ReadAllZoneRows(ZoneTable))
	{
		if (Row.bChargesEntryFee)
		{
			++FeeZones;
			TestFalse(*FString::Printf(TEXT("[%s] 입장료 칸은 PayFerry dest 가 있다"), *Row.ZoneID.ToString()),
				UZoneTravelComponent::GetFerryDestKey(Row.ZoneID).IsEmpty());
		}
	}
	// 새 지도에선 섬 숲이 본토 숲과 같은 존이라 입장료 칸이 없을 수 있다 — 배 삯은 선착장(8.14)이 받는다(DESIGN 1.3)
	AddInfo(FString::Printf(TEXT("입장료 칸 %d개"), FeeZones));
	return true;
}

// ── 바다에는 존 특성이 없다 — 설원 앞바다에서 추위로 깎이던 결함(루프#744, INBOX 감독 결함 10-08 20:45) ──
// 바다 칸에도 가장 가까운 뭍 존 코드가 붙어 있다. 판정이 코드만 보면 설원 중심 (22400, -7400)(해안 14m 바깥)이 설원이라
// 헤엄치는 몸이 얼고, 같은 몸이 숨이 차 죽는다. 칸마다: 뭍이면 그 코드의 행, 바다면 행 없음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneRegionSeaTest,
	"Snarehaven.Zone.Region.SeaHasNoTrait",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneRegionSeaTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	if (!TestNotNull(TEXT("DT_ZoneTable 로드"), ZoneTable))
	{
		return false;
	}
	const UWorld* Level = nullptr;
	const ASeaActor* const Sea = LoadHuntSea(Level);
	if (!TestNotNull(TEXT("L_Hunt 바다(거리장)"), Sea))
	{
		return false;
	}
	TMap<int32, FName> RowByCode;
	for (const FZoneRow& Row : ReadAllZoneRows(ZoneTable))
	{
		if (Row.FieldCode > 0)
		{
			RowByCode.Add(Row.FieldCode, Row.ZoneID);
		}
	}
	TestTrue(TEXT("칸 코드를 가진 존 행이 있다"), RowByCode.Num() > 0);

	int32 SeaCells = 0;
	int32 SeaCellsWithCode = 0;
	int32 LandCells = 0;
	int32 Wrong = 0;
	for (int32 I = 0; I < Sea->ZoneFieldSize.X; ++I)
	{
		for (int32 J = 0; J < Sea->ZoneFieldSize.Y; ++J)
		{
			const FVector Center(Sea->FieldOriginCm.X + (I + 0.5) * Sea->ZoneCellCm, Sea->FieldOriginCm.Y + (J + 0.5) * Sea->ZoneCellCm, 0.0);
			const int32 Code = Sea->ZoneCodeAt(Center) & 0x0F;
			const bool bLand = Sea->CoastDistance(Center) < 0.f;
			const FName* const Expected = bLand ? RowByCode.Find(Code) : nullptr;
			const FZoneRow* const At = SnareZones::FindRegionAt(ZoneTable, RegionLevel, Center, Sea);
			SeaCells += bLand ? 0 : 1;
			SeaCellsWithCode += !bLand && Code > 0 ? 1 : 0;
			LandCells += bLand ? 1 : 0;
			if ((At == nullptr) != (Expected == nullptr) || (At != nullptr && At->ZoneID != *Expected))
			{
				if (++Wrong <= 5)
				{
					AddError(FString::Printf(TEXT("칸 (%d, %d) %s 인데 판정 %s"), I, J, bLand ? TEXT("뭍") : TEXT("바다"),
						At ? *At->ZoneID.ToString() : TEXT("없음")));
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("[Zone] 칸 판정: 뭍 %d칸 · 바다 %d칸(코드 붙은 %d) · 어긋남 %d"), LandCells, SeaCells, SeaCellsWithCode, Wrong));
	TestEqual(TEXT("뭍·바다 판정이 어긋난 칸"), Wrong, 0);
	// 코드만 보는 옛 판정이었다면 이 칸들이 전부 틀렸다 — 이 시험이 빈 껍데기가 아니라는 증거
	TestTrue(TEXT("코드가 붙은 바다 칸이 있다(시험이 바다를 본다)"), SeaCellsWithCode > 0);

	// 감독이 캡처한 자리 — 설원 중심은 바다다
	const FVector Reported(22400.0, -7400.0, 0.0);
	TestTrue(TEXT("설원 중심 (22400, -7400) 은 바다"), Sea->CoastDistance(Reported) > 0.f);
	TestNull(TEXT("설원 중심 (22400, -7400) 은 어느 존도 아니다"), SnareZones::FindRegionAt(ZoneTable, RegionLevel, Reported, Sea));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
