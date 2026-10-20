// ROADMAP M1-03 2층 검사 — 근접 공격이 사거리 안 가장 가까운 동물을 골라 HP 를 깎는지,
// 끝난 동물(Dead/Captured)은 건드리지 않는지, 그리고 INBOX 지시(감독, 2026-09-19 05:05)대로
// 돼지 테이블 값 + 확정 데미지로 3타째 Capturable · 4타째 Dead 가 되는지 (M22 돼지 HP 80).
// M11-05: 공격 네 숫자(데미지·사거리·간격·타격 지연)가 `DT_PlayerCombatTable` 에서 오는지도 여기서 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRow.h"
#include "Data/PlayerCombatRow.h"
#include "Equipment/EquipmentComponent.h"
#include "Player/PlayerVitalsComponent.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "CoreGlobals.h"
#include "SnarehavenCharacter.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr float TestFarDistanceCm = 200.f;
	constexpr float TestOutOfRangeDistanceCm = 1000.f;
	constexpr float TestMaxHP = 100.f;

	// 타격 지연 검사 전용 값 (Verification.md 6 — DESIGN 3.1 의 0.2 와 **일부러 다르다**.
	// 설계값 그대로면 지연을 코드에 박아도 검사가 통과한다). 간격 1.0 의 0.3 이라 겹치지 않는다.
	constexpr float TestAttackHitDelaySeconds = 0.3f;
	constexpr float TestTimerStepSeconds = 0.01f;
	constexpr float TestTimeToleranceSeconds = 0.05f;
	constexpr float TestCaptureThresholdPct = 10.f;
	constexpr float TestTolerance = 0.01f;

	// M11-04 감쇠 검사 전용 값 (Verification.md 6 — DESIGN 8.6-b 근접 행 1.0/1.0 과 **일부러 다르다**.
	// 설계 값 그대로면 곡선이 통째로 없어도 검사가 통과한다).
	constexpr float TestFalloffStartRatio = 0.5f;
	constexpr float TestMinDamageRatio = 0.25f;
	constexpr float TestFullDamageDistanceCm = 150.f;   // = 300 * 0.5, 감쇠 시작점
	constexpr float TestMidFalloffDistanceCm = 225.f;   // 감쇠 구간 한가운데
	constexpr float TestEdgeDistanceCm = 300.f;         // = 사거리 끝

	// M11-05: 공격 네 숫자가 사는 표. **검사가 값을 들고 있지 않다** — 표와 설계서를 맞댄다.
	const TCHAR* CombatTablePath = TEXT("/Game/Game/Data/Balance/DT_PlayerCombatTable.DT_PlayerCombatTable");
	const TCHAR* CombatRowName = TEXT("Player");

	// DESIGN 3.1 "확정 수치" 표의 왼쪽 칸 이름 그대로. **여기 있는 것은 이름뿐이고 값은 문서에서 읽는다**
	// (`Snarehaven.PlayerVitals.Table.MatchesDesign` 과 같은 짜임).
	struct FCombatField
	{
		const TCHAR* DesignLabel;
		const TCHAR* FieldName;
		float FPlayerCombatRow::* Member;
	};

	const FCombatField CombatFields[] = {
		{ TEXT("플레이어 근접 공격 데미지"), TEXT("AttackDamage"), &FPlayerCombatRow::AttackDamage },
		{ TEXT("근접 공격 사거리 = 자동 상호작용 하이라이트 거리"), TEXT("AttackRangeCm"),
			&FPlayerCombatRow::AttackRangeCm },
		{ TEXT("공격 간격"), TEXT("AttackIntervalSeconds"), &FPlayerCombatRow::AttackIntervalSeconds },
		{ TEXT("공격 타격 지연"), TEXT("AttackHitDelaySeconds"), &FPlayerCombatRow::AttackHitDelaySeconds },
		{ TEXT("상인 대화 거리"), TEXT("NpcTalkRangeCm"), &FPlayerCombatRow::NpcTalkRangeCm },   // M22
		// M21-02 때리는 손맛 아홉 칸.
		{ TEXT("히트스톱 시간"), TEXT("HitStopSeconds"), &FPlayerCombatRow::HitStopSeconds },
		{ TEXT("히트스톱 시간 배율"), TEXT("HitStopTimeDilation"), &FPlayerCombatRow::HitStopTimeDilation },
		{ TEXT("넉백 거리"), TEXT("KnockbackCm"), &FPlayerCombatRow::KnockbackCm },
		{ TEXT("넉백 시간"), TEXT("KnockbackSeconds"), &FPlayerCombatRow::KnockbackSeconds },
		{ TEXT("카메라 흔들림 진폭"), TEXT("CameraShakeAmplitudeCm"), &FPlayerCombatRow::CameraShakeAmplitudeCm },
		{ TEXT("카메라 흔들림 시간"), TEXT("CameraShakeSeconds"), &FPlayerCombatRow::CameraShakeSeconds },
		{ TEXT("흰 깜빡임 시간"), TEXT("HitFlashSeconds"), &FPlayerCombatRow::HitFlashSeconds },
		{ TEXT("피격 파편 최소 개수"), TEXT("HitDebrisMin"), &FPlayerCombatRow::HitDebrisMin },
		{ TEXT("피격 파편 최대 개수"), TEXT("HitDebrisMax"), &FPlayerCombatRow::HitDebrisMax },
		{ TEXT("같이 잡기 드롭 창"), TEXT("ShareDropWindowSeconds"), &FPlayerCombatRow::ShareDropWindowSeconds },   // M21-06
		// M21-09 올가미 조이기 링 여섯 칸.
		{ TEXT("올가미 링 길이 (M21-09)"), TEXT("CaptureRingSeconds"), &FPlayerCombatRow::CaptureRingSeconds },
		{ TEXT("올가미 링 성공 창"), TEXT("CaptureWindowSeconds"), &FPlayerCombatRow::CaptureWindowSeconds },
		{ TEXT("올가미 링 성공 창 (희귀·큰 종)"), TEXT("CaptureWindowHardSeconds"), &FPlayerCombatRow::CaptureWindowHardSeconds },
		{ TEXT("올가미 링 큰 종 기준 HP"), TEXT("CaptureHardMinHP"), &FPlayerCombatRow::CaptureHardMinHP },
		{ TEXT("올가미 링 실패 도주"), TEXT("CaptureEscapeSeconds"), &FPlayerCombatRow::CaptureEscapeSeconds },
		{ TEXT("올가미 링 게스트 허용 오차"), TEXT("CaptureRingToleranceSeconds"), &FPlayerCombatRow::CaptureRingToleranceSeconds },
	};

	/** `### 3.1 …` 부터 다음 제목 전까지만 잘라 낸다 (PlayerVitals·DangerZone 검사와 같은 방식). */
	FString ExtractDesignSection(const FString& Doc, const FString& HeadingPrefix)
	{
		const int32 Start = Doc.Find(HeadingPrefix, ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return FString();
		}

		const int32 BodyStart = Start + HeadingPrefix.Len();
		const int32 NextHeading = Doc.Find(TEXT("\n#"), ESearchCase::CaseSensitive,
			ESearchDir::FromStart, BodyStart);
		return NextHeading == INDEX_NONE ? Doc.Mid(BodyStart) : Doc.Mid(BodyStart, NextHeading - BodyStart);
	}

	/** `| 공격 간격 | 0.6초 | 근거 |` 같은 줄에서 왼쪽 칸 이름과 값 칸의 첫 숫자를 꺼낸다. */
	TMap<FString, double> ParseLabelledNumbers(const FString& Section)
	{
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);

		const FRegexPattern RowPattern(TEXT("^\\|\\s*([^|]+?)\\s*\\|\\s*([0-9]+(?:\\.[0-9]+)?)"));
		TMap<FString, double> Values;
		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(RowPattern, Line);
			if (Matcher.FindNext())
			{
				Values.Add(Matcher.GetCaptureGroup(1), FCString::Atod(*Matcher.GetCaptureGroup(2)));
			}
		}
		return Values;
	}

	// 문제를 모아 돌려준다 — 아래 자기검사가 "이 검사가 정말 잡는가" 를 에셋·문서 없이 다시 볼 수 있게.
	TArray<FString> CollectCombatIssues(const FPlayerCombatRow& Row, const TMap<FString, double>& DesignValues)
	{
		TArray<FString> Issues;
		for (const FCombatField& Field : CombatFields)
		{
			// 설계서 칸을 못 읽었으면 "비교할 게 없어 통과" 가 된다 — 그게 제일 위험하다.
			const double* const DesignValue = DesignValues.Find(Field.DesignLabel);
			if (DesignValue == nullptr)
			{
				Issues.Add(FString::Printf(TEXT("DESIGN 3.1 에서 '%s' 줄을 못 읽었다"), Field.DesignLabel));
				continue;
			}

			const double TableValue = static_cast<double>(Row.*(Field.Member));
			if (!FMath::IsNearlyEqual(TableValue, *DesignValue, 0.01))
			{
				Issues.Add(FString::Printf(TEXT("%s 가 DESIGN 3.1 '%s' 와 다르다 (문서 %.2f / 표 %.2f)"),
					Field.FieldName, Field.DesignLabel, *DesignValue, TableValue));
			}
		}
		return Issues;
	}

	/** 검사 전용 행 한 줄 (Verification.md 6 — 네 칸 전부 설계값과 일부러 다르다). */
	FPlayerCombatRow MakeTestCombatRow()
	{
		FPlayerCombatRow Row;
		Row.AttackDamage = TestAttackDamage;
		Row.AttackRangeCm = TestAttackRangeCm;
		Row.AttackIntervalSeconds = TestAttackIntervalSeconds;
		Row.AttackHitDelaySeconds = TestAttackHitDelaySeconds;
		return Row;
	}

	/** 실제 아이템 표의 몽둥이 행 (M5-06a). 사거리·감쇠 세 칸이 M11-04 로 채워졌다. */
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const TCHAR* ClubRowName = TEXT("Item_Club");

	// 실제 데이터 테이블 (M1-01). 경로는 engines/unreal.md 폴더 구조 그대로.
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* PigRowName = TEXT("Animal_Pig");

	// INBOX 지시(감독, 2026-09-19 05:05) 의 합격 조건. DESIGN 3.1 확정 수치로 계산된 타수라
	// 테스트 전용 값이 아니라 **사람이 못 박은 결과**다 — 밸런스가 바뀌면 이 검사가 먼저 깨져야 한다.
	// M22(2026-10-03) 돼지 체력 100 → 80 (DESIGN 3.1 「동물 체력」) — 3타째 HP 20 → Capturable, 4타째 Dead.
	// 검사 이름(`PigTakesFourHits…`)은 STATUS·ROADMAP 기록이 가리켜 그대로 둔다.
	constexpr int32 ExpectedHitsToCapturable = 3;
	constexpr int32 ExpectedHitsToDead = 4;

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	// 휘두르기 길이·타격 지연은 타이머가 잰다. 검사 월드(`UWorld::CreateWorld`)는 게임모드 없이 돌아
	// 통째로 틱할 수 없으므로 **타이머 매니저만** 흘린다 — 실제 플레이와 같은 타이머 길을 탄다.
	// `GFrameCounter` 를 같이 올리는 것이 핵심이다: `FTimerManager::Tick` 은 한 프레임에 두 번째부터
	// `HasBeenTickedThisFrame()` 으로 **그냥 돌아간다**. 안 올리면 검사 하나가 0.01초에서 멈춰 선다
	// (루프#162 실측 — 지연 타이머가 영영 안 울려 "판정 시각 없음" 이 나왔다).
	void TickTimerStep(UWorld* World)
	{
		++GFrameCounter;
		World->GetTimerManager().Tick(TestTimerStepSeconds);
	}

	void AdvanceTimers(UWorld* World, float Seconds)
	{
		const int32 Steps = FMath::RoundToInt(Seconds / TestTimerStepSeconds);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			TickTimerStep(World);
		}
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	// 테스트 전용 체력으로 초기화한 동물. 실제 테이블을 읽지 않는다.
	AAnimalCharacter* SpawnTestAnimal(UWorld* World, float DistanceCm, float MaxHP = TestMaxHP)
	{
		AAnimalCharacter* Animal =
			World->SpawnActor<AAnimalCharacter>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			return nullptr;
		}

		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_dummy");
		Row.MaxHP = MaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Animal->GetAnimalState()->InitializeFromRow(Row);
		return Animal;
	}

	// 플레이어는 원점에 두고, 공격 숫자는 테스트 전용 값으로 갈아 끼운다.
	ASnarehavenCharacter* SpawnTestPlayer(UWorld* World)
	{
		ASnarehavenCharacter* Player =
			World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		if (Player == nullptr)
		{
			return nullptr;
		}

		Player->GetMeleeAttack()->SetAttackSettings(
			TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
		return Player;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeHitsNearestTest, "Snarehaven.Combat.Melee.HitsNearestAnimal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeleeHitsNearestTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Near = SpawnTestAnimal(World, TestNearDistanceCm);
	AAnimalCharacter* Far = SpawnTestAnimal(World, TestFarDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("가까운 동물"), Near)
		|| !TestNotNull(TEXT("먼 동물"), Far))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	TestTrue(TEXT("한 대 들어갔다"), Melee->AttackOnce());
	TestTrue(TEXT("하이라이트는 가장 가까운 동물"), Melee->GetCurrentTarget() == Near);
	TestEqual(TEXT("가까운 동물 HP 가 데미지만큼 줄었다"),
		Near->GetAnimalState()->GetCurrentHP(), TestMaxHP - TestAttackDamage, TestTolerance);
	TestEqual(TEXT("먼 동물은 안 맞았다"),
		Far->GetAnimalState()->GetCurrentHP(), TestMaxHP, TestTolerance);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeRangeTest, "Snarehaven.Combat.Melee.IgnoresOutOfRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeleeRangeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* TooFar = SpawnTestAnimal(World, TestOutOfRangeDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("사거리 밖 동물"), TooFar))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	TestFalse(TEXT("사거리 밖이면 헛손질"), Melee->AttackOnce());
	TestNull(TEXT("하이라이트도 없다"), Melee->GetCurrentTarget());
	TestEqual(TEXT("HP 는 그대로"),
		TooFar->GetAnimalState()->GetCurrentHP(), TestMaxHP, TestTolerance);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeSkipsFinishedTest, "Snarehaven.Combat.Melee.SkipsFinishedAnimal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeleeSkipsFinishedTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Dead = SpawnTestAnimal(World, TestNearDistanceCm);
	AAnimalCharacter* Alive = SpawnTestAnimal(World, TestFarDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("죽을 동물"), Dead)
		|| !TestNotNull(TEXT("성한 동물"), Alive))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 가까운 쪽을 먼저 죽여 둔다 (데미지는 테스트 전용 값이 아니라 한 방에 끝내는 값).
	Dead->GetAnimalState()->ApplyDamage(TestMaxHP);
	TestEqual(TEXT("가까운 동물이 죽었다"), Dead->GetAnimalState()->GetState(), EAnimalState::Dead);

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	TestTrue(TEXT("성한 동물을 대신 때린다"), Melee->AttackOnce());
	TestTrue(TEXT("하이라이트가 성한 동물로 넘어갔다"), Melee->GetCurrentTarget() == Alive);
	TestEqual(TEXT("성한 동물이 맞았다"),
		Alive->GetAnimalState()->GetCurrentHP(), TestMaxHP - TestAttackDamage, TestTolerance);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeHoldTest, "Snarehaven.Combat.Melee.HoldStartsAndStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeleeHoldTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	Melee->SetAttackHitDelaySeconds(TestAttackHitDelaySeconds);
	Melee->StartAttacking();
	TestTrue(TEXT("누르고 있는 상태"), Melee->IsAttacking());

	// 첫 휘두르기는 타이머를 기다리지 않고 바로 시작된다 (모바일 반응성). **데미지는 그 순간이 아니다** —
	// 곤봉이 몸 앞을 지날 때 들어간다 (DESIGN 3.1 공격 타격 지연, 사용자 결함 2026-09-23 16:40 ③).
	TestTrue(TEXT("누른 즉시 휘두르기가 돈다"), Melee->IsSwinging());
	TestEqual(TEXT("누른 순간에는 아직 안 맞는다"),
		Animal->GetAnimalState()->GetCurrentHP(), TestMaxHP, TestTolerance);

	AdvanceTimers(World, TestAttackHitDelaySeconds + TestTimeToleranceSeconds);
	TestEqual(TEXT("지연이 지나면 한 대"),
		Animal->GetAnimalState()->GetCurrentHP(), TestMaxHP - TestAttackDamage, TestTolerance);

	Melee->StopAttacking();
	TestFalse(TEXT("떼면 멈춘다"), Melee->IsAttacking());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleePigTest, "Snarehaven.Combat.Melee.PigTakesFourHitsToCapturable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeleePigTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), Table))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();

	// 여기서는 숫자를 갈아 끼우지 않는다 — DESIGN 3.1 확정 데미지(`DT_PlayerCombatTable` 값)와
	// 테이블의 돼지 체력이 만나면 몇 대에 포획 창이 열리는지가 이 검사의 전부다.
	ASnarehavenCharacter* Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AAnimalCharacter* Pig =
		World->SpawnActor<AAnimalCharacter>(FVector(TestNearDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("돼지 스폰"), Pig))
	{
		DestroyTestWorld(World);
		return false;
	}

	Pig->SetAnimalData(Table, FName(PigRowName));
	if (!TestTrue(TEXT("돼지 행으로 초기화"), Pig->InitializeFromTable()))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	UAnimalStateComponent* State = Pig->GetAnimalState();
	AddInfo(FString::Printf(TEXT("[animal.pig] 최대 %.0f · 임계 %.0f · 1타 %.0f · 사거리 %.0fcm"),
		State->GetMaxHP(), State->GetCaptureThresholdHP(),
		Melee->GetAttackDamage(), Melee->GetAttackRangeCm()));

	int32 HitsToCapturable = 0;
	int32 HitsToDead = 0;
	for (int32 HitIndex = 1; HitIndex <= ExpectedHitsToDead && !State->IsFinished(); ++HitIndex)
	{
		if (!Melee->AttackOnce())
		{
			break;
		}

		if (HitsToCapturable == 0 && State->GetState() == EAnimalState::Capturable)
		{
			HitsToCapturable = HitIndex;
		}
		if (HitsToDead == 0 && State->GetState() == EAnimalState::Dead)
		{
			HitsToDead = HitIndex;
		}
	}

	AddInfo(FString::Printf(TEXT("[animal.pig] %d타째 Capturable · %d타째 Dead"), HitsToCapturable, HitsToDead));
	TestEqual(TEXT("3타째 포획 가능 (DESIGN 3.1)"), HitsToCapturable, ExpectedHitsToCapturable);
	TestEqual(TEXT("4타째 사망 (DESIGN 3.1)"), HitsToDead, ExpectedHitsToDead);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeOnePressOneSwingTest,
	"Snarehaven.Combat.Melee.OnePressOneSwing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 사용자 2026-10-04 「레퍼런스는 한 번 클릭, 클릭하면 다시 누를 수 있을 때까지 딜레이가 있다」 (INBOX 15:30 ⓪).
