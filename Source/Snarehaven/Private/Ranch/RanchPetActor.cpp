#include "Ranch/RanchPetActor.h"

#include "Animation/AnimSequence.h"
#include "AnimationRuntime.h"
#include "Animals/AnimalCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#if WITH_EDITORONLY_DATA
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#endif

namespace
{
	// 묶인 가축이 느릿느릿 — 사냥터 배회(`AAnimalCharacter::UpdateWander`)보다 짧게 서고 짧게 걷는다.
	constexpr float PetWalkSpeedCmPerSec = 60.f;
	constexpr float PetStandMinSeconds = 2.f;
	constexpr float PetStandMaxSeconds = 6.f;
	constexpr float PetTurnDegPerSec = 240.f;
	constexpr float PetGroundTraceCm = 300.f;
	// 머리 뼈는 머리 한가운데라 모자·리본이 머리에 박힌다 — 펫 키의 이만큼 위로 올린다(목 뼈는 그대로).
	constexpr float PetCosmeticHeadLiftRatio = 0.08f;
	// 목걸이 바깥 폭 = 목 굵기 × 이만큼. 고리 메시 안쪽 지름이 바깥의 약 0.75배(관 굵기 r 0.017 / 반지름 R 0.12,
	// `ranch_cosmetic_make.py`)라 1.4배면 안쪽이 목 단면보다 약 5% 넓다 — 목에 붙어 걸린다(M40-02 감독 보정 「목 × 1.1 이하」 =
	// 관 가운데 지름 ≈ 1.2배).
	// 행 `PetCollarWidthPerNeck` 이 있으면 그 값을 쓴다.
	constexpr float PetCollarWidthPerNeck = 1.4f;
	// 목 굵기를 잴 때 목 뼈 자리에서 목 축 앞뒤로 볼 판 두께 — 목 → 머리 거리의 이만큼. 몸 키의 5% 로 얇게 잘랐더니
	// 정점이 성긴 돼지·소·양·곰은 판 안에 정점이 하나도 없어 0 이 나왔다(루프#466 실측).
	constexpr float PetNeckSlabRatio = 0.5f;
	// 고리 자리 둘레로 단면을 잴 띠 반폭 — 목 → 머리 거리의 이만큼. 판 전체로 재면 머리 쪽으로 부푼 목 끝(염소 뒷덜미)까지
	// 들어간다. 고리는 25° 기울어 이만큼 위아래를 지나간다.
	constexpr float PetNeckBandRatio = 0.2f;
	// 멱까지 넣어 잰 몸(돼지)의 목걸이 기본 바깥 폭 = 목 앞뒤 높이 × (1 + 이만큼) — 10-17 감독 보정 「반폭 + 여유 5%」.
	constexpr float PetNeckBodyDepthSlackRatio = 0.05f;

	// 새 리그는 목이 마디로 갈린다(`neck_01`·`neck_02`) — 이름 그대로가 없으면 첫 마디(목 밑동)를 쓴다.
	FName ResolveBone(const FReferenceSkeleton& Skeleton, FName Bone)
	{
		if (Bone.IsNone() || Skeleton.FindBoneIndex(Bone) != INDEX_NONE)
		{
			return Bone;
		}
		const FName FirstSegment(*(Bone.ToString() + TEXT("_01")));
		return Skeleton.FindBoneIndex(FirstSegment) != INDEX_NONE ? FirstSegment : Bone;
	}

	// 고리 위 한 점. `FromYaw` 가 있으면 거기서 20~80° 옆으로만 — 반대편으로 곧장 가면 말뚝을 뚫고 지나간다
	// (현 길이의 최단 거리 ≥ 안쪽 반경 × cos 40° ≈ 0.77배). `FrontYaw` 가 있으면 그 부채꼴 안으로 자른다.
	FVector RingPoint(const FVector& Home, TOptional<float> FromYaw, TOptional<float> FrontYaw)
	{
		const float Side = FMath::RandBool() ? 1.f : -1.f;
		float Yaw = FromYaw.IsSet() ? *FromYaw + Side * FMath::FRandRange(20.f, 80.f) : FMath::FRandRange(0.f, 360.f);
		if (FrontYaw.IsSet())
		{
			const float Arc = ARanchPetActor::FrontArcDeg;
			Yaw = *FrontYaw + FMath::Clamp(FMath::FindDeltaAngleDegrees(*FrontYaw, Yaw), -Arc, Arc);
		}
		const FVector Dir = FRotator(0.f, Yaw, 0.f).Vector();
		return Home + Dir * FMath::FRandRange(ARanchPetActor::TetherMinCm, ARanchPetActor::TetherRadiusCm);
	}
}

