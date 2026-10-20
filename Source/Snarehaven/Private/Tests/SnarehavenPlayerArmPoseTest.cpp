// INBOX 지시 12:25 2층 검사 — 대기·걷기·달리기 팔이 「복싱 자세」(팔꿈치 직각·주먹이 몸 앞)가 아니라 레퍼런스처럼 몸통 옆에 축 늘어지는지.
// 게임 몸 섞기(`UPlayerBodyAnimInstance`)로 클립 한 주기를 고르게 나눈 칸마다 양팔을 잰다.
//  ① 팔꿈치 굽힘 = 위팔(어깨→팔꿈치) 과 아래팔(팔꿈치→손목) 방향 사이각 — 대기·걷기 ≤ 20°, 달리기 ≤ 45° (지시 ②).
//  ② 팔 앞 기울기 = 어깨→손목 선이 바로 아래에서 몸 앞(+Y)으로 기운 각 — 대기·걷기는 손이 허벅지 옆(지시 ② 「몸 앞으로 안 나옴」).
//     (바르코 바탕 자세는 팔 전체가 약 29° 앞으로 들려 있었다 — 루프#402 실측.)
//  ③ 걷기 위팔 흔들기 = 위팔이 몸 옆면(YZ)에서 아래 방향과 이루는 앞뒤 각의 한 주기 폭 — 지시 ② 약 ±12°.
// 몸 앞이 +Y 인지는 `HeadLiftShowsFace` 와 같이 참조 자세 발끝으로 먼저 확인한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Player.Body.ArmsHang; Quit" -unattended -nullrhi -log
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

namespace SnarehavenArmPoseTest
{
	struct FClipLimit
	{
		const TCHAR* Path;
		float MaxElbowDegrees;
		float MaxArmForwardDegrees;
	};

	// 지시 12:25 ② 의 기준을 그대로 옮긴 검사 전용 값 (Verification.md 6). 팔 앞 기울기 한도: 대기 = 숨쉬기만이라 거의 수직(8°),
	// 걷기 = 흔들기 ±12° + 상체 앞숙임 8°(걷기 v3 `walk_cycle_v2_make.py` lean 3+5) 만큼(20°), 달리기 = 지시에 기준 없음(0 = 안 봄).
	const FClipLimit Clips[] = {
		{ TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle"), 20.f, 8.f },
		{ TEXT("/Game/Game/Characters/Player/Snare/A_Player_Walk.A_Player_Walk"), 20.f, 20.f },
		{ TEXT("/Game/Game/Characters/Player/Snare/A_Player_Run.A_Player_Run"), 45.f, 0.f },
	};

	// 검사 전용 값: 한 주기를 나누는 칸 수, 섞기가 다 끝나는 틱,
	// 걷기 흔들기 폭 한도(지시 「약 ±12°」 를 양쪽 합친 폭으로 넓게 — 너무 작으면 뻣뻣, 너무 크면 v2 의 ±36° 로 돌아감).
	constexpr int32 SamplesPerClip = 16;
	constexpr float SettleSeconds = 1.f;
	constexpr float MinWalkSwingDegrees = 12.f;
	constexpr float MaxWalkSwingDegrees = 36.f;

	const TCHAR* const Sides[] = { TEXT("Left"), TEXT("Right") };

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

	float AngleBetweenDegrees(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(A.GetSafeNormal() | B.GetSafeNormal(), -1.f, 1.f)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerArmPoseTest, "Snarehaven.Player.Body.ArmsHangRelaxed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerArmPoseTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenArmPoseTest;

	UWorld* World = CreateWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr || !Player->ApplyPlayerBody())
	{
		AddError(TEXT("플레이어 몸을 못 올렸다 — 아래 팔 자세 검사는 뜻이 없다"));
		DestroyWorld(World);
		return false;
	}
	UPlayerBodyAnimInstance* const Anim = Player->GetBodyAnimInstance();
	if (!TestNotNull(TEXT("섞기 애님 인스턴스"), Anim))
	{
		DestroyWorld(World);
		return false;
	}

	USkeletalMeshComponent* const Body = Player->GetMesh();
	const FReferenceSkeleton& RefSkeleton = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 FootIndex = Body->GetBoneIndex(TEXT("LeftFoot"));
	const int32 ToeIndex = Body->GetBoneIndex(TEXT("LeftToeBase"));
	int32 ArmIndex[2], ForeArmIndex[2], HandIndex[2];
	bool bBonesFound = FootIndex != INDEX_NONE && ToeIndex != INDEX_NONE;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		ArmIndex[Side] = Body->GetBoneIndex(FName(FString(Sides[Side]) + TEXT("Arm")));
		ForeArmIndex[Side] = Body->GetBoneIndex(FName(FString(Sides[Side]) + TEXT("ForeArm")));
		HandIndex[Side] = Body->GetBoneIndex(FName(FString(Sides[Side]) + TEXT("Hand")));
		bBonesFound &= ArmIndex[Side] != INDEX_NONE && ForeArmIndex[Side] != INDEX_NONE && HandIndex[Side] != INDEX_NONE;
	}
	if (!TestTrue(TEXT("양쪽 위팔·아래팔·손·발끝 뼈가 몸에 있다"), bBonesFound))
	{
		DestroyWorld(World);
		return false;
	}

