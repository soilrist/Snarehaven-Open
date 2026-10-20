#include "World/DangerZoneActor.h"
#include "SnareCheckSwitch.h"
#include "Snarehaven.h"
#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Components/SceneComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Net/UnrealNetwork.h"
#include "SnarehavenCharacter.h"
#include "TimerManager.h"
#include "UI/HuntHudWidget.h"
#include "UObject/ConstructorHelpers.h"
#include "World/LootChestActor.h"
#include "World/SnareZoneRules.h"

#define LOCTEXT_NAMESPACE "DangerZone"

namespace
{
	/** 이 동물이 아직 구역 자리를 차지하고 있는가. 죽거나 잡힌 동물은 액터가 남아도 빈자리로 센다. */
	bool IsAnimalStanding(const AAnimalCharacter* Animal)
	{
		if (!IsValid(Animal))
		{
			return false;
		}

		const UAnimalStateComponent* State = Animal->GetAnimalState();
		return IsValid(State) && !State->IsFinished();
	}

	// 추락 지점 표식 배치 (DESIGN 8.1b "추락 지점 표식" 표 — 화면 표식이라 게임 규칙 숫자가 아니다.
	// `SnareMinimapHud` 의 배치 상수와 같은 방식). 링 반지름은 여기 없다 — 행의 `RadiusCm` 이다.
	constexpr int32 RingSegments = 24;
	constexpr float DebrisSpreadRatio = 0.3f;

	/** 서버 월드 시계. 게스트도 GameState 가 맞춰 둔 서버 시각을 읽는다 (게임스테이트 없는 검사 월드는 자기 시계). */
	double GetServerNow(const UWorld* World)
	{
		const AGameStateBase* GameState = World != nullptr ? World->GetGameState() : nullptr;
		return GameState != nullptr ? GameState->GetServerWorldTimeSeconds() : (World != nullptr ? World->GetTimeSeconds() : 0.0);
	}

	/** 점 바로 아래 바닥(WorldStatic 만 — 동물 캡슐에 걸리지 않게). 못 찾으면 그 점 그대로. */
	FVector ProjectToGround(const UWorld* World, const FVector& Point, float ReachCm)
	{
		FHitResult Hit;
		const FVector Up(0.f, 0.f, ReachCm);
		return World->LineTraceSingleByObjectType(Hit, Point + Up, Point - Up, FCollisionObjectQueryParams(ECC_WorldStatic),
			FCollisionQueryParams(SCENE_QUERY_STAT(WorldEventMarker), false)) ? Hit.ImpactPoint : Point;
	}
}

