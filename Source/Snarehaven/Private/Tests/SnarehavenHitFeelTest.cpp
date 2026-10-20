// ROADMAP M21-02 2층 검사 — 때리는 손맛. 한 대 치면
//  ① 0.1초 안에 동물이 피격 클립(HitAnim)을 틀고 하얗게 번쩍이며 파편이 튀는지,
//  ② 히트스톱이 **때린 몸과 맞은 동물만** 느리게 하고(다른 플레이어·월드 배율 1 그대로) 시간이 지나면 풀리는지,
//     시작/끝 로그가 남는지,
//  ③ 넉백으로 100cm 이상 밀리는지, Capturable 이 된 타격에선 안 밀리는지.
// 숫자는 전부 **검사 전용 값**이다 (Verification.md 6 — DESIGN 3.1 의 0.06초·120cm 등과 일부러 다르다).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Combat.HitFeel; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Animation/AnimSequence.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/AnimalRow.h"
#include "Data/PlayerCombatRow.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeLock.h"
#include "SnarehavenCharacter.h"
#include "Player/SnareHaptics.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "CoreGlobals.h"

namespace
{
	const TCHAR* FoxMeshPath = TEXT("/Game/Game/Characters/Animals/Fox/SK_Animal_Fox.SK_Animal_Fox");
	const TCHAR* FoxIdlePath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Idle.A_Animal_Fox_Idle");
	const TCHAR* FoxHitPath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Hit.A_Animal_Fox_Hit");

	// 공격 네 칸 (검사 전용).
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestAttackHitDelaySeconds = 0.3f;
	// 손맛 아홉 칸 (검사 전용 — 설계값 0.06/0.05/120/0.15/3/0.12/0.08/6/10 과 전부 다르다).
	constexpr float TestHitStopSeconds = 0.2f;
	constexpr float TestHitStopTimeDilation = 0.1f;
	constexpr float TestKnockbackCm = 150.f;
	constexpr float TestKnockbackSeconds = 0.2f;
	constexpr float TestShakeAmplitudeCm = 5.f;
	constexpr float TestShakeSeconds = 0.3f;
	constexpr float TestFlashSeconds = 0.25f;
	constexpr float TestDebrisMin = 3.f;
	constexpr float TestDebrisMax = 4.f;

	// 동물 (검사 전용).
	constexpr float TestMaxHP = 100.f;
	constexpr float TestCaptureThresholdPct = 10.f;
	constexpr float TestAnimalDistanceCm = 100.f;
	constexpr float TestBystanderDistanceCm = 2000.f;

	// 합격 조건 (감독 지시 M21-02) — 검사 값이 아니라 **사람이 못 박은 기준**.
	constexpr float RequiredHitAnimWithinSeconds = 0.1f;
	constexpr float RequiredKnockbackCm = 100.f;

	constexpr float TimerStepSeconds = 0.01f;
	constexpr float FrameSeconds = 1.f / 60.f;
	constexpr float Tolerance = 0.001f;

