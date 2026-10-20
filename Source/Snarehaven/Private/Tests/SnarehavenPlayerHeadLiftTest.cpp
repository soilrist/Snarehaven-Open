// INBOX 결함 12:40 ③ 2층 검사 — 게임 카메라(55° 내려다봄)에서 정수리만 보이던 것을 머리 젖힘으로 줄인다.
// 몸 섞기(`UPlayerBodyAnimInstance`)가 두 층을 섞은 뒤 머리 뼈를 머리 자기 좌우축으로 `HeadLiftDegrees` 만큼 젖힌다.
// 같은 클립·같은 시각을 도는 두 몸(젖힘 0 / 게임 기본값)을 나란히 두고 대기·걷기·달리기 한 바퀴 8칸에서
//  ① 얼굴 앞 방향이 젖힘 각도만큼 돌았는지 ② 그 돌림이 위쪽(턱 듦)인지 ③ 게임 카메라 쪽으로 얼굴이 더 돌아왔는지 잰다.
// 얼굴 앞 방향 = 참조 자세에서 몸 앞(+Y)을 머리 뼈에 붙여 함께 돌린 것. 몸이 +Y 를 보는지는 발끝 뼈로 먼저 확인한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Player.Body.HeadLift; Quit" -unattended -nullrhi -log
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

namespace SnarehavenHeadLiftTest
{
	const TCHAR* const ClipPaths[] = {
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle"),
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Walk.A_Player_Walk"),
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Run.A_Player_Run"),
	};

	// 검사 전용 값 (Verification.md 6): 한 바퀴를 나누는 칸 수(INBOX 「각 8칸」), 섞기가 다 끝나는 틱, 각도 여유.
	constexpr int32 SamplesPerClip = 8;
	constexpr float SettleSeconds = 1.f;
	constexpr float AngleToleranceDegrees = 0.5f;
	// 게임 카메라가 내려다보는 각 — 카메라 각은 그대로 두라는 INBOX 12:40 ① 의 55°. 몸이 카메라를 마주 볼 때의 카메라 쪽 방향을 만든다.
	constexpr float CameraPitchDegrees = 55.f;

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

