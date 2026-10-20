// ROADMAP M5-01c 2층 검사 — 플레이어의 보이는 몸이 자리표시 실린더에서 사람 스켈레탈 메시로
// 바뀌는지, 그리고 속도·공격에 따라 도는 클립이 갈리는지.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "Player/PlayerBodyAnimInstance.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/PlayerVitalsRow.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Data/ItemRow.h"
#include "Equipment/EquipmentComponent.h"
#include "Player/PlayerVitalsComponent.h"
#include "TimerManager.h"
#include "CoreGlobals.h"

namespace
{
	// M5-01a·M5-01b 가 임포트한 에셋. 경로·이름은 engines/unreal.md 폴더 구조·접두어 표 그대로.
	const TCHAR* PlayerBodyAssetName = TEXT("SK_Player_Body");
	const TCHAR* PlayerIdleAssetName = TEXT("A_Player_Idle");
	const TCHAR* PlayerWalkAssetName = TEXT("A_Player_Walk");
	const TCHAR* PlayerRunAssetName = TEXT("A_Player_Run");
	// M18-11: 맨손 휘두르기는 `DT_PlayerCombatTable` 의 잽(홀수 번째)·크로스(짝수 번째). 맨손 든 자세는 없다(루프#399 —
	// 주먹 가드가 상체를 늘 덮어 복싱 자세·고개 숙임이 됐다, DESIGN 3.3 「맨손 든 자세」). 상체 행동 칸이 비면 이름은 None.
	const TCHAR* PlayerJabAssetName = TEXT("A_Player_Jab");
	const TCHAR* PlayerCrossAssetName = TEXT("A_Player_Cross");
	const TCHAR* NoClipName = TEXT("None");
	const TCHAR* PlayerDeathAssetName = TEXT("A_Player_Death");

	// 검사 전용 속도다 — 게임 규칙 숫자가 아니라 "0 이 아닌 값" 이면 된다 (Verification.md 6).
	constexpr float TestMoveSpeedCmPerSecond = 300.f;

	// 공격 설정도 검사 전용 — DESIGN 의 데미지 20·사거리 150·간격 0.6 이 바뀌어도 안 깨지게.
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 200.f;
	// 클립 길이(지금 0.600초 — 감독 `club_swing`, 루프#161)와 뚜렷이 다른 값이라야 재생 속도 보정이
	// 빠졌을 때 검사가 크게 어긋난다. 0.6 으로 두면 보정 없이도 맞아떨어져 검사가 눈을 감는다
	// (옛 클립 1.033초 시절에 1.0 을 피한 것과 같은 이유).
	constexpr float TestAttackIntervalSeconds = 0.5f;

	// 아래 셋도 검사 전용이다 — 경계 바로 아래를 만드는 여유, 절반을 뜻하는 비, 소수 비교 여유.
	constexpr float InputEpsilon = 0.01f;
	constexpr float HalfSpeedRatio = 0.5f;
	constexpr float PlayRateTolerance = 0.01f;

	// 스태미나 고갈 검사 전용 — 한 번에 밀 시간과, 무한 루프를 막는 상한 (Verification.md 6).
	constexpr float SpeedTolerance = 0.1f;
	constexpr float DrainStepSeconds = 0.25f;
	constexpr int32 DrainStepLimit = 400;
	// 소모가 살아 있는 행을 지어 먹일 때 쓰는 값 — 위 두 상수로 몇 걸음이면 바닥나는 크기다.
	constexpr float TestVitalsMax = 40.f;
	constexpr float TestVitalsDrainPerSecond = 20.f;

	// 히스테리시스 검사 전용 — 문턱에 딱 걸치지 않게 비켜서는 여유(cm/s)와, 속도를 떨게 할 표본 수.
	// 게임 규칙 숫자가 아니라 "문턱 안쪽" 을 만드는 값이다 (Verification.md 6).
	constexpr float BandEdgeMarginCmPerSecond = 0.1f;
	constexpr int32 BandSampleCount = 8;

	// 이동 버프 검사 전용 (13.6 표 값이 아니다 — 표를 고쳐도 검사가 안 깨지게, Verification.md 6).
	constexpr float TestSpeedBuffValue = 0.5f;
	constexpr float TestSpeedBuffSeconds = 0.05f;

	// 쓰러짐 검사 전용 값 (Verification.md 6 — DESIGN 3.1 의 1.0 이 아니다. 설계값을 베끼면
	// 사람이 표를 고칠 때 검사가 깨지고, 코드가 1.0 을 박아 넣어도 안 잡힌다).
	constexpr float TestDeathLingerSeconds = 0.4f;
	constexpr float TestDeathLingerAltSeconds = 0.9f;   // 표를 갈면 시간이 따라오는지 보는 둘째 값
	constexpr float TestDownedStepSeconds = 0.02f;
	constexpr int32 TestDownedStepLimit = 2000;         // 무한 루프 막이 (약 40초어치)
	constexpr float TestDownedTolerance = 0.1f;         // 감독 지시 2026-09-23 16:10 의 ±0.1초

	UWorld* CreatePlayerTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	/** 쓰러짐 검사 전용 행. 숫자는 전부 검사용이고, 누운 시간만 인자로 갈아 끼운다. */
	FPlayerVitalsRow MakeDownedTestRow(float LingerSeconds)
	{
		FPlayerVitalsRow Row;
		Row.MaxHealth = TestVitalsMax;
		Row.MaxStamina = TestVitalsMax;
		Row.RunStaminaPerSecond = TestVitalsDrainPerSecond;
		Row.AttackStaminaCost = 0.f;
		Row.StaminaRegenPerSecond = 0.f;
		Row.StaminaRegenDelaySeconds = 0.f;
		Row.ExhaustionHealthPerSecond = TestVitalsDrainPerSecond;   // 스태미나가 마르면 곧 죽는다
		Row.HealthRegenPerSecond = 0.f;
		Row.HealthRegenStaminaThreshold = 0.f;
		Row.RespawnHealth = TestVitalsMax;
		Row.RespawnStamina = TestVitalsMax;
		Row.DeathLingerSeconds = LingerSeconds;
		return Row;
	}

