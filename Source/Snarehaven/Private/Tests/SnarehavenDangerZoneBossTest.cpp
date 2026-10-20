// ROADMAP M21-07f 2층 검사 — 주간 우두머리 스폰(호스트) (DESIGN 10.6 「우두머리 자리·종」·「우두머리 HP」·「우두머리 재생성」).
// 보는 것 넷:
//  ① 우두머리 행이면 정원 밖으로 **한 마리 더** 선다 — 체력 = 종 `MaxHP` × 행 배수, 몸 = 행 배율, 이름 카드 `우두머리`.
//     쓰러지면 다음 채움에 다시 선다(보상은 서버가 주 1회로 막는다). 배수 0 인 행은 우두머리가 없다.
//  ② **포획 불가** — 임계 아래로 깎아도 Capturable 이 안 되고 `TryCapture` 가 거절된다.
//  ③ 처치 때 막타 사람 + 같이 잡기 창 안 명중자 **수만큼** `Client_ClaimBossReward` 가 간다 — 안 때린 사람은 0.
//     서버 `ClaimBoss` 답 → 알림 문장(모자·코인·꽝·이미 받음).
//  ④ `DT_DangerZoneTable` 의 우두머리 칸 = DESIGN 10.6 글자 (상시 구역 ×5·1.5, 이벤트 구역 0).
// 검사 값은 설계값과 **일부러 다르다**(Verification.md 6) — 5·1.5 가 코드에 박혀 있으면 ①이 운다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.DangerZone.Boss; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Data/AnimalRow.h"
#include "Data/DangerZoneRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SnarehavenCharacter.h"
#include "World/DangerZoneActor.h"

namespace
{
	const TCHAR* DangerTablePath = TEXT("/Game/Game/Data/World/DT_DangerZoneTable.DT_DangerZoneTable");
	const FName ZoneRowName = TEXT("Danger_HuntField");
	const FName EventRowName = TEXT("Event_CrashSite");

	// 검사 전용 값 (설계 숫자 아님 — DESIGN 의 ×5·1.5 와 다르다).
	constexpr int32 TestAnimalCount = 2;
	constexpr float TestRadiusCm = 400.f;
	constexpr int32 TestRespawnSeconds = 60;
	constexpr float TestBossHPMultiplier = 3.f;
	constexpr float TestBossBodyScale = 2.f;
	constexpr float TestShareWindowSeconds = 4.f;
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestAttackRangeCm = 300.f;
	constexpr float TestAttackIntervalSeconds = 1.f;
	constexpr float TestMaxHP = 10.f;          // ×3 = 30 — 세 대면 죽는다
	constexpr float TestCaptureThresholdPct = 50.f;
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

	const FDangerZoneRow* FindZoneRow(FName RowName)
	{
		const UDataTable* Table = LoadObject<UDataTable>(nullptr, DangerTablePath);
		return IsValid(Table) ? Table->FindRow<FDangerZoneRow>(RowName, TEXT("DangerZoneBossTest"), false) : nullptr;
	}

	ASnarehavenCharacter* SpawnTestPlayer(UWorld* World, const FVector& At)
	{
		ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>(At, FRotator::ZeroRotator);
		if (Player != nullptr)
		{
			Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestAttackRangeCm, TestAttackIntervalSeconds);
			Player->GetMeleeAttack()->SetShareDropWindowSeconds(TestShareWindowSeconds);
		}
		return Player;
	}

	AAnimalCharacter* SpawnTestBoss(UWorld* World)
	{
		AAnimalCharacter* Animal = World->SpawnActor<AAnimalCharacter>(TestAnimalAt, FRotator::ZeroRotator);
		if (Animal == nullptr)
		{
			return nullptr;
		}
		FAnimalRow Row;
		Row.AnimalID = TEXT("animal.test_dummy");
		Row.DisplayName = FText::FromString(TEXT("시험견"));
		Row.MaxHP = TestMaxHP;
		Row.CaptureThresholdPct = TestCaptureThresholdPct;
		Animal->GetAnimalState()->InitializeFromRow(Row);
		Animal->GetAnimalState()->MakeBoss(TestBossHPMultiplier);
		return Animal;
	}

	/** DESIGN 10.6 표에서 왼쪽 칸이 `Label` 인 줄의 값 칸. */
	FString FindDesignCell(const FString& Doc, const FString& Label)
	{
		TArray<FString> Lines;
		Doc.ParseIntoArrayLines(Lines);
		const FString Prefix = FString::Printf(TEXT("| %s |"), *Label);
		for (const FString& Line : Lines)
		{
			if (Line.StartsWith(Prefix))
			{
				FString Rest = Line.Mid(Prefix.Len());
				FString Value;
				Rest.Split(TEXT("|"), &Value, nullptr);
				return Value;
			}
		}
		return FString();
	}

	/** 문장에서 정규식 첫 묶음을 숫자로. 못 찾으면 -1. */
	double MatchNumber(const FString& Text, const TCHAR* Pattern)
	{
		FRegexMatcher Matcher(FRegexPattern(Pattern), Text);
		return Matcher.FindNext() ? FCString::Atod(*Matcher.GetCaptureGroup(1)) : -1.0;
	}
}

