// INBOX 감독 지시 2026-10-04 ④ 2층 검사 — 전기톱 쓰기 (docs/reference/character_motion.md 「전기톱 — 쓸 때(누르는 동안)」).
// 레퍼런스 원본 = 약 0.2초 만에 톱을 **수평으로 내리며 허리 높이에서 몸 앞으로 쭉 내민다** → 누르는 동안 버티며 떤다.
// 전기톱 행의 `UseAnim` 을 걸고 전기톱을 든 뒤 타격 시점(0.2초 — DESIGN 3.1 「공격 타격 지연」)·버팀 끝(돌아오기 직전)에서 잰다.
// 루프#387: 한 번 누르면 정해진 길이(1.5초, 재생 속도 1)만큼 내밀어 떨다 끝 0.3초에 세로로 돌아온다(INBOX 15:30 ⓪).
// ① 쥔 점(행 `GripOffset`)→날 끝 축이 수평 ±20° — 수평
// ② 날 끝이 두 손목보다 앞 — 몸 앞으로 내밂
// ③ 두 손목 높이가 골반 뼈 위·가슴 뼈(Spine2) 아래 — 허리 높이
// ④ 클립 첫 프레임 오른손목이 든 자세(`HoldPoseAnim`) 첫 프레임과 2cm 안 — 세로에서 출발해 튀지 않는다
// ⑤ 역검증: 든 자세(세로)는 ① 이나 ③ 에 걸린다 — 기준이 세로 자세를 가른다.
// ⑥ 클립 끝 프레임 오른손목이 든 자세 첫 프레임과 2cm 안 — 떨다 세로로 돌아온다(몸 블렌드에 맡기지 않는다).
// ⑦ 버팀 구간 오른손목 높이가 장마다 자주 뒤집히고 폭은 작다 — 「그 자리에서 덜덜 떨며 버틴다」.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Equipment.Chainsaw; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "MeshDescription.h"
#include "Snarehaven.h"

namespace SnarehavenChainsawThrustTest
{
	constexpr double LevelMaxDeg = 20.0;        // 관찰표 「수평으로」 — 수평에서 20° 넘게 서거나 처지면 아니다
	constexpr double StartMatchMaxCm = 2.0;     // 든 자세에서 출발 — 몸 리그(키 약 100cm) 기준
	constexpr float HitSeconds = 0.2f;          // 타격 = 클립 f6(재생 속도 1) = DESIGN 3.1 「공격 타격 지연」
	constexpr float ReturnSeconds = 0.3f;       // 끝 0.3초(f36~45)는 세로로 돌아오는 구간 — 그 직전까지가 버팀
	constexpr float FrameSeconds = 1.f / 30.f;  // 클립 장 간격
	constexpr double MinShakeReversalsPerSecond = 6.0;  // 「덜덜」 — 1초에 위아래를 세 번 넘게 오간다
	constexpr double MaxShakeCm = 3.0;          // 떨림이지 다시 휘두르기가 아니다
	const FName RightHandBone(TEXT("RightHand"));
	const FName LeftHandBone(TEXT("LeftHand"));
	const FName HipsBone(TEXT("Hips"));
	const FName ChestBone(TEXT("Spine2"));

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

	/** 클립의 Seconds 자세로 뼈를 놓는다 — 시험 월드는 틱이 없어 한 번 평가해야 화면과 같은 자세가 된다. */
	void SetPose(USkeletalMeshComponent* Body, UAnimSequence* Anim, float Seconds)
	{
		Body->PlayAnimation(Anim, false);
		Body->SetPosition(Seconds, false);
		Body->TickAnimation(0.f, false);
		Body->RefreshBoneTransforms();
	}

