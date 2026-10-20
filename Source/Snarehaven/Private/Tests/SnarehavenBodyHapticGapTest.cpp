// ROADMAP M61-05 2층 단정 — 몸 진동 빈 길 0 (DESIGN 2.2 「몸 진동」).
// ① 진동을 내는 세 일(맞음·쓰러짐·위험 체력 심장)이 모두 `SnareBodyHaptic::EEvent` 로 `PlayBodyHaptic` 을 부르는가.
// ② `PlayBodyHaptic` 이 규칙 함수 `CueFor` 를 거쳐 `SnareHaptics::Play` 로 가는가 (세기·길이를 제멋대로 넣는 길 0).
// ③ (M62-03) 쏘기 진동도 같다: 쏘는 곳은 `PlayShotHaptic` 만 부르고, 그 함수는 `CueFor(Gun, …)` 만 거쳐 `SnareHaptics::Play` 로 간다.
// ④ (M63-04) 타격 진동도 같다: 때리는 곳·채집하는 곳은 `PlayStrikeHaptic` 만 부르고, 그 함수는 `CueFor(Strike, …)` 만 거쳐 `SnareHaptics::Play` 로 간다.
// ⑤ (M64-03) 먹기 진동도 같다: 먹는 곳은 `PlayEatHaptic` 만 부르고, 그 함수는 `CueFor(Eat, …)` 만 거쳐 `SnareHaptics::Play` 로 간다.
// 세기·간격 값 자체는 `Snarehaven.Player.Haptic` 시험이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.BodyHapticGap; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* const ShotHapticMeleeFile = TEXT("Source/Snarehaven/Private/Combat/MeleeAttackComponent.cpp");
	const TCHAR* const StrikeHapticGatherFile = TEXT("Source/Snarehaven/Private/Gathering/GatherComponent.cpp");
	const TCHAR* const BodyHapticCharacterFile = TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp");

	/** 소스에 호출이 있는가 — 없으면 한 줄. */
	TArray<FString> SurveyUnwiredBodyHaptic(const TArray<TPair<FString, FString>>& FileAndNeedle)
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
				Gaps.Add(FString::Printf(TEXT("%s: `%s` 호출 없음 — 몸 진동 빈 길"), *Item.Key, *Item.Value));
			}
		}
		return Gaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBodyHapticGapTest, "Snarehaven.World.BodyHapticGap.NoUnruledPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBodyHapticGapTest::RunTest(const FString& Parameters)
{
	const TArray<TPair<FString, FString>> Wiring = {
		// ① 세 일이 모두 같은 함수로 간다.
		{ BodyHapticCharacterFile, TEXT("PlayBodyHaptic(SnareBodyHaptic::EEvent::Hurt)") },
		{ BodyHapticCharacterFile, TEXT("PlayBodyHaptic(SnareBodyHaptic::EEvent::Down)") },
		{ BodyHapticCharacterFile, TEXT("PlayBodyHaptic(SnareBodyHaptic::EEvent::Heartbeat)") },
		// ② 그 함수는 규칙 함수의 세기·길이만 쓴다.
		{ BodyHapticCharacterFile, TEXT("SnareBodyHaptic::CueFor(Event, SinceLast)") },
		{ BodyHapticCharacterFile, TEXT("SnareHaptics::Play(Cast<APlayerController>(GetController()), Cue.Intensity, Cue.Seconds)") },
		{ BodyHapticCharacterFile, TEXT("if (!IsLocallyControlled()") },
		// ③ 쏘기 진동: 쏘는 곳 → PlayShotHaptic → 규칙 함수의 세기·길이만.
		{ ShotHapticMeleeFile, TEXT("Shooter->PlayShotHaptic(Kind)") },
		{ BodyHapticCharacterFile, TEXT("SnareBodyHaptic::CueFor(Gun, SinceLast)") },
		// ④ 타격 진동: 때림·채집 → PlayStrikeHaptic → 규칙 함수의 세기·길이만.
		{ ShotHapticMeleeFile, TEXT("Striker->PlayStrikeHaptic(SnareBodyHaptic::EStrike::Melee)") },
		{ StrikeHapticGatherFile, TEXT("Striker->PlayStrikeHaptic(SnareBodyHaptic::EStrike::Gather)") },
		{ BodyHapticCharacterFile, TEXT("SnareBodyHaptic::CueFor(Strike, SinceLast)") },
		// ⑤ 먹기 진동: 먹는 곳 → PlayEatHaptic → 규칙 함수의 세기·길이만.
		{ BodyHapticCharacterFile, TEXT("PlayEatHaptic(Heal < 0.f ? SnareBodyHaptic::EEat::Poison : SnareBodyHaptic::EEat::Food)") },
		{ BodyHapticCharacterFile, TEXT("SnareBodyHaptic::CueFor(Eat, SinceLast)") },
	};
	const TArray<FString> Gaps = SurveyUnwiredBodyHaptic(Wiring);

	AddInfo(FString::Printf(TEXT("[Haptics] 몸 진동 길: 연결 %d · 빈 길 %d"), Wiring.Num(), Gaps.Num()));
	for (const FString& Gap : Gaps)
	{
		AddError(Gap);
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 가짜 값으로 거름망이 살아 있는지 본다.
	{
		const TArray<FString> FakeGaps = SurveyUnwiredBodyHaptic({ { BodyHapticCharacterFile, TEXT("NoSuchBodyHapticCall(") } });
		TestTrue(TEXT("역검증: 없는 호출 한 줄 → 빈 길 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("NoSuchBodyHapticCall")));
	}
	{
		const TArray<FString> FakeGaps = SurveyUnwiredBodyHaptic({ { TEXT("Source/NoSuchFile.cpp"), TEXT("x") } });
		TestTrue(TEXT("역검증: 없는 파일 → 못 읽음 1개 잡음"), FakeGaps.Num() == 1 && FakeGaps[0].Contains(TEXT("못 읽음")));
	}
	return true;
}

#endif
