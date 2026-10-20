// ROADMAP M3-02 2층 검사 — 동물 HP/상태가 실제로 복제 대상으로 등록됐는지, 그리고
// **호스트가 아닌 쪽에서는 판정이 일어나지 않는지**(게스트는 요청만 보낸다 — DESIGN 4.1 권위 표).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
//
// 진짜 두 프로세스 왕복은 여기서 못 한다(넷드라이버가 없는 테스트 월드다) — 그건 3층 몫이다.
// 대신 액터의 네트워크 역할을 게스트 것으로 바꿔서 **권위 없는 쪽 코드 경로**를 그대로 태운다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Combat/CaptureComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "SnarehavenCharacter.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestMaxHP = 20.f;
	constexpr float TestCaptureThresholdPct = 50.f;
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestTolerance = 0.01f;

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

	AAnimalCharacter* SpawnTestAnimal(UWorld* World, float DistanceCm)
	{
		AAnimalCharacter* Animal =
			World->SpawnActor<AAnimalCharacter>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			return nullptr;
		}

		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_dummy");
		Row.MaxHP = TestMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Row.PetItemID = TEXT("item.test_pet");
		Animal->GetAnimalState()->InitializeFromRow(Row);
		return Animal;
	}

	ASnarehavenCharacter* SpawnTestPlayer(UWorld* World)
	{
		ASnarehavenCharacter* Player =
			World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Player != nullptr)
		{
			Player->GetMeleeAttack()->SetAttackSettings(
				TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
		}

		return Player;
	}

	// 복제용 `RepIndex` 는 UClass 가 늦게(Link·PostLoad 때) 채운다. 게임에서는 넷드라이버가 뜨기 전에
	// 채워지지만 테스트는 그 길을 안 타서 전부 0 으로 남는다 — 그 상태로 `GetLifetimeReplicatedProps` 를
	// 부르면 부모가 등록한 0번과 겹쳐 `bIsPushBased` assert 로 엔진이 죽는다 (루프#40 에서 실제로 죽었다).
	// 플래그를 보고 한 번만 도는 함수라 여러 번 불러도 괜찮다.
	void EnsureReplicationDataIsSetUp()
	{
		UAnimalStateComponent::StaticClass()->SetUpRuntimeReplicationData();
	}

	// 그 이름의 프로퍼티가 복제 목록(`GetLifetimeReplicatedProps`)에 실제로 들어 있는지.
	// UPROPERTY 에 `ReplicatedUsing` 만 적고 DOREPLIFETIME 을 빼먹으면 조용히 안 나가므로 둘 다 본다.
	bool IsInLifetimeProps(const UAnimalStateComponent* Component, const TCHAR* PropertyName)
	{
		EnsureReplicationDataIsSetUp();

		const FProperty* Property = UAnimalStateComponent::StaticClass()->FindPropertyByName(FName(PropertyName));
		if (Property == nullptr || !Property->HasAnyPropertyFlags(CPF_Net))
		{
			return false;
		}

		TArray<FLifetimeProperty> Props;
		Component->GetLifetimeReplicatedProps(Props);
		for (const FLifetimeProperty& Lifetime : Props)
		{
			if (Lifetime.RepIndex == Property->RepIndex)
			{
				return true;
			}
		}

		return false;
	}

	FName GetRepNotifyName(const TCHAR* PropertyName)
	{
		const FProperty* Property = UAnimalStateComponent::StaticClass()->FindPropertyByName(FName(PropertyName));
		return Property != nullptr ? Property->RepNotifyFunc : NAME_None;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSnarehavenAnimalNetReplicatedPropsTest,
	"Snarehaven.Animal.Net.HealthAndStateAreReplicated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSnarehavenAnimalNetReplicatedPropsTest::RunTest(const FString& Parameters)
{
	EnsureReplicationDataIsSetUp();
	const UAnimalStateComponent* State = NewObject<UAnimalStateComponent>();

	TestTrue(TEXT("동물 상태 컴포넌트가 복제 대상"), State->GetIsReplicated());
	TestTrue(TEXT("CurrentHP 가 복제 목록에 있다"), IsInLifetimeProps(State, TEXT("CurrentHP")));
	TestTrue(TEXT("CurrentState 가 복제 목록에 있다"), IsInLifetimeProps(State, TEXT("CurrentState")));

	// 복제만 되고 알림이 없으면 게스트 HUD 가 갱신되지 않는다 — 콜백 이름까지 본다.
	TestEqual(TEXT("CurrentHP 의 복제 알림"), GetRepNotifyName(TEXT("CurrentHP")), FName(TEXT("OnRep_CurrentHP")));
	TestEqual(TEXT("CurrentState 의 복제 알림"), GetRepNotifyName(TEXT("CurrentState")), FName(TEXT("OnRep_CurrentState")));

	// 요청용 컴포넌트는 값을 복제하지 않지만, 복제 대상이어야 `Server_` RPC 가 호스트로 간다.
	TestTrue(TEXT("근접 공격 컴포넌트가 복제 대상"), NewObject<UMeleeAttackComponent>()->GetIsReplicated());
	TestTrue(TEXT("포획 컴포넌트가 복제 대상"), NewObject<UCaptureComponent>()->GetIsReplicated());

	// 액터에 안 붙은 컴포넌트는 네트워크 역할이 없다 — 2층 검사가 쓰는 길이라 권위로 본다.
	TestTrue(TEXT("주인 없는 컴포넌트는 권위"), State->HasAnimalAuthority());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSnarehavenAnimalNetGuestIgnoresTest,
	"Snarehaven.Animal.Net.GuestAnimalIgnoresDamageAndCapture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSnarehavenAnimalNetGuestIgnoresTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	if (Animal == nullptr)
	{
		DestroyTestWorld(World);
		AddError(TEXT("동물 스폰 실패"));
		return false;
	}

	UAnimalStateComponent* State = Animal->GetAnimalState();

	// 먼저 호스트로서 포획 창까지 연다 (여기까진 지금까지와 같다).
	TestTrue(TEXT("호스트에서는 체력이 깎인다"), State->ApplyDamage(TestMaxHP * 0.5f));
	TestEqual(TEXT("포획 가능 상태"), State->GetState(), EAnimalState::Capturable);

	// 이제 같은 동물을 게스트 화면의 복제본처럼 만든다.
	Animal->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("게스트 복제본은 권위가 아니다"), State->HasAnimalAuthority());

	const float HPBefore = State->GetCurrentHP();
	TestFalse(TEXT("게스트에서 때려도 안 깎인다"), State->ApplyDamage(TestMaxHP));
	TestEqual(TEXT("체력 그대로"), State->GetCurrentHP(), HPBefore, TestTolerance);

	TestFalse(TEXT("게스트에서 포획해도 확정되지 않는다"), State->TryCapture());
	TestEqual(TEXT("상태 그대로"), State->GetState(), EAnimalState::Capturable);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSnarehavenCombatNetGuestRequestsTest,
	"Snarehaven.Combat.Net.GuestAttackAndCaptureOnlyRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSnarehavenCombatNetGuestRequestsTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	if (Player == nullptr || Animal == nullptr)
	{
		DestroyTestWorld(World);
		AddError(TEXT("플레이어·동물 스폰 실패"));
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	UCaptureComponent* Capture = Player->GetCapture();
	UAnimalStateComponent* State = Animal->GetAnimalState();

	// 게스트 화면: 내 폰은 AutonomousProxy, 동물은 호스트가 보내 준 복제본이다.
	Player->SetRole(ROLE_AutonomousProxy);
	Animal->SetRole(ROLE_SimulatedProxy);
	TestFalse(TEXT("게스트 폰은 공격 권위가 없다"), Melee->HasAttackAuthority());
	TestFalse(TEXT("게스트 폰은 포획 권위가 없다"), Capture->HasCaptureAuthority());

	// 하이라이트(대상 고르기)는 게스트에서도 돈다 — 그래야 HUD 가 이름·HP 를 띄운다.
	TestEqual(TEXT("게스트도 대상은 고른다"), Melee->RefreshTarget(), Animal);

	const float HPBefore = State->GetCurrentHP();
	TestFalse(TEXT("게스트 공격은 요청만 — 여기서 맞지 않는다"), Melee->AttackOnce());
	TestEqual(TEXT("동물 체력 그대로"), State->GetCurrentHP(), HPBefore, TestTolerance);

	// 대상이 Capturable 이어도 게스트 쪽에서 확정되지 않는다 (호스트가 다시 판정한다).
	Animal->SetRole(ROLE_Authority);
	TestTrue(TEXT("호스트로 돌려 포획 창을 연다"), State->ApplyDamage(TestMaxHP * 0.5f));
	Animal->SetRole(ROLE_SimulatedProxy);

	TestTrue(TEXT("게스트에서도 포획 버튼은 켜진다"), Capture->CanCapture());
	TestFalse(TEXT("게스트 포획은 요청만 — 여기서 확정되지 않는다"), Capture->TryCaptureTarget());
	TestEqual(TEXT("동물 상태 그대로"), State->GetState(), EAnimalState::Capturable);

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
