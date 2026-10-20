#include "World/SnareHarpoonCampActor.h"

#include "Backend/SnareStory.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/UObjectIterator.h"
#include "Snarehaven.h"
#include "World/SnareCampCageActor.h"
#include "World/SnareHookNpcActor.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 반입 스크립트 `tools/unreal/import_harpoon_camp_mesh.py` 가 만드는 에셋 (원본 `make_harpoon_camp_mesh.py` — 원점 = 캠프 중심 바닥).
	const TCHAR* CampMeshPath = TEXT("/Game/Game/Environment/Props/Story/SM_Prop_HarpoonCamp_01.SM_Prop_HarpoonCamp_01");
}

ASnareHarpoonCampActor::ASnareHarpoonCampActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);

	USceneComponent* const Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	CampMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CampMesh"));
	CampMesh->SetupAttachment(Root);
	CampMesh->SetMobility(EComponentMobility::Movable);
	const ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(CampMeshPath);
	if (Mesh.Succeeded())
	{
		CampMesh->SetStaticMesh(Mesh.Object);
	}
}

const TArray<FVector2D>& ASnareHarpoonCampActor::TentSpots()
{
	static const TArray<FVector2D> Spots = { FVector2D(-450.0, 300.0), FVector2D(0.0, 520.0), FVector2D(450.0, 300.0) };
	return Spots;
}

void ASnareHarpoonCampActor::ClearRocksAround(UWorld* World, const FVector2D& Center)
{
	const auto IsRock = [](const UStaticMesh* Mesh) { return Mesh != nullptr && (Mesh->GetName().Contains(TEXT("Rock")) || Mesh->GetName().Contains(TEXT("Boulder"))); };
	int32 Removed = 0;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		UStaticMeshComponent* const Mesh = It->GetStaticMeshComponent();
		if (IsRock(Mesh->GetStaticMesh()) && FVector2D::Distance(FVector2D(It->GetActorLocation()), Center) <= ClearRocksRadiusCm)
		{
			Mesh->SetVisibility(false);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			++Removed;
		}
	}
	for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
	{
		if (It->GetWorld() != World || !IsRock(It->GetStaticMesh()))
		{
			continue;
		}
		for (int32 Index = It->GetInstanceCount() - 1; Index >= 0; --Index)
		{
			FTransform Instance;
			if (It->GetInstanceTransform(Index, Instance, true) && FVector2D::Distance(FVector2D(Instance.GetLocation()), Center) <= ClearRocksRadiusCm)
			{
				It->RemoveInstance(Index);
				++Removed;
			}
		}
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[Camp] 캠프 안 바위 %d개 치움"), Removed);
}

ASnareHarpoonCampActor* ASnareHarpoonCampActor::SpawnAtSpot(UWorld* World)
{
	if (World == nullptr)
	{
		return nullptr;
	}
	if (TActorIterator<ASnareHarpoonCampActor> Existing(World); Existing)
	{
		return *Existing;
	}

	FVector Spot(SnareStory::CampSpotX, SnareStory::CampSpotY, 0.f);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, FVector(Spot.X, Spot.Y, 5000.0), FVector(Spot.X, Spot.Y, -5000.0), ECC_WorldStatic))
	{
		Spot.Z = Hit.ImpactPoint.Z;
	}
	ClearRocksAround(World, FVector2D(Spot));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnareHarpoonCampActor* const Camp = World->SpawnActor<ASnareHarpoonCampActor>(Spot, FRotator::ZeroRotator, Params);
	// 쇠창살 우리 셋은 캠프 메시와 따로 — 부술 수 있다 (18단계). 캠프 바닥 높이를 그대로 쓴다.
	for (const FVector2D& Offset : ASnareCampCageActor::Offsets())
	{
		World->SpawnActor<ASnareCampCageActor>(Spot + FVector(Offset.X, Offset.Y, 0.0), FRotator::ZeroRotator, Params);
	}
	ASnareHookNpcActor::SpawnAtCamp(World, Spot);  // 두목 갈고리 — 18·20단계 대사 상대
	UE_LOG(LogSnarehaven, Log, TEXT("[Camp] 작살단 캠프 세움 — (%.0f, %.0f, %.0f)"), Spot.X, Spot.Y, Spot.Z);
	return Camp;
}
