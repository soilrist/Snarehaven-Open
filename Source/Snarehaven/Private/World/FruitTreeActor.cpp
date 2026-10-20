#include "World/FruitTreeActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UI/SnareWorldLabelWidget.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 🟡 나무 몸은 월드 활엽수 에셋(가지 소나무·술 치마 잎, 맵 나무와 같은 몸) 하나 — 직접 쌓은 도형은 맵의 다른 나무와 그림체가 달라 자리표시로 보였다(감독 판정 루프#947).
	const TCHAR* const TreeMeshPath = TEXT("/Game/Game/Environment/Nature/SM_Nature_Tree_04.SM_Nature_Tree_04");
	// 🟡 열매 세 개가 앉는 자리 — 잎 반지름·키에 대한 비율(카메라 쪽 −X 면). (앞뒤 / 좌우 / 높이)
	const FVector FruitSpotRatios[AFruitTreeActor::FruitSlots] = {
		FVector(-0.55f, -0.10f, 0.60f), FVector(-0.30f, -0.50f, 0.72f), FVector(-0.50f, 0.35f, 0.78f)};
	constexpr float FruitSizeCm = 44.f;   // 머리 한 변의 1/3 쯤 — 멀리서도 점이 아니게
	constexpr float MoundRadiusCm = 70.f;
	constexpr float LabelGapCm = 20.f;        // 이름표가 나무 꼭대기 위로 뜨는 틈 (cm)
	constexpr float LabelMinBodyCm = 30.f;    // 빈 자리(몸 없음)일 때 둔덕 위에 잡는 높이 (cm)
}

AFruitTreeActor::AFruitTreeActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	TreeRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TreeRoot"));
	TreeRoot->SetupAttachment(Root);

	TreeBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TreeBody"));
	TreeBody->SetupAttachment(TreeRoot);
	TreeBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TreeBody->SetMobility(EComponentMobility::Movable);

	Mound = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mound"));
	Mound->SetupAttachment(Root);
	Mound->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mound->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TreeMesh(TreeMeshPath);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ShapeMaterial.Succeeded())
	{
		FruitBaseMaterial = ShapeMaterial.Object;
	}
	if (Cylinder.Succeeded())
	{
		Mound->SetStaticMesh(Cylinder.Object);
		// 지름 = 둔덕 반지름 × 2 (기본 원기둥 100cm), 두께 6cm.
		Mound->SetRelativeScale3D(FVector(MoundRadiusCm / 50.f, MoundRadiusCm / 50.f, 0.06f));
		Mound->SetRelativeLocation(FVector(0.f, 0.f, 3.f));
	}
	Mound->SetMobility(EComponentMobility::Movable);
	// 메시 크기를 재서 다 자란 키(GrownHeightCm)에 맞추고, 바닥을 뿌리에 앉힌다. 열매 자리도 같은 크기에서 비율로 낸다.
	float CanopyRadiusCm = 100.f;
	float BodyScale = 1.f;
	float BodyBaseZ = 0.f;
	if (TreeMesh.Succeeded())
	{
		TreeBody->SetStaticMesh(TreeMesh.Object);
		const FBoxSphereBounds Bounds = TreeMesh.Object->GetBounds();
		BodyScale = GrownHeightCm / FMath::Max(2.f * Bounds.BoxExtent.Z, 1.f);
		BodyBaseZ = (Bounds.BoxExtent.Z - Bounds.Origin.Z) * BodyScale;
		CanopyRadiusCm = FMath::Max(Bounds.BoxExtent.X, Bounds.BoxExtent.Y) * BodyScale;
		TreeBody->SetRelativeScale3D(FVector(BodyScale));
		TreeBody->SetRelativeLocation(FVector(-Bounds.Origin.X * BodyScale, -Bounds.Origin.Y * BodyScale, BodyBaseZ));
	}
	for (int32 Index = 0; Index < FruitSlots; ++Index)
	{
		UStaticMeshComponent* const Fruit = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Fruit%d"), Index));
		Fruit->SetupAttachment(Root);
		Fruit->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Fruit->SetMobility(EComponentMobility::Movable);
		if (Sphere.Succeeded())
		{
			Fruit->SetStaticMesh(Sphere.Object);
		}
		Fruit->SetRelativeScale3D(FVector(FruitSizeCm / 100.f));
		Fruit->SetRelativeLocation(FVector(FruitSpotRatios[Index].X * CanopyRadiusCm, FruitSpotRatios[Index].Y * CanopyRadiusCm, FruitSpotRatios[Index].Z * GrownHeightCm));
		Fruit->SetVisibility(false);
		Fruits.Add(Fruit);
	}
	TreeRoot->SetVisibility(false, true);

	// 친구 나무 이름표 — 이름표(`UPlayerNameTagWidget`)와 같은 길: 화면 공간이라 거리에 안 작아지고 바닥 쪽 기준점이 나무 위에 앉는다.
	LabelCard = CreateDefaultSubobject<UWidgetComponent>(TEXT("LabelCard"));
	LabelCard->SetupAttachment(Root);
	LabelCard->SetRelativeLocation(FVector(0.f, 0.f, GrownHeightCm + LabelGapCm));
	LabelCard->SetWidgetSpace(EWidgetSpace::Screen);
	LabelCard->SetDrawAtDesiredSize(true);
	LabelCard->SetPivot(FVector2D(0.5f, 1.f));
	LabelCard->SetWidgetClass(USnareWorldLabelWidget::StaticClass());
	LabelCard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LabelCard->SetGenerateOverlapEvents(false);
	LabelCard->SetVisibility(false);
}

