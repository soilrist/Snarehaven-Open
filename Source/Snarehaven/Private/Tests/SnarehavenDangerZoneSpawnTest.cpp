// ROADMAP M6-04b 2층 검사 — 위험구역 스포너(`ADangerZoneActor`).
// 보는 것 셋:
//  ① 구역 안 동물 수가 **표 값과 같다** — 그리고 사라진 마리는 다시 채워진다.
//  ② **코드에 박힌 숫자가 0개다** — 표와 다른 마리수를 먹이면 스포너가 그쪽을 따라간다.
//     (설계값 5 가 코드에 박혀 있으면 이 검사가 운다.)
//  ③ **클라이언트는 스스로 스폰하지 않는다** — 액터 역할을 게스트 것으로 바꾸면 한 마리도 안 선다.
// 표 숫자를 여기 적지 않는다: 500·5·300 을 아는 것은 `DT_DangerZoneTable` 과 DESIGN 8.1 뿐이고,
// 그 둘이 같은지는 M6-04a 검사(`Snarehaven.DangerZone.Table.MatchesDesign`)가 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.DangerZone; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Data/DangerZoneRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "World/DangerZoneActor.h"

namespace
{
	const TCHAR* DangerTablePath = TEXT("/Game/Game/Data/World/DT_DangerZoneTable.DT_DangerZoneTable");

	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다). 표의 마리수와 **다른 수**여야
	// "행을 따라간다" 를 증명할 수 있다. 3 은 표의 5 와도, 코드 기본값(-1)과도 다르다.
	constexpr int32 TestAnimalCount = 3;
	constexpr float TestRadiusCm = 400.f;
	constexpr int32 TestRespawnSeconds = 60;

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	// 표를 읽지 않고 수치를 직접 먹인 구역 하나. BeginPlay 를 안 태우므로 스폰은 검사가 직접 부른다.
	ADangerZoneActor* SpawnTestZone(UWorld* World, const FDangerZoneRow& Row)
	{
		ADangerZoneActor* Zone =
			World->SpawnActor<ADangerZoneActor>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Zone != nullptr)
		{
			Zone->SetDangerZoneRow(Row);
		}

		return Zone;
	}

	FDangerZoneRow MakeTestRow(FName AnimalRowName)
	{
		FDangerZoneRow Row;
		Row.ZoneID = FName(TEXT("zone.hunt"));
		Row.AnimalRowName = AnimalRowName;
		Row.RadiusCm = TestRadiusCm;
		Row.AnimalCount = TestAnimalCount;
		Row.RespawnSeconds = TestRespawnSeconds;
		return Row;
	}

	/** 표 첫 행의 종 이름 — 세울 종까지 검사가 지어내지 않는다 (표에 있는 행이어야 동물이 선다). */
	FName ReadAnyAnimalRowNameFromTable()
	{
		const UDataTable* Table = LoadObject<UDataTable>(nullptr, DangerTablePath);
		if (!IsValid(Table))
		{
			return NAME_None;
		}

		for (const FName& RowName : Table->GetRowNames())
		{
			if (const FDangerZoneRow* Row = Table->FindRow<FDangerZoneRow>(RowName, TEXT("DangerZoneSpawnTest")))
			{
				return Row->AnimalRowName;
			}
		}

		return NAME_None;
	}
}

