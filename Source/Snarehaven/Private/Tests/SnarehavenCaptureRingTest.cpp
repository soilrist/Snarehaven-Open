// ROADMAP M21-09a·09b·09c 2층 검사 — 올가미 조이기 링 판정(호스트)·실패 도주·게스트 길. DESIGN 3.1 「올가미 링」 줄들 · 명세 `docs/specs/snare-ring.md`.
// 보는 것: 판정 함수 경계 · 창 안 둘째 누름 → Captured · 이른 누름·시간 초과 → `timing` 실패·HP 그대로·Escaping ·
// 큰 종(MaxHP ≥ 기준)·변종은 좁은 창 · 링 길이 0 → 첫 누름에 즉시 포획 · 실제 표 행이 링을 켠다 ·
// 도주 시간이 차면 Capturable · 도주 중 버튼 꺼짐·맞으면 HP 만 깎이고 0 이면 Dead · 도주 0 → 그 자리 ·
// 게스트 경과가 허용 오차 안 → 게스트 시각으로 판정 · 밖 → `timing` · 링 없이 Resolve → 무시 · 게스트 링 시간 초과는 허용 오차만큼 늦게.
// 숫자는 전부 검사 전용 값이다(Verification.md 6) — 표 = DESIGN 맞대기는 `Snarehaven.Combat.Melee.TableMatchesDesign` 몫.
// 누름마다 던지기 몸짓·소리(M28-09c) · 올가미 클립이 성공·실패·받는 기기 모두에서 도는지.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Capture.Ring; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Animation/AnimSequence.h"
#include "Combat/CaptureComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Combat/SnareCaptureRing.h"
#include "Data/AnimalRow.h"
#include "Data/PlayerCombatRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SnarehavenCharacter.h"
#include "TimerManager.h"

namespace
{
	// 검사 전용 값 (Verification.md 6 — DESIGN 1.5·0.6·0.4·140 과 일부러 다르다).
	constexpr float TestRingSeconds = 2.f;
	constexpr float TestWindowSeconds = 0.5f;
	constexpr float TestWindowHardSeconds = 0.3f;
	constexpr float TestHardMinHP = 50.f;
	constexpr float TestEscapeSeconds = 1.f;
	constexpr float TestGuestToleranceSeconds = 0.2f;
	constexpr float TestSmallMaxHP = 20.f;
	constexpr float TestBigMaxHP = 60.f;
	constexpr float TestCaptureThresholdPct = 50.f;
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestTimerStepSeconds = 0.01f;
	constexpr float TestTolerance = 0.001f;
	const FName TestPetItemID = TEXT("item.test_ring_pet");
	const FName TestVariantPetItemID = TEXT("item.test_ring_pet_variant");

	const TCHAR* CombatTablePath = TEXT("/Game/Game/Data/Balance/DT_PlayerCombatTable.DT_PlayerCombatTable");
	const TCHAR* CombatRowName = TEXT("Player");

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

