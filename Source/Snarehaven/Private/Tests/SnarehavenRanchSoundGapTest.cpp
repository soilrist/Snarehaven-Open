// ROADMAP M50-06 2층 단정 — 목장 소리 빈 길 0 (DESIGN 3.2 「목장 소리」).
// ① 목장 서버 답(`URanchSyncComponent::HandleServerReply`)으로 오는 함수 이름 전부 — 칸 요청(`GetFunctionName` 의
//    정거장 × 요청 종류 × 칸 종류) + 유지·칸 늘리기·데코 — 가 「소리 있음」 셋 중 하나인가:
//    수확(`IsHarvestFunction` → 코인) · 심기(`PlantCrop` → 설치 「쿵」) · `SnareSound::RanchCue` 이름 있음.
// ② 표 「소리 파일」 줄의 새 이름(`SFX_<이름>_01~03`)이 전부 반입 변형 3이고, `RanchCue`·수확이 내는 이름이 전부 반입 에셋인가.
// 재생 자체(받아들임 1 · 거절 0)는 `Snarehaven.RanchSync.Sound.*` 가 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.RanchSync.SoundGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Building/SnareBuildRules.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchSyncComponent.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* const RanchSoundDesignFile = TEXT("docs/DESIGN.md");
	const TCHAR* const RanchSoundTableHeader = TEXT("**목장 소리 (M50-00");

	/** 목장 서버 답으로 오는 함수 이름 전부 — 칸 요청 조합 + 칸 밖 호출 3. 중복은 한 번. */
	TArray<FString> CollectRanchFunctions()
	{
		TArray<FString> Names;
		const ERanchStationKind Stations[] = { ERanchStationKind::Ranch, ERanchStationKind::Fishing };
		const ERanchRequestKind Kinds[] = { ERanchRequestKind::RegisterPet, ERanchRequestKind::Harvest,
			ERanchRequestKind::PlantCrop, ERanchRequestKind::RemovePet, ERanchRequestKind::SetCosmetic };
		const ERanchSlotKind Slots[] = { ERanchSlotKind::AnimalProduct, ERanchSlotKind::Crop, ERanchSlotKind::Fishing };
		for (const ERanchStationKind Station : Stations)
		{
			for (const ERanchRequestKind Kind : Kinds)
			{
				for (const ERanchSlotKind Slot : Slots)
				{
					Names.AddUnique(URanchSyncComponent::GetFunctionName(Station, Kind, Slot));
				}
			}
		}
		Names.AddUnique(URanchSyncComponent::GetFeedUpkeepFunctionName());
		Names.AddUnique(URanchSyncComponent::GetExpandSlotFunctionName());
		Names.AddUnique(URanchSyncComponent::GetSetRanchDecoFunctionName());
		return Names;
	}

	/** 받아들인 답에서 그 함수가 내는 소리 이름 — 셋 다 아니면 None(빈 길). */
	FName CueForFunction(const FString& Function)
	{
		if (URanchSyncComponent::IsHarvestFunction(Function))
		{
			return URanchSyncComponent::GetHarvestSoundCue();
		}
		if (Function == TEXT("PlantCrop"))
		{
			return SnareBuildRules::PlaceConfirmCue();
		}
		return SnareSound::RanchCue(Function, true, FString()).Cue;
	}

	/** 함수마다 소리 이름이 있는가 — 빈 길 한 줄씩. */
	TArray<FString> SurveySilentFunctions(const TArray<FString>& Functions)
	{
		TArray<FString> Gaps;
		for (const FString& Function : Functions)
		{
			if (CueForFunction(Function).IsNone())
			{
				Gaps.Add(FString::Printf(TEXT("%s: 받아들여도 소리 없음 (수확·심기·RanchCue 어디에도 없음)"), *Function));
			}
		}
		return Gaps;
	}

	/** DESIGN 3.2 표 「소리 파일」 줄에서 `SFX_<이름>_01~03` 이름들을 꺼낸다. 표를 못 찾으면 빈 목록. */
	TArray<FString> ReadRanchTableCueNames(const FString& Design)
	{
		TArray<FString> Names;
		const int32 HeaderAt = Design.Find(RanchSoundTableHeader, ESearchCase::CaseSensitive);
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
	TArray<FString> SurveyRanchMissingAssets(const TArray<FString>& Names, int32 MinVariants, USnareSoundSubsystem* Sounds)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSoundGapTest, "Snarehaven.RanchSync.SoundGap.NoSilentRanchPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSoundGapTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN 을 읽는다"), FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / RanchSoundDesignFile))))
	{
		return false;
	}

	// ① 함수 이름 전부 소리 있음. 수가 줄면 모으는 길이 낡은 것 — 거짓 통과를 막는다.
	const TArray<FString> Functions = CollectRanchFunctions();
	TestEqual(TEXT("목장 서버 함수 11개 (넣기·빼기·꾸미기·심기·수확 3·낚시터·유지·칸 늘리기·데코)"), Functions.Num(), 11);
	TArray<FString> Gaps = SurveySilentFunctions(Functions);

	// ② 표 새 이름 = 반입 변형 3 · 함수가 내는 이름(빌림 포함) = 반입 에셋.
	const TArray<FString> TableNames = ReadRanchTableCueNames(Design);
	TestEqual(TEXT("표 「소리 파일」 새 이름 3개 (RanchWelcome·RanchRelease·RanchExpand)"), TableNames.Num(), 3);
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	Gaps.Append(SurveyRanchMissingAssets(TableNames, 3, Sounds));
	TArray<FString> PlayedNames;
	for (const FString& Function : Functions)
	{
		const FName Cue = CueForFunction(Function);
		if (!Cue.IsNone())
		{
			PlayedNames.AddUnique(Cue.ToString());
		}
	}
	Gaps.Append(SurveyRanchMissingAssets(PlayedNames, 1, Sounds));

	AddInfo(FString::Printf(TEXT("[Sound] 목장 소리 길: 함수 %d · 표 새 이름 %d · 내는 이름 %d · 빈 길 %d"),
		Functions.Num(), TableNames.Num(), PlayedNames.Num(), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 이름으로 거름망이 살아 있는지 본다.
	{
		TArray<FString> WithFake = TableNames;
		WithFake.Add(TEXT("RanchTestMissing"));
		const TArray<FString> FakeGaps = SurveyRanchMissingAssets(WithFake, 3, Sounds);
		TestTrue(TEXT("역검증: 반입 안 된 소리 이름 한 줄 → 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("RanchTestMissing")));
	}
	{
		TArray<FString> WithFake = Functions;
		WithFake.Add(TEXT("RanchTestUnknown"));
		const TArray<FString> FakeGaps = SurveySilentFunctions(WithFake);
		TestTrue(TEXT("역검증: 표에 없는 함수 한 줄 → 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("RanchTestUnknown")));
	}
	return true;
}

#endif