ADangerZoneActor::ADangerZoneActor()
{
	// 구역은 재생성 타이머로만 일한다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryActorTick.bCanEverTick = false;

	// 자리표시 몸을 두지 않는다 (AZonePortalActor 는 큐브를 뒀다): 반지름이 사람 키의 몇 배라
	// 무엇을 세워도 구역 안 동물을 가린다. 화면에 보여야 하는 건 구역이 아니라 **동물 밀도**다(M6-04c).
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// 맵에 놓인 상시 구역은 복제하지 않는다 — 게스트가 볼 것은 이 액터가 아니라 **세워진 동물**이고,
	// 동물은 M3-02 부터 스스로 복제된다 (DESIGN 4.1 권위 2단 표).
	// 서버가 도중에 세우는 월드 이벤트 구역만 `SpawnWorldEvent` 가 복제를 켠다 — 게스트에 알림을 띄우고
	// 수명 끝의 사라짐을 전하려면 액터가 게스트에도 있어야 한다 (M12-07).

	// C++ 전용 프로젝트라 블루프린트 없이 기본 표를 주려면 생성자 경로 참조뿐이다
	// (AZonePortalActor·AAnimalCharacter 와 같은 방식).
	static ConstructorHelpers::FObjectFinder<UDataTable> DangerZoneTableAsset(
		TEXT("/Game/Game/Data/World/DT_DangerZoneTable.DT_DangerZoneTable"));
	if (DangerZoneTableAsset.Succeeded())
	{
		DangerZoneTable = DangerZoneTableAsset.Object;
	}

	// 추락 지점 잔해·링 (M12-07) — 전부 이미 Content 에 있는 에셋이다. 표식은 이벤트 구역만 붙인다(BeginPlay).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CartMesh(TEXT("/Game/Game/Environment/Props/SM_Prop_Cart_01.SM_Prop_Cart_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlanksMesh(TEXT("/Game/Game/Environment/Props/SM_Prop_Planks_01.SM_Prop_Planks_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PolesMesh(TEXT("/Game/Game/Environment/Props/SM_Prop_Poles_01.SM_Prop_Poles_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> LogMesh(TEXT("/Game/Game/Environment/Nature/SM_Nature_Log_01.SM_Nature_Log_01"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> RockMesh(TEXT("/Game/Game/Environment/Nature/SM_Nature_Rock_01.SM_Nature_Rock_01"));
	for (UStaticMesh* Mesh : { CartMesh.Object, PlanksMesh.Object, PolesMesh.Object, LogMesh.Object, RockMesh.Object })
	{
		if (Mesh != nullptr)
		{
			DebrisMeshes.Add(Mesh);
		}
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FenceMesh(
		TEXT("/Game/Game/Environment/Furniture/FenceWood/SM_Furn_FenceWood_01.SM_Furn_FenceWood_01"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RedMaterial(
		TEXT("/Game/Game/Environment/Nature/MI_Nature_ColorRed.MI_Nature_ColorRed"));
	RingMesh = FenceMesh.Object;
	RingMaterial = RedMaterial.Object;
}

void ADangerZoneActor::BeginPlay()
{
	Super::BeginPlay();

	if (!InitializeFromTable())
	{
		return;
	}

	if (ZoneRow.LifetimeSeconds > 0)
	{
		// 월드 이벤트 구역이다 — 호스트와 게스트 모두 자기 화면에 잔해·링을 세우고 등장 알림을 띄운다 (M12-07).
		BuildCrashSiteVisuals();
		NotifyWorldEventAppeared();
	}

	if (!HasAuthority())
	{
		// 게스트는 동물을 세우지 않는다 — 호스트가 세운 것이 복제로 온다 (CLAUDE.md 8).
		return;
	}

	StartLifetime();

	// 상시 구역 옆 상자 1·2 (DESIGN 8.10 「8.1 옆 2개」) — 이벤트 구역(수명 있음)엔 안 세운다. 맵에 한 벌뿐이다.
	if (ZoneRow.LifetimeSeconds <= 0)
	{
		ALootChestActor::SpawnBesideZone(GetWorld(), GetActorLocation(), ZoneRow.RadiusCm);
	}

	// 첫 채움은 로그로 남긴다 — 헤드리스 3층은 화면밖에 증거가 없어서(M6-04c 캡처) 눈대중이 된다.
	// 재생성 로그(`HandleRespawnTick`)와 같은 꼴이라 한 줄로 "구역이 켜졌고 몇 마리 섰는지" 를 읽는다.
	UE_LOG(LogSnarehaven, Log, TEXT("%s: 위험구역 첫 채움 - %d마리 (구역 정원 %d - 반지름 %.0fcm)"),
		*GetName(), RefillZone(), ZoneRow.AnimalCount, ZoneRow.RadiusCm);

	// ponytail: 타이머 하나가 구역 전체를 본다 — 한 마리가 사라진 **그 순간**부터 재는 게 아니라
	// 주기마다 빈자리를 채운다(실제 지연 0 ~ RespawnSeconds). 마리마다 정확히 재야 하면
	// 동물의 `OnStateChanged` 를 듣고 개별 타이머를 거는 쪽으로 올린다.
	GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &ADangerZoneActor::HandleRespawnTick,
		static_cast<float>(ZoneRow.RespawnSeconds), true);
}

bool ADangerZoneActor::InitializeFromTable()
{
	if (!IsValid(DangerZoneTable))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 위험구역 표가 없다 - 구역을 켜지 않는다"), *GetName());
		return false;
	}

	const FDangerZoneRow* Row =
		DangerZoneTable->FindRow<FDangerZoneRow>(DangerZoneRowName, TEXT("ADangerZoneActor::InitializeFromTable"));
	if (Row == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 위험구역 행 '%s' 가 표에 없다 - 구역을 켜지 않는다"),
			*GetName(), *DangerZoneRowName.ToString());
		return false;
	}

	if (!SetDangerZoneRow(*Row))
	{
		return false;
	}

	// #0204 표가 이긴다 — 맵에 놓인 자리가 행의 칸 밖이면 칸 가운데로 옮긴다(상자도 옮긴 자리를 따른다).
	const UDataTable* const ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	if (const FZoneRow* const Zone = SnareZones::FindZoneRow(ZoneTable, Row->ZoneID))
	{
		// 새 지도 존은 정사각형이 아니다 — 틀 가운데가 바다일 수 있어 존 안 무작위 점으로 옮긴다
		const FVector Here = GetActorLocation();
		FVector2D Inside;
		if (!SnareZones::IsInsideRegion(this, *Zone, Here))
		{
			SetActorLocation(Zone->FieldCode > 0 && SnareZones::RandomPointInRegion(this, *Zone, 0.f, Inside)
				? FVector(Inside, Here.Z) : FitToZone(*Zone, Here));
		}
	}

	// 선 칸이 이 종을 못 받으면(선공종이 평원·시작점에) 켜지 않는다 — DESIGN 8.7 · 3.1 시작점 안전.
	const FZoneRow* const Region = SnareZones::FindRegionAt(this, GetActorLocation());
	if (Region != nullptr && !CanStandInRegion(GetDefault<AAnimalCharacter>()->GetAnimalTable(), Row->AnimalRowName, *Region))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: %s 는 선공종이라 %s 칸에 못 선다 (DESIGN 8.7) - 구역을 켜지 않는다"),
			*GetName(), *Row->AnimalRowName.ToString(), *Region->ZoneID.ToString());
		bZoneActive = false;
		return false;
	}
	return true;
}

