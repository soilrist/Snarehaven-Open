// ROADMAP M85-02 2층 검사 — 표정 규칙 함수 표 전 행.
//  우선순위(쓰러짐 > 아픔 > 웃음 > 기본) · 문턱 경계값 위·아래 · 독/먹기 지속 경계 · 없던 일(음수) 처리.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Player/SnareFaceExpression.h"
#include "Player/SnareLowHealth.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFaceExpressionRuleTest, "Snarehaven.Player.Face.ExpressionRuleTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFaceExpressionRuleTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 (Verification.md 6) — 최대 체력 100, 문턱은 규칙 상수에서 계산한다.
	constexpr float Max = 100.f;
	const float AtThreshold = Max * SnareLowHealth::Threshold;
	const float JustAbove = AtThreshold + 1.f;
	constexpr float None = -1.f;

	struct FRow
	{
		const TCHAR* Label;
		float Health;
		float MaxHealth;
		bool bDowned;
		float SincePoison;
		float SinceAte;
		ESnareFaceExpression Expected;
	};
	const FRow Rows[] = {
		{ TEXT("멀쩡 = 기본"), Max, Max, false, None, None, ESnareFaceExpression::Neutral },
		{ TEXT("문턱 딱 = 아픔"), AtThreshold, Max, false, None, None, ESnareFaceExpression::Hurt },
		{ TEXT("문턱 바로 위 = 기본"), JustAbove, Max, false, None, None, ESnareFaceExpression::Neutral },
		{ TEXT("쓰러짐 = 기절"), Max, Max, true, None, None, ESnareFaceExpression::Down },
		{ TEXT("쓰러짐 + 체력 0 = 기절(아픔보다 우선)"), 0.f, Max, true, None, None, ESnareFaceExpression::Down },
		{ TEXT("쓰러짐 + 방금 먹음 = 기절(웃음보다 우선)"), Max, Max, true, None, 0.f, ESnareFaceExpression::Down },
		{ TEXT("독 직후 = 아픔"), Max, Max, false, 0.f, None, ESnareFaceExpression::Hurt },
		{ TEXT("독 1.5초 딱 = 아픔"), Max, Max, false, SnareFaceExpression::PoisonHurtSeconds, None, ESnareFaceExpression::Hurt },
		{ TEXT("독 1.5초 지남 = 기본"), Max, Max, false, SnareFaceExpression::PoisonHurtSeconds + 0.01f, None,
			ESnareFaceExpression::Neutral },
		{ TEXT("먹은 직후 = 웃음"), Max, Max, false, None, 0.f, ESnareFaceExpression::Happy },
		{ TEXT("먹고 1.5초 딱 = 웃음"), Max, Max, false, None, SnareFaceExpression::HappySeconds, ESnareFaceExpression::Happy },
		{ TEXT("먹고 1.5초 지남 = 기본"), Max, Max, false, None, SnareFaceExpression::HappySeconds + 0.01f,
			ESnareFaceExpression::Neutral },
		{ TEXT("위험 체력 + 방금 먹음 = 아픔이 웃음보다 우선"), AtThreshold, Max, false, None, 0.f, ESnareFaceExpression::Hurt },
		{ TEXT("독 + 방금 먹음(독 먹기) = 아픔"), Max, Max, false, 0.f, 0.f, ESnareFaceExpression::Hurt },
		{ TEXT("최대 체력 0 = 체력 비율 무시 → 기본"), 0.f, 0.f, false, None, None, ESnareFaceExpression::Neutral },
	};
	for (const FRow& Row : Rows)
	{
		const ESnareFaceExpression Got = SnareFaceExpression::For(Row.Health, Row.MaxHealth, Row.bDowned, Row.SincePoison, Row.SinceAte);
		TestEqual(Row.Label, SnareFaceExpression::NameOf(Got), SnareFaceExpression::NameOf(Row.Expected));
	}

	// 4개 이름이 서로 다르다 — 이름 표에 빈 칸·중복 없음.
	TSet<FString> Names;
	for (const ESnareFaceExpression E : { ESnareFaceExpression::Neutral, ESnareFaceExpression::Hurt, ESnareFaceExpression::Down, ESnareFaceExpression::Happy })
	{
		Names.Add(SnareFaceExpression::NameOf(E));
	}
	TestEqual(TEXT("표정 이름 4개 전부 다름"), Names.Num(), 4);
	// M86-02 검사 스위치 초 값 — 양수만 예약. 시험 전용 값.
	TestTrue(TEXT("스위치 2초 = 예약"), SnareFaceExpression::IsValidCheckDelay(2.f));
	TestTrue(TEXT("스위치 0.01초 = 예약"), SnareFaceExpression::IsValidCheckDelay(0.01f));
	TestFalse(TEXT("스위치 0 = 거절"), SnareFaceExpression::IsValidCheckDelay(0.f));
	TestFalse(TEXT("스위치 음수 = 거절"), SnareFaceExpression::IsValidCheckDelay(-1.f));
	TestFalse(TEXT("스위치 NaN = 거절"), SnareFaceExpression::IsValidCheckDelay(FMath::Sqrt(-1.f)));
	TestFalse(TEXT("스위치 무한대 = 거절"), SnareFaceExpression::IsValidCheckDelay(std::numeric_limits<float>::infinity()));
	AddInfo(FString::Printf(TEXT("[Face] 표정 규칙 표 %d행"), UE_ARRAY_COUNT(Rows)));
	return true;
}

#endif
