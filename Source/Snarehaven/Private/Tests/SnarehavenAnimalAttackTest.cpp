// ROADMAP M5-07d2·d3 2층 검사 — 동물이 **되받아치고**(궁지 반격·맞으면 반격) **선공 종이 먼저 달려드는지**,
// 그리고 맞은 플레이어가 `A_Player_Hit` 을 한 번 틀고 입력은 그대로 받는지.
// 판단은 이름이 아니라 실제로 깎인 체력·친 횟수·움직인 거리로 한다 (Verification.md).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/CapsuleComponent.h"
#include "Data/AnimalRow.h"
#include "Data/PlayerVitalsRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Player/PlayerVitalsComponent.h"
#include "Player/SnareHaptics.h"
#include "Snarehaven.h"

namespace
{
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* FoxMeshPath = TEXT("/Game/Game/Characters/Animals/Fox/SK_Animal_Fox.SK_Animal_Fox");
	const TCHAR* FoxIdlePath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Idle.A_Animal_Fox_Idle");
	const TCHAR* FoxWalkPath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Walk.A_Animal_Fox_Walk");
	const TCHAR* FoxRunPath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Run.A_Animal_Fox_Run");
	const TCHAR* FoxDeathPath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Death.A_Animal_Fox_Death");
	const TCHAR* FoxAttackPath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Attack.A_Animal_Fox_Attack");
	const TCHAR* PlayerHitAssetName = TEXT("A_Player_Hit");

	// **검사 전용 값** (Verification.md 6) — DESIGN 3.1 의 6·1.5초·150cm·500cm 를 베끼면 사람이 표를 고칠 때
	// 검사가 깨지고, 코드가 설계 숫자를 박아 넣어도 안 잡힌다. 그래서 전부 설계값과 다른 수를 쓴다.
	constexpr float TestMaxHP = 50.f;
	constexpr float TestCaptureThresholdPct = 10.f;
	constexpr float TestMoveSpeed = 40.f;
	constexpr float TestNickDamage = 1.f;
	constexpr float TestAttackDamage = 3.f;
	constexpr float TestAttackIntervalSec = 0.5f;
	constexpr float TestAttackDamageAlt = 5.f;         // 표를 갈면 결과가 따라오는지 보는 둘째 행
	constexpr float TestAttackIntervalSecAlt = 0.25f;
	constexpr float TestAttackRangeCm = 120.f;
	constexpr float TestAggroRadiusCm = 300.f;
	constexpr float TestPlayerMaxHealth = 100.f;

	// 간격을 2진수로 딱 떨어지는 값(1/8초)으로 밟는다 — 0.1 로 밟으면 0.5 - 0.1×5 가 0 이 안 돼 한 대가 밀린다.
	constexpr float TestStepSeconds = 0.125f;
	constexpr float TestRunSeconds = 2.f;
	constexpr float FrameSeconds = 1.f / 60.f;          // 추격·귀환은 게임이 실제로 밟는 걸음으로 (FleeStaysWithinLeash 와 같다)
	constexpr float TestChaseLimitSeconds = 60.f;       // 무한 루프 막이
	constexpr float TestFarAwayCm = 5000.f;             // 도주·추격 한계를 훌쩍 넘는 거리

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

	/** 이동 컴포넌트까지 한 프레임 돌린다 (손으로 만든 테스트 월드는 컴포넌트 틱을 등록하지 않는다). */
	void TickAnimalOnce(AAnimalCharacter& Animal, float DeltaSeconds)
	{
		Animal.Tick(DeltaSeconds);
		Animal.GetCharacterMovement()->TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
	}

	/** 공격 칸만 인자로 받는 행 — 몸·클립은 여우 진짜 에셋, 숫자는 전부 검사 전용 값. */
	FAnimalRow MakeAttackRow(EAnimalBehaviorKind Kind, float Damage = TestAttackDamage,
		float IntervalSec = TestAttackIntervalSec)
	{
		FAnimalRow Row;
		Row.MaxHP = TestMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Row.MoveSpeed = TestMoveSpeed;
		Row.MeshRef = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(FoxMeshPath));
		Row.IdleAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FoxIdlePath));
		Row.WalkAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FoxWalkPath));
		Row.FleeAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FoxRunPath));
		Row.DeathAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FoxDeathPath));
		Row.AttackAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FoxAttackPath));
		Row.BehaviorKind = Kind;
		Row.AttackDamage = Damage;
		Row.AttackIntervalSec = IntervalSec;
		Row.AttackRangeCm = TestAttackRangeCm;
		Row.AggroRadiusCm = TestAggroRadiusCm;
		return Row;
	}

	/** 체력 행 — 저절로 차거나 닳지 않게 재생·소모를 0 으로 둔다. 맞은 만큼만 깎여야 셈이 맞는다. */
	FPlayerVitalsRow MakeVitalsRow()
	{
		FPlayerVitalsRow Row;
		Row.MaxHealth = TestPlayerMaxHealth;
		Row.MaxStamina = TestPlayerMaxHealth;
		Row.RunStaminaPerSecond = 0.f;
		Row.AttackStaminaCost = 0.f;
		Row.StaminaRegenPerSecond = 0.f;
		Row.StaminaRegenDelaySeconds = 0.f;
		Row.ExhaustionHealthPerSecond = 0.f;
		Row.HealthRegenPerSecond = 0.f;
		Row.HealthRegenStaminaThreshold = 0.f;
		Row.RespawnHealth = TestPlayerMaxHealth;
		Row.RespawnStamina = TestPlayerMaxHealth;
		Row.DeathLingerSeconds = 0.f;
		return Row;
	}

	/** 행을 적용한 동물 하나 (원점). 바닥이 없는 월드라 비행 모드로 수평 이동만 본다. */
	AAnimalCharacter* SpawnAttackAnimal(UWorld& World, const FAnimalRow& Row)
	{
		AAnimalCharacter* Animal = World.SpawnActor<AAnimalCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			return nullptr;
		}
		Animal->ApplyBodyFromRow(Row);
		Animal->ApplyMoveSpeedFromRow(Row);
		Animal->ApplyAttackFromRow(Row);
		Animal->GetAnimalState()->InitializeFromRow(Row);
		// 비행 모드는 걷기 최고 속도 대신 비행 칸을 본다 — 행의 속도를 옮겨 놓아야 "접근 속도 = MoveSpeed" 를 잴 수 있다.
		Animal->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		Animal->GetCharacterMovement()->MaxFlySpeed = Row.MoveSpeed;
		return Animal;
	}

	/** 체력이 켜진 플레이어 하나. 실패하면 nullptr. */
	ASnarehavenCharacter* SpawnTestPlayer(UWorld& World, const FVector& Location)
	{
		ASnarehavenCharacter* Player = World.SpawnActor<ASnarehavenCharacter>(Location, FRotator::ZeroRotator);
		if (Player == nullptr || !IsValid(Player->GetPlayerVitals())
			|| !Player->GetPlayerVitals()->InitializeFromRow(MakeVitalsRow()))
		{
			return nullptr;
		}
		return Player;
	}

	float GetHealth(const ASnarehavenCharacter& Player)
	{
		return Player.GetPlayerVitals()->GetHealth();
	}

	/** 수치 한 줄을 검사 결과와 로그 둘 다에 남긴다 — STATUS 가 로그 줄을 인용한다 (Verification.md). */
	void Report(FAutomationTestBase& Test, const FString& Line)
	{
		Test.AddInfo(Line);
		UE_LOG(LogSnarehaven, Display, TEXT("[%s] %s"), *Test.GetTestName(), *Line);
	}
}


