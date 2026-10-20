#include "Gathering/GatherableActor.h"
#include "Snarehaven.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/GatherableRow.h"
#include "Engine/CollisionProfile.h"
#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "SnarehavenCharacter.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"

namespace
{
	// 채집물과 그 몸이 될 장식 나무는 스포너가 **같은 자리**에 세운다. 부동소수 오차만 받아 주는 거리다
	// (밸런스 숫자가 아니라 "같은 자리" 의 판정 여유).
	constexpr float DecorMatchToleranceCm = 10.f;

	// 숲 나무 인스턴스 채집물의 반경 — 밸런스 숫자가 아니라 성능·지연 여유다. 사거리의 4배 안에 미리 세워 둬야
	// 사람이 사거리에 들기 전에 게스트에게 복제돼 내려가고, 8배 밖으로 멀어져야 지워서 경계에서 깜빡이지 않는다.
	constexpr float InstancedTreeSpawnRangeScale = 4.f;
	constexpr float InstancedTreeReleaseRangeScale = 8.f;
	constexpr float InstancedTreeReleaseCheckSeconds = 5.f;

	// 나무 흔들림·넘어감 연출 (DESIGN 8.10 「쓰러짐 연출」) — 밸런스 숫자가 아니라 보이는 박자다.
	// 흔들림은 도끼질 한 번(공격 간격 0.6초) 안에 끝나고, 넘어감은 원본처럼 한 박자 넘어간 뒤 사라진다.
	constexpr float TiltStepSeconds = 1.f / 30.f;
	constexpr float ShakeSeconds = 0.25f;
	constexpr float ShakeAngleDeg = 4.f;
	constexpr float FallSeconds = 1.f;
	constexpr float FallAngleDeg = 85.f;

	bool MeshNameStartsWith(const UStaticMeshComponent* MeshComponent, const FString& Prefix)
	{
		const UStaticMesh* const Mesh = IsValid(MeshComponent) ? MeshComponent->GetStaticMesh() : nullptr;
		return IsValid(Mesh) && Mesh->GetName().StartsWith(Prefix);
	}

	bool MeshNameStartsWith(const AStaticMeshActor& Decor, const FString& Prefix)
	{
		return MeshNameStartsWith(Decor.GetStaticMeshComponent(), Prefix);
	}

	/** 이 월드에서 접두어 메시를 쓰는 인스턴스 컴포넌트(폴리지 포함)마다 부른다. */
	template <typename FuncType>
	void ForEachTreeInstances(const UWorld* World, const FString& Prefix, FuncType&& Visit)
	{
		// 폴리지는 맵 어느 액터에든 붙는다 — 액터 전부를 돌지 않고 클래스 해시로 이 컴포넌트만 훑는다.
		for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
		{
			UInstancedStaticMeshComponent* const Instances = *It;
			if (IsValid(Instances) && Instances->GetWorld() == World && Instances->IsRegistered()
				&& MeshNameStartsWith(Instances, Prefix))
			{
				Visit(*Instances);
			}
		}
	}