	const FVector RefFoot = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, FootIndex).GetLocation();
	const FVector RefToe = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, ToeIndex).GetLocation();
	TestTrue(TEXT("몸 앞 = 컴포넌트 +Y (발끝이 발보다 +Y)"), RefToe.Y > RefFoot.Y);

	for (const FClipLimit& Limit : Clips)
	{
		UAnimSequence* const Clip = LoadObject<UAnimSequence>(nullptr, Limit.Path);
		if (!TestNotNull(FString::Printf(TEXT("클립 %s"), Limit.Path), Clip))
		{
			continue;
		}
		Anim->ClearActionClip();
		Anim->SetBaseClip(Clip, true, 1.f, true);
		Body->TickAnimation(SettleSeconds, false);

		const float Length = Clip->GetPlayLength();
		float WorstElbow = 0.f;
		float WorstArmForward = -BIG_NUMBER;
		float SwingMin[2] = { BIG_NUMBER, BIG_NUMBER };
		float SwingMax[2] = { -BIG_NUMBER, -BIG_NUMBER };
		for (int32 Sample = 0; Sample < SamplesPerClip; ++Sample)
		{
			const float Time = Length * Sample / SamplesPerClip;
			Anim->SetBaseClip(Clip, true, 1.f, true);
			Body->TickAnimation(Time, false);
			Body->RefreshBoneTransforms();
			const TArray<FTransform>& Pose = Body->GetComponentSpaceTransforms();

			FString Report = FString::Printf(TEXT("[ArmPose] %s 칸 %2d (%.3f초)"), *Clip->GetName(), Sample + 1, Time);
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FVector Shoulder = Pose[ArmIndex[Side]].GetLocation();
				const FVector Elbow = Pose[ForeArmIndex[Side]].GetLocation();
				const FVector Wrist = Pose[HandIndex[Side]].GetLocation();
				const FVector Upper = Elbow - Shoulder;
				const float ElbowDeg = AngleBetweenDegrees(Upper, Wrist - Elbow);
				const FVector Line = Wrist - Shoulder;
				const float ArmForwardDeg = FMath::RadiansToDegrees(FMath::Atan2(Line.Y, -Line.Z));
				// 위팔 앞뒤 각: 몸 옆면(Y 앞·Z 위)에 비친 위팔이 아래(-Z)에서 앞(+Y)으로 든 각.
				const float SwingDeg = FMath::RadiansToDegrees(FMath::Atan2(Upper.Y, -Upper.Z));
				WorstElbow = FMath::Max(WorstElbow, ElbowDeg);
				WorstArmForward = FMath::Max(WorstArmForward, ArmForwardDeg);
				SwingMin[Side] = FMath::Min(SwingMin[Side], SwingDeg);
				SwingMax[Side] = FMath::Max(SwingMax[Side], SwingDeg);
				Report += FString::Printf(TEXT(" · %s 팔꿈치 %.1f° 팔 앞 %.1f° 위팔 앞 %.1f°"), Sides[Side], ElbowDeg, ArmForwardDeg, SwingDeg);
			}
			AddInfo(Report);
		}

		AddInfo(FString::Printf(TEXT("[ArmPose] %s 가장 굽은 팔꿈치 %.1f° (한도 %.0f°) · 가장 앞 팔 %.1f° · 위팔 흔들기 폭 왼 %.1f° 오른 %.1f°"),
			*Clip->GetName(), WorstElbow, Limit.MaxElbowDegrees, WorstArmForward,
			SwingMax[0] - SwingMin[0], SwingMax[1] - SwingMin[1]));
		TestTrue(FString::Printf(TEXT("%s 팔꿈치 굽힘 %.1f° ≤ %.0f°"), *Clip->GetName(), WorstElbow, Limit.MaxElbowDegrees),
			WorstElbow <= Limit.MaxElbowDegrees);
		if (Limit.MaxArmForwardDegrees > 0.f)
		{
			TestTrue(FString::Printf(TEXT("%s 손이 허벅지 옆 (팔 앞 기울기 %.1f° ≤ %.0f°)"), *Clip->GetName(), WorstArmForward, Limit.MaxArmForwardDegrees),
				WorstArmForward <= Limit.MaxArmForwardDegrees);
		}
		if (Clip->GetName() == TEXT("A_Player_Walk"))
		{
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const float Swing = SwingMax[Side] - SwingMin[Side];
				TestTrue(FString::Printf(TEXT("걷기 %s 위팔 흔들기 폭 %.1f° 가 %.0f~%.0f°"), Sides[Side], Swing, MinWalkSwingDegrees, MaxWalkSwingDegrees),
					Swing >= MinWalkSwingDegrees && Swing <= MaxWalkSwingDegrees);
			}
		}
	}

	DestroyWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
