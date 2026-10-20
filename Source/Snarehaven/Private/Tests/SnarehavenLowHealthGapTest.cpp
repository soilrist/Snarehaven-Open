// ROADMAP M60-05 2층 단정 — 위험 체력 알림 빈 길 0 (DESIGN 2.2 「위험 체력 알림」).
// ① 표 「심장 소리」 줄의 이름(`SFX_Heartbeat_01~03`)이 반입 변형 3이고, 캐릭터가 부르는 이름도 같은 에셋인가.
// ② 붉은 테두리(`HuntHudWidget`)와 심장 소리(`SnarehavenCharacter`)가 둘 다 `SnareLowHealth::StateFor` 를 거치는가.
// 재생 횟수(위험 → 간격마다 1 · 평소 0)는 `Snarehaven.World.HeartbeatPlay` 시험이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.LowHealthGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const LowHealthDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const LowHealthRowHead = TEXT("| 심장 소리 |");
	const TCHAR* const LowHealthCharacterFile = TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp");
	const TCHAR* const LowHealthHudFile = TEXT("Source/Snarehaven/Private/UI/HuntHudWidget.cpp");

	/** DESIGN 표 「심장 소리」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 줄을 못 찾으면 빈 목록. */
	TArray<FString> ReadLowHealthTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 RowAt = Design.Find(LowHealthRowHead, ESearchCase::CaseSensitive);
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
	TArray<FString> SurveyLowHealthMissingAssets(const TArray<FString>& Names, USnareSoundSubsystem* Sounds)
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
	TArray<FString> SurveyUnwiredLowHealth(const TArray<TPair<FString, FString>>& FileAndNeedle)
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
				Gaps.Add(FString::Printf(TEXT("%s: `%s` 호출 없음 — 위험해도 알림 없음"), *Item.Key, *Item.Value));
			}
		}
		return Gaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLowHealthGapTest, "Snarehaven.World.LowHealthGap.NoSilentCold",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLowHealthGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / LowHealthDesignFile))))
	{
		return false;
	}

	// ① 표 이름 = 반입 변형 3 = 캐릭터가 `Play2D` 로 부르는 이름.
	const TArray<FString> TableNames = ReadLowHealthTableCueNames(Design);
	TestEqual(TEXT("표 「심장 소리」 새 이름 1개 (Heartbeat)"), TableNames.Num(), 1);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	TArray<FString> Gaps = SurveyLowHealthMissingAssets(TableNames, Sounds);
	const FString PlayedCue = TEXT("Heartbeat");
	if (!TableNames.Contains(PlayedCue))
	{
		Gaps.Add(FString::Printf(TEXT("캐릭터가 부르는 %s 이(가) 표 「심장 소리」 이름과 다르다"), *PlayedCue));
	}

	// ② 테두리와 소리가 둘 다 규칙 함수를 거치고, 틱이 연결돼 있다.
	const TArray<TPair<FString, FString>> Wiring = {
		{ LowHealthCharacterFile, TEXT("TickHeartbeatSound(DeltaSeconds)") },
		{ LowHealthCharacterFile, TEXT("SnareLowHealth::StateFor(PlayerVitals->GetHealth()") },
		{ LowHealthCharacterFile, TEXT("USnareSoundSubsystem::Play2D(this, TEXT(\"Heartbeat\"), SnareLowHealth::HeartbeatVolume)") },
		{ LowHealthHudFile, TEXT("SnareLowHealth::StateFor(WatchedVitals->GetHealth()") },
		{ LowHealthHudFile, TEXT("SnareLowHealth::PulsedEdgeOpacity(Strength, Wave)") },
		{ LowHealthHudFile, TEXT("UpdateLowHealthEdge(") },
	};
	Gaps.Append(SurveyUnwiredLowHealth(Wiring));

	AddInfo(FString::Printf(TEXT("[Sound] 위험 체력 알림 길: 표 이름 %d · 내는 이름 %s · 연결 %d · 빈 길 %d"),
		TableNames.Num(), *PlayedCue, Wiring.Num(), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 값으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("LowHealthTestMissing"));
		const TArray<FString> FakeGaps = SurveyLowHealthMissingAssets(WithFake, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 소리 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("LowHealthTestMissing")));
	}
	{
		const TArray<FString> FakeGaps = SurveyUnwiredLowHealth({ { LowHealthHudFile, TEXT("NoSuchLowHealthCall(") } });
		TestTrue(TEXT("역검증: 없는 호출 한 줄 → 연결 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("NoSuchLowHealthCall")));
	}
	return true;
}

#endif
