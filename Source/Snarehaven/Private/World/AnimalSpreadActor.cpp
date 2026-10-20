#include "World/AnimalSpreadActor.h"
#include "Snarehaven.h"
#include "Animals/AnimalCharacter.h"
#include "Data/AnimalRow.h"
#include "Animals/AnimalStateComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SeaActor.h"
#include "World/SnareZoneRules.h"
#include "Backend/SnareStory.h"
#include "SnareCheckSwitch.h"

namespace
{
	// 시작점 순한 동물 자리를 훑는 방향 수 — 30° 간격 (DESIGN 3.1 「시작점 순한 동물 거리」).
	constexpr int32 StarterDirectionCount = 12;

	// 격자 칸 사이 거리는 계산상 최소 간격과 **똑같다**. 실수 계산에서 1199.9999 가 나오면
	// 멀쩡한 칸이 전부 탈락해 한 마리도 안 서므로, 1cm 여유를 두고 뺀다 (설계 숫자 아님).
	constexpr float SpacingToleranceCm = 1.0f;

	/** 이 동물이 아직 자리를 차지하고 있는가. 죽거나 잡힌 동물은 액터가 남아도 빈자리로 센다. */
	bool IsAnimalStanding(const AAnimalCharacter* Animal)
	{
		if (!IsValid(Animal))
		{
			return false;
		}

		const UAnimalStateComponent* State = Animal->GetAnimalState();
		return IsValid(State) && !State->IsFinished();
	}
}

AAnimalSpreadActor::AAnimalSpreadActor()
{
	// 재등장 타이머로만 일한다 — 매 프레임 볼 것이 없다 (engines/unreal.md 코드 규칙 2).
	PrimaryActorTick.bCanEverTick = false;

	// 자리표시 몸을 두지 않는다 (ADangerZoneActor 와 같은 이유) — 화면에 보여야 하는 것은
	// 스포너가 아니라 흩어진 동물이다.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// C++ 전용 프로젝트라 블루프린트 없이 기본 표를 주려면 생성자 경로 참조뿐이다.
	static ConstructorHelpers::FObjectFinder<UDataTable> SpreadTableAsset(
		TEXT("/Game/Game/Data/World/DT_AnimalSpreadTable.DT_AnimalSpreadTable"));
	if (SpreadTableAsset.Succeeded())
	{
		SpreadTable = SpreadTableAsset.Object;
	}
}

void AAnimalSpreadActor::BeginPlay()
{
	Super::BeginPlay();

	if (!InitializeFromTable() || !HasAuthority())
	{
		// 게스트는 세우지 않는다 — 호스트가 세운 것이 복제로 온다 (CLAUDE.md 8).
		return;
	}

	// 첫 채움을 한 줄로 남긴다. 좌표는 `SpawnOneAnimal` 이 마리마다 따로 찍는다 —
	// 헤드리스 3층은 "흩어졌나" 를 캡처 한 장과 그 좌표 목록으로만 볼 수 있다.
	// 종 목록도 같이 남긴다 — "돼지·소·양이 안 섰나" 는 마리 수가 아니라 이 줄로 센다.
	const TArray<FName> SpawnPool = MakeSpawnPool(GetDefault<AAnimalCharacter>()->GetAnimalTable(),
		SpreadRow.ZoneID, AAnimalCharacter::ShouldStandPlaceholderSpecies());
	UE_LOG(LogSnarehaven, Log,
		TEXT("%s: 동물 분산 첫 채움 - %d마리 (정원 %d · 종 %d가지 · 자리표시 %s · 영역 반변 %.0fcm · 최소 간격 %.0fcm · 시작점 금지 %.0fcm)"),
		*GetName(), RefillArea(), SpreadRow.AnimalCount, SpawnPool.Num(),
		AAnimalCharacter::ShouldStandPlaceholderSpecies() ? TEXT("세움") : TEXT("뺌"),
		SpreadRow.AreaHalfExtentCm, SpreadRow.MinSpacingCm, SpreadRow.PlayerStartClearCm);

	// 시작점 순한 동물 (M21-01) — 금지 반경 때문에 첫 동물을 찾느라 처음 5분이 걷기로 새지 않게.
	RefillStoryAnimal(IsStoryAnimalWanted());

	ChooseStarterSpots();
	if (StarterLocations.Num() > 0)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 시작점 순한 동물 첫 채움 - %d마리 (자리 %d · 정한 수 %d)"),
			*GetName(), RefillStarters(), StarterLocations.Num(), SpreadRow.StarterAnimalCount);
	}

	// ponytail: 타이머 하나가 영역 전체를 본다 — 한 마리가 사라진 그 순간이 아니라 주기마다
	// 빈자리를 채운다(실제 지연 0 ~ RespawnSeconds). 마리마다 정확히 90초를 재야 하면
	// 동물의 상태 변화를 듣고 개별 타이머를 거는 쪽으로 올린다 (ADangerZoneActor 와 같은 한계).
	GetWorldTimerManager().SetTimer(RespawnTimerHandle, this, &AAnimalSpreadActor::HandleRespawnTick,
		static_cast<float>(SpreadRow.RespawnSeconds), true);
}