ARanchPetActor::ARanchPetActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	RootComponent = Body;
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);

	CosmeticMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CosmeticMesh"));
	CosmeticMesh->SetupAttachment(Body);
	CosmeticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CosmeticMesh->SetGenerateOverlapEvents(false);
	CosmeticMesh->SetVisibility(false);
}

void ARanchPetActor::SetCosmetic(const FItemRow* Row)
{
	const FName NewID = Row != nullptr ? Row->ItemID : NAME_None;
	if (NewID == CosmeticItemID)
	{
		return;
	}
	CosmeticItemID = NewID;
	UStaticMesh* const Mesh = Row != nullptr ? Row->EquipMeshRef.LoadSynchronous() : nullptr;
	USkeletalMesh* const BodyMesh = Body->GetSkeletalMeshAsset();
	if (Mesh == nullptr || BodyMesh == nullptr)
	{
		CosmeticMesh->SetStaticMesh(nullptr);
		CosmeticMesh->SetVisibility(false);
		return;
	}
	CosmeticMesh->SetStaticMesh(Mesh);

	// 몸 메시 단위(액터 배율 전)로 잰다 — 붙는 칸은 몸 아래라 액터 배율을 같이 받는다.
	const FBoxSphereBounds BodyBounds = BodyMesh->GetBounds();
	const float PetHeight = BodyBounds.BoxExtent.Z * 2.f;
	const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
	const float MeshWidth = 2.f * FMath::Max(MeshBounds.BoxExtent.X, MeshBounds.BoxExtent.Y);

	// 뼈가 있으면 뼈를 따라 움직이게 붙이되, 놓는 자리·방향은 몸 기준(위가 위)으로 한 번 계산한다 —
	// 뼈 축은 리그마다 제각각이라 뼈 기준으로 두면 모자가 옆으로 눕는다.
	const FName Bone = ResolveBone(BodyMesh->GetRefSkeleton(), Row->EquipSocketName);
	const bool bBone = !Bone.IsNone() && Body->DoesSocketExist(Bone);
	const bool bHead = Row->EquipSocketName == FName(TEXT("head"));
	// 목걸이는 잰 목 굵기로 벌린다 — 키 비율로는 목이 굵은 돼지에서 고리가 목 속에 묻힌다(스킨 조각 n 3층 첫 캡처).
	// 행이 배수를 주면 그 값(황금 갈기처럼 바깥으로 부푼 메시), 없으면 목걸이 기본.
	const float CollarPerNeck = Row->PetCollarWidthPerNeck > 0.f ? Row->PetCollarWidthPerNeck : PetCollarWidthPerNeck;
	const float TargetWidth = bBone && !bHead && NeckWidthCm > 0.f ? NeckWidthCm * CollarPerNeck
		: PetHeight * Row->PetCosmeticWidthRatio;
	const float Scale = MeshWidth > KINDA_SMALL_NUMBER ? TargetWidth / MeshWidth : 1.f;
	FVector Spot = bBone ? Body->GetSocketTransform(Bone, RTS_Component).GetLocation()
		: FVector(BodyBounds.Origin.X, BodyBounds.Origin.Y, BodyBounds.Origin.Z + BodyBounds.BoxExtent.Z);
	// 목걸이는 뼈가 아니라 잰 목 단면 가운데에 — 뼈는 목 뒤쪽·가슴 속에 있다(행 `PetNeckCenterCm`, 기준 자세).
	const bool bCollar = bBone && !bHead && NeckWidthCm > 0.f;
	if (bCollar)
	{
		Spot += NeckCenterCm;
	}
	if (bBone && bHead)
	{
		Spot.Z += PetHeight * PetCosmeticHeadLiftRatio;
	}
	// 메시 밑면이 자리에 앉게 — 모자 원점이 밑면이 아닐 수 있다. 머리 말고 다른 뼈(목)는 앉히지 않고 원점을
	// 뼈에 둔다 — 목걸이는 원점이 고리 가운데라 밑면을 앉히면 고리가 목 위로 떠오른다(스킨 조각 n).
	if (!bBone || bHead)
	{
		Spot.Z -= (MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z) * Scale;
	}
	// 목 뼈면 고리를 목에 직각으로 세운다 — 메시 위(+Z) = 목 → 머리 방향, 메시 앞(+Y, 방울)은 몸 앞(+Y)에 가깝게.
	// 돼지처럼 목이 앞으로 누운 몸에 고리를 눕혀 두면 목 속에 묻혀 안 보인다(스킨 조각 n 3층 첫 캡처).
	FQuat Facing = FQuat::Identity;
	const FName HeadBone(TEXT("head"));
	if (bBone && !bHead && Body->DoesSocketExist(HeadBone))
	{
		const FVector NeckToHead = Body->GetSocketTransform(HeadBone, RTS_Component).GetLocation()
			- Body->GetSocketTransform(Bone, RTS_Component).GetLocation();
		if (!NeckToHead.IsNearlyZero())
		{
			Facing = FRotationMatrix::MakeFromZY(NeckToHead, FVector::YAxisVector).ToQuat();
		}
	}
	const FTransform Wanted(Facing, Spot, FVector(Scale));
	if (bBone)
	{
		const FTransform BoneCS = Body->GetSocketTransform(Bone, RTS_Component);
		CosmeticMesh->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform, Bone);
		CosmeticMesh->SetRelativeTransform(Wanted.GetRelativeTransform(BoneCS));
	}
	else
	{
		CosmeticMesh->AttachToComponent(Body, FAttachmentTransformRules::KeepRelativeTransform);
		CosmeticMesh->SetRelativeTransform(Wanted);
	}
	CosmeticMesh->SetVisibility(true);
}

