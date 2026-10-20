// INBOX 감독 지시 2026-10-04 ③ 2층 검사 — 전기톱 들기 자세 (docs/reference/character_motion.md 「전기톱 — 들고 있을 때」).
// 레퍼런스 원본 = 톱을 **세로로 세워 날이 위**, **두 손으로 가슴 앞**. 전기톱 행의 `HoldPoseAnim` 첫 프레임을 걸고 전기톱을 든 뒤 잰다.
// ① 쥔 점(행 `GripOffset`)→날 끝(쥔 점에서 가장 먼 메시 꼭짓점) 축이 수평 위 ≥ 60° — 세로
// ② 날 끝·두 손목이 폰 앞(앞 벡터 내적 > 0) — 몸 앞
// ③ 두 손목 높이가 골반 뼈 위·머리 뼈 아래, 두 손목 사이 ≤ 15cm × 몸 배율 — 두 손 가슴 앞
// ④ 역검증: 옛 자세 `A_Player_HoldTwoHand`(2hand Idle — 루프#1354 부터 쓰는 행 없음)는 ① 이나 ③ 에 걸린다 — 기준이 옛 자세를 가른다.
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

namespace SnarehavenChainsawHoldTest
{
	constexpr double UprightMinDeg = 60.0;      // 관찰표 「세로로 세워」 — 수평 위 60° 밑이면 눕혀 든 것으로 본다
	constexpr double HandsApartMaxCm = 15.0;    // 「두 손으로」 — 몸 리그(키 약 100cm) 기준 한 뼘
	const TCHAR* const OldHoldPath = TEXT("/Game/Game/Characters/Player/Snare/A_Player_HoldTwoHand.A_Player_HoldTwoHand");
	const FName RightHandBone(TEXT("RightHand"));
	const FName LeftHandBone(TEXT("LeftHand"));
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