	/** `Where` 에 뿌리(원점)를 둔 인스턴스 (없으면 INDEX_NONE). */
	int32 FindInstanceAt(const UInstancedStaticMeshComponent& Instances, const FVector& Where)
	{
		for (const int32 Index : Instances.GetInstancesOverlappingSphere(Where, DecorMatchToleranceCm))
		{
			FTransform Transform;
			if (Instances.GetInstanceTransform(Index, Transform, true)
				&& FVector::DistSquared(Transform.GetLocation(), Where) <= FMath::Square(DecorMatchToleranceCm))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	/** 장식 자리에 복제 나무 하나를 세운다 (StaticMeshActor 나무·숲 인스턴스 나무 공용). 부른 쪽이 FinishSpawning 한다. */
	AGatherableActor* SpawnTreeAt(UWorld* World, UDataTable* Table, FName RowName, const FTransform& Spot)
	{
		AGatherableActor* const Tree = World->SpawnActorDeferred<AGatherableActor>(
			AGatherableActor::StaticClass(), Spot, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Tree != nullptr)
		{
			Tree->SetGatherableData(Table, RowName);
			// 누가 베면 모두에게 사라져야 한다 — 호스트가 세운 것만 복제한다 (DESIGN 8.10 "호스트 굴림").
			Tree->SetReplicates(true);
		}
		return Tree;
	}
}

AGatherableActor::AGatherableActor()
{
	// 채집은 버튼을 누른 순간에만 일어난다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryActorTick.bCanEverTick = false;

	// 맵에 깐 채집 더미도 누가 집으면 모두에게 사라져야 한다 (M13-02 — 존 칸 채집 더미는 여럿이 같이 본다).
	bReplicates = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	RootComponent = Body;
	// 충돌은 이번 조각 범위 밖이다 — 채집은 거리로 판정하고(콜리전이 아니라), 막고 못 막고는
	// 맵 배치 조각에서 정한다. 지금 켜 두면 자리표시 큐브 크기에 플레이어 동선이 묶인다.
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);

	// 자리표시 메시 — 진짜 아트(행의 MeshRef)가 들어오기 전까지 화면에 보이는 몸 역할이다
	// (ASnarehavenCharacter 의 자리표시 실린더와 같은 이유 · Autonomy.md 🟢 임시 그래픽).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaceholderMeshAsset(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (PlaceholderMeshAsset.Succeeded())
	{
		Body->SetStaticMesh(PlaceholderMeshAsset.Object);
	}

	// 줄기 충돌 — 존 소품 나무 `TrunkCollision` 과 같은 엔진 원기둥(지름·높이 100cm, 가운데 기준)을 숨겨 쓴다.
	Trunk = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Trunk"));
	Trunk->SetupAttachment(Body);
	Trunk->SetHiddenInGame(true);
	Trunk->SetVisibility(false);
	Trunk->SetCastShadow(false);
	Trunk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Trunk->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TrunkMeshAsset(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (TrunkMeshAsset.Succeeded())
	{
		Trunk->SetStaticMesh(TrunkMeshAsset.Object);
	}

	// C++ 전용 프로젝트라 블루프린트 없이 기본 테이블을 주려면 생성자 경로 참조뿐이다
	// (AAnimalCharacter 와 같은 방식 — 하드 참조라 런타임 문자열 조회가 아니다).
	static ConstructorHelpers::FObjectFinder<UDataTable> GatherableTableAsset(
		TEXT("/Game/Game/Data/Gatherables/DT_GatherableTable.DT_GatherableTable"));
	if (GatherableTableAsset.Succeeded())
	{
		GatherableTable = GatherableTableAsset.Object;
	}
}

void AGatherableActor::BeginPlay()
{
	Super::BeginPlay();

	InitializeFromTable();
	// 호스트·게스트 모두 자기 맵의 장식 나무를 찾는다 — 장식은 복제되지 않고 맵에 각자 있다.
	AdoptDecorIfAny();
	ApplyGatheredVisual();
}

void AGatherableActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 타이머를 남겨 두면 파괴된 액터를 다시 부른다 (engines/unreal.md 코드 규칙 7).
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
		World->GetTimerManager().ClearTimer(ReleaseTimerHandle);
		World->GetTimerManager().ClearTimer(GatheredVisualTimerHandle);
	}
	StopTilt();

	// 게스트에선 멀어져 복제가 끊기면 이 액터만 사라진다 — 숨긴 인스턴스를 되돌려 둬야 다시 가까이 왔을 때
	// 원래 모양(배율)을 다시 읽는다. 아직 베인 상태면 복제가 다시 숨긴다.
	SetInstanceHidden(false);

	Super::EndPlay(EndPlayReason);
}

void AGatherableActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGatherableActor, GatherableRowName);
	DOREPLIFETIME(AGatherableActor, bGathered);
	DOREPLIFETIME(AGatherableActor, HitsTaken);
	DOREPLIFETIME(AGatherableActor, HitYawDeg);
}

