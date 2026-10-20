#include "World/SeaActor.h"
#include "Snarehaven.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Texture.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/ConstructorHelpers.h"
#include "World/WaterRules.h"

namespace
{
	// 엔진 기본 도형은 100cm 한 변이다 — 배율 = 원하는 cm ÷ 100.
	constexpr float BasicShapeCm = 100.f;
	constexpr float SeabedThicknessCm = 200.f;
	constexpr float WallThicknessCm = 100.f;
	constexpr float WallHeightCm = 1000.f;
	// 거리장 밖 = 먼 바다 — `islands.py` 와 같은 값
	constexpr float FieldOutsideCm = 30000.f;

	UInstancedStaticMeshComponent* MakeInstanced(AActor& OwnerActor, const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent)
	{
		UInstancedStaticMeshComponent* const Component = OwnerActor.CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Component->SetupAttachment(Parent);
		Component->SetStaticMesh(Mesh);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCastShadow(false);
		return Component;
	}

	void AddBox(UInstancedStaticMeshComponent& Component, const FVector& Center, const FVector& SizeCm)
	{
		Component.AddInstance(FTransform(FRotator::ZeroRotator, Center, SizeCm / BasicShapeCm));
	}
}

float SnareSea::BobScale(float HullLengthCm)
{
	return HullLengthCm > BobReferenceCm ? FMath::Max(0.2f, BobReferenceCm / HullLengthCm) : 1.f;
}

FVector2f SnareSea::BobOffset(double Seconds, float Phase)
{
	const double Angle = UE_DOUBLE_TWO_PI * Seconds / BobPeriodSeconds + Phase;
	// 기울기는 오르내림보다 조금 느리게(0.7배) — 같은 주기면 배가 한 몸짓으로 끄덕이는 것처럼 보인다
	return FVector2f(BobHeightCm * FMath::Sin(Angle), BobRollDeg * FMath::Sin(Angle * 0.7 + 1.3));
}

const FName ASeaActor::FloatTag(TEXT("SnareFloat"));

ASeaActor::ASeaActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;   // 흔들 배가 있을 때만 켠다

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent->SetMobility(EComponentMobility::Movable);

	WaterSurface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WaterSurface"));
	WaterSurface->SetupAttachment(RootComponent);
	WaterSurface->SetStaticMesh(PlaneMesh.Object);
	WaterSurface->SetMobility(EComponentMobility::Movable);
	WaterSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WaterSurface->SetCastShadow(false);

	// 해저는 물이 가려 안 보인다 — 그리지 않고 밟히기만 한다.
	Seabed = MakeInstanced(*this, TEXT("Seabed"), CubeMesh.Object, RootComponent);
	Seabed->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Seabed->SetVisibility(false);

	OuterWalls = MakeInstanced(*this, TEXT("OuterWalls"), CubeMesh.Object, RootComponent);
	OuterWalls->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	OuterWalls->SetVisibility(false);
}

void ASeaActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Build();
}

void ASeaActor::BeginPlay()
{
	Super::BeginPlay();
	Build();

	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(FloatTag))
		{
			FVector Origin, Extent;
			It->GetActorBounds(true, Origin, Extent);
			// 위상은 자리에서 — 배마다 다르게 흔들리고 실행마다 같다
			Floaters.Add({*It, It->GetActorTransform(), SnareSea::BobScale(2.f * FMath::Max(Extent.X, Extent.Y)),
				static_cast<float>(FMath::Fmod(It->GetActorLocation().X * 0.013 + It->GetActorLocation().Y * 0.007, UE_DOUBLE_TWO_PI))});
		}
	}
	SetActorTickEnabled(Floaters.Num() > 0);
	UE_LOG(LogSnarehaven, Log, TEXT("바다 — 흔들 배 %d척"), Floaters.Num());
}

void ASeaActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const AGameStateBase* const GameState = GetWorld()->GetGameState();
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	for (const FFloater& Floater : Floaters)
	{
		if (AActor* const Boat = Floater.Actor.Get())
		{
			const FVector2f Bob = SnareSea::BobOffset(Now, Floater.Phase) * Floater.Scale;
			const FRotator Rot = Floater.Rest.Rotator() + FRotator(0.f, 0.f, Bob.Y);
			Boat->SetActorLocationAndRotation(Floater.Rest.GetLocation() + FVector(0.f, 0.f, Bob.X), Rot);
		}
	}
}

