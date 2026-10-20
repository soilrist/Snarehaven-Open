// ROADMAP M55-04 2층 단정 — 막힘 소리 빈 길 0 (DESIGN 2.2 「막힘 소리」).
// ① 표 「소리 파일」 줄의 새 이름(`SFX_Deny_01~03`)이 반입 변형 3이고, `DenyCue` 가 내는 이름도 반입 에셋인가.
// ② 오류 줄 글자는 `ShowErrorBanner` 한 곳에서만 정해지는가 — 다른 소스가 `ErrorBannerText` 에 글자를 직접 넣으면(소리 없이 줄만 뜨면) 잡는다.
// ③ `ShowErrorBanner` → `PlayDenySound` → `DenyCue` → `Play2D` 로 이어지는가.
// 재생 횟수(유효 1 · 빈 문구 0 · 연타 1)는 `Snarehaven.HuntHud.ErrorPill.DenySound` 시험이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.DenySoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const DenySoundDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const DenySoundTableHeader = TEXT("**막힘 소리 (M55-00");
	const TCHAR* const DenyHudFile = TEXT("Source/Snarehaven/Private/UI/HuntHudWidget.cpp");

	/** 글자를 직접 넣는 대입 — 비우는 대입(`= FText::GetEmpty()`)은 뺀다. */
	const TCHAR* const DenyTextAssignPattern = TEXT("ErrorBannerText\\s*=(?![=\\s]*FText::GetEmpty)");

	/** DESIGN 2.2 표 「소리 파일」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 표를 못 찾으면 빈 목록. */
	TArray<FString> ReadDenyTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 HeaderAt = Design.Find(DenySoundTableHeader, ESearchCase::CaseSensitive);
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
	TArray<FString> SurveyDenyMissingAssets(const TArray<FString>& Names, USnareSoundSubsystem* Sounds)
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

	int32 CountDenyTextAssigns(const FString& Source)
	{
		int32 Count = 0;
		FRegexMatcher Matcher(FRegexPattern(DenyTextAssignPattern), Source);
		while (Matcher.FindNext())
		{
			++Count;
		}
		return Count;
	}

	/** 오류 줄 글자를 직접 넣는 소스 — `ShowErrorBanner` 가 든 한 파일 밖에 있으면 한 줄씩. 검사 스위치(-SnareErrorDemo)의 한 줄은 `AllowedInHud` 로 센다. */
	TArray<FString> SurveyBypassBanners(const FString& SourceRoot, const FString& HudFile, int32 AllowedInHud, int32& OutFiles)
	{
		TArray<FString> Gaps;
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *SourceRoot, TEXT("*.cpp"), true, false);
		OutFiles = Files.Num();
		for (const FString& File : Files)
		{
			if (File.Contains(TEXT("/Tests/")))
			{
				continue;
			}
			FString Source;
			if (!FFileHelper::LoadFileToString(Source, *File))
			{
				Gaps.Add(FString::Printf(TEXT("%s: 소스를 못 읽음"), *File));
				continue;
			}
			const int32 Assigns = CountDenyTextAssigns(Source);
			const int32 Allowed = File.EndsWith(HudFile) ? AllowedInHud : 0;
			if (Assigns != Allowed)
			{
				Gaps.Add(FString::Printf(TEXT("%s: 오류 줄 글자 대입 %d곳 (%d곳이어야) — 소리 없이 줄만 뜬다"), *File, Assigns, Allowed));
			}
		}
		return Gaps;
	}

	/** 소스에 재생 호출이 있는가 — 없으면 한 줄. */
	TArray<FString> SurveyUnwiredDeny(const TArray<TPair<FString, FString>>& FileAndNeedle)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDenySoundGapTest, "Snarehaven.World.DenySoundGap.NoSilentBanner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDenySoundGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / DenySoundDesignFile))))
	{
		return false;
	}

	// ① 표 새 이름 = 반입 변형 3 · 규칙 함수가 내는 이름 = 반입 에셋.
	const TArray<FString> TableNames = ReadDenyTableCueNames(Design);
	TestEqual(TEXT("표 「소리 파일」 새 이름 1개 (Deny)"), TableNames.Num(), 1);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	TArray<FString> Gaps = SurveyDenyMissingAssets(TableNames, Sounds);
	const FName PlayedCue = SnareSound::DenyCue(false, 100.f).Cue;
	TestFalse(TEXT("유효 문구는 소리 이름을 낸다"), PlayedCue.IsNone());
	Gaps.Append(SurveyDenyMissingAssets({ PlayedCue.ToString() }, Sounds));
	if (!TableNames.Contains(PlayedCue.ToString()))
	{
		Gaps.Add(FString::Printf(TEXT("DenyCue 가 내는 %s 이(가) 표 「소리 파일」 이름과 다르다"), *PlayedCue.ToString()));
	}

	// ② 오류 줄 글자는 ShowErrorBanner 한 곳 (+ 검사 스위치 -SnareErrorDemo 한 줄, 소리 없음을 이름으로 허용).
	int32 ScannedFiles = 0;
	const FString SourceRoot = FPaths::ProjectDir() / TEXT("Source/Snarehaven/Private");
	Gaps.Append(SurveyBypassBanners(SourceRoot, TEXT("UI/HuntHudWidget.cpp"), 2, ScannedFiles));

	// ③ 오류 줄 → 소리 사슬.
	const TArray<TPair<FString, FString>> Wiring = {
		{ DenyHudFile, TEXT("PlayDenySound(Message.IsEmpty(), FPlatformTime::Seconds())") },
		{ DenyHudFile, TEXT("SnareSound::DenyCue(bMessageEmpty") },
		{ DenyHudFile, TEXT("USnareSoundSubsystem::Play2D(this, Cue.Cue, Cue.Volume)") },
	};
	Gaps.Append(SurveyUnwiredDeny(Wiring));

	AddInfo(FString::Printf(TEXT("[Sound] 막힘 소리 길: 표 새 이름 %d · 내는 이름 %s · 훑은 소스 %d · 연결 %d · 빈 길 %d"),
		TableNames.Num(), *PlayedCue.ToString(), ScannedFiles, Wiring.Num(), Gaps.Num()));
	TestTrue(TEXT("소스를 훑었다"), ScannedFiles > 50);
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 값으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("DenyTestMissing"));
		const TArray<FString> FakeGaps = SurveyDenyMissingAssets(WithFake, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 소리 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("DenyTestMissing")));
	}
	{
		// 허용 수를 하나 줄이면 HuntHudWidget.cpp 의 대입이 우회로 잡힌다.
		int32 Unused = 0;
		const TArray<FString> FakeGaps = SurveyBypassBanners(SourceRoot, TEXT("UI/HuntHudWidget.cpp"), 1, Unused);
		TestTrue(TEXT("역검증: 허용보다 많은 글자 대입 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("HuntHudWidget.cpp")));
	}
	{
		TestEqual(TEXT("역검증: 글자 대입 한 줄은 1곳"), CountDenyTextAssigns(TEXT("ErrorBannerText = Message;")), 1);
		TestEqual(TEXT("역검증: 비우는 대입은 0곳"), CountDenyTextAssigns(TEXT("ErrorBannerText = FText::GetEmpty();")), 0);
	}
	{
		const TArray<FString> FakeGaps = SurveyUnwiredDeny({ { DenyHudFile, TEXT("NoSuchDenyCue(") } });
		TestTrue(TEXT("역검증: 없는 호출 한 줄 → 연결 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("NoSuchDenyCue")));
	}
	return true;
}

#endif
