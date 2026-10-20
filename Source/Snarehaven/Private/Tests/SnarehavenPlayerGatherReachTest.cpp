// INBOX 지시 15:30 ④ 2층 검사 — 줍기·열매 따기 `A_Player_Gather` 가 레퍼런스처럼 「숙이지 않고 서서 오른팔만 대상 쪽으로 뻗었다
// 돌아오는」 동작인지 (`docs/reference/character_motion.md` 25행 · 옛 클립은 쪼그려 1.0초).
// 게임 몸 섞기(`UPlayerBodyAnimInstance`)로 클립을 고르게 나눈 칸마다 잰다.
//  ① 길이 0.5~0.8초 (관찰표 범위).
//  ② 서 있다 = 골반 높이가 참조 자세에서 거의 안 바뀐다 (쪼그리면 몇 cm 내려간다).
//  ③ 오른팔이 뻗는다 = 오른 손목이 첫 칸보다 몸 앞(+Y)으로 팔 길이의 일정 비율 넘게 나갔다 돌아온다.
//  ④ 한 팔만 = 왼 손목이 왼 어깨 기준으로 거의 그대로.
//  ⑤ 첫 칸 = 끝 칸 (대기로 매끈하게 돌아옴).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Player.Body.GatherStandReach; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Player/PlayerBodyAnimInstance.h"

namespace SnarehavenGatherReachTest
{
	const TCHAR* const ClipPath = TEXT("/Game/Game/Characters/Player/Snare/A_Player_Gather.A_Player_Gather");

	// 검사 전용 값 (Verification.md 6): 관찰표 길이 범위, 칸 수, 섞기 안정 시간,
	// 골반 높이 변화 한도(키의 2% — 쪼그린 옛 클립은 약 10%), 앞으로 뻗음 하한(팔 길이의 40%),
	// 왼 손목 흔들림 한도(팔 길이의 10%), 첫 칸·끝 칸 손목 차 한도(cm).
	constexpr float MinLengthSeconds = 0.5f;
	constexpr float MaxLengthSeconds = 0.8f;
	constexpr int32 Samples = 24;
	constexpr float SettleSeconds = 1.f;
	constexpr float MaxHipDropOfHeight = 0.02f;
	constexpr float MinReachOfArm = 0.4f;
	constexpr float MaxLeftDriftOfArm = 0.1f;
	constexpr float MaxSeamCm = 0.5f;

	UWorld* CreateWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerGatherReachTest, "Snarehaven.Player.Body.GatherStandReach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerGatherReachTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenGatherReachTest;