float ARanchPetActor::MeasureNeckWidthCm(USkeletalMesh* Mesh)
{
	return MeasureNeck(Mesh).W;
}

float ARanchPetActor::MeasureNeckDepthCm(USkeletalMesh* Mesh)
{
	float Depth = 0.f;
	MeasureNeck(Mesh, &Depth);
	return Depth;
}

FVector ARanchPetActor::MeasureNeckCenterCm(USkeletalMesh* Mesh)
{
	const FVector4 Neck = MeasureNeck(Mesh);
	return FVector(Neck.X, Neck.Y, Neck.Z);
}

float ARanchPetActor::MeasureHeadRearAlongCm(USkeletalMesh* Mesh)
{
#if WITH_EDITORONLY_DATA
	const FSkeletalMeshModel* const Model = Mesh != nullptr ? Mesh->GetImportedModel() : nullptr;
	if (Model == nullptr || Model->LODModels.IsEmpty())
	{
		return 0.f;
	}
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	const int32 Neck = Skeleton.FindBoneIndex(ResolveBone(Skeleton, TEXT("neck")));
	const int32 Head = Skeleton.FindBoneIndex(TEXT("head"));
	if (Neck == INDEX_NONE || Head == INDEX_NONE)
	{
		return 0.f;
	}
	const FVector NeckAt = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Neck).GetLocation();
	const FVector Axis = (FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Head).GetLocation() - NeckAt).GetSafeNormal();
	float Rear = TNumericLimits<float>::Max();
	for (const FSkelMeshSection& Section : Model->LODModels[0].Sections)
	{
		for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
		{
			int32 Strongest = 0;
			for (int32 Influence = 1; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
			{
				if (Vertex.InfluenceWeights[Influence] > Vertex.InfluenceWeights[Strongest])
				{
					Strongest = Influence;
				}
			}
			const int32 BoneIndex = Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[Strongest])
				? Section.BoneMap[Vertex.InfluenceBones[Strongest]] : INDEX_NONE;
			if (BoneIndex != INDEX_NONE && (BoneIndex == Head || Skeleton.BoneIsChildOf(BoneIndex, Head)))
			{
				Rear = FMath::Min(Rear, static_cast<float>(FVector::DotProduct(FVector(Vertex.Position) - NeckAt, Axis)));
			}
		}
	}
	return Rear == TNumericLimits<float>::Max() ? 0.f : Rear;
#else
	return 0.f;
#endif
}