	void DestroyPlayerTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerBodyMeshTest, "Snarehaven.Player.Body.MeshReplacesPlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerBodyMeshTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreatePlayerTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyPlayerTestWorld(World);
		return false;
	}

	// 몸을 올리기 전에는 자리표시가 보인다 — 이게 없으면 M0-06 처럼 캡처에 바닥만 찍힌다.
	TestTrue(TEXT("처음엔 자리표시가 보인다"), Player->GetPlaceholderBody()->IsVisible());

	TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody());

	USkeletalMesh* const Body = Player->GetMesh()->GetSkeletalMeshAsset();
	if (Body == nullptr)
	{
		AddError(TEXT("스켈레탈 메시가 안 붙었다 — M5-01a 에셋을 못 찾았을 수 있다"));
		DestroyPlayerTestWorld(World);
		return false;
	}
	TestEqual(TEXT("M5-01a 가 임포트한 몸이다"), Body->GetName(), FString(PlayerBodyAssetName));

	// 둘 다 보이면 실린더가 사람을 뚫고 나온다 (동물과 같은 규칙).
	TestFalse(TEXT("자리표시는 숨었다"), Player->GetPlaceholderBody()->IsVisible());
	// M18-10: SingleNode(클립 하나) 대신 코드 애님 인스턴스가 기본·행동 두 층을 섞는다 — 애님 BP 에셋은 없다.
	TestEqual(TEXT("코드 애님 인스턴스 모드"), Player->GetMesh()->GetAnimationMode(),
		EAnimationMode::AnimationBlueprint);
	TestNotNull(TEXT("몸에 섞기 애님 인스턴스가 붙었다"), Player->GetBodyAnimInstance());

	// 가만히 서 있으면 Idle — 클립이 없으면 T 포즈로 굳는다.
	if (const UAnimSequence* const Current = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("서 있을 때는 Idle 클립"), Current->GetName(), FString(PlayerIdleAssetName));
	}
	else
	{
		AddError(TEXT("서 있는데 도는 클립이 없다 — M5-01b 에셋을 못 찾았을 수 있다"));
	}

	// 몸이 캡슐보다 크면 카메라 합격폭(M0-04)이 달라진다 — 세로를 캡슐에 맞췄는지 본다.
	const float CapsuleHeight = Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight() * 2.f;
	const float ScaledMeshHeight = Body->GetBounds().BoxExtent.Z * 2.f * Player->GetMesh()->GetRelativeScale3D().Z;
	TestTrue(TEXT("몸 세로가 캡슐 세로와 같다"), FMath::IsNearlyEqual(ScaledMeshHeight, CapsuleHeight, 1.f));

	DestroyPlayerTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerBodyClipSwitchTest, "Snarehaven.Player.Body.ClipFollowsMoveAndAttack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerBodyClipSwitchTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreatePlayerTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyPlayerTestWorld(World);
		return false;
	}

	if (!Player->ApplyPlayerBody())
	{
		AddError(TEXT("사람 메시를 못 올렸다 — 아래 클립 검사는 뜻이 없다"));
		DestroyPlayerTestWorld(World);
		return false;
	}

	// 움직이면 Walk. 속도는 이동 컴포넌트에 직접 넣는다 — 입력 길(M0-03)은 다른 검사가 본다.
	Player->GetCharacterMovement()->Velocity = FVector(TestMoveSpeedCmPerSecond, 0.f, 0.f);
	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const Moving = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("움직이면 Walk 클립"), Moving->GetName(), FString(PlayerWalkAssetName));
	}
	else
	{
		AddError(TEXT("움직이는데 도는 클립이 없다"));
	}

	// 때리는 동안은 걸어도 Attack 이 이긴다 (공격이 안 보이면 뭘 하는지 알 수 없다).
	// 간격은 검사 전용 값으로 덮는다 — DESIGN 의 0.6 초가 바뀌어도 이 검사는 안 깨져야 한다.
	Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
	Player->GetMeleeAttack()->StartAttacking();
	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const Attacking = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("맨손 첫 휘두르기는 잽 클립"), Attacking->GetName(), FString(PlayerJabAssetName));

		// 한 바퀴 = 공격 간격이라야 타격 한 번에 휘두르기 한 번이 보인다 (실플레이 결함 ③).
		// 재생 속도 자체가 아니라 **그 속도로 도는 한 바퀴의 시간**을 본다 — 클립을 갈아 끼워도 뜻이 같다.
		const float SwingSeconds = Attacking->GetPlayLength() / Player->GetBodyAnimPlayRate();
		TestEqual(TEXT("휘두르기 한 바퀴가 공격 간격과 같다"), SwingSeconds, TestAttackIntervalSeconds, PlayRateTolerance);
	}
	else
	{
		AddError(TEXT("때리는데 도는 클립이 없다"));
	}

	// 손을 떼도 **시작한 휘두르기 한 바퀴는 끝까지** 돈다 — 떼는 프레임에 대기로 갈아 끼우던 때가
	// 사용자가 본 "몇 프레임만 돌고 툭 끊긴다" 였다 (결함 2026-09-23 16:40 ①).
	Player->GetMeleeAttack()->StopAttacking();
	Player->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const StillSwinging = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("떼도 한 바퀴가 끝날 때까지는 잽 클립"),
			StillSwinging->GetName(), FString(PlayerJabAssetName));
	}
	else
	{
		AddError(TEXT("뗀 직후에 도는 클립이 없다"));
	}

	// 한 바퀴(= 공격 간격)가 지나면 다시 Idle. 시간은 타이머가 재므로 타이머만 흘린다.
	// `GFrameCounter` 를 같이 올려야 한다 — `FTimerManager::Tick` 은 한 프레임에 한 번만 일한다.
	constexpr float TimerStepSeconds = 0.01f;
	const int32 SwingSteps = FMath::RoundToInt(TestAttackIntervalSeconds * 2.f / TimerStepSeconds);
	for (int32 Step = 0; Step < SwingSteps; ++Step)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(TimerStepSeconds);
	}
	TestFalse(TEXT("한 바퀴가 지나면 휘두르기가 끝난다"), Player->GetMeleeAttack()->IsSwinging());

	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const Resting = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("휘두르기가 끝나면 다시 Idle 클립"), Resting->GetName(), FString(PlayerIdleAssetName));
	}
	else
	{
		AddError(TEXT("멈췄는데 도는 클립이 없다"));
	}

	DestroyPlayerTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerRunSplitTest, "Snarehaven.Player.Body.RunSplitsByInputSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerRunSplitTest::RunTest(const FString& Parameters)
{
	// 조이스틱을 민 크기로 걷기·달리기가 갈리고(감독 결정 0.9), 클립 재생 속도가 실제 속도를 따라가는지.
	// 숫자는 여기에 안 적는다 — 경계는 `GetRunInputThreshold`, 기준 속도는 `MaxWalkSpeed` 에서 읽는다
	// (클립에서 잰 값이라 검사가 베끼면 두 곳이 갈린다 · DataDriven.md 3).
	UWorld* World = CreatePlayerTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyPlayerTestWorld(World);
		return false;
	}

	if (!Player->ApplyPlayerBody())
	{
		AddError(TEXT("사람 메시를 못 올렸다 — 아래 클립 검사는 뜻이 없다"));
		DestroyPlayerTestWorld(World);
		return false;
	}

	UCharacterMovementComponent* const Movement = Player->GetCharacterMovement();
	const float Threshold = Player->GetRunInputThreshold();

	// 경계 바로 아래 — 걷기. 실제 속도를 걷기 기준 속도와 같게 두면 클립은 원래 빠르기로 돈다.
	Player->MoveByAxis(FVector2D(0.f, Threshold - InputEpsilon));
	Player->UpdateMoveSpeed();
	const float WalkMaxSpeed = Movement->MaxWalkSpeed;
	TestTrue(TEXT("걷기 최고 속도가 0 보다 크다"), WalkMaxSpeed > 0.f);
	TestTrue(TEXT("반 밀기 최소 속도가 켜져 있고(0 초과) 걷기 최고를 넘지 않는다"),
		Movement->MinAnalogWalkSpeed > 0.f && Movement->MinAnalogWalkSpeed <= WalkMaxSpeed);
	TestTrue(TEXT("반 밀기 최소 속도가 걷기 최고의 절반보다 크다(기어가지 않는다)"), Movement->MinAnalogWalkSpeed > WalkMaxSpeed * 0.5f);

	Movement->Velocity = FVector(WalkMaxSpeed, 0.f, 0.f);
	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const Walking = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("경계 아래로 밀면 Walk 클립"), Walking->GetName(), FString(PlayerWalkAssetName));
	}
	else
	{
		AddError(TEXT("걷는데 도는 클립이 없다"));
	}
	TestEqual(TEXT("기준 속도로 걸으면 재생 속도 1"), Player->GetBodyAnimPlayRate(), 1.f, PlayRateTolerance);

	// 반만큼 나아가면 클립도 반만큼 느리게 — 이게 발 미끄러짐 보정이다.
	Movement->Velocity = FVector(WalkMaxSpeed * HalfSpeedRatio, 0.f, 0.f);
	Player->UpdateBodyAnimation();
	TestEqual(TEXT("반 속도면 재생 속도도 반"), Player->GetBodyAnimPlayRate(), HalfSpeedRatio, PlayRateTolerance);

	// 경계 위 — 달리기. 최고 속도도 같이 올라간다.
	Player->MoveByAxis(FVector2D(0.f, Threshold));
	Player->UpdateMoveSpeed();
	const float RunMaxSpeed = Movement->MaxWalkSpeed;
	TestTrue(TEXT("달리기 최고 속도가 걷기보다 빠르다"), RunMaxSpeed > WalkMaxSpeed);

	Movement->Velocity = FVector(RunMaxSpeed, 0.f, 0.f);
	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const Running = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("경계 이상으로 밀면 Run 클립"), Running->GetName(), FString(PlayerRunAssetName));
	}
	else
	{
		AddError(TEXT("달리는데 도는 클립이 없다 — A_Player_Run 을 못 찾았을 수 있다"));
	}
	TestEqual(TEXT("기준 속도로 달리면 재생 속도 1"), Player->GetBodyAnimPlayRate(), 1.f, PlayRateTolerance);

	DestroyPlayerTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerIdleWalkHysteresisTest, "Snarehaven.Player.Body.IdleWalkUsesHysteresis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerIdleWalkHysteresisTest::RunTest(const FString& Parameters)
{
	// 걸을 때 멈칫하던 원인 검사 (감독 결함 2026-09-22 14:10). 문턱 하나로 Idle/걷기를 가르면
	// 가속·감속 중 속도가 그 하나를 여러 번 넘나들며 클립이 매번 프레임 0 으로 되감긴다.
	// 여기서 세는 것은 **클립이 바뀐 횟수** 다 — 되감김은 바뀔 때만 일어나므로 이것이 멈칫임의 수다.
	// 문턱 값은 여기 안 적는다 — `GetMoveAnimEnterSpeed`·`GetMoveAnimExitSpeed` 에서 읽는다.
	UWorld* World = CreatePlayerTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyPlayerTestWorld(World);
		return false;
	}

	if (!Player->ApplyPlayerBody())
	{
		AddError(TEXT("사람 메시를 못 올렸다 — 아래 클립 검사는 뜻이 없다"));
		DestroyPlayerTestWorld(World);
		return false;
	}

	UCharacterMovementComponent* const Movement = Player->GetCharacterMovement();
	const float EnterSpeed = Player->GetMoveAnimEnterSpeed();
	const float ExitSpeed = Player->GetMoveAnimExitSpeed();

	// 이게 깨지면 히스테리시스가 아니라 문턱 하나짜리로 돌아간 것이다.
	TestTrue(TEXT("나오는 문턱이 들어가는 문턱보다 낮다"), ExitSpeed < EnterSpeed);

	// ① 멈춤에서 시작 — Idle.
	Movement->Velocity = FVector::ZeroVector;
	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const Resting = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("멈춰 있으면 Idle 클립"), Resting->GetName(), FString(PlayerIdleAssetName));
	}
	else
	{
		AddError(TEXT("멈췄는데 도는 클립이 없다"));
	}

	// ② 두 문턱 사이로 올라가도 아직 Idle — 들어가는 문턱을 안 넘었다.
	Movement->Velocity = FVector(EnterSpeed - BandEdgeMarginCmPerSecond, 0.f, 0.f);
	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const StillResting = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("문턱 사이로 올라가도 Idle 그대로"), StillResting->GetName(), FString(PlayerIdleAssetName));
	}

	// ③ 출발할 때 속도가 0 과 문턱 아래를 오가도 클립은 한 번도 안 바뀐다.
	//    **문턱 하나였을 때는 표본마다 갈려 여기서 8번 바뀌었다** — 그게 눈에 보이던 멈칫임이다.
	const UAnimSequence* Previous = Player->GetCurrentBodyAnim();
	int32 StartJitterChanges = 0;
	for (int32 Sample = 0; Sample < BandSampleCount; ++Sample)
	{
		const float Speed = (Sample % 2 == 0) ? 0.f : EnterSpeed - BandEdgeMarginCmPerSecond;
		Movement->Velocity = FVector(Speed, 0.f, 0.f);
		Player->UpdateBodyAnimation();
		if (Player->GetCurrentBodyAnim() != Previous)
		{
			++StartJitterChanges;
			Previous = Player->GetCurrentBodyAnim();
		}
	}
	TestEqual(TEXT("출발 구간에서 떨어도 클립이 안 바뀐다"), StartJitterChanges, 0);

	// ④ 들어가는 문턱을 넘으면 걷기.
	Movement->Velocity = FVector(EnterSpeed, 0.f, 0.f);
	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const Walking = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("들어가는 문턱을 넘으면 Walk 클립"), Walking->GetName(), FString(PlayerWalkAssetName));
	}
	else
	{
		AddError(TEXT("걷는데 도는 클립이 없다"));
	}

	// ⑤ 멈추는 중 속도가 두 문턱 사이에서 떨어도 걷기 그대로 — 나오는 문턱까지 내려가야 Idle 이다.
	Previous = Player->GetCurrentBodyAnim();
	int32 StopJitterChanges = 0;
	for (int32 Sample = 0; Sample < BandSampleCount; ++Sample)
	{
		const float Speed = (Sample % 2 == 0)
			? ExitSpeed + BandEdgeMarginCmPerSecond
			: EnterSpeed - BandEdgeMarginCmPerSecond;
		Movement->Velocity = FVector(Speed, 0.f, 0.f);
		Player->UpdateBodyAnimation();
		if (Player->GetCurrentBodyAnim() != Previous)
		{
			++StopJitterChanges;
			Previous = Player->GetCurrentBodyAnim();
		}
	}
	TestEqual(TEXT("멈추는 구간에서 떨어도 클립이 안 바뀐다"), StopJitterChanges, 0);
	if (const UAnimSequence* const StillWalking = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("문턱 사이에서는 Walk 그대로"), StillWalking->GetName(), FString(PlayerWalkAssetName));
	}

	// ⑥ 나오는 문턱까지 내려가면 그제야 Idle — 벌린 문턱이 걸려서 안 돌아오는 것은 아니다.
	Movement->Velocity = FVector(ExitSpeed, 0.f, 0.f);
	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const BackToIdle = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("나오는 문턱까지 내려가면 Idle"), BackToIdle->GetName(), FString(PlayerIdleAssetName));
	}

	DestroyPlayerTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerRunCostsNoStaminaTest,
	"Snarehaven.Player.Body.RunningCostsNoStamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M11-02 — **스태미나 = 수영 전용** (DESIGN 3.1, 사용자 방향 2026-09-23). 이 검사는 원래
