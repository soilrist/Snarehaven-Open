#include "World/BoatActor.h"
#include "Snarehaven.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	void SetPassengerMode(APawn* Pawn, bool bSeated)
	{
		if (!IsValid(Pawn))
		{
			return;
		}
		// 앉은 승객 캡슐이 운전자를 밀면 배가 제자리에서 돈다 — 앉은 동안만 충돌을 끈다.
		Pawn->SetActorEnableCollision(!bSeated);
		if (ACharacter* const Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* const Movement = Character->GetCharacterMovement())
			{
				Movement->SetMovementMode(bSeated ? MOVE_None : MOVE_Walking);
			}
		}
	}
}

ABoatActor::ABoatActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	// 루트는 빈 씬 컴포넌트 — 흔들림은 Hull 의 상대 트랜스폼만 바꿔 루트(복제되는 이동)와 갈라 둔다.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> BoatMesh(TEXT("/Game/Game/Environment/Vehicles/Boat/SM_Vehicle_Boat_01.SM_Vehicle_Boat_01"));
	Hull = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Hull"));
	Hull->SetupAttachment(RootComponent);
	Hull->SetStaticMesh(BoatMesh.Object);
	Hull->SetMobility(EComponentMobility::Movable);
	// 운전자가 해저를 걸으며 끌고 간다 — 몸통이 막으면 운전자가 못 움직인다.
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ABoatActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	const UStaticMesh* const Mesh = Hull ? Hull->GetStaticMesh() : nullptr;
	if (Mesh == nullptr)
	{
		return;
	}
	// 메시 긴 축을 액터 +X(진행 방향)에 맞추고, 그 길이가 `HullLengthCm` 이 되게 균등 배율한다.
	const FVector Extent = Mesh->GetBoundingBox().GetSize();
	const bool bLongAxisIsY = Extent.Y > Extent.X;
	HullBaseYaw = bLongAxisIsY ? 90.f : 0.f;
	const float MeshLength = bLongAxisIsY ? Extent.Y : Extent.X;
	if (MeshLength > KINDA_SMALL_NUMBER)
	{
		Hull->SetRelativeScale3D(FVector(HullLengthCm / MeshLength));
	}
	Hull->SetRelativeLocationAndRotation(FVector(0.f, 0.f, -DraftCm), FRotator(0.f, HullBaseYaw, 0.f));
}

void ABoatActor::UpdateBob(float DeltaSeconds)
{
	if (Hull == nullptr || DeltaSeconds <= 0.f)
	{
		return;
	}
	// 속도는 이 기계가 본 루트 이동에서 잰다 — 손님은 복제된 위치, 호스트는 운전자를 따른 위치.
	const FVector Location = GetActorLocation();
	const float Speed = bHasLastBobLocation ? FVector::Dist2D(Location, LastBobLocation) / DeltaSeconds : 0.f;
	LastBobLocation = Location;
	bHasLastBobLocation = true;
	const float TargetAlpha = BobFullSpeedCmPerSecond > 0.f ? FMath::Clamp(Speed / BobFullSpeedCmPerSecond, 0.f, 1.f) : 0.f;
	// 복제 위치는 계단처럼 와서 한 틱 속도가 튄다 — 1초 남짓에 걸쳐 따라가게 거른다(보이는 값이라 표 밖).
	BobSpeedAlpha = FMath::FInterpTo(BobSpeedAlpha, TargetAlpha, DeltaSeconds, 3.f);

	BobTime += DeltaSeconds;
	const float Mul = FMath::Lerp(1.f, BobSpeedAmplitudeMul, BobSpeedAlpha);
	auto Wave = [this](float PeriodSeconds) { return PeriodSeconds > 0.f ? FMath::Sin(UE_TWO_PI * BobTime / PeriodSeconds) : 0.f; };
	const float Heave = HeaveAmplitudeCm * Mul * Wave(HeavePeriodSeconds);
	const float Roll = RollAmplitudeDeg * Mul * Wave(RollPeriodSeconds);
	const float Pitch = PitchAmplitudeDeg * Mul * Wave(PitchPeriodSeconds) + BowLiftMaxDeg * BobSpeedAlpha;
	// 롤·피치는 액터 축 기준이라 메시 기본 회전(HullBaseYaw) 위에 얹는다.
	const FQuat Tilt = FRotator(Pitch, 0.f, Roll).Quaternion() * FRotator(0.f, HullBaseYaw, 0.f).Quaternion();
	Hull->SetRelativeLocationAndRotation(FVector(0.f, 0.f, Heave - DraftCm), Tilt);
}

void ABoatActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABoatActor, Riders);
	DOREPLIFETIME(ABoatActor, OwnerPawn);
	DOREPLIFETIME(ABoatActor, MotorTier);
	DOREPLIFETIME(ABoatActor, SurfaceZ);
}

void ABoatActor::InitBoat(APawn* InOwnerPawn, int32 InMotorTier, int32 SeatCount, float InSurfaceZ)
{
	OwnerPawn = InOwnerPawn;
	MotorTier = InMotorTier;
	SurfaceZ = InSurfaceZ;
	Riders.Init(nullptr, FMath::Max(SeatCount, 1));
	UE_LOG(LogSnarehaven, Log, TEXT("[Boat] 띄움: 주인 %s · 모터 %d · 좌석 %d"),
		*GetNameSafe(InOwnerPawn), InMotorTier, Riders.Num());
}

