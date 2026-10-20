// INBOX 감독←사용자 2026-10-04 15:30 ② 2층 검사 — 고급·전기 곡괭이 쓰기 (docs/reference/character_motion.md 「고급 곡괭이」·「전기 곡괭이」).
// 레퍼런스 원본 = 휘두르기 반복이 아니라 「전기톱처럼」 광맥에 **대고 버티며 갈아낸다**(전기 곡괭이는 감독 🟡 같은 동작).
// 두 행의 `UseAnim` 을 걸고 곡괭이를 든 뒤 잰다.
// ① 댄 자세(0.2초 = 타격 — DESIGN 3.1 「공격 타격 지연」, 정해진 길이 행이라 재생 속도 1 — 루프#387): 곡괭이 가장 낮은 점이 몸 앞 · 오른손목보다 아래 · 허리(골반)보다 아래
// ② 두 손: 왼손목이 쥔 점에서 15cm 안 (댄 자세)
// ③ 버팀: 댄 뒤(타격 ~ 돌아오기 직전, 끝 0.3초는 든 자세로 돌아오는 구간)에 오른손목이 떨리되(가장 먼 두 장 사이 0.3cm 넘음) 제자리 — 4cm 안
// ④ 첫 장 = 든 자세(행의 `HoldPoseAnim` 첫 장)와 오른손목 2cm 안 — 든 자세 → 쓰기 이음매가 없다
// ⑥ 끝 장 = 든 자세와 오른손목 2cm 안 — 한 번 누름(1.5초)이 끝나면 든 자세로 돌아와 있다(루프#387)
// ⑤ 역검증: 옛 `A_Player_MineTwoHand`(머리 위 내리찍기)는 ③ 「제자리」 에 걸린다 — 기준이 휘두르기를 가른다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Equipment.Pickaxe; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/ItemRow.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "MeshDescription.h"
#include "Snarehaven.h"

namespace SnarehavenPickaxeGrindTest
{
	constexpr double TwoHandMaxCm = 15.0;       // 한 손으로 쥐면 왼손목이 자루에서 15cm 넘게 떨어진다 (한 손 시험과 같은 선)
	constexpr double ShakeMinCm = 0.3;          // 떨림이 있다
	constexpr double StayMaxCm = 4.0;           // 제자리에서 버틴다 (휘두르면 수십 cm 움직인다)
	constexpr double HoldMatchMaxCm = 2.0;
	constexpr int32 Samples = 12;
	constexpr float HitSeconds = 0.2f;          // 타격 = 클립 f6(재생 속도 1) = DESIGN 3.1 「공격 타격 지연」
	constexpr float ReturnSeconds = 0.3f;       // 끝 0.3초(f36~45)는 든 자세로 돌아오는 구간
	const FName RightHandBone(TEXT("RightHand"));
	const FName LeftHandBone(TEXT("LeftHand"));
	const FName HipsBone(TEXT("Hips"));
	const TCHAR* const OldTwoHandAnim = TEXT("/Game/Game/Characters/Player/Snare/A_Player_MineTwoHand.A_Player_MineTwoHand");

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

