// ROADMAP M5-07d 2층 검사 — 동물의 **걷기·도주 클립**이 코드가 아니라 행(`WalkAnimRef`·`FleeAnimRef`)에서
// 오는지, 빈 칸이 대기로 떨어지는지, 그리고 **공격 숫자 칸이 DESIGN 3.1·8.7 표와 같은지**.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "SnarehavenCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/AnimalRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ranch/RanchPetActor.h"

namespace
{
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");

	// M5-07b 가 들인 진짜 에셋 — 여우는 클립 6개를 다 가졌다. "빈 칸" 표본은 행을 손으로 비워 만든다
	// (돼지·양은 M41-02·03 에서 걷기·도주를 받았다).
	const TCHAR* FoxMeshPath = TEXT("/Game/Game/Characters/Animals/Fox/SK_Animal_Fox.SK_Animal_Fox");
	const TCHAR* FoxIdlePath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Idle.A_Animal_Fox_Idle");
	const TCHAR* FoxWalkPath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Walk.A_Animal_Fox_Walk");
	const TCHAR* FoxRunPath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Run.A_Animal_Fox_Run");
	const TCHAR* FoxDeathPath = TEXT("/Game/Game/Characters/Animals/Fox/A_Animal_Fox_Death.A_Animal_Fox_Death");

	// **검사 전용 값** (Verification.md 6) — 밸런스 숫자가 바뀌어도 안 깨지게 설계서 값을 안 쓴다.
	constexpr float TestMaxHP = 50.f;
	constexpr float TestCaptureThresholdPct = 10.f;
	constexpr float TestMoveSpeed = 40.f;
	constexpr float TestNickDamage = 1.f;
	constexpr float TestDeltaSeconds = 0.1f;
	constexpr int32 TestTickCount = 10;
	constexpr float TestThreatDistance = 400.f;
	// 펫이 걷기 시작할 때까지 틱하는 길이 — 서 있는 시간 최대값보다 넉넉히 (`SnarehavenRanchPetTest` 와 같은 30fps).
	constexpr float TestPetFrameSeconds = 1.f / 30.f;
	constexpr int32 TestPetMaxFrames = 30 * 20;

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

	/**
	 * 이동 컴포넌트까지 한 프레임 돌린다 (손으로 만든 테스트 월드는 컴포넌트 틱을 등록하지 않는다).
	 * 간격을 받는 이유는 한계 검사뿐이다 — 경계를 도는 기하는 게임이 실제로 밟는 1/60 걸음으로 재야 한다.
	 */
	void TickAnimalOnce(AAnimalCharacter& Animal, float DeltaSeconds = TestDeltaSeconds)
	{
		Animal.Tick(DeltaSeconds);
		Animal.GetCharacterMovement()->TickComponent(DeltaSeconds, LEVELTICK_All, nullptr);
	}

	/** 클립 칸만 다른 행을 만든다 — 숫자는 전부 검사 전용 값이라 설계서가 바뀌어도 안 깨진다. */
	FAnimalRow MakeClipRow(const TCHAR* IdlePath, const TCHAR* WalkPath, const TCHAR* FleePath)
	{
		FAnimalRow Row;
		Row.MaxHP = TestMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Row.MoveSpeed = TestMoveSpeed;
		Row.MeshRef = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(FoxMeshPath));
		Row.IdleAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(IdlePath));
		Row.DeathAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FoxDeathPath));
		if (WalkPath != nullptr)
		{
			Row.WalkAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(WalkPath));
		}
		if (FleePath != nullptr)
		{
			Row.FleeAnimRef = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(FleePath));
		}
		return Row;
	}

	/** 행을 적용하고 도망갈 상대까지 세운 동물 하나. 실패하면 nullptr. */
	AAnimalCharacter* SpawnAnimalWithRow(UWorld& World, const FAnimalRow& Row)
	{
		AAnimalCharacter* Animal = World.SpawnActor<AAnimalCharacter>();
		if (Animal == nullptr)
		{
			return nullptr;
		}
		Animal->ApplyBodyFromRow(Row);
		Animal->ApplyMoveSpeedFromRow(Row);
		Animal->GetAnimalState()->InitializeFromRow(Row);
		return Animal;
	}
}


