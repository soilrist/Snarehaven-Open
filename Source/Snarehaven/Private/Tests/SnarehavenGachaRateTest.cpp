// ROADMAP M7-01 2층 검사 — 올가미 기계의 확률표(`DT_GachaRates`)와 비용표(`DT_GachaConfig`)가 성한지.
// 아직 이 표를 읽는 런타임 코드는 없다(뽑기 화면은 M7-04) — 그래서 이 검사는 **표 자체**만 본다.
// 확률 합이 100%가 아니면 그건 고를 수 없는 결과가 있거나 확률이 부풀려진 표라는 뜻이고,
// 그대로 화면에 뜨면 확률 오표시다 (DESIGN 10.4 — 2년 이하 징역 또는 2000만원 이하 벌금).
// 아래 두 검사는 **표 안에서 닫히는 것만** 본다 — 네 값을 다 같이 바꿔도 합은 100% 라 통과한다.
// 문서(`docs/GACHA_DISCLOSURE.md`)와 맞대 보는 눈은 이 파일 끝의 M7-02 검사다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/GachaCostRow.h"
#include "Data/GachaRateRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* GachaRateTablePath = TEXT("/Game/Game/Data/Gacha/DT_GachaRates.DT_GachaRates");
	const TCHAR* GachaConfigTablePath = TEXT("/Game/Game/Data/Gacha/DT_GachaConfig.DT_GachaConfig");

	// 확률 백분율의 합. 100 이라는 숫자는 밸런스 값이 아니라 백분율의 정의다 (DataDriven.md 3 해당 없음).
	constexpr float TotalProbabilityPct = 100.0f;

	// float 합산 오차 한도. 70 + 22 + 6.5 + 1.5 를 더하면 정확히 100 이 아닐 수 있다.
	constexpr float ProbabilityTolerance = 0.001f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaRateSumTest, "Snarehaven.Gacha.Table.GradeRatesSumTo100",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaRateSumTest::RunTest(const FString& Parameters)
{
	UDataTable* RateTable = LoadObject<UDataTable>(nullptr, GachaRateTablePath);
	if (!TestNotNull(TEXT("DT_GachaRates 로드"), RateTable))
	{
		return false;
	}

	const TArray<FName> RowNames = RateTable->GetRowNames();
	if (!TestTrue(TEXT("확률 행이 하나 이상 있다"), RowNames.Num() > 0))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("DT_GachaRates 행 %d개를 본다"), RowNames.Num()));

	float SumPct = 0.0f;
	TSet<EGachaGrade> SeenGrades;
	for (const FName& RowName : RowNames)
	{
		const FGachaRateRow* Row = RateTable->FindRow<FGachaRateRow>(RowName, TEXT("GachaRateTest"));
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), *RowName.ToString()), Row))
		{
			continue;
		}

		// 자리표시(-1)가 남아 있으면 사람이 아직 안 정한 칸이다 (Verification.md 7).
		TestTrue(*FString::Printf(TEXT("%s 의 ProbabilityPct 가 미정(-1)이 아니다"), *RowName.ToString()),
			Row->ProbabilityPct > 0.0f);

		// 같은 등급이 두 줄이면 합은 맞아도 화면에 같은 등급이 두 번 뜬다.
		TestFalse(*FString::Printf(TEXT("%s 의 등급이 다른 행과 겹치지 않는다"), *RowName.ToString()),
			SeenGrades.Contains(Row->Grade));
		SeenGrades.Add(Row->Grade);

		TestFalse(*FString::Printf(TEXT("%s 의 DisplayName 이 비어 있지 않다"), *RowName.ToString()),
			Row->DisplayName.IsEmpty());

		AddInfo(FString::Printf(TEXT("[%s] %s = %.2f%%"),
			*RowName.ToString(), *Row->DisplayName.ToString(), Row->ProbabilityPct));
		SumPct += Row->ProbabilityPct;
	}

	AddInfo(FString::Printf(TEXT("확률 합 %.4f%%"), SumPct));
	TestTrue(*FString::Printf(TEXT("등급 확률의 합이 100%% 다 (실제 %.4f%%)"), SumPct),
		FMath::IsNearlyEqual(SumPct, TotalProbabilityPct, ProbabilityTolerance));

	// 위 반복은 **있는 행**만 훑는다 — 등급 하나를 통째로 빠뜨리면 합이 안 맞아 걸리지만,
	// 어느 등급이 빠졌는지는 안 나온다. 루프#91 이 목장 생산에서 겪은 구멍이라 반대 방향으로도 본다.
	const UEnum* GradeEnum = StaticEnum<EGachaGrade>();
	if (TestNotNull(TEXT("EGachaGrade enum"), GradeEnum))
	{
		// 마지막 항목은 UHT 가 붙이는 `_MAX` 라 뺀다.
		for (int32 Index = 0; Index < GradeEnum->NumEnums() - 1; ++Index)
		{
			const EGachaGrade Grade = static_cast<EGachaGrade>(GradeEnum->GetValueByIndex(Index));
			TestTrue(*FString::Printf(TEXT("%s 등급 행이 DT_GachaRates 에 있다"),
				*GradeEnum->GetNameStringByIndex(Index)), SeenGrades.Contains(Grade));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaCostTest, "Snarehaven.Gacha.Table.CostAndPityAreSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaCostTest::RunTest(const FString& Parameters)
{
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, GachaConfigTablePath);
	if (!TestNotNull(TEXT("DT_GachaConfig 로드"), ConfigTable))
	{
		return false;
	}

	// 기계가 하나라 행도 하나다 — 둘이 되면 어느 쪽이 진짜 비용인지 코드가 고를 수 없다.
	const TArray<FName> RowNames = ConfigTable->GetRowNames();
	if (!TestEqual(TEXT("DT_GachaConfig 는 행이 하나다"), RowNames.Num(), 1))
	{
		return false;
	}

	const FGachaCostRow* Row = ConfigTable->FindRow<FGachaCostRow>(RowNames[0], TEXT("GachaCostTest"));
	if (!TestNotNull(TEXT("비용 행"), Row))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("[%s] 1회 젬 %d · 1회 코인 %d · 10연차 젬 %d · 천장 %d회"),
		*RowNames[0].ToString(), Row->SinglePullGems, Row->SinglePullCoins, Row->TenPullGems, Row->PityPullCount));

	TestTrue(TEXT("SinglePullGems 가 미정(-1)이 아니다"), Row->SinglePullGems > 0);
	TestTrue(TEXT("SinglePullCoins 가 미정(-1)이 아니다"), Row->SinglePullCoins > 0);
	TestTrue(TEXT("TenPullGems 가 미정(-1)이 아니다"), Row->TenPullGems > 0);
	TestTrue(TEXT("PityPullCount 가 미정(-1)이 아니다"), Row->PityPullCount > 0);

	return true;
}