// ① 누른 채 간격이 몇 번 지나도 휘두르기·타격은 한 번 ② 간격 안 두 번째 누름은 버린다(미리 눌러 두기 없음)
// ③ 간격 뒤 누름은 휘두르기·타격. 옛 「누르는 동안 0.6초마다 자동 반복」 이면 ①에서 타격이 셋 이상 나온다.
bool FSnarehavenMeleeOnePressOneSwingTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm, TestMaxHP * 10.f);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	Melee->SetAttackHitDelaySeconds(TestAttackHitDelaySeconds);

	// ② 먼저 — 첫 누름 직후(타격 지연 안) 손을 뗐다 다시 누른다.
	Melee->StartAttacking();
	AdvanceTimers(World, TestAttackHitDelaySeconds + TestTimeToleranceSeconds);
	Melee->StopAttacking();
	Melee->StartAttacking();
	TestFalse(TEXT("간격 안이라 아직 못 누른다"), Melee->IsActionReady());
	TestEqual(TEXT("간격 안 두 번째 누름은 버린다"), Melee->GetIgnoredPressCount(), 1);
	TestEqual(TEXT("휘두르기는 한 번뿐"), Melee->GetSwingCount(), 1);

	// ① 그대로 누른 채 간격 세 번을 흘린다.
	AdvanceTimers(World, TestAttackIntervalSeconds * 3.f);
	AddInfo(FString::Printf(TEXT("누른 채 %.1f초 — 휘두르기 %d · 타격 %d · 버린 누름 %d"),
		TestAttackHitDelaySeconds + TestTimeToleranceSeconds + TestAttackIntervalSeconds * 3.f,
		Melee->GetSwingCount(), Melee->GetLandedHitCount(), Melee->GetIgnoredPressCount()));
	TestTrue(TEXT("누르고 있는 상태"), Melee->IsAttacking());
	TestEqual(TEXT("누른 채 유지해도 휘두르기 1번"), Melee->GetSwingCount(), 1);
	TestEqual(TEXT("누른 채 유지해도 타격 1번"), Melee->GetLandedHitCount(), 1);
	TestTrue(TEXT("간격이 지나면 다시 누를 수 있다"), Melee->IsActionReady());

	// ③ 간격 뒤 누름은 휘두르기·타격.
	Melee->StopAttacking();
	Melee->StartAttacking();
	AdvanceTimers(World, TestAttackHitDelaySeconds + TestTimeToleranceSeconds);
	Melee->StopAttacking();
	TestEqual(TEXT("간격 뒤 누름은 휘두르기"), Melee->GetSwingCount(), 2);
	TestEqual(TEXT("간격 뒤 누름은 타격"), Melee->GetLandedHitCount(), 2);
	TestEqual(TEXT("버린 누름은 그대로 1"), Melee->GetIgnoredPressCount(), 1);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeFixedLengthToolPressTest,
	"Snarehaven.Combat.Melee.FixedLengthToolPress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// INBOX 15:30 ⓪ 꼬리 — 전기톱·고급/전기 곡괭이는 「누르는 동안 버티기」 가 아니라 한 번 누르면 정해진 길이(쓰기 클립 길이,