// ① 정원만큼 서고, 한 마리가 사라지면 다시 채워진다 — 그리고 그 수는 **행**에서 온다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDangerZoneSpawnTest,
	"Snarehaven.DangerZone.Spawner.FillsZoneToRowCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDangerZoneSpawnTest::RunTest(const FString& Parameters)
{
	const FName AnimalRowName = ReadAnyAnimalRowNameFromTable();
	if (AnimalRowName.IsNone())
	{
		AddError(TEXT("DT_DangerZoneTable 에서 세울 종을 못 읽었다 - 표가 비었거나 행 구조체가 다르다"));
		return false;
	}

	UWorld* World = CreateTestWorld();
	ADangerZoneActor* Zone = SpawnTestZone(World, MakeTestRow(AnimalRowName));
	if (Zone == nullptr)
	{
		AddError(TEXT("위험구역 액터를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("수치가 성하면 구역이 켜진다"), Zone->IsZoneActive());
	TestEqual(TEXT("켜기 전에는 한 마리도 없다"), Zone->GetLiveAnimalCount(), 0);

	const int32 SpawnedNow = Zone->RefillZone();
	// 여기서 3(TestAnimalCount)이 나오는 것이 곧 "코드에 박힌 숫자 0개" 다 —
	// 설계값 5 가 어디 박혀 있으면 이 줄이 5 를 보고 운다.
	TestEqual(TEXT("행이 정한 마리수만큼 세운다"), SpawnedNow, TestAnimalCount);
	TestEqual(TEXT("구역 안에 그만큼 서 있다"), Zone->GetLiveAnimalCount(), TestAnimalCount);

	// 다 찼으면 또 불러도 늘지 않는다 (재생성 타이머가 주기마다 이걸 부른다).
	TestEqual(TEXT("정원이 찼으면 더 세우지 않는다"), Zone->RefillZone(), 0);
	TestEqual(TEXT("정원은 그대로다"), Zone->GetLiveAnimalCount(), TestAnimalCount);

	// 한 마리를 쓰러뜨린다 — 액터는 남지만(`Destroy` 하지 않는다) 자리는 비어야 한다.
	// 여기서 `InitializeFromTable()` 이 참이어야 한다는 것이 곧 **스포너가 종을 제대로 넣었다**는 뜻이다
	// (행 이름이 틀렸으면 동물이 자기 표에서 행을 못 찾는다).
	bool bFelled = false;
	for (TActorIterator<AAnimalCharacter> It(World); It && !bFelled; ++It)
	{
		UAnimalStateComponent* State = It->GetAnimalState();
		if (!IsValid(State) || !It->InitializeFromTable())
		{
			continue;
		}

		bFelled = State->ApplyDamage(State->GetMaxHP()) && State->IsFinished();
	}

	TestTrue(TEXT("검사가 한 마리를 실제로 쓰러뜨렸다"), bFelled);
	TestEqual(TEXT("쓰러진 마리는 자리를 비운다"), Zone->GetLiveAnimalCount(), TestAnimalCount - 1);
	TestEqual(TEXT("빈자리만큼만 다시 세운다"), Zone->RefillZone(), 1);
	TestEqual(TEXT("정원으로 돌아온다"), Zone->GetLiveAnimalCount(), TestAnimalCount);

	DestroyTestWorld(World);
	return true;
}

// ② 자리 계산도 행 값만 쓴다 — 반지름 밖으로 나가는 자리가 없고, 자리끼리 겹치지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDangerZoneOffsetTest,
	"Snarehaven.DangerZone.Spawner.OffsetsStayInsideRadius",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDangerZoneOffsetTest::RunTest(const FString& Parameters)
{
	TArray<FVector> Offsets;
	for (int32 Index = 0; Index < TestAnimalCount; ++Index)
	{
		const FVector Offset = ADangerZoneActor::MakeSpawnOffset(TestRadiusCm, Index, TestAnimalCount);
		TestTrue(FString::Printf(TEXT("%d번 자리가 반지름 안이다"), Index), Offset.Size2D() <= TestRadiusCm + KINDA_SMALL_NUMBER);
		TestTrue(TEXT("자리는 구역 바닥 높이 그대로다"), FMath::IsNearlyZero(Offset.Z));
		for (const FVector& Other : Offsets)
		{
			TestFalse(TEXT("두 자리가 같은 점이 아니다"), Other.Equals(Offset));
		}
		Offsets.Add(Offset);
	}

	// 미정 수치(음수)면 자리 계산도 하지 않는다 — 구역 중심에 겹쳐 세우는 사고를 막는다.
	TestTrue(TEXT("반지름이 미정이면 자리가 없다"),
		ADangerZoneActor::MakeSpawnOffset(-1.f, 0, TestAnimalCount).IsNearlyZero());
	TestTrue(TEXT("마리수가 미정이면 자리가 없다"),
		ADangerZoneActor::MakeSpawnOffset(TestRadiusCm, 0, -1).IsNearlyZero());
	return true;
}

// ③ 게스트는 스스로 세우지 않는다 — 스폰 권위는 호스트뿐이다 (CLAUDE.md 8 · DESIGN 4.1).
//    진짜 두 프로세스 왕복은 여기서 못 한다(넷드라이버가 없는 테스트 월드다 — `SnarehavenZoneTravelTest`
//    와 같은 이유). 대신 액터의 네트워크 역할을 게스트 것으로 바꿔 권위 없는 경로를 그대로 태운다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDangerZoneAuthorityTest,
	"Snarehaven.DangerZone.Spawner.GuestNeverSpawns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDangerZoneAuthorityTest::RunTest(const FString& Parameters)
{
	const FName AnimalRowName = ReadAnyAnimalRowNameFromTable();
	if (AnimalRowName.IsNone())
	{
		AddError(TEXT("DT_DangerZoneTable 에서 세울 종을 못 읽었다"));
		return false;
	}

	UWorld* World = CreateTestWorld();
	ADangerZoneActor* Zone = SpawnTestZone(World, MakeTestRow(AnimalRowName));
	if (Zone == nullptr)
	{
		AddError(TEXT("위험구역 액터를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	Zone->SetRole(ROLE_SimulatedProxy);
	TestEqual(TEXT("게스트는 한 마리도 안 세운다"), Zone->RefillZone(), 0);
	TestEqual(TEXT("구역이 비어 있다"), Zone->GetLiveAnimalCount(), 0);

	// 권위를 돌려주면 같은 호출이 정원을 채운다 — 막은 것이 권위뿐임을 보인다.
	Zone->SetRole(ROLE_Authority);
	TestEqual(TEXT("호스트에서는 채운다"), Zone->RefillZone(), TestAnimalCount);

	DestroyTestWorld(World);
	return true;
}

// ④ 수치가 미정(음수)이면 구역이 아예 안 켜진다 (Verification.md 7).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDangerZoneUnsetNumbersTest,
	"Snarehaven.DangerZone.Spawner.UnsetNumbersKeepZoneOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDangerZoneUnsetNumbersTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();

	// 빈 행 = 전부 미정(-1). 칸을 하나씩 채워도 나머지가 미정인 동안은 계속 꺼져 있어야 한다.
	FDangerZoneRow Row;
	ADangerZoneActor* Zone = SpawnTestZone(World, Row);
	if (Zone == nullptr)
	{
		AddError(TEXT("위험구역 액터를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}

	TestFalse(TEXT("미정 수치로는 구역이 안 켜진다"), Zone->IsZoneActive());
	TestEqual(TEXT("미정이면 한 마리도 안 세운다"), Zone->RefillZone(), 0);

	// 종이 비어 있는 것도 미정이다 — 이름 없는 동물을 세우면 기본 종으로 조용히 굳는다.
	Row.RadiusCm = TestRadiusCm;
	Row.AnimalCount = TestAnimalCount;
	Row.RespawnSeconds = TestRespawnSeconds;
	TestFalse(TEXT("세울 종이 비면 구역이 안 켜진다"), Zone->SetDangerZoneRow(Row));
	TestEqual(TEXT("그때도 한 마리도 안 세운다"), Zone->RefillZone(), 0);

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
