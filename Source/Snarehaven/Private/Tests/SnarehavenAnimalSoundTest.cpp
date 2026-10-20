// ROADMAP M43-02 2층 검사 — 동물 표 소리 칸 (DESIGN 8.7 「동물 소리」).
// 모든 행의 맞음·죽음 칸이 **실제로 들어온 소리**를 가리키고, 덤빔 칸은 `Flee` 아닌 종만 차 있는가.
// 판단은 칸 이름이 아니라 그 이름으로 소리 변형을 실제로 찾았는지(`CountVariants`)로 한다 (Verification.md).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Animal.Sound; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "World/SnareSound.h"

namespace
{
	const TCHAR* AnimalSoundTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");

	UWorld* CreateSoundTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroySoundTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	// M43-05 — 소리 칸 빈 자리 목록. 맞음·죽음은 모든 행, 덤빔은 `Flee` 아닌 행만 본다.
	// 빈 자리 = 칸 이름이 없거나, 이름은 있는데 실제 소리 변형을 못 찾음(`CountVariants` 0).
	TArray<FString> SurveySoundGaps(const TArray<TPair<FName, FAnimalRow>>& Rows, USnareSoundSubsystem* Sounds)
	{
		TArray<FString> Gaps;
		for (const TPair<FName, FAnimalRow>& Pair : Rows)
		{
			const FAnimalRow& Row = Pair.Value;
			const FString Name = Pair.Key.ToString();
			if (Sounds->CountVariants(Row.HitSoundCue) <= 0)
			{
				Gaps.Add(Name + TEXT(" 맞음"));
			}
			if (Sounds->CountVariants(Row.DeathSoundCue) <= 0)
			{
				Gaps.Add(Name + TEXT(" 죽음"));
			}
			if (Row.BehaviorKind != EAnimalBehaviorKind::Flee && Sounds->CountVariants(Row.AggroSoundCue) <= 0)
			{
				Gaps.Add(Name + TEXT(" 덤빔"));
			}
		}
		return Gaps;
	}

	// 표를 메모리 사본(행 이름 + 행 값)으로 — 역검증은 이 사본만 고치고 에셋은 건드리지 않는다.
	TArray<TPair<FName, FAnimalRow>> CopyAnimalRows(const UDataTable* Table)
	{
		TArray<TPair<FName, FAnimalRow>> Rows;
		for (const FName& RowName : Table->GetRowNames())
		{
			if (const FAnimalRow* Row = Table->FindRow<FAnimalRow>(RowName, TEXT("AnimalSoundGapTest")))
			{
				Rows.Emplace(RowName, *Row);
			}
		}
		return Rows;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalSoundTableTest, "Snarehaven.Animal.Sound.TableColumns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalSoundTableTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalSoundTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), Table))
	{
		return false;
	}
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();

	int32 Checked = 0;
	int32 FleeRows = 0;
	for (const FName& RowName : Table->GetRowNames())
	{
		const FAnimalRow* Row = Table->FindRow<FAnimalRow>(RowName, TEXT("AnimalSoundTableTest"));
		if (Row == nullptr)
		{
			continue;
		}
		const FString Name = RowName.ToString();
		const bool bFlee = Row->BehaviorKind == EAnimalBehaviorKind::Flee;
		const int32 HitCount = Sounds->CountVariants(Row->HitSoundCue);
		const int32 DeathCount = Sounds->CountVariants(Row->DeathSoundCue);
		const int32 AggroCount = Sounds->CountVariants(Row->AggroSoundCue);

		AddInfo(FString::Printf(TEXT("[%s] 맞음 %s(%d) · 죽음 %s(%d) · 덤빔 %s(%d) · 높낮이 %.2f · 도주형 %d"),
			*Name, *Row->HitSoundCue.ToString(), HitCount, *Row->DeathSoundCue.ToString(), DeathCount,
			*Row->AggroSoundCue.ToString(), AggroCount, Row->SoundPitch, bFlee ? 1 : 0));

		TestTrue(*FString::Printf(TEXT("[%s] 맞음 소리가 실제로 있다"), *Name), HitCount > 0);
		TestTrue(*FString::Printf(TEXT("[%s] 죽음 소리가 실제로 있다"), *Name), DeathCount > 0);
		if (bFlee)
		{
			TestTrue(*FString::Printf(TEXT("[%s] 도주형은 덤빔 칸이 비었다"), *Name), Row->AggroSoundCue.IsNone());
			++FleeRows;
		}
		else
		{
			TestTrue(*FString::Printf(TEXT("[%s] 덤비는 종은 덤빔 소리가 실제로 있다"), *Name), AggroCount > 0);
		}
		TestTrue(*FString::Printf(TEXT("[%s] 높낮이가 양수"), *Name), Row->SoundPitch > 0.f);
		++Checked;
	}

	// 표를 못 읽어 0행을 돌고 통과하는 일을 막는다 — 행 수는 표 자체에서 센다(숫자를 박지 않음).
	TestTrue(TEXT("행을 하나 이상 봤다"), Checked > 0);
	TestEqual(TEXT("본 행 수 = 표 행 수"), Checked, Table->GetRowNames().Num());
	TestTrue(TEXT("도주형 행이 있어 빈 덤빔 칸 쪽도 봤다"), FleeRows > 0 && FleeRows < Checked);
	return true;
}

// ROADMAP M43-05 2층 단정 — 소리 칸 빈 행 0. 새 동물 행이 소리 없이 들어오면 여기서 막힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalNoAnimalWithoutSoundTest, "Snarehaven.Animal.Sound.NoAnimalWithoutSound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalNoAnimalWithoutSoundTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, AnimalSoundTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), Table))
	{
		return false;
	}
	USnareSoundSubsystem* const Sounds = NewObject<USnareSoundSubsystem>();
	const TArray<TPair<FName, FAnimalRow>> Rows = CopyAnimalRows(Table);
	TestEqual(TEXT("사본 행 수 = 표 행 수(표를 못 읽어 0행 거짓 통과 막기)"), Rows.Num(), Table->GetRowNames().Num());
	TestTrue(TEXT("행이 하나 이상"), Rows.Num() > 0);