int32 AGatherableActor::SpawnAtDecorMeshes(UWorld* World)
{
	if (!IsValid(World))
	{
		return 0;
	}

	// 표는 기본값(CDO)에서 빌린다 — 표 경로는 생성자 한 곳에만 둔다.
	UDataTable* const Table = GetDefault<AGatherableActor>()->GatherableTable;
	if (!IsValid(Table))
	{
		return 0;
	}

	int32 Spawned = 0;
	int32 InstancedTrees = 0;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FGatherableRow* const Row = reinterpret_cast<const FGatherableRow*>(Pair.Value);
		if (Row == nullptr || Row->DecorMeshPrefix.IsEmpty())
		{
			continue;
		}

		// 맵을 한 번만 훑고 자리를 모은다 — 훑는 중에 새 액터를 세우지 않는다.
		TArray<FTransform> Spots;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			if (MeshNameStartsWith(**It, Row->DecorMeshPrefix))
			{
				Spots.Add(FTransform(It->GetActorRotation(), It->GetActorLocation()));
			}
		}

		for (const FTransform& Spot : Spots)
		{
			if (AGatherableActor* const Tree = SpawnTreeAt(World, Table, Pair.Key, Spot))
			{
				Tree->FinishSpawning(Spot);
				++Spawned;
			}
		}

		// 숲 나무(폴리지 인스턴스)는 여기서 세우지 않는다 — 사람 곁에서만 선다(`SpawnAtInstancedTreesNear`).
		ForEachTreeInstances(World, Row->DecorMeshPrefix, [&InstancedTrees](UInstancedStaticMeshComponent& Instances)
		{
			InstancedTrees += Instances.GetInstanceCount();
		});
	}

	UE_LOG(LogSnarehaven, Log, TEXT("장식 나무 자리에 벨 수 있는 나무 %d 그루를 세웠다"), Spawned);
	UE_LOG(LogSnarehaven, Log, TEXT("벨 수 있는 숲 나무 인스턴스 %d 그루 (사람 곁에서만 채집물이 선다)"), InstancedTrees);
	return Spawned;
}

int32 AGatherableActor::SpawnAtInstancedTreesNear(UWorld* World, const FVector& Center, float GatherRangeCm)
{
	UDataTable* const Table = GetDefault<AGatherableActor>()->GatherableTable;
	if (!IsValid(World) || !IsValid(Table) || GatherRangeCm <= 0.f)
	{
		return 0;
	}

	const float SpawnRadiusCm = GatherRangeCm * InstancedTreeSpawnRangeScale;

	// 이미 선 채집물 자리 — 같은 나무에 둘을 세우지 않는다 (게스트가 자리로 같은 인스턴스를 찾는 것과 같은 기준).
	TArray<FVector> Taken;
	for (TActorIterator<AGatherableActor> It(World); It; ++It)
	{
		if (FVector::DistSquared(It->GetActorLocation(), Center) <= FMath::Square(SpawnRadiusCm + DecorMatchToleranceCm))
		{
			Taken.Add(It->GetActorLocation());
		}
	}

	// 다 모은 다음 세운다 — 훑는 중에 새 액터를 세우지 않는다.
	TArray<TPair<FName, FTransform>> Spots;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FGatherableRow* const Row = reinterpret_cast<const FGatherableRow*>(Pair.Value);
		if (Row == nullptr || Row->DecorMeshPrefix.IsEmpty())
		{
			continue;
		}

		ForEachTreeInstances(World, Row->DecorMeshPrefix, [&](UInstancedStaticMeshComponent& Instances)
		{
			for (const int32 Index : Instances.GetInstancesOverlappingSphere(Center, SpawnRadiusCm))
			{
				FTransform Transform;
				// 배율 0 은 벤 나무다 — 그 채집물이 이미 서 있다.
				if (!Instances.GetInstanceTransform(Index, Transform, true) || Transform.GetScale3D().IsNearlyZero()
					|| FVector::DistSquared(Transform.GetLocation(), Center) > FMath::Square(SpawnRadiusCm))
				{
					continue;
				}
				const FVector Where = Transform.GetLocation();
				if (Taken.ContainsByPredicate([&Where](const FVector& Other)
					{
						return FVector::DistSquared(Other, Where) <= FMath::Square(DecorMatchToleranceCm);
					}))
				{
					continue;
				}
				Taken.Add(Where);
				Spots.Emplace(Pair.Key, FTransform(Transform.GetRotation(), Where));
			}
		});
	}

	int32 Spawned = 0;
	for (const TPair<FName, FTransform>& Spot : Spots)
	{
		if (AGatherableActor* const Tree = SpawnTreeAt(World, Table, Spot.Key, Spot.Value))
		{
			Tree->ReleaseRadiusCm = GatherRangeCm * InstancedTreeReleaseRangeScale;
			Tree->FinishSpawning(Spot.Value);
			++Spawned;
		}
	}
	return Spawned;
}