// M5-07d2 합격 ① — **두 갈래가 각자 조건에서만 켜진다.** 궁지 반격은 도주 한계 안에선 안 치고 한계 밖에서만,
// 맞으면 반격은 맞기 전(Idle)엔 안 치고 맞은 뒤(Fight)에만. 거리는 둘 다 사거리 안으로 같게 둔다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalCounterBranchTest,
	"Snarehaven.Animal.Attack.CounterBranchesOnlyWhenTheirConditionHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalCounterBranchTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	const float CloseCm = TestAttackRangeCm * 0.5f;

	// ── 궁지 반격 ──
	AAnimalCharacter* Cornered = SpawnAttackAnimal(*World, MakeAttackRow(EAnimalBehaviorKind::CorneredCounter));
	ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(CloseCm, 0.f, 0.f));
	if (Cornered == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·플레이어를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	Cornered->GetAnimalState()->ApplyDamage(TestNickDamage);
	TestEqual(TEXT("궁지: 한 대 맞으면 Fight"), Cornered->GetAnimalState()->GetState(), EAnimalState::Fight);
	TestFalse(TEXT("궁지: 한계 안에서는 사거리 안이어도 안 친다"), Cornered->TryAttackOnAuthority(Player));

	// 한계 밖으로 몰린 자리를 만든다 — 한계 값을 베끼지 않고 "훌쩍 밖" 에 둔 뒤 동물 자신에게 묻는다.
	Cornered->SetActorLocation(Cornered->GetFightHome() + FVector(TestFarAwayCm, 0.f, 0.f));
	Player->SetActorLocation(Cornered->GetActorLocation() + FVector(CloseCm, 0.f, 0.f));
	TestTrue(TEXT("궁지: 이제 도주 한계 밖이다"), Cornered->IsBeyondFleeLeash());
	TestTrue(TEXT("궁지: 한계에 몰리면 돌아서서 친다"), Cornered->TryAttackOnAuthority(Player));
	Cornered->Destroy();

	// ── 맞으면 반격 ──
	AAnimalCharacter* Retaliate = SpawnAttackAnimal(*World, MakeAttackRow(EAnimalBehaviorKind::RetaliateWhenHit));
	Player->SetActorLocation(FVector(CloseCm, 0.f, 0.f));
	if (Retaliate == nullptr)
	{
		AddError(TEXT("반격 동물을 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}
	TestFalse(TEXT("반격: 맞기 전(Idle)엔 사거리 안이어도 안 친다"), Retaliate->TryAttackOnAuthority(Player));
	Retaliate->GetAnimalState()->ApplyDamage(TestNickDamage);
	TestTrue(TEXT("반격: 맞은 뒤(Fight)엔 친다"), Retaliate->TryAttackOnAuthority(Player));
	TestTrue(TEXT("반격: 도주 한계 안인데도 친다 (궁지 조건과 다르다)"), !Retaliate->IsBeyondFleeLeash());

	// 도주 종은 어떤 상태에서도 안 친다 — 기본값이 사람을 때리는 쪽으로 기울면 안 된다.
	AAnimalCharacter* Flee = SpawnAttackAnimal(*World, MakeAttackRow(EAnimalBehaviorKind::Flee));
	Flee->GetAnimalState()->ApplyDamage(TestNickDamage);
	TestFalse(TEXT("도주 종은 맞아도 안 친다"), Flee->TryAttackOnAuthority(Player));

	DestroyTestWorld(World);
	return true;
}

// M5-07d2 합격 ② — **간격·사거리·데미지가 표에서 온다.** 두 행(간격·데미지가 다른)을 같은 시간 동안 돌려
// 깎인 체력 = 친 횟수 × 행의 데미지, 친 횟수 = 시간 ÷ 행의 간격 인지 본다. 사거리 밖이면 한 대도 없다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalAttackNumbersTest,
	"Snarehaven.Animal.Attack.IntervalRangeDamageComeFromRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalAttackNumbersTest::RunTest(const FString& Parameters)
{
	struct FCase { float Damage; float IntervalSec; };
	const FCase Cases[] = { { TestAttackDamage, TestAttackIntervalSec }, { TestAttackDamageAlt, TestAttackIntervalSecAlt } };

	for (const FCase& Case : Cases)
	{
		UWorld* World = CreateTestWorld();
		AAnimalCharacter* Animal = SpawnAttackAnimal(*World,
			MakeAttackRow(EAnimalBehaviorKind::RetaliateWhenHit, Case.Damage, Case.IntervalSec));
		ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(TestAttackRangeCm - 20.f, 0.f, 0.f));
		if (Animal == nullptr || Player == nullptr)
		{
			AddError(TEXT("동물·플레이어를 못 세웠다"));
			DestroyTestWorld(World);
			return false;
		}

		Animal->GetAnimalState()->ApplyDamage(TestNickDamage);
		const float HealthBefore = GetHealth(*Player);
		const int32 Steps = FMath::RoundToInt(TestRunSeconds / TestStepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			TickAnimalOnce(*Animal, TestStepSeconds);
		}

		// 첫 대는 사거리에 드는 순간 바로(기다리지 않는다), 그 뒤 간격마다 — 2초 ÷ 간격 번.
		const int32 ExpectedHits = FMath::RoundToInt(TestRunSeconds / Case.IntervalSec);
		const int32 Hits = Animal->GetLandedAttackCount();
		const float Lost = HealthBefore - GetHealth(*Player);
		Report(*this, FString::Printf(TEXT("행 데미지 %.1f · 간격 %.2f초 · %.1f초 동안 %d대 (기대 %d) · 체력 %.1f → %.1f (−%.1f)"),
			Case.Damage, Case.IntervalSec, TestRunSeconds, Hits, ExpectedHits, HealthBefore, GetHealth(*Player), Lost));
		TestEqual(TEXT("친 횟수 = 시간 ÷ 행의 간격"), Hits, ExpectedHits);
		TestEqual(TEXT("깎인 체력 = 친 횟수 × 행의 데미지"), Lost, Hits * Case.Damage, KINDA_SMALL_NUMBER);

		// 사거리 밖(행의 사거리 + 여유)이면 한 대도 안 닿는다.
		Player->SetActorLocation(Animal->GetActorLocation() + FVector(TestAttackRangeCm + 30.f, 0.f, 0.f));
		const float HealthOutside = GetHealth(*Player);
		TestFalse(TEXT("사거리 밖이면 안 친다"), Animal->TryAttackOnAuthority(Player));
		TestEqual(TEXT("사거리 밖이면 체력이 그대로"), GetHealth(*Player), HealthOutside);

		DestroyTestWorld(World);
	}
	return true;
}

// M5-07d2 합격 ③ — **Capturable 에서 멈춘다** (DESIGN 3.1 — 포획 직전의 좌절을 뺀다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalAttackStopsCapturableTest,
	"Snarehaven.Animal.Attack.StopsWhenCapturable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalAttackStopsCapturableTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = SpawnAttackAnimal(*World, MakeAttackRow(EAnimalBehaviorKind::RetaliateWhenHit));
	ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(TestAttackRangeCm - 20.f, 0.f, 0.f));
	if (Animal == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·플레이어를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	// 먼저 실제로 치는 것을 본다 — 안 치는 동물로 "멈췄다" 를 통과시키지 않게.
	UAnimalStateComponent* const State = Animal->GetAnimalState();
	State->ApplyDamage(TestNickDamage);
	TickAnimalOnce(*Animal, TestStepSeconds);
	TestEqual(TEXT("Fight 에서는 친다"), Animal->GetLandedAttackCount(), 1);

	// 임계값까지 정확히 깎는다 — 설계 숫자를 쓰지 않고 행이 말하는 만큼만.
	State->ApplyDamage(State->GetCurrentHP() - State->GetCaptureThresholdHP());
	TestEqual(TEXT("임계값에 닿으면 Capturable"), State->GetState(), EAnimalState::Capturable);

	const int32 HitsBefore = Animal->GetLandedAttackCount();
	const float HealthBefore = GetHealth(*Player);
	const int32 Steps = FMath::RoundToInt(TestRunSeconds / TestStepSeconds);
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		TickAnimalOnce(*Animal, TestStepSeconds);
	}
	Report(*this, FString::Printf(TEXT("Capturable %.1f초 동안 친 횟수 %d → %d · 플레이어 체력 %.1f → %.1f"),
		TestRunSeconds, HitsBefore, Animal->GetLandedAttackCount(), HealthBefore, GetHealth(*Player)));
	TestEqual(TEXT("Capturable 동안 한 대도 더 안 친다"), Animal->GetLandedAttackCount(), HitsBefore);
	TestEqual(TEXT("Capturable 동안 플레이어 체력이 그대로"), GetHealth(*Player), HealthBefore);
	TestFalse(TEXT("직접 불러도 안 친다"), Animal->TryAttackOnAuthority(Player));

	DestroyTestWorld(World);
	return true;
}