// 레퍼런스 시트 약 1.5초)만큼 내밀어 떨다 돌아오고 그동안 누름을 버린다. 행 칸 `bUseAnimFixedLength` 가 가른다.
// ① 행 칸이 켜진 세 행은 동작 길이 = 쓰기 클립 길이(공격 간격보다 길다) ② 한 번 누름 → 간격(1초)이 지나도 클립 길이 안이면
// 두 번째 누름을 버리고 휘두르기는 도는 중 ③ 클립 길이 뒤 누름은 휘두르기 ④ 누름 한 번에 타격 한 번
// ⑤ 칸이 꺼진 손도끼는 공격 간격 그대로 — 역검증: 칸을 안 보면 ②에서 두 번째 누름이 받아진다.
bool FSnarehavenMeleeFixedLengthToolPressTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm, TestMaxHP * 10.f);
	UEquipmentComponent* Equipment = IsValid(Player) ? Player->GetEquipment() : nullptr;
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal)
		|| !TestNotNull(TEXT("장비 컴포넌트"), Equipment)
		|| !TestTrue(TEXT("사람 메시를 올렸다 — 장비는 몸 소켓에 붙는다"), Player->ApplyPlayerBody()))
	{
		DestroyTestWorld(World);
		return false;
	}
	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	Melee->SetAttackHitDelaySeconds(TestAttackHitDelaySeconds);

	// ① 세 행 모두 동작 길이 = 그 행 쓰기 클립 길이.
	for (const TCHAR* const ItemID : { TEXT("item.pickaxe_advanced"), TEXT("item.pickaxe_electric"), TEXT("item.chainsaw") })
	{
		if (!TestTrue(FString::Printf(TEXT("%s 를 들었다"), ItemID), Equipment->EquipItemByItemID(FName(ItemID))))
		{
			continue;
		}
		const FItemRow* const Row = Equipment->GetEquippedRow(EEquipSlot::Tool);
		const UAnimSequence* const Use = Row ? Row->UseAnim.LoadSynchronous() : nullptr;
		const float ClipSeconds = IsValid(Use) ? Use->GetPlayLength() : 0.f;
		AddInfo(FString::Printf(TEXT("[FixedLength] %s 칸 %d · 쓰기 클립 %.2f초 · 동작 길이 %.2f초"), ItemID,
			Row && Row->bUseAnimFixedLength ? 1 : 0, ClipSeconds, Melee->GetActionLengthSeconds()));
		TestTrue(FString::Printf(TEXT("① %s 행 칸이 켜져 있다"), ItemID), Row != nullptr && Row->bUseAnimFixedLength);
		TestTrue(FString::Printf(TEXT("① %s 쓰기 클립이 공격 간격보다 길다 (%.2f)"), ItemID, ClipSeconds), ClipSeconds > TestAttackIntervalSeconds);
		TestEqual(FString::Printf(TEXT("① %s 동작 길이 = 쓰기 클립 길이"), ItemID), Melee->GetActionLengthSeconds(), ClipSeconds, TestTolerance);
	}

	// ② 전기톱을 든 채 한 번 누르고 손을 뗀 뒤, 공격 간격(1초)은 지났지만 클립 길이 안에서 다시 누른다.
	const float FixedSeconds = Melee->GetActionLengthSeconds();
	Melee->StartAttacking();
	Melee->StopAttacking();
	AdvanceTimers(World, TestAttackIntervalSeconds + TestTimeToleranceSeconds * 2.f);
	TestTrue(TEXT("② 간격이 지나도 클립 길이 안이면 휘두르기가 도는 중"), Melee->IsSwinging());
	TestFalse(TEXT("② 클립 길이 안이라 아직 못 누른다"), Melee->IsActionReady());
	Melee->StartAttacking();
	Melee->StopAttacking();
	TestEqual(TEXT("② 클립 길이 안 두 번째 누름은 버린다"), Melee->GetIgnoredPressCount(), 1);
	TestEqual(TEXT("② 휘두르기는 한 번뿐"), Melee->GetSwingCount(), 1);
	TestEqual(TEXT("④ 누름 한 번에 타격 한 번"), Melee->GetLandedHitCount(), 1);

	// ③ 클립 길이가 지나면 다시 누를 수 있다.
	AdvanceTimers(World, FixedSeconds - TestAttackIntervalSeconds);
	TestFalse(TEXT("③ 클립 길이 뒤 휘두르기가 끝났다"), Melee->IsSwinging());
	TestTrue(TEXT("③ 클립 길이 뒤 다시 누를 수 있다"), Melee->IsActionReady());
	Melee->StartAttacking();
	Melee->StopAttacking();
	AdvanceTimers(World, TestAttackHitDelaySeconds + TestTimeToleranceSeconds);
	AddInfo(FString::Printf(TEXT("[FixedLength] 전기톱 %.2f초 — 휘두르기 %d · 타격 %d · 버린 누름 %d"),
		TestAttackIntervalSeconds + TestTimeToleranceSeconds * 2.f + (FixedSeconds - TestAttackIntervalSeconds)
			+ TestAttackHitDelaySeconds + TestTimeToleranceSeconds,
		Melee->GetSwingCount(), Melee->GetLandedHitCount(), Melee->GetIgnoredPressCount()));
	TestEqual(TEXT("③ 클립 길이 뒤 누름은 휘두르기"), Melee->GetSwingCount(), 2);
	TestEqual(TEXT("④ 둘째 누름도 타격 한 번"), Melee->GetLandedHitCount(), 2);

	// ⑤ 칸이 꺼진 손도끼는 공격 간격 그대로.
	if (TestTrue(TEXT("손도끼를 들었다"), Equipment->EquipItemByItemID(FName(TEXT("item.hatchet")))))
	{
		AddInfo(FString::Printf(TEXT("[FixedLength] item.hatchet 동작 길이 %.2f초 (공격 간격 %.2f)"),
			Melee->GetActionLengthSeconds(), TestAttackIntervalSeconds));
		TestEqual(TEXT("⑤ 손도끼 동작 길이 = 공격 간격"), Melee->GetActionLengthSeconds(), TestAttackIntervalSeconds, TestTolerance);
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeRepressKillsCapturableTest,
	"Snarehaven.Combat.Melee.RepressKillsCapturable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 포획 창 동물도 버튼을 **다시 누르면** 그 타가 나가 죽는다 (사냥을 고르는 것은 플레이어의 재입력이다,
// DESIGN 3.1 — 누르고 있어도 반복이 없으니 5타째는 언제나 새 누름이다, 2026-10-04).
bool FSnarehavenMeleeRepressKillsCapturableTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	UAnimalStateComponent* State = Animal->GetAnimalState();

	// 포획 창에 걸쳐 둔다 — 남은 체력이 1타 데미지 이하라 다음 한 대면 죽는다.
	State->ApplyDamage(TestMaxHP - State->GetCaptureThresholdHP());
	if (!TestEqual(TEXT("포획 창이 열렸다"), State->GetState(), EAnimalState::Capturable))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 다시 누른다. 첫 타는 타격 지연 뒤에 나간다 (DESIGN 3.1) — 나가는 것 자체는 막히지 않는다.
	Melee->SetAttackHitDelaySeconds(TestAttackHitDelaySeconds);
	Melee->StartAttacking();
	TestEqual(TEXT("누른 순간에는 아직 살아 있다"), State->GetState(), EAnimalState::Capturable);

	AdvanceTimers(World, TestAttackHitDelaySeconds + TestTimeToleranceSeconds);
	TestEqual(TEXT("다시 누른 첫 타는 그대로 나가 죽인다"), State->GetState(), EAnimalState::Dead);

	Melee->StopAttacking();
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeFalloffTest,
	"Snarehaven.Combat.Melee.DamageFallsOffWithDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M11-04 · DESIGN 8.6-b 곡선: `d <= R*s` 는 온전 · 그 밖은 `m` 까지 선형 · 끝거리는 `m`.
// 검사 전용 계수(0.5 / 0.25)로 돌린다 — 설계값(근접 1.0/1.0)은 감쇠가 **없는** 쪽이라
// 그대로 쓰면 곡선을 지워도 검사가 통과한다.
bool FSnarehavenMeleeFalloffTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	Melee->SetFalloffSettings(TestFalloffStartRatio, TestMinDamageRatio);

	// 먼저 비율 자체를 본다 — 동물을 때리지 않고도 곡선의 세 구간이 다 보인다.
	TestEqual(TEXT("감쇠 시작점까지는 온전하다"),
		Melee->GetDamageRatioAtDistance(TestFullDamageDistanceCm), 1.f, TestTolerance);
	TestEqual(TEXT("감쇠 구간 한가운데는 1.0 과 m 의 한가운데다"),
		Melee->GetDamageRatioAtDistance(TestMidFalloffDistanceCm),
		(1.f + TestMinDamageRatio) * 0.5f, TestTolerance);
	TestEqual(TEXT("끝거리는 m 이다"),
		Melee->GetDamageRatioAtDistance(TestEdgeDistanceCm), TestMinDamageRatio, TestTolerance);

	// 그리고 실제 타격이 그 비율대로 깎는지 본다 (비율만 맞고 데미지에 안 쓰이면 여기서 걸린다).
	AAnimalCharacter* Far = SpawnTestAnimal(World, TestEdgeDistanceCm);
	if (!TestNotNull(TEXT("끝거리 동물"), Far))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("끝거리에서도 한 대 들어간다"), Melee->AttackOnce());
	TestEqual(TEXT("끝거리 데미지는 1타 데미지의 m 배다"),
		Far->GetAnimalState()->GetCurrentHP(),
		TestMaxHP - TestAttackDamage * TestMinDamageRatio, TestTolerance);

	// 감쇠 없음(설계의 근접 행)으로 되돌리면 같은 거리에서 온전한 데미지가 들어간다.
	Melee->SetFalloffSettings(1.f, 1.f);
	TestTrue(TEXT("감쇠를 끄고 한 대 더"), Melee->AttackOnce());
	TestEqual(TEXT("감쇠가 없으면 끝거리도 온전한 데미지다"),
		Far->GetAnimalState()->GetCurrentHP(),
		TestMaxHP - TestAttackDamage * TestMinDamageRatio - TestAttackDamage, TestTolerance);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeWeaponRowRangeTest,
	"Snarehaven.Combat.Melee.WeaponRowOverridesRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M11-04 · 사거리의 원본은 **손에 든 무기의 표 한 줄**이다 (DESIGN 8.6-b 데이터 자리).