// "스태미나 0 이면 걷기 강제" 를 보던 자리다. 표의 달리기 소모가 0 이 되어 뜻을 뒤집었다:
// ① 출하되는 표로는 **아무리 오래 달려도 스태미나가 안 줄고 달리기가 안 꺾인다**
// ② 걷기 강제 **길 자체는 살아 있다** — 소모값이 있는 검사 전용 행을 먹이면 예전처럼 꺾인다
//    (수영(8.9)이 그 길을 쓴다 — 지워 두면 수영 때 없어진 줄도 모른다).
// 숫자는 안 적는다: 기준 속도는 `MaxWalkSpeed`, 경계는 `GetRunInputThreshold`, 소모량은 표·검사 전용 행에서 온다.
bool FSnarehavenPlayerRunCostsNoStaminaTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreatePlayerTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyPlayerTestWorld(World);
		return false;
	}

	UPlayerVitalsComponent* const Vitals = Player->GetPlayerVitals();
	if (!TestNotNull(TEXT("플레이어에 체력·스태미나 컴포넌트가 붙어 있다"), Vitals))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}

	// 표를 못 읽으면 최대치가 미정(-1)으로 남는다 — 그러면 아래 검사는 뜻이 없다 (Verification.md 7).
	if (!TestTrue(TEXT("`DT_PlayerVitalsTable` 의 `Player` 행을 읽어 최대 스태미나가 정해졌다"),
		Vitals->GetMaxStamina() > 0.f))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}

	UCharacterMovementComponent* const Movement = Player->GetCharacterMovement();
	const float Threshold = Player->GetRunInputThreshold();

	Player->MoveByAxis(FVector2D(0.f, Threshold - InputEpsilon));
	Player->UpdateMoveSpeed();
	const float WalkMaxSpeed = Movement->MaxWalkSpeed;

	// 조이스틱을 끝까지 — 여기서부터 끝까지 손을 떼지 않는다.
	Player->MoveByAxis(FVector2D(0.f, 1.f));
	Player->UpdateMoveSpeed();
	const float RunMaxSpeed = Movement->MaxWalkSpeed;
	if (!TestTrue(TEXT("스태미나가 남았으면 달린다"), RunMaxSpeed > WalkMaxSpeed))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}

	// 13.6 이동 버프 — 발 속도 × (1 + 값), 시간이 끝나면 원래대로.
	Vitals->ApplyBuff(EBuffKind::Speed, TestSpeedBuffValue, TestSpeedBuffSeconds);
	Player->UpdateMoveSpeed();
	TestEqual(TEXT("이동 버프 = 달리기 × (1 + 값)"),
		Movement->MaxWalkSpeed, RunMaxSpeed * (1.f + TestSpeedBuffValue), SpeedTolerance);
	Vitals->AdvanceTime(TestSpeedBuffSeconds * 2.f);
	Player->UpdateMoveSpeed();
	TestEqual(TEXT("버프가 끝나면 원래 달리기"), Movement->MaxWalkSpeed, RunMaxSpeed, SpeedTolerance);

	// ① 출하되는 표로 계속 달린다 — 예전 값(5/초)이었다면 DrainStepLimit 의 일부만으로도 바닥났을 시간이다.
	const float StaminaBeforeRun = Vitals->GetStamina();
	for (int32 Steps = 0; Steps < DrainStepLimit; ++Steps)
	{
		Vitals->AdvanceTime(DrainStepSeconds);
	}
	TestEqual(TEXT("오래 달려도 스태미나가 그대로다"),
		Vitals->GetStamina(), StaminaBeforeRun, SpeedTolerance);
	TestTrue(TEXT("그래서 계속 달릴 수 있다"), Vitals->CanRun());

	Player->MoveByAxis(FVector2D(0.f, 1.f));
	Player->UpdateMoveSpeed();
	TestEqual(TEXT("끝까지 밀면 계속 달리기 속도다"),
		Movement->MaxWalkSpeed, RunMaxSpeed, SpeedTolerance);

	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const Clip = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("도는 클립도 Run 이다"), Clip->GetName(), FString(PlayerRunAssetName));
	}

	// ② 걷기 강제 길은 살아 있다 — 소모값이 있는 **검사 전용 행**(Verification.md 6)으로 다시 켜 본다.
	FPlayerVitalsRow SwimmingLikeRow;
	SwimmingLikeRow.MaxHealth = TestVitalsMax;
	SwimmingLikeRow.MaxStamina = TestVitalsMax;
	SwimmingLikeRow.RunStaminaPerSecond = TestVitalsDrainPerSecond;
	SwimmingLikeRow.AttackStaminaCost = 0.f;
	SwimmingLikeRow.StaminaRegenPerSecond = TestVitalsDrainPerSecond;
	SwimmingLikeRow.StaminaRegenDelaySeconds = 0.f;
	SwimmingLikeRow.ExhaustionHealthPerSecond = 0.f;
	SwimmingLikeRow.HealthRegenPerSecond = 0.f;
	SwimmingLikeRow.HealthRegenStaminaThreshold = 0.f;
	SwimmingLikeRow.RespawnHealth = TestVitalsMax;
	SwimmingLikeRow.RespawnStamina = TestVitalsMax;
	// M5-01e2 가 더한 칸. 이 검사는 달리기만 보지만 비워 두면 행이 미완성이라 컴포넌트가 안 켜진다.
	SwimmingLikeRow.DeathLingerSeconds = TestDeathLingerSeconds;
	if (!TestTrue(TEXT("검사 전용 행으로 다시 초기화"), Vitals->InitializeFromRow(SwimmingLikeRow)))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}

	Player->MoveByAxis(FVector2D(0.f, 1.f));
	Player->UpdateMoveSpeed();
	int32 Steps = 0;
	while (Vitals->GetStamina() > 0.f && Steps < DrainStepLimit)
	{
		Vitals->AdvanceTime(DrainStepSeconds);
		++Steps;
	}
	if (!TestTrue(TEXT("소모값이 있으면 달리는 동안 바닥난다"), Vitals->GetStamina() <= 0.f))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}

	Player->MoveByAxis(FVector2D(0.f, 1.f));
	Player->UpdateMoveSpeed();
	TestEqual(TEXT("그때는 끝까지 밀어도 걷기 속도다"),
		Movement->MaxWalkSpeed, WalkMaxSpeed, SpeedTolerance);

	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const Clip = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("걷기 강제면 도는 클립도 Walk 다"), Clip->GetName(), FString(PlayerWalkAssetName));
	}

	// 쉬면 다시 달릴 수 있어야 한다 — 한 번 바닥나고 영영 못 달리면 그건 버그다.
	Vitals->SetMoveInput(false);
	Vitals->SetRunInput(false);
	Steps = 0;
	while (!Vitals->CanRun() && Steps < DrainStepLimit)
	{
		Vitals->AdvanceTime(DrainStepSeconds);
		++Steps;
	}
	Player->MoveByAxis(FVector2D(0.f, 1.f));
	Player->UpdateMoveSpeed();
	TestEqual(TEXT("쉬어서 스태미나가 차면 다시 달린다"),
		Movement->MaxWalkSpeed, RunMaxSpeed, SpeedTolerance);

	DestroyPlayerTestWorld(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerDeathClipTest,
	"Snarehaven.Player.Body.DeathClipThenRevive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M5-01e2 — 죽으면 **쓰러짐 클립이 끝까지 돌고 누운 채로 표의 시간이 지난 뒤** 부활한다
// (DESIGN 3.1 「쓰러짐 → 부활」 · 감독 지시 2026-09-23 16:10 ③). 보는 것 넷:
//  ① 부활까지 걸리는 시간 = **클립 길이 + 표의 누운 시간** — 표를 갈면 따라온다(코드에 박힌 초 0개)
//  ② 쓰러진 동안 도는 클립은 `A_Player_Death` 이고 **되풀이하지 않는다**(마지막 프레임 = 누운 자세)
//  ③ 그동안 이동·공격 입력을 받지 않는다
//  ④ 시간이 다 되면 일어나 체력이 표의 부활 값으로 돌아오고 입력이 다시 먹는다
// 숫자는 검사 전용이다 (Verification.md 6) — 설계값 1.0 을 여기 적지 않는다.
bool FSnarehavenPlayerDeathClipTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreatePlayerTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr)
	{
		AddError(TEXT("플레이어 액터를 못 스폰했다"));
		DestroyPlayerTestWorld(World);
		return false;
	}
	// 사망 알림(`UPlayerVitalsComponent::OnDied`)을 캐릭터가 듣는 배선이 `BeginPlay` 에 있다.
	// 검사 월드에는 게임모드가 없어 `UWorld::BeginPlay` 가 `bBegunPlay` 를 세우지 않고
	// (엔진 `World.cpp` — `GetAuthGameMode()` 가 있을 때만 `StartPlay`), 그래서 스폰된 폰은
	// `BeginPlay` 를 못 받는다. 폰 하나만 손으로 시작시킨다.
	Player->DispatchBeginPlay();
	Player->ApplyPlayerBody();

	UPlayerVitalsComponent* const Vitals = Player->GetPlayerVitals();
	if (!TestNotNull(TEXT("플레이어에 체력·스태미나 컴포넌트가 붙어 있다"), Vitals))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}

	// ① 시간이 표에서 온다 — 누운 시간만 다른 두 행을 먹여 **차이만큼** 시간이 움직이는지 본다.
	//    (클립 길이는 두 행에서 같으니 차이는 오롯이 표 값이다.)
	if (!TestTrue(TEXT("검사 전용 행으로 초기화"),
		Vitals->InitializeFromRow(MakeDownedTestRow(TestDeathLingerSeconds))))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}
	const float ShortDuration = Player->GetDownedDurationSeconds();
	Vitals->InitializeFromRow(MakeDownedTestRow(TestDeathLingerAltSeconds));
	const float LongDuration = Player->GetDownedDurationSeconds();
	if (!TestTrue(TEXT("쓰러짐 시간을 잰다 (클립·표 둘 다 있다)"), ShortDuration > 0.f && LongDuration > 0.f))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}
	TestEqual(TEXT("표의 누운 시간을 갈면 부활까지의 시간이 그만큼 따라온다"),
		LongDuration - ShortDuration, TestDeathLingerAltSeconds - TestDeathLingerSeconds, KINDA_SMALL_NUMBER);

	// 짧은 쪽으로 되돌려 실제로 죽여 본다.
	Vitals->InitializeFromRow(MakeDownedTestRow(TestDeathLingerSeconds));

	// 스태미나를 말린 채 계속 밀어붙이면 탈진으로 죽는다 (DESIGN 3.1 — 체력이 닳는 길).
	Vitals->SetMoveInput(true);
	Vitals->SetRunInput(true);
	int32 Steps = 0;
	while (!Vitals->IsDead() && Steps < TestDownedStepLimit)
	{
		Vitals->AdvanceTime(TestDownedStepSeconds);
		++Steps;
	}
	if (!TestTrue(TEXT("탈진을 무시하고 계속 가면 죽는다"), Vitals->IsDead()))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}

	// ② 쓰러졌고, 도는 클립은 쓰러짐이다.
	if (!TestTrue(TEXT("죽으면 쓰러진다"), Player->IsDowned()))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}
	const UAnimSequence* const DownedClip = Player->GetCurrentBodyAnim();
	if (DownedClip == nullptr)
	{
		AddError(TEXT("쓰러졌는데 도는 클립이 없다 — M5-01e2 에셋(A_Player_Death)을 못 찾았을 수 있다"));
		DestroyPlayerTestWorld(World);
		return false;
	}
	TestEqual(TEXT("쓰러진 동안은 Death 클립"), DownedClip->GetName(), FString(PlayerDeathAssetName));
	if (TestNotNull(TEXT("섞기 애님 인스턴스"), Player->GetBodyAnimInstance()))
	{
		// 되풀이하면 누운 몸이 계속 다시 쓰러진다 — 누운 자세로 멈춰 있어야 한다.
		TestFalse(TEXT("쓰러짐 클립은 되풀이하지 않는다"), Player->GetBodyAnimInstance()->IsShownClipLooping());
	}
	// 시간은 클립에서 읽는다 — 검사도 클립 길이를 베끼지 않고 지금 도는 클립에서 잰다.
	TestEqual(TEXT("부활까지 = 클립 길이 + 표의 누운 시간"),
		Player->GetDownedDurationSeconds(), DownedClip->GetPlayLength() + TestDeathLingerSeconds,
		KINDA_SMALL_NUMBER);

	// ③ 그동안 입력이 안 먹는다 — 끝까지 밀어도 달리기 속도가 안 붙고 클립도 안 바뀐다.
	UCharacterMovementComponent* const Movement = Player->GetCharacterMovement();
	Player->MoveByAxis(FVector2D(0.f, 1.f));
	Player->UpdateMoveSpeed();
	const float DownedMaxSpeed = Movement->MaxWalkSpeed;
	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const StillDown = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("쓰러진 채로 밀어도 클립은 Death 그대로"),
			StillDown->GetName(), FString(PlayerDeathAssetName));
	}
	Player->BeginAttack();
	TestFalse(TEXT("쓰러진 채로는 공격이 시작되지 않는다"), Player->GetMeleeAttack()->IsAttacking());

	// ④ 시간이 다 되면 일어난다. 실제로 걸린 시간을 재서 위 ①의 값과 맞댄다.
	const float Expected = Player->GetDownedDurationSeconds();
	float Elapsed = 0.f;
	Steps = 0;
	while (Player->IsDowned() && Steps < TestDownedStepLimit)
	{
		++GFrameCounter;      // `FTimerManager::Tick` 은 한 프레임에 한 번만 일한다
		World->GetTimerManager().Tick(TestDownedStepSeconds);
		Elapsed += TestDownedStepSeconds;
		++Steps;
	}
	TestFalse(TEXT("시간이 지나면 일어난다"), Player->IsDowned());
	AddInfo(FString::Printf(TEXT("쓰러짐→부활 실측 %.2f초 (클립 %.2f + 누운 채 %.2f = %.2f초 기대)"),
		Elapsed, DownedClip->GetPlayLength(), TestDeathLingerSeconds, Expected));
	TestEqual(TEXT("걸린 시간이 클립 길이 + 누운 시간과 같다"), Elapsed, Expected, TestDownedTolerance);
	TestFalse(TEXT("부활하면 죽은 상태가 풀린다"), Vitals->IsDead());
	TestEqual(TEXT("체력이 표의 부활 값으로 돌아온다"), Vitals->GetHealth(), TestVitalsMax, SpeedTolerance);

	Player->UpdateBodyAnimation();
	if (const UAnimSequence* const Standing = Player->GetCurrentBodyAnim())
	{
		TestEqual(TEXT("일어나면 다시 Idle 클립"), Standing->GetName(), FString(PlayerIdleAssetName));
	}
	Player->MoveByAxis(FVector2D(0.f, 1.f));
	Player->UpdateMoveSpeed();
	TestTrue(TEXT("부활 뒤에는 입력이 다시 먹는다"), Movement->MaxWalkSpeed > DownedMaxSpeed);

	DestroyPlayerTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerBodyBlendTest, "Snarehaven.Player.Body.BlendsClipsAndUpperBodyAction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * M18-10 2층 — ① 클립이 바뀌면 툭 끊지 않고 섞는다, ② 걸으며 휘두르면 하체는 걷기·상체는 휘두르기,
 * ③ 서 있다가 걷기 시작하면 다리가 행동에서 걷기로 섞여 넘어간다. 뒤 둘은 **뼈 자세**로도 본다 —
 * 같은 걸음을 걷는 두 몸 중 하나만 휘두르게 해서 다리 뼈는 같고 팔 뼈는 달라야 한다.
 */
bool FSnarehavenPlayerBodyBlendTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 (Verification.md 6): 섞는 도중을 찍는 짧은 틱, 섞기가 다 끝나는 긴 틱, 뼈 비교 여유.
	constexpr float MidBlendTickSeconds = 0.05f;
	constexpr float SettleTickSeconds = 0.5f;
	constexpr float BoneTolerance = 0.001f;
	const FName LegBone(TEXT("LeftLeg"));
	const FName ArmBone(TEXT("RightForeArm"));

	UWorld* World = CreatePlayerTestWorld();
	ASnarehavenCharacter* const Walker = World->SpawnActor<ASnarehavenCharacter>();
	ASnarehavenCharacter* const Swinger = World->SpawnActor<ASnarehavenCharacter>();
	if (Walker == nullptr || Swinger == nullptr || !Walker->ApplyPlayerBody() || !Swinger->ApplyPlayerBody())
	{
		AddError(TEXT("플레이어 몸을 못 올렸다 — 아래 섞기 검사는 뜻이 없다"));
		DestroyPlayerTestWorld(World);
		return false;
	}
	UPlayerBodyAnimInstance* const SwingAnim = Swinger->GetBodyAnimInstance();
	UPlayerBodyAnimInstance* const WalkAnim = Walker->GetBodyAnimInstance();
	if (!TestNotNull(TEXT("섞기 애님 인스턴스(휘두르는 몸)"), SwingAnim) || !TestNotNull(TEXT("섞기 애님 인스턴스(걷는 몸)"), WalkAnim))
	{
		DestroyPlayerTestWorld(World);
		return false;
	}
	USkeletalMeshComponent* const SwingBody = Swinger->GetMesh();
	USkeletalMeshComponent* const WalkBody = Walker->GetMesh();

	// 상체 시작 뼈가 실제 몸에 있어야 상체·하체가 갈린다 — 이름을 지어내지 않았는지 몸에서 확인한다.
	TestTrue(FString::Printf(TEXT("상체 시작 뼈 %s 가 몸에 있다"), *SwingAnim->GetUpperBodyRootBone().ToString()),
		SwingBody->GetBoneIndex(SwingAnim->GetUpperBodyRootBone()) != INDEX_NONE);
	TestTrue(TEXT("비교할 다리·팔 뼈가 몸에 있다"),
		SwingBody->GetBoneIndex(LegBone) != INDEX_NONE && SwingBody->GetBoneIndex(ArmBone) != INDEX_NONE);

	// ③ 서서 휘두르면 몸 전체가 휘두르기.
	Swinger->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
	Swinger->GetMeleeAttack()->StartAttacking();
	Swinger->UpdateBodyAnimation();
	TestEqual(TEXT("서서 휘두르면 하체도 행동을 받는다"), SwingAnim->GetLowerBodyActionWeight(), 1.f);
	TestEqual(TEXT("행동이 막 들어왔으니 아직 0"), SwingAnim->GetActionWeight(), 0.f);
	SwingBody->TickAnimation(MidBlendTickSeconds, false);
	const float EnterWeight = SwingAnim->GetActionWeight();
	AddInfo(FString::Printf(TEXT("행동 들어오는 섞기 %.2f초 뒤 가중치 %.3f"), MidBlendTickSeconds, EnterWeight));
	TestTrue(TEXT("행동 시작 직후 섞기 가중치 0<w<1"), EnterWeight > 0.f && EnterWeight < 1.f);
	SwingBody->TickAnimation(SettleTickSeconds, false);
	TestEqual(TEXT("섞기가 끝나면 행동 가중치 1"), SwingAnim->GetActionWeight(), 1.f);

	// 걷기 진입 문턱 경계 — 바로 아래는 아직 대기, 조이스틱을 반쯤 민 걷기(3층 실측 약 25cm/s)는 걷기여야
	// 걸으며 휘두를 때 상체만 휘두른다. 옛 문턱 30 에서는 여기가 대기로 남아 다리가 미끄러졌다 (M18-10).
	constexpr float HalfStickWalkCmPerSecond = 25.f;
	UCharacterMovementComponent* const WalkMovement = Walker->GetCharacterMovement();
	WalkMovement->Velocity = FVector(Walker->GetMoveAnimEnterSpeed() - BandEdgeMarginCmPerSecond, 0.f, 0.f);
	Walker->UpdateBodyAnimation();
	TestEqual(TEXT("걷기 진입 문턱 바로 아래는 대기"), GetNameSafe(WalkAnim->GetBaseClip()), FString(PlayerIdleAssetName));
	WalkMovement->Velocity = FVector(HalfStickWalkCmPerSecond, 0.f, 0.f);
	Walker->UpdateBodyAnimation();
	TestEqual(TEXT("반쯤 민 걷기 25cm/s 는 걷기"), GetNameSafe(WalkAnim->GetBaseClip()), FString(PlayerWalkAssetName));

	// ① 걷기 시작: 두 몸 모두 대기 → 걷기 교차 섞기.
	for (ASnarehavenCharacter* const Player : { Walker, Swinger })
	{
		Player->GetCharacterMovement()->Velocity = FVector(TestMoveSpeedCmPerSecond, 0.f, 0.f);
		Player->UpdateBodyAnimation();
	}
	TestEqual(TEXT("걷는 몸 기본 클립 = Walk"), GetNameSafe(WalkAnim->GetBaseClip()), FString(PlayerWalkAssetName));
	TestEqual(TEXT("걸으며 휘두르는 몸 하체(기본) 클립 = Walk"), GetNameSafe(SwingAnim->GetBaseClip()), FString(PlayerWalkAssetName));
	TestEqual(TEXT("걸으며 휘두르는 몸 상체(행동) 클립 = 잽"), GetNameSafe(SwingAnim->GetActionClip()), FString(PlayerJabAssetName));
	TestEqual(TEXT("2층 시험이 읽는 보이는 클립은 그대로 잽"), GetNameSafe(Swinger->GetCurrentBodyAnim()), FString(PlayerJabAssetName));
	TestEqual(TEXT("걷기만 하는 맨손 몸은 상체(행동) 클립이 없다 — 팔도 걷기 클립 그대로"), GetNameSafe(WalkAnim->GetActionClip()), FString(NoClipName));
	TestEqual(TEXT("교차 섞기가 막 시작됐다(0)"), WalkAnim->GetBaseBlendAlpha(), 0.f);

	WalkBody->TickAnimation(MidBlendTickSeconds, false);
	SwingBody->TickAnimation(MidBlendTickSeconds, false);
	const float BaseAlpha = WalkAnim->GetBaseBlendAlpha();
	const float LowerWeight = SwingAnim->GetLowerBodyActionWeight();
	AddInfo(FString::Printf(TEXT("대기→걷기 교차 %.2f초 뒤 %.3f · 하체 행동 몫 %.3f"), MidBlendTickSeconds, BaseAlpha, LowerWeight));
	TestTrue(TEXT("대기→걷기 전환 직후 섞기 가중치 0<w<1"), BaseAlpha > 0.f && BaseAlpha < 1.f);
	TestTrue(TEXT("걷기 시작 직후 다리가 행동→걷기로 넘어가는 중 0<w<1"), LowerWeight > 0.f && LowerWeight < 1.f);

	WalkBody->TickAnimation(SettleTickSeconds, false);
	SwingBody->TickAnimation(SettleTickSeconds, false);
	TestEqual(TEXT("섞기가 끝나면 교차 가중치 1"), WalkAnim->GetBaseBlendAlpha(), 1.f);
	TestEqual(TEXT("걷는 동안 하체 행동 몫 0"), SwingAnim->GetLowerBodyActionWeight(), 0.f);

	// ② 뼈 자세: 둘 다 같은 걷기 시각이면 다리 뼈는 같고, 휘두르는 몸만 팔이 다르다.
	// 휘두르는 몸이 서서 먼저 흘린 시간은 대기 클립 몫이라, 걷기 클립 시각은 두 몸이 같아야 한다.
	TestEqual(TEXT("두 몸의 걷기 시각이 같다"), SwingAnim->GetBlendState().Base.Time, WalkAnim->GetBlendState().Base.Time, BoneTolerance);
	WalkBody->RefreshBoneTransforms();
	SwingBody->RefreshBoneTransforms();
	const FTransform WalkLeg = WalkBody->GetBoneSpaceTransforms()[WalkBody->GetBoneIndex(LegBone)];
	const FTransform SwingLeg = SwingBody->GetBoneSpaceTransforms()[SwingBody->GetBoneIndex(LegBone)];
	const FTransform WalkArm = WalkBody->GetBoneSpaceTransforms()[WalkBody->GetBoneIndex(ArmBone)];
	const FTransform SwingArm = SwingBody->GetBoneSpaceTransforms()[SwingBody->GetBoneIndex(ArmBone)];
	const float LegDiff = static_cast<float>(WalkLeg.GetRotation().AngularDistance(SwingLeg.GetRotation()));
	const float ArmDiff = static_cast<float>(WalkArm.GetRotation().AngularDistance(SwingArm.GetRotation()));
	AddInfo(FString::Printf(TEXT("걷기만 vs 걸으며 휘두르기 — %s 회전 차 %.4f rad · %s 회전 차 %.4f rad"),
		*LegBone.ToString(), LegDiff, *ArmBone.ToString(), ArmDiff));
	TestTrue(TEXT("걸으며 휘둘러도 다리는 걷기 자세 그대로"), LegDiff < BoneTolerance);
	TestTrue(TEXT("팔은 휘두르기 자세라 걷기와 다르다"), ArmDiff > BoneTolerance);

	// 휘두르기가 끝나면 상체가 잽에서 걷기 팔로 섞여 돌아간다(툭 갈아 끼우지 않는다 — 맨손은 든 자세가 없다, 루프#399).
	Swinger->GetMeleeAttack()->StopAttacking();
	constexpr float TimerStepSeconds = 0.01f;
	const int32 SwingSteps = FMath::RoundToInt(TestAttackIntervalSeconds * 2.f / TimerStepSeconds);
	for (int32 Step = 0; Step < SwingSteps; ++Step)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(TimerStepSeconds);
	}
	Swinger->UpdateBodyAnimation();
	TestEqual(TEXT("휘두르기가 끝나면 보이는 클립은 Walk"), GetNameSafe(Swinger->GetCurrentBodyAnim()), FString(PlayerWalkAssetName));
	TestEqual(TEXT("휘두르기가 끝나면 상체 행동 칸이 빈다"), GetNameSafe(SwingAnim->GetActionClip()), FString(NoClipName));
	TestEqual(TEXT("잽이 막 빠지기 시작했으니 아직 1"), SwingAnim->GetActionWeight(), 1.f);
	SwingBody->TickAnimation(MidBlendTickSeconds, false);
	const float ExitWeight = SwingAnim->GetActionWeight();
	AddInfo(FString::Printf(TEXT("잽→걷기 팔 섞기 %.2f초 뒤 잽 가중치 %.3f"), MidBlendTickSeconds, ExitWeight));
	TestTrue(TEXT("잽→걷기 팔 섞기 가중치 0<w<1"), ExitWeight > 0.f && ExitWeight < 1.f);

	DestroyPlayerTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHeldItemClipTest, "Snarehaven.Player.Body.HeldItemPicksOwnClips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * M18-11 2층 — 손에 든 것마다 몸 동작이 다르다. 맨손·몽둥이·손도끼·곡괭이·전기톱·마체테를 **실제로 들려**
 * 휘두르게 하고 몸에 걸린 쓰기 클립을 읽는다: 여섯이 서로 달라야 하고, 맨손은 잽·크로스가 번갈아 나와야 하며,
 * 휘두르지 않을 때는 상체에 든 자세가 얹혀 있어야 한다. 클립 이름은 표(`DT_ItemTable`·`DT_PlayerCombatTable`)
 * 가 정하므로 이 검사는 이름을 박지 않고 "서로 다르다·비어 있지 않다"만 본다 (맨손 잽·크로스 이름만 확인).
 */
