// INBOX 결함 12:40 ④ 2층 검사 — 게임 카메라(55° 내려다봄)에서 기본 머리카락 덩어리가 두 눈을 가리지 않는다.
// 실제 캐릭터(몸 + `Socket_Head` 에 붙은 `SM_Player_Hair_01`, 머리 젖힘 포함)를 대기·걷기·달리기 한 바퀴 8칸씩 돌리고,
// 몸이 카메라를 마주 볼 때 카메라 쪽 방향(앞 cos55·위 sin55)으로 두 눈 자리에서 선을 쏴 머리카락 삼각형에 걸리는지 잰다.
// 눈 자리 = 몸 메시 참조 자세 앞면 위의 점(정수리 아래·가운데에서 좌우) → 머리 뼈와 함께 돌려 지금 자세로 옮긴다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Player.Body.HairLeavesEyesVisible; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "MeshDescription.h"
#include "Player/PlayerBodyAnimInstance.h"

namespace SnarehavenHairFaceTest
{
	const TCHAR* const ClipPaths[] = {
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Idle.A_Player_Idle"),
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Walk.A_Player_Walk"),
		TEXT("/Game/Game/Characters/Player/Snare/A_Player_Run.A_Player_Run"),
	};

	// 검사 전용 값 (Verification.md 6).
	constexpr int32 SamplesPerClip = 8;
	constexpr float SettleSeconds = 1.f;
	constexpr float CameraPitchDegrees = 55.f;     // INBOX 12:40 ① 카메라 각 그대로
	constexpr float RayLengthCm = 300.f;            // 머리 크기(≈50cm)보다 넉넉히 — 머리카락만 본다
	constexpr float RayStartOffsetCm = 0.5f;        // 눈 점이 피부 위라 피부 바로 밖에서 쏜다
	// 눈 자리(메시 공간 cm) — `tools/unreal/make_player_face_texture.py` 가 그린 새 눈 가운데(앞면 좌표 u ±0.36·v 0.02)를
	// 몸 원본으로 옮긴 값: 정수리 아래 23.26cm, 가로 ±9.64cm = 머리 가로(53.96)의 ±0.179. 그림을 옮기면 같이 옮긴다.
	constexpr float EyeBelowCrownCm = 23.3f;
	constexpr float EyeSideOfWidth = 0.179f;
	constexpr float HeadBandTopCm = 10.f;           // 머리 가로를 재는 띠(정수리 아래) — `BoxHeadFlatFront` 와 같은 띠
	constexpr float HeadBandBottomCm = 30.f;
	constexpr float HeadBandMaxAbsX = 35.f;
	constexpr float SurfacePickCm = 2.5f;           // 눈 점 둘레에서 앞면 정점을 고르는 반경
	constexpr float HairMidlineHalfWidthCm = 8.f;   // 머리카락 앞뒤 끝을 재는 가운데 줄 폭의 절반

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