// 컴포넌트 기본값을 검사 전용 값으로 갈아 둔 채 몽둥이를 들어, 실사거리가 **표 값**으로 바뀌는지 본다.
bool FSnarehavenMeleeWeaponRowRangeTest::RunTest(const FString& Parameters)
{
	UDataTable* ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	if (!TestNotNull(TEXT("아이템 표를 읽었다"), ItemTable))
	{
		return false;
	}

	const FItemRow* ClubRow = ItemTable->FindRow<FItemRow>(ClubRowName, TEXT("MeleeWeaponRowRangeTest"));
	if (!TestNotNull(TEXT("몽둥이 행이 있다"), ClubRow))
	{
		return false;
	}

	// 표가 비어 있으면 아래 비교가 "기본값 == 기본값" 으로 조용히 통과한다 — 먼저 막는다.
	if (!TestTrue(TEXT("몽둥이 행에 사거리가 적혀 있다"), ClubRow->AttackRangeCm > 0.f))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	UEquipmentComponent* Equipment = Player->GetEquipment();
	if (!TestNotNull(TEXT("장비 컴포넌트"), Equipment)
		|| !TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 맨손일 때는 컴포넌트 기본값(검사 전용 300)이다.
	TestEqual(TEXT("맨손 실사거리는 컴포넌트 기본값"),
		Melee->GetEffectiveAttackRangeCm(), TestAttackRangeCm, TestTolerance);

	if (!TestTrue(TEXT("몽둥이를 들었다"), Equipment->EquipItem(FName(ClubRowName))))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestEqual(TEXT("든 뒤 실사거리는 표 값이다"),
		Melee->GetEffectiveAttackRangeCm(), ClubRow->AttackRangeCm, TestTolerance);
	TestNotEqual(TEXT("표 값이 컴포넌트 기본값과 다르다 (덮어쓰기가 실제로 일어났다)"),
		Melee->GetEffectiveAttackRangeCm(), TestAttackRangeCm);

	// 채집·목장·존이 쓰는 거리는 무기를 따라가지 않는다 (총기가 들어와도 베리 따는 거리는 그대로).
	TestEqual(TEXT("맨손 사거리 getter 는 무기에 안 흔들린다"),
		Melee->GetAttackRangeCm(), TestAttackRangeCm, TestTolerance);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeStaminaTest,
	"Snarehaven.Combat.Melee.AttackCostsNoStamina",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M11-04 · **스태미나 = 수영 전용** (DESIGN 3.1, 사용자 방향 2026-09-23). 원래 "한 대마다 표의 소모값이
// 빠지고 모자라면 타격이 안 나간다" 를 보던 자리다 — 표의 공격 소모가 0 이 되어 뜻을 뒤집었다:
// **몇 대를 쳐도 스태미나가 안 줄고, 스태미나 때문에 타격이 막히지 않는다.**
// 소모 길 자체는 `Snarehaven.PlayerVitals.*` 가 검사 전용 값으로 계속 본다 (수영(8.9)이 그 길을 쓴다).
bool FSnarehavenMeleeStaminaTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm, TestMaxHP * 100.f);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	UPlayerVitalsComponent* Vitals = Player->GetPlayerVitals();
	if (!TestNotNull(TEXT("생존 컴포넌트"), Vitals)
		|| !TestTrue(TEXT("표 값으로 초기화됐다"), Vitals->GetMaxStamina() > 0.f))
	{
		DestroyTestWorld(World);
		return false;
	}

	const float Before = Vitals->GetStamina();
	TestTrue(TEXT("한 대 들어갔다"), Melee->AttackOnce());
	TestEqual(TEXT("한 대 쳐도 스태미나가 그대로다"), Vitals->GetStamina(), Before, TestTolerance);

	// 예전 값(5/타)이었다면 스무 대를 채우기 전에 바닥나 타격이 막혔을 횟수다.
	// 동물 HP 는 넉넉히 줬으니(위 스폰) 죽어서 멈추는 일은 없다.
	constexpr int32 SwingCount = 20;
	for (int32 Swing = 0; Swing < SwingCount; ++Swing)
	{
		const float HpBeforeSwing = Animal->GetAnimalState()->GetCurrentHP();
		if (!TestTrue(FString::Printf(TEXT("%d타째도 나간다"), Swing + 2), Melee->AttackOnce()))
		{
			break;
		}

		// "나갔다" 를 반환값 하나로 믿지 않는다 — 동물 HP 가 실제로 줄어야 친 것이다 (PROMPT ③-12).
		TestTrue(TEXT("친 만큼 동물 HP 가 준다"),
			Animal->GetAnimalState()->GetCurrentHP() < HpBeforeSwing);
	}

	TestEqual(TEXT("스무 대를 더 쳐도 스태미나가 그대로다"), Vitals->GetStamina(), Before, TestTolerance);
	TestTrue(TEXT("스태미나 때문에 막히는 타격이 없다"), Vitals->CanAttack());

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeHitCountTest,
	"Snarehaven.Combat.Melee.CountsLandedHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 3층 로그가 "몇 타째" 를 찍으려면 세는 값이 **맞춘 것만** 세야 한다 — 빗나간 입력까지 세면
// 로그의 타수와 동물 HP 가 어긋나 인용이 또 증거로 못 선다 (루프#153 ⓑ).
bool FSnarehavenMeleeHitCountTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	// HP 를 넉넉히 줘서 세는 동안 동물이 죽지 않게 한다 (죽으면 그 뒤 타격이 안 들어가 타수가 멈춘다).
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm, TestMaxHP * 100.f);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	TestEqual(TEXT("치기 전에는 0타"), Melee->GetLandedHitCount(), 0);

	constexpr int32 TestHitsToCount = 5;
	for (int32 Hit = 0; Hit < TestHitsToCount; ++Hit)
	{
		Melee->AttackOnce();
	}
	TestEqual(TEXT("맞춘 만큼 센다"), Melee->GetLandedHitCount(), TestHitsToCount);

	// 사거리 밖으로 옮겨 놓고 또 쳐도 타수는 안 는다 — 데미지가 안 들어갔으니 타격이 아니다.
	Animal->SetActorLocation(FVector(TestOutOfRangeDistanceCm, 0.f, 0.f));
	Melee->AttackOnce();
	TestEqual(TEXT("빗나간 입력은 안 센다"), Melee->GetLandedHitCount(), TestHitsToCount);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeShortTapTest,
	"Snarehaven.Combat.Melee.ShortTapKeepsSwingAndHits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 🔴 결함(감독←사용자, 2026-09-23 16:40 ①): 짧게 누르면 휘두르기가 몇 프레임만 돌고 대기로 툭 끊겼다.
