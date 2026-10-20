// ROADMAP M56-04 2층 단정 — 숨참 소리 빈 길 0 (DESIGN 3.1 「숨참 소리」).
// ① 표 「소리 파일」 줄의 새 이름(`SFX_Gasp_01~03`)이 반입 변형 3이고, `CueFor` 가 내는 이름도 반입 에셋인가.
// ② `Tick` → `TickGaspSound` → `CueFor` → `Multicast_PlayGasp` → `PlayAt` 로 이어지는가.
// 재생 횟수(바닥 1 · 물 밖 0 · 간격 안 0)는 `Snarehaven.World.GaspPlay` 시험이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.GaspSoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/SnareGasp.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const GaspDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const GaspTableHeader = TEXT("**숨참 소리 (M56-00");
	const TCHAR* const GaspCharacterFile = TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp");

	/** DESIGN 표 「소리 파일」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 표를 못 찾으면 빈 목록. */
	TArray<FString> ReadGaspTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 HeaderAt = Design.Find(GaspTableHeader, ESearchCase::CaseSensitive);
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

	/** 변형 3이 아니면 한 줄씩. */
	TArray<FString> SurveyGaspMissingAssets(const TArray<FString>& Names, USnareSoundSubsystem* Sounds)
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

	/** 소스에 호출이 있는가 — 없으면 한 줄. */
	TArray<FString> SurveyUnwiredGasp(const TArray<TPair<FString, FString>>& FileAndNeedle)
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
				Gaps.Add(FString::Printf(TEXT("%s: `%s` 호출 없음 — 헐떡여도 무음"), *Item.Key, *Item.Value));
			}
		}
		return Gaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGaspSoundGapTest, "Snarehaven.World.GaspSoundGap.NoSilentDrown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGaspSoundGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / GaspDesignFile))))
	{
		return false;
	}

	// ① 표 새 이름 = 반입 변형 3 · 규칙 함수가 내는 이름 = 표 이름 = 반입 에셋.
	const TArray<FString> TableNames = ReadGaspTableCueNames(Design);
	TestEqual(TEXT("표 「소리 파일」 새 이름 1개 (Gasp)"), TableNames.Num(), 1);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	TArray<FString> Gaps = SurveyGaspMissingAssets(TableNames, Sounds);
	const FName PlayedCue = SnareGasp::CueFor(true, 0.f, 100.f).Cue;
	TestFalse(TEXT("바닥난 물속은 소리 이름을 낸다"), PlayedCue.IsNone());
	Gaps.Append(SurveyGaspMissingAssets({ PlayedCue.ToString() }, Sounds));
	if (!TableNames.Contains(PlayedCue.ToString()))
	{
		Gaps.Add(FString::Printf(TEXT("CueFor 가 내는 %s 이(가) 표 「소리 파일」 이름과 다르다"), *PlayedCue.ToString()));
	}

	// ② 틱 → 규칙 함수 → 모든 기기 재생 사슬.
	const TArray<TPair<FString, FString>> Wiring = {
		{ GaspCharacterFile, TEXT("TickGaspSound(DeltaSeconds)") },
		{ GaspCharacterFile, TEXT("SnareGasp::CueFor(PlayerVitals->IsSwimming()") },
		{ GaspCharacterFile, TEXT("Multicast_PlayGasp(Gasp.Volume)") },
		{ GaspCharacterFile, TEXT("USnareSoundSubsystem::PlayAt(this, TEXT(\"Gasp\")") },
	};
	Gaps.Append(SurveyUnwiredGasp(Wiring));

	AddInfo(FString::Printf(TEXT("[Sound] 숨참 소리 길: 표 새 이름 %d · 내는 이름 %s · 연결 %d · 빈 길 %d"),
		TableNames.Num(), *PlayedCue.ToString(), Wiring.Num(), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 값으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("GaspTestMissing"));
		const TArray<FString> FakeGaps = SurveyGaspMissingAssets(WithFake, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 소리 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("GaspTestMissing")));
	}
	{
		const TArray<FString> FakeGaps = SurveyUnwiredGasp({ { GaspCharacterFile, TEXT("NoSuchGaspCue(") } });
		TestTrue(TEXT("역검증: 없는 호출 한 줄 → 연결 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("NoSuchGaspCue")));
	}
	TestTrue(TEXT("역검증: 물 밖은 무음 이름"), SnareGasp::CueFor(false, 0.f, 100.f).Cue.IsNone());
	return true;
}

#endif