bool ADangerZoneActor::CanStandInRegion(const UDataTable* AnimalTable, FName AnimalRowName, const FZoneRow& Region)
{
	const FAnimalRow* const Animal = AnimalTable != nullptr
		? AnimalTable->FindRow<FAnimalRow>(AnimalRowName, TEXT("ADangerZoneActor::CanStandInRegion"), false) : nullptr;
	return Animal != nullptr
		&& (Animal->BehaviorKind != EAnimalBehaviorKind::Aggressive || Animal->HabitatZoneID == Region.ZoneID);
}

FVector ADangerZoneActor::FitToZone(const FZoneRow& Zone, const FVector& Location)
{
	if (!SnareZones::IsRegionRow(Zone) || SnareZones::IsInsideRegion(Zone, Location))
	{
		return Location;
	}
	return FVector(Zone.RegionCenterCm.X, Zone.RegionCenterCm.Y, Location.Z);
}

void ADangerZoneActor::SetDangerZoneData(UDataTable* Table, FName RowName)
{
	DangerZoneTable = Table;
	DangerZoneRowName = RowName;
}

bool ADangerZoneActor::SetDangerZoneRow(const FDangerZoneRow& Row)
{
	ZoneRow = Row;
	bZoneActive = false;

	// 미정 숫자로는 구역을 켜지 않는다 (Verification.md 7) — 음수 하나면 동물이 0마리로 서거나
	// 타이머가 매 프레임 도는데, 둘 다 "조용히 잘못 도는" 모양이다.
	if (Row.RadiusCm <= 0.f || Row.AnimalCount <= 0 || Row.RespawnSeconds <= 0)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("%s: 위험구역 수치가 미정이다 (반지름 %.1f - 마리수 %d - 재생성 %d초) - 구역을 켜지 않는다"),
			*GetName(), Row.RadiusCm, Row.AnimalCount, Row.RespawnSeconds);
		return false;
	}

	if (Row.AnimalRowName.IsNone())
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 위험구역에 세울 종이 안 적혀 있다 - 구역을 켜지 않는다"), *GetName());
		return false;
	}

	bZoneActive = true;
	return true;
}

int32 ADangerZoneActor::GetLiveAnimalCount() const
{
	int32 Standing = 0;
	for (const TObjectPtr<AAnimalCharacter>& Animal : SpawnedAnimals)
	{
		if (IsAnimalStanding(Animal))
		{
			++Standing;
		}
	}

	return Standing;
}