int32 ABoatActor::FindSeat(const APawn* Pawn) const
{
	return Pawn != nullptr ? Riders.IndexOfByKey(Pawn) : INDEX_NONE;
}

bool ABoatActor::HasFreeSeat() const
{
	return Riders.Contains(nullptr);
}

int32 ABoatActor::SeatPawn(APawn* Pawn)
{
	if (!IsValid(Pawn) || FindSeat(Pawn) != INDEX_NONE)
	{
		return INDEX_NONE;
	}
	const int32 Seat = Riders.IndexOfByKey(nullptr);
	if (Seat == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	Riders[Seat] = Pawn;
	ApplyRiders();
	return Seat;
}

bool ABoatActor::UnseatPawn(APawn* Pawn)
{
	const int32 Seat = FindSeat(Pawn);
	if (Seat == INDEX_NONE)
	{
		return false;
	}
	Riders[Seat] = nullptr;
	ApplyRiders();
	return true;
}

void ABoatActor::UnseatAll()
{
	for (TObjectPtr<APawn>& Rider : Riders)
	{
		Rider = nullptr;
	}
	ApplyRiders();
}

void ABoatActor::OnRep_Riders()
{
	ApplyRiders();
}

void ABoatActor::ApplyRiders()
{
	// 목록에서 빠진 승객 → 떼고 걷게 한다.
	for (const TWeakObjectPtr<APawn>& Weak : AttachedPassengers)
	{
		APawn* const Pawn = Weak.Get();
		const int32 Seat = FindSeat(Pawn);
		if (IsValid(Pawn) && (Seat == INDEX_NONE || Seat == 0))
		{
			Pawn->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			SetPassengerMode(Pawn, false);
		}
	}
	AttachedPassengers.Reset();

	for (int32 Seat = 1; Seat < Riders.Num(); ++Seat)
	{
		APawn* const Pawn = Riders[Seat];
		if (!IsValid(Pawn))
		{
			continue;
		}
		SetPassengerMode(Pawn, true);
		// 흔들리는 Hull 에 붙여야 출렁임·기울기를 같이 탄다. 캡슐 중심 = 좌석 바닥 + 반높이.
		const FTransform SeatTransform = GetSeatWorldTransform(Seat);
		Pawn->SetActorLocation(SeatTransform.GetLocation() + SeatTransform.GetRotation().GetUpVector() * Pawn->GetSimpleCollisionHalfHeight());
		Pawn->AttachToComponent(Hull, FAttachmentTransformRules::KeepWorldTransform);
		AttachedPassengers.Add(Pawn);
	}
}

void ABoatActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBob(DeltaSeconds);
	if (!HasAuthority())
	{
		return;
	}
	// 운전자 XY·방향을 따라 수면에 뜬다 — 운전자가 선미 좌석에 오도록 좌석 오프셋만큼 비켜 둔다. 움직임은 복제된다.
	if (const APawn* const Driver = GetDriver(); IsValid(Driver))
	{
		const FRotator Yaw(0.f, Driver->GetActorRotation().Yaw, 0.f);
		const FVector SeatXY(DriverSeatOffsetCm.X, DriverSeatOffsetCm.Y, 0.f);
		const FVector Center = Driver->GetActorLocation() - Yaw.RotateVector(SeatXY);
		SetActorLocationAndRotation(FVector(Center.X, Center.Y, SurfaceZ), Yaw);
	}
}

void ABoatActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 파괴 전에 승객을 내려 둔다 — 붙은 채 사라지면 승객도 같이 사라진다.
	for (TObjectPtr<APawn>& Rider : Riders)
	{
		Rider = nullptr;
	}
	ApplyRiders();
	Super::EndPlay(EndPlayReason);
}

FTransform ABoatActor::GetSeatWorldTransform(int32 Seat) const
{
	if (Hull == nullptr)
	{
		return GetActorTransform();
	}
	// Hull 회전에서 메시 기본 요를 빼면 액터 축 + 흔들림 기울기만 남는다. 거리는 cm 그대로(배율 제외).
	const FQuat Frame = Hull->GetComponentQuat() * FRotator(0.f, -HullBaseYaw, 0.f).Quaternion();
	const FVector Offset = Seat == 0 ? DriverSeatOffsetCm
		: PassengerSeatOffsetsCm.IsValidIndex(Seat - 1) ? PassengerSeatOffsetsCm[Seat - 1] : FVector::ZeroVector;
	return FTransform(Frame, Hull->GetComponentLocation() + Frame.RotateVector(FVector(Offset.X, Offset.Y, SeatFloorCm)));
}

ABoatActor* ABoatActor::FindBoatOf(const APawn* Pawn)
{
	UWorld* const World = Pawn ? Pawn->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	for (TActorIterator<ABoatActor> It(World); It; ++It)
	{
		if (It->FindSeat(Pawn) != INDEX_NONE)
		{
			return *It;
		}
	}
	return nullptr;
}