// ① 구역이 우두머리를 세운다 — 체력·몸·이름·재생성, 배수 0 이면 없음.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBossSpawnTest, "Snarehaven.DangerZone.Boss.SpawnsOneWithRowMultipliers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBossSpawnTest::RunTest(const FString& Parameters)
{
	const FDangerZoneRow* const TableRow = FindZoneRow(ZoneRowName);
	const UDataTable* const AnimalTable = GetDefault<AAnimalCharacter>()->GetAnimalTable();
	const FAnimalRow* const Species = TableRow != nullptr && AnimalTable != nullptr
		? AnimalTable->FindRow<FAnimalRow>(TableRow->AnimalRowName, TEXT("DangerZoneBossTest"), false) : nullptr;
	if (!TestNotNull(TEXT("상시 위험구역 행"), TableRow) || !TestNotNull(TEXT("그 종 행"), Species))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	FDangerZoneRow Row;
	Row.ZoneID = TableRow->ZoneID;
	Row.AnimalRowName = TableRow->AnimalRowName;
	Row.RadiusCm = TestRadiusCm;
	Row.AnimalCount = TestAnimalCount;
	Row.RespawnSeconds = TestRespawnSeconds;
	Row.BossHPMultiplier = TestBossHPMultiplier;
	Row.BossBodyScale = TestBossBodyScale;
	ADangerZoneActor* Zone = World->SpawnActor<ADangerZoneActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("구역"), Zone) || !TestTrue(TEXT("구역이 켜진다"), Zone->SetDangerZoneRow(Row)))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("우두머리 행이다"), Zone->HasBoss());
	TestEqual(TEXT("정원 + 우두머리 한 마리"), Zone->RefillZone(), TestAnimalCount + 1);
	AAnimalCharacter* const Boss = Zone->GetStandingBoss();
	if (!TestNotNull(TEXT("우두머리가 서 있다"), Boss))
	{
		DestroyTestWorld(World);
		return false;
	}
	UAnimalStateComponent* const State = Boss->GetAnimalState();
	TestTrue(TEXT("우두머리 표시"), State->IsBoss());
	TestEqual(TEXT("체력 = 종 MaxHP × 행 배수"), State->GetMaxHP(), Species->MaxHP * TestBossHPMultiplier);
	TestEqual(TEXT("가득 찬 채로 선다"), State->GetCurrentHP(), State->GetMaxHP());
	TestEqual(TEXT("몸 = 행 배율"), static_cast<float>(Boss->GetActorScale3D().X), TestBossBodyScale);
	TestTrue(TEXT("이름 카드 `우두머리`"), State->GetCardName().ToString().StartsWith(TEXT("우두머리 ")));
	TestEqual(TEXT("정원 동물 수는 그대로(우두머리는 정원 밖)"), Zone->GetLiveAnimalCount(), TestAnimalCount);
	TestEqual(TEXT("서 있으면 더 안 세운다"), Zone->RefillZone(), 0);
	AddInfo(FString::Printf(TEXT("우두머리 %s 체력 %.0f (종 %.0f ×%.1f) · 몸 ×%.2f · 카드 %s"),
		*Row.AnimalRowName.ToString(), State->GetMaxHP(), Species->MaxHP, TestBossHPMultiplier,
		Boss->GetActorScale3D().X, *State->GetCardName().ToString()));

	TestTrue(TEXT("쓰러뜨린다"), State->ApplyDamage(State->GetMaxHP()) && State->IsFinished());
	TestNull(TEXT("쓰러지면 서 있는 우두머리 없음"), Zone->GetStandingBoss());
	TestEqual(TEXT("다음 채움에 다시 선다"), Zone->RefillZone(), 1);
	TestNotNull(TEXT("새 우두머리"), Zone->GetStandingBoss());

	// 배수 0 = 우두머리 없는 구역 (이벤트 구역).
	FDangerZoneRow Plain = Row;
	Plain.BossHPMultiplier = 0.f;
	ADangerZoneActor* PlainZone = World->SpawnActor<ADangerZoneActor>(FVector(5000.f, 0.f, 0.f), FRotator::ZeroRotator);
	if (TestNotNull(TEXT("두 번째 구역"), PlainZone) && PlainZone->SetDangerZoneRow(Plain))
	{
		TestFalse(TEXT("배수 0 은 우두머리 없음"), PlainZone->HasBoss());
		TestEqual(TEXT("정원만 선다"), PlainZone->RefillZone(), TestAnimalCount);
		TestNull(TEXT("우두머리 없음"), PlainZone->GetStandingBoss());
	}

	DestroyTestWorld(World);
	return true;
}