int32 ADangerZoneActor::RefillZone()
{
	if (!bZoneActive || !HasAuthority())
	{
		// 클라이언트에서 불러도 아무 일도 일어나지 않는다 — 스폰 권위는 호스트뿐이다 (DESIGN 4.1).
		return 0;
	}

	// 슬롯 = 자리. 슬롯 i 는 늘 같은 자리라 재생성해도 구역 모양이 흐트러지지 않는다.
	if (SpawnedAnimals.Num() != ZoneRow.AnimalCount)
	{
		SpawnedAnimals.SetNum(ZoneRow.AnimalCount);
	}

	int32 SpawnedNow = 0;
	for (int32 Index = 0; Index < SpawnedAnimals.Num(); ++Index)
	{
		if (IsAnimalStanding(SpawnedAnimals[Index]))
		{
			continue;
		}

		if (AAnimalCharacter* Animal = SpawnOneAnimal(Index))
		{
			SpawnedAnimals[Index] = Animal;
			++SpawnedNow;
		}
	}

	// M21-07f 주간 우두머리 — 서 있는 우두머리가 없으면 한 마리 더 (DESIGN 10.6 「우두머리 재생성」 = 구역 재생성).
	if (HasBoss() && GetStandingBoss() == nullptr)
	{
		if (AAnimalCharacter* Boss = SpawnBoss())
		{
			BossAnimal = Boss;
			++SpawnedNow;
		}
	}

	return SpawnedNow;
}

AAnimalCharacter* ADangerZoneActor::GetStandingBoss() const
{
	return IsAnimalStanding(BossAnimal) ? BossAnimal.Get() : nullptr;
}

AAnimalCharacter* ADangerZoneActor::SpawnBoss()
{
	// 가운데 자리 — 슬롯 나선의 첫 자리(반지름 1/정원)보다 안쪽이라 겹치지 않는다. 몸 배율은 스폰 변환에 싣는다:
	// 게스트도 처음 받는 스폰 정보에 배율이 있어 같은 크기로 선다.
	const FTransform SpawnTransform(GetActorRotation(), GetActorLocation(), FVector(ZoneRow.BossBodyScale));
	AAnimalCharacter* const Boss = SpawnAnimalAt(SpawnTransform);
	UAnimalStateComponent* const State = IsValid(Boss) ? Boss->GetAnimalState() : nullptr;
	// 월드가 아직 시작 전이면(BeginPlay 전 — 2층 검사 월드) 몸이 행을 안 읽었다. 배수는 행 체력에 곱하므로 먼저 읽힌다.
	if (IsValid(State) && State->GetMaxHP() <= 0.f)
	{
		Boss->InitializeFromTable();
	}
	if (!IsValid(State) || !State->MakeBoss(ZoneRow.BossHPMultiplier))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 우두머리를 못 세웠다 (종 %s)"), *GetName(), *ZoneRow.AnimalRowName.ToString());
		if (IsValid(Boss))
		{
			Boss->Destroy();
		}
		return nullptr;
	}

	UE_LOG(LogSnarehaven, Log, TEXT("%s: 우두머리 섰다 - %s 체력 %.0f (x%.1f) - 몸 x%.2f"),
		*GetName(), *ZoneRow.AnimalRowName.ToString(), State->GetMaxHP(), ZoneRow.BossHPMultiplier, ZoneRow.BossBodyScale);
	return Boss;
}

void ADangerZoneActor::HandleRespawnTick()
{
	const int32 SpawnedNow = RefillZone();
	if (SpawnedNow > 0)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 위험구역 재생성 - %d마리 (구역 정원 %d)"),
			*GetName(), SpawnedNow, ZoneRow.AnimalCount);
	}
}

AAnimalCharacter* ADangerZoneActor::SpawnOneAnimal(int32 Index)
{
	const FTransform SpawnTransform(GetActorRotation(),
		GetActorLocation() + MakeSpawnOffset(ZoneRow.RadiusCm, Index, ZoneRow.AnimalCount));
	AAnimalCharacter* const Animal = SpawnAnimalAt(SpawnTransform);
	if (Animal == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 위험구역 %d번 자리에 동물을 못 세웠다"), *GetName(), Index);
	}
	return Animal;
}

