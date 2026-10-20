// ROADMAP M54-04 2층 단정 — 감정표현 말풍선 소리 빈 길 0 (DESIGN 2.2 「말풍선 소리」).
// ① 말풍선 번호 여섯이 전부 소리 이름을 내고, 범위 밖 번호는 무음인가. 번호가 늘었는데 소리를 안 붙였으면(7번째가 소리를 내면) 잡는다.
// ② 표 「소리 파일」 줄의 새 이름(`SFX_<이름>_01~03`)이 전부 반입 변형 3이고, 번호가 내는 이름이 전부 반입 에셋인가.
// ③ 말풍선 함수 소스가 `CueFor` 결과를 실제로 `PlayAt` 으로 부르는가.
// 재생 횟수(소리 1 · 연타 1 · 범위 밖 0)는 `Snarehaven.Ui.EmoteBubble` 시험이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.EmoteSoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/SnareEmoteBubble.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const EmoteSoundDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const EmoteSoundTableHeader = TEXT("**말풍선 소리 (M54-00");

	/** 번호 → 말풍선이 뜰 때 소리 이름. 범위 밖이면 None 이어야 한다. */
	FName CueForEmote(int32 Index)
	{
		return SnareEmoteBubble::CueFor(Index).Cue;
	}

	/** 번호마다 소리 이름이 있는가 — 빈 길 한 줄씩. `EmoteCount` 번째부터는 소리가 없어야 한다(번호가 늘었는데 이 시험이 낡은 경우). */
	TArray<FString> SurveySilentEmotes(int32 EmoteCount)
	{
		TArray<FString> Gaps;
		for (int32 Index = 0; Index < EmoteCount; ++Index)
		{
			if (CueForEmote(Index).IsNone())
			{
				Gaps.Add(FString::Printf(TEXT("말풍선 %d번: 떠도 소리 없음"), Index));
			}
		}
		if (!CueForEmote(EmoteCount).IsNone())
		{
			Gaps.Add(FString::Printf(TEXT("말풍선 %d번 도 소리가 있다 — 번호가 늘었으니 시험의 번호 수를 고친다"), EmoteCount));
		}
		return Gaps;
	}

	/** DESIGN 2.2 표 「소리 파일」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 표를 못 찾으면 빈 목록. */
	TArray<FString> ReadEmoteTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 HeaderAt = Design.Find(EmoteSoundTableHeader, ESearchCase::CaseSensitive);
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
	TArray<FString> SurveyEmoteMissingAssets(const TArray<FString>& Names, int32 MinVariants, USnareSoundSubsystem* Sounds)
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

	/** 말풍선 함수 소스에 재생 호출이 있는가 — 없으면 한 줄. 파일을 못 읽어도 한 줄. */
	TArray<FString> SurveyUnwiredEmotes(const TArray<TPair<FString, FString>>& FileAndNeedle)
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
				Gaps.Add(FString::Printf(TEXT("%s: `%s` 호출 없음 — 떠도 무음"), *Item.Key, *Item.Value));
			}
		}
		return Gaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEmoteSoundGapTest, "Snarehaven.World.EmoteSoundGap.NoSilentBubble",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEmoteSoundGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / EmoteSoundDesignFile))))
	{
		return false;
	}

	// ① 번호 전부 소리 있음.
	const int32 EmoteCount = SnareEmoteBubble::Num();
	TArray<FString> Gaps = SurveySilentEmotes(EmoteCount);

	// ② 표 새 이름 = 반입 변형 3 · 번호가 내는 이름 = 반입 에셋.
	const TArray<FString> TableNames = ReadEmoteTableCueNames(Design);
	TestEqual(TEXT("표 「소리 파일」 새 이름 1개 (EmotePop)"), TableNames.Num(), 1);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	Gaps.Append(SurveyEmoteMissingAssets(TableNames, 3, Sounds));
	TArray<FString> PlayedNames;
	for (int32 Index = 0; Index < EmoteCount; ++Index)
	{
		const FName Cue = CueForEmote(Index);
		if (!Cue.IsNone())
		{
			PlayedNames.AddUnique(Cue.ToString());
		}
	}
	Gaps.Append(SurveyEmoteMissingAssets(PlayedNames, 3, Sounds));

	// ③ 말풍선 함수가 소리를 부른다.
	const TArray<TPair<FString, FString>> Wiring = {
		{ TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp"), TEXT("SnareEmoteBubble::CueFor(Index)") },
		{ TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp"), TEXT("USnareSoundSubsystem::PlayAt(this, Cue.Cue") },
	};
	Gaps.Append(SurveyUnwiredEmotes(Wiring));

	AddInfo(FString::Printf(TEXT("[Sound] 말풍선 소리 길: 번호 %d · 표 새 이름 %d · 내는 이름 %d · 연결 %d · 빈 길 %d"),
		EmoteCount, TableNames.Num(), PlayedNames.Num(), Wiring.Num(), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 값으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("EmoteTestMissing"));
		const TArray<FString> FakeGaps = SurveyEmoteMissingAssets(WithFake, 3, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 소리 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("EmoteTestMissing")));
	}
	{
		// 번호 수를 하나 더 크게 잡으면 소리 없는 7번째 번호가 빈 길로 잡힌다.
		const TArray<FString> FakeGaps = SurveySilentEmotes(EmoteCount + 1);
		TestTrue(TEXT("역검증: 소리 없는 번호 한 줄 → 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(FString::Printf(TEXT("말풍선 %d번"), EmoteCount)));
	}
	{
		const TArray<FString> FakeGaps = SurveyUnwiredEmotes({ { TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp"), TEXT("NoSuchEmoteCue(") } });
		TestTrue(TEXT("역검증: 없는 호출 한 줄 → 연결 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("NoSuchEmoteCue")));
	}
	return true;
}

#endif