bool AAnimalSpreadActor::InitializeFromTable()
{
	if (!IsValid(SpreadTable))
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 동물 분산 표가 없다 - 세우지 않는다"), *GetName());
		return false;
	}

	const FAnimalSpreadRow* Row =
		SpreadTable->FindRow<FAnimalSpreadRow>(SpreadRowName, TEXT("AAnimalSpreadActor::InitializeFromTable"));
	if (Row == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 동물 분산 행 '%s' 가 표에 없다 - 세우지 않는다"),
			*GetName(), *SpreadRowName.ToString());
		return false;
	}

	// 존이 맵 안 칸(M12-00 평원·설원·숲)이면 격자를 **그 칸 안으로** 옮기고 줄인다 — 종이 자기 존 경계
	// 밖에 서지 않게(DESIGN 8.7 "서식 존 배정"). 배치 스크립트가 액터를 어디 놓았든 존 표가 이긴다.
	FAnimalSpreadRow Fitted = *Row;
	FVector2D Center(GetActorLocation());
	if (const FZoneRow* Zone = SnareZones::FindZoneRow(LoadObject<UDataTable>(nullptr, SnareZones::TablePath), Row->ZoneID))
	{
		// 새 지도 존(FieldCode)은 정사각형이 아니다 — 배치 스크립트가 놓은 자리·반변을 쓰고, 칸마다 존 안인지 거른다(CollectFreeCells)
		if (!(Zone->FieldCode > 0 && ASeaActor::FindSea(this) != nullptr && ASeaActor::FindSea(this)->HasField())
			&& FitToZone(*Zone, Fitted, Center))
		{
			SetActorLocation(FVector(Center, GetActorLocation().Z));
		}
	}

	return SetSpreadRow(Fitted);
}

bool AAnimalSpreadActor::FitToZone(const FZoneRow& Zone, FAnimalSpreadRow& InOutRow, FVector2D& OutCenter)
{
	if (!SnareZones::IsRegionRow(Zone))
	{
		return false;
	}
	OutCenter = Zone.RegionCenterCm;
	InOutRow.AreaHalfExtentCm = FMath::Min(InOutRow.AreaHalfExtentCm, Zone.RegionHalfExtentCm);
	return true;
}

void AAnimalSpreadActor::SetSpreadData(UDataTable* Table, FName RowName)
{
	SpreadTable = Table;
	SpreadRowName = RowName;
}

bool AAnimalSpreadActor::SetSpreadRow(const FAnimalSpreadRow& Row)
{
	SpreadRow = Row;
	bSpreadActive = false;

	// 미정 숫자로는 켜지 않는다 (Verification.md 7) — 음수 하나면 0마리가 서거나 타이머가
	// 매 프레임 도는데, 둘 다 "조용히 잘못 도는" 모양이다.
	if (Row.AreaHalfExtentCm <= 0.f || Row.AnimalCount <= 0 || Row.MinSpacingCm <= 0.f
		|| Row.PlayerStartClearCm < 0.f || Row.RespawnSeconds <= 0
		|| Row.StarterAnimalCount < 0 || Row.StarterRingCm < 0.f)
	{
		UE_LOG(LogSnarehaven, Warning,
			TEXT("%s: 동물 분산 수치가 미정이다 (반변 %.1f · 마리수 %d · 간격 %.1f · 금지 반경 %.1f · 재등장 %d초 · 시작점 동물 %d · 거리 %.1f) - 세우지 않는다"),
			*GetName(), Row.AreaHalfExtentCm, Row.AnimalCount, Row.MinSpacingCm,
			Row.PlayerStartClearCm, Row.RespawnSeconds, Row.StarterAnimalCount, Row.StarterRingCm);
		return false;
	}

	bSpreadActive = true;
	return true;
}

int32 AAnimalSpreadActor::GetLiveAnimalCount() const
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

