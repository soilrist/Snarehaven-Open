// ROADMAP M53-04 2층 단정 — 존·광산 건너기 소리 빈 길 0 (DESIGN 8.10 「존·광산 건너기 소리」).
// ① 건너기 갈래(`SnareSound::EZoneCrossing` 3갈래)가 확정됐을 때 전부 소리 이름이 있는가. 갈래가 늘었는데 소리를 안 붙였으면(4번째 값이 소리를 내면) 잡는다.
// ② 표 「소리 파일」 줄의 새 이름(`SFX_<이름>_01~03`)이 전부 반입 변형 3이고, 갈래가 내는 이름(나룻배 `BoatBoard` 빌림 포함)이 전부 반입 에셋인가.
// ③ 건너기 함수 3개(포털·나룻배·광산 문)의 소스가 갈래마다 `PlayCrossingCue` 를 실제로 부르는가.
// 재생 자체(확정 1 · 거절 0 · 멀티캐스트)는 `Snarehaven.Mine`·`Snarehaven.FerryPier` 시험이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.ZoneSoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const ZoneSoundDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const ZoneSoundTableHeader = TEXT("**존·광산 건너기 소리 (M53-00");
	constexpr int32 ZoneCrossingCount = 3;

	/** 갈래 번호 → 확정됐을 때 소리 이름. 번호가 갈래 수를 넘으면 모르는 갈래라 None 이어야 한다. */
	FName CueForCrossing(int32 Crossing)
	{
		return SnareSound::ZoneCue(static_cast<SnareSound::EZoneCrossing>(Crossing), true).Cue;
	}

	/** 갈래마다 소리 이름이 있는가 — 빈 길 한 줄씩. `CrossingCount` 번째부터는 소리가 없어야 한다(갈래가 늘었는데 이 시험이 낡은 경우). */
	TArray<FString> SurveySilentCrossings(int32 CrossingCount)
	{
		TArray<FString> Gaps;
		for (int32 Crossing = 0; Crossing < CrossingCount; ++Crossing)
		{
			if (CueForCrossing(Crossing).IsNone())
			{
				Gaps.Add(FString::Printf(TEXT("건너기 갈래 %d: 확정돼도 소리 없음"), Crossing));
			}
		}
		if (!CueForCrossing(CrossingCount).IsNone())
		{
			Gaps.Add(FString::Printf(TEXT("건너기 갈래 %d 도 소리가 있다 — 갈래가 늘었으니 시험의 갈래 수를 고친다"), CrossingCount));
		}
		return Gaps;
	}

	/** DESIGN 8.10 표 「소리 파일」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 표를 못 찾으면 빈 목록. */
	TArray<FString> ReadZoneTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 HeaderAt = Design.Find(ZoneSoundTableHeader, ESearchCase::CaseSensitive);
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

	/** 새로 만든 표 이름은 변형 3, 빌린 이름은 1개 이상 — 모자라면 한 줄씩. */
	TArray<FString> SurveyZoneMissingAssets(const TArray<FString>& Names, int32 MinVariants, USnareSoundSubsystem* Sounds)
	{
		TArray<FString> Gaps;
		for (const FString& Name : Names)
		{
			const int32 Variants = Sounds->CountVariants(FName(*Name));
			if (MinVariants == 3 ? Variants != 3 : Variants < MinVariants)
			{
				Gaps.Add(FString::Printf(TEXT("소리 %s: 반입 변형 %d개 (%s)"), *Name, Variants,
					MinVariants == 3 ? TEXT("3개여야") : TEXT("1개 이상이어야")));
			}
		}
		return Gaps;
	}

	/** 건너기 함수 소스에 갈래별 재생 호출이 있는가 — 없으면 한 줄. 파일을 못 읽어도 한 줄. */
	TArray<FString> SurveyUnwiredCrossings(const TArray<TPair<FString, FString>>& FileAndNeedle)
	{
		TArray<FString> Gaps;
		for (const TPair<FString, FString>& Item : FileAndNeedle)
		{
			FString Source;
			if (!FFileHelper::LoadFileToString(Source, *(FPaths::ProjectDir() / Item.Key)))
			{
				Gaps.Add(FString::Printf(TEXT("%s: 소스를 못 읽음"), *Item.Key));
			}
			else if (!Source.Contains(Item.Value, ESearchCase::CaseSensitive))
			{
				Gaps.Add(FString::Printf(TEXT("%s: `%s` 호출 없음 — 건너도 무음"), *Item.Key, *Item.Value));
			}
		}
		return Gaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenZoneSoundGapTest, "Snarehaven.World.ZoneSoundGap.NoSilentCrossing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenZoneSoundGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / ZoneSoundDesignFile))))
	{
		return false;
	}

	// ① 갈래 전부 소리 있음.
	TArray<FString> Gaps = SurveySilentCrossings(ZoneCrossingCount);

	// ② 표 새 이름 = 반입 변형 3 · 갈래가 내는 이름(빌림 포함) = 반입 에셋.
	const TArray<FString> TableNames = ReadZoneTableCueNames(Design);
	TestEqual(TEXT("표 「소리 파일」 새 이름 2개 (ZoneWarp·MineDoor)"), TableNames.Num(), 2);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	Gaps.Append(SurveyZoneMissingAssets(TableNames, 3, Sounds));
	TArray<FString> PlayedNames;
	for (int32 Crossing = 0; Crossing < ZoneCrossingCount; ++Crossing)
	{
		const FName Cue = CueForCrossing(Crossing);
		if (!Cue.IsNone())
		{
			PlayedNames.AddUnique(Cue.ToString());
		}
	}
	Gaps.Append(SurveyZoneMissingAssets(PlayedNames, 1, Sounds));

	// ③ 건너기 함수 3개가 소리를 부른다.
	const TArray<TPair<FString, FString>> Wiring = {
		{ TEXT("Source/Snarehaven/Private/World/ZonePortalActor.cpp"), TEXT("PlayCrossingCue(SnareSound::EZoneCrossing::Portal") },
		{ TEXT("Source/Snarehaven/Private/World/ZonePortalActor.cpp"), TEXT("PlayCrossingCue(SnareSound::EZoneCrossing::Ferry") },
		{ TEXT("Source/Snarehaven/Private/World/MineDoorActor.cpp"), TEXT("PlayCrossingCue(true") },
	};
	Gaps.Append(SurveyUnwiredCrossings(Wiring));

	AddInfo(FString::Printf(TEXT("[Sound] 건너기 소리 길: 갈래 %d · 표 새 이름 %d · 내는 이름 %d · 연결 %d · 빈 길 %d"),
		ZoneCrossingCount, TableNames.Num(), PlayedNames.Num(), Wiring.Num(), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 값으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("ZoneTestMissing"));
		const TArray<FString> FakeGaps = SurveyZoneMissingAssets(WithFake, 3, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 소리 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("ZoneTestMissing")));
	}
	{
		// 갈래 수를 하나 더 크게 잡으면 소리 없는 4번째 갈래가 빈 길로 잡힌다.
		const TArray<FString> FakeGaps = SurveySilentCrossings(ZoneCrossingCount + 1);
		TestTrue(TEXT("역검증: 소리 없는 갈래 한 줄 → 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("건너기 갈래 3")));
	}
	{
		const TArray<FString> FakeGaps = SurveyUnwiredCrossings({ { TEXT("Source/Snarehaven/Private/World/MineDoorActor.cpp"), TEXT("PlayCrossingCue(NoSuchCrossing") } });
		TestTrue(TEXT("역검증: 없는 호출 한 줄 → 연결 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("NoSuchCrossing")));
	}
	return true;
}

#endif
