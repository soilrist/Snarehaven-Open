// #0204 (M13-19 ①) 2층 검사 — 평원 시작점 늑대.
// 결함: 상시 위험구역이 L_Hunt 평원 시작점 코앞에 서서 선공 늑대가 부활하자마자 물었다(한 판 34번 사망).
// 보는 것:
//  ① `L_Hunt` 에 놓인 선공종 구역마다, 구역 자리가 제 존 안(BeginPlay 가 안 옮긴다) + 각 마리 자리(`MakeSpawnOffset`)가
//     **평원 칸 밖**이고 시작점(PlayerStart)에서 분산 표 `PlayerStartClearCm` 이상 떨어져 있다 (DESIGN 3.1·8.7).
//  ② 이벤트 행(수명 있는 행)이 시작점 칸(`CanStandInRegion`)을 못 고른다.
// 새 지도(2026-09-25)라 칸 판정은 바다 액터의 존 칸 코드(`FZoneRow::FieldCode`)로 한다.
//  ③ 늑대는 평원에 못 서고 설원엔 선다 — 규칙 함수 자체.
// 숫자는 여기 적지 않는다: 반지름·마리수는 위험구역 표, 1500 은 분산 표, 칸은 존 표에서 읽는다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Data/AnimalRow.h"
#include "Data/AnimalSpreadRow.h"
#include "Data/DangerZoneRow.h"
#include "Data/ZoneRow.h"
#include "Engine/DataTable.h"
#include "GameFramework/PlayerStart.h"
#include "World/DangerZoneActor.h"
#include "World/SeaActor.h"
#include "World/SnareZoneRules.h"