int32 AAnimalSpreadActor::RefillArea()
{
	if (!bSpreadActive || !HasAuthority())
	{
		// 클라이언트에서 불러도 아무 일도 안 일어난다 — 스폰 권위는 호스트뿐이다 (DESIGN 4.1).
		return 0;
	}

	if (SpawnedAnimals.Num() != SpreadRow.AnimalCount)
	{
		SpawnedAnimals.SetNum(SpreadRow.AnimalCount);
	}

	// 빈 칸 목록을 **한 번만** 만들고 하나씩 빼 쓴다. 칸끼리는 이미 최소 간격 이상 떨어져 있어서
	// (격자) 한 칸에 세워도 남은 칸의 자격이 바뀌지 않는다.
	TArray<FVector2D> FreeCells = CollectFreeCells();

	int32 SpawnedNow = 0;
	for (int32 Index = 0; Index < SpawnedAnimals.Num(); ++Index)
	{
		if (IsAnimalStanding(SpawnedAnimals[Index]))
		{
			continue;
		}

		// 자리는 **무작위**다 — 잡히거나 죽은 동물이 같은 칸에 다시 서면 "다른 지점에서 나온다"
		// (지시 ★4)가 아니게 된다. 칸이 정원보다 많아서 다시 설 자리가 남는다.
		while (FreeCells.Num() > 0)
		{
			const int32 Pick = FMath::RandRange(0, FreeCells.Num() - 1);
			const FVector2D Cell = FreeCells[Pick];
			FreeCells.RemoveAtSwap(Pick);

			FVector SpawnLocation = FVector::ZeroVector;
			if (!ProjectCellToGround(Cell, SpawnLocation))
			{
				// 걸을 수 있는 바닥이 없는 칸이다 (지시 ★4 "내비메시 위만" 의 대역 — 아래 주석).
				continue;
			}

			if (AAnimalCharacter* Animal = SpawnOneAnimal(Index, SpawnLocation))
			{
				SpawnedAnimals[Index] = Animal;
				++SpawnedNow;
			}
			break;
		}
	}

	if (SpawnedNow < SpreadRow.AnimalCount && GetLiveAnimalCount() < SpreadRow.AnimalCount)
	{
		// 조용히 모자라게 서지 않는다 — 칸이 모자라면 맵이 좁거나 간격이 큰 것이고, 그건 숫자 문제다.
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 동물 분산 - 정원 %d 중 %d마리만 섰다 (빈 칸이 모자라다)"),
			*GetName(), SpreadRow.AnimalCount, GetLiveAnimalCount());
	}

	return SpawnedNow;
}

void AAnimalSpreadActor::HandleRespawnTick()
{
	// 지붕 위 등 땅 아닌 바닥에 올라간 동물부터 내린다 — 그 다음 빈 칸 계산이 옮긴 자리를 본다.
	const int32 ReturnedNow = ReturnStrandedAnimals();
	if (ReturnedNow > 0)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 땅 아닌 바닥의 동물 %d마리를 땅으로 내렸다"), *GetName(), ReturnedNow);
	}

	// 시작점 동물부터 — 그 자리는 정해져 있어 흩는 동물이 먼저 차지하면 안 된다(아래 빈 칸 거름이 그 둘을 피한다).
	const int32 StartersNow = RefillStarters();
	if (StartersNow > 0)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 시작점 순한 동물 재등장 - %d마리 (같은 자리)"), *GetName(), StartersNow);
	}

	if (RefillStoryAnimal(IsStoryAnimalWanted()))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 이야기 동물 재등장"), *GetName());
	}

	const int32 SpawnedNow = RefillArea();
	if (SpawnedNow > 0)
	{
		UE_LOG(LogSnarehaven, Log, TEXT("%s: 동물 분산 재등장 - %d마리 (정원 %d)"),
			*GetName(), SpawnedNow, SpreadRow.AnimalCount);
	}
}

int32 AAnimalSpreadActor::GetLiveStarterCount() const
{
	int32 Standing = 0;
	for (const TObjectPtr<AAnimalCharacter>& Animal : StarterAnimals)
	{
		if (IsAnimalStanding(Animal))
		{
			++Standing;
		}
	}

	return Standing;
}