AAnimalCharacter* ADangerZoneActor::SpawnAnimalAt(const FTransform& SpawnTransform)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	// **Deferred 다**: `SpawnActor` 는 곧바로 BeginPlay 를 부르고, 동물의 BeginPlay 가 그 자리에서
	// `InitializeFromTable()` 을 한다 — 종(행 이름)을 그 전에 넣지 않으면 기본 종으로 굳는다.
	AAnimalCharacter* Animal = World->SpawnActorDeferred<AAnimalCharacter>(
		AAnimalCharacter::StaticClass(), SpawnTransform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (Animal == nullptr)
	{
		return nullptr;
	}

	// 동물 표 경로는 `AAnimalCharacter` 생성자 한 곳에만 둔다 — 여기서 또 찾으면 사본이 둘이 된다
	// (`AZonePortalActor::FindCurrentZoneID` 가 포탈 기본값의 표를 빌리는 것과 같은 방식).
	Animal->SetAnimalData(GetDefault<AAnimalCharacter>()->GetAnimalTable(), ZoneRow.AnimalRowName);
	Animal->FinishSpawning(SpawnTransform);
	return Animal;
}

FVector ADangerZoneActor::MakeSpawnOffset(float RadiusCm, int32 Index, int32 Count)
{
	if (Count <= 0 || RadiusCm <= 0.f)
	{
		return FVector::ZeroVector;
	}

	// 나선: 거리는 안쪽 자리부터 바깥 테두리까지 고르게, 각도는 한 바퀴를 마리수로 나눈다.
	// 새 숫자를 만들지 않으려고 둘 다 **행 값과 인덱스로만** 만든다 (DataDriven.md 3).
	const float Ratio = static_cast<float>(Index + 1) / static_cast<float>(Count);
	const float AngleRad = UE_TWO_PI * static_cast<float>(Index) / static_cast<float>(Count);
	const float DistanceCm = RadiusCm * Ratio;
	return FVector(DistanceCm * FMath::Cos(AngleRad), DistanceCm * FMath::Sin(AngleRad), 0.f);
}

void ADangerZoneActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ADangerZoneActor, DangerZoneRowName);
	DOREPLIFETIME(ADangerZoneActor, ExpireServerSeconds);
}

bool ADangerZoneActor::StartLifetime()
{
	if (!bZoneActive || !HasAuthority() || ZoneRow.LifetimeSeconds <= 0)
	{
		// 상시 구역(8.1)은 수명이 없다 — 0 이 "사라지지 않는다" 는 뜻이다.
		return false;
	}

	GetWorldTimerManager().SetTimer(LifetimeTimerHandle, this, &ADangerZoneActor::ExpireZone,
		static_cast<float>(ZoneRow.LifetimeSeconds), false);
	ExpireServerSeconds = GetServerNow(GetWorld()) + ZoneRow.LifetimeSeconds;
	return true;
}

float ADangerZoneActor::GetLifetimeRemaining() const
{
	if (ExpireServerSeconds <= 0.0)
	{
		return -1.f;
	}
	return static_cast<float>(FMath::Max(ExpireServerSeconds - GetServerNow(GetWorld()), 0.0));
}

const ADangerZoneActor* ADangerZoneActor::FindWorldEvent(const UWorld* World)
{
	if (World == nullptr)
	{
		return nullptr;
	}

	// ponytail: 첫 하나만 본다 — 수명(600초) < 등장 주기(1800초)라 이벤트는 한 번에 하나다(DESIGN 8.1b).
	// 여럿이 겹치게 표를 바꾸면 표식을 목록으로 올린다.
	for (TActorIterator<ADangerZoneActor> It(World); It; ++It)
	{
		if (It->IsZoneActive() && It->GetZoneRow().LifetimeSeconds > 0 && !It->IsActorBeingDestroyed())
		{
			return *It;
		}
	}
	return nullptr;
}