	/** 쥔 점에서 가장 먼 메시 꼭짓점 = 날 끝 (메시 공간). */
	bool BladeTip(const UStaticMesh* Mesh, const FVector& Grip, FVector& OutTip)
	{
		const FMeshDescription* const Description = Mesh->GetMeshDescription(0);
		if (Description == nullptr || Description->Vertices().Num() == 0)
		{
			return false;
		}
		OutTip = Grip;
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector Point(Description->GetVertexPosition(Vertex));
			OutTip = FVector::DistSquared(Point, Grip) > FVector::DistSquared(OutTip, Grip) ? Point : OutTip;
		}
		return true;
	}

	struct FThrustMeasure
	{
		double ElevationDeg = 0.0;
		double TipForward = 0.0;
		double RightForward = 0.0;
		double LeftForward = 0.0;
		double RightZ = 0.0;
		double LeftZ = 0.0;
		double HipsZ = 0.0;
		double ChestZ = 0.0;
		FVector Right = FVector::ZeroVector;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChainsawThrustTest, "Snarehaven.Equipment.Chainsaw.UseThrustsLevelAtWaist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenChainsawThrustTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenChainsawThrustTest;

	UWorld* const World = MakeWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	UEquipmentComponent* const Equipment = Player ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("장비 컴포넌트가 있다"), Equipment)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyWorld(World);
		return false;
	}
	USkeletalMeshComponent* const Body = Player->GetMesh();
	const FVector Origin = Player->GetActorLocation();
	const FVector Forward = Player->GetActorForwardVector();

	if (!TestTrue(TEXT("전기톱을 들었다"), Equipment->EquipItemByItemID(FName(TEXT("item.chainsaw")))))
	{
		DestroyWorld(World);
		return false;
	}
	const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Tool);
	UAnimSequence* const Use = Row ? Row->UseAnim.LoadSynchronous() : nullptr;
	UAnimSequence* const Hold = Row ? Row->HoldPoseAnim.LoadSynchronous() : nullptr;
	const UStaticMeshComponent* const Saw = Equipment->GetEquippedMesh(EEquipSlot::Tool);
	FVector TipLocal;
	if (!TestNotNull(TEXT("전기톱 행 쓰기 클립"), Use) || !TestNotNull(TEXT("전기톱 행 든 자세 클립"), Hold)
		|| !TestNotNull(TEXT("Tool 칸 메시"), Saw) || !TestTrue(TEXT("전기톱 행에 쥐는 자리가 있다"), Row->GripOffset.IsSet())
		|| !TestTrue(TEXT("전기톱 메시 꼭짓점"), BladeTip(Saw->GetStaticMesh(), Row->GripOffset.Location, TipLocal)))
	{
		DestroyWorld(World);
		return false;
	}

	const auto Measure = [&](UAnimSequence* Anim, float Seconds)
		{
			SetPose(Body, Anim, Seconds);
			const FTransform& SawWorld = Saw->GetComponentTransform();
			const FVector Grip = SawWorld.TransformPosition(Row->GripOffset.Location);
			const FVector Tip = SawWorld.TransformPosition(TipLocal);
			const FVector Axis = (Tip - Grip).GetSafeNormal();
			const FVector Right = Body->GetBoneLocation(RightHandBone);
			const FVector Left = Body->GetBoneLocation(LeftHandBone);
			FThrustMeasure M;
			M.ElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Axis.Z, -1.0, 1.0)));
			M.TipForward = FVector::DotProduct(Tip - Origin, Forward);
			M.RightForward = FVector::DotProduct(Right - Origin, Forward);
			M.LeftForward = FVector::DotProduct(Left - Origin, Forward);
			M.RightZ = Right.Z;
			M.LeftZ = Left.Z;
			M.HipsZ = Body->GetBoneLocation(HipsBone).Z;
			M.ChestZ = Body->GetBoneLocation(ChestBone).Z;
			M.Right = Right;
			UE_LOG(LogSnarehaven, Display,
				TEXT("[ChainsawThrust] %s %.2f초 톱 축 수평 위 %.1f° · 날 끝 앞 %.1f · 손목 앞 오른 %.1f 왼 %.1f · 손목 z 오른 %.1f 왼 %.1f (골반 %.1f 가슴 %.1f)"),
				*Anim->GetName(), Seconds, M.ElevationDeg, M.TipForward, M.RightForward, M.LeftForward,
				M.RightZ, M.LeftZ, M.HipsZ, M.ChestZ);
			return M;
		};
	const auto Level = [](const FThrustMeasure& M) { return FMath::Abs(M.ElevationDeg) <= LevelMaxDeg; };
	const auto AtWaist = [](const FThrustMeasure& M)
		{
			return FMath::Min(M.RightZ, M.LeftZ) > M.HipsZ && FMath::Max(M.RightZ, M.LeftZ) < M.ChestZ;
		};

	// 타격 = 0.2초 · 버팀 끝 = 돌아오기 직전 (정해진 길이 행이라 재생 속도 1 — 루프#387).
	const float Length = Use->GetPlayLength();
	for (const float Seconds : { HitSeconds, Length - ReturnSeconds })
	{
		const FThrustMeasure M = Measure(Use, Seconds);
		TestTrue(FString::Printf(TEXT("① %.2f초 톱 축이 수평 ±%.0f° 안 (%.1f°)"), Seconds, LevelMaxDeg, M.ElevationDeg), Level(M));
		TestTrue(FString::Printf(TEXT("② %.2f초 날 끝 %.1f 이 두 손목(%.1f·%.1f)보다 앞"), Seconds, M.TipForward, M.RightForward,
			M.LeftForward), M.TipForward > FMath::Max(M.RightForward, M.LeftForward) && M.RightForward > 0.0 && M.LeftForward > 0.0);
		TestTrue(FString::Printf(TEXT("③ %.2f초 손목 z %.1f/%.1f 이 골반 %.1f~가슴 %.1f 사이 (허리 높이)"), Seconds, M.RightZ, M.LeftZ,
			M.HipsZ, M.ChestZ), AtWaist(M));
	}

	const FThrustMeasure Start = Measure(Use, 0.f);
	const FThrustMeasure HoldStart = Measure(Hold, 0.f);
	const double StartGap = FVector::Dist(Start.Right, HoldStart.Right);
	TestTrue(FString::Printf(TEXT("④ 쓰기 첫 프레임 오른손목이 든 자세와 %.2fcm ≤ %.1f"), StartGap, StartMatchMaxCm), StartGap <= StartMatchMaxCm);
	TestTrue(FString::Printf(TEXT("⑤ 든 자세 %s(세로)는 기준에 걸린다 (축 %.1f°)"), *Hold->GetName(), HoldStart.ElevationDeg),
		!Level(HoldStart) || !AtWaist(HoldStart));
	const FThrustMeasure End = Measure(Use, Length);
	const double EndGap = FVector::Dist(End.Right, HoldStart.Right);
	TestTrue(FString::Printf(TEXT("⑥ 쓰기 끝 프레임 오른손목이 든 자세와 %.2fcm ≤ %.1f (축 %.1f°)"), EndGap, StartMatchMaxCm, End.ElevationDeg),
		EndGap <= StartMatchMaxCm);

	// ⑦ 버팀 구간(타격 ~ 돌아오기 직전)에 오른손목이 「그 자리에서 덜덜」 — 30fps 장마다 높이가 오르내림을 자주 뒤집고,
	//    폭은 작다(자리를 안 뜬다). 한 장씩 잰다 — 클립이 30fps 라 그보다 촘촘하면 보간 사이만 본다.
	TArray<double> HandZ;
	for (float Seconds = HitSeconds; Seconds <= Length - ReturnSeconds + KINDA_SMALL_NUMBER; Seconds += FrameSeconds)
	{
		SetPose(Body, Use, Seconds);
		HandZ.Add(Body->GetBoneLocation(RightHandBone).Z);
	}
	int32 Reversals = 0;
	for (int32 Index = 2; Index < HandZ.Num(); ++Index)
	{
		Reversals += (HandZ[Index] - HandZ[Index - 1]) * (HandZ[Index - 1] - HandZ[Index - 2]) < 0.0 ? 1 : 0;
	}
	const double HoldSeconds = Length - ReturnSeconds - HitSeconds;
	const double ReversalsPerSecond = HoldSeconds > 0.0 ? Reversals / HoldSeconds : 0.0;
	const double ShakeCm = FMath::Max(HandZ) - FMath::Min(HandZ);
	AddInfo(FString::Printf(TEXT("[ChainsawThrust] 버팀 %.2f초 · 손목 높이 뒤집힘 %d번(%.1f/초) · 떨림 폭 %.2fcm"),
		HoldSeconds, Reversals, ReversalsPerSecond, ShakeCm));
	TestTrue(FString::Printf(TEXT("⑦ 버팀 중 떨림 %.1f번/초 ≥ %.0f"), ReversalsPerSecond, MinShakeReversalsPerSecond),
		ReversalsPerSecond >= MinShakeReversalsPerSecond);
	TestTrue(FString::Printf(TEXT("⑦ 떨림 폭 %.2fcm 가 0~%.1f (자리를 안 뜬다)"), ShakeCm, MaxShakeCm), ShakeCm > 0.0 && ShakeCm <= MaxShakeCm);

	DestroyWorld(World);
	return true;
}

#endif