void AAnimalSpreadActor::ChooseStarterSpots()
{
	StarterLocations.Reset();
	UWorld* World = GetWorld();
	if (World == nullptr || SpreadRow.StarterAnimalCount <= 0)
	{
		return;
	}

	const FZoneRow* const Zone = SnareZones::FindZoneRow(LoadObject<UDataTable>(nullptr, SnareZones::TablePath), SpreadRow.ZoneID);
	const FVector Origin = GetActorLocation();
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		const FVector Start = It->GetActorLocation();
		if (Zone != nullptr && !SnareZones::IsInsideRegion(this, *Zone, Start))
		{
			continue;
		}

		// 같은 존 스포너가 여럿이면(새 지도는 존마다 둘) 시작점에 가장 가까운 하나만 맡는다 — 둘 다 세우면 두 배가 선다.
		// 다른 스포너의 BeginPlay 가 아직일 수 있어 그 행은 표에서 직접 읽는다.
		const double MyDistance = FVector::DistSquared2D(Origin, Start);
		bool bOtherOwns = false;
		for (TActorIterator<AAnimalSpreadActor> Other(World); Other; ++Other)
		{
			if (*Other == this || !IsValid(Other->SpreadTable))
			{
				continue;
			}
			const FAnimalSpreadRow* OtherRow = Other->SpreadTable->FindRow<FAnimalSpreadRow>(
				Other->SpreadRowName, TEXT("AAnimalSpreadActor::ChooseStarterSpots"), /*bWarnIfRowMissing=*/false);
			if (OtherRow == nullptr || OtherRow->ZoneID != SpreadRow.ZoneID || OtherRow->StarterAnimalCount <= 0)
			{
				continue;
			}
			const double OtherDistance = FVector::DistSquared2D(Other->GetActorLocation(), Start);
			if (OtherDistance < MyDistance || (OtherDistance == MyDistance && Other->GetName() < GetName()))
			{
				bOtherOwns = true;
				break;
			}
		}
		if (bOtherOwns)
		{
			continue;
		}

		// 뭍인 자리만 — 시작 거점은 해안 7m 안쪽이라 앞이 바다일 수 있다(DESIGN 3.1).
		TArray<FVector> Grounds;
		const TArray<FVector2D> Spots = MakeStarterSpots(FVector2D(Start), It->GetActorRotation().Yaw,
			SpreadRow.StarterAnimalCount, SpreadRow.StarterRingCm, SpreadRow.MinSpacingCm,
			[this, Zone, &Start, &Origin](const FVector2D& Spot)
			{
				if (Zone != nullptr && !SnareZones::IsInsideRegion(this, *Zone, FVector(Spot, Start.Z)))
				{
					return false;
				}
				FVector Ground;
				return ProjectCellToGround(Spot - FVector2D(Origin), Ground);
			});
		for (const FVector2D& Spot : Spots)
		{
			FVector Ground;
			if (ProjectCellToGround(Spot - FVector2D(Origin), Ground))
			{
				StarterLocations.Add(Ground);
				// 3층이 "시작점 15m 안" 을 캡처가 아니라 이 숫자로 잰다 (INBOX 상시 — 수치는 로그로).
				UE_LOG(LogSnarehaven, Log, TEXT("%s: 시작점 순한 동물 자리 (%.0f, %.0f) · 시작점 %s 에서 %.0fcm"),
					*GetName(), Ground.X, Ground.Y, *It->GetName(), FVector::Dist2D(Ground, Start));
			}
		}
		if (Spots.Num() < SpreadRow.StarterAnimalCount)
		{
			UE_LOG(LogSnarehaven, Warning, TEXT("%s: 시작점 %s 둘레 뭍 자리가 %d곳뿐이다 (정한 수 %d)"),
				*GetName(), *It->GetName(), Spots.Num(), SpreadRow.StarterAnimalCount);
		}
	}
}

int32 AAnimalSpreadActor::RefillStarters()
{
	if (!bSpreadActive || !HasAuthority() || StarterLocations.Num() == 0)
	{
		return 0;
	}

	const TArray<FName> Pool = MakeStarterPool(GetDefault<AAnimalCharacter>()->GetAnimalTable(), SpreadRow.ZoneID);
	if (Pool.Num() == 0)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 존 %s 에 도주만 하는 종이 없어 시작점 동물을 못 세운다"),
			*GetName(), *SpreadRow.ZoneID.ToString());
		return 0;
	}

	StarterAnimals.SetNum(StarterLocations.Num());
	int32 SpawnedNow = 0;
	for (int32 Index = 0; Index < StarterLocations.Num(); ++Index)
	{
		if (IsAnimalStanding(StarterAnimals[Index]))
		{
			continue;
		}
		if (AAnimalCharacter* Animal = SpawnAnimalOfKind(Pool[Index % Pool.Num()], StarterLocations[Index]))
		{
			StarterAnimals[Index] = Animal;
			++SpawnedNow;
			UE_LOG(LogSnarehaven, Log, TEXT("%s: 시작점 순한 동물 스폰 - %d번 %s 자리 (%.0f, %.0f, %.0f)"),
				*GetName(), Index, *Pool[Index % Pool.Num()].ToString(), StarterLocations[Index].X,
				StarterLocations[Index].Y, StarterLocations[Index].Z);
		}
	}

	return SpawnedNow;
}