// 사용자 지시 2026-09-23 16:55 ③ 이 건 조건 그대로 — **도망갈 때 도주 클립이 돈다.**
// 이 검사가 없던 동안 12종이 대기 클립인 채로 미끄러지며 달아났다. 판단은 이름이 아니라 실제로
// 걸린 클립 객체와 그때의 속도로 한다 (PROMPT ③-12).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalFleeClipTest, "Snarehaven.Animal.Clips.FleeClipPlaysWhileFleeing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalFleeClipTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	const FAnimalRow Row = MakeClipRow(FoxIdlePath, FoxWalkPath, FoxRunPath);
	AAnimalCharacter* Animal = SpawnAnimalWithRow(*World, Row);
	if (Animal == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	UAnimSequence* const IdleAnim = Row.IdleAnimRef.LoadSynchronous();
	UAnimSequence* const FleeAnim = Row.FleeAnimRef.LoadSynchronous();
	if (!TestNotNull(TEXT("대기 클립을 불러왔다"), IdleAnim) ||
		!TestNotNull(TEXT("도주 클립을 불러왔다"), FleeAnim))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 서 있는 동안은 대기다 — 여기서부터 갈리는지를 봐야 "도주 클립이 돈다" 가 뜻이 있다.
	TestEqual(TEXT("가만히 있으면 대기 클립"), Animal->GetCurrentBodyAnim(), IdleAnim);

	// 도망칠 상대를 세운다 — 상대가 없으면 방향이 영벡터라 동물이 안 움직인다.
	ASnarehavenCharacter* const Threat = World->SpawnActor<ASnarehavenCharacter>();
	if (Threat == nullptr)
	{
		AddError(TEXT("도망칠 상대(플레이어)를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}
	Threat->SetActorLocation(Animal->GetActorLocation() + FVector(TestThreatDistance, 0.f, 0.f));

	// 바닥이 없는 검사 월드라 걸어서는 안 움직인다 (같은 이유로 몸 검사도 날아서 잰다).
	Animal->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);
	TestEqual(TEXT("한 대 맞으면 Fight"), Animal->GetAnimalState()->GetState(), EAnimalState::Fight);

	for (int32 Step = 0; Step < TestTickCount; ++Step)
	{
		TickAnimalOnce(*Animal);
	}

	const float FleeSpeed = Animal->GetVelocity().Size2D();
	AddInfo(FString::Printf(TEXT("도망 중 속도 %.1fcm/s · 걸린 클립 %s"), FleeSpeed,
		IsValid(Animal->GetCurrentBodyAnim()) ? *Animal->GetCurrentBodyAnim()->GetName() : TEXT("없음")));
	TestTrue(TEXT("실제로 움직이고 있다"), FleeSpeed > 0.f);
	TestEqual(TEXT("도망가는 동안은 도주 클립"), Animal->GetCurrentBodyAnim(), FleeAnim);

	// 죽으면 죽음 클립이 **그 자리에 남아야** 한다 — 다음 틱이 속도만 보고 대기로 되돌리면
	// 쓰러진 동물이 도로 일어선다(루프#167 이 실제로 한 번 냈던 결함).
	UAnimSequence* const DeathAnim = Row.DeathAnimRef.LoadSynchronous();
	if (!TestNotNull(TEXT("죽음 클립을 불러왔다"), DeathAnim))
	{
		DestroyTestWorld(World);
		return false;
	}
	Animal->GetAnimalState()->ApplyDamage(TestMaxHP);
	TestEqual(TEXT("체력을 다 잃으면 Dead"), Animal->GetAnimalState()->GetState(), EAnimalState::Dead);
	TestEqual(TEXT("죽은 프레임에 죽음 클립"), Animal->GetCurrentBodyAnim(), DeathAnim);
	for (int32 Step = 0; Step < TestTickCount; ++Step)
	{
		TickAnimalOnce(*Animal);
	}
	TestEqual(TEXT("죽은 뒤로도 죽음 클립 그대로"), Animal->GetCurrentBodyAnim(), DeathAnim);

	DestroyTestWorld(World);
	return true;
}


// **걷기·도주 칸이 둘 다 비면 대기로 떨어진다** (걷기만 빈 새 4종은 아래 M41-04 시험 — 도주 클립을 느리게 빌린다).
// 클립을 못 찾았을 때 코드가 다른 클립을 지어내면 "표를 안 채웠다" 가 화면에서 안 보인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalEmptyClipTest, "Snarehaven.Animal.Clips.EmptyClipFallsBackToIdle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalEmptyClipTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();

	// ① 걷기·도주 칸이 둘 다 빈 행 — 움직여도 대기만 돈다.
	const FAnimalRow EmptyRow = MakeClipRow(FoxIdlePath, nullptr, nullptr);
	AAnimalCharacter* Empty = SpawnAnimalWithRow(*World, EmptyRow);
	// ② 둘 다 채운 행 — 같은 속도에서 걷기가 돈다(도망 중이 아니므로 도주가 아니다).
	const FAnimalRow FullRow = MakeClipRow(FoxIdlePath, FoxWalkPath, FoxRunPath);
	AAnimalCharacter* Full = SpawnAnimalWithRow(*World, FullRow);
	if (Empty == nullptr || Full == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	UAnimSequence* const IdleAnim = EmptyRow.IdleAnimRef.LoadSynchronous();
	UAnimSequence* const WalkAnim = FullRow.WalkAnimRef.LoadSynchronous();
	if (!TestNotNull(TEXT("대기 클립을 불러왔다"), IdleAnim) ||
		!TestNotNull(TEXT("걷기 클립을 불러왔다"), WalkAnim))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 속도를 직접 넣는다 — 도망이 아닌 이동(M5-07d3 의 접근)은 아직 코드에 길이 없어서다.
	// 문턱(진입 9cm/s)보다 확실히 큰 값이면 되고, 숫자 자체에 뜻은 없다.
	for (AAnimalCharacter* Animal : { Empty, Full })
	{
		Animal->GetCharacterMovement()->Velocity = FVector(TestMoveSpeed, 0.f, 0.f);
		Animal->UpdateLocomotionAnimation();
	}

	TestEqual(TEXT("빈 칸이면 움직여도 대기 클립"), Empty->GetCurrentBodyAnim(), IdleAnim);
	TestEqual(TEXT("채운 칸이면 걷기 클립"), Full->GetCurrentBodyAnim(), WalkAnim);

	DestroyTestWorld(World);
	return true;
}


// 감독 지시 2026-09-23 17:20 이 건 검증 조건 — **표의 공격 숫자가 DESIGN 과 같은지 맞댄다.**
// 기대값은 이 파일에 하나도 없다: 1타 데미지·간격·사거리·선공 반경은 DESIGN 3.1 "동물 공격" 표에서,
// 행동 갈래는 8.7 "행동" 열에서 읽는다. 한쪽만 고치면 여기서 운다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalAttackTableTest, "Snarehaven.Animal.Attack.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalAttackTableTest::RunTest(const FString& Parameters)
{
	FString Doc;
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
	if (!TestTrue(FString::Printf(TEXT("설계서를 읽었다 (%s)"), *DocPath),
		FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}

	// ── DESIGN 3.1 "동물 공격" 표 ──────────────────────────────────────────────
	// 종별 데미지 줄: `| 동물 1타 데미지 | 여우 6 · 염소 8 · … · 새 4종 0 | …`
	TMap<FString, double> DesignDamage;
	double BirdDamage = -1.0;
	double DesignInterval = -1.0;
	double DesignRange = -1.0;
	double DesignAggro = -1.0;

	TArray<FString> Lines;
	Doc.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		if (!Line.StartsWith(TEXT("| 동물 1타 데미지 |")) && !Line.StartsWith(TEXT("| 동물 공격 간격 |")) &&
			!Line.StartsWith(TEXT("| 동물 공격 사거리 |")) && !Line.StartsWith(TEXT("| 선공 감지 반경 |")))
		{
			continue;
		}

		// **값 칸만 본다.** 세 번째 칸(근거)에도 숫자가 잔뜩 있어서(`여우 12 · 곰 40` 같은 계산 예시)
		// 줄 전체를 훑으면 기대값이 근거 문장에 덮인다.
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"));
		if (Cells.Num() < 2)
		{
			continue;
		}
		const FString Value = Cells[1].TrimStartAndEnd();

		if (Line.StartsWith(TEXT("| 동물 1타 데미지 |")))
		{
			// `여우 6 · 염소 8 · … · 새 4종 0` — 가운뎃점으로 끊고 "이름 숫자" 로 읽는다.
			TArray<FString> Entries;
			Value.ParseIntoArray(Entries, TEXT("·"));
			for (const FString& Entry : Entries)
			{
				FRegexMatcher Matcher(FRegexPattern(TEXT("^(.*?)\\s+([0-9.]+)$")), Entry.TrimStartAndEnd());
				if (!Matcher.FindNext())
				{
					continue;
				}
				const FString Key = Matcher.GetCaptureGroup(1);
				const double Number = FCString::Atod(*Matcher.GetCaptureGroup(2));
				if (Key.Contains(TEXT("새")))
				{
					// `새 4종 0` 은 종 하나가 아니라 새 전체의 값이다.
					BirdDamage = Number;
				}
				else
				{
					DesignDamage.Add(Key, Number);
				}
			}
		}
		else if (Line.StartsWith(TEXT("| 동물 공격 간격 |")))
		{
			DesignInterval = FCString::Atod(*Value);
		}
		else if (Line.StartsWith(TEXT("| 동물 공격 사거리 |")))
		{
			DesignRange = FCString::Atod(*Value);
		}
		else
		{
			DesignAggro = FCString::Atod(*Value);
		}
	}

	// ── DESIGN 8.7 "종 | 드롭 | 행동 | 서식 존" 표의 행동 열 ────────────────────
	TMap<FString, EAnimalBehaviorKind> DesignBehavior;
	const int32 SectionStart = Doc.Find(TEXT("### 8.7 "), ESearchCase::CaseSensitive);
	if (SectionStart != INDEX_NONE)
	{
		const int32 NextHeading = Doc.Find(TEXT("\n### "), ESearchCase::CaseSensitive,
			ESearchDir::FromStart, SectionStart + 8);
		const FString Section = NextHeading == INDEX_NONE
			? Doc.Mid(SectionStart)
			: Doc.Mid(SectionStart, NextHeading - SectionStart);

		TArray<FString> SectionLines;
		Section.ParseIntoArrayLines(SectionLines);
		// 네 칸짜리 줄만 본다 — 같은 절의 크기 표(세 칸)와 섞이지 않게.
		const FRegexPattern RowPattern(TEXT("^\\|\\s*([^|]+?)\\s*\\|[^|]*\\|\\s*([^|]+?)\\s*\\|\\s*([^|]+?)\\s*\\|\\s*$"));
		for (const FString& Line : SectionLines)
		{
			FRegexMatcher Matcher(RowPattern, Line);
			if (!Matcher.FindNext())
			{
				continue;
			}
			const FString Species = Matcher.GetCaptureGroup(1);
			FString Behavior = Matcher.GetCaptureGroup(2).Replace(TEXT("*"), TEXT(""));
			if (Species == TEXT("종") || Behavior.Contains(TEXT("---")))
			{
				continue;
			}

			// 갈래는 네 가지뿐이다 (8.7 머리말 · `EAnimalBehaviorKind`).
			if (Behavior.Contains(TEXT("궁지")))
			{
				DesignBehavior.Add(Species, EAnimalBehaviorKind::CorneredCounter);
			}
			else if (Behavior.Contains(TEXT("맞으면 반격")))
			{
				DesignBehavior.Add(Species, EAnimalBehaviorKind::RetaliateWhenHit);
			}
			else if (Behavior.Contains(TEXT("선공")))
			{
				DesignBehavior.Add(Species, EAnimalBehaviorKind::Aggressive);
			}
			else if (Behavior.Contains(TEXT("도주")))
			{
				DesignBehavior.Add(Species, EAnimalBehaviorKind::Flee);
			}
		}
	}

	// 설계서를 한 줄도 못 읽었으면 아래 for 가 조용히 지나간다 — 그게 제일 위험하다 (빈 통과).
	if (!TestTrue(TEXT("DESIGN 3.1 동물 공격 표에서 종별 데미지를 읽었다"), DesignDamage.Num() > 0) ||
		!TestTrue(TEXT("DESIGN 3.1 에서 새 4종 데미지를 읽었다"), BirdDamage >= 0.0) ||
		!TestTrue(TEXT("DESIGN 3.1 에서 간격·사거리·선공 반경을 읽었다"),
			DesignInterval > 0.0 && DesignRange > 0.0 && DesignAggro > 0.0) ||
		!TestTrue(TEXT("DESIGN 8.7 행동 열을 읽었다"), DesignBehavior.Num() > 0))
	{
		return false;
	}

	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_AnimalTable 로드"), Table))
	{
		return false;
	}

	int32 Checked = 0;
	for (const FName& RowName : Table->GetRowNames())
	{
		const FAnimalRow* Row = Table->FindRow<FAnimalRow>(RowName, TEXT("AnimalAttackTableTest"));
		// 자리표시 돼지·소·양은 교체 대상이라 설계서 두 표에 줄이 없다 (M5-07 끝에 행째 빠진다).
		if (Row == nullptr || Row->bPlaceholderSpecies || Row->bStorySpecies)
		{
			continue;
		}

		const FString DisplayName = Row->DisplayName.ToString();
		if (!DesignBehavior.Contains(DisplayName))
		{
			AddError(FString::Printf(TEXT("[%s] DESIGN 8.7 행동 열에 '%s' 줄이 없다"),
				*RowName.ToString(), *DisplayName));
			continue;
		}

		const EAnimalBehaviorKind WantBehavior = DesignBehavior[DisplayName];
		// 종별 데미지 줄에 이름이 없는 종 = 새 4종 (표가 넷을 묶어 적는다). 묶음 값이 기대값이다.
		const double WantDamage = DesignDamage.Contains(DisplayName) ? DesignDamage[DisplayName] : BirdDamage;
		// 선공 반경은 **선공 종만** 그 값이고 나머지는 0 이다 (3.1 표 머리말 · 8.7 행동 열).
		const double WantAggro = WantBehavior == EAnimalBehaviorKind::Aggressive ? DesignAggro : 0.0;

		AddInfo(FString::Printf(TEXT("[%s] %s — 행동 %d · 1타 %.0f · 간격 %.1f초 · 사거리 %.0fcm · 선공 반경 %.0fcm"),
			*RowName.ToString(), *DisplayName, static_cast<int32>(Row->BehaviorKind),
			Row->AttackDamage, Row->AttackIntervalSec, Row->AttackRangeCm, Row->AggroRadiusCm));

		TestEqual(*FString::Printf(TEXT("[%s] 행동이 DESIGN 8.7 과 같다"), *RowName.ToString()),
			Row->BehaviorKind, WantBehavior);
		TestEqual(*FString::Printf(TEXT("[%s] 1타 데미지가 DESIGN 3.1 과 같다"), *RowName.ToString()),
			static_cast<double>(Row->AttackDamage), WantDamage);
		TestEqual(*FString::Printf(TEXT("[%s] 공격 간격이 DESIGN 3.1 과 같다"), *RowName.ToString()),
			static_cast<double>(Row->AttackIntervalSec), DesignInterval);
		TestEqual(*FString::Printf(TEXT("[%s] 공격 사거리가 DESIGN 3.1 과 같다"), *RowName.ToString()),
			static_cast<double>(Row->AttackRangeCm), DesignRange);
		TestEqual(*FString::Printf(TEXT("[%s] 선공 반경이 DESIGN 3.1 과 같다"), *RowName.ToString()),
			static_cast<double>(Row->AggroRadiusCm), WantAggro);

		// 클립 칸도 같이 본다 — **포유류 8종은 공격 클립을 가진다**(8.7). 새 4종은 비어 있어야 정상이다.
		const bool bMammal = WantBehavior != EAnimalBehaviorKind::Flee;
		TestEqual(*FString::Printf(TEXT("[%s] 도주 클립 칸이 차 있다"), *RowName.ToString()),
			!Row->FleeAnimRef.IsNull(), true);
		TestEqual(*FString::Printf(TEXT("[%s] 공격 클립 칸 = 포유류만"), *RowName.ToString()),
			!Row->AttackAnimRef.IsNull(), bMammal);

		++Checked;
	}

	// 12종을 다 돌았는지 — 표를 못 읽어 0종을 돌고 통과하는 일을 막는다.
	TestEqual(TEXT("설계서와 맞댄 종 수 = DESIGN 8.7 행동 열의 종 수"), Checked, DesignBehavior.Num());
	return true;
}


// 감독 결함 2026-09-24 00:05 ② 가 건 합격 조건 그대로 — **도주 한계에 닿은 뒤 10초 동안 클립 전환 ≤1.**
// 감독 PIE 실측: 늑대가 한계에 닿은 뒤 32초 동안 대기 ↔ 달리기를 10번 갈아탔고 그때 속도가
// 1.9 ↔ 15~21cm/s 를 오갔다. 깜빡임의 몸통은 클립 쪽이 아니라 **경계에서 속도가 0 근처로 죽는 것**이라,
// 전환 수와 그동안의 최저 속도를 같이 본다 (한쪽만 보면 "클립을 고정" 하는 눈속임이 통과한다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalLeashClipTest, "Snarehaven.Animal.Clips.LeashKeepsFleeClip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalLeashClipTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	const FAnimalRow Row = MakeClipRow(FoxIdlePath, FoxWalkPath, FoxRunPath);
	AAnimalCharacter* Animal = SpawnAnimalWithRow(*World, Row);
	ASnarehavenCharacter* const Threat = World->SpawnActor<ASnarehavenCharacter>();
	if (Animal == nullptr || Threat == nullptr)
	{
		AddError(TEXT("동물·사람 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	// 사람은 한 자리에 선 채다 — 깜빡임이 "사람이 움직여서" 가 아니라 경계 자체에서 난다는 것을 보려면
	// 위협이 가만히 있어야 한다. 감독 PIE 에서도 동물은 한계에 닿은 채 혼자 멈췄다 섰다 했다.
	Threat->SetActorLocation(Animal->GetActorLocation() + FVector(TestThreatDistance, 0.f, 0.f));

	// 바닥이 없는 검사 월드라 날아서 잰다(다른 이동 검사와 같은 길). 다만 비행 최고 속도는 `MaxWalkSpeed`
	// 와 딴 칸이라 그냥 두면 엔진 기본값으로 달린다 — 게임에서 실제로 도는 속도로 재야 문턱 비교가 뜻을 가진다.
	Animal->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	Animal->GetCharacterMovement()->MaxFlySpeed = TestMoveSpeed;
	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);
	TestEqual(TEXT("한 대 맞으면 Fight"), Animal->GetAnimalState()->GetState(), EAnimalState::Fight);

	// 게임이 실제로 밟는 걸음(1/60초)으로 재야 한다 — 0.1초 걸음이면 경계를 도는 기하가 왜곡된다
	// (`SnarehavenAnimalBodyTest` 의 한계 검사와 같은 이유).
	constexpr float FrameSeconds = 1.f / 60.f;
	constexpr int32 MaxFramesToReachLeash = 3600;
	int32 FramesToLeash = 0;
	while (!Animal->IsBeyondFleeLeash() && FramesToLeash < MaxFramesToReachLeash)
	{
		TickAnimalOnce(*Animal, FrameSeconds);
		++FramesToLeash;
	}
	if (!TestTrue(TEXT("한계까지 실제로 도망갔다"), Animal->IsBeyondFleeLeash()))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 여기서부터가 지시가 건 10초다.
	constexpr int32 TenSecondsOfFrames = 600;
	UAnimSequence* const FleeAnim = Row.FleeAnimRef.LoadSynchronous();
	UAnimSequence* Previous = Animal->GetCurrentBodyAnim();
	int32 ClipChanges = 0;
	float SlowestSpeed = TNumericLimits<float>::Max();
	for (int32 Step = 0; Step < TenSecondsOfFrames; ++Step)
	{
		TickAnimalOnce(*Animal, FrameSeconds);
		UAnimSequence* const Now = Animal->GetCurrentBodyAnim();
		if (Now != Previous)
		{
			++ClipChanges;
			Previous = Now;
		}
		SlowestSpeed = FMath::Min(SlowestSpeed, Animal->GetVelocity().Size2D());
	}

	AddInfo(FString::Printf(TEXT("한계 도달 %.1f초 · 그 뒤 10초 동안 클립 전환 %d번 · 최저 속도 %.1fcm/s"),
		FramesToLeash * FrameSeconds, ClipChanges, SlowestSpeed));

	TestTrue(FString::Printf(TEXT("한계에 닿은 뒤 10초 동안 클립 전환 1번 이하 (실제 %d번)"), ClipChanges),
		ClipChanges <= 1);

	// 전환 수만 보면 "클립을 대기로 고정" 해도 통과한다 — 그래서 **도주 속도가 살아 있는지**를 같이 본다.
	// 문턱 숫자는 여기 안 적는다: 이동 클립에서 빠져나가는 값(`GetMoveAnimExitSpeed`)을 코드에서 읽는다.
	TestTrue(FString::Printf(TEXT("경계를 도는 동안 속도가 이동 클립 문턱 아래로 안 죽는다 (최저 %.1f / 문턱 %.1f)"),
			SlowestSpeed, Animal->GetMoveAnimExitSpeed()),
		SlowestSpeed > Animal->GetMoveAnimExitSpeed());

	// **지시가 쓴 말 그대로 — 경계에서도 "도주 속도를 유지" 하는가.** 문턱 위이기만 하면 되는 것이 아니다:
	// 바깥 성분만 깎는 예전 길은 경계에 닿는 순간 속도를 한 번 크게 잃고 다시 붙는다(역검증에서 34.1cm/s).
	// 여유 5% 는 검사 전용 값이다 — 접선을 안쪽으로 기울여 넘친 거리를 되돌리는 몫만큼은 줄 수 있다.
	constexpr float SpeedKeepRatio = 0.95f;
	TestTrue(FString::Printf(TEXT("경계를 도는 동안 도주 속도를 유지한다 (최저 %.1f / 도주 %.1f)"),
			SlowestSpeed, TestMoveSpeed),
		SlowestSpeed >= TestMoveSpeed * SpeedKeepRatio);

	// 그리고 그때 돌던 클립은 대기가 아니라 도주다.
	TestEqual(TEXT("10초 뒤에도 도주 클립이 돈다"), Animal->GetCurrentBodyAnim(), FleeAnim);

	// **부딪혀 속도가 뒤집혀도 돌던 쪽을 이어 간다.** 맵에는 나무·바위가 있어 튕기는 프레임이 나오는데,
	// 그때 도는 쪽을 속도만 보고 다시 고르면 방향이 홱 뒤집혀 같은 깜빡임이 돌아온다.
	// (이 덩어리가 없으면 "도는 쪽을 기억한다" 를 일부러 꺼도 검사가 못 잡는다 — 루프#173 역검증에서 확인)
	const FVector BeforeBump = Animal->GetFleeDirection();
	Animal->GetCharacterMovement()->Velocity = -Animal->GetCharacterMovement()->Velocity;
	const FVector AfterBump = Animal->GetFleeDirection();
	TestTrue(TEXT("속도가 뒤집혀도 경계를 도는 쪽은 그대로"),
		FVector::DotProduct(BeforeBump, AfterBump) > 0.f);

	DestroyTestWorld(World);
	return true;
}

// ROADMAP M41-02·03 — **돼지·양 행이 걷기·도주 클립을 가진다.** 빈 칸이던 동안 둘은 대기 클립인 채
// 미끄러졌다(M41-01 조사). 이름이 아니라 실제로 불러온 클립 객체가 대기와 다르고 그 몸 스켈레톤에
// 붙었는지로 본다 — 다른 스켈레톤 클립이면 칸이 차 있어도 재생이 안 된다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalLivestockMoveClipTest, "Snarehaven.Animal.Clips.LivestockRowsHaveMoveClips",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalLivestockMoveClipTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("동물 표를 불러왔다"), Table))
	{
		return false;
	}

	const TPair<FName, const TCHAR*> Species[] = {
		{ TEXT("Animal_Pig"), TEXT("돼지") },   // M41-02
		{ TEXT("Animal_Sheep"), TEXT("양") },   // M41-03
	};
	for (const TPair<FName, const TCHAR*>& Kind : Species)
	{
		const FAnimalRow* Row = Table->FindRow<FAnimalRow>(Kind.Key, TEXT("AnimalLivestockMoveClipTest"));
		if (!TestNotNull(FString::Printf(TEXT("%s 행이 있다"), Kind.Value), Row))
		{
			continue;
		}

		USkeletalMesh* const Mesh = Row->MeshRef.LoadSynchronous();
		UAnimSequence* const Idle = Row->IdleAnimRef.LoadSynchronous();
		if (!TestNotNull(FString::Printf(TEXT("%s 메시"), Kind.Value), Mesh)
			|| !TestNotNull(FString::Printf(TEXT("%s 대기 클립"), Kind.Value), Idle))
		{
			continue;
		}
		const USkeleton* const BodySkeleton = Mesh->GetSkeleton();

		const TPair<const TCHAR*, const TSoftObjectPtr<UAnimSequence>*> MoveSlots[] = {
			{ TEXT("걷기"), &Row->WalkAnimRef },
			{ TEXT("도주"), &Row->FleeAnimRef },
		};
		for (const TPair<const TCHAR*, const TSoftObjectPtr<UAnimSequence>*>& Slot : MoveSlots)
		{
			UAnimSequence* const Clip = Slot.Value->LoadSynchronous();
			if (!TestNotNull(FString::Printf(TEXT("%s %s 클립을 불러왔다"), Kind.Value, Slot.Key), Clip))
			{
				continue;
			}
			TestNotEqual(FString::Printf(TEXT("%s %s 클립 ≠ 대기 클립"), Kind.Value, Slot.Key), Clip, Idle);
			TestEqual(FString::Printf(TEXT("%s %s 클립 스켈레톤 = 몸 스켈레톤"), Kind.Value, Slot.Key),
				static_cast<const USkeleton*>(Clip->GetSkeleton()), BodySkeleton);
			TestTrue(FString::Printf(TEXT("%s %s 클립 길이 > 0"), Kind.Value, Slot.Key), Clip->GetPlayLength() > 0.f);
		}
	}
	return true;
}

// ROADMAP M41-04 — **걷기 칸이 빈 종은 걷는 동안 도주 클립을 느리게 튼다.** 새 4종(꿩·공작·뇌조·큰뇌조)은 팩에
// 걷기가 없어 대기로 떨어진 채 미끄러졌다(M41-01 조사). 진짜 꿩 행으로 세워 실제로 걸린 클립 객체와 재생 빠르기를 본다.
// 기대 빠르기는 이 파일에 적지 않는다 — 「1 보다 작다」와 「도망치면 1배로 돌아온다」만 맞댄다(배율이 바뀌어도 안 깨지게).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAnimalBorrowedWalkClipTest, "Snarehaven.Animal.Clips.EmptyWalkBorrowsSlowFlee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAnimalBorrowedWalkClipTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("동물 표를 불러왔다"), Table))
	{
		return false;
	}
	const FAnimalRow* Row = Table->FindRow<FAnimalRow>(TEXT("Animal_Pheasant"), TEXT("AnimalBorrowedWalkClipTest"));
	if (!TestNotNull(TEXT("꿩 행이 있다"), Row))
	{
		return false;
	}
	TestTrue(TEXT("꿩 행 걷기 칸은 비어 있다(이 시험의 전제)"), Row->WalkAnimRef.IsNull());
	UAnimSequence* const IdleAnim = Row->IdleAnimRef.LoadSynchronous();
	UAnimSequence* const FleeAnim = Row->FleeAnimRef.LoadSynchronous();
	if (!TestNotNull(TEXT("꿩 대기 클립"), IdleAnim) || !TestNotNull(TEXT("꿩 도주 클립"), FleeAnim))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Animal = SpawnAnimalWithRow(*World, *Row);
	if (Animal == nullptr)
	{
		AddError(TEXT("동물 액터를 못 스폰했다"));
		DestroyTestWorld(World);
		return false;
	}

	// ① 걷기 상태(Idle 인 채 움직임) — 도주 클립을 느리게.
	Animal->GetCharacterMovement()->Velocity = FVector(Animal->GetMoveAnimEnterSpeed() * 2.f, 0.f, 0.f);
	Animal->UpdateLocomotionAnimation();
	const float WalkRate = Animal->GetCurrentBodyPlayRate();
	AddInfo(FString::Printf(TEXT("꿩 걷기: 클립 %s · 빠르기 %.2f"),
		IsValid(Animal->GetCurrentBodyAnim()) ? *Animal->GetCurrentBodyAnim()->GetName() : TEXT("없음"), WalkRate));
	TestEqual(TEXT("꿩 걷기 = 도주 클립"), Animal->GetCurrentBodyAnim(), FleeAnim);
	TestNotEqual(TEXT("꿩 걷기 ≠ 대기 클립"), Animal->GetCurrentBodyAnim(), IdleAnim);
	TestTrue(TEXT("꿩 걷기 빠르기 < 1"), WalkRate < 1.f);
	TestTrue(TEXT("꿩 걷기 빠르기 > 0"), WalkRate > 0.f);
	TestEqual(TEXT("고르는 함수도 같은 빠르기"), Animal->ChooseLocomotionPlayRate(), WalkRate);

	// ② 맞고 도망치면 같은 클립이 1배로 — 클립은 그대로라 되감지 않고 빠르기만 바뀐다.
	Animal->GetAnimalState()->ApplyDamage(TestNickDamage);
	if (Animal->GetAnimalState()->GetState() == EAnimalState::Fight)
	{
		Animal->UpdateLocomotionAnimation();
		AddInfo(FString::Printf(TEXT("꿩 도주: 빠르기 %.2f"), Animal->GetCurrentBodyPlayRate()));
		TestEqual(TEXT("꿩 도주 = 도주 클립"), Animal->GetCurrentBodyAnim(), FleeAnim);
		TestEqual(TEXT("꿩 도주 빠르기 = 1"), Animal->GetCurrentBodyPlayRate(), 1.f);
	}
	else
	{
		AddError(TEXT("한 대 맞았는데 Fight 가 아니다"));
	}

	DestroyTestWorld(World);
	return true;
}


// ROADMAP M41-05 — **목장 펫도 걷기 칸이 비면 도주 클립을 느리게 튼다.** `ARanchPetActor::Tick` 이 사냥터와 같은
// `AAnimalCharacter::PickWalkClip` 을 쓰는지, 진짜 공작 행으로 걷기 시작할 때까지 틱하고 걸린 클립·빠르기를 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchPetBorrowedWalkClipTest, "Snarehaven.Animal.Clips.PetEmptyWalkBorrowsSlowFlee",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchPetBorrowedWalkClipTest::RunTest(const FString& Parameters)
{
	UDataTable* Table = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("동물 표를 불러왔다"), Table))
	{
		return false;
	}
	const FAnimalRow* Row = Table->FindRow<FAnimalRow>(TEXT("Animal_Peacock"), TEXT("RanchPetBorrowedWalkClipTest"));
	if (!TestNotNull(TEXT("공작 행이 있다"), Row))
	{
		return false;
	}
	TestTrue(TEXT("공작 행 걷기 칸은 비어 있다(이 시험의 전제)"), Row->WalkAnimRef.IsNull());
	UAnimSequence* const IdleAnim = Row->IdleAnimRef.LoadSynchronous();
	UAnimSequence* const FleeAnim = Row->FleeAnimRef.LoadSynchronous();
	if (!TestNotNull(TEXT("공작 대기 클립"), IdleAnim) || !TestNotNull(TEXT("공작 도주 클립"), FleeAnim))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ARanchPetActor* const Pet = World->SpawnActor<ARanchPetActor>();
	if (Pet == nullptr || !Pet->ApplyRow(*Row))
	{
		AddError(TEXT("공작 펫을 못 세웠다"));
		DestroyTestWorld(World);
		return false;
	}
	Pet->SetTetherHome(Pet->GetActorLocation());
	TestEqual(TEXT("서 있을 땐 대기 클립"), Pet->GetCurrentClip(), IdleAnim);
	for (int32 Frame = 0; Frame < TestPetMaxFrames && !Pet->IsWalking(); ++Frame)
	{
		Pet->Tick(TestPetFrameSeconds);
	}
	if (TestTrue(TEXT("20초 안에 걷기 시작"), Pet->IsWalking()))
	{
		const float WalkRate = Pet->GetCurrentPlayRate();
		AddInfo(FString::Printf(TEXT("공작 펫 걷기: 클립 %s · 빠르기 %.2f"),
			IsValid(Pet->GetCurrentClip()) ? *Pet->GetCurrentClip()->GetName() : TEXT("없음"), WalkRate));
		TestEqual(TEXT("공작 펫 걷기 = 도주 클립"), Pet->GetCurrentClip(), FleeAnim);
		TestNotEqual(TEXT("공작 펫 걷기 ≠ 대기 클립"), Pet->GetCurrentClip(), IdleAnim);
		TestTrue(TEXT("공작 펫 걷기 빠르기 < 1"), WalkRate < 1.f);
		TestTrue(TEXT("공작 펫 걷기 빠르기 > 0"), WalkRate > 0.f);
	}

	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