	/** 이 검사 동안 찍힌 로그 줄을 모은다 — "히트스톱 시작/끝" 이 실제로 남았는지 본다. */
	class FLineCollector final : public FOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			FScopeLock Lock(&Guard);
			Lines.Add(V);
		}
		virtual bool CanBeUsedOnAnyThread() const override { return true; }
		virtual bool CanBeUsedOnMultipleThreads() const override { return true; }

		bool Contains(const TCHAR* Needle)
		{
			FScopeLock Lock(&Guard);
			return Lines.ContainsByPredicate([Needle](const FString& Line) { return Line.Contains(Needle); });
		}

	private:
		FCriticalSection Guard;
		TArray<FString> Lines;
	};

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	// `GFrameCounter` 를 같이 올려야 한 프레임 두 번째 Tick 이 그냥 돌아가지 않는다 (SnarehavenMeleeAttackTest 와 같다).
	void AdvanceTimers(UWorld* World, float Seconds)
	{
		const int32 Steps = FMath::RoundToInt(Seconds / TimerStepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(TimerStepSeconds);
		}
	}

	/** 손으로 만든 월드는 컴포넌트 틱을 등록하지 않는다 — 이동 컴포넌트까지 직접 돌린다. */
	void TickAnimal(AAnimalCharacter& Animal, float Seconds)
	{
		const int32 Steps = FMath::CeilToInt(Seconds / FrameSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Animal.Tick(FrameSeconds);
			Animal.GetCharacterMovement()->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
		}
	}

	FPlayerCombatRow MakeTestCombatRow()
	{
		FPlayerCombatRow Row;
		Row.AttackDamage = TestAttackDamage;
		Row.AttackRangeCm = TestAttackRangeCm;
		Row.AttackIntervalSeconds = TestAttackIntervalSeconds;
		Row.AttackHitDelaySeconds = TestAttackHitDelaySeconds;
		Row.HitStopSeconds = TestHitStopSeconds;
		Row.HitStopTimeDilation = TestHitStopTimeDilation;
		Row.KnockbackCm = TestKnockbackCm;
		Row.KnockbackSeconds = TestKnockbackSeconds;
		Row.CameraShakeAmplitudeCm = TestShakeAmplitudeCm;
		Row.CameraShakeSeconds = TestShakeSeconds;
		Row.HitFlashSeconds = TestFlashSeconds;
		Row.HitDebrisMin = TestDebrisMin;
		Row.HitDebrisMax = TestDebrisMax;
		return Row;
	}

	/** 여우 진짜 몸·피격 클립. 도망은 걸어도 속도 0 이라 제자리 — 움직임은 넉백만 보인다. */
	AAnimalCharacter* SpawnFox(UWorld& World, float DistanceCm)
	{
		AAnimalCharacter* Animal =
			World.SpawnActor<AAnimalCharacter>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			return nullptr;
		}

		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_hitfeel");
		Row.MaxHP = TestMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Row.BehaviorKind = EAnimalBehaviorKind::Flee;
		Row.MeshRef = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(FoxMeshPath));
		Row.IdleAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FoxIdlePath));
		Row.HitAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FoxHitPath));
		Animal->ApplyBodyFromRow(Row);
		Animal->GetAnimalState()->InitializeFromRow(Row);
		Animal->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		Animal->GetCharacterMovement()->MaxFlySpeed = 0.f;
		return Animal;
	}

	ASnarehavenCharacter* SpawnPlayer(UWorld& World, const FVector& Location)
	{
		return World.SpawnActor<ASnarehavenCharacter>(Location, FRotator::ZeroRotator);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHitFeelOnHitTest, "Snarehaven.Combat.HitFeel.HitShowsReactionStopsAndKnocksBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHitFeelOnHitTest::RunTest(const FString& Parameters)
{
	FLineCollector Log;
	GLog->AddOutputDevice(&Log);

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnPlayer(*World, FVector::ZeroVector);
	ASnarehavenCharacter* Bystander = SpawnPlayer(*World, FVector(0.f, TestBystanderDistanceCm, 0.f));
	AAnimalCharacter* Fox = SpawnFox(*World, TestAnimalDistanceCm);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("구경하는 플레이어"), Bystander)
		|| !TestNotNull(TEXT("여우"), Fox) || !TestNotNull(TEXT("여우 피격 클립"), Fox->GetHitAnim()))
	{
		DestroyTestWorld(World);
		GLog->RemoveOutputDevice(&Log);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	TestTrue(TEXT("검사 행이 들어갔다"), Melee->ApplyCombatRow(MakeTestCombatRow()));
	const FVector Start = Fox->GetActorLocation();

	TestTrue(TEXT("한 대 들어갔다"), Melee->AttackOnce());

	// ① 0.1초 안에 피격 클립 · 흰 깜빡임 · 파편.
	TickAnimal(*Fox, RequiredHitAnimWithinSeconds * 0.5f);
	TestTrue(TEXT("0.1초 안에 피격 클립(HitAnim)이 돈다"), Fox->GetCurrentBodyAnim() == Fox->GetHitAnim());
	TestTrue(TEXT("흰 깜빡임이 켜졌다"), Fox->IsHitFlashActive());
	const int32 Debris = Fox->GetHitDebrisCount();
	TestTrue(FString::Printf(TEXT("파편 %d개가 %.0f~%.0f 안"), Debris, TestDebrisMin, TestDebrisMax),
		Debris >= TestDebrisMin && Debris <= TestDebrisMax);

	// ② 히트스톱 — 때린 몸과 맞은 동물만. 다른 플레이어·월드 배율은 1 그대로.
	TestTrue(TEXT("히트스톱 중"), Melee->IsHitStopActive());
	TestEqual(TEXT("때린 플레이어 배율"), Player->CustomTimeDilation, TestHitStopTimeDilation, Tolerance);
	TestEqual(TEXT("맞은 여우 배율"), Fox->CustomTimeDilation, TestHitStopTimeDilation, Tolerance);
	TestEqual(TEXT("구경하는 플레이어 배율은 1"), Bystander->CustomTimeDilation, 1.f, Tolerance);
	TestEqual(TEXT("월드 시간 배율은 1"), World->GetWorldSettings()->TimeDilation, 1.f, Tolerance);
	TestTrue(TEXT("카메라 흔들림이 켜졌다 (때린 사람 기기)"), Melee->IsComponentTickEnabled());
	TestFalse(TEXT("구경꾼 카메라는 안 흔들린다"), Bystander->GetMeleeAttack()->IsComponentTickEnabled());

	AdvanceTimers(World, FMath::Max(TestHitStopSeconds, TestFlashSeconds) + 0.05f);
	TestFalse(TEXT("히트스톱이 풀렸다"), Melee->IsHitStopActive());
	TestEqual(TEXT("플레이어 배율 1 로 복귀"), Player->CustomTimeDilation, 1.f, Tolerance);
	TestEqual(TEXT("여우 배율 1 로 복귀"), Fox->CustomTimeDilation, 1.f, Tolerance);
	TestFalse(TEXT("흰 깜빡임이 꺼졌다"), Fox->IsHitFlashActive());
	TestTrue(TEXT("로그: 히트스톱 시작"), Log.Contains(TEXT("히트스톱 시작")));
	TestTrue(TEXT("로그: 히트스톱 끝"), Log.Contains(TEXT("히트스톱 끝")));

	// ③ 넉백 — 호스트가 민다. 100cm 이상.
	TickAnimal(*Fox, TestKnockbackSeconds + 0.1f);
	const float Moved = static_cast<float>(FVector::Dist2D(Start, Fox->GetActorLocation()));
	AddInfo(FString::Printf(TEXT("넉백 이동 %.1fcm (검사 행 %.0fcm)"), Moved, TestKnockbackCm));
	TestTrue(FString::Printf(TEXT("넉백 %.1fcm ≥ %.0fcm"), Moved, RequiredKnockbackCm), Moved >= RequiredKnockbackCm);
	TestTrue(TEXT("때린 사람 반대쪽(+X)으로 밀렸다"), Fox->GetActorLocation().X > Start.X);

	DestroyTestWorld(World);
	GLog->RemoveOutputDevice(&Log);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHitFeelCapturableTest, "Snarehaven.Combat.HitFeel.NoKnockbackWhenCapturable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHitFeelCapturableTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnPlayer(*World, FVector::ZeroVector);
	AAnimalCharacter* Fox = SpawnFox(*World, TestAnimalDistanceCm);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("여우"), Fox))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	TestTrue(TEXT("검사 행이 들어갔다"), Melee->ApplyCombatRow(MakeTestCombatRow()));

	// 이번 한 대로 임계값(10%) 밑으로 떨어지게 미리 깎아 둔다: 100 → 15 → (10 맞고) 5.
	Fox->GetAnimalState()->ApplyDamage(TestMaxHP - TestCaptureThresholdPct - (TestAttackDamage * 0.5f));
	const FVector Start = Fox->GetActorLocation();
	TestTrue(TEXT("한 대 들어갔다"), Melee->AttackOnce());
	TestEqual(TEXT("포획 가능이 됐다"), Fox->GetAnimalState()->GetState(), EAnimalState::Capturable);

	TickAnimal(*Fox, TestKnockbackSeconds + 0.1f);
	TestFalse(TEXT("포획 가능이면 넉백이 없다"), Fox->IsKnockbackActive());
	TestEqual(TEXT("자리 그대로 (포획 거리 밖으로 안 밀린다)"), static_cast<float>(FVector::Dist2D(Start, Fox->GetActorLocation())), 0.f, 1.f);

	AdvanceTimers(World, TestHitStopSeconds + 0.05f);
	DestroyTestWorld(World);
	return true;
}