	/** 곡괭이 메시 꼭짓점(메시 공간) — 자세마다 월드로 옮겨 가장 낮은 점을 찾는다. */
	bool MeshPoints(const UStaticMesh* Mesh, TArray<FVector>& OutPoints)
	{
		const FMeshDescription* const Description = Mesh->GetMeshDescription(0);
		if (Description == nullptr || Description->Vertices().Num() == 0)
		{
			return false;
		}
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			OutPoints.Add(FVector(Description->GetVertexPosition(Vertex)));
		}
		return true;
	}

	/** 구간 [From, To] 안 오른손목 자리들 중 가장 먼 두 장 사이 거리. */
	double WristTravel(USkeletalMeshComponent* Body, UAnimSequence* Anim, float From, float To)
	{
		TArray<FVector> Wrists;
		for (int32 Index = 0; Index <= Samples; ++Index)
		{
			SetPose(Body, Anim, FMath::Lerp(From, To, static_cast<float>(Index) / Samples));
			Wrists.Add(Body->GetBoneLocation(RightHandBone));
		}
		double Travel = 0.0;
		for (const FVector& A : Wrists)
		{
			for (const FVector& B : Wrists)
			{
				Travel = FMath::Max(Travel, FVector::Dist(A, B));
			}
		}
		return Travel;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPickaxeGrindTest, "Snarehaven.Equipment.Pickaxe.AdvancedElectricGrindInFront",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPickaxeGrindTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenPickaxeGrindTest;

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
	UAnimSequence* const OldTwoHand = LoadObject<UAnimSequence>(nullptr, OldTwoHandAnim);
	if (!TestNotNull(TEXT("옛 두 손 클립"), OldTwoHand))
	{
		DestroyWorld(World);
		return false;
	}

	for (const TCHAR* const ItemID : { TEXT("item.pickaxe_advanced"), TEXT("item.pickaxe_electric") })
	{
		if (!TestTrue(FString::Printf(TEXT("%s 를 들었다"), ItemID), Equipment->EquipItemByItemID(FName(ItemID))))
		{
			continue;
		}
		const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Tool);
		UAnimSequence* const Use = Row ? Row->UseAnim.LoadSynchronous() : nullptr;
		UAnimSequence* const Hold = Row ? Row->HoldPoseAnim.LoadSynchronous() : nullptr;
		const UStaticMeshComponent* const Pickaxe = Equipment->GetEquippedMesh(EEquipSlot::Tool);
		TArray<FVector> Points;
		if (!TestNotNull(*FString::Printf(TEXT("%s 쓰기 클립"), ItemID), Use) || !TestNotNull(*FString::Printf(TEXT("%s 든 자세 클립"), ItemID), Hold)
			|| !TestNotNull(*FString::Printf(TEXT("%s Tool 칸 메시"), ItemID), Pickaxe) || !TestTrue(*FString::Printf(TEXT("%s 쥐는 자리"), ItemID), Row->GripOffset.IsSet())
			|| !TestTrue(*FString::Printf(TEXT("%s 메시 꼭짓점"), ItemID), MeshPoints(Pickaxe->GetStaticMesh(), Points)))
		{
			continue;
		}

		// ① ② 댄 자세 = 타격 0.2초 (정해진 길이 행이라 재생 속도 1 — 루프#387).
		const float Length = Use->GetPlayLength();
		SetPose(Body, Use, HitSeconds);
		const FTransform& ToolWorld = Pickaxe->GetComponentTransform();
		FVector Low = ToolWorld.TransformPosition(Points[0]);
		for (const FVector& Point : Points)
		{
			const FVector P = ToolWorld.TransformPosition(Point);
			Low = P.Z < Low.Z ? P : Low;
		}
		const FVector Wrist = Body->GetBoneLocation(RightHandBone);
		const double HipsZ = Body->GetBoneLocation(HipsBone).Z;
		const double LowForward = FVector::DotProduct(Low - Origin, Forward);
		const double LeftToGrip = FVector::Dist(Body->GetBoneLocation(LeftHandBone), ToolWorld.TransformPosition(Row->GripOffset.Location));
		const double Travel = WristTravel(Body, Use, HitSeconds, Length - ReturnSeconds);
		SetPose(Body, Use, 0.f);
		const FVector FirstWrist = Body->GetBoneLocation(RightHandBone);
		SetPose(Body, Use, Length);
		const FVector LastWrist = Body->GetBoneLocation(RightHandBone);
		SetPose(Body, Hold, 0.f);
		const double HoldGap = FVector::Dist(FirstWrist, Body->GetBoneLocation(RightHandBone));
		const double EndGap = FVector::Dist(LastWrist, Body->GetBoneLocation(RightHandBone));
		UE_LOG(LogSnarehaven, Display,
			TEXT("[PickaxeGrind] %s %s %.2f초 가장 낮은 점 앞 %.1f z %.1f · 오른손목 z %.1f · 골반 z %.1f · 왼손목-쥔 점 %.1f · 댄 뒤 손목 움직임 %.2f · 첫 장-든 자세 %.2f · 끝 장(%.2f초)-든 자세 %.2f"),
			ItemID, *Use->GetName(), HitSeconds, LowForward, Low.Z, Wrist.Z, HipsZ, LeftToGrip, Travel, HoldGap, Length, EndGap);

		TestTrue(FString::Printf(TEXT("① %s 가장 낮은 점이 몸 앞(%.1f)"), ItemID, LowForward), LowForward > 0.0);
		TestTrue(FString::Printf(TEXT("① %s 가장 낮은 점 z %.1f 이 오른손목 %.1f·골반 %.1f 보다 아래"), ItemID, Low.Z, Wrist.Z, HipsZ),
			Low.Z < Wrist.Z && Low.Z < HipsZ);
		TestTrue(FString::Printf(TEXT("② %s 두 손 — 왼손목-쥔 점 %.1f ≤ %.0fcm"), ItemID, LeftToGrip, TwoHandMaxCm), LeftToGrip <= TwoHandMaxCm);
		TestTrue(FString::Printf(TEXT("③ %s 댄 뒤 떨림 %.2fcm > %.1f · 제자리 ≤ %.1f"), ItemID, Travel, ShakeMinCm, StayMaxCm),
			Travel > ShakeMinCm && Travel <= StayMaxCm);
		TestTrue(FString::Printf(TEXT("④ %s 첫 장 오른손목이 든 자세와 %.2fcm ≤ %.1f"), ItemID, HoldGap, HoldMatchMaxCm), HoldGap <= HoldMatchMaxCm);
		TestTrue(FString::Printf(TEXT("⑥ %s 끝 장 오른손목이 든 자세와 %.2fcm ≤ %.1f"), ItemID, EndGap, HoldMatchMaxCm), EndGap <= HoldMatchMaxCm);
	}

	const float OldLength = OldTwoHand->GetPlayLength();
	const double OldTravel = WristTravel(Body, OldTwoHand, OldLength / 3.f, OldLength);
	UE_LOG(LogSnarehaven, Display, TEXT("[PickaxeGrind] 역검증 %s 1/3 뒤 손목 움직임 %.2f"), *OldTwoHand->GetName(), OldTravel);
	TestTrue(FString::Printf(TEXT("⑤ 옛 %s(휘두르기)는 제자리 기준에 걸린다 (%.2fcm > %.1f)"), *OldTwoHand->GetName(), OldTravel, StayMaxCm),
		OldTravel > StayMaxCm);

	DestroyWorld(World);
	return true;
}

#endif