// ② 포획 불가 — 임계 아래에서도 Fight, 포획 거절.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBossNoCaptureTest, "Snarehaven.DangerZone.Boss.CannotBeCaptured",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBossNoCaptureTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	AAnimalCharacter* Boss = SpawnTestBoss(World);
	if (!TestNotNull(TEXT("우두머리"), Boss))
	{
		DestroyTestWorld(World);
		return false;
	}
	UAnimalStateComponent* const State = Boss->GetAnimalState();
	TestEqual(TEXT("체력 = 행 × 배수"), State->GetMaxHP(), TestMaxHP * TestBossHPMultiplier);

	// 임계(50%) 한참 아래, 1 만 남긴다.
	TestTrue(TEXT("깎인다"), State->ApplyDamage(State->GetMaxHP() - 1.f));
	TestEqual(TEXT("임계 아래여도 싸움 중(포획 가능 아님)"), State->GetState(), EAnimalState::Fight);
	TestFalse(TEXT("포획 거절"), State->TryCapture());

	// 일반 동물은 같은 깎임에서 포획 가능 — 시험이 상태를 잘못 읽는 것이 아님을 맞댄다.
	AAnimalCharacter* Plain = World->SpawnActor<AAnimalCharacter>(TestAnimalAt + FVector(0.f, 500.f, 0.f), FRotator::ZeroRotator);
	FAnimalRow Row;
	Row.AnimalID = TEXT("animal.test_dummy");
	Row.MaxHP = TestMaxHP;
	Row.CaptureThresholdPct = TestCaptureThresholdPct;
	if (TestNotNull(TEXT("일반 동물"), Plain) && Plain->GetAnimalState()->InitializeFromRow(Row))
	{
		Plain->GetAnimalState()->ApplyDamage(TestMaxHP - 1.f);
		TestEqual(TEXT("일반은 포획 가능"), Plain->GetAnimalState()->GetState(), EAnimalState::Capturable);
		TestFalse(TEXT("일반은 우두머리 아님"), Plain->GetAnimalState()->IsBoss());
	}

	DestroyTestWorld(World);
	return true;
}

