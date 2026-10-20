// M13-43 2층 검사 — 낚싯대 4종·하이킹 배낭 진짜 메시 (DESIGN 3.5 「도구 크기」 낚싯대 110cm · 배낭 35cm).
// 두 행은 몸 리그(키 약 100cm) 기준 크기라 붙을 때 몸 배율을 따른다(`FItemRow::bEquipFollowsBodyScale`).
// ① 행으로 들면 Tool 칸 메시 = `SM_Tool_<Name>_01`, 월드 참길이(손잡이 끝–끝) = 110 × 몸 배율 ± 3, LOD0 삼각형 1~3000
//    (M13-45 부터 낚싯대는 돌려 받아 상자 긴 변이 대각선이다 — 그래서 긴 변 대신 꼭짓점으로 잰 참길이)
// ② 낚싯대 다음 손도끼(몸 배율 안 따르는 행)를 들면 월드 긴 변이 다시 표 값 80 ± 3 — 돌려쓰는 컴포넌트에 배율이 안 남는다
// ③ 배낭 → Backpack 칸 메시 = `SM_Backpack_HikingBackpack_01`, 소켓 `Socket_Back` 이 몸에 있고 거기 붙었다,
//    긴 변 = 35 × 몸 배율 ± 3, 배낭 가운데가 가슴 뼈보다 등 쪽(캐릭터 앞 벡터와 내적 < 0)
// ④ M13-44(감독 3층 실패 #0238): 낚싯대 가장 낮은 점 > 발(캡슐 바닥) ·
//    배낭 몸 쪽 면과 등 표면(가슴 뼈 − 앞 벡터 × 10.6 × 몸 배율) 사이 틈 0~3cm(뚫지도 뜨지도 않는다)
//    ④-2 루프#849(감독 보정 「배낭이 등에서 뜸」): 실제 몸 꼭짓점과 배낭 꼭짓점을 가슴 뼈 로컬에서 재 평균 틈 −1~2cm
// ⑤ M13-45(감독 3층 결함 #0240 — 끝이 몸 뒤 86.5): 두 자세(Idle·FishIdle 첫 프레임)에서 (a) 끝이 폰 앞쪽 > 0
//    (b) FishIdle 손잡이→끝 축이 수평 위 30~60° · |오른쪽 성분| ≤ 앞 성분 × 0.5 (c) Idle 최저 z > 발 z(루프#402 부터 ⑧ HoldRod 가 판정) (d) 손잡이→끝 선분이 배낭 상자와 안 만난다.
// ⑥ 루프#388(INBOX 15:30 ③ · 관찰표 21·22행 · 감독 PIE 「기다리기·감기 동안 대가 옆으로 짧게 삐져 물 쪽을 안 가리킴」):
//    (b) 를 바꿨다 — FishIdle(기다리기) 축 수평 위 0~25° · 끝이 몸 앞으로 참길이의 절반 이상, FishReel(감기) 첫 프레임 축 수평 위 55~85° · 축 앞 성분 < 0(어깨 너머 뒤로).
// ⑦ 루프#389(INBOX 15:30 ③ · 관찰표 20행 「예비 동작 없이 한 번에 물 쪽으로 홱, 대가 앞 아래로, 0.5초 미만」): FishCast 길이 < 0.5초 ·
//    내려친 장(0.2초) 대 끝이 몸 앞 · 축이 수평 아래 · 끝 장 축이 FishIdle 첫 장 축과 3° 안(이어 붙어 튀지 않는다).
// ⑧ 루프#390(INBOX 15:30 ③ · 관찰표 19행 「한 손으로 손잡이 끝을 허리·엉덩이 옆에 쥐고 대를 거의 세워 뒤쪽 위로」): 든 자세 HoldRod
//    첫 프레임 축 수평 위 50~85° · 축 앞 성분 < 0(뒤로) · 손잡이 끝이 허리 아래 · 배낭 안 만남 · 발 안 뚫음, 그리고 낚싯대를 들면
//    몸 행동 층 = HoldRod(혼자 들어도, 몽둥이와 같이 들어도) — 몽둥이와 같이 들면 휘두르기는 몽둥이 클립 그대로.
// ⑨ 2026-10-05 감독 지시(레퍼런스 UlFyyrxMNt8 1:50 「한 손(오른손)」 · 관찰표 19행): 낚싯대 4종은 오른손 `Socket_Weapon` 에 붙고,
//    네 자세에서 손잡이 끝이 손 소켓 가까이(≤ 참길이 × 0.3) · 끝이 손에서 참길이 × 0.7 이상 · 끝이 몸 캡슐 밖(수평 거리 > 캡슐 반지름).
//    참조 자세에서 오른손 쥐는 각(`create_data_tables.py` ROD_GRIP) 계산 근거도 찍는다.
//    끝·손잡이 끝·최저 z 는 메시 원본 꼭짓점에서 고른다(피벗에서 가장 먼 점 = 끝, 그 반대쪽 가장 먼 점 = 손잡이 끝).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Equipment.RodBackpack; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "Snarehaven.h"
#include "Player/PlayerBodyAnimInstance.h"
#include "Combat/MeleeAttackComponent.h"