void ADangerZoneActor::BuildCrashSiteVisuals()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	// 표식은 보기만 한다 — 충돌을 끄면 동물·플레이어 길을 막지 않고, 바닥 트레이스에도 안 걸린다.
	auto AddPiece = [this](UStaticMesh* Mesh, UMaterialInterface* Material, const FTransform& WorldTransform)
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(this);
		Piece->SetMobility(EComponentMobility::Movable);
		Piece->SetStaticMesh(Mesh);
		if (Material != nullptr)
		{
			for (int32 Slot = 0; Slot < Piece->GetNumMaterials(); ++Slot)
			{
				Piece->SetMaterial(Slot, Material);
			}
		}
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetupAttachment(RootComponent);
		Piece->RegisterComponent();
		Piece->SetWorldTransform(WorldTransform);
	};

	const FVector Center = GetActorLocation();
	const float Reach = ZoneRow.RadiusCm;

	// 잔해 더미: 조각마다 가운데 둘레에 나선으로 (동물 자리와 같은 `MakeSpawnOffset`, 반경만 줄여서).
	for (int32 Index = 0; Index < DebrisMeshes.Num(); ++Index)
	{
		const FVector Spot = Center + MakeSpawnOffset(ZoneRow.RadiusCm * DebrisSpreadRatio, Index, DebrisMeshes.Num());
		const float Yaw = 360.f * Index / DebrisMeshes.Num();
		AddPiece(DebrisMeshes[Index], nullptr, FTransform(FRotator(0.f, Yaw, 0.f), ProjectToGround(World, Spot, Reach)));
	}

	// 링: 반지름 = 행의 RadiusCm. 울타리 조각을 원 둘레에 잇는다 (조각 길이 = 둘레 / 조각 수, 메시 길이는 경계 상자에서 읽는다).
	if (RingMesh == nullptr)
	{
		return;
	}
	const FBoxSphereBounds MeshBounds = RingMesh->GetBounds();
	const float MeshLengthCm = FMath::Max(MeshBounds.BoxExtent.X * 2.f, 1.f);
	const float MeshBaseOffsetCm = MeshBounds.BoxExtent.Z - MeshBounds.Origin.Z;   // 메시 밑면을 바닥에 맞춘다
	const float SegmentLength = UE_TWO_PI * ZoneRow.RadiusCm / RingSegments;
	const FVector Scale(SegmentLength / MeshLengthCm, 1.f, 1.f);
	UE_LOG(LogSnarehaven, Log, TEXT("%s: 추락 지점 링 - 울타리 %s 길이 %.1fcm 폭 %.1fcm 높이 %.1fcm, 조각 %d개 x %.1fcm (늘림 %.2f배)"),
		*GetName(), *RingMesh->GetName(), MeshLengthCm, MeshBounds.BoxExtent.Y * 2.f, MeshBounds.BoxExtent.Z * 2.f, RingSegments, SegmentLength, Scale.X);
	for (int32 Index = 0; Index < RingSegments; ++Index)
	{
		const float AngleRad = UE_TWO_PI * Index / RingSegments;
		const FVector Spot = Center + FVector(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.f) * ZoneRow.RadiusCm;
		const FVector Ground = ProjectToGround(World, Spot, Reach) + FVector(0.f, 0.f, MeshBaseOffsetCm);
		AddPiece(RingMesh, RingMaterial,
			FTransform(FRotator(0.f, FMath::RadiansToDegrees(AngleRad) + 90.f, 0.f), Ground, Scale));
	}
}

void ADangerZoneActor::ExpireZone()
{
	if (!HasAuthority())
	{
		return;
	}

	// 서 있는 동물만 치운다 — 죽거나 잡힌 동물은 이미 드롭·포획 지급이 대기열에 올라갔고
	// (`QueuePendingGrant` → CloudScript `ClaimLoot`) 액터는 숨겨진 채라 건드릴 까닭이 없다.
	int32 Removed = 0;
	for (const TObjectPtr<AAnimalCharacter>& Animal : SpawnedAnimals)
	{
		if (IsAnimalStanding(Animal))
		{
			Animal->Destroy();
			++Removed;
		}
	}

	UE_LOG(LogSnarehaven, Log, TEXT("%s: 월드 이벤트 수명 끝 - 서 있던 %d마리를 치우고 구역을 없앤다 (수명 %d초)"),
		*GetName(), Removed, ZoneRow.LifetimeSeconds);
	SpawnedAnimals.Reset();
	bZoneActive = false;
	Destroy();
}