bool FSnarehavenHeldItemClipTest::RunTest(const FString& Parameters)
{
	struct FHeld
	{
		const TCHAR* Label;
		FName ItemID;      // 비면 맨손
		EEquipSlot Slot;
	};
	const FHeld Rows[] = {
		{ TEXT("맨손"),   NAME_None,                    EEquipSlot::Tool },
		{ TEXT("몽둥이"), TEXT("item.club"),            EEquipSlot::Weapon },
		{ TEXT("손도끼"), TEXT("item.hatchet"),         EEquipSlot::Tool },
		{ TEXT("곡괭이"), TEXT("item.pickaxe_basic"),   EEquipSlot::Tool },
		{ TEXT("전기톱"), TEXT("item.chainsaw"),        EEquipSlot::Tool },
		{ TEXT("마체테"), TEXT("item.machete"),         EEquipSlot::Tool },
	};
	constexpr float TimerStepSeconds = 0.01f;

	UWorld* World = CreatePlayerTestWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr || !Player->ApplyPlayerBody() || !TestNotNull(TEXT("장비 컴포넌트"), Player->GetEquipment()))
	{
		AddError(TEXT("플레이어 몸을 못 올렸다 — 아래 클립 검사는 뜻이 없다"));
		DestroyPlayerTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	UPlayerBodyAnimInstance* const BodyAnim = Player->GetBodyAnimInstance();
	Melee->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);

	// 다음 휘두르기가 시작될 때까지 타이머를 흘린다. 간격만큼만 흘리면 모자란다 — 반복 타이머는 건 프레임이 아니라
	// 다음 틱부터 시간을 세서 한 걸음 늦게 터진다(진단: 0.51초를 흘려도 횟수 1 그대로). 두 바퀴를 넘기면 멈춘다.
	const auto TickOneSwing = [World, Melee]()
	{
		// 한 번 누름 = 한 번이라(2026-10-04) 간격이 끝나는 순간 다시 누른다.
		const int32 StartCount = Melee->GetSwingCount();
		const int32 MaxSteps = FMath::RoundToInt(2.f * TestAttackIntervalSeconds / TimerStepSeconds);
		for (int32 Step = 0; Step < MaxSteps && Melee->GetSwingCount() == StartCount; ++Step)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(TimerStepSeconds);
			if (Melee->IsActionReady())
			{
				Melee->StopAttacking();
				Melee->StartAttacking();
			}
		}
	};
	// 손을 뗀 뒤 도는 휘두르기가 끝날 때까지 흘린다 (다음 행을 서 있는 몸으로 시작하려고).
	const auto TickUntilSwingEnds = [World, Melee]()
	{
		const int32 MaxSteps = FMath::RoundToInt(2.f * TestAttackIntervalSeconds / TimerStepSeconds);
		for (int32 Step = 0; Step < MaxSteps && (Melee->IsSwinging() || !Melee->IsActionReady()); ++Step)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(TimerStepSeconds);
		}
	};

	TArray<FString> UseClips;
	TArray<FString> Lines;
	for (const FHeld& Row : Rows)
	{
		TArray<FEquippedSlot> Slots;
		if (!Row.ItemID.IsNone())
		{
			FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = Row.Slot;
			Slot.ItemRowName = Player->GetEquipment()->FindEquipmentRowByItemID(Row.ItemID);
			if (!TestFalse(*FString::Printf(TEXT("[%s] 장비 행이 표에 있다"), Row.Label), Slot.ItemRowName.IsNone()))
			{
				continue;
			}
		}
		Player->GetEquipment()->ApplyEquippedSlots(Slots);

		// 서 있을 때: 무엇이든 들었으면 든 자세가 행동 층에 얹혀 있고, 맨손이면 비어 있다(루프#399 — 팔이 대기 클립 그대로 늘어진다).
		Player->UpdateBodyAnimation();
		const FString Hold = GetNameSafe(BodyAnim != nullptr ? BodyAnim->GetActionClip() : nullptr);
		if (!Row.ItemID.IsNone())
		{
			TestTrue(*FString::Printf(TEXT("[%s] 든 자세가 얹혀 있다 (%s)"), Row.Label, *Hold), Hold != NoClipName);
		}

		// 휘두를 때: 쓰기 클립.
		Melee->StartAttacking();
		Player->UpdateBodyAnimation();
		const FString Use = GetNameSafe(Player->GetCurrentBodyAnim());
		UseClips.Add(Use);
		Lines.Add(FString::Printf(TEXT("%s: 든 자세 %s · 쓰기 %s"), Row.Label, *Hold, *Use));

		if (Row.ItemID.IsNone())
		{
			// 맨손 원투 — 첫째 잽, 둘째 크로스, 셋째 다시 잽.
			TestEqual(TEXT("[맨손] 첫 휘두르기 = 잽"), Use, FString(PlayerJabAssetName));
			TestEqual(TEXT("[맨손] 든 자세 없음 — 주먹 가드를 안 얹는다"), Hold, FString(NoClipName));
			const int32 FirstCount = Melee->GetSwingCount();
			TickOneSwing();
			Player->UpdateBodyAnimation();
			TestEqual(TEXT("[맨손] 둘째 휘두르기가 실제로 시작됐다"), Melee->GetSwingCount(), FirstCount + 1);
			const FString Second = GetNameSafe(Player->GetCurrentBodyAnim());
			TestEqual(TEXT("[맨손] 둘째 휘두르기 = 크로스"), Second, FString(PlayerCrossAssetName));
			TickOneSwing();
			Player->UpdateBodyAnimation();
			const FString Third = GetNameSafe(Player->GetCurrentBodyAnim());
			TestEqual(TEXT("[맨손] 셋째 휘두르기 = 다시 잽"), Third, FString(PlayerJabAssetName));
			AddInfo(FString::Printf(TEXT("[BareOneTwo] 휘두르기 %d·%d·%d회 = %s → %s → %s"),
				FirstCount, FirstCount + 1, Melee->GetSwingCount(), *Use, *Second, *Third));
		}
		Melee->StopAttacking();
		TickUntilSwingEnds();
		Player->UpdateBodyAnimation();
	}

	AddInfo(FString::Printf(TEXT("[HeldClips] %s"), *FString::Join(Lines, TEXT(" | "))));
	TestEqual(TEXT("여섯 행을 다 들어 봤다"), UseClips.Num(), static_cast<int32>(UE_ARRAY_COUNT(Rows)));
	for (int32 A = 0; A < UseClips.Num(); ++A)
	{
		for (int32 B = A + 1; B < UseClips.Num(); ++B)
		{
			TestNotEqual(*FString::Printf(TEXT("%s 와 %s 의 쓰기 클립이 다르다"), Rows[A].Label, Rows[B].Label), UseClips[A], UseClips[B]);
		}
	}

	DestroyPlayerTestWorld(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHeldToolTableClipTest, "Snarehaven.Player.Body.HeldToolPlaysTableClips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 감독 2026-10-05 — 손도구 든 자세·쓰기가 표대로 재생된다 (docs/reference/character_motion.md 「마체테」·「채집 바구니」·「망원경」).
 * ① 바구니·망원경·마체테를 하나씩 **실제로 들려** 서 있을 때 행동 층 = 그 행 `HoldPoseAnim`, 휘두를 때 몸 클립 = 그 행 `UseAnim`
 *    (마체테 든 자세 = `A_Player_HoldOneHand`, 망원경은 「정해진 길이」라 동작 길이 = 보기 클립 길이)
 * ② 왼손 도구(바구니)와 몽둥이를 같이 들면 휘두르기·든 자세는 몽둥이 것 — 역검증: 도구 행을 먼저 보면 바구니 줍기가 나온다.
 */
bool FSnarehavenHeldToolTableClipTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreatePlayerTestWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player != nullptr ? Player->GetEquipment() : nullptr;
	if (Player == nullptr || !TestNotNull(TEXT("장비 컴포넌트"), Equipment) || !Player->ApplyPlayerBody())
	{
		AddError(TEXT("플레이어 몸을 못 올렸다 — 아래 클립 검사는 뜻이 없다"));
		DestroyPlayerTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	UPlayerBodyAnimInstance* const BodyAnim = Player->GetBodyAnimInstance();
	Melee->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);

	// 든 것을 바꿔 들고 (든 자세, 쓰기 클립) 이름을 읽는다. 휘두르기는 다음 들기 전에 끝까지 흘린다.
	const auto HoldAndUse = [&](const TArray<TPair<EEquipSlot, FName>>& Items, FString& OutHold, FString& OutUse)
	{
		TArray<FEquippedSlot> Slots;
		for (const TPair<EEquipSlot, FName>& Item : Items)
		{
			FEquippedSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.Slot = Item.Key;
			Slot.ItemRowName = Equipment->FindEquipmentRowByItemID(Item.Value);
			TestFalse(*FString::Printf(TEXT("[%s] 장비 행이 표에 있다"), *Item.Value.ToString()), Slot.ItemRowName.IsNone());
		}
		Equipment->ApplyEquippedSlots(Slots);
		Player->UpdateBodyAnimation();
		OutHold = GetNameSafe(BodyAnim != nullptr ? BodyAnim->GetActionClip() : nullptr);
		Melee->StartAttacking();
		Player->UpdateBodyAnimation();
		OutUse = GetNameSafe(Player->GetCurrentBodyAnim());
		Melee->StopAttacking();
		for (int32 Step = 0; Step < 400 && (Melee->IsSwinging() || !Melee->IsActionReady()); ++Step)
		{
			++GFrameCounter;
			World->GetTimerManager().Tick(0.01f);
		}
		Player->UpdateBodyAnimation();
	};

	// ① 혼자 든 도구는 그 행의 두 클립.
	for (const TCHAR* const ItemID : { TEXT("item.gather_basket"), TEXT("item.binoculars"), TEXT("item.machete") })
	{
		FString Hold, Use;
		HoldAndUse({ { EEquipSlot::Tool, FName(ItemID) } }, Hold, Use);
		const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Tool);
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 들었다"), ItemID), Row))
		{
			continue;
		}
		const FString RowHold = GetNameSafe(Row->HoldPoseAnim.LoadSynchronous());
		const FString RowUse = GetNameSafe(Row->UseAnim.LoadSynchronous());
		AddInfo(FString::Printf(TEXT("[ToolClips] %s: 든 자세 %s(표 %s) · 쓰기 %s(표 %s) · 동작 길이 %.2f초"),
			ItemID, *Hold, *RowHold, *Use, *RowUse, Melee->GetActionLengthSeconds()));
		TestNotEqual(*FString::Printf(TEXT("① [%s] 표에 든 자세가 있다"), ItemID), RowHold, FString(NoClipName));
		TestNotEqual(*FString::Printf(TEXT("① [%s] 표에 쓰기 클립이 있다"), ItemID), RowUse, FString(NoClipName));
		TestEqual(*FString::Printf(TEXT("① [%s] 서 있을 때 든 자세 = 표"), ItemID), Hold, RowHold);
		TestEqual(*FString::Printf(TEXT("① [%s] 휘두를 때 쓰기 = 표"), ItemID), Use, RowUse);
		if (FName(ItemID) == FName(TEXT("item.machete")))
		{
			TestEqual(TEXT("① 마체테 든 자세 = 한 손 무기 자세"), Hold, FString(TEXT("A_Player_HoldOneHand")));
		}
		if (FName(ItemID) == FName(TEXT("item.binoculars")))
		{
			const UAnimSequence* const Look = Row->UseAnim.LoadSynchronous();
			TestTrue(TEXT("① 망원경은 「정해진 길이」 행"), Row->bUseAnimFixedLength);
			TestEqual(TEXT("① 망원경 동작 길이 = 보기 클립 길이"), Melee->GetActionLengthSeconds(),
				IsValid(Look) ? Look->GetPlayLength() : 0.f, 0.01f);
		}
	}

	// ② 바구니(왼손) + 몽둥이(오른손) — 휘두르기·든 자세는 몽둥이 행.
	{
		FString Hold, Use;
		HoldAndUse({ { EEquipSlot::Weapon, FName(TEXT("item.club")) }, { EEquipSlot::Tool, FName(TEXT("item.gather_basket")) } }, Hold, Use);
		const FItemRow* const Club = Equipment->GetEquippedRow(EEquipSlot::Weapon);
		const FString ClubHold = Club != nullptr ? GetNameSafe(Club->HoldPoseAnim.LoadSynchronous()) : FString();
		const FString ClubUse = Club != nullptr ? GetNameSafe(Club->UseAnim.LoadSynchronous()) : FString();
		AddInfo(FString::Printf(TEXT("[ToolClips] 바구니+몽둥이: 든 자세 %s(몽둥이 %s) · 쓰기 %s(몽둥이 %s)"), *Hold, *ClubHold, *Use, *ClubUse));
		TestEqual(TEXT("② 바구니+몽둥이 휘두르기 = 몽둥이 쓰기"), Use, ClubUse);
		TestEqual(TEXT("② 바구니+몽둥이 든 자세 = 몽둥이 든 자세"), Hold, ClubHold);
	}

	DestroyPlayerTestWorld(World);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerBodyKeepsClipsTest, "Snarehaven.Player.Body.ClipsSurviveGarbageCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * 루프#437 — 손에 든 것의 클립은 표의 소프트 참조를 `LoadSynchronous` 로 받아 아무도 붙들지 않는다. 섞는 중인 클립 넷
 * (지금·이전 × 기본·행동)을 GC 가 거둬 가면 다음 갱신이 해제된 클립을 읽어 죽는다(몽둥이 휘두르기 실측 SIGSEGV).
 * 다른 참조가 하나도 없는 클립 넷을 걸어 두고 GC 를 돌려도 넷 다 살아 있어야 한다.
 */