void ASeaActor::Build()
{
	Seabed->ClearInstances();
	OuterWalls->ClearInstances();
	BuiltSeaWidthCm = -1.f;

	const FWaterActivityRow* const Row = SnareWater::LoadRow();
	if (Row == nullptr)
	{
		WaterSurface->SetVisibility(false);
		return;
	}

	const float Half = IslandHalfExtentCm;
	const float Outer = Half + Row->SeaWidthCm;
	BuiltSeaWidthCm = Row->SeaWidthCm;

	// 수면 — 섬 밑까지 한 장으로 깐다(섬 바닥이 위에서 가린다). 삼각형 2개.
	WaterSurface->SetVisibility(true);
	WaterSurface->SetRelativeScale3D(FVector(Outer * 2.f / BasicShapeCm, Outer * 2.f / BasicShapeCm, 1.f));
	if (WaterMaterial)
	{
		// 해안 밝은 띠·거품 줄은 셰이더가 해안까지 거리로 그린다 — 목장은 정사각 섬, 사냥터는 거리 텍스처
		UMaterialInstanceDynamic* const Water = WaterSurface->CreateDynamicMaterialInstance(0, WaterMaterial);
		Water->SetVectorParameterValue(TEXT("IslandCenter"), FLinearColor(GetActorLocation()));
		Water->SetScalarParameterValue(TEXT("IslandHalfCm"), Half);
		const FBox2D Field = GetFieldBounds();
		Water->SetScalarParameterValue(TEXT("FieldOn"), Field.bIsValid && CoastTexture ? 1.f : 0.f);
		if (Field.bIsValid && CoastTexture)
		{
			Water->SetTextureParameterValue(TEXT("CoastTex"), CoastTexture);
			const FVector2D Size = Field.GetSize();
			Water->SetVectorParameterValue(TEXT("FieldBox"), FLinearColor(Field.Min.X, Field.Min.Y, Size.X, Size.Y));
		}
	}

	// 사냥터 — 땅 메시가 해안에서 물속으로 비탈져 내려가 깊은 바닥에 닿는다. 해저는 그 깊이의 판 한 장.
	if (HasField())
	{
		AddBox(*Seabed, FVector(0.f, 0.f, -DeepFloorBelowSurfaceCm - SeabedThicknessCm * 0.5f),
			FVector(Outer * 2.f, Outer * 2.f, SeabedThicknessCm));
	}

	// 해저 계단 — 섬 바닥에서 한 칸씩 내려가다 가장 깊은 곳에서 바깥 끝까지 평평하다.
	const float SurfaceZ = GetActorLocation().Z;
	const float DeepTopLocal = -DeepFloorBelowSurfaceCm;
	float Inner = Half;
	for (int32 Step = 1; !HasField(); ++Step)
	{
		const float TopLocal = FMath::Max(IslandFloorZ - Step * ShelfStepHeightCm - SurfaceZ, DeepTopLocal);
		const bool bLast = TopLocal <= DeepTopLocal || Inner + ShelfStepWidthCm >= Outer;
		const float BandOuter = bLast ? Outer : Inner + ShelfStepWidthCm;
		const float Mid = (Inner + BandOuter) * 0.5f;
		const float Width = BandOuter - Inner;
		const float Z = TopLocal - SeabedThicknessCm * 0.5f;
		AddBox(*Seabed, FVector(0.f, Mid, Z), FVector(BandOuter * 2.f, Width, SeabedThicknessCm));
		AddBox(*Seabed, FVector(0.f, -Mid, Z), FVector(BandOuter * 2.f, Width, SeabedThicknessCm));
		AddBox(*Seabed, FVector(Mid, 0.f, Z), FVector(Width, BandOuter * 2.f, SeabedThicknessCm));
		AddBox(*Seabed, FVector(-Mid, 0.f, Z), FVector(Width, BandOuter * 2.f, SeabedThicknessCm));
		if (bLast)
		{
			break;
		}
		Inner = BandOuter;
	}

	// 바깥 끝 — 보이지 않는 벽 4면.
	const float WallMid = Outer + WallThicknessCm * 0.5f;
	const float WallSpan = (Outer + WallThicknessCm) * 2.f;
	AddBox(*OuterWalls, FVector(0.f, WallMid, 0.f), FVector(WallSpan, WallThicknessCm, WallHeightCm));
	AddBox(*OuterWalls, FVector(0.f, -WallMid, 0.f), FVector(WallSpan, WallThicknessCm, WallHeightCm));
	AddBox(*OuterWalls, FVector(WallMid, 0.f, 0.f), FVector(WallThicknessCm, WallSpan, WallHeightCm));
	AddBox(*OuterWalls, FVector(-WallMid, 0.f, 0.f), FVector(WallThicknessCm, WallSpan, WallHeightCm));

	UE_LOG(LogSnarehaven, Log, TEXT("[Sea] 바다 지음: 섬 반폭 %.0f + 폭 %.0f = 벽 %.0fcm, 해저 %d, 거리장 %dx%d"),
		Half, Row->SeaWidthCm, Outer, Seabed->GetInstanceCount(), CoastFieldSize.X, CoastFieldSize.Y);
}