void AGatherableActor::AdoptDecorIfAny()
{
	UWorld* const World = GetWorld();
	if (DecorMeshPrefix.IsEmpty() || World == nullptr)
	{
		return;
	}

	// BeginPlay 에서 한 번만 훑는다 (매 프레임 검색 금지 — engines/unreal.md 코드 규칙 2).
	const FVector Here = GetActorLocation();
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		if (FVector::DistSquared(It->GetActorLocation(), Here) <= FMath::Square(DecorMatchToleranceCm)
			&& MeshNameStartsWith(**It, DecorMeshPrefix))
		{
			AdoptedDecor = *It;
			break;
		}
	}

	// 액터 나무가 없으면 숲 나무 인스턴스다 — 호스트·게스트 모두 자기 맵에서 같은 자리의 인스턴스를 찾는다.
	if (!AdoptedDecor.IsValid() && !AdoptedInstances.IsValid())
	{
		ForEachTreeInstances(World, DecorMeshPrefix, [this, &Here](UInstancedStaticMeshComponent& Instances)
		{
			const int32 Index = AdoptedInstances.IsValid() ? INDEX_NONE : FindInstanceAt(Instances, Here);
			if (Index != INDEX_NONE && Instances.GetInstanceTransform(Index, AdoptedInstanceTransform, true))
			{
				AdoptedInstances = &Instances;
				AdoptedInstanceIndex = Index;
			}
		});
	}

	// 장식이 몸이면 자리표시 큐브는 안 보인다. 못 찾으면 큐브가 "메시 대기" 자리표시로 남는다.
	if ((AdoptedDecor.IsValid() || AdoptedInstances.IsValid()) && IsValid(Body))
	{
		Body->SetVisibility(false);
	}

	// 호스트가 사람 곁에 세운 숲 나무만 — 곁에 아무도 없으면 스스로 사라진다.
	if (HasAuthority() && AdoptedInstances.IsValid() && ReleaseRadiusCm > 0.f)
	{
		World->GetTimerManager().SetTimer(ReleaseTimerHandle, this, &AGatherableActor::ReleaseIfUnwatched,
			InstancedTreeReleaseCheckSeconds, true);
	}
}

void AGatherableActor::SetInstanceHidden(bool bHide)
{
	UInstancedStaticMeshComponent* const Instances = AdoptedInstances.Get();
	// 같은 상태로 다시 쓰지 않는다 — 폴리지는 인스턴스 하나만 바꿔도 렌더 상태를 다시 만든다.
	if (Instances == nullptr || bHide == bInstanceHidden)
	{
		return;
	}

	FTransform Shown = AdoptedInstanceTransform;
	if (bHide)
	{
		Shown.SetScale3D(FVector::ZeroVector);
	}
	// 배율 0 이면 엔진이 그 인스턴스의 충돌 몸을 걷어 낸다 — 벤 나무는 막지도 않는다.
	Instances->UpdateInstanceTransform(AdoptedInstanceIndex, Shown, true, true, true);
	bInstanceHidden = bHide;
}

void AGatherableActor::ReleaseIfUnwatched()
{
	if (bGathered)
	{
		return;
	}
	for (TActorIterator<ASnarehavenCharacter> It(GetWorld()); It; ++It)
	{
		if (FVector::DistSquared(It->GetActorLocation(), GetActorLocation()) <= FMath::Square(ReleaseRadiusCm))
		{
			return;
		}
	}
	Destroy();
}

void AGatherableActor::SetGatherableData(UDataTable* Table, FName RowName)
{
	GatherableTable = Table;
	GatherableRowName = RowName;
}

bool AGatherableActor::InitializeFromTable()
{
	if (!IsValid(GatherableTable) || GatherableRowName.IsNone())
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("%s: 채집물 테이블·행이 지정되지 않았다 — 채집할 수 없는 자리표시로 둔다"), *GetName());
		return false;
	}

	// FindRow 는 못 찾으면 로그를 남긴다. 우리가 부르는 곳을 알 수 있게 문맥 문자열을 넘긴다.
	const FGatherableRow* Row =
		GatherableTable->FindRow<FGatherableRow>(GatherableRowName, TEXT("AGatherableActor::InitializeFromTable"));
	if (Row == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 채집물 행 %s 를 못 찾았다"),
			*GetName(), *GatherableRowName.ToString());
		return false;
	}

	return InitializeFromRow(*Row);
}

