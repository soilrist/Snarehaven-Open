#include "World/FishingSpotActor.h"
#include "Snarehaven.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/RotatingMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "World/WaterRules.h"

AFishingSpotActor::AFishingSpotActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> FishMesh(
		TEXT("/Game/Game/Environment/Props/FishShadow/SM_Prop_FishShadow_01.SM_Prop_FishShadow_01"));
	// 원본 머리 = 메시 +X 끝(DESIGN 8.13 「방향」). 길이를 맞추는 배율을 폭에도(원본 비율), 높이만 따로 눌러 납작하게.
	const FBox FishBox = FishMesh.Succeeded() ? FishMesh.Object->GetBoundingBox() : FBox(FVector(-50.f), FVector(50.f));
	const FVector FishSize = FishBox.GetSize();
	const float LengthScale = ShadowLengthCm / FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(FishSize.X));
	const FVector ShadowScale(LengthScale, LengthScale, ShadowHeightCm / FMath::Max(KINDA_SMALL_NUMBER, static_cast<float>(FishSize.Z)));
	// 메시 가운데가 원 위 점에, 밑면이 포인트 높이에 오도록 옮길 몫(메시 로컬, 배율 적용).
	const FVector MeshOffset = FVector(-FishBox.GetCenter().X, -FishBox.GetCenter().Y, -FishBox.Min.Z) * ShadowScale;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	ShadowPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ShadowPivot"));
	ShadowPivot->SetupAttachment(RootComponent);

	// 물고기 셋이 120도씩 떨어져 한 원을 돈다.
	constexpr int32 FishCount = 3;
	for (int32 Index = 0; Index < FishCount; ++Index)
	{
		UStaticMeshComponent* const Shadow = CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Shadow%d"), Index));
		Shadow->SetupAttachment(ShadowPivot);
		Shadow->SetStaticMesh(FishMesh.Object);
		Shadow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Shadow->SetCastShadow(false);
		const float Angle = 2.f * PI * Index / FishCount;
		// 요 +90° = 도는 방향(아래 Yaw +) 접선 — 메시 +X(머리)가 앞으로 간다.
		const FRotator Facing(0.f, FMath::RadiansToDegrees(Angle) + 90.f, 0.f);
		Shadow->SetRelativeLocation(FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * ShadowCircleRadiusCm + Facing.RotateVector(MeshOffset));
		Shadow->SetRelativeRotation(Facing);
		Shadow->SetRelativeScale3D(ShadowScale);
		Shadows.Add(Shadow);
	}

	Circling = CreateDefaultSubobject<URotatingMovementComponent>(TEXT("Circling"));
	Circling->SetUpdatedComponent(ShadowPivot);
	Circling->RotationRate = FRotator(0.f, 45.f, 0.f);
}

void AFishingSpotActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AFishingSpotActor, RemainingCatches);
}

void AFishingSpotActor::BeginPlay()
{
	Super::BeginPlay();
	for (UStaticMeshComponent* const Shadow : Shadows)
	{
		if (Shadow && ShadowMaterial)
		{
			Shadow->SetMaterial(0, ShadowMaterial);
		}
	}
	EFishingSpotKind Kind;
	if (!GetKind(Kind))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("[Fishing] %s: spotId '%s' 의 머리를 모른다 — 서버 FISHING_SPOTS 키와 같아야 한다"),
			*GetName(), *SpotId);
	}
	if (HasAuthority())
	{
		Refill();
	}
	ApplyVisibility();
}

bool AFishingSpotActor::GetKind(EFishingSpotKind& OutKind) const
{
	return SnareWater::SpotKindFromId(SpotId, OutKind);
}

void AFishingSpotActor::Refill()
{
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	RemainingCatches = Row ? Row->SpotCatchesBeforeGone : -1;
	ApplyVisibility();
}

void AFishingSpotActor::ConsumeCatch()
{
	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	if (!HasAuthority() || Row == nullptr || RemainingCatches <= 0)
	{
		return;
	}
	--RemainingCatches;
	UE_LOG(LogSnarehaven, Log, TEXT("[Fishing] %s 남은 마리 %d"), *SpotId, RemainingCatches);
	if (RemainingCatches == 0)
	{
		GetWorldTimerManager().SetTimer(RespawnTimer, this, &AFishingSpotActor::Refill, Row->SpotRespawnSeconds, false);
	}
	ApplyVisibility();
}

float AFishingSpotActor::GetRespawnRemainingSeconds() const
{
	return GetWorldTimerManager().IsTimerActive(RespawnTimer) ? GetWorldTimerManager().GetTimerRemaining(RespawnTimer) : -1.f;
}

void AFishingSpotActor::OnRep_RemainingCatches()
{
	ApplyVisibility();
}

void AFishingSpotActor::ApplyVisibility()
{
	ShadowPivot->SetVisibility(IsAvailable(), true);
}

AFishingSpotActor* AFishingSpotActor::FindNearestAvailable(const UObject* WorldContext, const FVector& From, float RangeCm)
{
	UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (World == nullptr || RangeCm < 0.f)
	{
		return nullptr;
	}
	AFishingSpotActor* Best = nullptr;
	float BestDistance = RangeCm;
	for (TActorIterator<AFishingSpotActor> It(World); It; ++It)
	{
		const float Distance = FVector::Dist2D(It->GetActorLocation(), From);
		if (It->IsAvailable() && Distance <= BestDistance)
		{
			Best = *It;
			BestDistance = Distance;
		}
	}
	return Best;
}
