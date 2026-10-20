// 레퍼런스 원본 모션표(docs/reference/character_motion.md) 걷기·대기 줄 2층 검사 — 클립을 실제로 틀어 뼈 위치를 잰다.
//  걷기: 두 발 한 주기 ≈ 0.67초(클립 길이 = 기준 속도에서의 한 주기, 재생 속도 1) · 걸음마다 몸이 통통(골반 높이 봉우리 2개/주기) ·
//        상체 살짝 앞숙임.
//  대기: 거의 가만히 — 골반 오르내림·머리 흔들림이 숨쉬기 수준.
// 한도는 관찰표 말을 숫자로 옮긴 **검사 전용 값**(Verification.md 6)이고, 잰 값은 [MotionMatch] 줄로 남긴다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Player.Body.MotionMatches; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Snarehaven.h"

namespace SnarehavenMotionMatchTest
{
	const TCHAR* const WalkPath = TEXT("/Game/Game/Characters/Player/Snare/A_Player_Walk.A_Player_Walk");
	const TCHAR* const IdlePath = TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle");

	constexpr int32 Samples = 60;
	constexpr float WalkCycleTargetSeconds = 0.67f;   // 관찰표 「두 발 한 주기 ≈ 0.67초」
	constexpr float WalkCycleToleranceSeconds = 0.05f;
	constexpr int32 WalkBouncesPerCycle = 2;          // 한 걸음에 한 번 튐 — 두 발이면 둘
	constexpr double WalkMinBobCm = 1.0;              // 「통통」 — 눈에 보일 만큼은 튄다
	constexpr double WalkLeanMinDeg = 2.0;            // 「살짝 앞으로」 — 곧게 서면 아니다
	constexpr double WalkLeanMaxDeg = 15.0;
	constexpr double IdleMaxBobCm = 1.0;              // 「숨쉬기 정도」
	constexpr double IdleMaxHeadCm = 2.0;
	const FName HipsBone(TEXT("Hips"));
	const FName HeadBone(TEXT("Head"));

	UWorld* MakeWorld()
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	struct FTrack
	{
		TArray<double> HipsZ;
		TArray<FVector> Head;
		double LeanSumDeg = 0.0;
	};

