// ROADMAP M11-01 2층 검사 — 플레이어 체력·스태미나가 DESIGN 3.1 표대로 닳고 차는지.
// 보는 것 둘: ① `UPlayerVitalsComponent` 의 소모·회복·탈진·사망이 **표에서 받은 값**으로 도는가
// ② `DT_PlayerVitalsTable` 의 아홉 칸이 DESIGN 3.1 표와 같은가 (한쪽만 고치면 운다 —
// `Snarehaven.DangerZone.Table.MatchesDesign` 과 같은 짜임).
//
// ①은 **검사 전용 값**으로 돈다 (Verification.md 6) — 설계 숫자를 여기 적으면 사람이 밸런스를
// 바꿀 때마다 검사가 깨진다. 설계 숫자와 표를 맞대는 것은 ②의 몫이다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.PlayerVitals; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/PlayerVitalsRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/PlayerVitalsComponent.h"

namespace
{
	const TCHAR* VitalsTablePath = TEXT("/Game/Game/Data/Balance/DT_PlayerVitalsTable.DT_PlayerVitalsTable");
	const TCHAR* VitalsRowName = TEXT("Player");

	// DESIGN 3.1 "플레이어 체력·스태미나" 표의 왼쪽 칸 그대로. **여기 있는 것은 이름뿐이고 값은 문서에서 읽는다.**
	struct FVitalsField
	{
		const TCHAR* DesignLabel;
		const TCHAR* FieldName;
		float FPlayerVitalsRow::* Member;
	};

	const FVitalsField VitalsFields[] = {
		{ TEXT("플레이어 최대 체력"),        TEXT("MaxHealth"),                  &FPlayerVitalsRow::MaxHealth },
		{ TEXT("플레이어 최대 스태미나"),    TEXT("MaxStamina"),                 &FPlayerVitalsRow::MaxStamina },
		{ TEXT("달리기 스태미나 소모"),      TEXT("RunStaminaPerSecond"),        &FPlayerVitalsRow::RunStaminaPerSecond },
		{ TEXT("공격 1타 스태미나 소모"),    TEXT("AttackStaminaCost"),          &FPlayerVitalsRow::AttackStaminaCost },
		{ TEXT("스태미나 회복"),             TEXT("StaminaRegenPerSecond"),      &FPlayerVitalsRow::StaminaRegenPerSecond },
		{ TEXT("스태미나 회복 대기"),        TEXT("StaminaRegenDelaySeconds"),   &FPlayerVitalsRow::StaminaRegenDelaySeconds },
		{ TEXT("탈진 체력 감소"),            TEXT("ExhaustionHealthPerSecond"),  &FPlayerVitalsRow::ExhaustionHealthPerSecond },
		{ TEXT("체력 자연 회복"),            TEXT("HealthRegenPerSecond"),       &FPlayerVitalsRow::HealthRegenPerSecond },
		{ TEXT("체력 회복 스태미나 문턱"),   TEXT("HealthRegenStaminaThreshold"), &FPlayerVitalsRow::HealthRegenStaminaThreshold },
		// 아래 둘은 DESIGN 3.1 「사망 시 잃는 것」 표의 **같은 한 줄**("50 / 50")을 본다 (M11-03).
		{ TEXT("부활 시 체력·스태미나"),     TEXT("RespawnHealth"),              &FPlayerVitalsRow::RespawnHealth },
		{ TEXT("부활 시 체력·스태미나"),     TEXT("RespawnStamina"),             &FPlayerVitalsRow::RespawnStamina },
		// 이 줄의 값 칸은 숫자가 아니라 문장이다 — 아래 `LingerPattern` 이 "누운 채 N 초" 를 집는다 (M5-01e2).
		{ TEXT("쓰러짐 → 부활"),             TEXT("DeathLingerSeconds"),         &FPlayerVitalsRow::DeathLingerSeconds },
	};

	/** `### 3.1 …` 부터 다음 제목 전까지만 잘라 낸다 (DangerZone 검사와 같은 방식). */
	FString ExtractSection(const FString& Doc, const FString& HeadingPrefix)
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