TArray<FVector2D> AAnimalSpreadActor::MakeStarterSpots(const FVector2D& Start, float StartYawDeg, int32 Count,
	float RingCm, float MinSpacingCm, TFunctionRef<bool(const FVector2D&)> IsUsable)
{
	TArray<FVector2D> Spots;
	if (Count <= 0 || RingCm <= 0.f)
	{
		return Spots;
	}

	const float StepDeg = 360.f / static_cast<float>(StarterDirectionCount);
	for (int32 Step = 0; Step < StarterDirectionCount && Spots.Num() < Count; ++Step)
	{
		// 앞쪽부터 좌우로 번갈아 벌린다: 0, +1, −1, +2, −2 … +6(=뒤). 깨어나 바라보는 쪽에 먼저 선다.
		const int32 Turn = (Step % 2 == 1) ? (Step + 1) / 2 : -(Step / 2);
		const float YawRad = FMath::DegreesToRadians(StartYawDeg + StepDeg * static_cast<float>(Turn));
		const FVector2D Spot = Start + RingCm * FVector2D(FMath::Cos(YawRad), FMath::Sin(YawRad));
		if (!IsUsable(Spot))
		{
			continue;
		}

		// 둘 사이도 흩는 동물과 같은 최소 간격 — 하나를 쫓다 다른 하나가 섞이지 않게.
		const bool bTooClose = Spots.ContainsByPredicate([&Spot, MinSpacingCm](const FVector2D& Taken)
		{
			return FVector2D::Distance(Taken, Spot) < MinSpacingCm - SpacingToleranceCm;
		});
		if (!bTooClose)
		{
			Spots.Add(Spot);
		}
	}

	return Spots;
}

TArray<FName> AAnimalSpreadActor::MakeStarterPool(const UDataTable* AnimalTable, FName ZoneID)
{
	TArray<FName> Pool = MakeSpawnPool(AnimalTable, ZoneID, /*bIncludePlaceholder=*/false);
	Pool.RemoveAll([AnimalTable](const FName& RowName)
	{
		// 종 이름이 아니라 행의 행동 칸으로 가른다 (PROMPT ③-12) — 평원에 순한 종이 늘면 저절로 섞인다.
		const FAnimalRow* Row = AnimalTable->FindRow<FAnimalRow>(RowName, FString(), /*bWarnIfRowMissing=*/false);
		return Row == nullptr || Row->BehaviorKind != EAnimalBehaviorKind::Flee;
	});
	return Pool;
}

TArray<FVector2D> AAnimalSpreadActor::CollectPlayerStartCenters() const
{
	TArray<FVector2D> Centers;

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return Centers;
	}

	// 시작점 좌표를 배치 스크립트와 이 코드 양쪽에 적지 않는다 — 맵에 선 `APlayerStart` 가 원본이다.
	const FVector Origin = GetActorLocation();
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		const FVector Location = It->GetActorLocation() - Origin;
		Centers.Add(FVector2D(Location.X, Location.Y));
	}

	return Centers;
}

TArray<FVector2D> AAnimalSpreadActor::CollectFreeCells() const
{
	const TArray<FVector2D> Cells = MakeSpreadCells(SpreadRow.AreaHalfExtentCm, SpreadRow.MinSpacingCm,
		SpreadRow.PlayerStartClearCm, CollectPlayerStartCenters());

	// 서 있는 동물의 **지금 자리**로 거른다 (도망치면 움직이니까 처음 선 칸이 아니다).
	// 위험구역(8.1)·무대 자리표시 동물은 일부러 안 본다 — 위험구역은 밀도를 높이는 곳이고,
	// 그 규칙은 그대로 둔다는 것이 지시 ★4 의 단서다.
	const FVector Origin = GetActorLocation();
	const FZoneRow* const Zone = SnareZones::FindZoneRow(LoadObject<UDataTable>(nullptr, SnareZones::TablePath), SpreadRow.ZoneID);
	TArray<FVector2D> FreeCells;
	for (const FVector2D& Cell : Cells)
	{
		// 새 지도 — 제 존 뭍 칸만 (바다·이웃 존에 서지 않는다). 옛 정사각 존은 FitToZone 이 이미 칸 안으로 맞췄다.
		if (Zone != nullptr && Zone->FieldCode > 0 && !SnareZones::IsInsideRegion(this, *Zone, Origin + FVector(Cell, 0.0)))
		{
			continue;
		}
		bool bTaken = false;
		// 시작점 동물도 자리를 차지한다 — 금지 반경 바로 밖 칸이 시작점 동물과 1200 안일 수 있다.
		TArray<const AAnimalCharacter*> Standing;
		for (const TObjectPtr<AAnimalCharacter>& Animal : SpawnedAnimals)
		{
			Standing.Add(Animal);
		}
		for (const TObjectPtr<AAnimalCharacter>& Animal : StarterAnimals)
		{
			Standing.Add(Animal);
		}
		for (const AAnimalCharacter* Animal : Standing)
		{
			if (!IsAnimalStanding(Animal))
			{
				continue;
			}

			const FVector Location = Animal->GetActorLocation() - Origin;
			if (FVector2D::Distance(FVector2D(Location.X, Location.Y), Cell)
				< SpreadRow.MinSpacingCm - SpacingToleranceCm)
			{
				bTaken = true;
				break;
			}
		}

		if (!bTaken)
		{
			FreeCells.Add(Cell);
		}
	}

	return FreeCells;
}