	/** 참조 자세 몸 메시에서 두 눈 점(메시 공간). 앞면 위 — 눈 높이·좌우 둘레 정점 중 가장 앞(+Y). */
	bool FindEyePoints(const FMeshDescription& Body, FVector OutEyes[2])
	{
		const TVertexAttributesConstRef<FVector3f> Positions = Body.GetVertexPositions();
		float Crown = -BIG_NUMBER;
		for (const FVertexID Vertex : Body.Vertices().GetElementIDs())
		{
			Crown = FMath::Max(Crown, Positions[Vertex].Z);
		}
		FBox3f Band(ForceInit);
		for (const FVertexID Vertex : Body.Vertices().GetElementIDs())
		{
			const FVector3f P = Positions[Vertex];
			if (P.Z <= Crown - HeadBandTopCm && P.Z >= Crown - HeadBandBottomCm && FMath::Abs(P.X) <= HeadBandMaxAbsX)
			{
				Band += P;
			}
		}
		if (!Band.IsValid)
		{
			return false;
		}
		const float EyeZ = Crown - EyeBelowCrownCm;
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float EyeX = Band.GetCenter().X + (Side == 0 ? -1.f : 1.f) * EyeSideOfWidth * Band.GetSize().X;
			float FrontY = -BIG_NUMBER;
			for (const FVertexID Vertex : Body.Vertices().GetElementIDs())
			{
				const FVector3f P = Positions[Vertex];
				if (FMath::Abs(P.X - EyeX) <= SurfacePickCm && FMath::Abs(P.Z - EyeZ) <= SurfacePickCm)
				{
					FrontY = FMath::Max(FrontY, P.Y);
				}
			}
			if (FrontY <= Band.GetCenter().Y)
			{
				return false;
			}
			OutEyes[Side] = FVector(EyeX, FrontY, EyeZ);
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerHairFaceTest, "Snarehaven.Player.Body.HairLeavesEyesVisible",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerHairFaceTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenHairFaceTest;

	UWorld* World = CreateWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	if (Player == nullptr || !Player->ApplyPlayerBody())
	{
		AddError(TEXT("플레이어 몸을 못 올렸다"));
		DestroyWorld(World);
		return false;
	}
	Player->GetEquipment()->RefreshEquippedVisuals();   // `BeginPlay` 의 몸 올린 직후 길 — 모자 칸이 비어 머리카락이 붙는다
	USkeletalMeshComponent* const Body = Player->GetMesh();
	UStaticMeshComponent* const Hair = Player->GetEquipment()->GetHairMesh();
	UPlayerBodyAnimInstance* const Anim = Player->GetBodyAnimInstance();
	const FMeshDescription* const BodyDescription = Body->GetSkeletalMeshAsset()->GetMeshDescription(0);
	const FMeshDescription* const HairDescription = Hair && Hair->GetStaticMesh() ? Hair->GetStaticMesh()->GetMeshDescription(0) : nullptr;
	if (!TestNotNull(TEXT("머리카락 컴포넌트"), Hair) || !TestNotNull(TEXT("섞기 애님 인스턴스"), Anim)
		|| !TestNotNull(TEXT("몸 메시 설명"), BodyDescription) || !TestNotNull(TEXT("머리카락 메시 설명"), HairDescription))
	{
		DestroyWorld(World);
		return false;
	}
	FVector RefEyes[2];
	if (!TestTrue(TEXT("몸 메시 앞면에서 두 눈 자리를 찾았다"), FindEyePoints(*BodyDescription, RefEyes)))
	{
		DestroyWorld(World);
		return false;
	}
	AddInfo(FString::Printf(TEXT("[HairFace] 참조 자세 눈 %s · %s"), *RefEyes[0].ToCompactString(), *RefEyes[1].ToCompactString()));

	const FReferenceSkeleton& RefSkeleton = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
	const int32 HeadIndex = Body->GetBoneIndex(TEXT("Head"));
	if (!TestTrue(TEXT("머리 뼈"), HeadIndex != INDEX_NONE))
	{
		DestroyWorld(World);
		return false;
	}
	const FTransform RefHead = FAnimationRuntime::GetComponentSpaceTransformRefPose(RefSkeleton, HeadIndex);

	// 머리카락 삼각형(메시 공간) — 칸마다 지금 컴포넌트 변환으로 옮긴다.
	TArray<FVector> HairCorners;
	{
		const TVertexAttributesConstRef<FVector3f> Positions = HairDescription->GetVertexPositions();
		for (const FTriangleID Triangle : HairDescription->Triangles().GetElementIDs())
		{
			for (const FVertexID Vertex : HairDescription->GetTriangleVertices(Triangle))
			{
				HairCorners.Add(FVector(Positions[Vertex]));
			}
		}
	}

	// 반입된 머리카락의 앞(+Y)·뒤(-Y) 가운데 줄 끝 높이 — 앞은 이마 위, 뒤는 뒤통수 아래(`make_player_hair_mesh.py`).
	// OBJ 반입이 Y 를 뒤집어 #396 까지 거꾸로 씌워졌다 — 앞 끝이 뒤 끝보다 높아야 한다.
	{
		float FrontLowest = BIG_NUMBER;
		float BackLowest = BIG_NUMBER;
		for (const FVector& Corner : HairCorners)
		{
			if (FMath::Abs(Corner.X) < HairMidlineHalfWidthCm)
			{
				float& Lowest = Corner.Y > 0.0 ? FrontLowest : BackLowest;
				Lowest = FMath::Min(Lowest, static_cast<float>(Corner.Z));
			}
		}
		AddInfo(FString::Printf(TEXT("[HairFace] 머리카락 가운데 줄 끝 높이(정수리 기준) 앞 %.2f · 뒤 %.2f"), FrontLowest, BackLowest));
		TestTrue(TEXT("머리카락 앞 끝이 뒤 끝보다 높다(앞뒤 거꾸로 아님)"), FrontLowest > BackLowest);
	}

	// 몸이 카메라를 마주 볼 때 카메라 쪽 방향(컴포넌트 공간, 몸 앞 = +Y — `HeadLiftShowsFace` 가 발끝으로 확인).
	const float Pitch = FMath::DegreesToRadians(CameraPitchDegrees);
	const FVector ToCamera(0.f, FMath::Cos(Pitch), FMath::Sin(Pitch));

	int32 Blocked = 0;
	int32 Rays = 0;
	for (const TCHAR* const Path : ClipPaths)
	{
		UAnimSequence* const Clip = LoadObject<UAnimSequence>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("클립 %s"), Path), Clip))
		{
			continue;
		}
		Anim->ClearActionClip();
		Anim->SetBaseClip(Clip, true, 1.f, true);
		Body->TickAnimation(SettleSeconds, false);
		const float Length = Clip->GetPlayLength();
		for (int32 Sample = 0; Sample < SamplesPerClip; ++Sample)
		{
			const float Time = Length * Sample / SamplesPerClip;
			Anim->SetBaseClip(Clip, true, 1.f, true);
			Body->TickAnimation(Time, false);
			Body->RefreshBoneTransforms();
			Hair->UpdateComponentToWorld();
			const FTransform HeadNow = Body->GetComponentSpaceTransforms()[HeadIndex];
			const FTransform HairToBody = Hair->GetComponentTransform().GetRelativeTransform(Body->GetComponentTransform());

			TArray<FVector> Corners;
			Corners.Reserve(HairCorners.Num());
			for (const FVector& Corner : HairCorners)
			{
				Corners.Add(HairToBody.TransformPosition(Corner));
			}
			if (Rays == 0)
			{
				// 머리카락 앞(+Y 메시) 끝이 몸 어느 쪽을 보는지 — 소켓 회전이 틀리면 뒤쪽 긴 술이 얼굴을 덮는다.
				AddInfo(FString::Printf(TEXT("[HairFace] 머리카락 → 몸 회전 %s · 위치 %s · 머리카락 앞(+Y) → 몸 %s · 위(+Z) → 몸 %s"),
					*HairToBody.Rotator().ToCompactString(), *HairToBody.GetLocation().ToCompactString(),
					*HairToBody.TransformVectorNoScale(FVector::YAxisVector).ToCompactString(),
					*HairToBody.TransformVectorNoScale(FVector::ZAxisVector).ToCompactString()));
			}
			FString Line;
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FVector Eye = HeadNow.TransformPosition(RefHead.InverseTransformPosition(RefEyes[Side]));
				const FVector Start = Eye + ToCamera * RayStartOffsetCm;
				const FVector End = Eye + ToCamera * RayLengthCm;
				bool bHit = false;
				for (int32 Corner = 0; Corner + 2 < Corners.Num() && !bHit; Corner += 3)
				{
					FVector Point;
					FVector Normal;
					bHit = FMath::SegmentTriangleIntersection(Start, End, Corners[Corner], Corners[Corner + 1], Corners[Corner + 2], Point, Normal);
					if (bHit && Rays == 0)
					{
						AddInfo(FString::Printf(TEXT("[HairFace] 첫 가림 — 눈 %s · 맞은 점(참조 자세 메시 공간) %s · 머리카락 정점 %s"),
							*RefHead.TransformPosition(HeadNow.InverseTransformPosition(Eye)).ToCompactString(),
							*RefHead.TransformPosition(HeadNow.InverseTransformPosition(Point)).ToCompactString(),
							*HairCorners[Corner].ToCompactString()));
					}
				}
				++Rays;
				Blocked += bHit ? 1 : 0;
				Line += FString::Printf(TEXT(" · 눈 %d %s"), Side + 1, bHit ? TEXT("가림") : TEXT("보임"));
				TestFalse(FString::Printf(TEXT("%s 칸 %d 눈 %d 이 머리카락에 안 가린다"), *Clip->GetName(), Sample + 1, Side + 1), bHit);
			}
			AddInfo(FString::Printf(TEXT("[HairFace] %s 칸 %d (%.3f초)%s"), *Clip->GetName(), Sample + 1, Time, *Line));
		}
	}
	AddInfo(FString::Printf(TEXT("[HairFace] 선 %d 중 가림 %d"), Rays, Blocked));
	TestTrue(TEXT("선을 쐈다"), Rays > 0);

	DestroyWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