	/** `| 달리기 스태미나 소모 | 10 / 초 | 근거 |` 같은 줄에서 왼쪽 칸 이름과 값 칸의 첫 숫자를 꺼낸다. */
	TMap<FString, double> ParseLabelledNumbers(const FString& Section)
	{
		TArray<FString> Lines;
		Section.ParseIntoArrayLines(Lines);

		const FRegexPattern RowPattern(TEXT("^\\|\\s*([^|]+?)\\s*\\|\\s*([0-9]+(?:\\.[0-9]+)?)"));
		// 값 칸이 문장인 줄도 있다 — 「쓰러짐 → 부활」은 클립 길이 + **누운 채 N 초** 꼴이라
		// 위 패턴으로는 안 읽힌다. 코드가 가진 숫자는 이 "N" 하나뿐이라 그것만 집는다 (M5-01e2).
		const FRegexPattern LingerPattern(
			TEXT("^\\|\\s*([^|]+?)\\s*\\|[^|]*?누운 채\\s*\\**\\s*([0-9]+(?:\\.[0-9]+)?)"));
		TMap<FString, double> Values;
		for (const FString& Line : Lines)
		{
			FRegexMatcher Matcher(RowPattern, Line);
			if (Matcher.FindNext())
			{
				Values.Add(Matcher.GetCaptureGroup(1), FCString::Atod(*Matcher.GetCaptureGroup(2)));
				continue;
			}

			FRegexMatcher LingerMatcher(LingerPattern, Line);
			if (LingerMatcher.FindNext())
			{
				Values.Add(LingerMatcher.GetCaptureGroup(1), FCString::Atod(*LingerMatcher.GetCaptureGroup(2)));
			}
		}
		return Values;
	}