bool AAnimalSpreadActor::ProjectCellToGround(const FVector2D& Cell, FVector& OutLocation) const
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	// **내비메시가 아니라 바닥 트레이스다.** 이 프로젝트엔 `NavMeshBoundsVolume` 도 AIController 도
	// 없고(동물은 `AddMovementInput` 으로만 움직인다, DESIGN 3.1), 없는 내비메시에 투영하면
	// 한 칸도 안 남는다. 대신 "걸을 수 있는 바닥" 을 캐릭터 이동이 쓰는 것과 **같은 기준**
	// (`GetWalkableFloorZ`)으로 본다 — 지시 ★4 의 뜻(걸을 수 있는 바닥 위만)은 이걸로 지킨다.
	const FVector Origin = GetActorLocation();
	const FVector Column(Origin.X + Cell.X, Origin.Y + Cell.Y, Origin.Z);

	// 트레이스 길이는 영역 반변을 그대로 쓴다 — 새 숫자를 만들지 않으려고 행 값을 빌린다.
	const FVector Start = Column + FVector(0.f, 0.f, SpreadRow.AreaHalfExtentCm);
	const FVector End = Column - FVector(0.f, 0.f, SpreadRow.AreaHalfExtentCm);

	FVector Impact;
	if (!FindAnimalGround(World, Start, End, this, Impact))
	{
		return false;
	}

	// 캡슐 절반만큼 띄워 세운다 — 바닥에 묻힌 채로 시작하면 첫 프레임에 튀어 오른다.
	const float HalfHeight = GetDefault<AAnimalCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	OutLocation = Impact + FVector(0.f, 0.f, HalfHeight);
	return true;
}

bool AAnimalSpreadActor::FindAnimalGround(const UWorld* World, const FVector& Top, const FVector& Bottom,
	const AActor* Ignore, FVector& OutImpact)
{
	if (World == nullptr)
	{
		return false;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(AnimalSpreadGround), false, Ignore);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_WorldStatic, Params))
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = GetDefault<AAnimalCharacter>()->GetCharacterMovement();
	if (Movement != nullptr && Hit.ImpactNormal.Z < Movement->GetWalkableFloorZ())
	{
		// 바위 옆면·나무 줄기처럼 설 수 없는 면이다.
		return false;
	}

	// 오두막 지붕(위에서 처음 닿는 면)·울타리 윗면 — 걸을 수는 있어도 땅이 아니다.
	if (!IsAnimalGroundFloor(Hit.GetComponent()))
	{
		return false;
	}

	OutImpact = Hit.ImpactPoint;
	return true;
}

bool AAnimalSpreadActor::IsAnimalGroundFloor(const UPrimitiveComponent* Floor)
{
	if (Floor == nullptr)
	{
		return false;
	}

	// 월드 정렬 상자 반폭 = 배회 반경이면 가로·세로 폭 = 배회 지름. 사냥터 땅 타일은 128m, 오두막 지붕은 수 m.
	const FVector Extent = Floor->Bounds.BoxExtent;
	const float MinHalf = AAnimalCharacter::GetWanderRadiusCm();
	return Extent.X >= MinHalf && Extent.Y >= MinHalf;
}

bool AAnimalSpreadActor::IsStrandedOffGround(const AAnimalCharacter* Animal)
{
	if (!IsAnimalStanding(Animal) || Animal->GetAnimalState()->GetState() != EAnimalState::Idle)
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Animal->GetCharacterMovement();
	return Movement != nullptr && Movement->IsMovingOnGround() && Movement->CurrentFloor.bBlockingHit
		&& !IsAnimalGroundFloor(Movement->CurrentFloor.HitResult.GetComponent());
}

bool AAnimalSpreadActor::ReturnToGroundIfStranded(AAnimalCharacter* Animal, const FVector& GroundLocation)
{
	if (!IsStrandedOffGround(Animal))
	{
		return false;
	}

	const UCharacterMovementComponent* Movement = Animal->GetCharacterMovement();
	UE_LOG(LogSnarehaven, Log, TEXT("%s: 땅 아닌 바닥(%s)에 서 있어 땅으로 내린다 %s -> %s"), *Animal->GetName(),
		*GetNameSafe(Movement->CurrentFloor.HitResult.GetComponent()), *Animal->GetActorLocation().ToCompactString(),
		*GroundLocation.ToCompactString());
	Animal->SetActorLocation(GroundLocation, false, nullptr, ETeleportType::TeleportPhysics);
	Animal->ResetWanderHome();
	return true;
}