bool FSnarehavenPlayerBodyKeepsClipsTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 (Verification.md 6): 이전 칸이 남도록 섞기 시간만 0 보다 크게.
	constexpr float TestBlendSeconds = 0.2f;

	// 애님 인스턴스는 스켈레탈 메시 컴포넌트 안에만 산다(ClassWithin). 둘 다 뿌리에 묶어 GC 대상에서 뺀다.
	USkeletalMeshComponent* const Mesh = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	Mesh->AddToRoot();
	UPlayerBodyAnimInstance* const Body = NewObject<UPlayerBodyAnimInstance>(Mesh);
	Body->AddToRoot();
	Body->SetBlendSeconds(TestBlendSeconds, TestBlendSeconds);
	TArray<TWeakObjectPtr<UAnimSequence>> Clips;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		Clips.Add(NewObject<UAnimSequence>(GetTransientPackage()));
	}
	// 기본 0 → 1 (0 이 이전 기본), 행동 2 → 3 (2 가 이전 행동).
	Body->SetBaseClip(Clips[0].Get(), true, 1.f, false);
	Body->SetBaseClip(Clips[1].Get(), true, 1.f, false);
	Body->SetActionClip(Clips[2].Get(), true, 1.f, false, false);
	Body->SetActionClip(Clips[3].Get(), true, 1.f, false, false);
	const FPlayerBodyBlendState& Blend = Body->GetBlendState();
	TestTrue(TEXT("네 칸에 네 클립이 걸렸다"), Blend.PreviousBase.Anim == Clips[0].Get() && Blend.Base.Anim == Clips[1].Get()
		&& Blend.PreviousAction.Anim == Clips[2].Get() && Blend.Action.Anim == Clips[3].Get());

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, true);

	int32 Alive = 0;
	for (const TWeakObjectPtr<UAnimSequence>& Clip : Clips)
	{
		Alive += Clip.IsValid() ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("[KeepClips] GC 뒤 살아 있는 클립 %d/4"), Alive));
	TestEqual(TEXT("GC 뒤에도 섞는 클립 넷이 다 살아 있다"), Alive, 4);

	Body->RemoveFromRoot();
	Mesh->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
