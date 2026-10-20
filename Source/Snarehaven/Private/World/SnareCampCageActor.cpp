#include "World/SnareCampCageActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Snarehaven.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SnareSound.h"

namespace
{
	// 반입 스크립트 `tools/unreal/import_harpoon_camp_mesh.py` 가 만드는 에셋 (원점 = 우리 중심 바닥).
	const TCHAR* CageMeshPath = TEXT("/Game/Game/Environment/Props/Story/SM_Prop_HarpoonCage_01.SM_Prop_HarpoonCage_01");
}

ASnareCampCageActor::ASnareCampCageActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SetReplicates(false);

	USceneComponent* const Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	CageMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CageMesh"));
	CageMesh->SetupAttachment(Root);
	CageMesh->SetMobility(EComponentMobility::Movable);
	const ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(CageMeshPath);
	if (Mesh.Succeeded())
	{
		CageMesh->SetStaticMesh(Mesh.Object);
	}
}

const TArray<FVector2D>& ASnareCampCageActor::Offsets()
{
	static const TArray<FVector2D> Spots = { FVector2D(-350.0, -350.0), FVector2D(0.0, -450.0), FVector2D(350.0, -350.0) };
	return Spots;
}

bool ASnareCampCageActor::TakeStrike()
{
	if (IsBroken())
	{
		return false;
	}
	++HitsTaken;
	USnareSoundSubsystem::PlayAt(this, TEXT("Hit"), GetActorLocation(), 0.8f);
	if (!IsBroken())
	{
		// 맞을 때마다 철컹 기울어 흔들린다 — 몇 번 더 쳐야 하는지 눈으로 보인다.
		CageMesh->SetRelativeRotation(FRotator(0.f, HitsTaken % 2 == 0 ? 3.f : -3.f, HitsTaken % 2 == 0 ? 2.f : -2.f));
		return false;
	}
	CageMesh->SetVisibility(false);
	CageMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UE_LOG(LogSnarehaven, Log, TEXT("[Camp] 우리 부서짐 — %d/%d"), CountBroken(GetWorld()), CountAll(GetWorld()));
	return true;
}

bool ASnareCampCageActor::StrikeNearest(UWorld* World, const FVector& From)
{
	ASnareCampCageActor* Best = nullptr;
	float BestDist = StrikeRangeCm;
	for (TActorIterator<ASnareCampCageActor> It(World); It; ++It)
	{
		const float Dist = static_cast<float>(FVector::Dist2D(From, It->GetActorLocation()));
		if (!It->IsBroken() && Dist <= BestDist)
		{
			Best = *It;
			BestDist = Dist;
		}
	}
	if (Best == nullptr)
	{
		return false;
	}
	Best->TakeStrike();
	return true;
}

int32 ASnareCampCageActor::CountAll(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<ASnareCampCageActor> It(World); It; ++It)
	{
		++Count;
	}
	return Count;
}

int32 ASnareCampCageActor::CountBroken(UWorld* World)
{
	int32 Count = 0;
	for (TActorIterator<ASnareCampCageActor> It(World); It; ++It)
	{
		Count += It->IsBroken() ? 1 : 0;
	}
	return Count;
}

bool ASnareCampCageActor::AllBroken(UWorld* World)
{
	const int32 All = CountAll(World);
	return All > 0 && CountBroken(World) == All;
}