// M63-02 — **때린 사람 본인 기기만 손이 톡 떤다** (DESIGN 2.2 「타격 진동」). 실제 길(`SwingOnce` → 타격 지연 → `HandleDelayedHit`)로 친다.
// 남이 때림(조종기 없음) → 0 · 내가 때림 → 1 · 간격 안 또 → 그대로 1 · 끔 → 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHitFeelStrikeHapticTest, "Snarehaven.Combat.HitFeel.StrikeHaptic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHitFeelStrikeHapticTest::RunTest(const FString& Parameters)
{
	const bool bWasEnabled = SnareHaptics::IsEnabled();
	SnareHaptics::SetEnabled(true);
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnPlayer(*World, FVector::ZeroVector);
	AAnimalCharacter* Fox = SpawnFox(*World, TestAnimalDistanceCm);
	APlayerController* const Controller = Player != nullptr ? World->SpawnActor<APlayerController>() : nullptr;
	if (Player == nullptr || Fox == nullptr || Controller == nullptr)
	{
		AddError(TEXT("플레이어·여우·조종기를 못 세웠다"));
		SnareHaptics::SetEnabled(bWasEnabled);
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	TestTrue(TEXT("검사 행이 들어갔다"), Melee->ApplyCombatRow(MakeTestCombatRow()));
	TestNotNull(TEXT("과녁"), Melee->RefreshTarget());

	// ① 남이 때림 — 조종기 없는 몸은 0
	int32 Before = SnareHaptics::GetPlayCount();
	Melee->SwingOnce();
	AdvanceTimers(World, TestAttackHitDelaySeconds + 0.05f);
	TestEqual(TEXT("남이 때림 → 내 진동 0"), SnareHaptics::GetPlayCount() - Before, 0);

	// ② 내 기기 — 넷 드라이버 없는 월드는 `ULocalPlayer` 붙은 조종기만 "이 기기"
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Controller->Possess(Player);
	TestTrue(TEXT("내 몸은 내 기기 조종"), Player->IsLocallyControlled());
	AdvanceTimers(World, 1.f);
	TestNotNull(TEXT("과녁"), Melee->RefreshTarget());
	Before = SnareHaptics::GetPlayCount();
	Melee->SwingOnce();
	AdvanceTimers(World, TestAttackHitDelaySeconds + 0.05f);
	TestEqual(TEXT("내가 때림 → 진동 1"), SnareHaptics::GetPlayCount() - Before, 1);
	TestFalse(TEXT("간격 안 또 → 안 튼다"), Player->PlayStrikeHaptic(SnareBodyHaptic::EStrike::Melee));
	TestEqual(TEXT("간격 안 또 → 그대로 1"), SnareHaptics::GetPlayCount() - Before, 1);

	// ③ 끔 — 간격이 지나도 설정이 먼저다
	SnareHaptics::SetEnabled(false);
	AdvanceTimers(World, 1.f);
	Before = SnareHaptics::GetPlayCount();
	TestFalse(TEXT("끔 → 안 튼다"), Player->PlayStrikeHaptic(SnareBodyHaptic::EStrike::Melee));
	TestEqual(TEXT("끔 → 0"), SnareHaptics::GetPlayCount() - Before, 0);

	AddInfo(FString::Printf(TEXT("[StrikeHaptic] 내가 때림 1 · 남 0 · 끔 0 (누적 %d)"), SnareHaptics::GetPlayCount()));
	SnareHaptics::SetEnabled(bWasEnabled);
	DestroyTestWorld(World);
	return true;
}

// M64-02 — **먹은 사람 본인 기기만 손이 톡 떤다** (DESIGN 2.2 「먹기 진동」). 서버 성공 콜백이 부르는 `PlayEatVoice` 를 그대로 부른다.
// 남이 먹음(조종기 없음) → 0 · 내가 먹음 → 1 · 간격 안 또 → 그대로 1 · 독(회복 < 0) 간격 뒤 → 2 · 끔 → 그대로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHitFeelEatHapticTest, "Snarehaven.Combat.HitFeel.EatHaptic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHitFeelEatHapticTest::RunTest(const FString& Parameters)
{
	const bool bWasEnabled = SnareHaptics::IsEnabled();
	SnareHaptics::SetEnabled(true);
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnPlayer(*World, FVector::ZeroVector);
	APlayerController* const Controller = Player != nullptr ? World->SpawnActor<APlayerController>() : nullptr;
	if (Player == nullptr || Controller == nullptr)
	{
		AddError(TEXT("플레이어·조종기를 못 세웠다"));
		SnareHaptics::SetEnabled(bWasEnabled);
		DestroyTestWorld(World);
		return false;
	}

	// ① 남이 먹음 — 조종기 없는 몸은 0
	int32 Before = SnareHaptics::GetPlayCount();
	Player->PlayEatVoice(1.f);
	TestEqual(TEXT("남이 먹음 → 내 진동 0"), SnareHaptics::GetPlayCount() - Before, 0);

	// ② 내 기기
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Controller->Possess(Player);
	TestTrue(TEXT("내 몸은 내 기기 조종"), Player->IsLocallyControlled());
	Before = SnareHaptics::GetPlayCount();
	Player->PlayEatVoice(1.f);
	TestEqual(TEXT("내가 먹음 → 진동 1"), SnareHaptics::GetPlayCount() - Before, 1);
	Player->PlayEatVoice(1.f);
	TestEqual(TEXT("간격 안 또 → 그대로 1"), SnareHaptics::GetPlayCount() - Before, 1);
	World->Tick(LEVELTICK_All, SnareBodyHaptic::EatMinGapSeconds + 0.05f);
	Player->PlayEatVoice(-5.f);
	TestEqual(TEXT("간격 뒤 독 → 진동 2"), SnareHaptics::GetPlayCount() - Before, 2);

	// ③ 끔 — 간격이 지나도 설정이 먼저다
	SnareHaptics::SetEnabled(false);
	World->Tick(LEVELTICK_All, 1.f);
	Before = SnareHaptics::GetPlayCount();
	Player->PlayEatVoice(1.f);
	TestEqual(TEXT("끔 → 0"), SnareHaptics::GetPlayCount() - Before, 0);

	AddInfo(FString::Printf(TEXT("[EatHaptic] 내가 먹음 1 · 독 +1 · 남 0 · 끔 0 (누적 %d)"), SnareHaptics::GetPlayCount()));
	SnareHaptics::SetEnabled(bWasEnabled);
	DestroyTestWorld(World);
	return true;
}

#endif