	AAnimalCharacter* SpawnTestAnimal(UWorld* World, float MaxHP)
	{
		AAnimalCharacter* Animal =
			World->SpawnActor<AAnimalCharacter>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			return nullptr;
		}

		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_ring");
		Row.MaxHP = MaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Row.PetItemID = TestPetItemID;
		Row.VariantPetItemID = TestVariantPetItemID;
		Animal->GetAnimalState()->InitializeFromRow(Row);
		return Animal;
	}

	/** 링 숫자를 검사 값으로 갈아 끼운 플레이어 (표에서 받은 값은 덮는다). */
	ASnarehavenCharacter* SpawnTestPlayer(UWorld* World, float RingSeconds = TestRingSeconds)
	{
		ASnarehavenCharacter* Player =
			World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Player == nullptr)
		{
			return nullptr;
		}

		Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
		Player->GetCapture()->SetCaptureRingSettings(RingSeconds, TestWindowSeconds, TestWindowHardSeconds, TestHardMinHP);
		Player->GetCapture()->SetCaptureEscapeSeconds(TestEscapeSeconds);
		Player->GetCapture()->SetCaptureRingTolerance(TestGuestToleranceSeconds);
		return Player;
	}

	/** 체력을 직접 깎아 포획 창을 연다 — 타수는 이 검사 몫이 아니다. */
	bool OpenCaptureWindow(ASnarehavenCharacter* Player, AAnimalCharacter* Animal)
	{
		UAnimalStateComponent* State = Animal->GetAnimalState();
		State->ApplyDamage(State->GetMaxHP() * (1.f - TestCaptureThresholdPct / 100.f));
		Player->GetMeleeAttack()->RefreshTarget();
		return State->GetState() == EAnimalState::Capturable && Player->GetCapture()->GetCaptureTarget() == Animal;
	}

	void AdvanceTimers(UWorld* World, float Seconds)
	{
		const int32 Steps = FMath::RoundToInt(Seconds / TestTimerStepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter; // 한 프레임에 두 번 틱하면 타이머 매니저가 그냥 돌아간다 (MeleeAttackTest 와 같은 까닭).
			World->GetTimerManager().Tick(TestTimerStepSeconds);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingJudgeTest, "Snarehaven.Capture.Ring.JudgeBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingJudgeTest::RunTest(const FString& Parameters)
{
	using SnareCaptureRing::EResult;
	using SnareCaptureRing::Judge;

	const float WindowStart = TestRingSeconds - TestWindowSeconds;
	TestTrue(TEXT("창보다 이르면 Early"), Judge(WindowStart - 0.01f, TestRingSeconds, TestWindowSeconds) == EResult::Early);
	TestTrue(TEXT("창 시작 = 성공"), Judge(WindowStart, TestRingSeconds, TestWindowSeconds) == EResult::InWindow);
	TestTrue(TEXT("창 한가운데 = 성공"), Judge(TestRingSeconds - TestWindowSeconds * 0.5f, TestRingSeconds, TestWindowSeconds) == EResult::InWindow);
	TestTrue(TEXT("링 끝 = 성공"), Judge(TestRingSeconds, TestRingSeconds, TestWindowSeconds) == EResult::InWindow);
	TestTrue(TEXT("링 끝을 넘으면 Timeout"), Judge(TestRingSeconds + 0.01f, TestRingSeconds, TestWindowSeconds) == EResult::Timeout);
	TestTrue(TEXT("0초 누름은 Early"), Judge(0.f, TestRingSeconds, TestWindowSeconds) == EResult::Early);
	TestTrue(TEXT("창이 링보다 길면 링 전체가 창"), Judge(0.f, TestRingSeconds, TestRingSeconds * 2.f) == EResult::InWindow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingInWindowTest, "Snarehaven.Capture.Ring.InWindowCaptures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingInWindowTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestSmallMaxHP);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("동물"), Animal)
		|| !TestTrue(TEXT("포획 창을 연다"), OpenCaptureWindow(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}

	UCaptureComponent* Capture = Player->GetCapture();
	AAnimalCharacter* StartedTarget = nullptr;
	float StartedWindow = -1.f;
	// 다이내믹 델리게이트라 람다를 못 붙인다 — 결과는 상태로 본다.
	TestFalse(TEXT("첫 누름은 링 시작 — 아직 안 잡는다"), Capture->TryCaptureTarget());
	TestTrue(TEXT("링이 돈다"), Capture->IsCaptureRingActive());
	StartedTarget = Capture->GetCaptureRingTarget();
	StartedWindow = Capture->GetCaptureWindowFor(StartedTarget);
	TestEqual(TEXT("링 대상 = 하이라이트 동물"), StartedTarget, Animal);
	TestEqual(TEXT("작은 종은 보통 창"), StartedWindow, TestWindowSeconds, TestTolerance);
	TestEqual(TEXT("첫 누름 뒤 상태 그대로"), Animal->GetAnimalState()->GetState(), EAnimalState::Capturable);

	const float Elapsed = TestRingSeconds - TestWindowSeconds * 0.5f;
	TestTrue(TEXT("창 안 둘째 누름이 잡는다"), Capture->ResolveCaptureRingAt(Elapsed));
	TestEqual(TEXT("상태가 Captured"), Animal->GetAnimalState()->GetState(), EAnimalState::Captured);
	TestFalse(TEXT("판정 뒤 링은 사라진다"), Capture->IsCaptureRingActive());
	AddInfo(FString::Printf(TEXT("[Ring] 창 안 %.2f초 → Captured (링 %.2f · 창 %.2f)"), Elapsed, TestRingSeconds, StartedWindow));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingEarlyTest, "Snarehaven.Capture.Ring.EarlyFailsKeepsHP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingEarlyTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestSmallMaxHP);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("동물"), Animal)
		|| !TestTrue(TEXT("포획 창을 연다"), OpenCaptureWindow(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}

	UCaptureComponent* Capture = Player->GetCapture();
	UAnimalStateComponent* State = Animal->GetAnimalState();
	const float HPBefore = State->GetCurrentHP();

	Capture->TryCaptureTarget();
	const float Elapsed = TestRingSeconds - TestWindowSeconds - 0.1f;
	TestFalse(TEXT("창보다 이른 둘째 누름은 실패"), Capture->ResolveCaptureRingAt(Elapsed));
	TestEqual(TEXT("실패 사유 timing"), Capture->GetLastCaptureFailureReason(), FString(TEXT("timing")));
	TestEqual(TEXT("HP 그대로"), State->GetCurrentHP(), HPBefore, TestTolerance);
	TestEqual(TEXT("실패하면 도주"), State->GetState(), EAnimalState::Escaping);
	TestFalse(TEXT("실패 뒤 링은 사라진다"), Capture->IsCaptureRingActive());

	// 도주가 끝나면 다시 누를 수 있고 새 링이 선다 (M21-09b).
	State->AdvanceEscape(TestEscapeSeconds);
	TestFalse(TEXT("다시 누름 = 새 링 시작"), Capture->TryCaptureTarget());
	TestTrue(TEXT("새 링이 돈다"), Capture->IsCaptureRingActive());
	AddInfo(FString::Printf(TEXT("[Ring] 이른 누름 %.2f초 → %s · HP %.1f 그대로"), Elapsed, *Capture->GetLastCaptureFailureReason(), State->GetCurrentHP()));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingTimeoutTest, "Snarehaven.Capture.Ring.TimeoutFails",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingTimeoutTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestSmallMaxHP);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("동물"), Animal)
		|| !TestTrue(TEXT("포획 창을 연다"), OpenCaptureWindow(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}

	UCaptureComponent* Capture = Player->GetCapture();
	UAnimalStateComponent* State = Animal->GetAnimalState();
	const float HPBefore = State->GetCurrentHP();

	Capture->TryCaptureTarget();
	AdvanceTimers(World, TestRingSeconds - 0.1f);
	TestTrue(TEXT("링 끝 전엔 아직 돈다"), Capture->IsCaptureRingActive());
	AdvanceTimers(World, 0.2f);
	TestFalse(TEXT("링 끝을 넘기면 링이 사라진다"), Capture->IsCaptureRingActive());
	TestEqual(TEXT("시간 초과 사유 timing"), Capture->GetLastCaptureFailureReason(), FString(TEXT("timing")));
	TestEqual(TEXT("HP 그대로"), State->GetCurrentHP(), HPBefore, TestTolerance);
	TestEqual(TEXT("시간 초과도 도주"), State->GetState(), EAnimalState::Escaping);
	AddInfo(FString::Printf(TEXT("[Ring] 시간 초과(%.2f초 링) → %s · 상태 Escaping"), TestRingSeconds, *Capture->GetLastCaptureFailureReason()));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingHardWindowTest, "Snarehaven.Capture.Ring.HardWindowForBigAndVariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingHardWindowTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Small = SpawnTestAnimal(World, TestSmallMaxHP);
	AAnimalCharacter* Big = SpawnTestAnimal(World, TestBigMaxHP);
	AAnimalCharacter* Variant = SpawnTestAnimal(World, TestSmallMaxHP);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("작은 종"), Small)
		|| !TestNotNull(TEXT("큰 종"), Big) || !TestNotNull(TEXT("변종"), Variant)
		|| !TestTrue(TEXT("변종으로 정한다"), Variant->GetAnimalState()->SetVariant(true)))
	{
		DestroyTestWorld(World);
		return false;
	}

	UCaptureComponent* Capture = Player->GetCapture();
	TestEqual(TEXT("작은 종 = 보통 창"), Capture->GetCaptureWindowFor(Small), TestWindowSeconds, TestTolerance);
	TestEqual(TEXT("MaxHP ≥ 기준 = 좁은 창"), Capture->GetCaptureWindowFor(Big), TestWindowHardSeconds, TestTolerance);
	TestEqual(TEXT("변종 = 좁은 창"), Capture->GetCaptureWindowFor(Variant), TestWindowHardSeconds, TestTolerance);

	// 보통 창 안·좁은 창 밖인 시각 — 큰 종은 실패, 작은 종은 성공.
	const float Elapsed = TestRingSeconds - (TestWindowSeconds + TestWindowHardSeconds) * 0.5f;
	Small->Destroy();
	Variant->Destroy();
	if (!TestTrue(TEXT("큰 종 포획 창을 연다"), OpenCaptureWindow(Player, Big)))
	{
		DestroyTestWorld(World);
		return false;
	}
	Capture->TryCaptureTarget();
	TestFalse(TEXT("큰 종은 좁은 창 밖이면 실패"), Capture->ResolveCaptureRingAt(Elapsed));
	TestEqual(TEXT("큰 종 실패 사유 timing"), Capture->GetLastCaptureFailureReason(), FString(TEXT("timing")));
	Big->GetAnimalState()->AdvanceEscape(TestEscapeSeconds); // 실패 도주(M21-09b)가 끝나야 다시 누른다.
	Capture->TryCaptureTarget();
	TestTrue(TEXT("큰 종도 좁은 창 안이면 잡힌다"), Capture->ResolveCaptureRingAt(TestRingSeconds - TestWindowHardSeconds * 0.5f));

	AAnimalCharacter* Small2 = SpawnTestAnimal(World, TestSmallMaxHP);
	if (!TestTrue(TEXT("작은 종 포획 창을 연다"), OpenCaptureWindow(Player, Small2)))
	{
		DestroyTestWorld(World);
		return false;
	}
	Capture->TryCaptureTarget();
	TestTrue(TEXT("작은 종은 같은 시각에 잡힌다"), Capture->ResolveCaptureRingAt(Elapsed));
	AddInfo(FString::Printf(TEXT("[Ring] 경과 %.2f초 — 큰 종 실패 · 작은 종 성공 (창 %.2f / 좁은 창 %.2f)"), Elapsed, TestWindowSeconds, TestWindowHardSeconds));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingZeroTest, "Snarehaven.Capture.Ring.ZeroLengthCapturesImmediately",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingZeroTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World, 0.f);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestSmallMaxHP);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("동물"), Animal)
		|| !TestTrue(TEXT("포획 창을 연다"), OpenCaptureWindow(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("링 길이 0 → 첫 누름에 즉시 포획"), Player->GetCapture()->TryCaptureTarget());
	TestFalse(TEXT("링은 서지 않는다"), Player->GetCapture()->IsCaptureRingActive());
	TestEqual(TEXT("상태가 Captured"), Animal->GetAnimalState()->GetState(), EAnimalState::Captured);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingTableTest, "Snarehaven.Capture.Ring.TableRowTurnsRingOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingTableTest::RunTest(const FString& Parameters)
{
	// 실제 표 행이 링을 켜는지만 본다 (값 = DESIGN 은 Melee 표 대조 시험 몫).
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, CombatTablePath);
	const FPlayerCombatRow* Row = Table != nullptr ? Table->FindRow<FPlayerCombatRow>(CombatRowName, TEXT("CaptureRingTest")) : nullptr;
	if (!TestNotNull(TEXT("DT_PlayerCombatTable Player 행"), Row))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어"), Player))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("표 행이 링을 켠다"), Player->GetCapture()->ApplyCombatRow(*Row));
	TestEqual(TEXT("캐릭터가 받은 링 길이 = 표"), Player->GetCapture()->GetCaptureRingSeconds(), Row->CaptureRingSeconds, TestTolerance);
	TestEqual(TEXT("캐릭터가 받은 실패 도주 = 표"), Player->GetCapture()->GetCaptureEscapeSeconds(), Row->CaptureEscapeSeconds, TestTolerance);
	TestEqual(TEXT("캐릭터가 받은 게스트 허용 오차 = 표"), Player->GetCapture()->GetCaptureRingToleranceSeconds(), Row->CaptureRingToleranceSeconds, TestTolerance);

	FPlayerCombatRow Undecided = *Row;
	Undecided.CaptureWindowSeconds = -1.f;
	TestFalse(TEXT("창이 미정이면 링을 끈다"), Player->GetCapture()->ApplyCombatRow(Undecided));
	TestEqual(TEXT("꺼진 링 길이 0"), Player->GetCapture()->GetCaptureRingSeconds(), 0.f, TestTolerance);
	AddInfo(FString::Printf(TEXT("[Ring] 표 링 %.2f초 · 창 %.2f · 좁은 창 %.2f · 큰 종 HP %.0f · 실패 도주 %.2f초 · 게스트 허용 오차 %.2f초"),
		Row->CaptureRingSeconds, Row->CaptureWindowSeconds, Row->CaptureWindowHardSeconds, Row->CaptureHardMinHP, Row->CaptureEscapeSeconds,
		Row->CaptureRingToleranceSeconds));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingEscapeTest, "Snarehaven.Capture.Ring.FailEscapesThenWaits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingEscapeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestSmallMaxHP);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("동물"), Animal)
		|| !TestTrue(TEXT("포획 창을 연다"), OpenCaptureWindow(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}

	UCaptureComponent* Capture = Player->GetCapture();
	UAnimalStateComponent* State = Animal->GetAnimalState();
	const float HPBefore = State->GetCurrentHP();

	// 실패 → Escaping, 남은 시간 = 도주 시간, 버튼 꺼짐.
	Capture->TryCaptureTarget();
	Capture->ResolveCaptureRingAt(0.f);
	TestEqual(TEXT("실패 → Escaping"), State->GetState(), EAnimalState::Escaping);
	TestEqual(TEXT("남은 도주 = 도주 시간"), State->GetEscapeSecondsLeft(), TestEscapeSeconds, TestTolerance);
	TestFalse(TEXT("도주 중 포획 버튼 꺼짐"), Capture->CanCapture());
	TestFalse(TEXT("도주 중 누름은 링을 안 건다"), Capture->TryCaptureTarget() || Capture->IsCaptureRingActive());
	TestEqual(TEXT("도주 중 누름 사유 escaping"), Capture->GetLastCaptureFailureReason(), FString(TEXT("escaping")));

	// 도주 중 맞으면 HP 만 깎이고 도주는 그대로.
	State->ApplyDamage(TestAttackDamage * 0.1f);
	TestEqual(TEXT("도주 중 맞으면 HP 깎임"), State->GetCurrentHP(), HPBefore - TestAttackDamage * 0.1f, TestTolerance);
	TestEqual(TEXT("맞아도 도주 그대로"), State->GetState(), EAnimalState::Escaping);

	// 시간이 차기 전엔 그대로, 차면 Capturable 로 서서 버튼이 켜진다.
	TestFalse(TEXT("시간 전엔 안 끝남"), State->AdvanceEscape(TestEscapeSeconds - 0.1f));
	TestEqual(TEXT("끝 직전 Escaping"), State->GetState(), EAnimalState::Escaping);
	TestTrue(TEXT("시간이 차면 끝남"), State->AdvanceEscape(0.2f));
	TestEqual(TEXT("도주 뒤 Capturable"), State->GetState(), EAnimalState::Capturable);
	TestTrue(TEXT("도주 뒤 포획 버튼 켜짐"), Capture->CanCapture());
	AddInfo(FString::Printf(TEXT("[Escape] 실패 → Escaping %.2f초 → Capturable · HP %.1f (도주 중 맞음 %.1f)"),
		TestEscapeSeconds, State->GetCurrentHP(), TestAttackDamage * 0.1f));

	// 다시 놓치고, 도주 중 막타면 Dead.
	Capture->TryCaptureTarget();
	Capture->ResolveCaptureRingAt(0.f);
	TestEqual(TEXT("다시 놓치면 다시 도주"), State->GetState(), EAnimalState::Escaping);
	State->ApplyDamage(State->GetCurrentHP());
	TestEqual(TEXT("도주 중 0 → Dead"), State->GetState(), EAnimalState::Dead);
	TestFalse(TEXT("Dead 뒤 도주 시간 흘려도 그대로"), State->AdvanceEscape(TestEscapeSeconds));
	TestEqual(TEXT("Dead 그대로"), State->GetState(), EAnimalState::Dead);

	// 도주 0 = 그 자리에서 다시 대기 (표 칸이 미정일 때의 길). 같은 자리에 세우려고 첫 동물은 치운다.
	Animal->Destroy();
	AAnimalCharacter* Second = SpawnTestAnimal(World, TestSmallMaxHP);
	if (TestNotNull(TEXT("둘째 동물"), Second) && TestTrue(TEXT("둘째 포획 창"), OpenCaptureWindow(Player, Second)))
	{
		Capture->SetCaptureEscapeSeconds(0.f);
		Capture->TryCaptureTarget();
		Capture->ResolveCaptureRingAt(0.f);
		TestEqual(TEXT("도주 0 → Capturable 그대로"), Second->GetAnimalState()->GetState(), EAnimalState::Capturable);
		TestFalse(TEXT("Capturable 아니면 도주 안 걸림"), Second->GetAnimalState()->BeginEscape(0.f));
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingGuestTrustTest, "Snarehaven.Capture.Ring.GuestToleranceBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingGuestTrustTest::RunTest(const FString& Parameters)
{
	using SnareCaptureRing::IsGuestElapsedTrusted;

	const float Host = TestRingSeconds * 0.5f;
	TestTrue(TEXT("같으면 믿는다"), IsGuestElapsedTrusted(Host, Host, TestGuestToleranceSeconds));
	// 경계 바로 안쪽 — float 뺄셈이 정확히 허용 오차로 안 떨어져 TestTolerance 만큼 안으로 본다.
	TestTrue(TEXT("허용 오차 끝(늦게)이면 믿는다"), IsGuestElapsedTrusted(Host + TestGuestToleranceSeconds - TestTolerance, Host, TestGuestToleranceSeconds));
	TestTrue(TEXT("허용 오차 끝(일찍)이면 믿는다"), IsGuestElapsedTrusted(Host - TestGuestToleranceSeconds + TestTolerance, Host, TestGuestToleranceSeconds));
	TestFalse(TEXT("허용 오차를 넘으면 안 믿는다"), IsGuestElapsedTrusted(Host + TestGuestToleranceSeconds + 0.01f, Host, TestGuestToleranceSeconds));
	TestFalse(TEXT("음수 경과는 안 믿는다"), IsGuestElapsedTrusted(-0.01f, 0.f, TestGuestToleranceSeconds));
	TestFalse(TEXT("숫자 아님은 안 믿는다"), IsGuestElapsedTrusted(FMath::Sqrt(-1.f), Host, TestGuestToleranceSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingGuestResolveTest, "Snarehaven.Capture.Ring.GuestResolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingGuestResolveTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestSmallMaxHP);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("동물"), Animal)
		|| !TestTrue(TEXT("포획 창을 연다"), OpenCaptureWindow(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}

	UCaptureComponent* Capture = Player->GetCapture();
	UAnimalStateComponent* State = Animal->GetAnimalState();
	const float HPBefore = State->GetCurrentHP();
	const float InWindow = TestRingSeconds - TestWindowSeconds * 0.5f;

	// 링 없이 판정 요청 → 무시: 실패로 안 세고 상태·사유 그대로.
	TestFalse(TEXT("링 없이 Resolve → 무시"), Capture->ResolveGuestCaptureRingAt(InWindow, InWindow));
	Capture->Server_ResolveCaptureRing(InWindow);
	TestEqual(TEXT("무시 뒤 상태 그대로"), State->GetState(), EAnimalState::Capturable);
	TestTrue(TEXT("무시는 실패 사유를 안 남긴다"), Capture->GetLastCaptureFailureReason().IsEmpty());

	// 오차 밖 → timing 실패·HP 그대로·도주. 게스트 경과 자체는 창 안이어도 시계를 못 믿으면 잡지 않는다.
	Capture->Server_BeginCaptureRing();
	TestTrue(TEXT("시작 요청이 링을 건다"), Capture->IsCaptureRingActive());
	const float FarHost = InWindow - TestGuestToleranceSeconds - 0.1f;
	TestFalse(TEXT("오차 밖은 잡지 않는다"), Capture->ResolveGuestCaptureRingAt(InWindow, FarHost));
	TestEqual(TEXT("오차 밖 사유 timing"), Capture->GetLastCaptureFailureReason(), FString(TEXT("timing")));
	TestEqual(TEXT("오차 밖 HP 그대로"), State->GetCurrentHP(), HPBefore, TestTolerance);
	TestEqual(TEXT("오차 밖 → 도주"), State->GetState(), EAnimalState::Escaping);
	TestFalse(TEXT("판정 뒤 링 없음"), Capture->IsCaptureRingActive());
	State->AdvanceEscape(TestEscapeSeconds);

	// 오차 안 → 게스트 시각으로 판정: 호스트가 잰 간격은 링 끝을 넘었어도(지터) 게스트 경과가 창 안이면 잡는다.
	Capture->Server_BeginCaptureRing();
	const float LateHost = TestRingSeconds + TestGuestToleranceSeconds * 0.5f;
	const float GuestNearEnd = TestRingSeconds - 0.01f;
	TestTrue(TEXT("오차 안 → 게스트 시각으로 잡힌다"), Capture->ResolveGuestCaptureRingAt(GuestNearEnd, LateHost));
	TestEqual(TEXT("상태가 Captured"), State->GetState(), EAnimalState::Captured);
	AddInfo(FString::Printf(TEXT("[Guest] 무시 · 오차 밖(게스트 %.2f / 호스트 %.2f) → timing · 오차 안(게스트 %.2f / 호스트 %.2f) → Captured · 허용 오차 %.2f"),
		InWindow, FarHost, GuestNearEnd, LateHost, TestGuestToleranceSeconds));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingGuestTimeoutTest, "Snarehaven.Capture.Ring.GuestTimeoutWaitsTolerance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingGuestTimeoutTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestSmallMaxHP);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("동물"), Animal)
		|| !TestTrue(TEXT("포획 창을 연다"), OpenCaptureWindow(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}

	UCaptureComponent* Capture = Player->GetCapture();
	UAnimalStateComponent* State = Animal->GetAnimalState();

	// 게스트 링은 둘째 누름이 흔들림만큼 늦게 닿아도 받으려고 호스트 시간 초과를 허용 오차만큼 늦춘다.
	Capture->Server_BeginCaptureRing();
	AdvanceTimers(World, TestRingSeconds + TestGuestToleranceSeconds * 0.5f);
	TestTrue(TEXT("링 끝 + 허용 오차 전엔 아직 돈다"), Capture->IsCaptureRingActive());
	AdvanceTimers(World, TestGuestToleranceSeconds);
	TestFalse(TEXT("링 끝 + 허용 오차를 넘기면 사라진다"), Capture->IsCaptureRingActive());
	TestEqual(TEXT("게스트 시간 초과 사유 timing"), Capture->GetLastCaptureFailureReason(), FString(TEXT("timing")));
	TestEqual(TEXT("게스트 시간 초과도 도주"), State->GetState(), EAnimalState::Escaping);

	// 허용 오차 미정(-1)이면 게스트 시각을 안 쓰고 호스트 간격으로 판정한다.
	State->AdvanceEscape(TestEscapeSeconds);
	Capture->SetCaptureRingTolerance(-1.f);
	Capture->Server_BeginCaptureRing();
	TestFalse(TEXT("미정이면 호스트 간격(이른 누름)으로 실패"), Capture->ResolveGuestCaptureRingAt(TestRingSeconds - 0.01f, 0.f));
	TestEqual(TEXT("미정 길 사유 timing"), Capture->GetLastCaptureFailureReason(), FString(TEXT("timing")));
	AddInfo(FString::Printf(TEXT("[Guest] 시간 초과 = 링 %.2f + 허용 오차 %.2f초 → timing · 미정이면 호스트 시계"),
		TestRingSeconds, TestGuestToleranceSeconds));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureRingPressGestureTest, "Snarehaven.Capture.Ring.PressPlaysThrowGesture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureRingPressGestureTest::RunTest(const FString& Parameters)
{
	// M28-09c 빈 칸(#0510 실측): 링 시작·둘째 누름에 몸짓·소리가 없었다. 무언가 하는 누름마다 던지기 몸짓(올가미)·소리 하나.
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestSmallMaxHP);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("동물"), Animal) || !TestNotNull(TEXT("조종기"), Controller))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 주인 기기는 몸짓을 먼저 틀고 복제본은 안 되감는다 — 조종기가 없으면 「이 기기」 가 아니라 두 번 센다(포획 연출 시험과 같이 붙인다).
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Controller->Possess(Player);

	UCaptureComponent* Capture = Player->GetCapture();
	const int32 ActionsBefore = Player->GetItemActionCount();
	const int32 SoundsBefore = Player->GetSoundRequestCount();
	Player->GetMeleeAttack()->RefreshTarget();
	Capture->TryCaptureTarget();
	TestEqual(TEXT("포획 창 전 누름은 몸짓 없음"), Player->GetItemActionCount(), ActionsBefore);
	TestEqual(TEXT("포획 창 전 누름은 소리 없음"), Player->GetSoundRequestCount(), SoundsBefore);

	if (!TestTrue(TEXT("포획 창을 연다"), OpenCaptureWindow(Player, Animal)))
	{
		DestroyTestWorld(World);
		return false;
	}
	Capture->TryCaptureTarget();
	TestTrue(TEXT("첫 누름 = 링 시작"), Capture->IsCaptureRingActive());
	TestEqual(TEXT("첫 누름 몸짓 하나"), Player->GetItemActionCount(), ActionsBefore + 1);
	TestEqual(TEXT("던지기 몸짓 = 올가미"), Player->GetLastItemAction(), EItemAction::Lasso);
	TestEqual(TEXT("첫 누름 소리 하나"), Player->GetSoundRequestCount(), SoundsBefore + 1);

	// 둘째 누름(곧바로 = 이른 누름 실패)도 몸짓·소리는 낸다 — 결과는 판정 몫.
	Capture->TryCaptureTarget();
	TestEqual(TEXT("둘째 누름 몸짓 하나 더"), Player->GetItemActionCount(), ActionsBefore + 2);
	TestEqual(TEXT("둘째 누름 소리 하나 더"), Player->GetSoundRequestCount(), SoundsBefore + 2);
	TestEqual(TEXT("이른 누름이라 도주"), Animal->GetAnimalState()->GetState(), EAnimalState::Escaping);

	// 도주 중엔 버튼이 꺼져 있다 — 눌러도 아무 몸짓 없음.
	Capture->TryCaptureTarget();
	TestEqual(TEXT("도주 중 누름은 몸짓 없음"), Player->GetItemActionCount(), ActionsBefore + 2);
	AddInfo(FString::Printf(TEXT("[Ring] 누름 몸짓 %d · 소리 %d (창 전 0 · 첫 1 · 둘째 1 · 도주 중 0)"),
		Player->GetItemActionCount() - ActionsBefore, Player->GetSoundRequestCount() - SoundsBefore));

	DestroyTestWorld(World);
	return true;
}

// 포획 던지기 몸짓(DESIGN 8.8 「포획 던지기」) — 누름마다 올가미 클립(`A_Player_FishCast` 재사용) 한 번, 길이 0.5초 안팎.
// 성공(링 0 = 첫 누름에 포획)에서도 다른 클립이 덮지 않고, 실패(이른 누름)에서도 같은 몸짓. 받는 기기(남의 사본)도 같은 클립을 튼다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCaptureLassoGestureTest, "Snarehaven.Capture.Lasso.GestureOnSuccessAndFail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCaptureLassoGestureTest::RunTest(const FString& Parameters)
{
	// DESIGN 8.8 「포획 던지기」 0.5초 안팎 — 위아래 0.2초 여유.
	constexpr float MinLassoSeconds = 0.3f;
	constexpr float MaxLassoSeconds = 0.7f;
	const FString ExpectedClip = TEXT("A_Player_FishCast");

	TArray<FString> Lines;
	const auto CheckLasso = [&](const TCHAR* Label, const ASnarehavenCharacter& Who, int32 WantCount)
	{
		const UAnimSequence* const Anim = Who.GetItemActionAnim();
		const float Seconds = Who.GetItemActionSecondsLeft();
		TestEqual(*FString::Printf(TEXT("[%s] 몸짓 = 올가미"), Label), Who.GetLastItemAction(), EItemAction::Lasso);
		TestEqual(*FString::Printf(TEXT("[%s] 몸짓 수"), Label), Who.GetItemActionCount(), WantCount);
		TestEqual(*FString::Printf(TEXT("[%s] 클립"), Label), Anim != nullptr ? Anim->GetName() : FString(), ExpectedClip);
		TestTrue(*FString::Printf(TEXT("[%s] 0.5초 안팎"), Label), Seconds >= MinLassoSeconds && Seconds <= MaxLassoSeconds);
		Lines.Add(FString::Printf(TEXT("%s %s %s %.3f초 ×%d"), Label, *UEnum::GetValueAsString(Who.GetLastItemAction()),
			Anim != nullptr ? *Anim->GetName() : TEXT("없음"), Seconds, Who.GetItemActionCount()));
	};
	/** 월드 하나에 조종기 붙은 플레이어(링 길이 지정)·동물 — 주인 기기라 복제본을 다시 안 센다. */
	const auto Setup = [](float RingSeconds, ASnarehavenCharacter*& OutPlayer, AAnimalCharacter*& OutAnimal) -> UWorld*
	{
		UWorld* const NewWorld = CreateTestWorld();
		OutPlayer = SpawnTestPlayer(NewWorld, RingSeconds);
		OutAnimal = SpawnTestAnimal(NewWorld, TestSmallMaxHP);
		APlayerController* const Controller = NewWorld->SpawnActor<APlayerController>();
		if (OutPlayer != nullptr && Controller != nullptr)
		{
			Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
			Controller->Possess(OutPlayer);
		}
		return NewWorld;
	};

	// ① 성공 — 링 0 이라 첫 누름에 잡혀도 몸짓은 올가미 하나뿐(옛 성공 때 줍기 덮어쓰기 없음).
	ASnarehavenCharacter* Player = nullptr;
	AAnimalCharacter* Animal = nullptr;
	UWorld* World = Setup(0.f, Player, Animal);
	if (TestNotNull(TEXT("[성공] 플레이어"), Player) && TestNotNull(TEXT("[성공] 동물"), Animal)
		&& TestTrue(TEXT("[성공] 포획 창"), OpenCaptureWindow(Player, Animal)))
	{
		TestTrue(TEXT("[성공] 첫 누름에 포획"), Player->GetCapture()->TryCaptureTarget());
		CheckLasso(TEXT("성공"), *Player, 1);
	}
	DestroyTestWorld(World);

	// ② 실패 — 링 시작 + 곧바로 둘째 누름(이른 누름), 두 번 다 올가미.
	World = Setup(TestRingSeconds, Player, Animal);
	if (TestNotNull(TEXT("[실패] 플레이어"), Player) && TestNotNull(TEXT("[실패] 동물"), Animal)
		&& TestTrue(TEXT("[실패] 포획 창"), OpenCaptureWindow(Player, Animal)))
	{
		Player->GetCapture()->TryCaptureTarget();
		TestFalse(TEXT("[실패] 이른 누름은 실패"), Player->GetCapture()->TryCaptureTarget());
		TestEqual(TEXT("[실패] 도주"), Animal->GetAnimalState()->GetState(), EAnimalState::Escaping);
		CheckLasso(TEXT("실패"), *Player, 2);

		// 주인 기기는 같은 복제를 받아도 되감지 않는다.
		Player->Multicast_PlayItemAction_Implementation(EItemAction::Lasso, true);
		TestEqual(TEXT("[주인] 복제 받아도 중복 없음"), Player->GetItemActionCount(), 2);
	}

	// ③ 받는 기기 — 남의 사본(이 기기가 조종 안 함)은 주인이 이미 틀었다는 표시가 와도 튼다.
	ASnarehavenCharacter* const Other = World->SpawnActor<ASnarehavenCharacter>(FVector(0.f, 500.f, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("받는 기기 사본"), Other))
	{
		Other->Multicast_PlayItemAction_Implementation(EItemAction::Lasso, true);
		CheckLasso(TEXT("받는 기기"), *Other, 1);
	}

	AddInfo(FString::Printf(TEXT("[Lasso] %s"), *FString::Join(Lines, TEXT(" · "))));
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