// ③ 처치 → 명중자 수만큼 보상 묻기, 답 → 알림 문장.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBossClaimTest, "Snarehaven.DangerZone.Boss.KillAsksClaimPerHitter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBossClaimTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* First = SpawnTestPlayer(World, TestFirstAt);
	ASnarehavenCharacter* Second = SpawnTestPlayer(World, TestSecondAt);
	ASnarehavenCharacter* Bystander = SpawnTestPlayer(World, TestBystanderAt);
	AAnimalCharacter* Boss = SpawnTestBoss(World);
	if (!TestNotNull(TEXT("첫 사람"), First) || !TestNotNull(TEXT("둘째 사람"), Second)
		|| !TestNotNull(TEXT("구경꾼"), Bystander) || !TestNotNull(TEXT("우두머리"), Boss))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("첫 사람 한 대"), First->GetMeleeAttack()->AttackOnce());
	TestEqual(TEXT("살아 있는 동안엔 안 묻는다"), First->GetMeleeAttack()->GetBossClaimRequestCount(), 0);
	TestTrue(TEXT("둘째 사람 한 대"), Second->GetMeleeAttack()->AttackOnce());
	TestTrue(TEXT("둘째 사람 막타"), Second->GetMeleeAttack()->AttackOnce());
	TestEqual(TEXT("죽었다"), Boss->GetAnimalState()->GetState(), EAnimalState::Dead);

	const int32 FirstClaims = First->GetMeleeAttack()->GetBossClaimRequestCount();
	const int32 SecondClaims = Second->GetMeleeAttack()->GetBossClaimRequestCount();
	const int32 BystanderClaims = Bystander->GetMeleeAttack()->GetBossClaimRequestCount();
	TestEqual(TEXT("막타 사람 1번"), SecondClaims, 1);
	TestEqual(TEXT("창 안 명중자 1번"), FirstClaims, 1);
	TestEqual(TEXT("안 때린 사람 0번"), BystanderClaims, 0);
	AddInfo(FString::Printf(TEXT("우두머리 보상 묻기: 먼저 %d · 막타 %d · 구경 %d"), FirstClaims, SecondClaims, BystanderClaims));

	// 답 → 문장. 성공 답 셋 · 이미 받음 · 그 밖 거절.
	TSharedPtr<FJsonObject> Hat = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> HatReward = MakeShared<FJsonObject>();
	HatReward->SetStringField(TEXT("itemId"), TEXT("item.chief_hat"));
	Hat->SetBoolField(TEXT("won"), true);
	Hat->SetObjectField(TEXT("reward"), HatReward);
	TestTrue(TEXT("모자 당첨 문장"), UMeleeAttackComponent::DescribeBossClaimReply(true, Hat, FString()).ToString().Contains(TEXT("우두머리 모자")));

	TSharedPtr<FJsonObject> Coins = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> CoinReward = MakeShared<FJsonObject>();
	CoinReward->SetNumberField(TEXT("coins"), 7);
	Coins->SetBoolField(TEXT("won"), true);
	Coins->SetObjectField(TEXT("reward"), CoinReward);
	TestTrue(TEXT("코인 대체 문장에 서버 수"), UMeleeAttackComponent::DescribeBossClaimReply(true, Coins, FString()).ToString().Contains(TEXT("코인 7")));

	TSharedPtr<FJsonObject> Miss = MakeShared<FJsonObject>();
	Miss->SetBoolField(TEXT("won"), false);
	TestTrue(TEXT("꽝 문장"), UMeleeAttackComponent::DescribeBossClaimReply(true, Miss, FString()).ToString().Contains(TEXT("꽝")));

	TSharedPtr<FJsonObject> Already = MakeShared<FJsonObject>();
	Already->SetStringField(TEXT("error"), TEXT("이번 주 우두머리 보상은 이미 받았다"));
	Already->SetStringField(TEXT("code"), TEXT("already_claimed"));
	TestTrue(TEXT("이미 받음 문장"), UMeleeAttackComponent::DescribeBossClaimReply(true, Already, FString()).ToString().Contains(TEXT("이미 받았다")));

	TestTrue(TEXT("네트워크 실패는 실패 문장"), UMeleeAttackComponent::DescribeBossClaimReply(false, nullptr, TEXT("timeout")).ToString().Contains(TEXT("실패")));

	DestroyTestWorld(World);
	return true;
}

// ④ 표의 우두머리 칸 = DESIGN 10.6 글자.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBossTableTest, "Snarehaven.DangerZone.Boss.TableMatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBossTableTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!FFileHelper::LoadFileToString(Design, *FPaths::Combine(FPaths::ProjectDir(), TEXT("docs/DESIGN.md"))))
	{
		AddError(TEXT("docs/DESIGN.md 를 못 읽었다"));
		return false;
	}

	const double DesignHP = MatchNumber(FindDesignCell(Design, TEXT("우두머리 HP")), TEXT("×\\s*\\*\\*([0-9]+(?:\\.[0-9]+)?)\\*\\*"));
	const double DesignScale = MatchNumber(FindDesignCell(Design, TEXT("우두머리 자리·종")), TEXT("몸\\s*([0-9]+(?:\\.[0-9]+)?)배"));
	if (!TestTrue(TEXT("DESIGN 10.6 「우두머리 HP」 배수를 읽었다"), DesignHP > 0.0)
		|| !TestTrue(TEXT("DESIGN 10.6 「우두머리 자리·종」 몸 배율을 읽었다"), DesignScale > 0.0))
	{
		return false;
	}

	const FDangerZoneRow* const Zone = FindZoneRow(ZoneRowName);
	const FDangerZoneRow* const Event = FindZoneRow(EventRowName);
	if (!TestNotNull(TEXT("상시 위험구역 행"), Zone) || !TestNotNull(TEXT("이벤트 구역 행"), Event))
	{
		return false;
	}
	TestEqual(TEXT("상시 구역 체력 배수 = DESIGN"), static_cast<double>(Zone->BossHPMultiplier), DesignHP);
	TestEqual(TEXT("상시 구역 몸 배율 = DESIGN"), static_cast<double>(Zone->BossBodyScale), DesignScale);
	TestEqual(TEXT("이벤트 구역엔 우두머리 없음"), Event->BossHPMultiplier, 0.f);
	AddInfo(FString::Printf(TEXT("우두머리 표 = DESIGN: 체력 ×%.1f · 몸 ×%.2f"), DesignHP, DesignScale));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
