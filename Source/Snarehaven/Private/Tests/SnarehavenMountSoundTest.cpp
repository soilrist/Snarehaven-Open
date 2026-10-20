// ROADMAP M47-04 2층 검사 — 탈것에 타고 내릴 때 소리 요청이 **실제로 바뀐 때만** 한 번 나가는지.
// 규칙 함수(`SnareSound::MountCue`)는 `Snarehaven.World.EquipSound` 가 본다 — 여기서는 `UMountComponent` 가 그 함수를
// 부르는 때만 본다: 타기 1 · 내리기 1 · 거절 0 · 이미 같은 상태 0 · 표의 간격(0.6초) 안 연타 → 첫 한 번.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Mount.Sound; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnarePlayerSnapshot.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Player/MountComponent.h"
#include "SnarehavenCharacter.h"

namespace
{
	const FName SoundTestWolf(TEXT("animal.wolf"));
	const FName SoundTestSaddle(TEXT("item.saddle_hunting"));
	// 검사 전용 자리 (Verification.md 6): 바다가 없는 빈 월드의 뭍 한 점.
	const FVector SoundTestLandPoint(0.f, 0.f, 500.f);
	// 표의 탈것 간격(0.6초)을 넘기는 검사 전용 시간 — 표 숫자가 몇 배로 늘어도 넘게 넉넉히.
	constexpr double PastGapSeconds = 5.0;

	FSnarePlayerSnapshot MountSoundSnapshot(const TArray<FName>& Mounts)
	{
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TEXT("PFTESTOWNER");
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		Snapshot.Mounts = Mounts;
		Snapshot.MountSaddles.Add(SoundTestWolf, SoundTestSaddle);
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMountSoundTest, "Snarehaven.Mount.Sound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMountSoundTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(SoundTestLandPoint, FRotator::ZeroRotator, Params);
	// 게임모드 없는 검사 월드는 BeginPlay 를 저절로 돌리지 않는다 — 물 표(탈것 켬)를 읽는 곳이 BeginPlay 라 직접 부른다.
	if (Player && !Player->HasActorBegunPlay())
	{
		Player->DispatchBeginPlay();
	}
	UMountComponent* const Mount = Player ? Player->FindComponentByClass<UMountComponent>() : nullptr;
	if (!TestNotNull(TEXT("탈것 컴포넌트"), Mount))
	{
		return false;
	}
	const FSnarePlayerSnapshot Wolf = MountSoundSnapshot({ SoundTestWolf });
	TestEqual(TEXT("처음엔 소리 0"), Mount->GetMountCueCount(), 0);

	// ① 거절 — 탈것 종 0개(물 위 거절과 같은 `GetMountBlockCode` 길)면 바뀐 게 없어 소리 0.
	Mount->ToggleMountWith(MountSoundSnapshot({}));
	TestFalse(TEXT("종 0개면 못 탄다"), Mount->IsMounted());
	TestEqual(TEXT("거절 = 소리 0"), Mount->GetMountCueCount(), 0);
	// 안 탄 채 내리기 요청 = 이미 같은 상태.
	Mount->Server_SetMounted(false, NAME_None, NAME_None);
	TestEqual(TEXT("안 탄 채 내리기 = 소리 0"), Mount->GetMountCueCount(), 0);

	// ② 타기 1 · 탄 채 또 타기 0 · 간격 안 내리기 = 첫 한 번만.
	Mount->ToggleMountWith(Wolf);
	TestTrue(TEXT("탔다"), Mount->IsMounted());
	TestEqual(TEXT("타기 1"), Mount->GetMountCueCount(), 1);
	TestEqual(TEXT("타기 = 안장 삐걱"), Mount->GetLastMountCue(), FName(TEXT("MountOn")));
	Mount->Server_SetMounted(true, SoundTestWolf, SoundTestSaddle);
	TestEqual(TEXT("탄 채 또 타기 = 소리 0"), Mount->GetMountCueCount(), 1);
	Mount->ToggleMountWith(Wolf);
	TestFalse(TEXT("같은 버튼으로 내렸다"), Mount->IsMounted());
	TestEqual(TEXT("간격(0.6초) 안 내리기 = 첫 한 번만"), Mount->GetMountCueCount(), 1);

	// ③ 간격이 지난 뒤 타기 1 · 내리기 1.
	World->TimeSeconds += PastGapSeconds;
	Mount->ToggleMountWith(Wolf);
	TestTrue(TEXT("다시 탔다"), Mount->IsMounted());
	TestEqual(TEXT("간격 뒤 타기 1"), Mount->GetMountCueCount(), 2);
	World->TimeSeconds += PastGapSeconds;
	Mount->ToggleMountWith(Wolf);
	TestFalse(TEXT("다시 내렸다"), Mount->IsMounted());
	TestEqual(TEXT("간격 뒤 내리기 1"), Mount->GetMountCueCount(), 3);
	TestEqual(TEXT("내리기 = 디딤 툭"), Mount->GetLastMountCue(), FName(TEXT("MountOff")));
	return true;
}

#endif