namespace SnarehavenRodBackpackMeshTest
{
	// DESIGN 3.5 「도구 크기」 표 값.
	constexpr double RodLongCm = 110.0;
	constexpr double BackpackLongCm = 45.0;
	constexpr double HandAxeLongCm = 80.0;   // DESIGN 3.5 「도구 크기」 손도끼(2026-10-05 월드 cm)
	constexpr double ToleranceCm = 3.0;
	constexpr int32 TriangleLimit = 3000;
	// DESIGN 3.5 하이킹 배낭 행 — 가슴 뼈에서 등 표면까지(메시 공간 cm). `setup_equipment_socket.py` BACK_SURFACE_CM 과 같다.
	constexpr double BackSurfaceCm = 10.2;
	constexpr double BackGapMaxCm = 3.0;
	// 실제 몸 꼭짓점과 배낭 꼭짓점을 가슴 뼈 로컬에서 잰 틈 — 등 가운데(옆 ±6cm)의 위로 0·5·10·15cm 칸(칸 폭 ±2.5).
	// 평균 ≤2cm 는 감독 보정 「배낭 앞면이 몸통 뒤 표면에서 ≤2cm」. 칸별 한도는 등이 굽고 배낭이 둥글어 생기는 모양 차이를 받는다.
	constexpr double BackSliceHeightsCm[] = {0.0, 5.0, 10.0, 15.0};
	constexpr double BackSliceHalfCm = 2.5;
	constexpr double BackSliceSideCm = 6.0;
	constexpr double RealGapMeanMinCm = 1.0;
	constexpr double RealGapMeanMaxCm = 2.0;
	constexpr double RealGapSliceMaxCm = 4.0;
	constexpr double RealGapSliceBuriedCm = 6.0;
	constexpr double FloatSlackCm = 0.05;   // 틈 0 은 경계라 부동소수 오차만큼만 봐준다 — 뚫림(음수)은 그대로 잡힌다
	const FName BackSocket(TEXT("Socket_Back"));
	// DESIGN 3.5 낚싯대 행 — 입질 대기(FishIdle) 첫 프레임에서 손잡이→끝 축의 수평 위 각도 범위.
	// 루프#388: 레퍼런스 「허리 높이에서 거의 수평으로 물 쪽」 — 옛 30~60°(M13-45)는 위 카메라에서 대가 줄어 보였다.
	constexpr double FishElevationMinDeg = 0.0;
	constexpr double FishElevationMaxDeg = 25.0;
	constexpr double FishSideRatioMax = 0.5;   // 옆(오른쪽) 성분이 앞 성분의 절반 이하 — 옆으로 뻗지 않는다
	constexpr double FishTipReachRatio = 0.5;  // 끝이 몸 앞으로 참길이의 절반 이상 — 짧게 삐져 보이지 않는다
	// 루프#388 감기(FishReel) — 레퍼런스 「대를 세워 어깨 너머 뒤로 당긴」: 많이 세우고 끝이 뒤로 간다.
	constexpr double ReelElevationMinDeg = 55.0;
	constexpr double ReelElevationMaxDeg = 85.0;
	// 루프#389 던지기(FishCast) — DESIGN 8.9 「낚시 몸 클립」 행.
	constexpr double CastMaxSeconds = 0.5;
	constexpr double CastStrikeSeconds = 0.2;   // 대를 앞 아래로 내려친 장(`fish_cast_make.py` f6)
	constexpr double CastJoinMaxDeg = 3.0;
	// 루프#390 든 자세(HoldRod) — 레퍼런스 「대를 거의 세워 뒤쪽 위로」.
	constexpr double HoldElevationMinDeg = 50.0;
	constexpr double HoldElevationMaxDeg = 85.0;
	// ⑨ 오른손 쥠 — 손잡이 끝은 손 가까이, 끝은 손에서 멀리.
	const FName RodSocket(TEXT("Socket_Weapon"));
	constexpr double GripNearRatio = 0.3;
	constexpr double TipFarRatio = 0.7;
	const TCHAR* const HoldRodName = TEXT("A_Player_HoldRod");
	const TCHAR* const CastPose = TEXT("/Game/Game/Characters/Player/Snare/A_Player_FishCast.A_Player_FishCast");
	// 게임이 거는 클립(`ASnarehavenCharacter` 가 읽는 경로와 같다) — 서 있기·입질 대기·감기.
	const TCHAR* const Poses[] = {
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle"),
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_FishIdle.A_Player_FishIdle"),
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_FishReel.A_Player_FishReel"),
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_HoldRod.A_Player_HoldRod"),
	};

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

	/** 기울기를 뺀 메시 크기 × 월드 배율의 가장 긴 변 — `UEquipmentComponent` 로그의 `모양` 과 같은 값. */
	double WorldLongSide(const UStaticMeshComponent* Visual)
	{
		return (Visual->GetStaticMesh()->GetBounds().BoxExtent * 2.0 * Visual->GetComponentScale()).GetMax();
	}