	const TArray<FString> Gaps = SurveySoundGaps(Rows, Sounds);
	for (const FString& Gap : Gaps)
	{
		AddError(FString::Printf(TEXT("동물 소리 빈 칸: %s — create_data_tables.py 동물 소리 묶음 표에 넣는다(DESIGN 8.7)"), *Gap));
	}

	// 역검증: 덤비는 첫 행의 맞음·덤빔, 도주 첫 행의 죽음을 사본에서 비우면 정확히 그 셋이 빈 칸으로 잡힌다.
	// 도주 행 덤빔 칸은 원래 비어 있어 빈 칸이 아니다(덤빔은 `Flee` 아닌 종만).
	int32 AggroIndex = INDEX_NONE;
	int32 FleeIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const bool bFlee = Rows[Index].Value.BehaviorKind == EAnimalBehaviorKind::Flee;
		if (bFlee && FleeIndex == INDEX_NONE)
		{
			FleeIndex = Index;
		}
		else if (!bFlee && AggroIndex == INDEX_NONE)
		{
			AggroIndex = Index;
		}
	}
	if (!TestTrue(TEXT("덤비는 행·도주 행이 다 있다"), AggroIndex != INDEX_NONE && FleeIndex != INDEX_NONE))
	{
		return false;
	}
	TArray<TPair<FName, FAnimalRow>> Broken = Rows;
	Broken[AggroIndex].Value.HitSoundCue = NAME_None;
	Broken[AggroIndex].Value.AggroSoundCue = TEXT("AnimalNoSuchSoundForTest");
	Broken[FleeIndex].Value.DeathSoundCue = NAME_None;
	const TArray<FString> BrokenGaps = SurveySoundGaps(Broken, Sounds);
	const FString AggroName = Rows[AggroIndex].Key.ToString();
	const FString FleeName = Rows[FleeIndex].Key.ToString();
	TArray<FString> Expected = { AggroName + TEXT(" 맞음"), AggroName + TEXT(" 덤빔"), FleeName + TEXT(" 죽음") };
	TArray<FString> SortedBroken = BrokenGaps;
	Expected.Sort();
	SortedBroken.Sort();
	TestEqual(TEXT("역검증: 사본에서 비운 세 칸(없는 이름 포함)이 정확히 잡힌다"), SortedBroken, Expected);

	AddInfo(FString::Printf(TEXT("[SoundGaps] 행 %d 중 빈 칸 %d · 역검증 %s 맞음 비움·덤빔 없는 이름, %s 죽음 비움 → %s"),
		Rows.Num(), Gaps.Num(), *AggroName, *FleeName, *FString::Join(BrokenGaps, TEXT(", "))));
	return true;
}