// `StopAttacking` 이 그 프레임에 동작 상태를 꺼 버려서다. 이제 떼는 것은 **누름 상태만** 끄고,
// 시작한 한 바퀴(= 공격 간격)는 끝까지 돌며 그 안의 한 대도 그대로 나간다.
bool FSnarehavenMeleeShortTapTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	Melee->SetAttackHitDelaySeconds(TestAttackHitDelaySeconds);

	// 타격 지연보다 짧게 눌렀다 뗀다 — 사람이 화면을 탭하는 길이다.
	constexpr float TestTapSeconds = 0.1f;
	Melee->StartAttacking();
	AdvanceTimers(World, TestTapSeconds);
	Melee->StopAttacking();

	TestFalse(TEXT("뗐으니 누름 상태는 꺼진다"), Melee->IsAttacking());
	TestTrue(TEXT("떼도 휘두르기는 계속 돈다"), Melee->IsSwinging());
	TestEqual(TEXT("아직 타격 지연 전이라 안 맞았다"), Melee->GetLandedHitCount(), 0);

	// 뗀 뒤에도 예약된 타격이 나간다. 타이머는 지연 **직후** 프레임에 울리므로 허용오차만큼 더 흘린다.
	float ElapsedSeconds = TestAttackHitDelaySeconds + TestTimeToleranceSeconds;
	AdvanceTimers(World, ElapsedSeconds - TestTapSeconds);
	TestEqual(TEXT("떼도 한 대는 나간다"), Melee->GetLandedHitCount(), 1);

	// 휘두르기가 꺼지는 시각을 잰다 — 한 바퀴는 공격 간격과 같아야 한다 (DESIGN 3.1).
	float SwingSeconds = -1.f;
	for (int32 Step = 0; Step < FMath::RoundToInt(TestAttackIntervalSeconds * 3.f / TestTimerStepSeconds); ++Step)
	{
		if (!Melee->IsSwinging())
		{
			SwingSeconds = ElapsedSeconds;
			break;
		}

		TickTimerStep(World);
		ElapsedSeconds += TestTimerStepSeconds;
	}

	AddInfo(FString::Printf(TEXT("탭 %.2f초 → 휘두르기 유지 %.3f초 (간격 %.2f초)"),
		TestTapSeconds, SwingSeconds, TestAttackIntervalSeconds));
	TestEqual(TEXT("휘두르기 한 바퀴 = 공격 간격"),
		SwingSeconds, TestAttackIntervalSeconds, TestTimeToleranceSeconds);
	TestEqual(TEXT("한 번 탭에 한 대뿐"), Melee->GetLandedHitCount(), 1);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeHitDelayTest,
	"Snarehaven.Combat.Melee.HitLandsAfterSwingDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 🔴 결함 ③: 데미지가 클립 타격보다 먼저 들어갔다 — 누른 순간에 판정했기 때문이다.