bool AFruitTreeActor::IsLabelShown() const
{
	return IsValid(LabelCard) && LabelCard->IsVisible();
}

FText AFruitTreeActor::GetShownLabelText() const
{
	const USnareWorldLabelWidget* const Widget = IsValid(LabelCard) ? Cast<USnareWorldLabelWidget>(LabelCard->GetUserWidgetObject()) : nullptr;
	return Widget != nullptr ? Widget->GetLabel() : FText();
}

namespace
{
	AFruitTreeActor* FindTreeInRange(const APawn* Pawn, bool bFriend)
	{
		UWorld* const World = IsValid(Pawn) ? Pawn->GetWorld() : nullptr;
		const UMeleeAttackComponent* const Melee = IsValid(Pawn) ? Pawn->FindComponentByClass<UMeleeAttackComponent>() : nullptr;
		const float RangeCm = IsValid(Melee) ? Melee->GetNpcTalkRangeCm() : -1.f;
		if (World == nullptr || RangeCm < 0.f)
		{
			return nullptr;
		}
		TArray<AFruitTreeActor*> All;
		for (TActorIterator<AFruitTreeActor> It(World); It; ++It)
		{
			All.Add(*It);
		}
		return AFruitTreeActor::PickNearest(All, Pawn->GetActorLocation(), RangeCm, bFriend);
	}
}

AFruitTreeActor* AFruitTreeActor::PickNearest(const TArray<AFruitTreeActor*>& Candidates, const FVector& Origin, float RangeCm, bool bFriend)
{
	AFruitTreeActor* Nearest = nullptr;
	float NearestDistSq = FMath::Square(RangeCm);
	for (AFruitTreeActor* const Tree : Candidates)
	{
		if (!IsValid(Tree) || Tree->IsFriendTree() != bFriend)
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Origin, Tree->GetActorLocation());
		if (DistSq <= NearestDistSq)
		{
			Nearest = Tree;
			NearestDistSq = DistSq;
		}
	}
	return Nearest;
}

AFruitTreeActor* AFruitTreeActor::FindInRange(const APawn* Pawn)
{
	return FindTreeInRange(Pawn, false);
}

AFruitTreeActor* AFruitTreeActor::FindFriendInRange(const APawn* Pawn)
{
	return FindTreeInRange(Pawn, true);
}