	/** 메시 로컬 상자 8 꼭짓점을 월드로 옮겨 Direction 방향으로 가장 먼 값 — 월드 AABB 는 돌면 부푼다. */
	double FarthestAlong(const UStaticMeshComponent* Visual, const FVector& Direction)
	{
		const FBox Local = Visual->GetStaticMesh()->GetBoundingBox();
		double Best = -UE_BIG_NUMBER;
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Point((Corner & 1) ? Local.Max.X : Local.Min.X, (Corner & 2) ? Local.Max.Y : Local.Min.Y,
				(Corner & 4) ? Local.Max.Z : Local.Min.Z);
			Best = FMath::Max(Best, FVector::DotProduct(Visual->GetComponentTransform().TransformPosition(Point), Direction));
		}
		return Best;
	}

	/** 클립의 Seconds 자세로 뼈를 놓는다(기본 첫 프레임) — 시험 월드는 틱이 없어 한 번 평가해야 화면과 같은 자세가 된다. */
	bool SetPose(USkeletalMeshComponent* Body, const TCHAR* AnimPath, float Seconds = 0.f)
	{
		UAnimSequence* const Anim = LoadObject<UAnimSequence>(nullptr, AnimPath);
		if (Anim == nullptr)
		{
			return false;
		}
		Body->PlayAnimation(Anim, false);
		Body->SetPosition(FMath::Min(Seconds, Anim->GetPlayLength()), false);
		Body->TickAnimation(0.f, false);
		Body->RefreshBoneTransforms();
		return true;
	}

	/** 메시 원본 꼭짓점에서 피벗(손 소켓)에서 가장 먼 점 = 끝, 그 반대쪽으로 가장 먼 점 = 손잡이 끝. 메시 공간. */
	bool RodEnds(const UStaticMesh* Mesh, FVector& OutGrip, FVector& OutTip)
	{
		const FMeshDescription* const Description = Mesh->GetMeshDescription(0);
		if (Description == nullptr || Description->Vertices().Num() == 0)
		{
			return false;
		}
		OutTip = FVector::ZeroVector;
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector Point(Description->GetVertexPosition(Vertex));
			OutTip = Point.SizeSquared() > OutTip.SizeSquared() ? Point : OutTip;
		}
		const FVector Axis = OutTip.GetSafeNormal();
		OutGrip = FVector::ZeroVector;
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			const FVector Point(Description->GetVertexPosition(Vertex));
			OutGrip = FVector::DotProduct(Point, Axis) < FVector::DotProduct(OutGrip, Axis) ? Point : OutGrip;
		}
		return true;
	}

	/** 메시 원본 꼭짓점을 월드로 옮긴 가장 낮은 z — 돌린 메시의 상자 꼭짓점은 실제보다 낮게 나온다. */
	double LowestVertexZ(const UStaticMeshComponent* Visual)
	{
		const FMeshDescription* const Description = Visual->GetStaticMesh()->GetMeshDescription(0);
		double Lowest = UE_BIG_NUMBER;
		for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
		{
			Lowest = FMath::Min(Lowest, Visual->GetComponentTransform().TransformPosition(FVector(Description->GetVertexPosition(Vertex))).Z);
		}
		return Lowest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRodBackpackMeshTest, "Snarehaven.Equipment.RodBackpack.RealMeshes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRodBackpackMeshTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenRodBackpackMeshTest;

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
	const double FootZ = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Origin = Player->GetActorLocation();
	const FVector Forward = Player->GetActorForwardVector();
	const FVector Right = Player->GetActorRightVector();
	// ⑧ 게임이 거는 든 자세 — 낚싯대만, 그리고 몽둥이와 같이. 아래 `SetPose` 가 몸을 단일 클립 재생으로 바꾸기 전에 본다. 행 칸(`HoldPoseAnim`)과 고르는 코드(`ResolveHeldBodyClips`)를 같이 본다.
	{
		UPlayerBodyAnimInstance* const BodyAnim = Player->GetBodyAnimInstance();
		UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
		Equipment->UnequipSlot(EEquipSlot::Weapon);
		TestTrue(TEXT("item.rod_basic 를 들었다 (든 자세)"), Equipment->EquipItemByItemID(FName(TEXT("item.rod_basic"))));
		Player->UpdateBodyAnimation();
		const FString RodOnly = GetNameSafe(BodyAnim != nullptr ? BodyAnim->GetActionClip() : nullptr);
		TestEqual(TEXT("낚싯대만 들면 몸 행동 층 = HoldRod"), RodOnly, FString(HoldRodName));
		TestTrue(TEXT("item.club 를 같이 들었다"), Equipment->EquipItemByItemID(FName(TEXT("item.club"))));
		Player->UpdateBodyAnimation();
		const FString WithClub = GetNameSafe(BodyAnim != nullptr ? BodyAnim->GetActionClip() : nullptr);
		TestEqual(TEXT("몽둥이와 같이 들어도 몸 행동 층 = HoldRod"), WithClub, FString(HoldRodName));
		FString Swing = TEXT("None");
		if (TestNotNull(TEXT("근접 공격 컴포넌트"), Melee))
		{
			Melee->StartAttacking();
			Player->UpdateBodyAnimation();
			Swing = GetNameSafe(Player->GetCurrentBodyAnim());
			Melee->StopAttacking();
			TestEqual(TEXT("몽둥이와 같이 들면 휘두르기 = 몽둥이 쓰기 클립"), Swing, FString(TEXT("A_Player_SwingHorizontal")));
		}
		UE_LOG(LogSnarehaven, Display, TEXT("[RodBackpack] 든 자세: 낚싯대만 %s · 몽둥이와 같이 %s · 휘두르기 %s"), *RodOnly, *WithClub, *Swing);
		Equipment->UnequipSlot(EEquipSlot::Weapon);
	}

	// ⑨ 오른손 쥐는 각 근거 — 참조 자세(T자, 좌우 대칭)에서 옛 왼손(Socket_Tool, 쥐는 각 0) 대의 거울 자리를 오른손 소켓에서
	// 만드는 각 Q = R오른⁻¹ · S · R왼 · H. S = 폰 오른쪽 축 거울, H = 메시 대칭면(대 축과 릴 쪽 방향을 담는 면) 거울 — 두 거울이라 회전.
	{
		const UStaticMesh* const RodMesh = LoadObject<UStaticMesh>(nullptr,
			TEXT("/Game/Game/Characters/Equipment/Tools/FishingRod/SM_Tool_FishingRod_01.SM_Tool_FishingRod_01"));
		FVector GripLocal;
		FVector TipLocal;
		if (RodMesh != nullptr && RodEnds(RodMesh, GripLocal, TipLocal))
		{
			const FVector M = (TipLocal - GripLocal).GetSafeNormal();
			FVector Sum = FVector::ZeroVector;
			const FMeshDescription* const Description = RodMesh->GetMeshDescription(0);
			for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
			{
				Sum += FVector(Description->GetVertexPosition(Vertex));
			}
			const FVector Centroid = Sum / Description->Vertices().Num() - GripLocal;
			const FVector Side = Centroid - FVector::DotProduct(Centroid, M) * M;   // 릴·고리 쪽(대칭면 안)
			const FVector N = FVector::CrossProduct(M, Side).GetSafeNormal();
			// 참조 자세 소켓 회전은 뼈 대기 자세를 거슬러 올라가 곱해 얻는다 — 몸이 지금 어떤 자세로 평가돼 있든 상관없다.
			auto RefSocketQuat = [Body](FName SocketName)
			{
				const USkeletalMesh* const Mesh = Body->GetSkeletalMeshAsset();
				const USkeletalMeshSocket* const Socket = Mesh->FindSocket(SocketName);
				const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
				FQuat Rotation = Socket->RelativeRotation.Quaternion();
				for (int32 Bone = Ref.FindBoneIndex(Socket->BoneName); Bone != INDEX_NONE; Bone = Ref.GetParentIndex(Bone))
				{
					Rotation = Ref.GetRefBonePose()[Bone].GetRotation() * Rotation;
				}
				return Body->GetComponentQuat() * Rotation;
			};
			const FQuat Left = RefSocketQuat(FName(TEXT("Socket_Tool")));
			const FQuat RightHand = RefSocketQuat(RodSocket);
			auto Q = [&](const FVector& V)
			{
				const FVector Mirrored = V - 2.0 * FVector::DotProduct(V, N) * N;
				const FVector World = Left.RotateVector(Mirrored);
				return RightHand.UnrotateVector(World - 2.0 * FVector::DotProduct(World, Right) * Right);
			};
			const FRotator Grip = FMatrix(Q(FVector::XAxisVector), Q(FVector::YAxisVector), Q(FVector::ZAxisVector), FVector::ZeroVector).Rotator();
			UE_LOG(LogSnarehaven, Display,
				TEXT("[RodBackpack] 오른손 쥐는 각 근거: 메시 축 %s · 릴 쪽 %s (%.2fcm) · 참조 자세 왼손 소켓 %s · 오른손 소켓 %s → Pitch=%.2f Yaw=%.2f Roll=%.2f"),
				*M.ToString(), *Side.GetSafeNormal().ToString(), Side.Size(), *Left.ToString(), *RightHand.ToString(), Grip.Pitch, Grip.Yaw, Grip.Roll);
		}
	}

	// 틱이 없는 시험 월드는 뼈가 참조 자세(T자)로 남는다 — 손이 옆으로 뻗어 낚싯대가 눕는다. 게임이 거는 클립
	// 첫 프레임을 한 번 평가해 화면과 같은 자세에서 잰다(감독 3층 #0238·#0240 도 서 있는 자세였다).
	if (!TestTrue(TEXT("서 있기 클립 첫 프레임 자세"), SetPose(Body, Poses[0])))
	{
		DestroyWorld(World);
		return false;
	}
	const UAnimSingleNodeInstance* const Single = Body->GetSingleNodeInstance();
	const FString PoseName = Single && Single->GetAnimationAsset() ? Single->GetAnimationAsset()->GetName() : TEXT("참조 자세");
	UE_LOG(LogSnarehaven, Display, TEXT("[RodBackpack] 잰 자세 %s"), *PoseName);

	// ③ 배낭 — 낚싯대 선분이 배낭을 지나는지(⑤ d) 보려고 먼저 멘다.
	TestTrue(TEXT("몸에 Socket_Back 소켓이 있다"), Body->DoesSocketExist(BackSocket));
	TestTrue(TEXT("item.hiking_backpack 을 멨다"), Equipment->EquipItemByItemID(FName(TEXT("item.hiking_backpack"))));
	UStaticMeshComponent* const Pack = Equipment->GetEquippedMesh(EEquipSlot::Backpack);
	if (!TestNotNull(TEXT("Backpack 칸 메시 컴포넌트"), Pack) || !TestNotNull(TEXT("배낭 메시"), Pack->GetStaticMesh().Get()))
	{
		DestroyWorld(World);
		return false;
	}
	TestEqual(TEXT("배낭 메시 이름"), Pack->GetStaticMesh()->GetName(), FString(TEXT("SM_Backpack_HikingBackpack_01")));
	TestEqual(TEXT("배낭이 붙은 소켓"), Pack->GetAttachSocketName(), BackSocket);
	TestEqual(TEXT("배낭이 붙은 몸"), Pack->GetAttachParent(), static_cast<USceneComponent*>(Body));
	const double PackLong = WorldLongSide(Pack);
	const int32 PackTriangles = Pack->GetStaticMesh()->GetNumTriangles(0);
	TestTrue(FString::Printf(TEXT("배낭 긴 변 %.1f ≈ %.1f × 몸 배율 %.3f ± %.0f"), PackLong, BackpackLongCm, BodyScale, ToleranceCm),
		FMath::IsNearlyEqual(PackLong, BackpackLongCm * BodyScale, ToleranceCm));
	TestTrue(FString::Printf(TEXT("배낭 삼각형 %d 이 1~%d"), PackTriangles, TriangleLimit),
		PackTriangles > 0 && PackTriangles <= TriangleLimit);

	const FName ChestBone = Body->GetSocketBoneName(BackSocket);
	const FVector Chest = Body->GetBoneLocation(ChestBone);
	const FVector PackCenter = Pack->CalcBounds(Pack->GetComponentTransform()).Origin;
	const double Back = FVector::DotProduct(PackCenter - Chest, Forward);
	TestTrue(FString::Printf(TEXT("배낭 가운데가 가슴 뼈 %s 보다 등 쪽 (앞 벡터 내적 %.1f < 0)"), *ChestBone.ToString(), Back),
		Back < 0.0);
	// ④-2 실제 몸 등 표면과 배낭 앞끝의 틈 — 위 ④ 의 「등 표면 = 가슴 − 10.6」는 가정한 값이라 몸 메시를 바꾸면 어긋난 채로 통과한다
	// (감독 옆면 캡처 `m_w3`: 배낭이 등에서 한 마디 떠 있는데 ④ 는 0.00 이었다). 가슴 뼈 로컬(메시 cm)에서 높이 칸마다
	// 몸(참조 자세 꼭짓점)의 가장 뒤와 배낭 꼭짓점의 가장 앞을 비교한다. 양수 = 뜸, 음수 = 몸에 묻힘.
	{
		const USkeletalMesh* const BodyAsset = Body->GetSkeletalMeshAsset();
		const FReferenceSkeleton& RefSkel = BodyAsset->GetRefSkeleton();
		FTransform ChestRef = FTransform::Identity;
		for (int32 Bone = RefSkel.FindBoneIndex(ChestBone); Bone != INDEX_NONE; Bone = RefSkel.GetParentIndex(Bone))
		{
			ChestRef = ChestRef * RefSkel.GetRefBonePose()[Bone];
		}
		const FTransform ChestNow = Body->GetBoneTransform(ChestBone);
		const FVector Fwd = ChestNow.InverseTransformVectorNoScale(Forward).GetSafeNormal();
		const FVector UpL = ChestNow.InverseTransformVectorNoScale(FVector::UpVector).GetSafeNormal();
		const FVector SideL = FVector::CrossProduct(UpL, Fwd).GetSafeNormal();
		const FMeshDescription* const BodyDesc = BodyAsset->GetMeshDescription(0);
		const FMeshDescription* const PackDesc = Pack->GetStaticMesh()->GetMeshDescription(0);
		if (TestNotNull(TEXT("몸 메시 원본 설명"), BodyDesc) && TestNotNull(TEXT("배낭 메시 원본 설명"), PackDesc))
		{
			const TVertexAttributesConstRef<FVector3f> BodyPos = BodyDesc->GetVertexPositions();
			const TVertexAttributesConstRef<FVector3f> PackPos = PackDesc->GetVertexPositions();
			double GapSum = 0.0, GapMin = UE_BIG_NUMBER, GapMax = -UE_BIG_NUMBER;
			int32 Slices = 0;
			FString Report;
			for (const double Height : BackSliceHeightsCm)
			{
				double BodyBack = UE_BIG_NUMBER, PackFront = -UE_BIG_NUMBER;
				for (const FVertexID Vertex : BodyDesc->Vertices().GetElementIDs())
				{
					const FVector Local = ChestRef.InverseTransformPosition(FVector(BodyPos[Vertex]));
					if (FMath::Abs(FVector::DotProduct(Local, UpL) - Height) < BackSliceHalfCm && FMath::Abs(FVector::DotProduct(Local, SideL)) < BackSliceSideCm)
					{
						BodyBack = FMath::Min(BodyBack, FVector::DotProduct(Local, Fwd));
					}
				}
				for (const FVertexID Vertex : PackDesc->Vertices().GetElementIDs())
				{
					const FVector PackWorld = Pack->GetComponentTransform().TransformPosition(FVector(PackPos[Vertex]));
					const FVector Local = ChestNow.InverseTransformPosition(PackWorld);
					if (FMath::Abs(FVector::DotProduct(Local, UpL) - Height) < BackSliceHalfCm && FMath::Abs(FVector::DotProduct(Local, SideL)) < BackSliceSideCm)
					{
						PackFront = FMath::Max(PackFront, FVector::DotProduct(Local, Fwd));
					}
				}
				if (BodyBack > UE_BIG_NUMBER * 0.5 || PackFront < -UE_BIG_NUMBER * 0.5)
				{
					AddError(FString::Printf(TEXT("가슴 위 %+.0fcm 칸에 몸 또는 배낭 꼭짓점이 없다"), Height));
					continue;
				}
				const double SliceGap = (BodyBack - PackFront) * BodyScale;
				GapSum += SliceGap;
				GapMin = FMath::Min(GapMin, SliceGap);
				GapMax = FMath::Max(GapMax, SliceGap);
				++Slices;
				Report += FString::Printf(TEXT(" %+.0f:%.2f"), Height, SliceGap);
			}
			const double GapMean = Slices > 0 ? GapSum / Slices : UE_BIG_NUMBER;
			UE_LOG(LogSnarehaven, Display, TEXT("[RodBackpack] 실제 몸 등–배낭 틈(월드 cm, 가슴 위 높이:틈)%s · 평균 %.2f · 최소 %.2f · 최대 %.2f"), *Report, GapMean, GapMin, GapMax);
			TestTrue(FString::Printf(TEXT("실제 몸 등과 배낭 앞끝의 평균 틈 %.2fcm 이 %.0f~%.0f"), GapMean, -RealGapMeanMinCm, RealGapMeanMaxCm),
				GapMean >= -RealGapMeanMinCm && GapMean <= RealGapMeanMaxCm);
			TestTrue(FString::Printf(TEXT("어느 높이에서도 틈 %.2fcm 이 %.0f 이하 (뜸)"), GapMax, RealGapSliceMaxCm), GapMax <= RealGapSliceMaxCm);
			TestTrue(FString::Printf(TEXT("어느 높이에서도 묻힘 %.2fcm 이 %.0f 이하"), -GapMin, RealGapSliceBuriedCm), GapMin >= -RealGapSliceBuriedCm);
		}
	}
	// ④ 배낭 몸 쪽 면(앞 벡터 방향으로 가장 앞)과 등 표면 사이 틈. 양수 = 뜸, 음수 = 뚫음.
	const double BackSurface = FVector::DotProduct(Chest, Forward) - BackSurfaceCm * BodyScale;
	const double Gap = BackSurface - FarthestAlong(Pack, Forward);
	TestTrue(FString::Printf(TEXT("배낭 몸 쪽 면–등 표면 틈 %.2fcm 이 0~%.0f"), Gap, BackGapMaxCm),
		Gap >= -FloatSlackCm && Gap <= BackGapMaxCm);
	UE_LOG(LogSnarehaven, Display, TEXT("[RodBackpack] 배낭 앞면–등 표면 틈 %.2fcm (등 표면 = 가슴 − 앞 × %.1f × 몸 배율 %.3f)"),
		Gap, BackSurfaceCm, BodyScale);
	UE_LOG(LogSnarehaven, Display,
		TEXT("[RodBackpack] 배낭 = %s · 소켓 %s(뼈 %s) · 긴 변 %.1fcm (기대 %.1f) · 삼각형 %d · 가슴 %s · 배낭 가운데 %s · 앞 내적 %.1f"),
		*Pack->GetStaticMesh()->GetName(), *Pack->GetAttachSocketName().ToString(), *ChestBone.ToString(), PackLong,
		BackpackLongCm * BodyScale, PackTriangles, *Chest.ToCompactString(), *PackCenter.ToCompactString(), Back);

	// ① 낚싯대 4종 × ⑤ 세 자세(서 있기 · 입질 대기 · 감기 첫 프레임).
	const TCHAR* const Rods[][2] = {
		{TEXT("item.rod_basic"), TEXT("SM_Tool_FishingRod_01")},
		{TEXT("item.rod_telescopic"), TEXT("SM_Tool_TelescopicRod_01")},
		{TEXT("item.rod_bait"), TEXT("SM_Tool_BaitRod_01")},
		{TEXT("item.rod_lucky"), TEXT("SM_Tool_LuckyRod_01")},
	};
	for (int32 PoseIndex = 0; PoseIndex < UE_ARRAY_COUNT(Poses); ++PoseIndex)
	{
		const bool bStanding = PoseIndex == 0;
		const bool bReel = PoseIndex == 2;
		const bool bHold = PoseIndex == 3;
		const FString Pose = FPaths::GetBaseFilename(Poses[PoseIndex]);
		if (!bStanding && !TestTrue(FString::Printf(TEXT("%s 첫 프레임 자세"), *Pose), SetPose(Body, Poses[PoseIndex])))
		{
			break;
		}
		Pack->UpdateComponentToWorld();   // 자세가 바뀌면 가슴 뼈도 움직인다 — 배낭 상자를 새 자세로 옮긴다
		for (const auto& Rod : Rods)
		{
			TestTrue(FString::Printf(TEXT("%s 를 들었다"), Rod[0]), Equipment->EquipItemByItemID(FName(Rod[0])));
			UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::Tool);
			if (!TestNotNull(FString::Printf(TEXT("%s Tool 칸 메시 컴포넌트"), Rod[0]), Visual)
				|| !TestNotNull(FString::Printf(TEXT("%s 메시"), Rod[0]), Visual->GetStaticMesh().Get()))
			{
				continue;
			}
			FVector GripLocal;
			FVector TipLocal;
			if (!TestTrue(FString::Printf(TEXT("%s 원본 꼭짓점을 읽었다"), Rod[0]), RodEnds(Visual->GetStaticMesh(), GripLocal, TipLocal)))
			{
				continue;
			}
			const FTransform& RodToWorld = Visual->GetComponentTransform();
			const FVector Grip = RodToWorld.TransformPosition(GripLocal);
			const FVector Tip = RodToWorld.TransformPosition(TipLocal);
			const double LowestZ = LowestVertexZ(Visual);
			// ⑨ 오른손 쥠
			{
				const double RodLen = RodLongCm * BodyScale;
				const FVector HandAt = Body->GetSocketLocation(RodSocket);
				const double GripGap = FVector::Distance(Grip, HandAt);
				const double TipGap = FVector::Distance(Tip, HandAt);
				const double TipFromAxis = FVector::Dist2D(Tip, Origin);
				const double CapsuleRadius = Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
				TestEqual(FString::Printf(TEXT("%s %s 붙은 소켓"), *Pose, Rod[0]), Visual->GetAttachSocketName(), RodSocket);
				TestTrue(FString::Printf(TEXT("%s %s 손잡이 끝–오른손 %.1f ≤ 참길이 × %.1f"), *Pose, Rod[0], GripGap, GripNearRatio), GripGap <= RodLen * GripNearRatio);
				TestTrue(FString::Printf(TEXT("%s %s 끝–오른손 %.1f ≥ 참길이 × %.1f"), *Pose, Rod[0], TipGap, TipFarRatio), TipGap >= RodLen * TipFarRatio);
				TestTrue(FString::Printf(TEXT("%s %s 끝–몸 축 수평 %.1f > 캡슐 반지름 %.1f (몸에 안 묻힘)"), *Pose, Rod[0], TipFromAxis, CapsuleRadius),
					TipFromAxis > CapsuleRadius);
				UE_LOG(LogSnarehaven, Display, TEXT("[RodBackpack] ⑨ 자세 %s · %s · 소켓 %s · 손잡이 끝–손 %.1f · 끝–손 %.1f · 끝–몸 축 수평 %.1f (캡슐 반지름 %.1f)"),
					*Pose, Rod[0], *Visual->GetAttachSocketName().ToString(), GripGap, TipGap, TipFromAxis, CapsuleRadius);
			}
			if (bStanding)
			{
				const double LongSide = FVector::Distance(Tip, Grip);
				const int32 Triangles = Visual->GetStaticMesh()->GetNumTriangles(0);
				TestEqual(FString::Printf(TEXT("%s 메시 이름"), Rod[0]), Visual->GetStaticMesh()->GetName(), FString(Rod[1]));
				TestTrue(FString::Printf(TEXT("%s 참길이 %.1f ≈ %.1f × 몸 배율 %.3f ± %.0f"), Rod[0], LongSide, RodLongCm, BodyScale, ToleranceCm),
					FMath::IsNearlyEqual(LongSide, RodLongCm * BodyScale, ToleranceCm));
				TestTrue(FString::Printf(TEXT("%s 삼각형 %d 이 1~%d"), Rod[0], Triangles, TriangleLimit),
					Triangles > 0 && Triangles <= TriangleLimit);
				// ④·⑤(c) 발밑을 뚫지 않는다 — 루프#390 부터 낚싯대를 들면 상체가 늘 HoldRod(⑧)라 맨 대기 팔로 든 모습은 게임에 없다.
				// 루프#402(INBOX 12:25 팔) 대기 팔을 몸 옆으로 내리자 맨 대기 손의 대가 아래로 누워 이 자리 값은 기록만 하고, 판정은 ⑧ HoldRod 행이 한다.
				UE_LOG(LogSnarehaven, Display,
					TEXT("[RodBackpack] %s = %s · 소켓 %s · 참길이 %.1fcm (기대 %.1f) · 삼각형 %d · 최저 z %.1f · 발 z %.1f"),
					Rod[0], *Visual->GetStaticMesh()->GetName(), *Visual->GetAttachSocketName().ToString(), LongSide,
					RodLongCm * BodyScale, Triangles, LowestZ, FootZ);
			}

			const FVector Axis = (Tip - Grip).GetSafeNormal();
			const double TipForward = FVector::DotProduct(Tip - Origin, Forward);
			const double ElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Axis.Z, -1.0, 1.0)));
			// (d) 배낭 상자는 돌아 있다 — 선분을 배낭 공간으로 옮겨 메시 로컬 상자와 맞댄다(월드 AABB 는 부푼다).
			const FTransform& PackToWorld = Pack->GetComponentTransform();
			const FVector PackGrip = PackToWorld.InverseTransformPosition(Grip);
			const FVector PackTip = PackToWorld.InverseTransformPosition(Tip);
			const bool bHitsPack = FMath::LineBoxIntersection(Pack->GetStaticMesh()->GetBoundingBox(), PackGrip, PackTip, PackTip - PackGrip);

			const double AxisForward = FVector::DotProduct(Axis, Forward);
			if (bReel)
			{
				TestTrue(FString::Printf(TEXT("%s %s 손잡이→끝 수평 위 %.1f° 가 %.0f~%.0f"), *Pose, Rod[0], ElevationDeg,
					ReelElevationMinDeg, ReelElevationMaxDeg),
					ElevationDeg >= ReelElevationMinDeg && ElevationDeg <= ReelElevationMaxDeg);
				TestTrue(FString::Printf(TEXT("%s %s 축 앞 성분 %.2f < 0 (어깨 너머 뒤로)"), *Pose, Rod[0], AxisForward), AxisForward < 0.0);
			}
			else if (bHold)
			{
				TestTrue(FString::Printf(TEXT("%s %s 손잡이→끝 수평 위 %.1f° 가 %.0f~%.0f"), *Pose, Rod[0], ElevationDeg,
					HoldElevationMinDeg, HoldElevationMaxDeg),
					ElevationDeg >= HoldElevationMinDeg && ElevationDeg <= HoldElevationMaxDeg);
				TestTrue(FString::Printf(TEXT("%s %s 축 앞 성분 %.2f < 0 (뒤쪽 위로)"), *Pose, Rod[0], AxisForward), AxisForward < 0.0);
				TestTrue(FString::Printf(TEXT("%s %s 손잡이 끝 높이 %.1f < 0 (허리 아래 — 손이 허리 옆)"), *Pose, Rod[0], Grip.Z - Origin.Z),
					Grip.Z - Origin.Z < 0.0);
				TestTrue(FString::Printf(TEXT("%s %s 가장 낮은 z %.1f > 발 z %.1f"), *Pose, Rod[0], LowestZ, FootZ), LowestZ > FootZ);
			}
			else
			{
				TestTrue(FString::Printf(TEXT("%s %s 끝 앞쪽 %.1f > 0"), *Pose, Rod[0], TipForward), TipForward > 0.0);
			}
			if (!bStanding && !bReel && !bHold)
			{
				const double MinReach = RodLongCm * BodyScale * FishTipReachRatio;
				TestTrue(FString::Printf(TEXT("%s %s 끝 앞쪽 %.1f ≥ 참길이 × %.1f = %.1f"), *Pose, Rod[0], TipForward, FishTipReachRatio, MinReach),
					TipForward >= MinReach);
				TestTrue(FString::Printf(TEXT("%s %s 손잡이→끝 수평 위 %.1f° 가 %.0f~%.0f"), *Pose, Rod[0], ElevationDeg,
					FishElevationMinDeg, FishElevationMaxDeg),
					ElevationDeg >= FishElevationMinDeg && ElevationDeg <= FishElevationMaxDeg);
				const double AxisRight = FVector::DotProduct(Axis, Right);
				TestTrue(FString::Printf(TEXT("%s %s 축 |오른 %.2f| ≤ 앞 %.2f × %.1f"), *Pose, Rod[0], AxisRight, AxisForward, FishSideRatioMax),
					FMath::Abs(AxisRight) <= AxisForward * FishSideRatioMax);
			}
			TestFalse(FString::Printf(TEXT("%s %s 손잡이→끝 선분이 배낭 상자와 만난다"), *Pose, Rod[0]), bHitsPack);
			UE_LOG(LogSnarehaven, Display,
				TEXT("[RodBackpack] 자세 %s · %s · 끝 앞 %.1f 오른 %.1f 위 %.1f · 손잡이 앞 %.1f 오른 %.1f 위 %.1f · 축 수평 위 %.1f° (앞 %.2f 오른 %.2f) · 최저 z %.1f · 발 z %.1f · 배낭 교차 %s"),
				*Pose, Rod[0], TipForward, FVector::DotProduct(Tip - Origin, Right), Tip.Z - Origin.Z,
				FVector::DotProduct(Grip - Origin, Forward), FVector::DotProduct(Grip - Origin, Right), Grip.Z - Origin.Z,
				ElevationDeg, FVector::DotProduct(Axis, Forward), FVector::DotProduct(Axis, Right), LowestZ, FootZ,
				bHitsPack ? TEXT("있음") : TEXT("없음"));
		}
		// 회전을 고른 근거(DESIGN 3.5 낚싯대 행) — 이 자세의 손 소켓 회전과, 앞 45° 위 방향을 손 공간으로 옮긴 벡터.
		const FTransform Hand = Body->GetSocketTransform(RodSocket);
		UE_LOG(LogSnarehaven, Display, TEXT("[RodBackpack] 자세 %s · 손 소켓 회전 %s · 앞 45° 위의 손 공간 벡터 %s"),
			*Pose, *Hand.GetRotation().ToString(),
			*Hand.InverseTransformVectorNoScale((Forward + FVector::UpVector).GetSafeNormal()).ToString());
	}

	// ⑦ 던지기 — 기본 낚싯대 하나로 본다(대 네 종은 같은 손 소켓·같은 반입 회전).
	{
		UAnimSequence* const Cast = LoadObject<UAnimSequence>(nullptr, CastPose);
		const double CastSeconds = Cast ? Cast->GetPlayLength() : 0.0;
		TestTrue(FString::Printf(TEXT("A_Player_FishCast 길이 %.3f초 가 0 초과 %.1f초 미만"), CastSeconds, CastMaxSeconds),
			CastSeconds > 0.0 && CastSeconds < CastMaxSeconds);
		TestTrue(TEXT("item.rod_basic 를 들었다 (던지기)"), Equipment->EquipItemByItemID(FName(TEXT("item.rod_basic"))));
		UStaticMeshComponent* const Visual = Equipment->GetEquippedMesh(EEquipSlot::Tool);
		FVector GripLocal;
		FVector TipLocal;
		if (Cast != nullptr && TestNotNull(TEXT("던지기 Tool 칸 메시"), Visual) && RodEnds(Visual->GetStaticMesh(), GripLocal, TipLocal))
		{
			auto RodAxisAt = [&](const TCHAR* Path, float Seconds, FVector& OutTip)
			{
				SetPose(Body, Path, Seconds);
				Visual->UpdateComponentToWorld();
				const FTransform& RodToWorld = Visual->GetComponentTransform();
				OutTip = RodToWorld.TransformPosition(TipLocal);
				return (OutTip - RodToWorld.TransformPosition(GripLocal)).GetSafeNormal();
			};
			FVector StrikeTip;
			FVector EndTip;
			FVector WaitTip;
			const FVector StrikeAxis = RodAxisAt(CastPose, CastStrikeSeconds, StrikeTip);
			const FVector EndAxis = RodAxisAt(CastPose, CastSeconds, EndTip);
			const FVector WaitAxis = RodAxisAt(Poses[1], 0.f, WaitTip);
			const double StrikeForward = FVector::DotProduct(StrikeTip - Origin, Forward);
			const double StrikeElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(StrikeAxis.Z, -1.0, 1.0)));
			const double JoinDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(EndAxis, WaitAxis), -1.0, 1.0)));
			TestTrue(FString::Printf(TEXT("FishCast %.2f초 대 끝 앞쪽 %.1f ≥ 참길이 × %.1f"), CastStrikeSeconds, StrikeForward, FishTipReachRatio),
				StrikeForward >= RodLongCm * BodyScale * FishTipReachRatio);
			TestTrue(FString::Printf(TEXT("FishCast %.2f초 대 축 수평 위 %.1f° < 0 (앞 아래로)"), CastStrikeSeconds, StrikeElevationDeg),
				StrikeElevationDeg < 0.0);
			TestTrue(FString::Printf(TEXT("FishCast 끝 장 ↔ FishIdle 첫 장 대 축 차 %.2f° ≤ %.0f°"), JoinDeg, CastJoinMaxDeg), JoinDeg <= CastJoinMaxDeg);
			UE_LOG(LogSnarehaven, Display,
				TEXT("[RodBackpack] 던지기 A_Player_FishCast %.3f초 · %.2f초 대 끝 앞 %.1f 위 %.1f · 축 수평 위 %.1f° (앞 %.2f) · 끝 장 ↔ FishIdle 축 차 %.2f°"),
				CastSeconds, CastStrikeSeconds, StrikeForward, StrikeTip.Z - Origin.Z, StrikeElevationDeg, FVector::DotProduct(StrikeAxis, Forward), JoinDeg);
		}
	}

	// ② 같은 Tool 칸 컴포넌트로 손도끼 — 몸 배율이 남으면 80 이 아니라 약 141 이 된다.
	TestTrue(TEXT("item.hatchet 를 들었다"), Equipment->EquipItemByItemID(FName(TEXT("item.hatchet"))));
	if (UStaticMeshComponent* const Axe = Equipment->GetEquippedMesh(EEquipSlot::Tool))
	{
		const double AxeLong = WorldLongSide(Axe);
		TestTrue(FString::Printf(TEXT("낚싯대 다음 손도끼 긴 변 %.1f ≈ %.1f ± %.0f"), AxeLong, HandAxeLongCm, ToleranceCm),
			FMath::IsNearlyEqual(AxeLong, HandAxeLongCm, ToleranceCm));
	}

	DestroyWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