namespace
{
	const TCHAR* PlacementDangerTablePath = TEXT("/Game/Game/Data/World/DT_DangerZoneTable.DT_DangerZoneTable");
	const TCHAR* PlacementSpreadTablePath = TEXT("/Game/Game/Data/World/DT_AnimalSpreadTable.DT_AnimalSpreadTable");
	const FName PlainsZoneID(TEXT("zone.plains"));
	const FName SnowZoneID(TEXT("zone.snow"));
	const FName WolfRowName(TEXT("Animal_Wolf"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDangerZoneNoAggressiveNearStartTest,
	"Snarehaven.DangerZone.Placement.NoAggressiveNearStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDangerZoneNoAggressiveNearStartTest::RunTest(const FString& Parameters)
{
	const UDataTable* DangerTable = LoadObject<UDataTable>(nullptr, PlacementDangerTablePath);
	const UDataTable* ZoneTable = LoadObject<UDataTable>(nullptr, SnareZones::TablePath);
	const UDataTable* SpreadTable = LoadObject<UDataTable>(nullptr, PlacementSpreadTablePath);
	const UDataTable* AnimalTable = GetDefault<AAnimalCharacter>()->GetAnimalTable();
	if (!TestNotNull(TEXT("위험구역 표"), DangerTable) || !TestNotNull(TEXT("존 표"), ZoneTable)
		|| !TestNotNull(TEXT("분산 표"), SpreadTable) || !TestNotNull(TEXT("동물 표"), AnimalTable))
	{
		return false;
	}

	const FZoneRow* Plains = SnareZones::FindZoneRow(ZoneTable, PlainsZoneID);
	const FZoneRow* Snow = SnareZones::FindZoneRow(ZoneTable, SnowZoneID);
	if (!TestNotNull(TEXT("평원 칸 행"), Plains) || !TestNotNull(TEXT("설원 칸 행"), Snow))
	{
		return false;
	}

	// ③ 규칙 함수 — 늑대(선공·설원 서식)는 평원 X, 설원 O.
	TestFalse(TEXT("늑대는 평원 칸에 못 선다"), ADangerZoneActor::CanStandInRegion(AnimalTable, WolfRowName, *Plains));
	TestTrue(TEXT("늑대는 설원 칸엔 선다"), ADangerZoneActor::CanStandInRegion(AnimalTable, WolfRowName, *Snow));

	// 레벨에서 바다(존 칸)·시작점·놓인 구역을 읽는다 — 띄우지 않고 액터 목록만 (`Snarehaven.Mine.Level` 과 같은 길)
	const UWorld* const Level = LoadObject<UWorld>(nullptr, TEXT("/Game/Game/Maps/L_Hunt.L_Hunt"));
	if (!TestNotNull(TEXT("L_Hunt"), Level) || !TestNotNull(TEXT("레벨"), Level->PersistentLevel.Get()))
	{
		return false;
	}
	const ASeaActor* Sea = nullptr;
	const APlayerStart* Start = nullptr;
	TArray<const ADangerZoneActor*> Placed;
	for (const AActor* const Actor : Level->PersistentLevel->Actors)
	{
		Sea = Sea ? Sea : Cast<ASeaActor>(Actor);
		Start = Start ? Start : Cast<APlayerStart>(Actor);
		if (const ADangerZoneActor* const Zone = Cast<ADangerZoneActor>(Actor))
		{
			Placed.Add(Zone);
		}
	}
	if (!TestNotNull(TEXT("바다"), Sea) || !TestTrue(TEXT("바다가 존 칸을 든다"), Sea->HasField()) || !TestNotNull(TEXT("시작점"), Start))
	{
		return false;
	}
	const FVector StartLocation = Start->GetActorLocation();
	const FName LevelName(TEXT("L_Hunt"));
	auto RegionAt = [&](const FVector& At) { return SnareZones::FindRegionAt(ZoneTable, LevelName, At, Sea); };
	const FZoneRow* const StartRegion = RegionAt(StartLocation);
	TestTrue(TEXT("시작점은 평원 칸"), StartRegion != nullptr && StartRegion->ZoneID == PlainsZoneID);

	float ClearCm = 0.f;
	for (const TPair<FName, uint8*>& Pair : SpreadTable->GetRowMap())
	{
		ClearCm = FMath::Max(ClearCm, reinterpret_cast<const FAnimalSpreadRow*>(Pair.Value)->PlayerStartClearCm);
	}
	TestTrue(TEXT("분산 표에 시작점 반경이 있다"), ClearCm > 0.f);

	int32 AggressiveRows = 0;
	int32 AggressiveNearStart = 0;
	for (const TPair<FName, uint8*>& Pair : DangerTable->GetRowMap())
	{
		const FDangerZoneRow& Row = *reinterpret_cast<const FDangerZoneRow*>(Pair.Value);
		const FAnimalRow* Animal = AnimalTable->FindRow<FAnimalRow>(Row.AnimalRowName, TEXT("PlacementTest"));
		if (!Animal || Animal->BehaviorKind != EAnimalBehaviorKind::Aggressive)
		{
			continue;
		}
		++AggressiveRows;

		if (Row.LifetimeSeconds > 0)
		{
			// ② 이벤트 행: 시작점 칸에 이 종이 설 수 있으면 안 된다.
			if (StartRegion != nullptr && ADangerZoneActor::CanStandInRegion(AnimalTable, Row.AnimalRowName, *StartRegion))
			{
				AddError(FString::Printf(TEXT("이벤트 %s 가 시작점 칸 %s 를 고를 수 있다"), *Pair.Key.ToString(), *StartRegion->ZoneID.ToString()));
				++AggressiveNearStart;
			}
			continue;
		}

		// ① 상시 행: 레벨에 놓인 구역마다 — 제 존 안에 놓여 BeginPlay 가 안 옮기고, 마리마다 평원·시작점 밖
		const FZoneRow* const Zone = SnareZones::FindZoneRow(ZoneTable, Row.ZoneID);
		for (const ADangerZoneActor* const Actor : Placed)
		{
			if (Actor->GetDangerZoneRowName() != Pair.Key)
			{
				continue;
			}
			const FVector Center = Actor->GetActorLocation();
			const FZoneRow* const Here = RegionAt(Center);
			TestTrue(*FString::Printf(TEXT("%s 는 제 존 %s 안에 놓였다"), *Actor->GetActorNameOrLabel(), *Row.ZoneID.ToString()),
				Zone != nullptr && Here != nullptr && Here->ZoneID == Zone->ZoneID && Sea->CoastDistance(Center) < 0.f);
			for (int32 Index = 0; Index < Row.AnimalCount; ++Index)
			{
				const FVector Spot = Center + ADangerZoneActor::MakeSpawnOffset(Row.RadiusCm, Index, Row.AnimalCount);
				const FZoneRow* const SpotRegion = RegionAt(Spot);
				const bool bInPlains = SpotRegion != nullptr && SpotRegion->ZoneID == PlainsZoneID;
				const bool bNearStart = FVector::Dist2D(Spot, StartLocation) < ClearCm;
				if (bInPlains || bNearStart)
				{
					AddError(FString::Printf(TEXT("%s %d번 선공 %s 가 (%.0f,%.0f) — 평원=%d 시작점거리=%.0f"),
						*Pair.Key.ToString(), Index, *Row.AnimalRowName.ToString(), Spot.X, Spot.Y,
						bInPlains, FVector::Dist2D(Spot, StartLocation)));
					++AggressiveNearStart;
				}
			}
		}
	}

	TestTrue(TEXT("선공종 위험구역 행이 있다 (검사가 빈손이 아니다)"), AggressiveRows > 0);
	TestEqual(TEXT("평원·시작점 반경 안 선공 자리 수"), AggressiveNearStart, 0);
	AddInfo(FString::Printf(TEXT("선공 행 %d · 시작점 반경 %.0fcm · 반경 안 선공 %d"),
		AggressiveRows, ClearCm, AggressiveNearStart));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