int32 AAnimalSpreadActor::ReturnStrandedAnimals()
{
	if (!bSpreadActive || !HasAuthority())
	{
		return 0;
	}

	int32 Returned = 0;
	// 시작점 동물은 제 자리로 — 그 자리는 BeginPlay 에 땅 검사를 거쳤다.
	for (int32 Index = 0; Index < StarterAnimals.Num() && Index < StarterLocations.Num(); ++Index)
	{
		Returned += ReturnToGroundIfStranded(StarterAnimals[Index], StarterLocations[Index]) ? 1 : 0;
	}

	// 흩은 동물은 빈 칸 하나를 골라 그 땅으로 (재등장과 같은 고르기).
	TArray<FVector2D> FreeCells;
	bool bCellsCollected = false;
	for (const TObjectPtr<AAnimalCharacter>& Animal : SpawnedAnimals)
	{
		if (!IsStrandedOffGround(Animal))
		{
			continue;
		}
		if (!bCellsCollected)
		{
			FreeCells = CollectFreeCells();
			bCellsCollected = true;
		}
		while (FreeCells.Num() > 0)
		{
			const int32 Pick = FMath::RandRange(0, FreeCells.Num() - 1);
			const FVector2D Cell = FreeCells[Pick];
			FreeCells.RemoveAtSwap(Pick);
			FVector Ground;
			if (ProjectCellToGround(Cell, Ground))
			{
				Returned += ReturnToGroundIfStranded(Animal, Ground) ? 1 : 0;
				break;
			}
		}
	}

	return Returned;
}

FName AAnimalSpreadActor::FindStorySpecies(const UDataTable* AnimalTable, FName ZoneID)
{
	if (!IsValid(AnimalTable))
	{
		return NAME_None;
	}

	// 이름이 아니라 행의 칸으로 가른다 (PROMPT ③-12). 이름순 첫 행.
	TArray<FName> Found;
	for (const FName& RowName : AnimalTable->GetRowNames())
	{
		const FAnimalRow* Row = AnimalTable->FindRow<FAnimalRow>(RowName, FString(), /*bWarnIfRowMissing=*/false);
		if (Row != nullptr && Row->bStorySpecies && Row->HabitatZoneID == ZoneID)
		{
			Found.Add(RowName);
		}
	}
	Found.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return Found.Num() > 0 ? Found[0] : NAME_None;
}

bool AAnimalSpreadActor::HasStoryAnimalStanding() const
{
	return IsAnimalStanding(StoryAnimal);
}

bool AAnimalSpreadActor::IsStoryAnimalWanted() const
{
	return SnareCheckSwitch::Param(TEXT("SnareStageFogbuck")) || SnareStory::ShouldStandFogbuck(SnareStory::GetKnownCurrentStep());
}

bool AAnimalSpreadActor::RefillStoryAnimal(bool bWanted)
{
	if (!bWanted || !bSpreadActive || !HasAuthority() || HasStoryAnimalStanding())
	{
		return false;
	}

	const FName StoryRow = FindStorySpecies(GetDefault<AAnimalCharacter>()->GetAnimalTable(), SpreadRow.ZoneID);
	if (StoryRow.IsNone())
	{
		return false;
	}

	TArray<FVector2D> FreeCells = CollectFreeCells();
	while (FreeCells.Num() > 0)
	{
		const int32 Pick = FMath::RandRange(0, FreeCells.Num() - 1);
		const FVector2D Cell = FreeCells[Pick];
		FreeCells.RemoveAtSwap(Pick);

		FVector SpawnLocation = FVector::ZeroVector;
		if (!ProjectCellToGround(Cell, SpawnLocation))
		{
			continue;
		}

		StoryAnimal = SpawnAnimalOfKind(StoryRow, SpawnLocation);
		if (StoryAnimal != nullptr)
		{
			UE_LOG(LogSnarehaven, Log, TEXT("%s: 이야기 동물 스폰 - %s 자리 (%.0f, %.0f, %.0f)"),
				*GetName(), *StoryRow.ToString(), SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z);
			return true;
		}
		break;
	}
	return false;
}

TArray<FName> AAnimalSpreadActor::MakeSpawnPool(const UDataTable* AnimalTable, FName ZoneID, bool bIncludePlaceholder)
{
	TArray<FName> RowNames;
	if (!IsValid(AnimalTable))
	{
		return RowNames;
	}

	for (const FName& RowName : AnimalTable->GetRowNames())
	{
		// 자리표시 종(돼지·소·양)은 기본 플레이에서 빠진다 — **이름이 아니라 행의 칸으로 가른다**
		// (사용자 지시 2026-09-23 16:55 ② · PROMPT ③-12). 종이 더 들어오고 빠져도 코드는 그대로다.
		const FAnimalRow* Row = AnimalTable->FindRow<FAnimalRow>(RowName, FString(), /*bWarnIfRowMissing=*/false);
		if (Row == nullptr || Row->bStorySpecies || (Row->bPlaceholderSpecies && !bIncludePlaceholder))
		{
			continue;
		}

		// 서식 존으로 한 번 더 거른다 (M5-07e · DESIGN 8.7 "서식 존 배정") — 존이 늘면 행의 칸만 바뀐다.
		if (Row->HabitatZoneID != ZoneID)
		{
			continue;
		}

		RowNames.Add(RowName);
	}

	// 이름순으로 고정한다 — 표를 다시 만들 때 행 순서가 흔들려도 같은 자리에 같은 종이 서게.
	RowNames.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return RowNames;
}