FVector4 ARanchPetActor::MeasureNeck(USkeletalMesh* Mesh, float* OutDepthCm)
{
#if WITH_EDITORONLY_DATA
	const FSkeletalMeshModel* const Model = Mesh != nullptr ? Mesh->GetImportedModel() : nullptr;
	if (Model == nullptr || Model->LODModels.IsEmpty())
	{
		return FVector4(0.f, 0.f, 0.f, 0.f);
	}
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	const int32 Neck = Skeleton.FindBoneIndex(ResolveBone(Skeleton, TEXT("neck")));
	const int32 Head = Skeleton.FindBoneIndex(TEXT("head"));
	if (Neck == INDEX_NONE || Head == INDEX_NONE)
	{
		return FVector4(0.f, 0.f, 0.f, 0.f);
	}
	const FVector NeckAt = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Neck).GetLocation();
	const FVector NeckToHead = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, Head).GetLocation() - NeckAt;
	const FVector Axis = NeckToHead.GetSafeNormal();
	const float Slab = NeckToHead.Size() * PetNeckSlabRatio;
	// 단면을 잴 두 축 — 옆(몸 좌우)과 그에 직각(목 앞뒤). 뼈는 목 뒤쪽에 치우쳐 있어 「뼈 축에서 가장 먼 정점 × 2」는
	// 목 앞뒤 두께의 거의 두 배가 된다 — 염소 고리가 얼굴부터 가슴 밑까지 둘렀다(M40-02 3층 감독 보정).
	FVector Side = FVector::CrossProduct(Axis, FVector::UpVector).GetSafeNormal();
	if (Side.IsNearlyZero())
	{
		Side = FVector::XAxisVector;
	}
	const FVector Depth = FVector::CrossProduct(Side, Axis);

	// 목 마디 뼈가 가장 세게 잡은 정점만 — 판이 어깨·다리를 같이 자르면 목보다 훨씬 넓게 잰다. 머리 뼈 쪽(턱·귀·뿔)은
	// 목 뼈의 자식이어도 뺀다. 판 안에 목 마디 정점이 없는 몸(Quaternius 돼지 — 목 정점이 판 밖, 머리 바로 뒤에만 있다)은
	// 판 밖 목 정점의 몸 쪽 끝(목이 몸에서 나오는 곳)으로 잰다. 머리 뒤쪽 끝에서 안으로 들어간 단면으로 쟀더니 고리가
	// 얼굴 둘레에 걸렸다(M40-04 3층 감독 보정). 판 안 정점 전부면 앞다리까지 들어가 고리가 몸통보다 커졌다(M40-02 3층 캡처).
	TArray<FVector> NeckPoints;
	TArray<FVector> NeckFarPoints;
	TArray<FVector> HeadPoints;
	TArray<FVector> BodyPoints;   // 머리 아닌 정점 전부 — 목 마디 정점이 목 위쪽만 덮는 몸의 목 아래(멱) 끝을 찾는다
	for (const FSkelMeshSection& Section : Model->LODModels[0].Sections)
	{
		for (const FSoftSkinVertex& Vertex : Section.SoftVertices)
		{
			int32 Strongest = 0;
			for (int32 Influence = 1; Influence < MAX_TOTAL_INFLUENCES; ++Influence)
			{
				if (Vertex.InfluenceWeights[Influence] > Vertex.InfluenceWeights[Strongest])
				{
					Strongest = Influence;
				}
			}
			const FVector Offset = FVector(Vertex.Position) - NeckAt;
			const FVector Local(FVector::DotProduct(Offset, Side), FVector::DotProduct(Offset, Depth), FVector::DotProduct(Offset, Axis));
			const int32 BoneIndex = Section.BoneMap.IsValidIndex(Vertex.InfluenceBones[Strongest])
				? Section.BoneMap[Vertex.InfluenceBones[Strongest]] : INDEX_NONE;
			if (BoneIndex == INDEX_NONE)
			{
				continue;
			}
			if (BoneIndex == Head || Skeleton.BoneIsChildOf(BoneIndex, Head))
			{
				HeadPoints.Add(Local);
				continue;
			}
			BodyPoints.Add(Local);
			if (BoneIndex == Neck || Skeleton.BoneIsChildOf(BoneIndex, Neck))
			{
				(FMath::Abs(Local.Z) <= Slab ? NeckPoints : NeckFarPoints).Add(Local);
			}
		}
	}
	const TArray<FVector>& Points = !NeckPoints.IsEmpty() ? NeckPoints : !NeckFarPoints.IsEmpty() ? NeckFarPoints : HeadPoints;
	if (Points.IsEmpty())
	{
		return FVector4(0.f, 0.f, 0.f, 0.f);
	}
	// 고리 자리 = 목 정점들의 축 방향 한가운데. 소·양·여우는 목 뼈가 가슴 속(목 정점이 뼈보다 한참 위)이라 뼈 자리에
	// 두면 고리가 가슴을 두른다. 그 둘레 띠(목 → 머리 거리의 PetNeckBandRatio)만으로 단면 상자를 잰다.
	// 판 밖 목 정점으로 재는 몸은 그 몸 쪽 끝, 머리 정점으로 재는 몸은 머리 뒤쪽 끝에서 띠 반폭만큼 들어간 곳.
	float MinAlong = TNumericLimits<float>::Max();
	float MaxAlong = TNumericLimits<float>::Lowest();
	for (const FVector& Point : Points)
	{
		MinAlong = FMath::Min(MinAlong, Point.Z);
		MaxAlong = FMath::Max(MaxAlong, Point.Z);
	}
	const float Band = NeckToHead.Size() * PetNeckBandRatio;
	const float RingAlong = !NeckPoints.IsEmpty() ? 0.5f * (MinAlong + MaxAlong) : !NeckFarPoints.IsEmpty() ? MinAlong : MinAlong + Band;
	FBox2D Section(ForceInit);
	for (const FVector& Point : Points)
	{
		if (FMath::Abs(Point.Z - RingAlong) <= Band)
		{
			Section += FVector2D(Point.X, Point.Y);
		}
	}
	// 판 밖 목 정점으로 재는 몸(Quaternius 돼지·소)은 목 마디 정점이 목 위쪽 껍질뿐이고 멱·가슴 앞은 앞다리 뼈가 잡는다 —
	// 목 정점만으로 재면 단면 가운데가 목 위로 떠 고리가 등 위에 걸렸다(10-17 3층, 10-07 리본 목줄도 같은 뿌리). 그 몸만
	// 같은 띠·같은 옆 폭 안의 몸 정점까지 넣어 멱 끝을 잡는다. 옆 폭은 목 정점 그대로라 어깨·다리 옆면은 안 들어간다.
	// 판 안 목 정점으로 재는 몸(염소·공작 등)은 그대로 — 넣으면 어깨까지 들어가 고리가 커졌다.
	const bool bBodyDepth = NeckPoints.IsEmpty() && !NeckFarPoints.IsEmpty() && Section.bIsValid;
	if (bBodyDepth)
	{
		const FBox2D NeckOnly = Section;
		for (const FVector& Point : BodyPoints)
		{
			if (FMath::Abs(Point.Z - RingAlong) <= Band && Point.X >= NeckOnly.Min.X && Point.X <= NeckOnly.Max.X)
			{
				Section += FVector2D(Point.X, Point.Y);
			}
		}
	}
	const FVector2D Size = Section.GetSize();
	const FVector2D Mid = Section.GetCenter();
	const FVector Center = Side * Mid.X + Depth * Mid.Y + Axis * RingAlong;
	if (OutDepthCm != nullptr)
	{
		*OutDepthCm = Size.Y;
	}
	// 멱까지 넣어 잰 몸은 목 앞뒤 높이(목 위쪽 정점 ↔ 멱)로 고리를 벌린다 — 목걸이 기본 바깥 폭이 그 높이 × (1 + 여유)가
	// 되게. 둘레 상자 그대로(264.9cm) 쓰면 바깥이 목 × 1.4 라 고리가 훌라후프처럼 등선 위로 솟았다(10-17 3층 감독 반려).
	if (bBodyDepth)
	{
		return FVector4(Center, Size.Y * (1.f + PetNeckBodyDepthSlackRatio) / PetCollarWidthPerNeck);
	}
	return FVector4(Center, FMath::Max(Size.X, Size.Y));