// 이제 **휘두르기 시작 + 타격 지연**에 판정한다. 값은 테스트 전용(0.3/1.0)이라 설계 숫자가 바뀌어도
// 이 검사는 안 깨진다 — 설계값 그대로의 시각은 아래 `DesignSwingTiming` 이 본다.
bool FSnarehavenMeleeHitDelayTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm, TestMaxHP * 10.f);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	Melee->SetAttackHitDelaySeconds(TestAttackHitDelaySeconds);

	// 누르고, 간격이 끝나는 순간 다시 누른다 (한 번 누름 = 한 번, 2026-10-04). 판정 시각은 지연 · 지연+간격.
	TArray<float> HitSeconds;
	int32 LastHitCount = 0;
	bool bRepressed = false;
	Melee->StartAttacking();
	const int32 Steps = FMath::RoundToInt(
		(TestAttackIntervalSeconds + TestAttackHitDelaySeconds + TestTimerStepSeconds) * 2.f / TestTimerStepSeconds);
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		TickTimerStep(World);
		if (!bRepressed && Melee->IsActionReady())
		{
			Melee->StopAttacking();
			Melee->StartAttacking();
			bRepressed = true;
		}
		if (Melee->GetLandedHitCount() > LastHitCount)
		{
			LastHitCount = Melee->GetLandedHitCount();
			HitSeconds.Add(static_cast<float>(Step + 1) * TestTimerStepSeconds);
		}
	}
	Melee->StopAttacking();

	AddInfo(FString::Printf(TEXT("판정 시각 %s (지연 %.2f · 간격 %.2f)"),
		*FString::JoinBy(HitSeconds, TEXT(", "), [](float Seconds) { return FString::Printf(TEXT("%.3f"), Seconds); }),
		TestAttackHitDelaySeconds, TestAttackIntervalSeconds));

	if (!TestTrue(TEXT("두 바퀴 동안 두 번 판정됐다"), HitSeconds.Num() >= 2))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestEqual(TEXT("첫 판정은 휘두르기 시작 + 지연"),
		HitSeconds[0], TestAttackHitDelaySeconds, TestTimeToleranceSeconds);
	TestEqual(TEXT("둘째 판정은 거기서 한 간격 뒤"),
		HitSeconds[1], TestAttackHitDelaySeconds + TestAttackIntervalSeconds, TestTimeToleranceSeconds);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeDesignSwingTimingTest,
	"Snarehaven.Combat.Melee.DesignSwingTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 결함(감독←사용자, 2026-09-23 16:40)이 건 합격 조건 그대로 — **설계 숫자로** 잰다:
