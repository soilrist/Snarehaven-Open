// ROADMAP M57-04 2층 단정 — 추위 떨림 소리 빈 길 0 (DESIGN 2.2 「추위 떨림 소리」).
// ① 표 「소리 파일」 줄의 새 이름(`SFX_Shiver_01~03`)이 반입 변형 3이고, `CueFor` 가 내는 이름도 반입 에셋인가.
// ② `Tick` → `TickShiverSound` → `CueFor` → `Multicast_PlayShiver` → `PlayAt` 로 이어지는가.
// 재생 횟수(깎임 1 · 칸 밖 0 · 막이 0 · 간격 안 0)는 `Snarehaven.World.ShiverPlay` 시험이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.ShiverSoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/SnareShiver.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const ShiverDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const ShiverTableHeader = TEXT("**추위 떨림 소리 (M57-00");
	const TCHAR* const ShiverCharacterFile = TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp");

	/** DESIGN 표 「소리 파일」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 표를 못 찾으면 빈 목록. */
	TArray<FString> ReadShiverTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 HeaderAt = Design.Find(ShiverTableHeader, ESearchCase::CaseSensitive);
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
	TArray<FString> SurveyShiverMissingAssets(const TArray<FString>& Names, USnareSoundSubsystem* Sounds)
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
	TArray<FString> SurveyUnwiredShiver(const TArray<TPair<FString, FString>>& FileAndNeedle)
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
				Gaps.Add(FString::Printf(TEXT("%s: `%s` 호출 없음 — 떨려도 무음"), *Item.Key, *Item.Value));
			}
		}
		return Gaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShiverSoundGapTest, "Snarehaven.World.ShiverSoundGap.NoSilentCold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShiverSoundGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / ShiverDesignFile))))
	{
		return false;
	}

	// ① 표 새 이름 = 반입 변형 3 · 규칙 함수가 내는 이름 = 표 이름 = 반입 에셋.
	const TArray<FString> TableNames = ReadShiverTableCueNames(Design);
	TestEqual(TEXT("표 「소리 파일」 새 이름 1개 (Shiver)"), TableNames.Num(), 1);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	TArray<FString> Gaps = SurveyShiverMissingAssets(TableNames, Sounds);
	const FName PlayedCue = SnareShiver::CueFor(2.f, 0.f, 100.f).Cue;
	TestFalse(TEXT("설원 칸 피해가 있으면 소리 이름을 낸다"), PlayedCue.IsNone());
	Gaps.Append(SurveyShiverMissingAssets({ PlayedCue.ToString() }, Sounds));
	if (!TableNames.Contains(PlayedCue.ToString()))
	{
		Gaps.Add(FString::Printf(TEXT("CueFor 가 내는 %s 이(가) 표 「소리 파일」 이름과 다르다"), *PlayedCue.ToString()));
	}

	// ② 틱 → 규칙 함수 → 모든 기기 재생 사슬.
	const TArray<TPair<FString, FString>> Wiring = {
		{ ShiverCharacterFile, TEXT("TickShiverSound(DeltaSeconds)") },
		{ ShiverCharacterFile, TEXT("SnareShiver::CueFor(PlayerVitals->GetZoneHealthDrain()") },
		{ ShiverCharacterFile, TEXT("Multicast_PlayShiver(Shiver.Volume)") },
		{ ShiverCharacterFile, TEXT("USnareSoundSubsystem::PlayAt(this, TEXT(\"Shiver\")") },
	};
	Gaps.Append(SurveyUnwiredShiver(Wiring));

	AddInfo(FString::Printf(TEXT("[Sound] 추위 떨림 소리 길: 표 새 이름 %d · 내는 이름 %s · 연결 %d · 빈 길 %d"),
		TableNames.Num(), *PlayedCue.ToString(), Wiring.Num(), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 값으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("ShiverTestMissing"));
		const TArray<FString> FakeGaps = SurveyShiverMissingAssets(WithFake, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 소리 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("ShiverTestMissing")));
	}
	{
		const TArray<FString> FakeGaps = SurveyUnwiredShiver({ { ShiverCharacterFile, TEXT("NoSuchShiverCue(") } });
		TestTrue(TEXT("역검증: 없는 호출 한 줄 → 연결 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("NoSuchShiverCue")));
	}
	TestTrue(TEXT("역검증: 칸 밖(피해 0)은 무음 이름"), SnareShiver::CueFor(0.f, 0.f, 100.f).Cue.IsNone());
	TestTrue(TEXT("역검증: 추위 막이 100% 도 무음 이름"), SnareShiver::CueFor(2.f, 1.f, 100.f).Cue.IsNone());
	return true;
}

#endif