// M5-07d2 합격 ④ — **권한 없는 쪽에서 부르면 체력이 안 깎인다** (DESIGN 4.1 권위 표).
// 대조군으로 권한을 되돌리면 같은 자리에서 깎이는 것도 본다 — 늘 안 깎이는 코드로 통과되지 않게.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalAttackAuthorityTest,
	"Snarehaven.Animal.Attack.NonAuthorityCannotDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalAttackAuthorityTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = SpawnAttackAnimal(*World, MakeAttackRow(EAnimalBehaviorKind::RetaliateWhenHit));
	ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(TestAttackRangeCm - 20.f, 0.f, 0.f));
	if (Animal == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·플레이어를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	// 맞히는 것은 권한이 있을 때 한다 — 상태를 Fight 로 만들어 두어야 "칠 때" 다.
	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);
	const float HealthBefore = GetHealth(*Player);

	// 게스트 화면의 복제본 흉내 (SnarehavenAnimalReplicationTest 와 같은 길).
	Animal->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("권한 없는 쪽의 직접 호출은 안 친다"), Animal->TryAttackOnAuthority(Player));
	const int32 Steps = FMath::RoundToInt(TestRunSeconds / TestStepSeconds);
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		TickAnimalOnce(*Animal, TestStepSeconds);
	}
	TestEqual(TEXT("권한 없는 쪽 틱에서도 체력이 그대로"), GetHealth(*Player), HealthBefore);
	TestEqual(TEXT("권한 없는 쪽은 친 횟수 0"), Animal->GetLandedAttackCount(), 0);

	Animal->SetRole(ROLE_Authority);
	TestTrue(TEXT("대조군: 권한을 되돌리면 같은 자리에서 친다"), Animal->TryAttackOnAuthority(Player));
	TestEqual(TEXT("대조군: 행의 데미지만큼 깎인다"), GetHealth(*Player), HealthBefore - TestAttackDamage, KINDA_SMALL_NUMBER);

	DestroyTestWorld(World);
	return true;
}

// M44-02 — **동물에게 한 대 맞으면 신음 한 번, 0.6초 안 두 번째는 묶인다** (DESIGN 2.2 「플레이어 몸 소리」).
// 소리 함수를 직접 부르지 않고 실제 공격 길(`TryAttackOnAuthority`)로 친다 — 길에서 빠지면 0 이 나온다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerHurtVoiceTest,
	"Snarehaven.Player.Sound.HurtVoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerHurtVoiceTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = SpawnAttackAnimal(*World, MakeAttackRow(EAnimalBehaviorKind::RetaliateWhenHit));
	ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(TestAttackRangeCm - 20.f, 0.f, 0.f));
	if (Animal == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·플레이어를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}
	const FName HurtCue = ASnarehavenCharacter::GetHurtCue();
	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);   // Fight 로 — 맞으면 반격 종이 칠 때

	TestEqual(TEXT("맞기 전 신음 0"), Player->GetBodyVoiceRequestCount(HurtCue), 0);

	// ① 한 대 = 신음 1
	TestTrue(TEXT("첫 대가 들어간다"), Animal->TryAttackOnAuthority(Player));
	TestEqual(TEXT("한 대 = 신음 1"), Player->GetBodyVoiceRequestCount(HurtCue), 1);

	// ② 0.1초 안 둘째 대 — 체력은 깎이지만 신음은 묶인다(늑대 여럿에게 동시에 물림).
	World->TimeSeconds += 0.1;
	const float HealthBefore = GetHealth(*Player);
	TestTrue(TEXT("둘째 대도 들어간다"), Animal->TryAttackOnAuthority(Player));
	TestEqual(TEXT("둘째 대 체력은 깎인다"), GetHealth(*Player), HealthBefore - TestAttackDamage, KINDA_SMALL_NUMBER);
	TestEqual(TEXT("0.1초 안 두 대 = 신음 1"), Player->GetBodyVoiceRequestCount(HurtCue), 1);

	// ③ 간격이 지나면 다시 — 막아 둔 채 영영 무음이 되지 않는지.
	World->TimeSeconds += 0.6;
	TestTrue(TEXT("셋째 대가 들어간다"), Animal->TryAttackOnAuthority(Player));
	TestEqual(TEXT("간격 뒤 다시 맞으면 2"), Player->GetBodyVoiceRequestCount(HurtCue), 2);

	// ④ 권한 없는 쪽이 부른 공격은 안 들어가고 소리도 0 (DESIGN 4.1 — 호스트가 순간을 정한다).
	World->TimeSeconds += 1.0;
	Animal->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("권한 없는 쪽은 안 친다"), Animal->TryAttackOnAuthority(Player));
	TestEqual(TEXT("권한 없는 쪽 호출 = 신음 그대로 2"), Player->GetBodyVoiceRequestCount(HurtCue), 2);

	Report(*this, FString::Printf(TEXT("맞음 소리: 대 3 → 신음 %d · 체력 %.1f/%.1f"),
		Player->GetBodyVoiceRequestCount(HurtCue), GetHealth(*Player), TestPlayerMaxHealth));
	DestroyTestWorld(World);
	return true;
}