bool AGatherableActor::InitializeFromRow(const FGatherableRow& Row)
{
	GatherableID = Row.GatherableID;

	// 진짜 메시가 행에 적혀 있으면 자리표시를 갈아 끼운다. 비어 있으면 자리표시 그대로 —
	// 데이터 무결성 검사가 "참조가 비었다" 경고로 세 준다.
	if (IsValid(Body) && !Row.MeshRef.IsNull())
	{
		if (UStaticMesh* Mesh = Row.MeshRef.LoadSynchronous())
		{
			Body->SetStaticMesh(Mesh);
		}
	}

	// 탑다운에서 한눈에 보이는 크기 — 몸(루트)을 표 높이에 맞춰 고르게 키운다 (DESIGN 3.3 더미 크기 표).
	const UStaticMesh* const BodyMesh = IsValid(Body) ? Body->GetStaticMesh() : nullptr;
	const float MeshHeightCm = BodyMesh != nullptr ? BodyMesh->GetBoundingBox().GetSize().Z : 0.f;
	if (Row.BodyHeightCm > 0.f && MeshHeightCm > KINDA_SMALL_NUMBER)
	{
		SetActorScale3D(FVector(Row.BodyHeightCm / MeshHeightCm));
	}

	// 줄기는 지급과 상관없이 선다 — 지급이 미정이어도 나무는 서 있으니 막아야 한다.
	// 몸 배율(맵 배치가 높이에 맞춰 키운다)을 따라가지 않게 월드 배율·위치로 둔다.
	bHasTrunk = IsValid(Trunk) && Row.TrunkCollisionCm.X > 0.f && Row.TrunkCollisionCm.Y > 0.f;
	if (bHasTrunk)
	{
		Trunk->SetUsingAbsoluteScale(true);
		Trunk->SetWorldScale3D(FVector(Row.TrunkCollisionCm.X, Row.TrunkCollisionCm.X, Row.TrunkCollisionCm.Y) / 100.f);
		Trunk->SetWorldLocation(GetActorLocation() + FVector(0.f, 0.f, Row.TrunkCollisionCm.Y * 0.5f));
		Trunk->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
		Trunk->SetCollisionEnabled(bGathered ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
	}

	// 미정 개수(-1)로 켜면 "0개 주는 채집물" 이 조용히 생긴다. 채우지 않고 경고만 남긴다
	// (Verification.md 7 · DataDriven.md 3 — 추천 숫자도 쓰지 않는다).
	if ((Row.GrantItemID.IsNone() && Row.GrantOptions.IsEmpty()) || Row.GrantCount <= 0)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("채집물 %s 의 지급이 아직 미정이다 (아이템=%s, 개수=%d) — 채집을 켜지 않는다"),
			*Row.GatherableID.ToString(), *Row.GrantItemID.ToString(), Row.GrantCount);
		GrantItemID = NAME_None;
		GrantCount = -1;
		return false;
	}

	// 후보가 있으면 첫 후보가 기본값이다 — 게스트 사본이 들고 있을 뿐, 실제 아이템은 아래 굴림이 정한다.
	GrantOptions = Row.GrantOptions;
	GrantItemID = Row.GrantItemID.IsNone() ? Row.GrantOptions[0].ItemID : Row.GrantItemID;
	GrantCountMin = Row.GrantCount;
	GrantCountMax = FMath::Max(Row.GrantCount, Row.GrantCountMax);
	ExtraItemID = Row.ExtraItemID;
	ExtraCountMax = Row.ExtraItemID.IsNone() ? 0 : FMath::Max(0, Row.ExtraCountMax);
	RespawnSeconds = FMath::Max(0.f, Row.RespawnSeconds);
	bKeepBodyWhenGathered = Row.bKeepBodyWhenGathered;
	GatheredVisualDelaySeconds = FMath::Max(0.f, Row.GatheredVisualDelaySeconds);
	RequiredToolItemID = Row.RequiredToolItemID;
	BonusToolItemID = Row.BonusToolItemID;
	AlsoToolItemIDs = Row.AlsoToolItemIDs;
	DecorMeshPrefix = Row.DecorMeshPrefix;
	HitsToFell = FMath::Max(0, Row.HitsToFell);
	RollGrantCount();
	// 다시 초기화하면 채집 흔적은 지운다 (맵 재사용·검사 대비). 게스트 사본은 호스트가 내려준
	// `bGathered` 를 믿는다 — 늦게 들어온 게스트가 벤 나무를 멀쩡하게 보면 안 된다.
	if (HasAuthority())
	{
		bGathered = false;
		HitsTaken = 0;
	}
	return true;
}

