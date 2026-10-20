// ROADMAP M51-05 2층 단정 — 배 소리 빈 길 0 (DESIGN 8.9 「배 소리」).
// ① 배 동작(`SnareSound::EBoatAction` 4갈래)이 받아들여졌을 때 전부 소리 이름이 있는가. 동작이 늘었는데 소리를 안 붙였으면(5번째 값이 소리를 내면) 잡는다.
// ② 표 「소리 파일」 줄의 새 이름(`SFX_<이름>_01~03`)이 전부 반입 변형 3이고, 동작이 내는 이름(넣기 `Storage` 빌림 포함)이 전부 반입 에셋인가.
// 재생 자체(받아들임 1 · 거절 0)는 `Snarehaven.UI.WaterAction` 이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.BoatSoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const BoatSoundDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const BoatSoundTableHeader = TEXT("**배 소리 (M51-00");
	constexpr int32 BoatActionCount = 4;

	/** 동작 번호 → 받아들였을 때 소리 이름. 번호가 동작 수를 넘으면 모르는 동작이라 None 이어야 한다. */
	FName CueForAction(int32 Action)
	{
		return SnareSound::BoatCue(static_cast<SnareSound::EBoatAction>(Action), true, FString()).Cue;
	}

	/** 동작마다 소리 이름이 있는가 — 빈 길 한 줄씩. `ActionCount` 번째부터는 소리가 없어야 한다(동작이 늘었는데 이 시험이 낡은 경우). */
	TArray<FString> SurveySilentBoatActions(int32 ActionCount)
	{
		TArray<FString> Gaps;
		for (int32 Action = 0; Action < ActionCount; ++Action)
		{
			if (CueForAction(Action).IsNone())
			{
				Gaps.Add(FString::Printf(TEXT("배 동작 %d: 받아들여도 소리 없음"), Action));
			}
		}
		if (!CueForAction(ActionCount).IsNone())
		{
			Gaps.Add(FString::Printf(TEXT("배 동작 %d 도 소리가 있다 — 동작이 늘었으니 시험의 동작 수를 고친다"), ActionCount));
		}
		return Gaps;
	}

	/** DESIGN 8.9 표 「소리 파일」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 표를 못 찾으면 빈 목록. */
	TArray<FString> ReadBoatTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 HeaderAt = Design.Find(BoatSoundTableHeader, ESearchCase::CaseSensitive);
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
	TArray<FString> SurveyBoatMissingAssets(const TArray<FString>& Names, int32 MinVariants, USnareSoundSubsystem* Sounds)
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBoatSoundGapTest, "Snarehaven.World.BoatSoundGap.NoSilentBoatPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBoatSoundGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / BoatSoundDesignFile))))
	{
		return false;
	}

	// ① 동작 전부 소리 있음.
	TArray<FString> Gaps = SurveySilentBoatActions(BoatActionCount);

	// ② 표 새 이름 = 반입 변형 3 · 동작이 내는 이름(빌림 포함) = 반입 에셋.
	const TArray<FString> TableNames = ReadBoatTableCueNames(Design);
	TestEqual(TEXT("표 「소리 파일」 새 이름 3개 (BoatLaunch·BoatBoard·BoatLeave)"), TableNames.Num(), 3);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	Gaps.Append(SurveyBoatMissingAssets(TableNames, 3, Sounds));
	TArray<FString> PlayedNames;
	for (int32 Action = 0; Action < BoatActionCount; ++Action)
	{
		const FName Cue = CueForAction(Action);
		if (!Cue.IsNone())
		{
			PlayedNames.AddUnique(Cue.ToString());
		}
	}
	Gaps.Append(SurveyBoatMissingAssets(PlayedNames, 1, Sounds));

	AddInfo(FString::Printf(TEXT("[Sound] 배 소리 길: 동작 %d · 표 새 이름 %d · 내는 이름 %d · 빈 길 %d"),
		BoatActionCount, TableNames.Num(), PlayedNames.Num(), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 값으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("BoatTestMissing"));
		const TArray<FString> FakeGaps = SurveyBoatMissingAssets(WithFake, 3, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 소리 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("BoatTestMissing")));
	}
	{
		// 동작 수를 하나 더 크게 잡으면 소리 없는 5번째 동작이 빈 길로 잡힌다.
		const TArray<FString> FakeGaps = SurveySilentBoatActions(BoatActionCount + 1);
		TestTrue(TEXT("역검증: 소리 없는 동작 한 줄 → 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("배 동작 4")));
	}
	return true;
}

#endif
