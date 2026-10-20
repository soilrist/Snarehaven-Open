// ROADMAP M49-06 2층 단정 — 가방·상자 소리 빈 길 0 (DESIGN 8.12 「가방·상자 소리」).
// ① 표 「소리 파일」 줄의 이름(`SFX_<이름>_01~03`)이 전부 **실제로 들어온 소리 에셋**(`CountVariants` = 3)이고,
//    네 행동(버리기·죽음 떨굼·줍기·상자 열기)의 `SnareSound::BagCue` 이름이 그 표 안에 있는가.
// ② 네 서버 호출(`DropItem`·`DropOnDeath`·`PickupDrop`·`OpenChest`)의 응답 람다 안에 그 행동의 `PlayBagCue(` 가 있는가.
// 재생 자체(받아들임 1 · 거절 0)는 `Snarehaven.Backpack.Sound.*`·`Snarehaven.Chest.Sound.*` 가 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Backpack.SoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const BagSoundDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const BagSoundBackpackFile = TEXT("Source/Snarehaven/Private/Inventory/BackpackComponent.cpp");
	const TCHAR* const BagSoundTableHeader = TEXT("**가방·상자 소리 (M49-00");

	/** 행동 하나 = 서버 호출 이름 + 그 람다 안에 있어야 할 재생 줄. */
	struct FBagSoundPath
	{
		const TCHAR* Label;
		SnareSound::EBagAction Action;
		const TCHAR* Signature;
		const TCHAR* ServerCall;
		const TCHAR* SoundCall;
	};

	const FBagSoundPath BagSoundPaths[] = {
		{ TEXT("버리기"), SnareSound::EBagAction::Drop, TEXT("bool UBackpackComponent::RequestDrop("),
			TEXT("CallServerFunction(TEXT(\"DropItem\")"), TEXT("PlayBagCue(SnareSound::EBagAction::Drop,") },
		{ TEXT("죽음 떨굼"), SnareSound::EBagAction::DeathDrop, TEXT("bool UBackpackComponent::RequestDropOnDeath("),
			TEXT("CallServerFunction(TEXT(\"DropOnDeath\")"), TEXT("PlayBagCue(SnareSound::EBagAction::DeathDrop,") },
		{ TEXT("줍기"), SnareSound::EBagAction::Pickup, TEXT("bool UBackpackComponent::RequestPickup("),
			TEXT("CallServerFunction(TEXT(\"PickupDrop\")"), TEXT("PlayBagCue(SnareSound::EBagAction::Pickup,") },
		{ TEXT("상자 열기"), SnareSound::EBagAction::OpenChest, TEXT("bool UBackpackComponent::RequestOpenChest("),
			TEXT("CallServerFunction(TEXT(\"OpenChest\")"), TEXT("PlayBagCue(SnareSound::EBagAction::OpenChest,") },
	};

	/** 여는 중괄호부터 짝 맞는 닫는 중괄호까지 (`SnarehavenBaseSoundPathTest.cpp` 와 같은 방식). */
	FString ExtractBagFunctionBody(const FString& Text, const TCHAR* Signature)
	{
		const int32 SignatureAt = Text.Find(Signature, ESearchCase::CaseSensitive);
		const int32 OpenAt = SignatureAt == INDEX_NONE ? INDEX_NONE
			: Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SignatureAt);
		if (OpenAt == INDEX_NONE)
		{
			return FString();
		}
		int32 Depth = 0;
		for (int32 At = OpenAt; At < Text.Len(); ++At)
		{
			if (Text[At] == TEXT('{'))
			{
				++Depth;
			}
			else if (Text[At] == TEXT('}') && --Depth == 0)
			{
				return Text.Mid(OpenAt, At - OpenAt + 1);
			}
		}
		return FString();
	}

	/** DESIGN 8.12 표 「소리 파일」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 표를 못 찾으면 빈 목록. */
	TArray<FString> ReadTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 HeaderAt = Design.Find(BagSoundTableHeader, ESearchCase::CaseSensitive);
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

	/** 표 이름 중 반입 변형이 3개가 아닌 것 — 빈 자리 한 줄씩. */
	TArray<FString> SurveyMissingAssets(const TArray<FString>& Names, USnareSoundSubsystem* Sounds)
	{
		TArray<FString> Gaps;
		for (const FString& Name : Names)
		{
			const int32 Variants = Sounds->CountVariants(FName(*Name));
			if (Variants != 3)
			{
				Gaps.Add(FString::Printf(TEXT("표 소리 %s: 반입 변형 %d개 (3개여야)"), *Name, Variants));
			}
		}
		return Gaps;
	}

	/** 행동마다 서버 호출 뒤(응답 람다 안)에 재생 줄이 있는가 — 빈 길 한 줄씩. */
	TArray<FString> SurveySilentPaths(const FString& Source)
	{
		TArray<FString> Gaps;
		for (const FBagSoundPath& Path : BagSoundPaths)
		{
			const FString Body = ExtractBagFunctionBody(Source, Path.Signature);
			const int32 CallAt = Body.Find(Path.ServerCall, ESearchCase::CaseSensitive);
			if (CallAt == INDEX_NONE)
			{
				Gaps.Add(FString::Printf(TEXT("%s: 서버 호출 '%s' 없음"), Path.Label, Path.ServerCall));
				continue;
			}
			if (Body.Find(Path.SoundCall, ESearchCase::CaseSensitive, ESearchDir::FromStart, CallAt) == INDEX_NONE)
			{
				Gaps.Add(FString::Printf(TEXT("%s: 응답 람다에 '%s' 없음"), Path.Label, Path.SoundCall));
			}
		}
		return Gaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBagSoundGapTest, "Snarehaven.Backpack.SoundGap.NoSilentBagPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBagSoundGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	FString Source;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / BagSoundDesignFile)))
		|| !TestTrue(TEXT("배낭 소스를 읽는다"), FFileHelper::LoadFileToString(Source, *(FPaths::ProjectDir() / BagSoundBackpackFile))))
	{
		return false;
	}

	// ① 표 이름 = 반입 에셋. 표 이름 수가 줄면 찾기 글자가 낡은 것 — 거짓 통과를 막는다.
	const TArray<FString> TableNames = ReadTableCueNames(Design);
	TestEqual(TEXT("표 「소리 파일」 이름 3개 (BagDrop·BagPickup·ChestOpen)"), TableNames.Num(), 3);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	TArray<FString> Gaps = SurveyMissingAssets(TableNames, Sounds);

	for (const FBagSoundPath& Path : BagSoundPaths)
	{
		const FName Cue = SnareSound::BagCue(Path.Action, true, FString()).Cue;
		if (Cue.IsNone() || !TableNames.Contains(Cue.ToString()))
		{
			Gaps.Add(FString::Printf(TEXT("%s: BagCue 이름 '%s' 가 표에 없음"), Path.Label, *Cue.ToString()));
		}
	}

	// ② 네 행동의 응답 람다마다 재생 줄.
	Gaps.Append(SurveySilentPaths(Source));

	AddInfo(FString::Printf(TEXT("[Sound] 가방·상자 소리 길: 표 이름 %d · 행동 %d · 빈 길 %d"),
		TableNames.Num(), UE_ARRAY_COUNT(BagSoundPaths), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 메모리 사본·가짜 이름으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("BagTestMissing"));
		const TArray<FString> FakeGaps = SurveyMissingAssets(WithFake, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("BagTestMissing")));
	}
	{
		const FString NoPickupSound = Source.Replace(TEXT("PlayBagCue(SnareSound::EBagAction::Pickup,"), TEXT("Muted("));
		const TArray<FString> CopyGaps = SurveySilentPaths(NoPickupSound);
		TestTrue(TEXT("역검증: 줍기 재생 줄을 지운 사본은 줍기 길이 빈다"), CopyGaps.Num() == 1 && CopyGaps[0].Contains(TEXT("줍기")));
	}
	return true;
}

#endif