// ROADMAP M7-02 — 확률 공개 문서(`docs/GACHA_DISCLOSURE.md`)와 표가 어긋나면 여기서 운다.
// 게임산업법(DESIGN 10.4)의 전형적인 사고는 **수치를 한쪽만 고쳐 화면과 공개 문서가 따로 노는 것**이고,
// 그 벌은 2년 이하 징역 또는 2000만원 이하 벌금이다. 문서가 원본, 표는 사본 — 둘이 같은지 본다.
//
// **이 검사는 숫자를 하나도 들고 있지 않다.** 70/22/6.5/1.5·100·500·900·50 을 여기 적으면 사본이
// 셋이 되어, 검사 자신이 막으려던 바로 그 사고를 저지른다. 문서에서 읽어 표와 맞댈 뿐이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGachaDisclosureTest, "Snarehaven.Gacha.Disclosure.TableMatchesDocument",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGachaDisclosureTest::RunTest(const FString& Parameters)
{
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/GACHA_DISCLOSURE.md");
	FString Doc;
	if (!TestTrue(*FString::Printf(TEXT("공개 문서를 읽는다 (%s)"), *DocPath), FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}

	// 문서의 등급표 줄을 그대로 긁는다 — `| 커먼 | 70.0% |`. 줄 단위로 보는 이유는 `|` 가
	// 줄을 넘어 짝지어지면 엉뚱한 칸끼리 붙기 때문이다.
	//
	// **칸 안에 `|` 가 못 들어가게 `[^|]` 로 묶는 것이 이 정규식의 핵심이다** (루프#130).
	// `(.+?)` 였을 때는 아이템표의 다섯 칸 줄 `| gacha.x | 이름 | 커먼 | 펫 의상 | 10.0% |` 이
	// 앞 네 칸을 통째로 한 이름으로 삼켜 등급 줄 행세를 했다 — 등급이 4개가 아니라 23개로 세어졌다.
	TArray<FString> Lines;
	Doc.ParseIntoArrayLines(Lines);

	const FRegexPattern GradeRowPattern(TEXT("^\\|\\s*([^|]+?)\\s*\\|\\s*([0-9]+(?:\\.[0-9]+)?)\\s*%\\s*\\|\\s*$"));
	// 아이템 줄은 다섯 칸이다 — ID · 이름 · 등급 · 카테고리 · 개별 확률.
	const FRegexPattern ItemRowPattern(TEXT("^\\|\\s*([^|]+?)\\s*\\|\\s*[^|]+?\\s*\\|\\s*([^|]+?)\\s*\\|\\s*[^|]+?\\s*\\|\\s*([0-9]+(?:\\.[0-9]+)?)\\s*%\\s*\\|"));
	TMap<FString, float> DocRates;
	TMap<FString, float> ItemSumByGrade;
	int32 ItemRowCount = 0;
	for (const FString& Line : Lines)
	{
		FRegexMatcher GradeMatcher(GradeRowPattern, Line);
		if (GradeMatcher.FindNext())
		{
			DocRates.Add(GradeMatcher.GetCaptureGroup(1), FCString::Atof(*GradeMatcher.GetCaptureGroup(2)));
			continue;
		}

		FRegexMatcher ItemMatcher(ItemRowPattern, Line);
		if (ItemMatcher.FindNext())
		{
			++ItemRowCount;
			ItemSumByGrade.FindOrAdd(ItemMatcher.GetCaptureGroup(2)) += FCString::Atof(*ItemMatcher.GetCaptureGroup(3));
		}
	}

	// 문서 모양이 바뀌어 한 줄도 못 읽으면 "비교할 게 없어 전부 통과" 가 된다 — 그게 제일 위험하다.
	if (!TestTrue(TEXT("문서에서 등급 줄을 하나 이상 읽었다"), DocRates.Num() > 0))
	{
		return false;
	}

	UDataTable* RateTable = LoadObject<UDataTable>(nullptr, GachaRateTablePath);
	if (!TestNotNull(TEXT("DT_GachaRates 로드"), RateTable))
	{
		return false;
	}

	const TArray<FName> RowNames = RateTable->GetRowNames();
	TestEqual(TEXT("문서의 등급 수와 DT_GachaRates 행 수가 같다"), RowNames.Num(), DocRates.Num());

	// ① 표 → 문서: 표에 있는 등급이 문서에도 같은 값으로 적혀 있나.
	TSet<FString> MatchedDocNames;
	for (const FName& RowName : RowNames)
	{
		const FGachaRateRow* Row = RateTable->FindRow<FGachaRateRow>(RowName, TEXT("GachaDisclosureTest"));
		if (!TestNotNull(*FString::Printf(TEXT("%s 행"), *RowName.ToString()), Row))
		{
			continue;
		}

		const FString DisplayName = Row->DisplayName.ToString();
		const float* DocPct = DocRates.Find(DisplayName);
		if (!TestNotNull(*FString::Printf(TEXT("문서에 '%s' 등급 줄이 있다"), *DisplayName), DocPct))
		{
			continue;
		}

		AddInfo(FString::Printf(TEXT("[%s] 문서 %.2f%% ↔ 표 %.2f%%"), *DisplayName, *DocPct, Row->ProbabilityPct));
		TestTrue(*FString::Printf(TEXT("'%s' 확률이 문서와 같다 (문서 %.4f%% / 표 %.4f%%)"),
			*DisplayName, *DocPct, Row->ProbabilityPct),
			FMath::IsNearlyEqual(Row->ProbabilityPct, *DocPct, ProbabilityTolerance));
		MatchedDocNames.Add(DisplayName);
	}

	// ② 문서 → 표: 있는 것만 훑으면 한쪽에만 있는 등급을 못 본다 (루프#91 이 목장 생산에서 겪은 구멍).
	for (const TPair<FString, float>& DocRate : DocRates)
	{
		TestTrue(*FString::Printf(TEXT("문서의 '%s' 등급이 DT_GachaRates 에도 있다"), *DocRate.Key),
			MatchedDocNames.Contains(DocRate.Key));
	}

	// ③ 아이템 줄 → 등급 줄: 등급표를 두 칸짜리로 좁힌 만큼(루프#130) 아이템 19줄이 아무 검사도
	// 안 받는 자리가 생긴다. 표가 따로 없으니 맞댈 상대는 문서 자신이다 — 한 등급 아이템들의
	// 개별 확률 합이 그 등급 확률과 같아야 한다(문서 19행의 "등급 안에서 균등 배분").
	// 한쪽만 고쳐 문서가 스스로 어긋나는 것이 이 검사가 막으려는 바로 그 사고다.
	if (ItemRowCount > 0)
	{
		for (const TPair<FString, float>& GradeSum : ItemSumByGrade)
		{
			const float* GradeRate = DocRates.Find(GradeSum.Key);
			if (!TestNotNull(*FString::Printf(TEXT("아이템 줄의 등급 '%s' 이 등급표에도 있다"), *GradeSum.Key), GradeRate))
			{
				continue;
			}

			AddInfo(FString::Printf(TEXT("[%s] 아이템 합 %.2f%% ↔ 등급 %.2f%%"), *GradeSum.Key, GradeSum.Value, *GradeRate));
			TestTrue(*FString::Printf(TEXT("'%s' 아이템 확률 합이 등급 확률과 같다 (합 %.4f%% / 등급 %.4f%%)"),
				*GradeSum.Key, GradeSum.Value, *GradeRate),
				FMath::IsNearlyEqual(GradeSum.Value, *GradeRate, ProbabilityTolerance));
		}
	}
	else
	{
		AddInfo(TEXT("문서에 아이템별 확률 줄이 없다 — 등급 확률만 맞댄다"));
	}

	// ④ 비용·천장은 표가 아니라 문서의 글머리 줄에 적혀 있다 — 같은 방식으로 숫자만 꺼내 맞댄다.
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, GachaConfigTablePath);
	if (!TestNotNull(TEXT("DT_GachaConfig 로드"), ConfigTable))
	{
		return false;
	}

	const TArray<FName> ConfigRowNames = ConfigTable->GetRowNames();
	if (!TestEqual(TEXT("DT_GachaConfig 는 행이 하나다"), ConfigRowNames.Num(), 1))
	{
		return false;
	}

	const FGachaCostRow* Cost = ConfigTable->FindRow<FGachaCostRow>(ConfigRowNames[0], TEXT("GachaDisclosureTest"));
	if (!TestNotNull(TEXT("비용 행"), Cost))
	{
		return false;
	}

	// 문서에 그 문구 자체가 없어지면 -1 이 나와 어느 값과도 안 맞는다 — 조용히 통과하지 않는다.
	auto DocInt = [&Doc](const TCHAR* Pattern) -> int32
	{
		FRegexMatcher Matcher(FRegexPattern(Pattern), Doc);
		return Matcher.FindNext() ? FCString::Atoi(*Matcher.GetCaptureGroup(1)) : -1;
	};

	const int32 DocSingleGems = DocInt(TEXT("1회 젬\\s*([0-9]+)"));
	const int32 DocSingleCoins = DocInt(TEXT("스네어 코인\\s*([0-9]+)"));
	const int32 DocTenGems = DocInt(TEXT("10연차 젬\\s*([0-9]+)"));
	const int32 DocPity = DocInt(TEXT("천장:\\s*([0-9]+)회"));

	AddInfo(FString::Printf(TEXT("문서: 1회 젬 %d · 1회 코인 %d · 10연차 젬 %d · 천장 %d회"),
		DocSingleGems, DocSingleCoins, DocTenGems, DocPity));

	TestEqual(TEXT("1회 젬 비용이 문서와 같다"), Cost->SinglePullGems, DocSingleGems);
	TestEqual(TEXT("1회 코인 비용이 문서와 같다"), Cost->SinglePullCoins, DocSingleCoins);
	TestEqual(TEXT("10연차 젬 비용이 문서와 같다"), Cost->TenPullGems, DocTenGems);
	TestEqual(TEXT("천장 횟수가 문서와 같다"), Cost->PityPullCount, DocPity);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
