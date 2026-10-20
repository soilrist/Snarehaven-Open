// ROADMAP M1-02 2층 검사 — 동물 상태 머신(Idle/Fight/Capturable/Dead/Captured)이 DESIGN 3.1 대로 도는지,
// 그리고 숫자를 코드가 아니라 DT_AnimalTable 에서 읽어 쓰는 경로가 실제로 살아 있는지.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다. 설계 숫자를 쓰면
	// 사람이 밸런스를 바꿀 때마다 검사가 깨진다).
	constexpr float TestMaxHP = 200.f;
	constexpr float TestCaptureThresholdPct = 50.f;
	constexpr float TestSmallDamage = 20.f;
	constexpr float TestTolerance = 0.01f;

	// 설계 숫자 대신 테스트 전용 값을 넣은 가짜 행.
	FAnimalRow MakeTestRow()
	{
		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_dummy");
		Row.MaxHP = TestMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		return Row;
	}

	UAnimalStateComponent* MakeInitializedComponent(FAutomationTestBase& Test)
	{
		UAnimalStateComponent* Component = NewObject<UAnimalStateComponent>();
		Test.TestTrue(TEXT("테스트 행으로 초기화"), Component->InitializeFromRow(MakeTestRow()));
		return Component;
	}

	// 동물 액터를 스폰하려면 월드가 필요하다 (SnarehavenCharacterTest 와 같은 방식).
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

	// 실제 데이터 테이블 (M1-01 에서 만든 것). 경로는 engines/unreal.md 폴더 구조 그대로.
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* PigRowName = TEXT("Animal_Pig");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalDamageTest, "Snarehaven.Animal.State.DamageLeadsToCapturable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalDamageTest::RunTest(const FString& Parameters)
{
	UAnimalStateComponent* Component = MakeInitializedComponent(*this);

	TestEqual(TEXT("초기 상태는 Idle"), Component->GetState(), EAnimalState::Idle);
	TestEqual(TEXT("초기 체력은 최대치"), Component->GetCurrentHP(), TestMaxHP, TestTolerance);
	TestEqual(TEXT("임계 체력은 최대치의 퍼센트"), Component->GetCaptureThresholdHP(),
		TestMaxHP * TestCaptureThresholdPct / 100.f, TestTolerance);

	// 임계값보다 위로만 깎으면 Fight 에 머문다.
	TestTrue(TEXT("첫 데미지가 들어간다"), Component->ApplyDamage(TestSmallDamage));
	TestEqual(TEXT("임계 위에서는 Fight"), Component->GetState(), EAnimalState::Fight);
	TestEqual(TEXT("깎인 만큼 줄었다"), Component->GetCurrentHP(), TestMaxHP - TestSmallDamage, TestTolerance);

	// 임계값에 정확히 닿는 순간 Capturable (DESIGN 3.1 "HP ≤ 임계" — 이하이지 미만이 아니다).
	const float ToThreshold = Component->GetCurrentHP() - Component->GetCaptureThresholdHP();
	TestTrue(TEXT("임계까지 깎는다"), Component->ApplyDamage(ToThreshold));
	TestEqual(TEXT("임계에서 Capturable"), Component->GetState(), EAnimalState::Capturable);

	// 임계 이하에서 더 때려도 죽기 전까지는 Capturable 유지 (플레이어가 사냥/포획을 고르는 구간).
	TestTrue(TEXT("임계 아래로 더 깎는다"), Component->ApplyDamage(TestSmallDamage));
	TestEqual(TEXT("임계 아래에서도 Capturable"), Component->GetState(), EAnimalState::Capturable);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalCaptureTest, "Snarehaven.Animal.State.CaptureNeedsCapturable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalCaptureTest::RunTest(const FString& Parameters)
{
	UAnimalStateComponent* Component = MakeInitializedComponent(*this);

	// Idle 에서는 포획 불가 — 포획 버튼이 언제 켜지는지가 이 검사에 걸려 있다 (M1-05·M1-08).
	TestFalse(TEXT("Idle 에서는 포획 실패"), Component->TryCapture());
	TestEqual(TEXT("실패해도 상태 그대로"), Component->GetState(), EAnimalState::Idle);

	Component->ApplyDamage(Component->GetMaxHP() - Component->GetCaptureThresholdHP());
	TestEqual(TEXT("임계에서 Capturable"), Component->GetState(), EAnimalState::Capturable);
	TestTrue(TEXT("Capturable 에서는 포획 성공"), Component->TryCapture());
	TestEqual(TEXT("포획 뒤 Captured"), Component->GetState(), EAnimalState::Captured);

	// 포획된 동물은 끝 상태 — 더 때릴 수도, 다시 포획할 수도 없다.
	TestFalse(TEXT("포획 뒤에는 데미지 무시"), Component->ApplyDamage(TestSmallDamage));
	TestFalse(TEXT("두 번 포획되지 않는다"), Component->TryCapture());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalDeathTest, "Snarehaven.Animal.State.DeathIsFinal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalDeathTest::RunTest(const FString& Parameters)
{
	UAnimalStateComponent* Component = MakeInitializedComponent(*this);

	// 한 방에 최대 체력을 넘겨 때려도 체력은 0 에서 멈춘다 (음수 체력은 HUD 를 망가뜨린다).
	TestTrue(TEXT("치명타가 들어간다"), Component->ApplyDamage(TestMaxHP * 2.f));
	TestEqual(TEXT("체력은 0 에서 멈춘다"), Component->GetCurrentHP(), 0.f, TestTolerance);
	TestEqual(TEXT("체력 0 이면 Dead"), Component->GetState(), EAnimalState::Dead);

	TestFalse(TEXT("죽은 뒤에는 데미지 무시"), Component->ApplyDamage(TestSmallDamage));
	TestFalse(TEXT("죽은 동물은 포획 불가"), Component->TryCapture());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalUnsetNumbersTest, "Snarehaven.Animal.State.UnsetNumbersStayOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalUnsetNumbersTest::RunTest(const FString& Parameters)
{
	// 숫자가 아직 미정(-1)인 행. 이때 상태 머신이 돌면 "체력 0 이하 = 즉사" 로 조용히 오작동한다
	// — 그래서 아예 켜지지 않아야 한다 (Verification.md 7).
	UAnimalStateComponent* Component = NewObject<UAnimalStateComponent>();
	const FAnimalRow EmptyRow;

	TestFalse(TEXT("미정 숫자로는 초기화되지 않는다"), Component->InitializeFromRow(EmptyRow));
	TestEqual(TEXT("상태는 Idle 그대로"), Component->GetState(), EAnimalState::Idle);
	TestTrue(TEXT("임계 체력도 미정(음수)"), Component->GetCaptureThresholdHP() < 0.f);
	TestFalse(TEXT("초기화 전에는 데미지도 안 들어간다"), Component->ApplyDamage(TestSmallDamage));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalTableWiringTest, "Snarehaven.Animal.Table.PigUsesTableNumbers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalTableWiringTest::RunTest(const FString& Parameters)
{
	// 이 검사만 실제 데이터를 본다. 설계 숫자(100·25%)를 여기 적지 않는 이유는
	// 사람이 밸런스를 바꾸면 그때마다 검사가 깨지기 때문 — 값은 테이블에서 읽어 관계만 본다.
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable))
	{
		return false;
	}

	const FAnimalRow* PigRow = AnimalTable->FindRow<FAnimalRow>(PigRowName, TEXT("FSnarehavenAnimalTableWiringTest"));
	if (!TestNotNull(TEXT("Animal_Pig 행"), PigRow))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	Animal->SetAnimalData(AnimalTable, PigRowName);
	TestTrue(TEXT("테이블 행으로 초기화"), Animal->InitializeFromTable());

	UAnimalStateComponent* State = Animal->GetAnimalState();
	if (!TestNotNull(TEXT("상태 컴포넌트"), State))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 코드 기본값(-1)이 아니라 테이블 값이 들어왔는가 — 이게 이 조각의 핵심이다.
	TestEqual(TEXT("최대 체력이 테이블 값"), State->GetMaxHP(), PigRow->MaxHP, TestTolerance);
	TestEqual(TEXT("현재 체력이 최대치"), State->GetCurrentHP(), PigRow->MaxHP, TestTolerance);
	TestEqual(TEXT("임계 체력이 테이블 퍼센트"), State->GetCaptureThresholdHP(),
		PigRow->MaxHP * PigRow->CaptureThresholdPct / 100.f, TestTolerance);

	// ROADMAP M1-02 합격 기준 — 테이블이 정한 임계 체력에 닿으면 Capturable.
	State->ApplyDamage(PigRow->MaxHP - State->GetCaptureThresholdHP());
	TestEqual(TEXT("테이블 임계에서 Capturable"), State->GetState(), EAnimalState::Capturable);
	AddInfo(FString::Printf(TEXT("[%s] 최대 %.0f → 임계 %.0f 에서 Capturable"),
		*PigRow->AnimalID.ToString(), State->GetMaxHP(), State->GetCaptureThresholdHP()));

	DestroyTestWorld(World);
	return true;
}

// ROADMAP M5-04b 합격 기준 — 사냥터에 선 **3종이 각각 포획된다**. 종마다 검사를 하나씩 쓰지 않고
// 표를 통째로 돌린다: 동물이 늘어도 이 검사는 늘어난 종을 그대로 본다 (DataDriven.md 1).
// 설계 숫자를 여기 적지 않는 것은 위 PigUsesTableNumbers 와 같은 이유다 — 값은 행에서 읽고 관계만 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalSpeciesCaptureTest,
	"Snarehaven.Animal.Table.EverySpeciesCanBeCaptured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalSpeciesCaptureTest::RunTest(const FString& Parameters)
{
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable))
	{
		return false;
	}

	const TArray<FName> RowNames = AnimalTable->GetRowNames();
	// 표가 비면 아래 for 문이 한 번도 안 돌아 **아무것도 안 보고 통과**한다 (Verification.md).
	if (!TestTrue(TEXT("표에 행이 있다"), RowNames.Num() > 0))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	for (const FName& RowName : RowNames)
	{
		const FString Label = RowName.ToString();
		const FAnimalRow* Row = AnimalTable->FindRow<FAnimalRow>(RowName,
			TEXT("FSnarehavenAnimalSpeciesCaptureTest"));
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 행"), *Label), Row))
		{
			continue;
		}

		AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 동물 스폰"), *Label), Animal))
		{
			continue;
		}

		Animal->SetAnimalData(AnimalTable, RowName);
		TestTrue(*FString::Printf(TEXT("[%s] 행으로 초기화"), *Label), Animal->InitializeFromTable());

		UAnimalStateComponent* State = Animal->GetAnimalState();
		if (!TestNotNull(*FString::Printf(TEXT("[%s] 상태 컴포넌트"), *Label), State))
		{
			Animal->Destroy();
			continue;
		}

		// 포획 창까지 깎는다 — 얼마나 깎을지는 그 종의 행이 정한다.
		State->ApplyDamage(State->GetCurrentHP() - State->GetCaptureThresholdHP());
		TestEqual(*FString::Printf(TEXT("[%s] 임계에서 Capturable"), *Label),
			State->GetState(), EAnimalState::Capturable);
		TestTrue(*FString::Printf(TEXT("[%s] 포획된다"), *Label), State->TryCapture());
		TestEqual(*FString::Printf(TEXT("[%s] 포획 뒤 Captured"), *Label),
			State->GetState(), EAnimalState::Captured);

		// 펫이 비어 있으면 포획해도 지급 요청이 만들어지지 않는다 (FAnimalRow 주석 · M1-05).
		TestFalse(*FString::Printf(TEXT("[%s] 펫 아이템이 있다"), *Label), State->GetPetItemID().IsNone());
		// 죽는 쪽 길도 종마다 재료가 있어야 빈손이 되지 않는다 (DESIGN 3.1 "재료 아이템 1~3개").
		TestTrue(*FString::Printf(TEXT("[%s] 드롭 재료가 있다"), *Label), Row->DeathDropItems.Num() > 0);

		AddInfo(FString::Printf(TEXT("[%s] 최대 %.0f → 임계 %.0f 에서 포획 · 펫 %s · 드롭 %d종"),
			*Row->AnimalID.ToString(), State->GetMaxHP(), State->GetCaptureThresholdHP(),
			*State->GetPetItemID().ToString(), Row->DeathDropItems.Num()));
		Animal->Destroy();
	}

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