void AGatherableActor::RollGrantCount()
{
	// 굴림은 한 곳에서만 — 복제 나무면 호스트, 아니면 이 프로세스 (DESIGN 8.10 "호스트 굴림").
	// 게스트 사본은 최소값을 들고 있을 뿐 지급에 쓰이지 않는다 (게스트는 호스트에게 베기를 부탁한다).
	GrantCount = (HasAuthority() && GrantCountMax > GrantCountMin)
		? FMath::RandRange(GrantCountMin, GrantCountMax)
		: GrantCountMin;
	// 덤(나무의 수지)도 같은 자리에서 굴린다 — 게스트 사본은 0 이다(지급은 호스트 굴림만 쓴다).
	ExtraCount = HasAuthority() && ExtraCountMax > 0 ? FMath::RandRange(0, ExtraCountMax) : 0;

	// 채집 더미(DESIGN 13.4)는 무엇을 줄지도 같은 자리에서 굴린다 — 처음 설 때와 다시 날 때마다.
	int32 TotalWeight = 0;
	for (const FGatherGrantOption& Option : GrantOptions)
	{
		TotalWeight += FMath::Max(0, Option.Weight);
	}
	if (HasAuthority() && TotalWeight > 0)
	{
		GrantItemID = PickWeightedItem(GrantOptions, FMath::RandRange(0, TotalWeight - 1));
	}
}

FName AGatherableActor::PickWeightedItem(const TArray<FGatherGrantOption>& Options, int32 Roll)
{
	for (const FGatherGrantOption& Option : Options)
	{
		if (Option.Weight <= 0)
		{
			continue;
		}
		if (Roll < Option.Weight)
		{
			return Option.ItemID;
		}
		Roll -= Option.Weight;
	}
	return NAME_None;
}

bool AGatherableActor::CanGather() const
{
	return !bGathered && !GrantItemID.IsNone() && GrantCount > 0;
}

bool AGatherableActor::CanGatherWithTool(FName ToolItemID) const
{
	// 보너스 도구(전기톱)는 필요한 도구(손도끼) 대신 써도 된다 (DESIGN 13.5). 윗급 곡괭이도 (DESIGN 8.16).
	return CanGather() && (RequiredToolItemID.IsNone() || RequiredToolItemID == ToolItemID
		|| (!BonusToolItemID.IsNone() && BonusToolItemID == ToolItemID)
		|| (!ToolItemID.IsNone() && AlsoToolItemIDs.Contains(ToolItemID)));
}

bool AGatherableActor::TakeHit(const FVector& HitterLocation, bool bFellAtOnce)
{
	if (!CanGather())
	{
		return false;
	}
	const FVector Away = GetActorLocation() - HitterLocation;
	if (!Away.IsNearlyZero())
	{
		HitYawDeg = Away.Rotation().Yaw;
	}
	// 전동 톱은 손도끼처럼 여러 번 찍지 않고 한 번 대서 썰어 넘긴다 (사용자 2026-10-16 「전기톱은 금방 베야」).
	if (bFellAtOnce || HitsTaken + 1 >= HitsToFell)
	{
		return true;
	}

	++HitsTaken;
	UE_LOG(LogSnarehaven, Log, TEXT("나무 치기: %s %d/%d"), *GatherableID.ToString(), HitsTaken, HitsToFell);
	// 호스트(리슨 서버의 사람)도 흔들림을 본다 — 게스트는 `OnRep_HitsTaken` 에서.
	StartTilt(ETiltMode::Shake);
	return false;
}

void AGatherableActor::OnRep_HitsTaken()
{
	if (HitsTaken > 0 && !bGathered)
	{
		StartTilt(ETiltMode::Shake);
	}
}

bool AGatherableActor::Gather()
{
	if (!CanGather())
	{
		return false;
	}

	bGathered = true;

	UE_LOG(LogSnarehaven, Log, TEXT("채집: %s → %s %d개 (지급은 백엔드 몫)"),
		*GatherableID.ToString(), *GrantItemID.ToString(), GrantCount);

	SetHighlighted(false);

	// 나무(8.10)는 숨었다가 `RespawnSeconds` 뒤 다시 선다. 복제 나무면 `bGathered` 가 게스트에게 내려가 같이 숨는다.
	if (RespawnSeconds > 0.f)
	{
		ScheduleGatheredVisual();
		if (UWorld* const World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				RespawnTimerHandle, this, &AGatherableActor::Respawn, RespawnSeconds, false);
		}
		return true;
	}

	// 열매 덤불은 다시 나지 않아도 몸은 남는다 — 열매만 없어진 덤불로 선다 (레퍼런스, INBOX 결함 10-06 02:20).
	if (bKeepBodyWhenGathered)
	{
		ScheduleGatheredVisual();
		return true;
	}

	// 마른 나무는 사라지고 **리스폰하지 않는다** (DESIGN 3.3 감독 확정).
	// 부른 쪽이 `GetGrantItemID()` 를 읽을 수 있게, 파괴는 눈에서 지운 다음 한 박자 뒤가 아니라
	// 여기서 바로 한다 — 파괴돼도 UObject 는 이번 프레임 동안 유효해서 값을 읽는 데 지장이 없다.
	Destroy();
	return true;
}