	/** 클립 시각 Time 의 얼굴 앞 방향(컴포넌트 공간). 참조 자세의 몸 앞 +Y 를 머리 뼈와 함께 돌린다. */
	FVector FaceForwardAt(USkeletalMeshComponent* Body, UPlayerBodyAnimInstance* Anim, UAnimSequence* Clip, int32 HeadIndex,
		const FQuat& RefHeadRotation, float Time)
	{
		Anim->SetBaseClip(Clip, true, 1.f, true);
		Body->TickAnimation(Time, false);
		Body->RefreshBoneTransforms();
		const FQuat HeadRotation = Body->GetComponentSpaceTransforms()[HeadIndex].GetRotation();
		return (HeadRotation * RefHeadRotation.Inverse()).RotateVector(FVector::YAxisVector);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerHeadLiftTest, "Snarehaven.Player.Body.HeadLiftShowsFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerHeadLiftTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenHeadLiftTest;

	UWorld* World = CreateWorld();
	ASnarehavenCharacter* const Plain = World->SpawnActor<ASnarehavenCharacter>();
	ASnarehavenCharacter* const Lifted = World->SpawnActor<ASnarehavenCharacter>();
	if (Plain == nullptr || Lifted == nullptr || !Plain->ApplyPlayerBody() || !Lifted->ApplyPlayerBody())
	{
		AddError(TEXT("플레이어 몸을 못 올렸다 — 아래 머리 젖힘 검사는 뜻이 없다"));
		DestroyWorld(World);
		return false;
	}
	UPlayerBodyAnimInstance* const PlainAnim = Plain->GetBodyAnimInstance();
	UPlayerBodyAnimInstance* const LiftedAnim = Lifted->GetBodyAnimInstance();
	if (!TestNotNull(TEXT("섞기 애님 인스턴스(젖힘 0)"), PlainAnim) || !TestNotNull(TEXT("섞기 애님 인스턴스(게임 기본)"), LiftedAnim))
	{
		DestroyWorld(World);
		return false;
	}

	// 게임 기본값이 캐릭터에서 인스턴스로 넘어왔는지 — 0 이면 아래 비교가 같은 두 몸을 견주게 된다.
	const float LiftDegrees = LiftedAnim->GetHeadLiftDegrees();
	AddInfo(FString::Printf(TEXT("[HeadLift] 게임 기본 젖힘 %.1f°"), LiftDegrees));
	TestTrue(TEXT("게임 기본 머리 젖힘이 0 보다 크다"), LiftDegrees > 0.f);
	PlainAnim->SetHeadLift(TEXT("Head"), 0.f);

	USkeletalMeshComponent* const PlainBody = Plain->GetMesh();
	USkeletalMeshComponent* const LiftedBody = Lifted->GetMesh();
	const FReferenceSkeleton& RefSkeleton = LiftedBody->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 HeadIndex = LiftedBody->GetBoneIndex(TEXT("Head"));
	const int32 FootIndex = LiftedBody->GetBoneIndex(TEXT("LeftFoot"));
	const int32 ToeIndex = LiftedBody->GetBoneIndex(TEXT("LeftToeBase"));
	if (!TestTrue(TEXT("머리·발·발끝 뼈가 몸에 있다"), HeadIndex != INDEX_NONE && FootIndex != INDEX_NONE && ToeIndex != INDEX_NONE))
	{
		DestroyWorld(World);
		return false;
	}

	// 몸 앞이 +Y 인지 참조 자세 발끝으로 확인 — 아니면 「얼굴 앞 방향」 자체가 틀린다.
	const FVector RefFoot = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, FootIndex).GetLocation();
	const FVector RefToe = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, ToeIndex).GetLocation();
	AddInfo(FString::Printf(TEXT("[HeadLift] 참조 자세 발 %s → 발끝 %s"), *RefFoot.ToCompactString(), *RefToe.ToCompactString()));
	TestTrue(TEXT("몸 앞 = 컴포넌트 +Y (발끝이 발보다 +Y)"), RefToe.Y > RefFoot.Y);
	const FQuat RefHeadRotation = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, HeadIndex).GetRotation();

	// 몸이 카메라를 마주 볼 때 얼굴에서 카메라 쪽 방향 — 앞(+Y)으로 cos, 위(+Z)로 sin.
	const float Pitch = FMath::DegreesToRadians(CameraPitchDegrees);
	const FVector ToCamera(0.f, FMath::Cos(Pitch), FMath::Sin(Pitch));

	for (const TCHAR* const Path : ClipPaths)
	{
		UAnimSequence* const Clip = LoadObject<UAnimSequence>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("클립 %s"), Path), Clip))
		{
			continue;
		}
		// 앞 클립에서 넘어오는 교차 섞기를 다 끝낸 뒤 잰다.
		for (UPlayerBodyAnimInstance* const Anim : { PlainAnim, LiftedAnim })
		{
			Anim->ClearActionClip();
			Anim->SetBaseClip(Clip, true, 1.f, true);
		}
		PlainBody->TickAnimation(SettleSeconds, false);
		LiftedBody->TickAnimation(SettleSeconds, false);

		const float Length = Clip->GetPlayLength();
		float WorstPlainCameraDeg = 0.f;
		float WorstLiftedCameraDeg = 0.f;
		for (int32 Sample = 0; Sample < SamplesPerClip; ++Sample)
		{
			const float Time = Length * Sample / SamplesPerClip;
			const FVector PlainFace = FaceForwardAt(PlainBody, PlainAnim, Clip, HeadIndex, RefHeadRotation, Time);
			const FVector LiftedFace = FaceForwardAt(LiftedBody, LiftedAnim, Clip, HeadIndex, RefHeadRotation, Time);
			const float TurnDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(PlainFace | LiftedFace, -1.f, 1.f)));
			const float PlainElevDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(PlainFace.Z, -1.f, 1.f)));
			const float LiftedElevDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(LiftedFace.Z, -1.f, 1.f)));
			const float PlainCameraDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(PlainFace | ToCamera, -1.f, 1.f)));
			const float LiftedCameraDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(LiftedFace | ToCamera, -1.f, 1.f)));
			WorstPlainCameraDeg = FMath::Max(WorstPlainCameraDeg, PlainCameraDeg);
			WorstLiftedCameraDeg = FMath::Max(WorstLiftedCameraDeg, LiftedCameraDeg);
			AddInfo(FString::Printf(TEXT("[HeadLift] %s 칸 %d (%.3f초) · 돌림 %.2f° · 얼굴 높이 %.1f° → %.1f° · 카메라와 사이각 %.1f° → %.1f°"),
				*Clip->GetName(), Sample + 1, Time, TurnDeg, PlainElevDeg, LiftedElevDeg, PlainCameraDeg, LiftedCameraDeg));
			TestNearlyEqual(FString::Printf(TEXT("%s 칸 %d 얼굴이 젖힘만큼 돈다"), *Clip->GetName(), Sample + 1), TurnDeg, LiftDegrees, AngleToleranceDegrees);
			TestTrue(FString::Printf(TEXT("%s 칸 %d 턱이 들린다(얼굴 높이 오름)"), *Clip->GetName(), Sample + 1), LiftedElevDeg > PlainElevDeg);
			TestTrue(FString::Printf(TEXT("%s 칸 %d 얼굴이 게임 카메라 쪽으로 더 돈다"), *Clip->GetName(), Sample + 1), LiftedCameraDeg < PlainCameraDeg);
		}
		AddInfo(FString::Printf(TEXT("[HeadLift] %s 가장 먼 칸 카메라와 사이각 %.1f° → %.1f°"), *Clip->GetName(), WorstPlainCameraDeg, WorstLiftedCameraDeg));
	}

	DestroyWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