// M44-03 — **쓰러지는 대는 털썩 한 번, 그 대의 신음은 건너뛴다** (DESIGN 2.2 「맞음·쓰러짐 겹침」).
// 실제 공격 길로 한 대에 쓰러뜨린다 — 쓰러짐 길에서 소리가 빠지면 0, 겹침 막이가 빠지면 신음 1 이 나온다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerDownVoiceTest,
	"Snarehaven.Player.Sound.DownVoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerDownVoiceTest::RunTest(const FString& Parameters)
{
	// 한 대에 쓰러지게 — 체력의 두 배 (검사 전용 값).
	constexpr float TestLethalDamage = TestPlayerMaxHealth * 2.f;

	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = SpawnAttackAnimal(*World,
		MakeAttackRow(EAnimalBehaviorKind::RetaliateWhenHit, TestLethalDamage));
	ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(TestAttackRangeCm - 20.f, 0.f, 0.f));
	// 사망 알림 배선은 `BeginPlay` 에 있는데 검사 월드는 폰에 `BeginPlay` 를 안 준다 — 손으로 시작시킨다
	// (`Snarehaven.Player.Body` 쓰러짐 시험과 같다). 시작이 표로 체력을 다시 채울 수 있어 검사 행을 다시 먹인다.
	// 몸(쓰러짐 클립)이 있어야 누운 시간이 정해져 쓰러진다 — 없으면 바로 부활하는 옛 길이다.
	if (Player != nullptr)
	{
		Player->DispatchBeginPlay();
	}
	if (Animal == nullptr || Player == nullptr || !Player->ApplyPlayerBody()
		|| !Player->GetPlayerVitals()->InitializeFromRow(MakeVitalsRow()))
	{
		AddError(TEXT("동물·몸 있는 플레이어를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}
	const FName HurtCue = ASnarehavenCharacter::GetHurtCue();
	const FName DownCue = ASnarehavenCharacter::GetDownCue();
	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);   // Fight 로 — 맞으면 반격 종이 칠 때

	TestEqual(TEXT("쓰러지기 전 털썩 0"), Player->GetBodyVoiceRequestCount(DownCue), 0);
	TestTrue(TEXT("막타가 들어간다"), Animal->TryAttackOnAuthority(Player));
	TestTrue(TEXT("한 대에 쓰러진다"), Player->IsDowned());
	TestEqual(TEXT("쓰러짐 = 털썩 1"), Player->GetBodyVoiceRequestCount(DownCue), 1);
	TestEqual(TEXT("쓰러지는 대의 신음은 건너뜀 = 0"), Player->GetBodyVoiceRequestCount(HurtCue), 0);

	// 누운 동안 또 맞음 소리가 와도(다른 늑대·늦게 온 멀티캐스트) 신음 0 그대로.
	World->TimeSeconds += 1.0;
	Player->Multicast_PlayHurtVoice();
	TestEqual(TEXT("누운 동안 신음 0"), Player->GetBodyVoiceRequestCount(HurtCue), 0);

	Report(*this, FString::Printf(TEXT("쓰러짐 소리: 막타 1 → 털썩 %d · 신음 %d · 쓰러짐 %s"),
		Player->GetBodyVoiceRequestCount(DownCue), Player->GetBodyVoiceRequestCount(HurtCue),
		Player->IsDowned() ? TEXT("예") : TEXT("아니오")));
	DestroyTestWorld(World);
	return true;
}

// M61-03 — **맞으면 본인 기기만 짧게, 쓰러지면 길게 떤다** (DESIGN 2.2 「몸 진동」). 실제 공격 길로 친다.
// 내가 맞음 → 1 · 0.1초 안 또 맞음 → 그대로 · 남이 맞음(조종기 없음) → 0 · 끔 → 0 · 쓰러짐 → 맞음 진동 없이 쓰러짐 1.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerBodyHapticTest,
	"Snarehaven.Player.Haptic.HurtAndDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerBodyHapticTest::RunTest(const FString& Parameters)
{
	constexpr float TestLethalDamage = TestPlayerMaxHealth * 2.f;
	const bool bWasEnabled = SnareHaptics::IsEnabled();
	SnareHaptics::SetEnabled(true);

	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = SpawnAttackAnimal(*World, MakeAttackRow(EAnimalBehaviorKind::RetaliateWhenHit));
	ASnarehavenCharacter* Mine = SpawnTestPlayer(*World, FVector(TestAttackRangeCm - 20.f, 0.f, 0.f));
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (Animal == nullptr || Mine == nullptr || Controller == nullptr)
	{
		AddError(TEXT("동물·플레이어·조종기를 못 세웠다"));
		SnareHaptics::SetEnabled(bWasEnabled);
		DestroyTestWorld(World);
		return false;
	}
	// 쓰러짐 길은 몸·BeginPlay 가 있어야 선다 (`Snarehaven.Player.Sound.DownVoice` 와 같다).
	Mine->DispatchBeginPlay();
	if (!Mine->ApplyPlayerBody() || !Mine->GetPlayerVitals()->InitializeFromRow(MakeVitalsRow()))
	{
		AddError(TEXT("몸 있는 플레이어를 못 세웠다"));
		SnareHaptics::SetEnabled(bWasEnabled);
		DestroyTestWorld(World);
		return false;
	}
	// 넷 드라이버 없는 월드는 `ULocalPlayer` 붙은 조종기만 "이 기기" (ZoneEffectTest 와 같다).
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Controller->Possess(Mine);
	TestTrue(TEXT("내 몸에 조종기가 붙었다"), Mine->GetController() == Controller);
	TestTrue(TEXT("내 몸은 내 기기 조종"), Mine->IsLocallyControlled());
	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);

	// ① 남이 맞음 — 조종기 없는 몸은 0
	ASnarehavenCharacter* Other = SpawnTestPlayer(*World, FVector(TestAttackRangeCm - 20.f, 100.f, 0.f));
	int32 Before = SnareHaptics::GetPlayCount();
	if (Other != nullptr)
	{
		Other->Multicast_PlayHurtVoice();
		TestEqual(TEXT("남이 맞음 → 내 진동 0"), SnareHaptics::GetPlayCount() - Before, 0);
	}

	// ② 내가 맞음 → 1, 0.1초 안 또 → 그대로, 간격 뒤 → 2
	Before = SnareHaptics::GetPlayCount();
	TestTrue(TEXT("첫 대가 들어간다"), Animal->TryAttackOnAuthority(Mine));
	TestEqual(TEXT("내가 맞음 → 진동 1"), SnareHaptics::GetPlayCount() - Before, 1);
	World->TimeSeconds += 0.1;
	TestTrue(TEXT("둘째 대가 들어간다"), Animal->TryAttackOnAuthority(Mine));
	TestEqual(TEXT("간격 안 또 맞음 → 그대로 1"), SnareHaptics::GetPlayCount() - Before, 1);
	World->TimeSeconds += 0.6;
	TestTrue(TEXT("셋째 대가 들어간다"), Animal->TryAttackOnAuthority(Mine));
	TestEqual(TEXT("간격 뒤 → 2"), SnareHaptics::GetPlayCount() - Before, 2);

	// ③ 끔 → 0
	SnareHaptics::SetEnabled(false);
	World->TimeSeconds += 1.0;
	Before = SnareHaptics::GetPlayCount();
	TestTrue(TEXT("끈 채 맞음"), Animal->TryAttackOnAuthority(Mine));
	TestEqual(TEXT("끔 → 0"), SnareHaptics::GetPlayCount() - Before, 0);
	SnareHaptics::SetEnabled(true);

	// ④ 쓰러짐 — 막타 한 대. 맞음 진동은 건너뛰고 쓰러짐 진동만 1
	World->TimeSeconds += 1.0;
	AAnimalCharacter* Killer = SpawnAttackAnimal(*World,
		MakeAttackRow(EAnimalBehaviorKind::RetaliateWhenHit, TestLethalDamage));
	if (!TestNotNull(TEXT("막타 동물"), Killer))
	{
		SnareHaptics::SetEnabled(bWasEnabled);
		DestroyTestWorld(World);
		return false;
	}
	Killer->GetAnimalState()->ApplyDamage(TestNickDamage);
	Before = SnareHaptics::GetPlayCount();
	TestTrue(TEXT("막타가 들어간다"), Killer->TryAttackOnAuthority(Mine));
	TestTrue(TEXT("쓰러진다"), Mine->IsDowned());
	TestEqual(TEXT("쓰러짐 → 진동 1 (맞음은 건너뜀)"), SnareHaptics::GetPlayCount() - Before, 1);

	Report(*this, FString::Printf(TEXT("몸 진동: 맞음 2 · 남 0 · 끔 0 · 쓰러짐 1 (누적 %d)"), SnareHaptics::GetPlayCount()));
	SnareHaptics::SetEnabled(bWasEnabled);
	DestroyTestWorld(World);
	return true;
}