void AGatherableActor::Respawn()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
	}

	RollGrantCount();
	bGathered = false;
	HitsTaken = 0;
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GatheredVisualTimerHandle);
	}
	StopTilt();
	ApplyGatheredVisual();
	UE_LOG(LogSnarehaven, Log, TEXT("다시 자람: %s (다음 %s %d개)"),
		*GatherableID.ToString(), *GrantItemID.ToString(), GrantCount);
}

void AGatherableActor::OnRep_Gathered()
{
	// 게스트도 같은 박자로 — 거둔 것은 늦춰 입히고, 다시 난 것은 바로 돌린다.
	if (bGathered)
	{
		ScheduleGatheredVisual();
		return;
	}
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GatheredVisualTimerHandle);
	}
	ApplyGatheredVisual();
}

void AGatherableActor::ScheduleGatheredVisual()
{
	UWorld* const World = GetWorld();
	// 여러 번 쳐야 쓰러지는 나무는 넘어가는 연출이 끝난 뒤에 숨는다 (DESIGN 8.10).
	if (World != nullptr && HitsToFell > 1)
	{
		StartTilt(ETiltMode::Fall);
		return;
	}
	if (World == nullptr || GatheredVisualDelaySeconds <= 0.f)
	{
		ApplyGatheredVisual();
		return;
	}
	World->GetTimerManager().SetTimer(
		GatheredVisualTimerHandle, this, &AGatherableActor::ApplyGatheredVisual, GatheredVisualDelaySeconds, false);
}

void AGatherableActor::StartTilt(ETiltMode Mode)
{
	UWorld* const World = GetWorld();
	// 넘어가는 중엔 흔들림이 끼어들지 않는다.
	if (World == nullptr || (TiltMode == ETiltMode::Fall && Mode == ETiltMode::Shake))
	{
		return;
	}
	TiltMode = Mode;
	TiltElapsedSeconds = 0.f;
	// 줄기 충돌은 넘어가기 시작할 때 끈다 — 눕는 나무에 사람이 걸리지 않게.
	if (Mode == ETiltMode::Fall && IsValid(Trunk))
	{
		Trunk->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	World->GetTimerManager().SetTimer(TiltTimerHandle, this, &AGatherableActor::TickTilt, TiltStepSeconds, true);
	TickTilt();
}

void AGatherableActor::TickTilt()
{
	const float Duration = TiltMode == ETiltMode::Fall ? FallSeconds : ShakeSeconds;
	// 먼저 한 칸 나아간다 — 친 그 프레임부터 기운 게 보이게.
	TiltElapsedSeconds += TiltStepSeconds;
	const float Alpha = FMath::Clamp(TiltElapsedSeconds / Duration, 0.f, 1.f);

	if (TiltMode == ETiltMode::Shake)
	{
		// 친 쪽 반대로 한 번 젖혔다가 되돌아온다 (반 사인).
		SetBodyTilt(ShakeAngleDeg * FMath::Sin(Alpha * PI));
	}
	else if (TiltMode == ETiltMode::Fall)
	{
		// 처음엔 천천히, 갈수록 빨리 — 무거운 나무가 넘어가는 박자 (제곱 가속).
		SetBodyTilt(FallAngleDeg * Alpha * Alpha);
	}

	if (Alpha >= 1.f)
	{
		const bool bWasFalling = TiltMode == ETiltMode::Fall;
		StopTilt();
		if (bWasFalling)
		{
			ApplyGatheredVisual();
		}
	}
}

void AGatherableActor::StopTilt()
{
	if (UWorld* const World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TiltTimerHandle);
	}
	TiltMode = ETiltMode::None;
	SetBodyTilt(0.f);
}