// 0.1초 눌렀다 떼면 휘두르기가 한 간격 유지 · 꾹 누르면 판정 시각이 지연·지연+간격(±0.05).
// M11-05 부터 그 두 숫자를 **표에서 읽는다** — 검사가 값을 들고 있지 않으니 밸런스를 표에서 바꾸면
// 여기도 같이 따라온다. 표가 DESIGN 3.1 과 같은지는 `TableMatchesDesign` 이 따로 본다 (DataDriven.md 3).
bool FSnarehavenMeleeDesignSwingTimingTest::RunTest(const FString& Parameters)
{
	constexpr float DesignTapSeconds = 0.1f;

	UDataTable* const CombatTable = LoadObject<UDataTable>(nullptr, CombatTablePath);
	if (!TestNotNull(TEXT("DT_PlayerCombatTable 로드"), CombatTable))
	{
		return false;
	}

	const FPlayerCombatRow* const CombatRow =
		CombatTable->FindRow<FPlayerCombatRow>(CombatRowName, TEXT("DesignSwingTiming"));
	if (!TestNotNull(TEXT("`Player` 행이 있다"), CombatRow))
	{
		return false;
	}

	const float DesignAttackIntervalSeconds = CombatRow->AttackIntervalSeconds;
	const float DesignAttackHitDelaySeconds = CombatRow->AttackHitDelaySeconds;
	AddInfo(FString::Printf(TEXT("표에서 읽은 설계 숫자 — 간격 %.2f초 · 타격 지연 %.2f초"),
		DesignAttackIntervalSeconds, DesignAttackHitDelaySeconds));

	UWorld* World = CreateTestWorld();

	// **공격 숫자를 덮어쓰지 않는다** — 설계 기본값 그대로 도는 유일한 검사다.
	ASnarehavenCharacter* Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm, TestMaxHP * 10.f);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* Melee = Player->GetMeleeAttack();
	TestEqual(TEXT("공격 간격이 표 값 (DT_PlayerCombatTable)"),
		Melee->GetAttackIntervalSeconds(), DesignAttackIntervalSeconds, TestTolerance);
	TestEqual(TEXT("타격 지연이 표 값 (DT_PlayerCombatTable)"),
		Melee->GetAttackHitDelaySeconds(), DesignAttackHitDelaySeconds, TestTolerance);

	// ① 누르고 간격이 끝나는 순간 다시 누름 — 1.2초 동안 판정 시각을 모은다 (한 번 누름 = 한 번, 2026-10-04).
	TArray<float> HitSeconds;
	int32 LastHitCount = 0;
	bool bRepressed = false;
	Melee->StartAttacking();
	const int32 HoldSteps = FMath::RoundToInt(DesignAttackIntervalSeconds * 2.f / TestTimerStepSeconds);
	for (int32 Step = 0; Step < HoldSteps; ++Step)
	{
		TickTimerStep(World);
		if (!bRepressed && Melee->IsActionReady())
		{
			Melee->StopAttacking();
			Melee->StartAttacking();
			bRepressed = true;
		}
		if (Melee->GetLandedHitCount() > LastHitCount)
		{
			LastHitCount = Melee->GetLandedHitCount();
			HitSeconds.Add(static_cast<float>(Step + 1) * TestTimerStepSeconds);
		}
	}
	Melee->StopAttacking();

	AddInfo(FString::Printf(TEXT("간격마다 다시 누름 %.1f초 — 판정 시각 %s"), DesignAttackIntervalSeconds * 2.f,
		*FString::JoinBy(HitSeconds, TEXT(", "), [](float Seconds) { return FString::Printf(TEXT("%.3f"), Seconds); })));

	if (TestTrue(TEXT("두 간격 동안 두 번 맞는다"), HitSeconds.Num() >= 2))
	{
		TestEqual(TEXT("첫 판정은 표의 타격 지연에"),
			HitSeconds[0], DesignAttackHitDelaySeconds, TestTimeToleranceSeconds);
		TestEqual(TEXT("둘째 판정은 거기서 표의 한 간격 뒤"),
			HitSeconds[1], DesignAttackHitDelaySeconds + DesignAttackIntervalSeconds, TestTimeToleranceSeconds);
	}

	// ② 0.1초 탭 — 휘두르기 유지 시간을 잰다. 앞선 휘두르기가 끝나도록 한 바퀴 비워 두고 시작한다.
	AdvanceTimers(World, DesignAttackIntervalSeconds);
	Melee->StartAttacking();
	AdvanceTimers(World, DesignTapSeconds);
	Melee->StopAttacking();
	TestTrue(TEXT("0.1초 탭 뒤에도 휘두르는 중"), Melee->IsSwinging());

	float SwingSeconds = -1.f;
	const int32 SwingSteps = FMath::RoundToInt(DesignAttackIntervalSeconds * 3.f / TestTimerStepSeconds);
	for (int32 Step = 0; Step < SwingSteps; ++Step)
	{
		if (!Melee->IsSwinging())
		{
			SwingSeconds = static_cast<float>(Step) * TestTimerStepSeconds + DesignTapSeconds;
			break;
		}

		TickTimerStep(World);
	}

	AddInfo(FString::Printf(TEXT("0.1초 탭 — 휘두르기 유지 %.3f초"), SwingSeconds));
	TestEqual(TEXT("탭 한 번에 휘두르기가 표의 한 간격만큼 유지된다"),
		SwingSeconds, DesignAttackIntervalSeconds, TestTimeToleranceSeconds);

	DestroyTestWorld(World);
	return true;
}

// M11-05 — 감독 지시(2026-09-23 17:25)가 건 검증 조건 그대로: **표의 지연을 바꾸면 판정 시각이 따라온다.**
// 코드에 박힌 값이라면 행을 어떻게 갈아도 판정 시각이 0.2 에 머문다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeRowDrivesHitDelayTest,
	"Snarehaven.Combat.Melee.RowDrivesHitDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeleeRowDrivesHitDelayTest::RunTest(const FString& Parameters)
{
	// 같은 행에서 **지연 칸만** 두 값으로 갈아 본다 (0.3 → 0.5). 간격 1.0 보다 짧아 다음 휘두르기와 겹치지 않는다.
	const TArray<float> DelaysToTry = { TestAttackHitDelaySeconds, TestAttackHitDelaySeconds + 0.2f };

	for (const float Delay : DelaysToTry)
	{
		UWorld* World = CreateTestWorld();
		ASnarehavenCharacter* Player =
			World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
		AAnimalCharacter* Animal = SpawnTestAnimal(World, TestNearDistanceCm, TestMaxHP * 10.f);
		if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("동물 스폰"), Animal))
		{
			DestroyTestWorld(World);
			return false;
		}

		UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();

		FPlayerCombatRow Row = MakeTestCombatRow();
		Row.AttackHitDelaySeconds = Delay;
		if (!TestTrue(TEXT("검사 전용 행으로 채워진다"), Melee->ApplyCombatRow(Row)))
		{
			DestroyTestWorld(World);
			return false;
		}

		TestEqual(TEXT("행의 타격 지연이 그대로 들어갔다"), Melee->GetAttackHitDelaySeconds(), Delay, TestTolerance);
		TestEqual(TEXT("행의 간격도 같이 들어갔다"),
			Melee->GetAttackIntervalSeconds(), TestAttackIntervalSeconds, TestTolerance);
		TestEqual(TEXT("행의 데미지도 같이 들어갔다"), Melee->GetAttackDamage(), TestAttackDamage, TestTolerance);
		TestEqual(TEXT("행의 사거리도 같이 들어갔다"),
			Melee->GetAttackRangeCm(), TestAttackRangeCm, TestTolerance);

		// 한 번 휘둘러 **실제 판정 시각**을 잰다 (getter 만 보면 지연을 무시하고 때려도 통과한다).
		float HitSeconds = -1.f;
		const int32 Steps = FMath::RoundToInt(TestAttackIntervalSeconds / TestTimerStepSeconds);
		Melee->SwingOnce();
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			TickTimerStep(World);
			if (Melee->GetLandedHitCount() > 0)
			{
				HitSeconds = static_cast<float>(Step + 1) * TestTimerStepSeconds;
				break;
			}
		}

		AddInfo(FString::Printf(TEXT("표의 타격 지연 %.2f초 -> 판정 시각 %.3f초"), Delay, HitSeconds));
		TestEqual(FString::Printf(TEXT("판정 시각이 표의 %.2f초를 따라온다"), Delay),
			HitSeconds, Delay, TestTimeToleranceSeconds);

		DestroyTestWorld(World);
	}

	return true;
}