#else
	return FVector4(0.f, 0.f, 0.f, 0.f);
#endif
}

bool ARanchPetActor::ApplyRow(const FAnimalRow& Row)
{
	USkeletalMesh* const Mesh = Row.MeshRef.LoadSynchronous();
	if (Mesh == nullptr)
	{
		return false;
	}
	Body->SetSkeletalMesh(Mesh);

	// 크기·발 맞춤은 `AAnimalCharacter::ApplyBodyFromRow` 와 같은 규칙 — 행 `MeshFitHeightCm` 이 있으면 그 높이로.
	// 루트가 곧 몸이라 발 맞춤은 Tick 의 바닥 높이에 메시 밑면만큼 더해 준다.
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const float MeshHeight = Bounds.BoxExtent.Z * 2.f;
	const float Scale = Row.MeshFitHeightCm > 0.f && MeshHeight > KINDA_SMALL_NUMBER ? Row.MeshFitHeightCm / MeshHeight : 1.f;
	SetActorScale3D(FVector(Scale));
	FootOffsetZ = -(Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale;

	NeckWidthCm = Row.PetNeckWidthCm;
	NeckCenterCm = Row.PetNeckCenterCm;

	IdleAnim = Row.IdleAnimRef.LoadSynchronous();
	WalkAnim = Row.WalkAnimRef.LoadSynchronous();
	FleeAnim = Row.FleeAnimRef.LoadSynchronous();
	PlayLoop(IdleAnim);
	StandSecondsLeft = FMath::FRandRange(0.f, PetStandMaxSeconds);   // 여러 칸이 한꺼번에 걷기 시작하지 않게
	return true;
}

void ARanchPetActor::SetTetherHome(const FVector& Home, const FVector& Front)
{
	TetherHome = Home;
	FrontYaw.Reset();
	if (Front.Size2D() > KINDA_SMALL_NUMBER)
	{
		FrontYaw = Front.Rotation().Yaw;
	}
	SetActorLocation(RingPoint(Home, {}, FrontYaw));
	SetActorRotation(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f));
	WalkTarget = FVector::ZeroVector;   // 걷던 목표는 옛 말뚝 기준 — 새 부채꼴 밖일 수 있다
	PlayLoop(IdleAnim);
}