// M61-04 — **체력이 위험하면 심장 소리 박자마다 본인 기기가 가볍게 뛴다** (DESIGN 2.2 「몸 진동」 위험 체력 심장).
// 위험 → 박동마다 1 · 간격 안 → 그대로 · 평소 → 0 · 끔 → 0 · 쓰러짐 → 0.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerHeartbeatHapticTest,
	"Snarehaven.Player.Haptic.Heartbeat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerHeartbeatHapticTest::RunTest(const FString& Parameters)
{
	const bool bWasEnabled = SnareHaptics::IsEnabled();
	SnareHaptics::SetEnabled(true);

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Mine = SpawnTestPlayer(*World, FVector::ZeroVector);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (Mine == nullptr || Controller == nullptr)
	{
		AddError(TEXT("플레이어·조종기를 못 세웠다"));
		SnareHaptics::SetEnabled(bWasEnabled);
		DestroyTestWorld(World);
		return false;
	}
	Mine->DispatchBeginPlay();
	if (!Mine->ApplyPlayerBody() || !Mine->GetPlayerVitals()->InitializeFromRow(MakeVitalsRow()))
	{
		AddError(TEXT("몸 있는 플레이어를 못 세웠다"));
		SnareHaptics::SetEnabled(bWasEnabled);
		DestroyTestWorld(World);
		return false;
	}
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Controller->Possess(Mine);

	// ① 평소(체력 가득) → 0
	int32 Before = SnareHaptics::GetPlayCount();
	Mine->TickHeartbeatSound(5.f);
	TestEqual(TEXT("평소 → 진동 0"), SnareHaptics::GetPlayCount() - Before, 0);

	// ② 위험(체력 10) → 첫 박동 1, 간격 안 → 그대로, 간격 뒤 → 2
	Mine->GetPlayerVitals()->ApplyDamage(TestPlayerMaxHealth - 10.f);
	Before = SnareHaptics::GetPlayCount();
	Mine->TickHeartbeatSound(0.1f);
	TestEqual(TEXT("위험 첫 박동 → 진동 1"), SnareHaptics::GetPlayCount() - Before, 1);
	World->TimeSeconds += 0.1;
	Mine->TickHeartbeatSound(0.1f);
	TestEqual(TEXT("박동 간격 안 → 그대로 1"), SnareHaptics::GetPlayCount() - Before, 1);
	World->TimeSeconds += 1.5;
	Mine->TickHeartbeatSound(1.5f);
	TestEqual(TEXT("다음 박동 → 2"), SnareHaptics::GetPlayCount() - Before, 2);

	// ③ 끔 → 0
	SnareHaptics::SetEnabled(false);
	World->TimeSeconds += 1.5;
	Before = SnareHaptics::GetPlayCount();
	Mine->TickHeartbeatSound(1.5f);
	TestEqual(TEXT("끔 → 0"), SnareHaptics::GetPlayCount() - Before, 0);
	SnareHaptics::SetEnabled(true);

	// ④ 쓰러짐 → 0
	Mine->GetPlayerVitals()->KillForCheck();
	World->TimeSeconds += 1.5;
	Before = SnareHaptics::GetPlayCount();
	Mine->TickHeartbeatSound(5.f);
	TestEqual(TEXT("쓰러짐 → 0"), SnareHaptics::GetPlayCount() - Before, 0);

	Report(*this, FString::Printf(TEXT("심장 진동: 위험 2 · 평소 0 · 끔 0 · 쓰러짐 0 (누적 %d)"), SnareHaptics::GetPlayCount()));
	SnareHaptics::SetEnabled(bWasEnabled);
	DestroyTestWorld(World);
	return true;
}

