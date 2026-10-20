// ROADMAP M1-07 2층 검사 — 동물의 보이는 몸이 코드가 아니라 행(`MeshRef`·`IdleAnimRef`)에서 오는지,
// 그리고 행이 비었을 때 자리표시 실린더가 그대로 남는지.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "SnarehavenCharacter.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// M1-07 에서 임포트한 진짜 에셋. 경로는 engines/unreal.md 폴더 구조·접두어 표 그대로.
	const TCHAR* PigMeshPath = TEXT("/Game/Game/Characters/Animals/Pig/SK_Animal_Pig.SK_Animal_Pig");
	const TCHAR* PigIdlePath = TEXT("/Game/Game/Characters/Animals/Pig/A_Animal_Pig_Idle.A_Animal_Pig_Idle");
	const TCHAR* PigDeathPath = TEXT("/Game/Game/Characters/Animals/Pig/A_Animal_Pig_Death.A_Animal_Pig_Death");
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* PigRowName = TEXT("Animal_Pig");

	// DESIGN 3.1 「동물 몸 세로(캡슐 높이)」. 검사 쪽에 따로 적어 둬야 코드 상수가 바뀌면 여기서 걸린다.
	constexpr float DesignAnimalHeight = 90.f;

	// DESIGN 3.1 「동물 도주 속도」 = 45cm/s (감독 결정 2026-09-23, 루프#153 답 — 250 -> 140 -> 45).
	// 위와 같은 이유로 여기 따로 적는다 — 코드나 데이터에서 읽어 비교하면 검사가 제 꼬리를 무는 셈이 된다.
	constexpr float DesignAnimalMoveSpeed = 45.f;

	// DESIGN 3.1 「동물 도주 이탈 한계」 = 500cm. 같은 이유로 여기 따로 적는다.
	constexpr float DesignAnimalFleeLeash = 500.f;

	// 감독 결함 2026-09-23 18:55 이 건 합격 조건 그대로 — 몸 높이가 DESIGN 8.7 표 값 ±2cm.
	// (임포트 배율이 한 번 더 굽는 자리라 소수점 반올림만큼은 벌어진다 — `verify_animal_species.py` 는 ±1cm)
	constexpr float DesignSizeToleranceCm = 2.f;

	// 아래 셋은 **검사 전용 값**이다 (Verification.md 6) — 밸런스 숫자가 바뀌어도 안 깨지게
	// 설계서 값(공격 데미지 20 등)을 쓰지 않는다. 한 대는 "임계값보다 훨씬 작은 아무 값".
	constexpr float TestNickDamage = 1.f;
	constexpr float TestDeltaSeconds = 0.1f;
	constexpr int32 TestTickCount = 10;

	// 사람을 동물에서 이만큼 떨어뜨려 놓는다 — 방향만 보므로 거리 자체에 뜻은 없다.
	constexpr float TestThreatDistance = 400.f;

	/**
	 * 이동 컴포넌트까지 한 프레임 돌린다. 손으로 만든 테스트 월드는 컴포넌트 틱을 등록하지 않는다.
	 * 간격을 받는 이유는 도주 한계 검사뿐이다 — 거기선 실제 프레임만큼 잘게 밟아야 한다(아래 주석).
	 */
	void TickAnimalOnce(AAnimalCharacter& Animal, float DeltaSeconds = TestDeltaSeconds)
	{
		Animal.Tick(DeltaSeconds);
		// 틱 함수 인자는 엔진에서 상위 호출로만 넘겨져 nullptr 로 안전 (SnarehavenCharacterTest 와 같은 길).
		Animal.GetCharacterMovement()->TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
	}

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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalBodyFromRowTest, "Snarehaven.Animal.Body.RowMeshReplacesPlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalBodyFromRowTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>();
	if (Animal == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	// 자리표시가 먼저 보이는 상태여야 이 검사가 뜻이 있다 (없으면 3층 캡처에 아무것도 안 찍힌다).
	TestTrue(TEXT("처음엔 자리표시가 보인다"), Animal->GetPlaceholderBody()->IsVisible());
	TestNull(TEXT("처음엔 스켈레탈 메시가 없다"), ToRawPtr(Animal->GetMesh()->GetSkeletalMeshAsset()));

	FAnimalRow Row;
	Row.MeshRef = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(PigMeshPath));
	Row.IdleAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(PigIdlePath));

	TestTrue(TEXT("행에 적힌 메시를 올렸다"), Animal->ApplyBodyFromRow(Row));
	TestNotNull(TEXT("스켈레탈 메시가 붙었다"), ToRawPtr(Animal->GetMesh()->GetSkeletalMeshAsset()));
	// 진짜 몸이 올라오면 자리표시는 내려간다 — 둘 다 보이면 실린더가 돼지를 뚫고 나온다.
	TestFalse(TEXT("자리표시는 숨었다"), Animal->GetPlaceholderBody()->IsVisible());
	// 애님 블루프린트가 없어도 Idle 이 돌게 SingleNode 모드로 건다(루프#46).
	TestEqual(TEXT("한 클립을 돌리는 모드"), Animal->GetMesh()->GetAnimationMode(), EAnimationMode::AnimationSingleNode);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalBodyEmptyRowTest, "Snarehaven.Animal.Body.EmptyRowKeepsPlaceholder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalBodyEmptyRowTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>();
	if (Animal == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	// 메시가 아직 없는 종(행이 빈 참조)은 자리표시 실린더가 계속 몸이어야 한다.
	const FAnimalRow EmptyRow;
	TestFalse(TEXT("올릴 메시가 없으면 false"), Animal->ApplyBodyFromRow(EmptyRow));
	TestTrue(TEXT("자리표시는 그대로 보인다"), Animal->GetPlaceholderBody()->IsVisible());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalBodyTableWiringTest, "Snarehaven.Animal.Body.PigRowPointsAtImportedAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalBodyTableWiringTest::RunTest(const FString& Parameters)
{
	// 액터가 아니라 **표**가 임포트한 에셋을 가리키는지 본다 — 경로가 틀리면 3층에서만 드러난다.
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (Table == nullptr)
	{
		AddError(FString::Printf(TEXT("동물 테이블을 못 찾았다: %s"), AnimalTablePath));
		return false;
	}

	const FAnimalRow* Row = Table->FindRow<FAnimalRow>(PigRowName, TEXT("FSnarehavenAnimalBodyTableWiringTest"));
	if (Row == nullptr)
	{
		AddError(FString::Printf(TEXT("돼지 행을 못 찾았다: %s"), PigRowName));
		return false;
	}

	TestEqual(TEXT("행의 메시가 임포트한 돼지"), Row->MeshRef.ToSoftObjectPath().ToString(), FString(PigMeshPath));
	TestEqual(TEXT("행의 Idle 이 임포트한 클립"), Row->IdleAnimRef.ToSoftObjectPath().ToString(), FString(PigIdlePath));
	TestEqual(TEXT("행의 Death 가 임포트한 클립"), Row->DeathAnimRef.ToSoftObjectPath().ToString(), FString(PigDeathPath));
	TestNotNull(TEXT("죽음 클립 경로가 실제로 열린다"), Row->DeathAnimRef.LoadSynchronous());
	TestNotNull(TEXT("경로가 실제로 열린다"), Row->MeshRef.LoadSynchronous());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalDeathClipTest, "Snarehaven.Animal.Body.DeadStateSwapsClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalDeathClipTest::RunTest(const FString& Parameters)
{
	// M1-09 — 애님 블루프린트 없이 C++ 이 클립을 갈아 끼우는지(DESIGN 3.1 의 (c)).
	// 3층 캡처는 "죽은 자세로 누워 있다" 를 눈으로 보지만, **클립이 실제로 바뀌었는지**는 여기서만 볼 수 있다.
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>();
	if (Animal == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, PigIdlePath);
	UAnimSequence* Death = LoadObject<UAnimSequence>(nullptr, PigDeathPath);
	if (Idle == nullptr || Death == nullptr)
	{
		AddError(TEXT("돼지 Idle·Death 클립을 못 불러왔다"));
		DestroyTestWorld(World);
		return false;
	}

	FAnimalRow Row;
	Row.MaxHP = 10.f;
	Row.CaptureThresholdPct = 0.f;
	Row.MeshRef = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(PigMeshPath));
	Row.IdleAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(PigIdlePath));
	Row.DeathAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(PigDeathPath));

	Animal->ApplyBodyFromRow(Row);
	Animal->GetAnimalState()->InitializeFromRow(Row);

	UAnimSingleNodeInstance* Node = Animal->GetMesh()->GetSingleNodeInstance();
	if (Node == nullptr)
	{
		AddError(TEXT("SingleNode 인스턴스가 없다 — 클립을 갈아 끼울 자리가 아예 없다"));
		DestroyTestWorld(World);
		return false;
	}
	TestEqual(TEXT("살아 있을 땐 Idle 이 걸려 있다"), Node->GetAnimationAsset(), static_cast<UAnimationAsset*>(Idle));

	// 테스트 전용 숫자로 죽인다 — 설계의 돼지 HP 에 기대지 않는다(행을 직접 만들었다).
	Animal->GetAnimalState()->ApplyDamage(999.f);
	TestEqual(TEXT("상태가 죽음이다"), Animal->GetAnimalState()->GetState(), EAnimalState::Dead);

	// `PlayAnimation` 은 인스턴스를 새로 만들 수 있다 — 갈아 끼운 뒤의 것을 다시 꺼내 본다.
	Node = Animal->GetMesh()->GetSingleNodeInstance();
	TestEqual(TEXT("죽으면 죽음 클립으로 갈아 끼운다"), Node->GetAnimationAsset(), static_cast<UAnimationAsset*>(Death));
	// 반복하면 죽은 동물이 쓰러지는 동작을 계속 되풀이한다 — 끝 프레임에서 멈춰야 한다.
	TestFalse(TEXT("죽음 클립은 반복하지 않는다"), Node->IsLooping());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalCapturedHidesTest, "Snarehaven.Animal.Body.CapturedLeavesTheField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalCapturedHidesTest::RunTest(const FString& Parameters)
{
	// M1-10e2 — 포획 성공과 죽음이 화면에서 똑같아 보이던 문제(감독 재현 2026-09-20 13:00 (3)).
	// 3층 캡처로는 "안 보인다" 까지만 보이고, 충돌까지 꺼졌는지는 여기서만 볼 수 있다.
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>();
	if (Animal == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	// 테스트 전용 숫자다 — 돼지 행의 설계 숫자에 기대지 않는다(Verification.md 7).
	FAnimalRow Row;
	Row.MaxHP = 10.f;
	Row.CaptureThresholdPct = 50.f;
	UAnimalStateComponent* State = Animal->GetAnimalState();
	State->InitializeFromRow(Row);

	TestFalse(TEXT("살아 있는 동물은 보인다"), Animal->IsHidden());
	TestTrue(TEXT("살아 있는 동물은 공격이 닿는다"), Animal->GetActorEnableCollision());

	State->ApplyDamage(5.f);
	TestEqual(TEXT("임계값에 닿아 포획 가능이 됐다"), State->GetState(), EAnimalState::Capturable);
	TestFalse(TEXT("포획 전에는 아직 보인다"), Animal->IsHidden());

	TestTrue(TEXT("포획에 성공했다"), State->TryCapture());
	TestEqual(TEXT("상태가 포획됨이다"), State->GetState(), EAnimalState::Captured);
	TestTrue(TEXT("포획하면 사냥터에서 사라진다"), Animal->IsHidden());
	TestFalse(TEXT("사라진 몸에는 공격이 닿지 않는다"), Animal->GetActorEnableCollision());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalDeadStaysVisibleTest, "Snarehaven.Animal.Body.DeadStaysOnTheField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalDeadStaysVisibleTest::RunTest(const FString& Parameters)
{
	// 숨기기를 죽음에까지 걸면 죽음 애니가 보일 자리가 없어진다 (감독 지시 (c): "Dead 는 지금처럼 죽음 애니").
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>();
	if (Animal == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	FAnimalRow Row;
	Row.MaxHP = 10.f;
	Row.CaptureThresholdPct = 0.f;
	UAnimalStateComponent* State = Animal->GetAnimalState();
	State->InitializeFromRow(Row);

	State->ApplyDamage(999.f);
	TestEqual(TEXT("상태가 죽음이다"), State->GetState(), EAnimalState::Dead);
	TestFalse(TEXT("죽은 동물은 쓰러진 채로 보인다"), Animal->IsHidden());

	DestroyTestWorld(World);
	return true;
}

// ROADMAP M1-11a 2층 검사 — 동물 캡슐이 사람 캡슐을 그대로 물려받지 않는지.
// 사용자 실사용 지적("동물이 나보다 크다")의 원인이 ACharacter 기본 캡슐(176cm)이었다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalCapsuleSizeTest, "Snarehaven.Animal.Body.CapsuleIsAnimalSized",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalCapsuleSizeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>();
	if (Animal == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	const UCapsuleComponent* AnimalCapsule = Animal->GetCapsuleComponent();
	const float AnimalHeight = AnimalCapsule->GetUnscaledCapsuleHalfHeight() * 2.f;

	// DESIGN 3.1 표의 값을 그대로 적는다 — 코드의 상수를 읽어 비교하면 검사가 제 꼬리를 무는 셈이 된다.
	TestEqual(TEXT("동물 캡슐 세로가 DESIGN 3.1 의 90cm 다"), AnimalHeight, DesignAnimalHeight);

	// 사람 캡슐과 실제로 갈라졌는지 — 이 줄이 사용자 지적("동물이 나보다 크다")에 직접 대응한다.
	const UCapsuleComponent* PlayerCapsule = GetDefault<ASnarehavenCharacter>()->GetCapsuleComponent();
	const float PlayerHeight = PlayerCapsule->GetUnscaledCapsuleHalfHeight() * 2.f;
	TestTrue(TEXT("동물이 사람보다 낮다"), AnimalHeight < PlayerHeight);

	// 반지름이 세로와 같은 비율로 줄었는지 — 새 배율 상수를 코드에 박으면 이 비교가 어긋난다.
	const float ExpectedRadius = PlayerCapsule->GetUnscaledCapsuleRadius() * (DesignAnimalHeight / PlayerHeight);
	TestEqual(TEXT("반지름도 세로와 같은 비율로 줄었다"), AnimalCapsule->GetUnscaledCapsuleRadius(), ExpectedRadius, 0.01f);

	// 캡슐은 렌더되지 않는다 — 보이는 자리표시 실린더까지 따라 줄어야 화면에서 작아진 것이다.
	TestEqual(TEXT("자리표시 실린더가 캡슐 세로를 따라간다"),
		static_cast<float>(Animal->GetPlaceholderBody()->GetRelativeScale3D().Z),
		AnimalCapsule->GetUnscaledCapsuleHalfHeight() / 50.f, 0.001f);

	DestroyTestWorld(World);
	return true;
}

// ROADMAP M1-11b 2층 검사 — 맞은 동물이 사람에게서 멀어지는지, 그 속도가 코드가 아니라 표에서 오는지.
// 사용자 지적("동물이 조각상처럼 안 움직인다")의 나머지 반쪽이다 (DESIGN 3.1 「Fight 상태 이동」).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalFleeTest, "Snarehaven.Animal.Move.FightFleesFromPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalFleeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("동물 테이블을 불러왔다"), Table))
	{
		DestroyTestWorld(World);
		return false;
	}

	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	// 사람은 동물의 -X 쪽에 선다 — 그러면 도망 방향은 +X 여야 한다.
	ASnarehavenCharacter* Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector(-TestThreatDistance, 0.f, 0.f), FRotator::ZeroRotator);
	if (Animal == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·사람 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	Animal->SetAnimalData(Table, PigRowName);
	TestTrue(TEXT("표 행으로 초기화했다"), Animal->InitializeFromTable());

	// 속도는 코드가 아니라 `DT_AnimalTable` 행에서 온다 (DataDriven.md 3) — 이 줄이 그걸 확인한다.
	TestEqual(TEXT("도주 속도가 DESIGN 3.1 의 45cm/s"),
		Animal->GetCharacterMovement()->MaxWalkSpeed, DesignAnimalMoveSpeed);

	// 아직 아무도 안 건드렸다 — 가만히 서 있어야 한다.
	TestTrue(TEXT("Idle 이면 도망 방향이 없다"), Animal->GetFleeDirection().IsNearlyZero());

	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);
	TestEqual(TEXT("한 대 맞으면 Fight"), Animal->GetAnimalState()->GetState(), EAnimalState::Fight);

	const FVector FleeDirection = Animal->GetFleeDirection();
	TestTrue(TEXT("사람 반대쪽(+X)으로 도망간다"), FleeDirection.X > 0.9f);
	TestTrue(TEXT("위아래로는 안 도망간다"), FMath::IsNearlyZero(FleeDirection.Z));

	// 테스트 월드엔 바닥이 없다 — 낙하 대신 수평 이동만 보려고 비행 모드로 (SnarehavenCharacterTest 와 같다).
	Animal->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	const float DistanceBefore = FVector::Dist(Animal->GetActorLocation(), Player->GetActorLocation());
	for (int32 Step = 0; Step < TestTickCount; ++Step)
	{
		TickAnimalOnce(*Animal);
	}
	TestTrue(TEXT("Fight 동안 사람에게서 실제로 멀어진다"),
		FVector::Dist(Animal->GetActorLocation(), Player->GetActorLocation()) > DistanceBefore);

	// DESIGN 3.1 「Capturable 진입 시 즉시 정지」 — 포획 직전에 도망가면 좌절 요소가 된다.
	// 임계값까지 정확히 깎는다: 설계 숫자(데미지 20)를 쓰지 않고 표가 말하는 만큼만 깎는다.
	UAnimalStateComponent* State = Animal->GetAnimalState();
	State->ApplyDamage(State->GetCurrentHP() - State->GetCaptureThresholdHP());
	TestEqual(TEXT("임계값에 닿으면 Capturable"), State->GetState(), EAnimalState::Capturable);
	TestTrue(TEXT("그 자리에서 즉시 멈춘다"), Animal->GetVelocity().IsNearlyZero());
	TestTrue(TEXT("Capturable 이면 도망 방향이 없다"), Animal->GetFleeDirection().IsNearlyZero());

	const FVector StoppedLocation = Animal->GetActorLocation();
	for (int32 Step = 0; Step < TestTickCount; ++Step)
	{
		TickAnimalOnce(*Animal);
	}
	TestTrue(TEXT("멈춘 뒤로는 자리가 안 바뀐다"),
		Animal->GetActorLocation().Equals(StoppedLocation, 1.f));

	DestroyTestWorld(World);
	return true;
}

// 사용자 실사용 결함 2026-09-22 ① 2층 검사 — 맞은 동물이 **맵 밖으로 직선 도주하지 않는지**.
// 고친 뒤에도 도망은 가야 하므로 "안 움직인다" 로 통과되지 않게 두 방향을 같이 본다:
// 한계 안에 남는가(위) · 한계 근처까지 실제로 갔는가(아래).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalFleeLeashTest, "Snarehaven.Animal.Move.FleeStaysWithinLeash",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalFleeLeashTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("동물 테이블을 불러왔다"), Table))
	{
		DestroyTestWorld(World);
		return false;
	}

	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	ASnarehavenCharacter* Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector(-TestThreatDistance, 0.f, 0.f), FRotator::ZeroRotator);
	if (Animal == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·사람 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	Animal->SetAnimalData(Table, PigRowName);
	TestTrue(TEXT("표 행으로 초기화했다"), Animal->InitializeFromTable());
	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);
	TestEqual(TEXT("한 대 맞으면 Fight"), Animal->GetAnimalState()->GetState(), EAnimalState::Fight);

	// 테스트 월드엔 바닥이 없다 — 낙하 대신 수평 이동만 본다 (위 검사와 같은 길).
	Animal->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	const FVector FightStart = Animal->GetActorLocation();

	// **틱 간격을 실제 프레임(1/60)으로 잡는다.** 경계에 닿은 뒤 접선으로 도는 동안 한 걸음마다
	// 걸음길이²/(2×반지름)만큼 바깥으로 밀린다(원을 직선 조각으로 도는 기하) — 0.1초 걸음이면 그 값이
	// 눈에 띄고, 게임이 실제로 밟는 1/60 걸음이면 초당 1cm 아래다. 검사가 재야 하는 것은 게임 쪽이다.
	constexpr float FrameSeconds = 1.f / 60.f;
	// 한계(500cm)를 훌쩍 넘길 만큼 오래 — 45cm/s 로 30초면 막는 것이 없을 때 1350cm 다.
	// **도주 속도가 140 -> 45 로 내려가며 10초(=450cm)로는 한계에 닿지도 못하게 됐다**(루프#154):
	// 시간을 늘리지 않으면 이 검사가 "한계를 지킨다" 를 잰 적 없이 조용히 통과한다.
	constexpr int32 ThirtySecondsOfFrames = 1800;
	for (int32 Step = 0; Step < ThirtySecondsOfFrames; ++Step)
	{
		TickAnimalOnce(*Animal, FrameSeconds);
	}

	const float TravelledFromFight = FVector::Dist2D(Animal->GetActorLocation(), FightStart);
	// 접선 회전의 바깥 밀림 + 한 걸음 넘침을 감안한 여유. 막는 것이 없을 때의 1350cm 와는 자릿수가 다르다.
	constexpr float LeashTolerance = 50.f;
	TestTrue(
		FString::Printf(TEXT("30초를 도망쳐도 싸움 시작 자리에서 %.0fcm 안이다 (실제 %.0fcm)"),
			DesignAnimalFleeLeash + LeashTolerance, TravelledFromFight),
		TravelledFromFight <= DesignAnimalFleeLeash + LeashTolerance);

	// 반대쪽 — 한계를 "안 움직이게 만들어" 지키면 사용자가 지적했던 조각상으로 되돌아간다.
	TestTrue(
		FString::Printf(TEXT("그래도 한계 근처까지는 실제로 도망갔다 (실제 %.0fcm)"), TravelledFromFight),
		TravelledFromFight >= DesignAnimalFleeLeash * 0.9f);

	// 경계에 선 지금, 도망 방향은 바깥이 아니라 **경계를 따라**여야 한다.
	TestTrue(TEXT("경계에 닿았다고 스스로 안다"), Animal->IsBeyondFleeLeash());
	const FVector OutDirection = (Animal->GetActorLocation() - FightStart).GetSafeNormal2D();
	const FVector FleeDirection = Animal->GetFleeDirection();
	TestFalse(TEXT("경계에서도 멈추지 않는다"), FleeDirection.IsNearlyZero());
	TestTrue(TEXT("바깥으로 나가는 성분이 없다"),
		FVector::DotProduct(FleeDirection, OutDirection) <= KINDA_SMALL_NUMBER);

	// **부딪혀 속도가 죽어도 다음 걸음에 도주 속도를 되찾는다** (루프#175). 경계를 도는 동안
	// `ApplyLeashToVelocity` 가 지켜야 할 크기는 지금 실린 속도가 아니라 **도주 속도**다
	// (DESIGN 3.1 「속도를 깎지 않고 방향만 돌린다」). 죽은 값을 그대로 다시 실으면 한 번 부딪힌
	// 동물이 스스로 못 빠져나온다 — 추격자에 눌려 둘이 함께 멈춘 결함(루프#173)의 절반이 이것이다.
	// 부딪힐 몸을 세우는 대신 속도를 0 으로 직접 내려 같은 상황을 만든다(막는 것이 없어 결과가 한결같다).
	Animal->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Animal->Tick(FrameSeconds);
	const float RestoredSpeed = Animal->GetCharacterMovement()->Velocity.Size2D();
	TestTrue(
		FString::Printf(TEXT("속도가 0 으로 죽어도 한 걸음에 도주 속도를 되찾는다 (%.1f / %.1fcm/s)"),
			RestoredSpeed, DesignAnimalMoveSpeed),
		RestoredSpeed >= DesignAnimalMoveSpeed * 0.95f);

	DestroyTestWorld(World);
	return true;
}