void ARanchPetActor::PlayLoop(UAnimSequence* Clip, float PlayRate)
{
	if (Clip == nullptr || (Clip == CurrentClip && PlayRate == CurrentPlayRate))
	{
		return;
	}
	if (Clip != CurrentClip)
	{
		CurrentClip = Clip;
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->PlayAnimation(Clip, true);
	}
	CurrentPlayRate = PlayRate;
	Body->SetPlayRate(PlayRate);
}

void ARanchPetActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FVector Here = GetActorLocation();
	if (WalkTarget.IsZero())
	{
		StandSecondsLeft -= DeltaSeconds;
		if (StandSecondsLeft <= 0.f)
		{
			WalkTarget = RingPoint(TetherHome, (Here - TetherHome).Rotation().Yaw, FrontYaw);
			// 사냥터 동물과 같은 떨어지는 순서 — 걷기 → 도주 느리게 → 대기 (M41-05). 새 4종은 걷기 칸이 비어 있다.
			float WalkRate = 1.f;
			UAnimSequence* const Clip = AAnimalCharacter::PickWalkClip(WalkAnim, FleeAnim, IdleAnim, WalkRate);
			PlayLoop(Clip, WalkRate);
		}
	}
	else
	{
		const FVector ToTarget = (WalkTarget - Here) * FVector(1.f, 1.f, 0.f);
		const float Step = PetWalkSpeedCmPerSec * DeltaSeconds;
		if (ToTarget.Size() <= Step)
		{
			Here.X = WalkTarget.X;
			Here.Y = WalkTarget.Y;
			WalkTarget = FVector::ZeroVector;
			StandSecondsLeft = FMath::FRandRange(PetStandMinSeconds, PetStandMaxSeconds);
			PlayLoop(IdleAnim);
		}
		else
		{
			Here += ToTarget.GetSafeNormal() * Step;
			// 임포트한 몸은 +Y 를 본다 — `AAnimalCharacter` 의 -90° 보정과 같은 것.
			const float WantYaw = ToTarget.Rotation().Yaw - 90.f;
			const FRotator Turned = FMath::RInterpConstantTo(GetActorRotation(), FRotator(0.f, WantYaw, 0.f),
				DeltaSeconds, PetTurnDegPerSec);
			SetActorRotation(Turned);
		}
	}

	// 발을 땅에 — 목장 바닥이 발판·흙이라 높이가 칸마다 조금씩 다르다.
	FHitResult Hit;
	const FVector Top(Here.X, Here.Y, TetherHome.Z + PetGroundTraceCm);
	const FVector Bottom(Here.X, Here.Y, TetherHome.Z - PetGroundTraceCm);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RanchPetGround), false, this);
	Params.AddIgnoredActor(GetOwner());   // 말뚝(칸 액터) 꼭대기에 올라서지 않게 — 발판은 따로 붙은 액터라 밟힌다
	Here.Z = GetWorld()->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Visibility, Params)
		? Hit.ImpactPoint.Z + FootOffsetZ : TetherHome.Z + FootOffsetZ;
	SetActorLocation(Here);
}
