#include "Player/SnareCaptureBurstActor.h"
#include "Snarehaven.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// 반짝 조각 모양 (M21-03b2b). **보이는 모양만 정하는 연출 튜닝값**이라 DESIGN 3.1 엔 문장으로만 적는다.
	// 크기 5cm = 엔진 기본 상자(100cm)의 0.05배 — 피격 파편(4cm)보다 살짝 커서 "다른 일" 로 읽힌다.
	constexpr float SparkleSizeScale = 0.05f;
	constexpr float SparkleMinSpeed = 120.f;
	constexpr float SparkleMaxSpeed = 240.f;
	// 파편(980)보다 가볍게 떨어져 위에 머문다.
	constexpr float SparkleGravity = 300.f;
	// 조각이 도는 빠르기(도/초) — 면이 번갈아 빛을 받아 반짝인다.
	constexpr float SparkleSpinDegPerSecond = 540.f;
	// 옅은 금색 — 피격 파편 흙 갈색(0.45,0.35,0.25)과 갈린다.
	const FLinearColor SparkleColor(1.f, 0.85f, 0.3f);
	// 동물 메시가 없을 때 날아갈 자리표시 공 크기(엔진 공 100cm 의 0.4배 = 40cm, 작은 동물 몸통쯤).
	constexpr float PlaceholderScale = 0.4f;
	// 배낭 소켓을 못 찾을 때 몸 가운데 위로 잡는 높이(cm).
	constexpr float FallbackBackHeightCm = 40.f;
	const FName BackSocketName(TEXT("Socket_Back"));
}

ASnareCaptureBurstActor::ASnareCaptureBurstActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
	SetCanBeDamaged(false);

	USceneComponent* const Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	SparkleMesh = CubeAsset.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	PlaceholderMesh = SphereAsset.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialAsset(
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = MaterialAsset.Object;
}

void ASnareCaptureBurstActor::Start(const FVector& AnimalLocation, USkeletalMesh* PetMesh, const FVector& PetScale, AActor* Catcher)
{
	CatcherActor = Catcher;
	StartLocation = AnimalLocation;
	StartScale = PetScale;
	ElapsedSeconds = 0.f;
	bStarted = true;
	SetActorLocation(AnimalLocation);

	// ① 반짝 조각 — 동물 자리에서 위·바깥으로 퍼진다(피격 파편과 같은 인스턴스 메시 방식).
	if (SparkleMesh != nullptr)
	{
		Sparkles = NewObject<UInstancedStaticMeshComponent>(this, TEXT("Sparkles"));
		Sparkles->SetStaticMesh(SparkleMesh);
		Sparkles->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sparkles->SetCastShadow(false);
		Sparkles->SetMobility(EComponentMobility::Movable);
		Sparkles->SetUsingAbsoluteLocation(true);
		Sparkles->SetUsingAbsoluteRotation(true);
		Sparkles->SetUsingAbsoluteScale(true);
		if (BaseMaterial != nullptr)
		{
			UMaterialInstanceDynamic* const Mid = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			Mid->SetVectorParameterValue(TEXT("Color"), SparkleColor);
			Sparkles->SetMaterial(0, Mid);
		}
		Sparkles->SetupAttachment(GetRootComponent());
		Sparkles->RegisterComponent();
		Sparkles->SetWorldLocationAndRotation(AnimalLocation, FRotator::ZeroRotator);

		SparkleVelocities.Reset(SparkleCount);
		for (int32 Index = 0; Index < SparkleCount; ++Index)
		{
			FVector Dir = FMath::VRand();
			Dir.Z = FMath::Abs(Dir.Z);
			SparkleVelocities.Add(Dir * FMath::FRandRange(SparkleMinSpeed, SparkleMaxSpeed));
			Sparkles->AddInstance(FTransform(FRotator(45.f, FMath::FRandRange(0.f, 360.f), 45.f),
				AnimalLocation, FVector(SparkleSizeScale)), true);
		}
	}

	// ② 날아갈 몸 — 동물 메시를 같은 크기로 복사(애니 없이 기본 자세). 메시가 없는 동물은 자리표시 공.
	if (PetMesh != nullptr)
	{
		PetSkeletal = NewObject<USkeletalMeshComponent>(this, TEXT("PetFlight"));
		PetSkeletal->SetSkeletalMeshAsset(PetMesh);
		PetSkeletal->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PetSkeletal->SetCastShadow(false);
		PetSkeletal->SetupAttachment(GetRootComponent());
	}
	else if (PlaceholderMesh != nullptr)
	{
		StartScale = FVector(PlaceholderScale);
		PetPlaceholder = NewObject<UStaticMeshComponent>(this, TEXT("PetFlight"));
		PetPlaceholder->SetStaticMesh(PlaceholderMesh);
		PetPlaceholder->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PetPlaceholder->SetCastShadow(false);
		PetPlaceholder->SetupAttachment(GetRootComponent());
	}
	// 날아가는 동안 월드 기준으로 움직인다 — 등록 전에 절대 좌표로 놓아야 첫 프레임부터 동물 자리에 선다.
	if (USceneComponent* const Pet = GetPetBody())
	{
		Pet->SetUsingAbsoluteLocation(true);
		Pet->SetUsingAbsoluteScale(true);
		Pet->SetRelativeLocation_Direct(AnimalLocation);
		Pet->SetRelativeScale3D_Direct(StartScale);
		Pet->RegisterComponent();
	}

	UE_LOG(LogSnarehaven, Log, TEXT("[CaptureBurst] 반짝 %d개 · 펫 %s 이(가) %.2f초에 배낭으로"),
		GetSparkleInstanceCount(), PetMesh != nullptr ? *PetMesh->GetName() : TEXT("(자리표시 공)"), FlightSeconds);
}

void ASnareCaptureBurstActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bStarted)
	{
		return;
	}
	ElapsedSeconds += DeltaSeconds;

	// ① 반짝 — 끝으로 갈수록 작아지고 돈다.
	if (IsValid(Sparkles) && Sparkles->GetInstanceCount() > 0)
	{
		if (ElapsedSeconds >= SparkleLifeSeconds)
		{
			Sparkles->ClearInstances();
		}
		else
		{
			const float Shrink = 1.f - ElapsedSeconds / SparkleLifeSeconds;
			const int32 Count = FMath::Min(Sparkles->GetInstanceCount(), SparkleVelocities.Num());
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FTransform Instance;
				Sparkles->GetInstanceTransform(Index, Instance, true);
				SparkleVelocities[Index].Z -= SparkleGravity * DeltaSeconds;
				Instance.AddToTranslation(SparkleVelocities[Index] * DeltaSeconds);
				Instance.ConcatenateRotation(FQuat(FVector::UpVector, FMath::DegreesToRadians(SparkleSpinDegPerSecond * DeltaSeconds)));
				Instance.SetScale3D(FVector(SparkleSizeScale * Shrink));
				Sparkles->UpdateInstanceTransform(Index, Instance, true, Index == Count - 1);
			}
		}
	}

	// ② 펫 — 처음엔 천천히, 끝에 빨려 들듯(제곱) 배낭 쪽으로. 가운데가 위로 뜨는 포물선, 크기는 줄어든다.
	if (USceneComponent* const Pet = GetPetBody(); Pet != nullptr && Pet->IsVisible())
	{
		const float Alpha = GetFlightAlpha();
		if (Alpha >= 1.f)
		{
			Pet->SetWorldLocation(GetFlightTargetLocation());
			Pet->SetVisibility(false);
		}
		else
		{
			const float Eased = Alpha * Alpha;
			const FVector Line = FMath::Lerp(StartLocation, GetFlightTargetLocation(), Eased);
			const float Arc = 4.f * Eased * (1.f - Eased) * FlightArcCm;
			Pet->SetWorldLocation(Line + FVector(0.f, 0.f, Arc));
			Pet->SetWorldScale3D(StartScale * FMath::Lerp(1.f, FlightEndScale, Eased));
		}
	}

	if (ElapsedSeconds >= FMath::Max(SparkleLifeSeconds, FlightSeconds))
	{
		Destroy();
	}
}

FVector ASnareCaptureBurstActor::GetFlightTargetLocation() const
{
	const AActor* const Catcher = CatcherActor.Get();
	if (Catcher == nullptr)
	{
		return StartLocation;
	}
	if (const ACharacter* const Character = Cast<ACharacter>(Catcher))
	{
		const USkeletalMeshComponent* const Body = Character->GetMesh();
		if (IsValid(Body) && Body->DoesSocketExist(BackSocketName))
		{
			return Body->GetSocketLocation(BackSocketName);
		}
	}
	return Catcher->GetActorLocation() + FVector(0.f, 0.f, FallbackBackHeightCm);
}

int32 ASnareCaptureBurstActor::GetSparkleInstanceCount() const
{
	return IsValid(Sparkles) ? Sparkles->GetInstanceCount() : 0;
}

FVector ASnareCaptureBurstActor::GetPetLocation() const
{
	const USceneComponent* const Pet = GetPetBody();
	return Pet != nullptr ? Pet->GetComponentLocation() : FVector::ZeroVector;
}

bool ASnareCaptureBurstActor::IsPetVisible() const
{
	const USceneComponent* const Pet = GetPetBody();
	return Pet != nullptr && Pet->IsVisible();
}

USceneComponent* ASnareCaptureBurstActor::GetPetBody() const
{
	if (IsValid(PetSkeletal))
	{
		return PetSkeletal;
	}
	return IsValid(PetPlaceholder) ? PetPlaceholder.Get() : nullptr;
}