// M5-07d2 — **맞은 플레이어는 `A_Player_Hit` 을 한 번 틀고 입력은 막지 않는다** (DESIGN 3.1).
// 클립은 이름이 아니라 실제로 걸린 객체의 이름으로, "한 번" 은 클립이 걸리는 순간을 세어 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerHitClipTest,
	"Snarehaven.Player.Hit.ClipPlaysOnceWithoutBlockingInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerHitClipTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector::ZeroVector);
	if (Player == nullptr || !Player->ApplyPlayerBody())
	{
		AddError(TEXT("몸이 있는 플레이어를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	Player->Client_ReceiveAnimalHit(TestAttackDamage);
	TestEqual(TEXT("맞으면 체력이 데미지만큼 준다"), GetHealth(*Player), TestPlayerMaxHealth - TestAttackDamage, KINDA_SMALL_NUMBER);
	TestFalse(TEXT("한 대로는 쓰러지지 않는다"), Player->IsDowned());

	Player->UpdateBodyAnimation();
	const UAnimSequence* const HitClip = Player->GetCurrentBodyAnim();
	if (!TestNotNull(TEXT("맞은 직후 도는 클립이 있다"), HitClip))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("맞은 직후 클립은 A_Player_Hit"), HitClip->GetName(), FString(PlayerHitAssetName));

	// 입력은 막지 않는다 — 이동 입력이 실리고 공격 버튼도 먹는다.
	Player->MoveByAxis(FVector2D(0.f, 1.f));
	TestFalse(TEXT("맞는 중에도 이동 입력이 실린다"), Player->GetPendingMovementInputVector().IsNearlyZero());
	Player->ConsumeMovementInputVector();
	Player->GetMeleeAttack()->StartAttacking();
	TestTrue(TEXT("맞는 중에도 공격 입력이 먹는다"), Player->GetMeleeAttack()->IsAttacking());
	Player->GetMeleeAttack()->StopAttacking();

	// 클립 길이의 두 배만큼 흘리며 A_Player_Hit 이 새로 걸리는 순간을 센다 — 되풀이면 1 보다 크다.
	const float HitSeconds = HitClip->GetPlayLength();
	int32 HitStarts = 1;
	const UAnimSequence* Previous = HitClip;
	for (float Elapsed = 0.f; Elapsed < HitSeconds * 2.f; Elapsed += FrameSeconds)
	{
		static_cast<AActor*>(Player)->Tick(FrameSeconds);   // 캐릭터의 Tick 은 protected — 엔진처럼 액터로 부른다
		const UAnimSequence* const Current = Player->GetCurrentBodyAnim();
		if (Current == HitClip && Previous != HitClip)
		{
			++HitStarts;
		}
		Previous = Current;
	}
	Report(*this, FString::Printf(TEXT("피격 클립 길이 %.2f초 · %.2f초 동안 걸린 횟수 %d · 끝난 뒤 클립 %s"),
		HitSeconds, HitSeconds * 2.f, HitStarts, Previous != nullptr ? *Previous->GetName() : TEXT("(없음)")));
	TestEqual(TEXT("피격 클립은 한 번만 걸린다"), HitStarts, 1);
	TestTrue(TEXT("클립이 끝나면 피격 클립이 아니다"), Previous != HitClip);

	DestroyTestWorld(World);
	return true;
}

// M5-07d3 합격 ① — **반경 밖이면 안 다가온다.**
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalAggroOutsideTest,
	"Snarehaven.Animal.Aggro.StaysPutOutsideRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalAggroOutsideTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = SpawnAttackAnimal(*World, MakeAttackRow(EAnimalBehaviorKind::Aggressive));
	ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(TestAggroRadiusCm + 50.f, 0.f, 0.f));
	if (Animal == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·플레이어를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	const FVector Start = Animal->GetActorLocation();
	const int32 Steps = FMath::RoundToInt(TestRunSeconds / FrameSeconds);
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		TickAnimalOnce(*Animal, FrameSeconds);
	}
	const float Moved = FVector::Dist2D(Animal->GetActorLocation(), Start);
	Report(*this, FString::Printf(TEXT("반경 %.0fcm 밖 %.0fcm 의 사람 · %.1f초 동안 움직인 거리 %.1fcm · 쫓음 %s"),
		TestAggroRadiusCm, TestAggroRadiusCm + 50.f, TestRunSeconds, Moved, Animal->IsAggroed() ? TEXT("예") : TEXT("아니오")));
	TestFalse(TEXT("반경 밖이면 쫓지 않는다"), Animal->IsAggroed());
	TestTrue(TEXT("반경 밖이면 제자리다"), Moved < 1.f);
	TestEqual(TEXT("반경 밖이면 한 대도 안 친다"), Animal->GetLandedAttackCount(), 0);

	DestroyTestWorld(World);
	return true;
}

// M5-07d3 합격 ② — **반경 안에 들면 다가와 사거리 안에서 친다.**
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalAggroInsideTest,
	"Snarehaven.Animal.Aggro.ApproachesInsideRadiusAndHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalAggroInsideTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = SpawnAttackAnimal(*World, MakeAttackRow(EAnimalBehaviorKind::Aggressive));
	const float StartGap = TestAggroRadiusCm - 50.f;
	ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(StartGap, 0.f, 0.f));
	if (Animal == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·플레이어를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	const float HealthBefore = GetHealth(*Player);
	float Elapsed = 0.f;
	while (Animal->GetLandedAttackCount() == 0 && Elapsed < TestChaseLimitSeconds)
	{
		TickAnimalOnce(*Animal, FrameSeconds);
		Elapsed += FrameSeconds;
	}
	const float Gap = FVector::Dist2D(Animal->GetActorLocation(), Player->GetActorLocation());
	Report(*this, FString::Printf(TEXT("반경 %.0fcm 안 %.0fcm 의 사람 · %.2f초 뒤 첫 대 · 거리 %.1fcm (사거리 %.0fcm) · 체력 %.1f → %.1f"),
		TestAggroRadiusCm, StartGap, Elapsed, Gap, TestAttackRangeCm, HealthBefore, GetHealth(*Player)));
	TestTrue(TEXT("반경 안이면 쫓는다"), Animal->IsAggroed());
	TestTrue(TEXT("실제로 다가왔다"), Gap < StartGap - 1.f);
	TestTrue(TEXT("사거리 안까지 와서 쳤다"), Animal->GetLandedAttackCount() > 0 && Gap <= TestAttackRangeCm);
	TestEqual(TEXT("맞은 만큼 체력이 준다"), GetHealth(*Player), HealthBefore - TestAttackDamage, KINDA_SMALL_NUMBER);

	// 접근 속도는 행의 MoveSpeed 다 — 쫓아온 시간 × 속도를 넘게 오지 않았다.
	TestTrue(TEXT("행의 이동 속도보다 빨리 오지 않았다"), StartGap - Gap <= TestMoveSpeed * Elapsed + 1.f);

	DestroyTestWorld(World);
	return true;
}