// 미정(-1) 행으로는 **아무 칸도** 바뀌지 않는다 (Verification.md 7) — 반쪽만 채우면
// "사거리는 표, 지연은 옛 값" 같은 섞인 설정으로 돌게 된다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeUnsetRowChangesNothingTest,
	"Snarehaven.Combat.Melee.UnsetRowChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeleeUnsetRowChangesNothingTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = SpawnTestPlayer(World);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player))
	{
		DestroyTestWorld(World);
		return false;
	}

	UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	Melee->SetAttackHitDelaySeconds(TestAttackHitDelaySeconds);

	FPlayerCombatRow HalfEmpty = MakeTestCombatRow();
	HalfEmpty.AttackHitDelaySeconds = -1.f;   // 아직 안 채운 칸

	// 컴포넌트가 남기는 경고(미정 행)는 그대로 로그에 두고 AddExpectedError 로 잡지 않는다 —
	// 기존 검사들(Gather.UnsetNumbersStayOff 등)과 같은 취급이고, 경고는 검사를 실패시키지 않는다.
	TestFalse(TEXT("미정 칸이 있는 행은 거절된다"), Melee->ApplyCombatRow(HalfEmpty));
	TestEqual(TEXT("거절된 뒤 타격 지연이 그대로"),
		Melee->GetAttackHitDelaySeconds(), TestAttackHitDelaySeconds, TestTolerance);
	TestEqual(TEXT("거절된 뒤 데미지도 그대로 (SpawnTestPlayer 값)"),
		Melee->GetAttackDamage(), TestAttackDamage, TestTolerance);

	DestroyTestWorld(World);
	return true;
}


// 실제 플레이가 지나가는 길: 캐릭터를 그냥 세우면 **아무도 세터를 안 불러도** 네 칸이 표 값으로 서 있다
// (`ASnarehavenCharacter::PostInitializeComponents`). 이름이 아니라 **값**으로 확인한다 (PROMPT ③-12).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeCharacterReadsTableTest,
	"Snarehaven.Combat.Melee.CharacterReadsTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeleeCharacterReadsTableTest::RunTest(const FString& Parameters)
{
	UDataTable* const CombatTable = LoadObject<UDataTable>(nullptr, CombatTablePath);
	if (!TestNotNull(TEXT("DT_PlayerCombatTable 로드"), CombatTable))
	{
		return false;
	}

	const FPlayerCombatRow* const Row =
		CombatTable->FindRow<FPlayerCombatRow>(CombatRowName, TEXT("CharacterReadsTable"));
	if (!TestNotNull(TEXT("`Player` 행이 있다"), Row))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player =
		World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player))
	{
		DestroyTestWorld(World);
		return false;
	}

	const UMeleeAttackComponent* const Melee = Player->GetMeleeAttack();
	AddInfo(FString::Printf(TEXT("표에서 채워진 값 - 데미지 %.1f · 사거리 %.0fcm · 간격 %.2f초 · 타격 지연 %.2f초"),
		Melee->GetAttackDamage(), Melee->GetAttackRangeCm(),
		Melee->GetAttackIntervalSeconds(), Melee->GetAttackHitDelaySeconds()));

	TestEqual(TEXT("데미지가 표 값"), Melee->GetAttackDamage(), Row->AttackDamage, TestTolerance);
	TestEqual(TEXT("사거리가 표 값"), Melee->GetAttackRangeCm(), Row->AttackRangeCm, TestTolerance);
	TestEqual(TEXT("간격이 표 값"), Melee->GetAttackIntervalSeconds(), Row->AttackIntervalSeconds, TestTolerance);
	TestEqual(TEXT("타격 지연이 표 값"),
		Melee->GetAttackHitDelaySeconds(), Row->AttackHitDelaySeconds, TestTolerance);

	DestroyTestWorld(World);
	return true;
}


// 표가 DESIGN 3.1 "확정 수치" 표와 같은지. **기대값을 하나도 들고 있지 않다** — 설계서를 파일에서 읽는다.
// 한쪽만 고치면 운다 (`Snarehaven.PlayerVitals.Table.MatchesDesign` 과 같은 짜임).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeTableMatchesDesignTest,
	"Snarehaven.Combat.Melee.TableMatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeleeTableMatchesDesignTest::RunTest(const FString& Parameters)
{
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
	FString Doc;
	if (!TestTrue(*FString::Printf(TEXT("설계서를 읽는다 (%s)"), *DocPath),
		FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}

	const FString Section = ExtractDesignSection(Doc, TEXT("### 3.1"));
	if (!TestTrue(TEXT("DESIGN 3.1 절을 찾았다"), !Section.IsEmpty()))
	{
		return false;
	}

	const TMap<FString, double> DesignValues = ParseLabelledNumbers(Section);
	if (!TestTrue(TEXT("DESIGN 3.1 에서 숫자 줄을 하나 이상 읽었다"), DesignValues.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("DESIGN 3.1 에서 숫자 줄 %d개를 읽었다"), DesignValues.Num()));

	UDataTable* const CombatTable = LoadObject<UDataTable>(nullptr, CombatTablePath);
	if (!TestNotNull(TEXT("DT_PlayerCombatTable 로드"), CombatTable))
	{
		return false;
	}

	const FPlayerCombatRow* const Row =
		CombatTable->FindRow<FPlayerCombatRow>(CombatRowName, TEXT("CombatTableMatchesDesign"));
	if (!TestNotNull(TEXT("`Player` 행이 있다"), Row))
	{
		return false;
	}

	AddInfo(FString::Printf(TEXT("[Player] 1타 %.0f · 사거리 %.0fcm · 간격 %.2f초 · 타격 지연 %.2f초"),
		Row->AttackDamage, Row->AttackRangeCm, Row->AttackIntervalSeconds, Row->AttackHitDelaySeconds));

	for (const FString& Issue : CollectCombatIssues(*Row, DesignValues))
	{
		AddError(Issue);
	}

	return true;
}


// 위 검사가 "표가 멀쩡해서" 통과할 때도 비교 로직이 살아 있는지 본다 (역검증).
// 에셋·설계서를 건드리지 않고 행과 문서 값을 손으로 지어 먹인다 (Verification.md 6·13).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMeleeTableSelfTest,
	"Snarehaven.Combat.Melee.TableCheckCatchesBrokenRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMeleeTableSelfTest::RunTest(const FString& Parameters)
{
	TMap<FString, double> DesignValues;
	for (const FCombatField& Field : CombatFields)
	{
		DesignValues.Add(Field.DesignLabel, 7.0);
	}

	FPlayerCombatRow GoodRow;
	for (const FCombatField& Field : CombatFields)
	{
		GoodRow.*(Field.Member) = 7.0f;
	}
	TestEqual(TEXT("문서와 같은 행은 문제 0개"), CollectCombatIssues(GoodRow, DesignValues).Num(), 0);

	FPlayerCombatRow BadRow = GoodRow;
	BadRow.AttackHitDelaySeconds = 99.f;
	TestEqual(TEXT("타격 지연 한 칸만 달라도 잡는다"), CollectCombatIssues(BadRow, DesignValues).Num(), 1);

	// 문서 줄을 못 읽는 경우도 문제로 센다 (빈 통과 막기).
	TestEqual(TEXT("문서를 못 읽으면 모든 칸이 문제"),
		CollectCombatIssues(GoodRow, TMap<FString, double>()).Num(), static_cast<int32>(UE_ARRAY_COUNT(CombatFields)));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
