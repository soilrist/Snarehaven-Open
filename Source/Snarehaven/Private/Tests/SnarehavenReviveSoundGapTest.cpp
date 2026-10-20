// ROADMAP M59-04 2층 단정 — 부활 소리 빈 길 0 (DESIGN 2.2 「부활 소리」).
// ① 표 「소리 파일」 줄의 새 이름(`SFX_Revive_01~03`)이 반입 변형 3이고, `CueFor` 가 내는 이름도 반입 에셋인가.
// ② `Server_RespawnAtPlayerStart` → `PlayReviveSound` → `CueFor` → `Multicast_PlayReviveSound` → `PlayAt` 로 이어지는가.
// 재생 횟수(쓰러진 뒤 1 · 평소 0)는 `Snarehaven.World.RevivePlay` 시험이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.ReviveSoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/SnareRevive.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const ReviveDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const ReviveTableHeader = TEXT("**부활 소리 (M59-00");
	const TCHAR* const ReviveCharacterFile = TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp");

	/** DESIGN 표 「소리 파일」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 표를 못 찾으면 빈 목록. */
	TArray<FString> ReadReviveTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 HeaderAt = Design.Find(ReviveTableHeader, ESearchCase::CaseSensitive);
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
	TArray<FString> SurveyReviveMissingAssets(const TArray<FString>& Names, USnareSoundSubsystem* Sounds)
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
	TArray<FString> SurveyUnwiredRevive(const TArray<TPair<FString, FString>>& FileAndNeedle)
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
				Gaps.Add(FString::Printf(TEXT("%s: `%s` 호출 없음 — 부활해도 무음"), *Item.Key, *Item.Value));
			}
		}
		return Gaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenReviveSoundGapTest, "Snarehaven.World.ReviveSoundGap.NoSilentCold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenReviveSoundGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / ReviveDesignFile))))
	{
		return false;
	}

	// ① 표 새 이름 = 반입 변형 3 · 규칙 함수가 내는 이름 = 표 이름 = 반입 에셋.
	const TArray<FString> TableNames = ReadReviveTableCueNames(Design);
	TestEqual(TEXT("표 「소리 파일」 새 이름 1개 (Revive)"), TableNames.Num(), 1);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	TArray<FString> Gaps = SurveyReviveMissingAssets(TableNames, Sounds);
	const FName PlayedCue = SnareRevive::CueFor(true).Cue;
	TestFalse(TEXT("쓰러졌다 일어나면 소리 이름을 낸다"), PlayedCue.IsNone());
	Gaps.Append(SurveyReviveMissingAssets({ PlayedCue.ToString() }, Sounds));
	if (!TableNames.Contains(PlayedCue.ToString()))
	{
		Gaps.Add(FString::Printf(TEXT("CueFor 가 내는 %s 이(가) 표 「소리 파일」 이름과 다르다"), *PlayedCue.ToString()));
	}

	// ② 틱 → 규칙 함수 → 모든 기기 재생 사슬.
	const TArray<TPair<FString, FString>> Wiring = {
		{ ReviveCharacterFile, TEXT("PlayReviveSound(true)") },
		{ ReviveCharacterFile, TEXT("SnareRevive::CueFor(bWasDowned)") },
		{ ReviveCharacterFile, TEXT("Multicast_PlayReviveSound(Revive.Cue, GetActorLocation(), Revive.Volume)") },
		{ ReviveCharacterFile, TEXT("USnareSoundSubsystem::PlayAt(this, Cue, Location, Volume)") },
	};
	Gaps.Append(SurveyUnwiredRevive(Wiring));

	AddInfo(FString::Printf(TEXT("[Sound] 부활 소리 길: 표 새 이름 %d · 내는 이름 %s · 연결 %d · 빈 길 %d"),
		TableNames.Num(), *PlayedCue.ToString(), Wiring.Num(), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 값으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("ReviveTestMissing"));
		const TArray<FString> FakeGaps = SurveyReviveMissingAssets(WithFake, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 소리 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("ReviveTestMissing")));
	}
	{
		const TArray<FString> FakeGaps = SurveyUnwiredRevive({ { ReviveCharacterFile, TEXT("NoSuchReviveCue(") } });
		TestTrue(TEXT("역검증: 없는 호출 한 줄 → 연결 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("NoSuchReviveCue")));
	}
	TestTrue(TEXT("역검증: 쓰러진 적 없으면(평소) 무음 이름"), SnareRevive::CueFor(false).Cue.IsNone());
	return true;
}

#endif