// M5-07d3 합격 ③ — **싸움 시작 자리에서 한계 밖으로 나가면 포기하고 돌아가 Idle, 체력은 그대로.**
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalAggroGiveUpTest,
	"Snarehaven.Animal.Aggro.GivesUpBeyondLeashAndReturnsHome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalAggroGiveUpTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = SpawnAttackAnimal(*World, MakeAttackRow(EAnimalBehaviorKind::Aggressive));
	ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(TestAggroRadiusCm - 50.f, 0.f, 0.f));
	if (Animal == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·플레이어를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	// 선공으로 쫓기 시작하고, 한 대 맞아 싸움까지 붙는다 — 두 길 다 같은 끈에 묶여야 한다.
	TickAnimalOnce(*Animal, FrameSeconds);
	TestTrue(TEXT("반경 안이라 쫓기 시작했다"), Animal->IsAggroed());
	const FVector Home = Animal->GetFightHome();
	UAnimalStateComponent* const State = Animal->GetAnimalState();
	State->ApplyDamage(TestNickDamage);
	TestEqual(TEXT("맞으면 Fight"), State->GetState(), EAnimalState::Fight);
	TestTrue(TEXT("맞아도 시작 자리는 쫓기 시작한 자리 그대로"), Animal->GetFightHome().Equals(Home, 1.f));
	const float HPAfterNick = State->GetCurrentHP();

	// 사람이 훌쩍 멀리 달아났다 — 동물은 쫓다가 한계에서 그만둬야 한다.
	Player->SetActorLocation(FVector(TestFarAwayCm, 0.f, 0.f));
	float Elapsed = 0.f;
	float FarthestFromHome = 0.f;
	while (!Animal->IsReturningHome() && Elapsed < TestChaseLimitSeconds)
	{
		TickAnimalOnce(*Animal, FrameSeconds);
		Elapsed += FrameSeconds;
		FarthestFromHome = FMath::Max(FarthestFromHome, FVector::Dist2D(Animal->GetActorLocation(), Home));
	}
	TestTrue(TEXT("쫓다가 포기했다"), Animal->IsReturningHome());
	TestTrue(TEXT("포기한 자리는 도주 한계 밖이다"), Animal->IsBeyondFleeLeash());
	TestEqual(TEXT("포기하면 Idle"), State->GetState(), EAnimalState::Idle);
	const float GiveUpSeconds = Elapsed;
	const float GiveUpDistance = FVector::Dist2D(Animal->GetActorLocation(), Home);

	while (Animal->IsReturningHome() && Elapsed < TestChaseLimitSeconds * 2.f)
	{
		TickAnimalOnce(*Animal, FrameSeconds);
		Elapsed += FrameSeconds;
	}
	const float HomeGap = FVector::Dist2D(Animal->GetActorLocation(), Home);
	const float ArriveRadius = Animal->GetCapsuleComponent()->GetScaledCapsuleRadius();
	Report(*this, FString::Printf(TEXT("포기 %.2f초 · 시작 자리에서 %.1fcm (최대 %.1fcm) · 귀환 %.2f초 · 남은 거리 %.1fcm (도착 반경 %.1fcm) · 체력 %.1f → %.1f"),
		GiveUpSeconds, GiveUpDistance, FarthestFromHome, Elapsed - GiveUpSeconds, HomeGap, ArriveRadius, HPAfterNick, State->GetCurrentHP()));
	TestFalse(TEXT("시작 자리에 돌아왔다"), Animal->IsReturningHome());
	TestTrue(TEXT("시작 자리 반경 안이다"), HomeGap <= ArriveRadius + 1.f);
	TestEqual(TEXT("돌아와서 Idle"), State->GetState(), EAnimalState::Idle);
	TestEqual(TEXT("체력은 그대로 (회복·감소 없음)"), State->GetCurrentHP(), HPAfterNick);
	TestFalse(TEXT("돌아온 뒤 멀리 있는 사람을 다시 쫓지 않는다"), Animal->IsAggroed());

	DestroyTestWorld(World);
	return true;
}

