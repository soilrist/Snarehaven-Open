// ROADMAP M21-06a 2층 검사 — 같이 잡기 (DESIGN 3.1 「같이 잡기 드롭 창」 · 3.4).
// 막타 전 창 안에 명중한 사람은 한 마리 몫(`animal.<종>` 줄)을 **각자 자기 대기열**에 받는다 — 드롭은 서버가 사람마다 굴린다(지시 ①). 혼자 잡으면 한 사람만,
// 안 때린 사람·창 밖에 때린 사람은 못 받는다. 보유 수량은 누구도 안 바뀐다 (CLAUDE.md 8).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Combat.SharedDrop; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Inventory/InventoryComponent.h"
#include "SnarehavenCharacter.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다. 창은 DESIGN 의 10초와 **일부러 다르다**).
	const FName TestDropItem = TEXT("item.test_shared");
	const FName TestAnimalID = TEXT("animal.test_dummy");    // 대기열에 쌓이는 처치 줄
	constexpr int32 TestDropCount = 2;
	constexpr float TestShareWindowSeconds = 4.f;
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestMaxHP = 20.f;          // 두 대면 죽는다
	constexpr float TestCaptureThresholdPct = 10.f;
	const FVector TestAnimalAt(100.f, 0.f, 0.f);
	const FVector TestFirstAt(0.f, 0.f, 0.f);
	const FVector TestSecondAt(0.f, 200.f, 0.f);
	const FVector TestBystanderAt(0.f, -200.f, 0.f);

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

	ASnarehavenCharacter* SpawnTestPlayer(UWorld* World, const FVector& At)
	{
		ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>(At, FRotator::ZeroRotator);
		if (Player == nullptr)
		{
			return nullptr;
		}
		Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
		Player->GetMeleeAttack()->SetShareDropWindowSeconds(TestShareWindowSeconds);
		return Player;
	}

	AAnimalCharacter* SpawnTestAnimal(UWorld* World)
	{
		AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(TestAnimalAt, FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			return nullptr;
		}
		FAnimalRow Row;
		Row.AnimalID = TestAnimalID;
		Row.MaxHP = TestMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Row.DeathDropItems = { TestDropItem };
		Row.DeathDropCountMin = TestDropCount;
		Row.DeathDropCountMax = TestDropCount;
		Animal->GetAnimalState()->InitializeFromRow(Row);
		return Animal;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSharedDropBothHitTest, "Snarehaven.Combat.SharedDrop.BothHittersGetDrops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 둘이 한 대씩 — 막타가 아닌 사람도 한 마리 몫을 받고, 안 때린 사람은 0.
bool FSnarehavenSharedDropBothHitTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* First = SpawnTestPlayer(World, TestFirstAt);
	ASnarehavenCharacter* Second = SpawnTestPlayer(World, TestSecondAt);
	ASnarehavenCharacter* Bystander = SpawnTestPlayer(World, TestBystanderAt);
	AAnimalCharacter* Animal = SpawnTestAnimal(World);
	if (!TestNotNull(TEXT("첫 사람"), First) || !TestNotNull(TEXT("둘째 사람"), Second)
		|| !TestNotNull(TEXT("구경꾼"), Bystander) || !TestNotNull(TEXT("동물"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("첫 사람이 한 대"), First->GetMeleeAttack()->AttackOnce());
	TestTrue(TEXT("둘째 사람이 막타"), Second->GetMeleeAttack()->AttackOnce());
	TestEqual(TEXT("죽었다"), Animal->GetAnimalState()->GetState(), EAnimalState::Dead);

	const int32 FirstPending = First->GetInventory()->GetPendingCount(TestAnimalID);
	const int32 SecondPending = Second->GetInventory()->GetPendingCount(TestAnimalID);
	const int32 BystanderPending = Bystander->GetInventory()->GetPendingCount(TestAnimalID);
	TestEqual(TEXT("막타 사람 대기열 = 처치 1"), SecondPending, 1);
	TestEqual(TEXT("먼저 때린 사람도 처치 1 을 각자 받는다"), FirstPending, 1);
	TestEqual(TEXT("안 때린 사람은 못 받는다"), BystanderPending, 0);
	TestEqual(TEXT("호스트가 굴린 드롭 아이템은 안 쌓인다"), First->GetInventory()->GetPendingCount(TestDropItem), 0);
	TestEqual(TEXT("보유는 안 늘어난다(서버 몫)"), First->GetInventory()->GetItemCount(TestDropItem), 0);
	AddInfo(FString::Printf(TEXT("같이 잡기 대기열: 먼저 %d · 막타 %d · 구경 %d"), FirstPending, SecondPending, BystanderPending));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSharedDropSoloTest, "Snarehaven.Combat.SharedDrop.SoloKillerOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 혼자 두 대 — 한 사람만 받고, 두 번 받지 않는다(자기 자신은 나눔 대상이 아니다).
bool FSnarehavenSharedDropSoloTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Solo = SpawnTestPlayer(World, TestFirstAt);
	ASnarehavenCharacter* Other = SpawnTestPlayer(World, TestSecondAt);
	AAnimalCharacter* Animal = SpawnTestAnimal(World);
	if (!TestNotNull(TEXT("혼자"), Solo) || !TestNotNull(TEXT("다른 사람"), Other) || !TestNotNull(TEXT("동물"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	while (!Animal->GetAnimalState()->IsFinished())
	{
		if (!TestTrue(TEXT("사거리 안"), Solo->GetMeleeAttack()->AttackOnce()))
		{
			break;
		}
	}

	TestEqual(TEXT("혼자 잡으면 그 사람만 처치 1"), Solo->GetInventory()->GetPendingCount(TestAnimalID), 1);
	TestEqual(TEXT("다른 사람은 0"), Other->GetInventory()->GetPendingCount(TestAnimalID), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSharedDropWindowTest, "Snarehaven.Combat.SharedDrop.WindowDropsOldHitters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 창 계산 — 마지막 명중이 창 안인 사람만, 같은 사람은 한 줄(다시 때리면 시각만 고침), 행을 다시 읽으면 비운다.
bool FSnarehavenSharedDropWindowTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Early = SpawnTestPlayer(World, TestFirstAt);
	ASnarehavenCharacter* Late = SpawnTestPlayer(World, TestSecondAt);
	AAnimalCharacter* Animal = SpawnTestAnimal(World);
	if (!TestNotNull(TEXT("일찍"), Early) || !TestNotNull(TEXT("늦게"), Late) || !TestNotNull(TEXT("동물"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UAnimalStateComponent* State = Animal->GetAnimalState();
	State->RecordHitter(Early, 0.0);
	State->RecordHitter(Late, 3.0);
	State->RecordHitter(Late, 5.0);

	const TArray<AActor*> AtSix = State->GetHittersWithin(6.0, TestShareWindowSeconds);
	TestEqual(TEXT("6초에 창 4초면 늦게 때린 사람 하나"), AtSix.Num(), 1);
	TestTrue(TEXT("그 하나는 늦게 때린 사람"), AtSix.Contains(Late));

	const TArray<AActor*> AtFour = State->GetHittersWithin(4.0, TestShareWindowSeconds);
	TestEqual(TEXT("4초엔 둘 다(같은 사람은 한 줄)"), AtFour.Num(), 2);

	FAnimalRow Row;
	Row.AnimalID = TestAnimalID;
	Row.MaxHP = TestMaxHP;
	Row.CaptureThresholdPct = TestCaptureThresholdPct;
	State->InitializeFromRow(Row);
	TestEqual(TEXT("행을 다시 읽으면 명중자를 비운다"), State->GetHittersWithin(4.0, TestShareWindowSeconds).Num(), 0);

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