	/** 클립 첫 프레임 자세로 뼈를 놓는다 — 시험 월드는 틱이 없어 한 번 평가해야 화면과 같은 자세가 된다. */
	void SetPose(USkeletalMeshComponent* Body, UAnimSequence* Anim)
	{
		Body->PlayAnimation(Anim, false);
		Body->SetPosition(0.f, false);
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

	struct FHoldMeasure
	{
		double ElevationDeg = 0.0;
		double TipForward = 0.0;
		double RightForward = 0.0;
		double LeftForward = 0.0;
		double HandsApart = 0.0;
		double RightZ = 0.0;
		double LeftZ = 0.0;
		double HipsZ = 0.0;
		double HeadZ = 0.0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChainsawHoldTest, "Snarehaven.Equipment.Chainsaw.HoldUprightTwoHandsChest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenChainsawHoldTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenChainsawHoldTest;

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
	const double BodyScale = Body->GetComponentScale().Z;
	const FVector Origin = Player->GetActorLocation();
	const FVector Forward = Player->GetActorForwardVector();

	if (!TestTrue(TEXT("전기톱을 들었다"), Equipment->EquipItemByItemID(FName(TEXT("item.chainsaw")))))
	{
		DestroyWorld(World);
		return false;
	}
	const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Tool);
	UAnimSequence* const Hold = Row ? Row->HoldPoseAnim.LoadSynchronous() : nullptr;
	UAnimSequence* const OldHold = LoadObject<UAnimSequence>(nullptr, OldHoldPath);
	const UStaticMeshComponent* const Saw = Equipment->GetEquippedMesh(EEquipSlot::Tool);
	FVector TipLocal;
	if (!TestNotNull(TEXT("전기톱 행 든 자세 클립"), Hold) || !TestNotNull(TEXT("옛 두 손 자세 클립"), OldHold)
		|| !TestNotNull(TEXT("Tool 칸 메시"), Saw) || !TestTrue(TEXT("전기톱 행에 쥐는 자리가 있다"), Row->GripOffset.IsSet())
		|| !TestTrue(TEXT("전기톱 메시 꼭짓점"), BladeTip(Saw->GetStaticMesh(), Row->GripOffset.Location, TipLocal)))
	{
		DestroyWorld(World);
		return false;
	}

	// 자세를 걸면 붙은 톱도 손 소켓을 따라간다(쥐는 자리는 소켓 공간 상대값으로 남는다).
	const auto Measure = [&](UAnimSequence* Anim)
		{
			SetPose(Body, Anim);
			const FTransform& SawWorld = Saw->GetComponentTransform();
			const FVector Grip = SawWorld.TransformPosition(Row->GripOffset.Location);
			const FVector Tip = SawWorld.TransformPosition(TipLocal);
			const FVector Axis = (Tip - Grip).GetSafeNormal();
			const FVector Right = Body->GetBoneLocation(RightHandBone);
			const FVector Left = Body->GetBoneLocation(LeftHandBone);
			FHoldMeasure M;
			M.ElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Axis.Z, -1.0, 1.0)));
			M.TipForward = FVector::DotProduct(Tip - Origin, Forward);
			M.RightForward = FVector::DotProduct(Right - Origin, Forward);
			M.LeftForward = FVector::DotProduct(Left - Origin, Forward);
			M.HandsApart = FVector::Dist(Right, Left);
			M.RightZ = Right.Z;
			M.LeftZ = Left.Z;
			M.HipsZ = Body->GetBoneLocation(HipsBone).Z;
			M.HeadZ = Body->GetBoneLocation(HeadBone).Z;
			UE_LOG(LogSnarehaven, Display,
				TEXT("[ChainsawHold] %s 톱 축 수평 위 %.1f° · 날 끝 앞 %.1f · 손목 앞 오른 %.1f 왼 %.1f · 두 손 사이 %.1fcm · 손목 z 오른 %.1f 왼 %.1f (골반 %.1f 머리 %.1f)"),
				*Anim->GetName(), M.ElevationDeg, M.TipForward, M.RightForward, M.LeftForward, M.HandsApart,
				M.RightZ, M.LeftZ, M.HipsZ, M.HeadZ);
			return M;
		};
	const auto HandsAtChest = [&](const FHoldMeasure& M)
		{
			return M.HandsApart <= HandsApartMaxCm * BodyScale
				&& FMath::Min(M.RightZ, M.LeftZ) > M.HipsZ && FMath::Max(M.RightZ, M.LeftZ) < M.HeadZ;
		};

	const FHoldMeasure Now = Measure(Hold);
	TestTrue(FString::Printf(TEXT("① 톱 축이 수평 위 %.1f° ≥ %.0f° (세로)"), Now.ElevationDeg, UprightMinDeg),
		Now.ElevationDeg >= UprightMinDeg);
	TestTrue(FString::Printf(TEXT("② 날 끝이 몸 앞 %.1f > 0"), Now.TipForward), Now.TipForward > 0.0);
	TestTrue(FString::Printf(TEXT("② 두 손목이 몸 앞 (오른 %.1f · 왼 %.1f > 0)"), Now.RightForward, Now.LeftForward),
		Now.RightForward > 0.0 && Now.LeftForward > 0.0);
	TestTrue(FString::Printf(TEXT("③ 두 손 사이 %.1f ≤ %.1f · 손목 z %.1f/%.1f 이 골반 %.1f~머리 %.1f 사이 (가슴 앞)"),
		Now.HandsApart, HandsApartMaxCm * BodyScale, Now.RightZ, Now.LeftZ, Now.HipsZ, Now.HeadZ), HandsAtChest(Now));

	const FHoldMeasure Old = Measure(OldHold);
	TestTrue(FString::Printf(TEXT("④ 옛 자세 %s 는 기준에 걸린다 (축 %.1f° · 두 손 사이 %.1f)"), *OldHold->GetName(),
		Old.ElevationDeg, Old.HandsApart), Old.ElevationDeg < UprightMinDeg || !HandsAtChest(Old));

	DestroyWorld(World);
	return true;
}

#endif