AAnimalCharacter* AAnimalSpreadActor::SpawnOneAnimal(int32 SlotIndex, const FVector& Location)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	// 종은 이 존에 사는 행(`HabitatZoneID`)을 이름순으로 돌려 쓴다 — 종·존이 늘면 코드가 아니라 표가 는다.
	const TArray<FName> AnimalRowNames = MakeSpawnPool(GetDefault<AAnimalCharacter>()->GetAnimalTable(),
		SpreadRow.ZoneID, AAnimalCharacter::ShouldStandPlaceholderSpecies());
	if (AnimalRowNames.Num() == 0)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 동물 표에 세울 종이 없다 (자리표시 포함 %s)"),
			*GetName(), AAnimalCharacter::ShouldStandPlaceholderSpecies() ? TEXT("예") : TEXT("아니오"));
		return nullptr;
	}
	const FName AnimalRowName = AnimalRowNames[SlotIndex % AnimalRowNames.Num()];

	AAnimalCharacter* Animal = SpawnAnimalOfKind(AnimalRowName, Location);
	if (Animal == nullptr)
	{
		UE_LOG(LogSnarehaven, Warning, TEXT("%s: 동물 분산 %d번 자리에 못 세웠다"), *GetName(), SlotIndex);
		return nullptr;
	}

	// 좌표를 마리마다 남긴다 — 3층 검증이 "스폰 좌표 16개" 를 이 줄로 센다 (지시 ★4).
	UE_LOG(LogSnarehaven, Log, TEXT("%s: 동물 분산 스폰 - %d번 %s 자리 (%.0f, %.0f, %.0f)"),
		*GetName(), SlotIndex, *AnimalRowName.ToString(), Location.X, Location.Y, Location.Z);
	return Animal;
}

AAnimalCharacter* AAnimalSpreadActor::SpawnAnimalOfKind(FName AnimalRowName, const FVector& Location)
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return nullptr;
	}

	const FTransform SpawnTransform(GetActorRotation(), Location);

	// **Deferred 다**: `SpawnActor` 는 곧바로 BeginPlay 를 부르고, 동물의 BeginPlay 가 그 자리에서
	// 표를 읽는다 — 종을 그 전에 넣지 않으면 기본 종으로 굳는다 (ADangerZoneActor 와 같은 함정).
	AAnimalCharacter* Animal = World->SpawnActorDeferred<AAnimalCharacter>(
		AAnimalCharacter::StaticClass(), SpawnTransform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (Animal == nullptr)
	{
		return nullptr;
	}

	Animal->SetAnimalData(GetDefault<AAnimalCharacter>()->GetAnimalTable(), AnimalRowName);
	Animal->FinishSpawning(SpawnTransform);
	return Animal;
}

TArray<FVector2D> AAnimalSpreadActor::MakeSpreadCells(float AreaHalfExtentCm, float MinSpacingCm,
	float PlayerStartClearCm, const TArray<FVector2D>& ClearCenters)
{
	TArray<FVector2D> Cells;
	if (AreaHalfExtentCm <= 0.f || MinSpacingCm <= 0.f)
	{
		return Cells;
	}

	// 한 축에 들어가는 칸 수 = 영역 지름을 간격으로 나눈 것 + 1. 격자를 영역 가운데에 맞춰
	// 깔아서 칸이 한쪽으로 쏠리지 않게 한다. **새 숫자는 하나도 없다** — 행 값 둘로만 만든다.
	const int32 PerAxis = FMath::FloorToInt(2.f * AreaHalfExtentCm / MinSpacingCm) + 1;
	const float First = -MinSpacingCm * static_cast<float>(PerAxis - 1) * 0.5f;

	for (int32 IndexX = 0; IndexX < PerAxis; ++IndexX)
	{
		for (int32 IndexY = 0; IndexY < PerAxis; ++IndexY)
		{
			const FVector2D Cell(First + MinSpacingCm * static_cast<float>(IndexX),
				First + MinSpacingCm * static_cast<float>(IndexY));

			// 시작점 둘레는 비운다 — 부팅하자마자 코앞에 동물이 서 있으면 "흩어 놨다" 가 아니다.
			const bool bTooCloseToStart = ClearCenters.ContainsByPredicate(
				[&Cell, PlayerStartClearCm](const FVector2D& Center)
				{
					return FVector2D::Distance(Center, Cell) < PlayerStartClearCm;
				});

			if (!bTooCloseToStart)
			{
				Cells.Add(Cell);
			}
		}
	}

	return Cells;
}