	/** 클립 한 주기를 고르게 나눠 골반 높이·머리 위치·앞숙임(골반→머리 선이 수직에서 앞으로 기운 각)을 잰다. */
	FTrack Sample(USkeletalMeshComponent* Body, UAnimSequence* Anim, const FVector& Forward)
	{
		FTrack Track;
		Body->PlayAnimation(Anim, true);
		for (int32 Index = 0; Index < Samples; ++Index)
		{
			Body->SetPosition(Anim->GetPlayLength() * Index / Samples, false);
			Body->TickAnimation(0.f, false);
			Body->RefreshBoneTransforms();
			const FVector Hips = Body->GetBoneLocation(HipsBone);
			const FVector Head = Body->GetBoneLocation(HeadBone);
			const FVector Up = Head - Hips;
			Track.HipsZ.Add(Hips.Z);
			Track.Head.Add(Head);
			Track.LeanSumDeg += FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Up, Forward), Up.Z));
		}
		return Track;
	}

	double Range(const TArray<double>& Values)
	{
		return FMath::Max(Values) - FMath::Min(Values);
	}

	/** 되풀이 클립의 골반 높이 봉우리 수 — 이웃보다 높고, 범위의 절반 넘게 솟은 것만 센다(잔떨림 제외). */
	int32 CountPeaks(const TArray<double>& Values)
	{
		const double Floor = FMath::Min(Values) + Range(Values) * 0.5;
		int32 Peaks = 0;
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			const double Prev = Values[(Index + Values.Num() - 1) % Values.Num()];
			const double Next = Values[(Index + 1) % Values.Num()];
			Peaks += Values[Index] > Prev && Values[Index] >= Next && Values[Index] > Floor ? 1 : 0;
		}
		return Peaks;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerMotionMatchTest, "Snarehaven.Player.Body.MotionMatchesReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerMotionMatchTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenMotionMatchTest;

	UWorld* const World = MakeWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UAnimSequence* const Walk = LoadObject<UAnimSequence>(nullptr, WalkPath);
	UAnimSequence* const Idle = LoadObject<UAnimSequence>(nullptr, IdlePath);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody())
		|| !TestNotNull(TEXT("걷기 클립"), Walk) || !TestNotNull(TEXT("대기 클립"), Idle))
	{
		DestroyWorld(World);
		return false;
	}
	USkeletalMeshComponent* const Body = Player->GetMesh();
	const FVector Forward = Player->GetActorForwardVector();

	// ── 걷기 ── 기준 속도(조이스틱 끝까지)에서 재생 속도 1 이라 클립 길이가 곧 화면의 한 주기다(`SelectBaseBodyClip`).
	const FTrack WalkTrack = Sample(Body, Walk, Forward);
	const float Cycle = Walk->GetPlayLength();
	const int32 Bounces = CountPeaks(WalkTrack.HipsZ);
	const double WalkBob = Range(WalkTrack.HipsZ);
	const double Lean = WalkTrack.LeanSumDeg / Samples;
	AddInfo(FString::Printf(TEXT("[MotionMatch] 걷기 주기 %.3f초 · 골반 튐 봉우리 %d개 · 튐 폭 %.2fcm · 앞숙임 평균 %.1f°"),
		Cycle, Bounces, WalkBob, Lean));
	TestTrue(FString::Printf(TEXT("걷기 한 주기 %.3f초 ≈ %.2f±%.2f"), Cycle, WalkCycleTargetSeconds, WalkCycleToleranceSeconds),
		FMath::Abs(Cycle - WalkCycleTargetSeconds) <= WalkCycleToleranceSeconds);
	TestEqual(TEXT("걷기 한 주기에 몸이 두 번 튄다(걸음마다)"), Bounces, WalkBouncesPerCycle);
	TestTrue(FString::Printf(TEXT("걷기 튐 폭 %.2fcm ≥ %.1f"), WalkBob, WalkMinBobCm), WalkBob >= WalkMinBobCm);
	TestTrue(FString::Printf(TEXT("걷기 앞숙임 %.1f° 가 %.0f~%.0f°"), Lean, WalkLeanMinDeg, WalkLeanMaxDeg),
		Lean >= WalkLeanMinDeg && Lean <= WalkLeanMaxDeg);

	// ── 대기 ──
	const FTrack IdleTrack = Sample(Body, Idle, Forward);
	const double IdleBob = Range(IdleTrack.HipsZ);
	double HeadMove = 0.0;
	for (const FVector& Head : IdleTrack.Head)
	{
		HeadMove = FMath::Max(HeadMove, FVector::Dist(Head, IdleTrack.Head[0]));
	}
	AddInfo(FString::Printf(TEXT("[MotionMatch] 대기 길이 %.2f초 · 골반 오르내림 %.2fcm · 머리 최대 이동 %.2fcm"),
		Idle->GetPlayLength(), IdleBob, HeadMove));
	TestTrue(FString::Printf(TEXT("대기 골반 오르내림 %.2fcm ≤ %.1f (숨쉬기)"), IdleBob, IdleMaxBobCm), IdleBob <= IdleMaxBobCm);
	TestTrue(TEXT("대기가 완전히 굳어 있지 않다(숨쉰다)"), IdleBob > 0.0 || HeadMove > 0.0);
	TestTrue(FString::Printf(TEXT("대기 머리 이동 %.2fcm ≤ %.1f"), HeadMove, IdleMaxHeadCm), HeadMove <= IdleMaxHeadCm);

	DestroyWorld(World);
	return true;
}

#endif