AFruitTreeActor* AFruitTreeActor::SpawnFriendTree(UWorld* World, const FTransform& Where, const FString& HostPlayFabId, const FString& HostNickname)
{
	if (World == nullptr || HostPlayFabId.IsEmpty())
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AFruitTreeActor* const Tree = World->SpawnActor<AFruitTreeActor>(AFruitTreeActor::StaticClass(), Where, Params);
	if (Tree == nullptr)
	{
		return nullptr;
	}
	Tree->FriendHostId = HostPlayFabId;
	SnareFruitTree::FTreeView View;
	View.Fruit = SnareFruitTree::NativeFruitOf(HostPlayFabId);
	View.Stage = SnareFruitTree::EStage::Grown;
	View.Fruits = FruitSlots;
	Tree->ApplyView(View);
	Tree->FriendLabel = SnareFruitTree::FriendTreeLabel(HostNickname, View.Fruit);
	if (IsValid(Tree->LabelCard))
	{
		Tree->LabelCard->InitWidget();
		if (USnareWorldLabelWidget* const Widget = Cast<USnareWorldLabelWidget>(Tree->LabelCard->GetUserWidgetObject()))
		{
			Widget->SetLabel(Tree->FriendLabel);
		}
		Tree->LabelCard->SetVisibility(true);
	}
	return Tree;
}

FTransform AFruitTreeActor::GetFriendTreeSpot()
{
	return FTransform(FRotator(0.f, 35.f, 0.f), FVector(-150.f, -340.f, 0.f));
}

AFruitTreeActor* AFruitTreeActor::EnsureFriendTree(UWorld* World, const FString& HostPlayFabId, const FString& HostNickname)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	AFruitTreeActor* Existing = nullptr;
	for (TActorIterator<AFruitTreeActor> It(World); It; ++It)
	{
		if (!It->IsFriendTree())
		{
			continue;
		}
		if (Existing == nullptr && It->GetFriendHostId() == HostPlayFabId)
		{
			Existing = *It;
		}
		else
		{
			It->Destroy();
		}
	}
	if (Existing != nullptr || HostPlayFabId.IsEmpty())
	{
		return Existing;
	}
	return SpawnFriendTree(World, GetFriendTreeSpot(), HostPlayFabId, HostNickname);
}

void AFruitTreeActor::ApplyView(const SnareFruitTree::FTreeView& View)
{
	Stage = View.Stage;
	FruitColor = SnareFruitTree::FruitColor(View.Fruit);

	const float Scale = SnareFruitTree::StageScale(Stage);
	TreeRoot->SetVisibility(Scale > 0.f, true);
	TreeRoot->SetRelativeScale3D(FVector(FMath::Max(Scale, KINDA_SMALL_NUMBER)));
	Mound->SetVisibility(Stage == SnareFruitTree::EStage::Empty);
	// 이름표는 지금 단계의 나무 꼭대기 바로 위 — 묘목인데 다 자란 키에 떠 있지 않게(INBOX 보정 10-11).
	LabelCard->SetRelativeLocation(FVector(0.f, 0.f, FMath::Max(GrownHeightCm * Scale, LabelMinBodyCm) + LabelGapCm));

	if (FruitMaterial == nullptr && FruitBaseMaterial != nullptr)
	{
		FruitMaterial = UMaterialInstanceDynamic::Create(FruitBaseMaterial, this);
		MoundMaterial = UMaterialInstanceDynamic::Create(FruitBaseMaterial, this);
		MoundMaterial->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.16f, 0.07f, 0.03f));
		Mound->SetMaterial(0, MoundMaterial);
	}
	if (FruitMaterial != nullptr)
	{
		FruitMaterial->SetVectorParameterValue(TEXT("Color"), FruitColor);
	}
	const int32 Shown = SnareFruitTree::VisibleFruits(Stage, View.Fruits);
	for (int32 Index = 0; Index < Fruits.Num(); ++Index)
	{
		if (FruitMaterial != nullptr)
		{
			Fruits[Index]->SetMaterial(0, FruitMaterial);
		}
		Fruits[Index]->SetVisibility(Index < Shown);
	}
}

int32 AFruitTreeActor::GetShownFruitCount() const
{
	int32 Count = 0;
	for (const UStaticMeshComponent* const Fruit : Fruits)
	{
		Count += Fruit->IsVisible() ? 1 : 0;
	}
	return Count;
}