bool ASeaActor::IsOverSea(const FVector& WorldLocation) const
{
	const FVector Local = WorldLocation - GetActorLocation();
	const float Edge = FMath::Max(FMath::Abs(Local.X), FMath::Abs(Local.Y));
	return CoastDistance(WorldLocation) > 0.f && BuiltSeaWidthCm > 0.f && Edge < IslandHalfExtentCm + BuiltSeaWidthCm;
}

float ASeaActor::CoastDistance(const FVector& WorldLocation) const
{
	if (!HasField())
	{
		const FVector Local = WorldLocation - GetActorLocation();
		return FMath::Max(FMath::Abs(Local.X), FMath::Abs(Local.Y)) - IslandHalfExtentCm;
	}
	// `islands.py` signed_distance 와 같은 식 — 노드 쌍선형, 거리장 밖은 먼 바다
	const double FI = (WorldLocation.X - FieldOriginCm.X) / CoastCellCm;
	const double FJ = (WorldLocation.Y - FieldOriginCm.Y) / CoastCellCm;
	if (FI < 0.0 || FJ < 0.0 || FI >= CoastFieldSize.X - 1 || FJ >= CoastFieldSize.Y - 1)
	{
		return FieldOutsideCm;
	}
	const int32 I = FMath::FloorToInt32(FI);
	const int32 J = FMath::FloorToInt32(FJ);
	const float TI = static_cast<float>(FI - I);
	const float TJ = static_cast<float>(FJ - J);
	const int32 K = I * CoastFieldSize.Y + J;
	const float A = FMath::Lerp<float>(CoastField[K], CoastField[K + 1], TJ);
	const float B = FMath::Lerp<float>(CoastField[K + CoastFieldSize.Y], CoastField[K + CoastFieldSize.Y + 1], TJ);
	return FMath::Lerp(A, B, TI);
}

uint8 ASeaActor::ZoneCodeAt(const FVector& WorldLocation) const
{
	const int32 I = FMath::FloorToInt32((WorldLocation.X - FieldOriginCm.X) / ZoneCellCm);
	const int32 J = FMath::FloorToInt32((WorldLocation.Y - FieldOriginCm.Y) / ZoneCellCm);
	if (ZoneField.Num() != ZoneFieldSize.X * ZoneFieldSize.Y || I < 0 || J < 0 || I >= ZoneFieldSize.X || J >= ZoneFieldSize.Y)
	{
		return 0;
	}
	return ZoneField[I * ZoneFieldSize.Y + J];
}

bool ASeaActor::FindZonePoint(uint8 Code, const FVector2D& From, float MarginCm, float ReachCm, FVector2D& OutPoint) const
{
	// 존 칸 가운데를 From 에서 가까운 링부터 훑는다 — 링 안에서 가장 가까운 칸
	const int32 CI = FMath::FloorToInt32((From.X - FieldOriginCm.X) / ZoneCellCm);
	const int32 CJ = FMath::FloorToInt32((From.Y - FieldOriginCm.Y) / ZoneCellCm);
	const int32 MaxRing = FMath::CeilToInt32(ReachCm / ZoneCellCm);
	for (int32 Ring = 0; Ring <= MaxRing; ++Ring)
	{
		double Best = TNumericLimits<double>::Max();
		for (int32 DI = -Ring; DI <= Ring; ++DI)
		{
			for (int32 DJ = -Ring; DJ <= Ring; ++DJ)
			{
				if (FMath::Max(FMath::Abs(DI), FMath::Abs(DJ)) != Ring)
				{
					continue;
				}
				const FVector Cell(FieldOriginCm.X + (CI + DI + 0.5) * ZoneCellCm, FieldOriginCm.Y + (CJ + DJ + 0.5) * ZoneCellCm, 0.0);
				if ((ZoneCodeAt(Cell) & 0x0F) != Code || CoastDistance(Cell) > -MarginCm)
				{
					continue;
				}
				const double Dist = FVector2D::DistSquared(FVector2D(Cell), From);
				if (Dist < Best)
				{
					Best = Dist;
					OutPoint = FVector2D(Cell);
				}
			}
		}
		if (Best < TNumericLimits<double>::Max())
		{
			return true;
		}
	}
	return false;
}

FBox2D ASeaActor::GetMapBounds() const
{
	if (MapTexture == nullptr)
	{
		return FBox2D(ForceInit);
	}
	const FVector2D Center(GetActorLocation());
	return FBox2D(Center - FVector2D(IslandHalfExtentCm), Center + FVector2D(IslandHalfExtentCm));
}

FBox2D ASeaActor::GetFieldBounds() const
{
	if (!HasField())
	{
		return FBox2D(ForceInit);
	}
	return FBox2D(FieldOriginCm, FieldOriginCm + FVector2D(CoastFieldSize.X - 1, CoastFieldSize.Y - 1) * CoastCellCm);
}

ASeaActor* ASeaActor::FindSea(const UObject* WorldContext)
{
	UWorld* const World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	TActorIterator<ASeaActor> It(World);
	return It ? *It : nullptr;
}