// INBOX 결함(루프#173, 2026-09-24 03:40) 2층 검사 — **추격자가 등에 붙으면 둘이 함께 멈춘다.**
// 3층 로그에서 거리가 51.5cm(두 캡슐 반지름의 합)에 고정된 채 동물 8.8 · 플레이어 0.3cm/s 로
// 3초 동안 같이 죽었다. 검증 조건은 결함 줄이 쓴 그대로 — **등에 붙은 채 10초 동안 양쪽 속도가
// 도주 속도의 0.5배 아래로 안 떨어진다.** 한쪽만 보면 "동물을 통과시키는" 눈속임도 통과하므로
// 그동안 추격자가 실제로 등에 붙어 있었는지(최대 거리)도 같이 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalChaserJamTest, "Snarehaven.Animal.Move.ChaserOnBackKeepsBothMoving",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalChaserJamTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("동물 테이블을 불러왔다"), Table))
	{
		DestroyTestWorld(World);
		return false;
	}

	// **여기서만 바닥을 깐다.** 다른 이동 검사는 날아서 재지만(바닥 없는 월드), 이 결함은 두 몸이
	// 땅을 딛고 서로 밀 때 났다 — 걷기(MOVE_Walking)의 바닥 붙임·파고듦 해소가 증상의 일부다.
	AStaticMeshActor* const Floor = World->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, -50.f), FRotator::ZeroRotator);
	UStaticMesh* const CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Floor == nullptr || CubeMesh == nullptr)
	{
		AddError(TEXT("검사용 바닥을 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
	// 엔진 기본 큐브는 한 변 100cm·중심이 원점 — 200배로 펴면 20000cm 짜리 판이고 윗면이 Z=0 이다.
	Floor->GetStaticMeshComponent()->SetWorldScale3D(FVector(200.f, 200.f, 1.f));

	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(
		FVector(0.f, 0.f, DesignAnimalHeight * 0.5f), FRotator::ZeroRotator);
	if (Animal == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>(
		FVector(-TestThreatDistance, 0.f, Animal->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.f),
		FRotator::ZeroRotator);
	if (Player == nullptr)
	{
		AddError(TEXT("사람 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	Animal->SetAnimalData(Table, PigRowName);
	TestTrue(TEXT("표 행으로 초기화했다"), Animal->InitializeFromTable());
	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);
	TestEqual(TEXT("한 대 맞으면 Fight"), Animal->GetAnimalState()->GetState(), EAnimalState::Fight);

	// 도주 한계를 재는 기준점은 **싸움이 시작된 자리**다 (`AAnimalCharacter::HandleStateChanged`).
	const FVector FleeHome = Animal->GetActorLocation();

	UCharacterMovementComponent* const AnimalMove = Animal->GetCharacterMovement();
	UCharacterMovementComponent* const PlayerMove = Player->GetCharacterMovement();
	// 검사 월드에서 스폰한 사람은 이동 모드가 `MOVE_None` 으로 남는다(조종 컨트롤러가 없어
	// `SetDefaultMovementMode` 가 안 선다) — 걷게 해 줘야 쫓아온다. 동물은 제 생성자가 이미 걷는다.
	PlayerMove->SetMovementMode(MOVE_Walking);
	// 사람은 동물보다 빨라야 등에 붙는다 — **검사 전용 값**(달리기 실측이 바뀌어도 안 깨지게 배수로).
	constexpr float TestChaserSpeedRatio = 2.f;
	PlayerMove->MaxWalkSpeed = DesignAnimalMoveSpeed * TestChaserSpeedRatio;
	// 검사 월드의 사람에겐 조종 컨트롤러가 없다 — 엔진이 조종되지 않는 캐릭터의 이동을 통째로
	// 건너뛰므로(`bShouldPerformControlledCharMove`) 동물과 같은 플래그로 물리를 돌린다.
	PlayerMove->bRunPhysicsWithNoController = true;

	// 게임이 실제로 밟는 걸음(1/60초)으로 재야 두 캡슐이 부딪히는 프레임이 게임과 같아진다.
	constexpr float FrameSeconds = 1.f / 60.f;
	auto TickBoth = [&]()
	{
		// 사람은 늘 동물 쪽으로 조이스틱을 민 채다 — 3층의 `-SnareChase` 와 같은 입력이다.
		Player->AddMovementInput((Animal->GetActorLocation() - Player->GetActorLocation()).GetSafeNormal2D());
		Animal->Tick(FrameSeconds);
		AnimalMove->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
		PlayerMove->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
	};

	// 결함은 **경계를 도는 동안** 났다 — 한계에 닿을 때까지 쫓기고 나서부터 잰다.
	constexpr int32 MaxFramesToReachLeash = 3600;
	int32 FramesToLeash = 0;
	while (!Animal->IsBeyondFleeLeash() && FramesToLeash < MaxFramesToReachLeash)
	{
		TickBoth();
		++FramesToLeash;
	}
	if (!TestTrue(TEXT("한계까지 실제로 쫓겨 갔다"), Animal->IsBeyondFleeLeash()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// **앞을 막는 것을 세운다.** 사냥터에는 나무·바위가 있고, 등 뒤에서 사람이 밀 때 앞이 막히면
	// 동물은 양쪽에 끼인다 — 감독 PIE 가 본 3초짜리 멈춤이 이 모양이다(둘 다 속도가 죽었다가
	// 저절로 풀린다). 동물이 도는 쪽 바로 앞에 세워 확실히 부딪히게 한다.
	// 경계에 막 닿은 순간의 속도는 아직 바깥쪽(반지름 방향)이다 — 접선으로 돌아설 때까지 몇 걸음 둔 뒤
	// **그때 가려는 쪽**(`GetFleeDirection`)으로 세워야 실제로 부딪힌다.
	constexpr int32 SettleFrames = 30;
	for (int32 Step = 0; Step < SettleFrames; ++Step)
	{
		TickBoth();
	}
	const FVector AnimalHeading = Animal->GetFleeDirection().GetSafeNormal2D();
	constexpr float BlockerAheadCm = 60.f;
	constexpr float BlockerSideCm = 60.f;
	AStaticMeshActor* const Blocker = World->SpawnActor<AStaticMeshActor>(
		Animal->GetActorLocation() + AnimalHeading * BlockerAheadCm + FVector(0.f, 0.f, BlockerSideCm * 0.5f),
		FRotator::ZeroRotator);
	if (Blocker == nullptr)
	{
		AddError(TEXT("검사용 막는 것을 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}
	Blocker->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Blocker->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
	Blocker->GetStaticMeshComponent()->SetWorldScale3D(FVector(BlockerSideCm * 0.01f));

	constexpr int32 TenSecondsOfFrames = 600;
	// **한 프레임짜리 0 은 결함이 아니다.** 맞닿은 두 캡슐이 서로를 미는 프레임에는 엔진이 실제 변위로
	// 속도를 다시 재므로 그 프레임 속도는 0 이 된다 — 사람이 본 결함은 그게 **몇 초씩** 이어진 것이다.
	// 그래서 한 프레임 최저값이 아니라 **연속으로 멈춰 있던 시간**과 10초 평균으로 본다.
	const float StillSpeed = DesignAnimalMoveSpeed * 0.5f;
	int32 LongestAnimalStill = 0;
	int32 LongestPlayerStill = 0;
	int32 RunningAnimalStill = 0;
	int32 RunningPlayerStill = 0;
	float PlayerStillToBlocker = 0.f;
	float PlayerStillToAnimal = 0.f;
	float FarthestApart = 0.f;
	float FarthestFromHome = 0.f;
	double AnimalSpeedSum = 0.0;
	double PlayerSpeedSum = 0.0;
	for (int32 Step = 0; Step < TenSecondsOfFrames; ++Step)
	{
		TickBoth();

		const float AnimalSpeed = Animal->GetVelocity().Size2D();
		const float PlayerSpeed = Player->GetVelocity().Size2D();
		AnimalSpeedSum += AnimalSpeed;
		PlayerSpeedSum += PlayerSpeed;

		RunningAnimalStill = AnimalSpeed < StillSpeed ? RunningAnimalStill + 1 : 0;
		RunningPlayerStill = PlayerSpeed < StillSpeed ? RunningPlayerStill + 1 : 0;
		LongestAnimalStill = FMath::Max(LongestAnimalStill, RunningAnimalStill);
		if (RunningPlayerStill > LongestPlayerStill)
		{
			// 사람이 가장 오래 멈춘 순간 **무엇에 막혀 있었는지**를 같이 적어 둔다 — 동물이 아니라
			// 막는 것에 붙어 있었다면 그건 이 검사의 사람이 조향을 안 하기 때문이다(아래 판정 주석).
			LongestPlayerStill = RunningPlayerStill;
			PlayerStillToBlocker = FVector::Dist2D(Player->GetActorLocation(), Blocker->GetActorLocation());
			PlayerStillToAnimal = FVector::Dist2D(Player->GetActorLocation(), Animal->GetActorLocation());
		}

		FarthestApart = FMath::Max(FarthestApart,
			FVector::Dist2D(Animal->GetActorLocation(), Player->GetActorLocation()));
		FarthestFromHome = FMath::Max(FarthestFromHome, FVector::Dist2D(Animal->GetActorLocation(), FleeHome));
	}

	const float AnimalStillSeconds = LongestAnimalStill * FrameSeconds;
	const float PlayerStillSeconds = LongestPlayerStill * FrameSeconds;
	const float AnimalAverage = static_cast<float>(AnimalSpeedSum / TenSecondsOfFrames);
	const float PlayerAverage = static_cast<float>(PlayerSpeedSum / TenSecondsOfFrames);
	AddInfo(FString::Printf(
		TEXT("한계 도달 %.1f초 · 그 뒤 10초 동안 가장 길게 멈춰 있던 시간 동물 %.2f초 · 사람 %.2f초"),
		FramesToLeash * FrameSeconds, AnimalStillSeconds, PlayerStillSeconds));
	AddInfo(FString::Printf(
		TEXT("평균 속도 동물 %.1f · 사람 %.1fcm/s (도주 속도 %.0f) · 둘이 가장 멀어진 거리 %.1fcm · 싸움 자리에서 가장 멀어진 거리 %.1fcm"),
		AnimalAverage, PlayerAverage, DesignAnimalMoveSpeed, FarthestApart, FarthestFromHome));

	// **동물이 막힌 채로 굳지 않는다** — 결함 줄이 말한 "함께 멈춘다" 의 시작점이 동물 쪽이다.
	constexpr float AllowedStillSeconds = 0.5f;
	TestTrue(FString::Printf(TEXT("동물이 추격자에 눌려도 굳지 않는다 (가장 길게 %.2f초 / 봐주는 %.2f초)"),
			AnimalStillSeconds, AllowedStillSeconds),
		AnimalStillSeconds <= AllowedStillSeconds);
	TestTrue(FString::Printf(TEXT("동물이 10초 평균으로 도주 속도의 절반 넘게 간다 (%.1f / %.1fcm/s)"),
			AnimalAverage, StillSpeed),
		AnimalAverage >= StillSpeed);

	// 사람 쪽은 **평균**으로 본다. 이 검사의 사람은 조향을 안 하고 동물 쪽으로만 미는 손가락이라
	// (3층 `-SnareChase` 와 같은 입력) 동물이 바위를 돌아 빠져나가면 자기가 바위에 박혀 선다 —
	// 그건 사람이 벽을 밀고 있는 것이지 이 결함이 아니다. 어디에 막혀 있었는지는 위 줄에 적힌다.
	AddInfo(FString::Printf(TEXT("사람이 가장 오래 멈춘 순간 · 막는 것까지 %.1fcm · 동물까지 %.1fcm"),
		PlayerStillToBlocker, PlayerStillToAnimal));
	TestTrue(FString::Printf(TEXT("사람도 10초 평균으로 도주 속도의 절반 넘게 간다 (%.1f / %.1fcm/s)"),
			PlayerAverage, StillSpeed),
		PlayerAverage >= StillSpeed);

	// **그동안 추격자가 정말 등에 붙어 있었는가** — 안 그러면 "도망쳐서 안 부딪혔다" 가 통과로 보인다.
	// 두 캡슐 반지름의 합(3층 로그의 51.5cm)의 두 배 안이면 붙어 다닌 것으로 본다.
	const float ContactDistance =
		Animal->GetCapsuleComponent()->GetScaledCapsuleRadius() + Player->GetCapsuleComponent()->GetScaledCapsuleRadius();
	TestTrue(FString::Printf(TEXT("10초 동안 추격자가 등에 붙어 있었다 (가장 멀어진 %.1f / 붙은 거리 %.1fcm)"),
			FarthestApart, ContactDistance),
		FarthestApart <= ContactDistance * 2.f);

	// **끼임 풀이가 이탈이 되면 안 된다** — 막혀서 옆으로 빠지는 동안만 한계를 넘고, 넘은 만큼은
	// 바로 되돌아온다 (DESIGN 3.1 「끼였을 때의 여유」).
	constexpr float BlockedEscapeMarginCm = 60.f;
	TestTrue(FString::Printf(TEXT("끼임을 풀면서도 도주 한계 + 여유 안에 있다 (%.1f / %.0fcm)"),
			FarthestFromHome, DesignAnimalFleeLeash + BlockedEscapeMarginCm),
		FarthestFromHome <= DesignAnimalFleeLeash + BlockedEscapeMarginCm);

	DestroyTestWorld(World);
	return true;
}

// 행에 속도를 안 적은 종은 아예 안 움직인다 (Verification.md 7 — 미정 숫자는 기능을 켜지 않는다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalUndecidedSpeedTest, "Snarehaven.Animal.Move.UndecidedSpeedStaysStill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalUndecidedSpeedTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	ASnarehavenCharacter* Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector(-TestThreatDistance, 0.f, 0.f), FRotator::ZeroRotator);
	if (Animal == nullptr || Player == nullptr)
	{
		AddError(TEXT("동물·사람 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	// 검사 전용 행 — 체력 숫자는 설계서 값이 아니라 이 검사가 고른 값이다 (Verification.md 6).
	// `MoveSpeed` 는 손대지 않는다: 구조체 기본값이 곧 "아직 안 채움"(음수)이다.
	FAnimalRow Row;
	Row.MaxHP = 40.f;
	Row.CaptureThresholdPct = 25.f;
	TestTrue(TEXT("검사 전용 행으로 초기화했다"), Animal->GetAnimalState()->InitializeFromRow(Row));
	Animal->ApplyMoveSpeedFromRow(Row);

	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);
	TestEqual(TEXT("한 대 맞으면 Fight"), Animal->GetAnimalState()->GetState(), EAnimalState::Fight);
	TestTrue(TEXT("속도가 미정이면 도망 방향이 없다"), Animal->GetFleeDirection().IsNearlyZero());

	Animal->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	const FVector StartLocation = Animal->GetActorLocation();
	for (int32 Step = 0; Step < TestTickCount; ++Step)
	{
		TickAnimalOnce(*Animal);
	}
	TestTrue(TEXT("Fight 인데도 자리가 안 바뀐다"), Animal->GetActorLocation().Equals(StartLocation, 1.f));

	DestroyTestWorld(World);
	return true;
}


// 감독 실측 결함 2026-09-23 18:55 의 2층 검사 — **전 종 몸 높이가 똑같이 90.0cm** 였다.
// 원인은 이 파일이 아니라 `AAnimalCharacter::ApplyBodyFromRow` 가 메시를 **캡슐 높이**(전 종 공통
// 90cm)에 맞춰 굽던 것이다. 그래서 검사도 "행을 실제로 적용한 뒤 선 몸의 높이" 를 잰다 —
// 표 칸만 보면 코드가 또 굽어도 모른다 (PROMPT ③-12).
//
// **기대값은 이 파일에 하나도 없다** — DESIGN 8.7 "크기" 표를 읽어서 맞댄다. 표를 고치면 검사가
// 따라 움직이고, 표와 다르게 굽으면 여기서 운다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalBodySizeTest, "Snarehaven.Animal.Body.SizeMatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalBodySizeTest::RunTest(const FString& Parameters)
{
	FString Doc;
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
	if (!TestTrue(FString::Printf(TEXT("설계서를 읽었다 (%s)"), *DocPath),
		FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}

	// `### 8.7 …` 부터 다음 제목 전까지만 본다 — 다른 절의 표까지 긁으면 엉뚱한 값이 섞인다.
	const FString Heading = TEXT("### 8.7 ");
	const int32 SectionStart = Doc.Find(Heading, ESearchCase::CaseSensitive);
	if (!TestTrue(TEXT("DESIGN 8.7 절을 찾았다"), SectionStart != INDEX_NONE))
	{
		return false;
	}
	const int32 NextHeading = Doc.Find(TEXT("\n### "), ESearchCase::CaseSensitive,
		ESearchDir::FromStart, SectionStart + Heading.Len());
	const FString Section = NextHeading == INDEX_NONE
		? Doc.Mid(SectionStart)
		: Doc.Mid(SectionStart, NextHeading - SectionStart);

	// `| 여우 | 0.9 | 32×90×61 | … |` → 종 이름과 높이(세 번째 숫자).
	TArray<FString> Lines;
	Section.ParseIntoArrayLines(Lines);
	const FRegexPattern SizeRowPattern(
		TEXT("^\\|\\s*([^|]+?)\\s*\\|\\s*[0-9.]+\\s*\\|\\s*[0-9.]+×[0-9.]+×([0-9.]+)\\s*\\|"));
	TMap<FString, double> DesignHeights;
	for (const FString& Line : Lines)
	{
		FRegexMatcher Matcher(SizeRowPattern, Line);
		if (Matcher.FindNext())
		{
			DesignHeights.Add(Matcher.GetCaptureGroup(1), FCString::Atod(*Matcher.GetCaptureGroup(2)));
		}
	}

	// 같은 절의 "플레이어 키 176cm" — 어떤 종도 이 위로는 못 간다(8.7 크기 원칙 ①).
	double PlayerHeightCm = 0.0;
	FRegexMatcher PlayerMatcher(FRegexPattern(TEXT("플레이어 키 ([0-9.]+)cm")), Section);
	if (PlayerMatcher.FindNext())
	{
		PlayerHeightCm = FCString::Atod(*PlayerMatcher.GetCaptureGroup(1));
	}

	// 설계서를 한 줄도 못 읽었으면 아래 for 가 안 돌아 조용히 통과한다 — 그게 제일 위험하다.
	if (!TestTrue(TEXT("DESIGN 8.7 크기 표에서 종 줄을 읽었다"), DesignHeights.Num() > 0) ||
		!TestTrue(TEXT("DESIGN 8.7 에서 플레이어 키를 읽었다"), PlayerHeightCm > 0.0))
	{
		return false;
	}

	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), Table))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	int32 Checked = 0;
	for (const FName& RowName : Table->GetRowNames())
	{
		const FAnimalRow* Row = Table->FindRow<FAnimalRow>(RowName, TEXT("AnimalBodySizeTest"));
		// 자리표시 3종은 DESIGN 8.7 크기 표에 줄이 없다 — 그 셋은 여전히 캡슐 높이에 맞춰 굽는다.
		if (Row == nullptr || Row->bPlaceholderSpecies || Row->bStorySpecies || Row->MeshRef.IsNull())
		{
			continue;
		}

		const FString DisplayName = Row->DisplayName.ToString();
		if (!DesignHeights.Contains(DisplayName))
		{
			AddError(FString::Printf(TEXT("[%s] DESIGN 8.7 크기 표에 '%s' 줄이 없다"),
				*RowName.ToString(), *DisplayName));
			continue;
		}

		AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>();
		if (Animal == nullptr)
		{
			AddError(TEXT("동물 액터를 못 스폰했다"));
			break;
		}

		// 행 이름부터 심어 둔다 — 안 그러면 `ApplyBodyFromRow` 의 크기 로그가 기본값(돼지)으로 찍힌다.
		Animal->SetAnimalData(Table, RowName);
		if (!TestTrue(FString::Printf(TEXT("[%s] 행의 메시를 올렸다"), *RowName.ToString()),
			Animal->ApplyBodyFromRow(*Row)))
		{
			Animal->Destroy();
			continue;
		}

		// **선 몸의 높이** = 메시 바운드 높이 × 실제로 걸린 배율. 행 칸이 아니라 결과를 잰다.
		const USkeletalMeshComponent* Body = Animal->GetMesh();
		const FBoxSphereBounds MeshBounds = Body->GetSkeletalMeshAsset()->GetBounds();
		const float Scale = Body->GetRelativeScale3D().Z;
		const float StoodHeight = MeshBounds.BoxExtent.Z * 2.f * Scale;
		const double DesignHeight = DesignHeights[DisplayName];

		AddInfo(FString::Printf(TEXT("[%s] %s — 메시 %.1fcm × 배율 %.2f = 선 높이 %.1fcm (설계 %.0fcm)"),
			*RowName.ToString(), *DisplayName, MeshBounds.BoxExtent.Z * 2.f, Scale, StoodHeight, DesignHeight));

		TestEqual(*FString::Printf(TEXT("[%s] 몸 높이가 DESIGN 8.7 표와 같다"), *RowName.ToString()),
			StoodHeight, static_cast<float>(DesignHeight), DesignSizeToleranceCm);
		TestTrue(*FString::Printf(TEXT("[%s] 플레이어 키 %.0fcm 를 안 넘는다"),
			*RowName.ToString(), PlayerHeightCm), StoodHeight < PlayerHeightCm);

		// 발이 캡슐 바닥에 붙어 있는지 — 배율만 고치고 자리를 안 내리면 큰 종이 공중에 뜬다.
		const float CapsuleBottomZ = -Animal->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
		const float BodyBottomZ = Body->GetRelativeLocation().Z
			+ (MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z) * Scale;
		TestEqual(*FString::Printf(TEXT("[%s] 발이 캡슐 바닥에 있다"), *RowName.ToString()),
			BodyBottomZ, CapsuleBottomZ, DesignSizeToleranceCm);

		++Checked;
		Animal->Destroy();
	}
	DestroyTestWorld(World);

	// 종이 하나도 안 돌았으면 위의 통과는 전부 빈 통과다.
	TestEqual(TEXT("크기 표의 종 수만큼 실제로 재 봤다"), Checked, DesignHeights.Num());

	return true;
}


// 사용자 2026-09-25 "동물들이 자율적으로 움직이게" 2층 검사 — **아무도 안 건드린 동물이 혼자 걷다 서다 하고,
// 처음 선 자리 600cm 밖으로는 안 나간다** (DESIGN 3.1 🟡 「동물 배회」). 60초를 게임 걸음으로 돌린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalWanderTest, "Snarehaven.Animal.Move.IdleWanders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalWanderTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	AStaticMeshActor* const Floor = World->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, -50.f), FRotator::ZeroRotator);
	UStaticMesh* const CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	AAnimalCharacter* const Animal = World->SpawnActor<AAnimalCharacter>(
		FVector(0.f, 0.f, DesignAnimalHeight * 0.5f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("표"), Table) || Floor == nullptr || CubeMesh == nullptr || Animal == nullptr)
	{
		AddError(TEXT("검사 월드를 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
	Floor->GetStaticMeshComponent()->SetWorldScale3D(FVector(200.f, 200.f, 1.f));

	Animal->SetAnimalData(Table, PigRowName);
	TestTrue(TEXT("표 행으로 초기화했다"), Animal->InitializeFromTable());
	const FVector Home = Animal->GetActorLocation();
	UCharacterMovementComponent* const Move = Animal->GetCharacterMovement();

	constexpr float FrameSeconds = 1.f / 60.f;
	constexpr int32 SixtySecondsOfFrames = 3600;
	float MaxFromHome = 0.f;
	float PathCm = 0.f;
	int32 WalkStarts = 0;
	bool bWasWalking = false;
	FVector Last = Home;
	for (int32 Step = 0; Step < SixtySecondsOfFrames; ++Step)
	{
		Animal->Tick(FrameSeconds);
		Move->TickComponent(FrameSeconds, LEVELTICK_All, nullptr);
		const FVector Now = Animal->GetActorLocation();
		PathCm += FVector::Dist2D(Now, Last);
		Last = Now;
		MaxFromHome = FMath::Max(MaxFromHome, FVector::Dist2D(Now, Home));
		WalkStarts += (Animal->IsWanderWalking() && !bWasWalking) ? 1 : 0;
		bWasWalking = Animal->IsWanderWalking();
	}

	TestEqual(TEXT("건드리지 않았으니 Idle 그대로"), Animal->GetAnimalState()->GetState(), EAnimalState::Idle);
	TestTrue(FString::Printf(TEXT("60초에 걷기를 여러 번 시작했다 (%d번)"), WalkStarts), WalkStarts >= 3);
	TestTrue(FString::Printf(TEXT("실제로 걸어 다녔다 (누적 %.0fcm)"), PathCm), PathCm >= 100.f);
	TestTrue(FString::Printf(TEXT("집에서 600cm 밖으로 안 나갔다 (최대 %.0fcm)"), MaxFromHome), MaxFromHome <= 650.f);

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