	UWorld* World = CreateWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr || !Player->ApplyPlayerBody())
	{
		AddError(TEXT("플레이어 몸을 못 올렸다 — 아래 줍기 자세 검사는 뜻이 없다"));
		DestroyWorld(World);
		return false;
	}
	UPlayerBodyAnimInstance* const Anim = Player->GetBodyAnimInstance();
	UAnimSequence* const Clip = LoadObject<UAnimSequence>(nullptr, ClipPath);
	if (!TestNotNull(TEXT("섞기 애님 인스턴스"), Anim) || !TestNotNull(TEXT("A_Player_Gather"), Clip))
	{
		DestroyWorld(World);
		return false;
	}

	USkeletalMeshComponent* const Body = Player->GetMesh();
	const FReferenceSkeleton& RefSkeleton = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 HipsIndex = Body->GetBoneIndex(TEXT("Hips"));
	const int32 HeadIndex = Body->GetBoneIndex(TEXT("Head"));
	const int32 FootIndex = Body->GetBoneIndex(TEXT("LeftFoot"));
	const int32 ToeIndex = Body->GetBoneIndex(TEXT("LeftToeBase"));
	const int32 RShoulderIndex = Body->GetBoneIndex(TEXT("RightArm"));
	const int32 RElbowIndex = Body->GetBoneIndex(TEXT("RightForeArm"));
	const int32 RWristIndex = Body->GetBoneIndex(TEXT("RightHand"));
	const int32 LShoulderIndex = Body->GetBoneIndex(TEXT("LeftArm"));
	const int32 LWristIndex = Body->GetBoneIndex(TEXT("LeftHand"));
	const bool bBonesFound = HipsIndex != INDEX_NONE && HeadIndex != INDEX_NONE && FootIndex != INDEX_NONE && ToeIndex != INDEX_NONE
		&& RShoulderIndex != INDEX_NONE && RElbowIndex != INDEX_NONE && RWristIndex != INDEX_NONE
		&& LShoulderIndex != INDEX_NONE && LWristIndex != INDEX_NONE;
	if (!TestTrue(TEXT("골반·머리·발·양팔 뼈가 몸에 있다"), bBonesFound))
	{
		DestroyWorld(World);
		return false;
	}

	auto RefLocation = [&RefSkeleton](int32 Index)
	{
		return FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, Index).GetLocation();
	};
	TestTrue(TEXT("몸 앞 = 컴포넌트 +Y (발끝이 발보다 +Y)"), RefLocation(ToeIndex).Y > RefLocation(FootIndex).Y);
	const float Height = RefLocation(HeadIndex).Z - RefLocation(FootIndex).Z;
	const float ArmLength = (RefLocation(RElbowIndex) - RefLocation(RShoulderIndex)).Size() + (RefLocation(RWristIndex) - RefLocation(RElbowIndex)).Size();

	const float Length = Clip->GetPlayLength();
	TestTrue(FString::Printf(TEXT("길이 %.2f초가 %.1f~%.1f초"), Length, MinLengthSeconds, MaxLengthSeconds),
		Length >= MinLengthSeconds && Length <= MaxLengthSeconds);

	Anim->ClearActionClip();
	Anim->SetBaseClip(Clip, false, 1.f, true);
	Body->TickAnimation(SettleSeconds, false);

	float HipMin = BIG_NUMBER, HipMax = -BIG_NUMBER;
	float ReachMax = 0.f, LeftDrift = 0.f;
	FVector FirstRWrist = FVector::ZeroVector, FirstLeft = FVector::ZeroVector, LastRWrist = FVector::ZeroVector;
	for (int32 Sample = 0; Sample <= Samples; ++Sample)
	{
		// 끝 칸은 길이 바로 앞 — 되풀이 끔이라 길이를 넘기면 끝 장에 멈춰 있다.
		const float Time = FMath::Min(Length * Sample / Samples, Length - KINDA_SMALL_NUMBER);
		Anim->SetBaseClip(Clip, false, 1.f, true);
		Body->TickAnimation(Time, false);
		Body->RefreshBoneTransforms();
		const TArray<FTransform>& Pose = Body->GetComponentSpaceTransforms();
		const FVector Hips = Pose[HipsIndex].GetLocation();
		const FVector RWrist = Pose[RWristIndex].GetLocation();
		const FVector Left = Pose[LWristIndex].GetLocation() - Pose[LShoulderIndex].GetLocation();
		if (Sample == 0)
		{
			FirstRWrist = RWrist;
			FirstLeft = Left;
		}
		LastRWrist = RWrist;
		HipMin = FMath::Min(HipMin, Hips.Z);
		HipMax = FMath::Max(HipMax, Hips.Z);
		ReachMax = FMath::Max(ReachMax, RWrist.Y - FirstRWrist.Y);
		LeftDrift = FMath::Max(LeftDrift, (Left - FirstLeft).Size());
		AddInfo(FString::Printf(TEXT("[GatherReach] 칸 %2d (%.3f초) 골반 z %.1f · 오른 손목 앞 %+.1f 위 %+.1f · 왼 손목(어깨 기준) 움직임 %.1f cm"),
			Sample, Time, Hips.Z, RWrist.Y - FirstRWrist.Y, RWrist.Z - FirstRWrist.Z, (Left - FirstLeft).Size()));
	}

	const float HipDrop = HipMax - HipMin;
	AddInfo(FString::Printf(TEXT("[GatherReach] %s 길이 %.2f초 · 키 %.1f · 팔 길이 %.1f · 골반 높이 변화 %.2f · 오른 손목 가장 앞 %.1f · 왼 손목 최대 움직임 %.2f · 첫·끝 칸 오른 손목 차 %.2f cm"),
		*Clip->GetName(), Length, Height, ArmLength, HipDrop, ReachMax, LeftDrift, (LastRWrist - FirstRWrist).Size()));
	TestTrue(FString::Printf(TEXT("서 있다: 골반 높이 변화 %.2f ≤ 키의 %.0f%% (%.2f)"), HipDrop, MaxHipDropOfHeight * 100.f, Height * MaxHipDropOfHeight),
		HipDrop <= Height * MaxHipDropOfHeight);
	TestTrue(FString::Printf(TEXT("오른팔이 앞으로 뻗는다: %.1f ≥ 팔 길이의 %.0f%% (%.1f)"), ReachMax, MinReachOfArm * 100.f, ArmLength * MinReachOfArm),
		ReachMax >= ArmLength * MinReachOfArm);
	TestTrue(FString::Printf(TEXT("한 팔만: 왼 손목 움직임 %.2f ≤ 팔 길이의 %.0f%% (%.1f)"), LeftDrift, MaxLeftDriftOfArm * 100.f, ArmLength * MaxLeftDriftOfArm),
		LeftDrift <= ArmLength * MaxLeftDriftOfArm);
	TestTrue(FString::Printf(TEXT("끝 칸이 첫 칸으로 돌아온다: 오른 손목 차 %.2f ≤ %.1f cm"), (LastRWrist - FirstRWrist).Size(), MaxSeamCm),
		(LastRWrist - FirstRWrist).Size() <= MaxSeamCm);

	DestroyWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