void AGatherableActor::SetBodyTilt(float AngleDeg)
{
	if (FMath::IsNearlyEqual(AngleDeg, CurrentTiltDeg))
	{
		return;
	}
	// 처음 기울일 때 원래 자세를 적어 둔다 — 되돌릴 때 같은 자리로.
	AActor* const Decor = AdoptedDecor.Get();
	if (FMath::IsNearlyZero(CurrentTiltDeg))
	{
		DecorRestRotation = IsValid(Decor) ? Decor->GetActorQuat() : FQuat::Identity;
		BodyRestRotation = GetActorQuat();
	}
	CurrentTiltDeg = AngleDeg;

	// 넘어가는 쪽(HitYawDeg)에 수직인 수평 축을 돈다 — 뿌리(원점)가 축이라 줄기 밑동은 제자리다.
	const FVector FallDirection = FRotator(0.f, HitYawDeg, 0.f).Vector();
	const FVector Axis = FVector::CrossProduct(FVector::UpVector, FallDirection).GetSafeNormal();
	const FQuat Tilt(Axis, FMath::DegreesToRadians(AngleDeg));

	if (UInstancedStaticMeshComponent* const Instances = AdoptedInstances.Get())
	{
		// 숨긴(배율 0) 인스턴스는 건드리지 않는다 — 벤 뒤 숨김이 이긴다.
		if (!bInstanceHidden)
		{
			FTransform Tilted = AdoptedInstanceTransform;
			Tilted.SetRotation(Tilt * AdoptedInstanceTransform.GetRotation());
			Instances->UpdateInstanceTransform(AdoptedInstanceIndex, Tilted, true, true, true);
		}
		return;
	}

	if (IsValid(Decor))
	{
		// 맵 장식은 정적(Static)으로 깔려 있어 그대로는 안 돈다 — 기울일 때만 움직이게 바꾼다.
		if (USceneComponent* const DecorRoot = Decor->GetRootComponent();
			IsValid(DecorRoot) && DecorRoot->Mobility != EComponentMobility::Movable && !FMath::IsNearlyZero(AngleDeg))
		{
			DecorRoot->SetMobility(EComponentMobility::Movable);
		}
		Decor->SetActorRotation(Tilt * DecorRestRotation, ETeleportType::TeleportPhysics);
		return;
	}

	SetActorRotation(Tilt * BodyRestRotation, ETeleportType::TeleportPhysics);
}

void AGatherableActor::SetFruitStripped(bool bStrip)
{
	if (!IsValid(Body))
	{
		return;
	}
	// 0번 칸은 잎이다(`make_tree_meshes.py` 베리 덤불 = ["Leaves_Bush", 열매 칸]). 덮개를 비우면 메시 원래 재질로 돌아간다.
	UMaterialInterface* const Leaves = bStrip ? Body->GetMaterial(0) : nullptr;
	for (int32 Index = 1; Index < Body->GetNumMaterials(); ++Index)
	{
		Body->SetMaterial(Index, Leaves);
	}
}

void AGatherableActor::ApplyGatheredVisual()
{
	if (IsValid(Trunk))
	{
		Trunk->SetCollisionEnabled(bHasTrunk && !bGathered ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}

	if (AdoptedInstances.IsValid())
	{
		SetInstanceHidden(bGathered);
		return;
	}

	if (AActor* const Decor = AdoptedDecor.Get())
	{
		// 벤 나무는 막지도 않는다 — 안 보이는데 부딪히면 버그처럼 읽힌다.
		Decor->SetActorHiddenInGame(bGathered);
		Decor->SetActorEnableCollision(!bGathered);
		return;
	}

	if (IsValid(Body))
	{
		if (bKeepBodyWhenGathered)
		{
			SetFruitStripped(bGathered);
			return;
		}
		// 몸에 붙인 것(광석 바위의 결정 덩이)도 같이 숨긴다 — 안 하면 바위가 무너져도 덩이만 떠 있다.
		Body->SetVisibility(!bGathered, true);
	}
}

void AGatherableActor::SetHighlighted(bool bHighlighted)
{
	if (!IsValid(Body))
	{
		return;
	}

	// 커스텀 뎁스 스텐실만 켜 둔다 — 실제 외곽선은 포스트프로세스 머티리얼 몫이고,
	// 그건 아트 단계에서 붙는다 (UMeleeAttackComponent 의 동물 하이라이트와 같은 방식).
	Body->SetRenderCustomDepth(bHighlighted);

	// 장식 나무가 몸이면 빛나야 할 것도 그 나무다.
	if (const AStaticMeshActor* const Decor = Cast<AStaticMeshActor>(AdoptedDecor.Get()))
	{
		if (UStaticMeshComponent* const DecorMesh = Decor->GetStaticMeshComponent())
		{
			DecorMesh->SetRenderCustomDepth(bHighlighted);
		}
	}
}