// ROADMAP M43-03 2층 검사 — 맞음·죽음 소리가 정해진 순간에 정해진 횟수만 나는가 (DESIGN 8.7 「최소 간격」 0.6초).
// 행은 테스트 전용 값으로 직접 만든다 — 표의 묶음·체력 숫자가 바뀌어도 안 깨지게.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalHitDeathVoiceTest, "Snarehaven.Animal.Sound.HitAndDeathVoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalHitDeathVoiceTest::RunTest(const FString& Parameters)
{
	const FName HitCue(TEXT("AnimalBirdHit"));
	const FName DeathCue(TEXT("AnimalBirdDeath"));

	UWorld* World = CreateSoundTestWorld();
	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>();
	AAnimalCharacter* Mute = World->SpawnActor<AAnimalCharacter>();
	if (Animal == nullptr || Mute == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroySoundTestWorld(World);
		return false;
	}

	FAnimalRow Row;
	Row.MaxHP = 10.f;
	Row.CaptureThresholdPct = 0.f;
	Row.HitSoundCue = HitCue;
	Row.DeathSoundCue = DeathCue;
	Animal->ApplySoundFromRow(Row);
	Animal->GetAnimalState()->InitializeFromRow(Row);

	FAnimalRow MuteRow = Row;
	MuteRow.HitSoundCue = NAME_None;
	MuteRow.DeathSoundCue = NAME_None;
	Mute->ApplySoundFromRow(MuteRow);
	Mute->GetAnimalState()->InitializeFromRow(MuteRow);

	// ① 한 번 맞힘 = 울음 한 번. 맞힘 경로(`UMeleeAttackComponent`)와 같이 피해 → 멀티캐스트 순서로 부른다.
	Animal->GetAnimalState()->ApplyDamage(1.f);
	Animal->Multicast_PlayHitReaction(0.f, 0);
	TestEqual(TEXT("맞힘 1번 = 맞음 소리 요청 1"), Animal->GetVoiceRequestCount(HitCue), 1);

	// ② 0.1초 안에 또 맞히면 묶인다(여럿이 같이 때림).
	World->TimeSeconds += 0.1;
	Animal->GetAnimalState()->ApplyDamage(1.f);
	Animal->Multicast_PlayHitReaction(0.f, 0);
	TestEqual(TEXT("0.1초 안 두 번 = 1"), Animal->GetVoiceRequestCount(HitCue), 1);

	// ③ 간격이 지나면 다시 운다 — 막아 둔 채 영영 무음이 되지 않는지.
	World->TimeSeconds += 0.6;
	Animal->GetAnimalState()->ApplyDamage(1.f);
	Animal->Multicast_PlayHitReaction(0.f, 0);
	TestEqual(TEXT("간격 뒤 다시 맞히면 2"), Animal->GetVoiceRequestCount(HitCue), 2);

	// ④ 막타 — 죽음 소리 한 번, 맞음 소리는 더 안 난다(죽는 울음이 이긴다).
	World->TimeSeconds += 1.0;
	Animal->GetAnimalState()->ApplyDamage(999.f);
	Animal->Multicast_PlayHitReaction(0.f, 0);
	TestEqual(TEXT("상태가 죽음이다"), Animal->GetAnimalState()->GetState(), EAnimalState::Dead);
	TestEqual(TEXT("죽음 = 죽음 소리 1"), Animal->GetVoiceRequestCount(DeathCue), 1);
	TestEqual(TEXT("막타에는 맞음 소리가 더 안 난다"), Animal->GetVoiceRequestCount(HitCue), 2);

	// ⑤ 칸이 빈 행은 무음 — 지어낸 기본 소리를 내지 않는다.
	Mute->GetAnimalState()->ApplyDamage(1.f);
	Mute->Multicast_PlayHitReaction(0.f, 0);
	Mute->GetAnimalState()->ApplyDamage(999.f);
	TestEqual(TEXT("빈 칸 동물은 맞음 소리 0"), Mute->GetVoiceRequestCount(HitCue), 0);
	TestEqual(TEXT("빈 칸 동물은 죽음 소리 0"), Mute->GetVoiceRequestCount(DeathCue), 0);
	TestEqual(TEXT("빈 칸 동물은 이름 없는 소리도 0"), Mute->GetVoiceRequestCount(NAME_None), 0);

	DestroySoundTestWorld(World);
	return true;
}

#endif
