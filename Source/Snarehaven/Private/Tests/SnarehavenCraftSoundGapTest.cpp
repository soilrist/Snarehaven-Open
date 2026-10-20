// ROADMAP M52-03 2층 단정 — 제작 소리 빈 길 0 (DESIGN 8.10 「제작 소리」).
// ① 제작 곳 4군데(배낭·작업대·불·제련소)가 확정됐을 때 전부 소리 이름과 0 보다 큰 크기가 있고, 이름이 3갈래(서로 다른 3개)인가.
// ② 표 「소리 파일」 줄의 새 이름(`SFX_<이름>_01~03`)이 전부 반입 변형 3이고, 곳이 내는 이름이 표 이름과 같은가.
// 재생 자체(확정 1 · 거절 0)는 `Snarehaven.Craft` 시험이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Craft.SoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Crafting/SnareCraftRules.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const CraftSoundDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const CraftSoundTableHeader = TEXT("**제작 소리 (M52-00");
	constexpr int32 CraftPlaceCount = 4;

	/** 곳 번호 → 확정 소리 이름. */
	FName CueForPlace(int32 Place)
	{
		return SnareCraftRules::ConfirmCue(static_cast<ESnareCraftPlace>(Place));
	}

	/** 곳마다 소리 이름과 크기가 있는가 — 빈 길 한 줄씩. */
	TArray<FString> SurveySilentCraftPlaces(int32 PlaceCount, FName (*CueOf)(int32))
	{
		TArray<FString> Gaps;
		for (int32 Place = 0; Place < PlaceCount; ++Place)
		{
			if (CueOf(Place).IsNone())
			{
				Gaps.Add(FString::Printf(TEXT("제작 곳 %d: 확정해도 소리 이름 없음"), Place));
			}
			if (SnareCraftRules::ConfirmVolume(static_cast<ESnareCraftPlace>(Place)) <= 0.f)
			{
				Gaps.Add(FString::Printf(TEXT("제작 곳 %d: 소리 크기 0"), Place));
			}
		}
		return Gaps;
	}

	/** DESIGN 8.10 표 「소리 파일」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 표를 못 찾으면 빈 목록. */
	TArray<FString> ReadCraftTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 HeaderAt = Design.Find(CraftSoundTableHeader, ESearchCase::CaseSensitive);
		const int32 RowAt = HeaderAt == INDEX_NONE ? INDEX_NONE
			: Design.Find(TEXT("| 소리 파일 |"), ESearchCase::CaseSensitive, ESearchDir::FromStart, HeaderAt);
		if (RowAt == INDEX_NONE)
		{
			return Names;
		}
		const int32 RowEnd = Design.Find(TEXT("\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, RowAt);
		const FString Row = Design.Mid(RowAt, (RowEnd == INDEX_NONE ? Design.Len() : RowEnd) - RowAt);
		FRegexMatcher Matcher(FRegexPattern(TEXT("SFX_([A-Za-z]+)_01~03")), Row);
		while (Matcher.FindNext())
		{
			Names.AddUnique(Matcher.GetCaptureGroup(1));
		}
		return Names;
	}

	/** 이름마다 반입 변형이 3개인가 — 모자라면 한 줄씩. */
	TArray<FString> SurveyCraftMissingAssets(const TArray<FString>& Names, USnareSoundSubsystem* Sounds)
	{
		TArray<FString> Gaps;
		for (const FString& Name : Names)
		{
			const int32 Variants = Sounds->CountVariants(FName(*Name));
			if (Variants != 3)
			{
				Gaps.Add(FString::Printf(TEXT("소리 %s: 반입 변형 %d개 (3개여야)"), *Name, Variants));
			}
		}
		return Gaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCraftSoundGapTest, "Snarehaven.Craft.SoundGap.NoSilentCraftPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCraftSoundGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / CraftSoundDesignFile))))
	{
		return false;
	}

	// ① 곳 전부 소리 이름·크기 있음, 이름 3갈래.
	TArray<FString> Gaps = SurveySilentCraftPlaces(CraftPlaceCount, &CueForPlace);
	TArray<FString> PlayedNames;
	for (int32 Place = 0; Place < CraftPlaceCount; ++Place)
	{
		PlayedNames.AddUnique(CueForPlace(Place).ToString());
	}
	TestEqual(TEXT("제작 소리 이름이 서로 다른 3갈래 (망치·쉭·지글)"), PlayedNames.Num(), 3);

	// ② 표 새 이름 = 반입 변형 3 · 곳이 내는 이름 = 표 이름.
	const TArray<FString> TableNames = ReadCraftTableCueNames(Design);
	TestEqual(TEXT("표 「소리 파일」 새 이름 3개 (CraftHammer·CraftSmelt·CraftSizzle)"), TableNames.Num(), 3);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	Gaps.Append(SurveyCraftMissingAssets(TableNames, Sounds));
	for (const FString& Played : PlayedNames)
	{
		if (!TableNames.Contains(Played))
		{
			Gaps.Add(FString::Printf(TEXT("소리 %s: 곳이 내는데 DESIGN 표에 없음"), *Played));
		}
	}

	AddInfo(FString::Printf(TEXT("[Sound] 제작 소리 길: 곳 %d · 표 새 이름 %d · 내는 이름 %d · 빈 길 %d"),
		CraftPlaceCount, TableNames.Num(), PlayedNames.Num(), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 값으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("CraftTestMissing"));
		const TArray<FString> FakeGaps = SurveyCraftMissingAssets(WithFake, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 소리 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("CraftTestMissing")));
	}
	{
		// 이름이 비는 곳 하나를 흉내 내면 빈 길로 잡힌다.
		const TArray<FString> FakeGaps = SurveySilentCraftPlaces(1, [](int32) { return FName(); });
		TestTrue(TEXT("역검증: 이름 없는 곳 한 줄 → 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("제작 곳 0")));
	}
	return true;
}

#endif