// M5-07d3 합격 ④ — **새 4종(데미지 0)은 한 번도 안 친다.** 진짜 표에서 데미지 0 인 행을 전부 골라,
// 맞혀도 · 갈래를 선공으로 바꿔 사람 곁에 세워도 친 횟수가 0 이고 플레이어 체력이 그대로인지 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalZeroDamageTest,
	"Snarehaven.Animal.Aggro.ZeroDamageRowsNeverHit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalZeroDamageTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("동물 테이블을 불러왔다"), Table))
	{
		return false;
	}

	TArray<FName> ZeroRows;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		if (reinterpret_cast<const FAnimalRow*>(Pair.Value)->AttackDamage == 0.f)
		{
			ZeroRows.Add(Pair.Key);
		}
	}
	TestTrue(TEXT("표에 데미지 0 인 행이 있다 (새 4종)"), ZeroRows.Num() > 0);

	const float CloseCm = TestAttackRangeCm * 0.5f;
	const int32 Steps = FMath::RoundToInt(TestRunSeconds / TestStepSeconds);
	for (const FName& RowName : ZeroRows)
	{
		UWorld* World = CreateTestWorld();
		AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(CloseCm, 0.f, 0.f));
		if (Animal == nullptr || Player == nullptr)
		{
			AddError(TEXT("동물·플레이어를 못 세웠다"));
			DestroyTestWorld(World);
			return false;
		}
		Animal->SetAnimalData(Table, RowName);
		TestTrue(FString::Printf(TEXT("%s: 표 행으로 초기화했다"), *RowName.ToString()), Animal->InitializeFromTable());
		Animal->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		const float HealthBefore = GetHealth(*Player);

		// 그대로의 행: 맞혀서 싸움을 붙여도 안 친다.
		Animal->GetAnimalState()->ApplyDamage(TestNickDamage);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			Player->SetActorLocation(Animal->GetActorLocation() + FVector(CloseCm, 0.f, 0.f));
			TickAnimalOnce(*Animal, TestStepSeconds);
		}

		// 갈래만 선공으로 바꾼 행: 데미지 0 이 막아야 한다 (갈래가 틀려도 사람을 치지 않는다).
		FAnimalRow AggressiveRow = *Table->FindRow<FAnimalRow>(RowName, TEXT("ZeroDamageRowsNeverHit"));
		AggressiveRow.BehaviorKind = EAnimalBehaviorKind::Aggressive;
		AggressiveRow.AggroRadiusCm = TestAggroRadiusCm;
		Animal->ApplyAttackFromRow(AggressiveRow);
		Animal->GetAnimalState()->InitializeFromRow(AggressiveRow);
		Player->SetActorLocation(Animal->GetActorLocation() + FVector(CloseCm, 0.f, 0.f));
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			TickAnimalOnce(*Animal, TestStepSeconds);
		}

		Report(*this, FString::Printf(TEXT("%s: 데미지 0 · 친 횟수 %d · 쫓음 %s · 플레이어 체력 %.1f → %.1f"),
			*RowName.ToString(), Animal->GetLandedAttackCount(), Animal->IsAggroed() ? TEXT("예") : TEXT("아니오"),
			HealthBefore, GetHealth(*Player)));
		TestEqual(FString::Printf(TEXT("%s: 한 번도 안 쳤다"), *RowName.ToString()), Animal->GetLandedAttackCount(), 0);
		TestFalse(FString::Printf(TEXT("%s: 선공으로 바꿔도 쫓지 않는다"), *RowName.ToString()), Animal->IsAggroed());
		TestEqual(FString::Printf(TEXT("%s: 플레이어 체력이 그대로"), *RowName.ToString()), GetHealth(*Player), HealthBefore);
		DestroyTestWorld(World);
	}
	return true;
}


// M43-04 합격 — **덤빔 소리는 대상을 처음 잡을 때 1번, 쫓는 동안엔 다시 안 나고, 공격 동작에서 또 운다.**
// 도주 종(꿩 자리)은 덤빔 칸을 채워 둬도 0 — 행동과 소리가 어긋나지 않는다 (DESIGN 8.7 「덤빔이 나는 순간」).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalAggroVoiceTest,
	"Snarehaven.Animal.Sound.AggroVoice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalAggroVoiceTest::RunTest(const FString& Parameters)
{
	const FName AggroCue(TEXT("AnimalHunterAggro"));
	constexpr float TestHoldSeconds = 1.f;   // 사거리 밖에서 쫓기만 하는 시간 — 접근 250→120cm 는 속도 40 으로 3초 넘게 걸린다

	UWorld* World = CreateTestWorld();
	FAnimalRow WolfRow = MakeAttackRow(EAnimalBehaviorKind::Aggressive);
	WolfRow.AggroSoundCue = AggroCue;
	AAnimalCharacter* Wolf = SpawnAttackAnimal(*World, WolfRow);
	const float StartGap = TestAggroRadiusCm - 50.f;
	ASnarehavenCharacter* Player = SpawnTestPlayer(*World, FVector(StartGap, 0.f, 0.f));
	if (Wolf == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·플레이어를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}
	Wolf->ApplySoundFromRow(WolfRow);

	TestEqual(TEXT("잡기 전엔 덤빔 소리 0"), Wolf->GetVoiceRequestCount(AggroCue), 0);

	// ① 반경 안 사람을 잡는 첫 프레임 = 1번. 테스트 월드는 시각을 스스로 안 흘리니 프레임마다 손으로 민다.
	TickAnimalOnce(*Wolf, FrameSeconds);
	World->TimeSeconds += FrameSeconds;
	TestTrue(TEXT("반경 안이라 대상을 잡았다"), Wolf->IsAggroed());
	TestEqual(TEXT("대상을 잡으면 덤빔 소리 1"), Wolf->GetVoiceRequestCount(AggroCue), 1);

	// ② 대상을 유지한 채 쫓는 동안(간격 0.6초를 넘겨도) 다시 안 운다.
	float Elapsed = 0.f;
	while (Elapsed < TestHoldSeconds)
	{
		TickAnimalOnce(*Wolf, FrameSeconds);
		World->TimeSeconds += FrameSeconds;
		Elapsed += FrameSeconds;
	}
	TestEqual(TEXT("아직 한 대도 안 쳤다 (사거리 밖 추격 중)"), Wolf->GetLandedAttackCount(), 0);
	TestTrue(TEXT("대상 유지 중"), Wolf->IsAggroed());
	TestEqual(TEXT("대상 유지 중엔 다시 안 남"), Wolf->GetVoiceRequestCount(AggroCue), 1);

	// ③ 사거리에 닿아 공격 동작을 시작하면 또 운다.
	while (Wolf->GetLandedAttackCount() == 0 && Elapsed < TestChaseLimitSeconds)
	{
		TickAnimalOnce(*Wolf, FrameSeconds);
		World->TimeSeconds += FrameSeconds;
		Elapsed += FrameSeconds;
	}
	Report(*this, FString::Printf(TEXT("덤빔 소리: 잡음 1 · 추격 %.2f초 동안 그대로 · %.2f초에 첫 대 → %d번"),
		TestHoldSeconds, Elapsed, Wolf->GetVoiceRequestCount(AggroCue)));
	TestEqual(TEXT("첫 공격 동작 = 덤빔 소리 2"), Wolf->GetVoiceRequestCount(AggroCue), 2);

	// ④ 도주 종 — 덤빔 칸을 채워 둬도 사람 곁에서 한 번도 안 운다.
	FAnimalRow PheasantRow = MakeAttackRow(EAnimalBehaviorKind::Flee);
	PheasantRow.AggroSoundCue = AggroCue;
	AAnimalCharacter* Pheasant = SpawnAttackAnimal(*World, PheasantRow);
	if (!TestNotNull(TEXT("도주 종 스폰"), Pheasant))
	{
		DestroyTestWorld(World);
		return false;
	}
	Pheasant->ApplySoundFromRow(PheasantRow);
	Player->SetActorLocation(Pheasant->GetActorLocation() + FVector(TestAttackRangeCm * 0.5f, 0.f, 0.f));
	for (float Step = 0.f; Step < TestRunSeconds; Step += FrameSeconds)
	{
		TickAnimalOnce(*Pheasant, FrameSeconds);
		World->TimeSeconds += FrameSeconds;
	}
	TestFalse(TEXT("도주 종은 대상을 안 잡는다"), Pheasant->IsAggroed());
	TestEqual(TEXT("도주 종 덤빔 소리 0"), Pheasant->GetVoiceRequestCount(AggroCue), 0);

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