FTimerHandle ADangerZoneActor::ScheduleWorldEvent(UWorld* World, UDataTable* Table, FName RowName)
{
	FTimerHandle Handle;
	if (World == nullptr || World->GetNetMode() == NM_Client || !IsValid(Table))
	{
		return Handle;
	}

	const FDangerZoneRow* Row = Table->FindRow<FDangerZoneRow>(RowName, TEXT("ADangerZoneActor::ScheduleWorldEvent"));
	if (Row == nullptr || Row->AppearIntervalSeconds <= 0 || Row->LifetimeSeconds <= 0)
	{
		return Handle;
	}

	const TWeakObjectPtr<UDataTable> WeakTable(Table);
	World->GetTimerManager().SetTimer(Handle,
		FTimerDelegate::CreateWeakLambda(World, [World, WeakTable, RowName]()
		{
			SpawnWorldEvent(World, WeakTable.Get(), RowName);
		}),
		static_cast<float>(Row->AppearIntervalSeconds), true);
	return Handle;
}

ADangerZoneActor* ADangerZoneActor::SpawnWorldEvent(UWorld* World, UDataTable* Table, FName RowName)
{
	if (World == nullptr || World->GetNetMode() == NM_Client || !IsValid(Table))
	{
		return nullptr;
	}

	const FDangerZoneRow* Row = Table->FindRow<FDangerZoneRow>(RowName, TEXT("ADangerZoneActor::SpawnWorldEvent"));
	const UDataTable* ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	if (Row == nullptr || !IsValid(ZoneTable))
	{
		return nullptr;
	}

	// 이 맵의 성격 존 칸들(평원·설원·숲·늪) — 새 좌표를 만들지 않고 `DT_ZoneTable` 의 칸을 빌린다.
	const FName LevelName(*UWorld::RemovePIEPrefix(World->GetMapName()));
	// 선공종이면 제 서식 칸만 후보다 — 평원(시작점)에 선공 무리를 세우지 않는다 (DESIGN 8.7 · #0204).
	const UDataTable* const AnimalTable = GetDefault<AAnimalCharacter>()->GetAnimalTable();
	TArray<const FZoneRow*> Regions;
	ZoneTable->ForeachRow<FZoneRow>(TEXT("ADangerZoneActor::SpawnWorldEvent"),
		[&Regions, LevelName, AnimalTable, Row](const FName&, const FZoneRow& Zone)
		{
			if (SnareZones::IsRegionRow(Zone) && Zone.LevelName == LevelName
				&& CanStandInRegion(AnimalTable, Row->AnimalRowName, Zone))
			{
				Regions.Add(&Zone);
			}
		});

	// 칸을 무작위 순서로 돌며 칸마다 무작위 한 점을 바닥에 쏜다 — 바다·절벽이라 못 서면 다음 칸.
	// 시도 횟수를 새 숫자로 두지 않으려고 "칸 수만큼" 이다.
	const AAnimalCharacter* AnimalCDO = GetDefault<AAnimalCharacter>();
	const int32 First = Regions.Num() > 0 ? FMath::RandRange(0, Regions.Num() - 1) : 0;
	for (int32 Step = 0; Step < Regions.Num(); ++Step)
	{
		const FZoneRow& Zone = *Regions[(First + Step) % Regions.Num()];
		// 구역 원이 칸 밖으로 안 나가게 반변에서 구역 반지름을 뺀다. 새 지도는 존 칸 안 점만 받는다.
		FVector2D Point;
		if (!SnareZones::RandomPointInRegion(World, Zone, Row->RadiusCm, Point))
		{
			continue;
		}
		const FVector Column(Point, 0.f);

		// 트레이스 길이는 칸 반변을 빌린다 (`AAnimalSpreadActor::ProjectCellToGround` 와 같은 방식).
		FHitResult Hit;
		const FVector Up(0.f, 0.f, Zone.RegionHalfExtentCm);
		if (!World->LineTraceSingleByChannel(Hit, Column + Up, Column - Up, ECC_WorldStatic,
				FCollisionQueryParams(SCENE_QUERY_STAT(WorldEventGround), false)))
		{
			continue;
		}

		if (Hit.ImpactNormal.Z < AnimalCDO->GetCharacterMovement()->GetWalkableFloorZ())
		{
			continue;
		}

		// 동물은 구역 액터 높이에 서므로 캡슐 절반만큼 띄운다 — 바닥에 묻혀 튀어 오르지 않게.
		const FTransform SpawnTransform(Hit.ImpactPoint +
			FVector(0.f, 0.f, AnimalCDO->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
		ADangerZoneActor* EventZone = World->SpawnActorDeferred<ADangerZoneActor>(
			ADangerZoneActor::StaticClass(), SpawnTransform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (EventZone == nullptr)
		{
			return nullptr;
		}

		EventZone->SetDangerZoneData(Table, RowName);
		EventZone->SetReplicates(true);
		// 등장 알림은 거리와 상관없이 모두에게 가야 한다 — 이벤트 액터는 몸이 없어 대역폭도 거의 안 든다.
		EventZone->bAlwaysRelevant = true;
		EventZone->FinishSpawning(SpawnTransform);

		UE_LOG(LogSnarehaven, Log, TEXT("%s: 월드 이벤트 등장 - 행 %s - 칸 %s - 위치 %s - 수명 %d초 - 동물 %d마리"),
			*EventZone->GetName(), *RowName.ToString(), *Zone.ZoneID.ToString(), *SpawnTransform.GetLocation().ToString(),
			Row->LifetimeSeconds, EventZone->GetLiveAnimalCount());
		return EventZone;
	}

	UE_LOG(LogSnarehaven, Warning, TEXT("월드 이벤트 %s: 이 맵(%s)의 칸 %d개에서 설 바닥을 못 찾았다 - 이번 등장은 건너뛴다"),
		*RowName.ToString(), *LevelName.ToString(), Regions.Num());
	return nullptr;
}

int32 ADangerZoneActor::ScheduleWorldEvents(UWorld* World)
{
	UDataTable* Table = GetDefault<ADangerZoneActor>()->DangerZoneTable;
	const UDataTable* ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	if (World == nullptr || World->GetNetMode() == NM_Client || !IsValid(Table) || !IsValid(ZoneTable))
	{
		return 0;
	}

	const FName LevelName(*UWorld::RemovePIEPrefix(World->GetMapName()));
	const bool bSpawnNow = SnareCheckSwitch::Param(TEXT("SnareWorldEventNow"));
	int32 Scheduled = 0;
	for (const FName& RowName : Table->GetRowNames())
	{
		const FDangerZoneRow* Row = Table->FindRow<FDangerZoneRow>(RowName, TEXT("ADangerZoneActor::ScheduleWorldEvents"));
		const FZoneRow* Zone = Row != nullptr ? SnareZones::FindZoneRow(ZoneTable, Row->ZoneID) : nullptr;
		if (Zone == nullptr || Zone->LevelName != LevelName)
		{
			// 이 맵(목장·로비)에 속한 구역이 아니다.
			continue;
		}

		if (!ScheduleWorldEvent(World, Table, RowName).IsValid())
		{
			continue;
		}

		++Scheduled;
		UE_LOG(LogSnarehaven, Log, TEXT("월드 이벤트 %s: %d초마다 등장 예약 (%s)"),
			*RowName.ToString(), Row->AppearIntervalSeconds, *LevelName.ToString());
		if (bSpawnNow)
		{
			// 3층 캡처용 — 첫 주기(수십 분)를 기다리지 않는다. 다음 틱에 세운다: GameMode BeginPlay 도중엔
			// 월드가 아직 BeginPlay 를 돌리는 중이고 플레이어 HUD 도 안 떠서 등장 알림이 빠진다.
			const TWeakObjectPtr<UDataTable> WeakTable(Table);
			World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(World,
				[World, WeakTable, RowName]() { SpawnWorldEvent(World, WeakTable.Get(), RowName); }));
		}
	}

	return Scheduled;
}

void ADangerZoneActor::NotifyWorldEventAppeared() const
{
	const ASnarehavenCharacter* Player = Cast<ASnarehavenCharacter>(UGameplayStatics::GetPlayerCharacter(this, 0));
	UHuntHudWidget* Hud = Player != nullptr ? Player->GetHuntHud() : nullptr;
	if (Hud == nullptr)
	{
		return;
	}

	const FZoneRow* Region = SnareZones::FindRegionAt(this, GetActorLocation());
	Hud->ShowNoticeBanner(FText::Format(LOCTEXT("WorldEventAppeared", "월드 이벤트 - {0}에 추락 지점이 나타났다"),
		Region != nullptr ? Region->DisplayName : LOCTEXT("WorldEventSomewhere", "어딘가")));
}

#undef LOCTEXT_NAMESPACE
