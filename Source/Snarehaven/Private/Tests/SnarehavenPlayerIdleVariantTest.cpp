// INBOX 결함 2026-10-04 12:40 (루프#404) 2층 검사 — 가끔 나오는 대기 변주 2개 (레퍼런스 `idleVariation1·2`).
//  ① 변주 클립 둘(`A_Player_IdleLook`·`A_Player_IdleStretch`)이 대기와 같은 스켈레톤으로 플레이어에 실려 있다.
//  ② 맨손으로 가만히 서 있으면 정해진 시간 뒤 변주 하나가 한 번 돌고, 클립 길이가 지나면 대기로 돌아온다.
//     정해진 시간 전에는 대기 그대로(역검증), 변주 중 움직이면 바로 걷기로 끊기고 변주를 접는다.
// 시간은 캐릭터 틱(`AActor::Tick`)이 센다 — 게임과 같은 길(`UpdateBodyAnimation` → `UpdateIdleVariant`).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "SnarehavenCharacter.h"

namespace
{
	const TCHAR* IdlePath = TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle");
	const TCHAR* WalkPath = TEXT("/Game/Game/Characters/Player/Snare/A_Player_Walk.A_Player_Walk");

	// 검사 전용 값 (Verification.md 6) — DESIGN 숫자가 바뀌어도 안 깨진다.
	constexpr float TestDelaySeconds = 0.5f;
	constexpr float TestNeverSeconds = 1000.f;
	constexpr float TestFrameSeconds = 1.f / 30.f;
	constexpr float TestWalkSpeedCmPerSecond = 200.f;   // 걷기 들어가는 문턱 위
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenIdleVariantClipsTest, "Snarehaven.Player.Body.IdleVariantClipsLoaded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenIdleVariantClipsTest::RunTest(const FString& Parameters)
{
	const UAnimSequence* const Idle = LoadObject<UAnimSequence>(nullptr, IdlePath);
	const ASnarehavenCharacter* const Defaults = GetDefault<ASnarehavenCharacter>();
	if (!TestNotNull(TEXT("대기 클립"), Idle))
	{
		return false;
	}
	const TArray<TObjectPtr<UAnimSequence>>& Variants = Defaults->GetIdleVariantAnims();
	TestEqual(TEXT("대기 변주 둘"), Variants.Num(), 2);
	TSet<FString> Names;
	for (const UAnimSequence* const Anim : Variants)
	{
		if (!TestNotNull(TEXT("변주 클립"), Anim))
		{
			continue;
		}
		Names.Add(Anim->GetName());
		TestTrue(TEXT("대기와 같은 스켈레톤"), Anim->GetSkeleton() == Idle->GetSkeleton());
		TestTrue(TEXT("길이가 있다"), Anim->GetPlayLength() > 1.f);
		AddInfo(FString::Printf(TEXT("[IdleVariant] %s 길이 %.2f초"), *Anim->GetName(), Anim->GetPlayLength()));
	}
	TestTrue(TEXT("두리번"), Names.Contains(TEXT("A_Player_IdleLook")));
	TestTrue(TEXT("기지개"), Names.Contains(TEXT("A_Player_IdleStretch")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenIdleVariantPlayTest, "Snarehaven.Player.Body.IdleVariantPlaysOnceAndYields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenIdleVariantPlayTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	const UAnimSequence* const Idle = LoadObject<UAnimSequence>(nullptr, IdlePath);
	const UAnimSequence* const Walk = LoadObject<UAnimSequence>(nullptr, WalkPath);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("대기"), Idle) || !TestNotNull(TEXT("걷기"), Walk))
	{
		return false;
	}
	Player->DispatchBeginPlay();
	if (!TestTrue(TEXT("사람 몸 올림"), Player->ApplyPlayerBody()))
	{
		return false;
	}
	auto Run = [Player](float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - KINDA_SMALL_NUMBER; Elapsed += TestFrameSeconds)
		{
			static_cast<AActor*>(Player)->Tick(TestFrameSeconds);
		}
	};
	auto Shown = [Player]() { return (const UAnimSequence*)Player->GetCurrentBodyAnim(); };

	// 역검증 — 기다릴 시간이 남았으면 대기 그대로.
	Player->SetIdleVariantDelay(TestNeverSeconds, TestNeverSeconds);
	Run(2.f);
	TestEqual(TEXT("기다릴 시간 전엔 대기"), Shown(), Idle);
	TestNull(TEXT("기다릴 시간 전엔 변주 안 고름"), Player->GetIdleVariantAnim());

	Player->SetIdleVariantDelay(TestDelaySeconds, TestDelaySeconds);
	Run(TestDelaySeconds - 0.2f);
	TestEqual(TEXT("정한 시간 직전 = 대기"), Shown(), Idle);
	Run(0.3f);
	const UAnimSequence* const Variant = Shown();
	const bool bIsVariant = Variant != nullptr && Player->GetIdleVariantAnims().Contains(Variant);
	if (!TestTrue(TEXT("정한 시간 뒤 = 대기 변주"), bIsVariant))
	{
		AddInfo(FString::Printf(TEXT("[IdleVariant] 보인 클립 %s"), *GetNameSafe(Variant)));
		return false;
	}
	const float Length = Variant->GetPlayLength();
	Run(Length - 0.3f);
	TestEqual(TEXT("길이 안에서는 변주 그대로"), Shown(), Variant);
	Run(0.5f);
	TestEqual(TEXT("길이가 지나면 대기로"), Shown(), Idle);
	TestNull(TEXT("끝나면 변주를 놓는다"), Player->GetIdleVariantAnim());
	AddInfo(FString::Printf(TEXT("[IdleVariant] %.2f초 뒤 %s 한 번(%.2f초) → %s"),
		TestDelaySeconds, *Variant->GetName(), Length, *GetNameSafe(Shown())));

	// 다시 기다리면 또 나오고, 그 사이 움직이면 바로 걷기로 끊긴다.
	Run(TestDelaySeconds + 0.2f);
	const UAnimSequence* const Second = Shown();
	TestTrue(TEXT("다시 기다리면 또 변주"), Second != nullptr && Player->GetIdleVariantAnims().Contains(Second));
	Player->GetCharacterMovement()->Velocity = FVector(TestWalkSpeedCmPerSecond, 0.f, 0.f);
	Run(TestFrameSeconds * 2.f);
	TestEqual(TEXT("움직이면 걷기"), Shown(), Walk);
	TestNull(TEXT("움직이면 변주를 접는다"), Player->GetIdleVariantAnim());
	Player->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Run(TestFrameSeconds * 2.f);
	TestEqual(TEXT("멈추면 변주가 아니라 대기부터"), Shown(), Idle);
	AddInfo(FString::Printf(TEXT("[IdleVariant] 두 번째 %s 중 걷기 → %s, 멈춤 → %s"),
		*GetNameSafe(Second), *GetNameSafe(Walk), *GetNameSafe(Shown())));
	return true;
}

#endif