	// 문제를 모아 돌려준다 — 아래 자기검사가 "이 검사가 정말 잡는가" 를 에셋·문서 없이 다시 볼 수 있게.
	TArray<FString> CollectVitalsIssues(const FPlayerVitalsRow& Row, const TMap<FString, double>& DesignValues)
	{
		TArray<FString> Issues;
		for (const FVitalsField& Field : VitalsFields)
		{
			// 설계서 칸을 못 읽었으면 "비교할 게 없어 통과" 가 된다 — 그게 제일 위험하다.
			const double* DesignValue = DesignValues.Find(Field.DesignLabel);
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

	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestMaxHealth = 60.f;
	constexpr float TestMaxStamina = 40.f;
	constexpr float TestRunDrain = 20.f;      // 2초 달리면 바닥
	constexpr float TestAttackCost = 15.f;
	constexpr float TestRegen = 8.f;
	constexpr float TestRegenDelay = 1.f;
	constexpr float TestExhaustionDrain = 5.f;
	constexpr float TestHealthRegen = 2.f;
	constexpr float TestHealthRegenThreshold = 20.f;
	constexpr float TestRespawnHealth = 24.f;   // 최대(60)의 절반이 **아니다** - 비율을 코드가 지어내지 않는지 본다
	constexpr float TestRespawnStamina = 12.f;
	constexpr float TestDeathLinger = 0.4f;     // 설계값 1.0 이 아니다 (Verification.md 6)
	constexpr float TestTolerance = 0.01f;

	FPlayerVitalsRow MakeTestRow()
	{
		FPlayerVitalsRow Row;
		Row.MaxHealth = TestMaxHealth;
		Row.MaxStamina = TestMaxStamina;
		Row.RunStaminaPerSecond = TestRunDrain;
		Row.AttackStaminaCost = TestAttackCost;
		Row.StaminaRegenPerSecond = TestRegen;
		Row.StaminaRegenDelaySeconds = TestRegenDelay;
		Row.ExhaustionHealthPerSecond = TestExhaustionDrain;
		Row.HealthRegenPerSecond = TestHealthRegen;
		Row.HealthRegenStaminaThreshold = TestHealthRegenThreshold;
		Row.RespawnHealth = TestRespawnHealth;
		Row.RespawnStamina = TestRespawnStamina;
		Row.DeathLingerSeconds = TestDeathLinger;
		return Row;
	}

	UPlayerVitalsComponent* MakeInitializedComponent(FAutomationTestBase& Test)
	{
		UPlayerVitalsComponent* Component = NewObject<UPlayerVitalsComponent>();
		Test.TestTrue(TEXT("검사 전용 행으로 초기화"), Component->InitializeFromRow(MakeTestRow()));
		return Component;
	}

	/**
	 * 함수 하나의 몸통(여는 중괄호부터 짝이 맞는 닫는 중괄호까지)을 잘라낸다.
	 * `Snarehaven.Analytics.Channel.SinglePath` 가 쓰는 것과 같은 방식 — 소스를 글자로 훑는 검사는
	 * 파일 전체가 아니라 **그 함수 안에서만** 봐야 옆 함수의 정상적인 호출에 걸려 거짓으로 울지 않는다.
	 */
	FString ExtractFunctionBody(const FString& Text, const FString& Signature)
	{
		const int32 SignatureAt = Text.Find(Signature, ESearchCase::CaseSensitive);
		if (SignatureAt == INDEX_NONE)
		{
			return FString();
		}

		const int32 OpenAt = Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SignatureAt);
		if (OpenAt == INDEX_NONE)
		{
			return FString();
		}

		int32 Depth = 0;
		for (int32 At = OpenAt; At < Text.Len(); ++At)
		{
			if (Text[At] == TEXT('{'))
			{
				++Depth;
			}
			else if (Text[At] == TEXT('}'))
			{
				--Depth;
				if (Depth == 0)
				{
					return Text.Mid(OpenAt, At - OpenAt + 1);
				}
			}
		}

		return FString();
	}

	/**
	 * 컴포넌트를 액터·월드 없이 직접 굴린다. `TickComponent` 가 아니라 `AdvanceTime` 을 부르는 이유는
	 * 엔진의 `UActorComponent::TickComponent` 가 `bRegistered` 를 단언하기 때문이다(등록 안 된 컴포넌트면 죽는다).
	 */
	void TickFor(UPlayerVitalsComponent& Component, float Seconds, float Step = 0.1f)
	{
		// 실수 누적으로 틱 수가 하나 늘었다 줄었다 하지 않게 횟수를 먼저 센다.
		const int32 Steps = FMath::RoundToInt(Seconds / Step);
		for (int32 Index = 0; Index < Steps; ++Index)
		{
			Component.AdvanceTime(Step);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerVitalsComponentTest,
	"Snarehaven.PlayerVitals.Component.DrainsAndRecovers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerVitalsComponentTest::RunTest(const FString& Parameters)
{
	// 미정(-1) 행으로는 켜지지 않는다.
	{
		UPlayerVitalsComponent* Component = NewObject<UPlayerVitalsComponent>();
		TestFalse(TEXT("미정 행으로는 초기화되지 않는다"), Component->InitializeFromRow(FPlayerVitalsRow()));
		TestFalse(TEXT("미정 상태에서는 달릴 수 없다"), Component->CanRun());
	}

	// 시작은 가득.
	{
		UPlayerVitalsComponent* Component = MakeInitializedComponent(*this);
		TestEqual(TEXT("시작 체력 = 최대"), Component->GetHealth(), TestMaxHealth, TestTolerance);
		TestEqual(TEXT("시작 스태미나 = 최대"), Component->GetStamina(), TestMaxStamina, TestTolerance);
	}

	// 달리면 표 값대로 닳는다: 1초 = TestRunDrain.
	{
		UPlayerVitalsComponent* Component = MakeInitializedComponent(*this);
		Component->SetMoveInput(true);
		Component->SetRunInput(true);
		TickFor(*Component, 1.f);
		TestEqual(TEXT("1초 달리기 = 소모 1초분"), Component->GetStamina(),
			TestMaxStamina - TestRunDrain, TestTolerance);
	}

	// 걷기는 0 소모 — 그리고 대기가 지나면 표 값대로 찬다.
	{
		UPlayerVitalsComponent* Component = MakeInitializedComponent(*this);
		Component->SetMoveInput(true);
		Component->SetRunInput(true);
		TickFor(*Component, 1.f);
		const float AfterRun = Component->GetStamina();

		Component->SetRunInput(false);
		TickFor(*Component, TestRegenDelay);
		TestEqual(TEXT("대기 중에는 안 찬다"), Component->GetStamina(), AfterRun, TestTolerance);

		TickFor(*Component, 1.f);
		TestEqual(TEXT("대기 뒤 1초 = 회복 1초분"), Component->GetStamina(),
			AfterRun + TestRegen, TestTolerance);
	}

	// 공격 1타는 정액을 먹고, 모자라면 타격이 안 나간다 (스태미나 0 처벌).
	{
		UPlayerVitalsComponent* Component = MakeInitializedComponent(*this);
		TestTrue(TEXT("가득이면 공격 1타가 나간다"), Component->TryConsumeAttackStamina());
		TestEqual(TEXT("1타 = 표의 소모값"), Component->GetStamina(),
			TestMaxStamina - TestAttackCost, TestTolerance);

		Component->TryConsumeAttackStamina();
		TestFalse(TEXT("남은 스태미나가 1타보다 적으면 못 친다"), Component->TryConsumeAttackStamina());
		TestEqual(TEXT("실패한 타격은 스태미나를 안 먹는다"), Component->GetStamina(),
			TestMaxStamina - 2.f * TestAttackCost, TestTolerance);
	}

	// 스태미나가 바닥나면 달리기 불가 — 조작을 빼앗지는 않는다.
	{
		UPlayerVitalsComponent* Component = MakeInitializedComponent(*this);
		Component->SetMoveInput(true);
		Component->SetRunInput(true);
		TickFor(*Component, TestMaxStamina / TestRunDrain);
		TestEqual(TEXT("스태미나 바닥"), Component->GetStamina(), 0.f, TestTolerance);
		TestFalse(TEXT("바닥이면 달릴 수 없다"), Component->CanRun());
	}

	// 탈진: 바닥인 채로 계속 이동하면 체력이 표 값대로 깎이고, 멈추면 풀린다.
	{
		UPlayerVitalsComponent* Component = MakeInitializedComponent(*this);
		Component->SetMoveInput(true);
		Component->SetRunInput(true);
		TickFor(*Component, TestMaxStamina / TestRunDrain);
		TestTrue(TEXT("바닥 + 이동 = 탈진"), Component->IsExhausted());

		// 스태미나가 0 이 된 그 틱부터 이미 탈진이라, 기준은 최대 체력이 아니라 지금 체력이다.
		const float HealthWhenExhausted = Component->GetHealth();
		TickFor(*Component, 1.f);
		TestEqual(TEXT("탈진 1초 = 체력 1초분 감소"), Component->GetHealth(),
			HealthWhenExhausted - TestExhaustionDrain, TestTolerance);
		TestEqual(TEXT("탈진 중에는 스태미나가 안 찬다"), Component->GetStamina(), 0.f, TestTolerance);

		Component->SetMoveInput(false);
		Component->SetRunInput(false);
		TestFalse(TEXT("멈추면 탈진이 풀린다"), Component->IsExhausted());
		// 탈진 중에도 "달리기·공격을 쉰 시간" 은 이미 흘렀다 — 풀리자마자 회복이 붙는다.
		TickFor(*Component, 1.f);
		TestEqual(TEXT("풀린 뒤 1초 = 회복 1초분"), Component->GetStamina(), TestRegen, TestTolerance);
	}

	// 체력 자연 회복은 문턱 위에서만 (탈진 중에는 없다 — 위에서 확인).
	{
		UPlayerVitalsComponent* Component = MakeInitializedComponent(*this);
		Component->SetMoveInput(true);
		Component->SetRunInput(true);
		TickFor(*Component, TestMaxStamina / TestRunDrain);  // 스태미나 0, 탈진
		TickFor(*Component, 2.f);                            // 체력을 좀 깎아 둔다
		const float Damaged = Component->GetHealth();

		Component->SetMoveInput(false);
		Component->SetRunInput(false);
		TickFor(*Component, 1.f);                            // 스태미나가 문턱 아래인 동안
		TestEqual(TEXT("문턱 아래에서는 체력이 안 찬다"), Component->GetHealth(), Damaged, TestTolerance);

		TickFor(*Component, TestMaxStamina / TestRegen);     // 스태미나를 문턱 위로
		TestTrue(TEXT("문턱을 넘으면 체력이 찬다"), Component->GetHealth() > Damaged + TestTolerance);
	}

	// 체력 0 = 사망. 알림은 한 번이고, 죽은 뒤에는 더 깎이지 않는다.
	{
		UPlayerVitalsComponent* Component = MakeInitializedComponent(*this);
		Component->SetMoveInput(true);
		Component->SetRunInput(true);
		TickFor(*Component, TestMaxStamina / TestRunDrain + TestMaxHealth / TestExhaustionDrain + 1.f);
		TestTrue(TEXT("탈진을 무시하고 계속 가면 죽는다"), Component->IsDead());
		TestEqual(TEXT("체력은 0 아래로 안 내려간다"), Component->GetHealth(), 0.f, TestTolerance);

		// 거점 전회복이 사망을 되돌린다 (부활 규칙은 M11-03 이 얹는다).
		Component->RestoreToFull();
		TestFalse(TEXT("전회복하면 살아난다"), Component->IsDead());
		TestEqual(TEXT("전회복 = 최대 체력"), Component->GetHealth(), TestMaxHealth, TestTolerance);
		TestEqual(TEXT("전회복 = 최대 스태미나"), Component->GetStamina(), TestMaxStamina, TestTolerance);
	}

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerVitalsTableTest,
	"Snarehaven.PlayerVitals.Table.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerVitalsTableTest::RunTest(const FString& Parameters)
{
	const FString DocPath = FPaths::ProjectDir() / TEXT("docs/DESIGN.md");
	FString Doc;
	if (!TestTrue(*FString::Printf(TEXT("설계서를 읽는다 (%s)"), *DocPath),
		FFileHelper::LoadFileToString(Doc, *DocPath)))
	{
		return false;
	}

	const FString Section = ExtractSection(Doc, TEXT("### 3.1"));
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

	UDataTable* VitalsTable = LoadObject<UDataTable>(nullptr, VitalsTablePath);
	if (!TestNotNull(TEXT("DT_PlayerVitalsTable 로드"), VitalsTable))
	{
		return false;
	}

	const FPlayerVitalsRow* Row = VitalsTable->FindRow<FPlayerVitalsRow>(VitalsRowName, TEXT("PlayerVitalsTableTest"));
	if (!TestNotNull(TEXT("`Player` 행이 있다"), Row))
	{
		return false;
	}

	for (const FString& Issue : CollectVitalsIssues(*Row, DesignValues))
	{
		AddError(Issue);
	}

	return true;
}


// 스태미나 = 수영 전용 (DESIGN 3.1, 사용자 방향 2026-09-23) — **실제 표의 행**으로 굴려서
// 달리기·공격이 스태미나를 한 방울도 안 쓰는지 본다. 위 `Component.DrainsAndRecovers` 는 소모 기능이
// 살아 있는지를 검사 전용 값으로 보고(수영이 그 기능을 쓴다), 이 검사는 **지금 출하되는 값**이
// 사용자 방향대로인지를 본다 — 둘 다 있어야 "기능은 남기고 값만 껐다" 가 성립한다.
// 숫자는 여기 없다: 달린 시간은 표의 최대 스태미나 ÷ 회복 속도보다 넉넉히 길게 잡고, 기대값은 표에서 읽는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerVitalsRunAndAttackAreFreeTest,
	"Snarehaven.PlayerVitals.Table.RunAndAttackAreFree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerVitalsRunAndAttackAreFreeTest::RunTest(const FString& Parameters)
{
	UDataTable* const VitalsTable = LoadObject<UDataTable>(nullptr, VitalsTablePath);
	if (!TestNotNull(TEXT("DT_PlayerVitalsTable 로드"), VitalsTable))
	{
		return false;
	}

	const FPlayerVitalsRow* const Row =
		VitalsTable->FindRow<FPlayerVitalsRow>(VitalsRowName, TEXT("RunAndAttackAreFree"));
	if (!TestNotNull(TEXT("`Player` 행이 있다"), Row))
	{
		return false;
	}

	UPlayerVitalsComponent* const Component = NewObject<UPlayerVitalsComponent>();
	if (!TestTrue(TEXT("출하되는 행으로 초기화된다 (0 은 미정이 아니다)"), Component->InitializeFromRow(*Row)))
	{
		return false;
	}

	const float FullStamina = Component->GetMaxStamina();
	const float FullHealth = Component->GetHealth();

	// 20초 달린다 — 소모가 조금이라도 있으면(설계 이력의 5/초라면 4초면 바닥) 여기서 티가 난다.
	constexpr float RunSeconds = 20.f;
	Component->SetMoveInput(true);
	Component->SetRunInput(true);
	TickFor(*Component, RunSeconds);
	TestEqual(TEXT("20초 달려도 스태미나가 가득 그대로"), Component->GetStamina(), FullStamina, TestTolerance);
	TestTrue(TEXT("계속 달릴 수 있다"), Component->CanRun());
	TestFalse(TEXT("탈진하지 않는다"), Component->IsExhausted());

	// 그 상태로 5타. 공격도 공짜라 한 대도 막히지 않는다.
	for (int32 Hit = 0; Hit < 5; ++Hit)
	{
		TestTrue(FString::Printf(TEXT("%d타째가 나간다"), Hit + 1), Component->TryConsumeAttackStamina());
	}
	TestEqual(TEXT("5타 뒤에도 스태미나가 가득 그대로"), Component->GetStamina(), FullStamina, TestTolerance);
	TestTrue(TEXT("계속 때릴 수 있다"), Component->CanAttack());

	// 스태미나를 쓰는 길이 없으니 탈진도 없다 = 체력이 여기서 깎일 이유가 없다 (DESIGN 3.1 "체력이 닳는 길").
	TestEqual(TEXT("달리고 때려도 체력이 안 준다"), Component->GetHealth(), FullHealth, TestTolerance);

	// 소모 **기능** 자체는 남아 있다 — 수영(8.9)이 이 길을 쓴다. 표 값만 0 이라는 것을 여기서 못 박는다.
	{
		FPlayerVitalsRow SwimmingLikeRow = *Row;
		SwimmingLikeRow.RunStaminaPerSecond = 10.f;   // 검사 전용 값 (Verification.md 6)
		UPlayerVitalsComponent* const Swimmer = NewObject<UPlayerVitalsComponent>();
		Swimmer->InitializeFromRow(SwimmingLikeRow);
		Swimmer->SetMoveInput(true);
		Swimmer->SetRunInput(true);
		TickFor(*Swimmer, 1.f);
		TestTrue(TEXT("역검증: 소모값이 0 이 아니면 그대로 닳는다"), Swimmer->GetStamina() < FullStamina);
	}

	return true;
}


// 위 검사가 "표가 멀쩡해서" 통과할 때도 검사 로직이 살아 있는지 본다 (역검증).
// 에셋·설계서를 건드리지 않고 행과 문서 값을 손으로 지어 먹인다 (Verification.md 6·13).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerVitalsTableSelfTest,
	"Snarehaven.PlayerVitals.Table.CheckCatchesBrokenRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerVitalsTableSelfTest::RunTest(const FString& Parameters)
{
	TMap<FString, double> DesignValues;
	for (const FVitalsField& Field : VitalsFields)
	{
		DesignValues.Add(Field.DesignLabel, 7.0);
	}

	FPlayerVitalsRow GoodRow;
	for (const FVitalsField& Field : VitalsFields)
	{
		GoodRow.*(Field.Member) = 7.0f;
	}
	TestEqual(TEXT("문서와 같은 행은 문제 0개"), CollectVitalsIssues(GoodRow, DesignValues).Num(), 0);

	FPlayerVitalsRow BadRow = GoodRow;
	BadRow.RunStaminaPerSecond = 99.f;
	TestEqual(TEXT("한 칸만 달라도 잡는다"), CollectVitalsIssues(BadRow, DesignValues).Num(), 1);

	// 설계서 줄을 못 읽는 경우도 통과가 아니라 문제로 센다.
	DesignValues.Remove(TEXT("체력 자연 회복"));
	TestEqual(TEXT("문서 줄을 못 읽으면 문제로 센다"), CollectVitalsIssues(GoodRow, DesignValues).Num(), 1);

	return true;
}


// ROADMAP M11-03 ①: 죽으면 사망 알림이 **한 번** 오고, 부활은 표의 값으로만 돌아온다.
// 검사 전용 값으로 돈다 (Verification.md 6) — 부활 값은 일부러 최대의 절반이 아니어서,
// 코드가 "최대 * 0.5" 를 지어내면 여기서 잡힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerVitalsDeathTest,
	"Snarehaven.PlayerVitals.Component.DiesAndRevives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerVitalsDeathTest::RunTest(const FString& Parameters)
{
	// 부활 칸이 미정(-1)인 행으로는 켜지지 않는다 — 부활하자마자 다시 죽는 것을 막는다.
	{
		FPlayerVitalsRow Row = MakeTestRow();
		Row.RespawnHealth = -1.f;
		UPlayerVitalsComponent* Component = NewObject<UPlayerVitalsComponent>();
		TestFalse(TEXT("부활 체력이 미정이면 초기화되지 않는다"), Component->InitializeFromRow(Row));
	}

	UPlayerVitalsComponent* Component = MakeInitializedComponent(*this);

	// 스태미나를 말리고 계속 밀어붙이면 탈진으로 죽는다 (DESIGN 3.1 — 체력이 닳는 길 ①).
	Component->SetMoveInput(true);
	Component->SetRunInput(true);
	TickFor(*Component, 2.f);   // 20/초 * 2초 = 40 = 최대 스태미나
	TestTrue(TEXT("달려서 스태미나가 바닥났다"), Component->GetStamina() <= TestTolerance);

	// 탈진 5/초 * 12초 = 60 = 최대 체력. 넉넉히 굴려 반드시 0 을 지나게 한다.
	TickFor(*Component, 15.f);
	TestTrue(TEXT("탈진으로 죽었다"), Component->IsDead());
	TestEqual(TEXT("죽은 체력은 0"), Component->GetHealth(), 0.f, TestTolerance);
	TestFalse(TEXT("죽으면 달릴 수 없다"), Component->CanRun());
	TestFalse(TEXT("죽으면 공격할 수 없다"), Component->CanAttack());

	// 죽은 뒤로는 시간이 더 흘러도 값이 안 움직인다 — 사망 알림이 여러 번 나갈 자리가 없다.
	TickFor(*Component, 5.f);
	TestEqual(TEXT("죽은 뒤에는 체력이 그대로"), Component->GetHealth(), 0.f, TestTolerance);

	// 부활은 표의 값 그대로. **최대치의 절반이 아니다** (검사 행은 24/12, 최대는 60/40).
	Component->ReviveAtRespawnValues();
	TestFalse(TEXT("부활하면 사망 상태가 풀린다"), Component->IsDead());
	TestEqual(TEXT("부활 체력은 표 값"), Component->GetHealth(), TestRespawnHealth, TestTolerance);
	TestEqual(TEXT("부활 스태미나는 표 값"), Component->GetStamina(), TestRespawnStamina, TestTolerance);
	TestFalse(TEXT("부활하면 탈진이 풀린다"), Component->IsExhausted());
	TestTrue(TEXT("부활하면 다시 달릴 수 있다"), Component->CanRun());

	// 살아 있는 사람에게 부활을 부르면 체력이 **깎이지 않는다** (100 -> 50 사고 방지).
	const float HealthBefore = Component->GetHealth();
	Component->ReviveAtRespawnValues();
	TestEqual(TEXT("살아 있으면 부활이 아무것도 안 한다"), Component->GetHealth(), HealthBefore, TestTolerance);

	// 부활 직후 회복이 곧바로 돈다 — 대기 시간이 0 에 머물러 있으면 여기서 안 찬다.
	Component->SetMoveInput(false);
	Component->SetRunInput(false);
	TickFor(*Component, 1.f);
	TestTrue(TEXT("부활 뒤 스태미나가 다시 찬다"), Component->GetStamina() > TestRespawnStamina);

	return true;
}


// ROADMAP M11-03 ②: 사망 처리가 **로컬에서 재화·인벤토리를 직접 바꾸지 않는가** (합격 기준 그대로).
// 소스를 글자로 훑는 검사다 — 월드·백엔드 없이 돌고, 금지된 호출이 새로 끼면 바로 운다
// (`Snarehaven.Analytics.Channel.SinglePath` 와 같은 짜임).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerDeathTouchesNoBalanceTest,
	"Snarehaven.PlayerVitals.Death.TouchesNoBalance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerDeathTouchesNoBalanceTest::RunTest(const FString& Parameters)
{
	const FString SourcePath = FPaths::ProjectDir() / TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp");
	FString Source;
	if (!TestTrue(*FString::Printf(TEXT("캐릭터 소스를 읽는다 (%s)"), *SourcePath),
		FFileHelper::LoadFileToString(Source, *SourcePath)))
	{
		return false;
	}

	const FString Body = ExtractFunctionBody(Source, TEXT("void ASnarehavenCharacter::HandlePlayerDied()"));
	if (!TestTrue(TEXT("HandlePlayerDied 몸통을 잘라냈다"), !Body.IsEmpty()))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("HandlePlayerDied 몸통 %d글자를 훑는다"), Body.Len()));

	// 보유를 바꾸는 길은 백엔드 스냅샷뿐이다 (DESIGN 4.5 · CLAUDE.md 8) — 사망 처리가 그 길을 타면 반려다.
	const TCHAR* ForbiddenCalls[] = {
		TEXT("ApplyBackendSnapshot"),
		TEXT("QueuePendingGrant"),
		TEXT("ApplyPlayerSnapshot"),
		TEXT("SetCoin"),
		TEXT("SetGem"),
		TEXT("Balance"),
	};
	// 부활은 이제 **쓰러짐이 끝난 뒤**(`FinishDownedSequence`)로 옮겼다 (M5-01e2) — 재화를 안 건드리는지는
	// 그쪽 몸통도 같이 봐야 한다. 둘을 이어 붙여 한 번에 훑는다.
	const FString FinishBody =
		ExtractFunctionBody(Source, TEXT("void ASnarehavenCharacter::FinishDownedSequence()"));
	if (!TestTrue(TEXT("FinishDownedSequence 몸통을 잘라냈다"), !FinishBody.IsEmpty()))
	{
		return false;
	}
	const FString DeathPath = Body + FinishBody;

	for (const TCHAR* Call : ForbiddenCalls)
	{
		TestFalse(FString::Printf(TEXT("사망 처리가 '%s' 를 부르지 않는다 (CLAUDE.md 8)"), Call),
			DeathPath.Contains(Call));
	}

	// 반대로 **있어야 하는 것** — 잃는 것은 지급 대기열뿐이고(DESIGN 3.1), 백엔드로는 사실 한 줄만 나간다.
	TestTrue(TEXT("지급 대기열을 버린다"), Body.Contains(TEXT("ClearPendingGrants")));
	TestTrue(TEXT("죽었다는 사실 한 줄을 통로로 보낸다"), Body.Contains(TEXT("WriteGameEvent(TEXT(\"player_died\")")));
	// 죽자마자 옮기지 않는다 — 쓰러짐을 먼저 틀고(M5-01e2), 부활은 그 끝에서 호스트가 한다 (DESIGN 3.1).
	TestFalse(TEXT("죽는 즉시 옮기지 않는다"), Body.Contains(TEXT("Server_RespawnAtPlayerStart")));
	TestTrue(TEXT("먼저 쓰러진다"), Body.Contains(TEXT("BeginDownedSequence")));
	TestTrue(TEXT("쓰러짐이 끝나면 시작 자리 부활을 호스트에 맡긴다"),
		FinishBody.Contains(TEXT("Server_RespawnAtPlayerStart")));

	// 자기검사 — 위 훑기가 정말 무는지 지어낸 몸통으로 확인한다 (Verification.md 13).
	const FString FakeSource =
		TEXT("void ASnarehavenCharacter::HandlePlayerDied()\n")
		TEXT("{\n")
		TEXT("\tInventory->ApplyBackendSnapshot(Slots);\n")
		TEXT("}\n");
	const FString FakeBody = ExtractFunctionBody(FakeSource, TEXT("void ASnarehavenCharacter::HandlePlayerDied()"));
	TestTrue(TEXT("역검증: 지어낸 몸통에서는 금지 호출이 보인다"), FakeBody.Contains(TEXT("ApplyBackendSnapshot")));
	TestFalse(TEXT("역검증: 지어낸 몸통에는 대기열 폐기가 없다"), FakeBody.Contains(TEXT("ClearPendingGrants")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerVitalsEquipmentRegenTest,
	"Snarehaven.PlayerVitals.Component.EquipmentRegen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerVitalsEquipmentRegenTest::RunTest(const FString& Parameters)
{
	// DESIGN 13.5 딸기 모자 — 쓴 쪽이 안 쓴 쪽보다 10초에 +1 더 찬다(자연 회복은 둘이 같다). 벗으면(0) 차이가 멈춘다.
	UPlayerVitalsComponent* const Bare = MakeInitializedComponent(*this);
	UPlayerVitalsComponent* const Worn = MakeInitializedComponent(*this);
	Bare->ApplyDamage(50.f);
	Worn->ApplyDamage(50.f);
	Worn->SetEquipmentHealthRegen(0.1f);
	TickFor(*Bare, 10.f);
	TickFor(*Worn, 10.f);
	TestEqual(TEXT("모자 10초 = 체력 +1 더"), Worn->GetHealth() - Bare->GetHealth(), 1.f, TestTolerance);

	Worn->SetEquipmentHealthRegen(0.f);
	TickFor(*Bare, 5.f);
	TickFor(*Worn, 5.f);
	TestEqual(TEXT("벗으면 더 벌어지지 않는다"), Worn->GetHealth() - Bare->GetHealth(), 1.f, TestTolerance);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
