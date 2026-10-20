// ROADMAP M17-04 2층 검사 — 성능 예산 판정과 로그 줄. 판정은 순수 함수의 실제 반환값이다 (PROMPT 12).
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.World.PerfLog; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/SnarePerfLog.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPerfLogTest, "Snarehaven.World.PerfLog.BudgetAndLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPerfLogTest::RunTest(const FString& Parameters)
{
	// 60 프레임이 2초 → 33.3ms
	TestEqual(TEXT("평균 프레임 시간"), SnarePerfLog::AverageFrameMs(60, 2.0), 1000.f / 30.f, 0.01f);
	TestEqual(TEXT("프레임 0 이면 0"), SnarePerfLog::AverageFrameMs(0, 1.0), 0.f);
	TestEqual(TEXT("시간 0 이면 0"), SnarePerfLog::AverageFrameMs(10, 0.0), 0.f);

	TestTrue(TEXT("16.6ms 는 예산 안"), SnarePerfLog::IsWithinBudget(16.6f));
	TestTrue(TEXT("33ms 는 예산 안"), SnarePerfLog::IsWithinBudget(33.f));
	TestTrue(TEXT("33.4ms 는 예산 안"), SnarePerfLog::IsWithinBudget(33.4f));
	TestFalse(TEXT("34.5ms 는 예산 초과"), SnarePerfLog::IsWithinBudget(34.5f));
	TestFalse(TEXT("50ms 는 예산 초과"), SnarePerfLog::IsWithinBudget(50.f));
	TestFalse(TEXT("표본 없음은 안이 아니다"), SnarePerfLog::IsWithinBudget(0.f));

	// M18-05: Metal 은 RHI 드로콜이 0 → 메시 드로 명령 수로 대신한다. RHI 값이 있으면 그것이 이긴다.
	TestEqual(TEXT("RHI 0 이면 메시 드로 명령"), SnarePerfLog::ChooseDrawCalls(0, 412), 412);
	TestEqual(TEXT("RHI 값이 있으면 RHI"), SnarePerfLog::ChooseDrawCalls(900, 412), 900);
	TestEqual(TEXT("둘 다 없으면 0"), SnarePerfLog::ChooseDrawCalls(0, -1), 0);

	SnarePerfLog::FSample Sample;
	Sample.AverageFrameMs = 50.f;
	Sample.WorstFrameMs = 80.f;
	Sample.Animals = 12;
	Sample.DrawCalls = 840;
	const FString Over = SnarePerfLog::FormatLine(Sample);
	TestEqual(TEXT("초과 줄"), Over, FString(TEXT("[Perf] 평균 50.0ms (20.0fps) 최악 80.0ms 동물 12 드로콜 840 예산 초과 발열 모름")));
	Sample.AverageFrameMs = 25.f;
	Sample.Thermal = FCoreDelegates::ETemperatureSeverity::Good;
	TestTrue(TEXT("안 줄은 예산 안·발열로 끝난다"), SnarePerfLog::FormatLine(Sample).EndsWith(TEXT("예산 안 발열 좋음")));
	return true;
}

// ROADMAP M21-08b 2층 검사 — 콜드 스타트 → 조작 가능 초와 발열 줄 형식. 목표 상수는 DESIGN 4.2 표 값이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPerfColdStartTest, "Snarehaven.World.PerfLog.ColdStartAndThermal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPerfColdStartTest::RunTest(const FString& Parameters)
{
	using ESeverity = FCoreDelegates::ETemperatureSeverity;
	TestEqual(TEXT("목표 = DESIGN 4.2 15초"), SnarePerfLog::ColdStartTargetSeconds, 15.f);
	TestEqual(TEXT("시작 100초 · 지금 112.34초 → 12.34초"), SnarePerfLog::ColdStartSeconds(100.0, 112.34), 12.34f, 0.001f);
	TestEqual(TEXT("시계가 거꾸로면 0"), SnarePerfLog::ColdStartSeconds(100.0, 99.0), 0.f);

	TestEqual(TEXT("목표 안 줄"), SnarePerfLog::FormatColdStartLine(12.34f, ESeverity::Good),
		FString(TEXT("[Perf] 콜드 스타트 → 조작 가능 12.3초 (목표 15초 안) 발열 좋음")));
	TestEqual(TEXT("경계 15초는 안"), SnarePerfLog::FormatColdStartLine(15.f, ESeverity::Unknown),
		FString(TEXT("[Perf] 콜드 스타트 → 조작 가능 15.0초 (목표 15초 안) 발열 모름")));
	TestEqual(TEXT("목표 초과 줄"), SnarePerfLog::FormatColdStartLine(15.1f, ESeverity::Serious),
		FString(TEXT("[Perf] 콜드 스타트 → 조작 가능 15.1초 (목표 15초 초과) 발열 뜨거움")));

	TestEqual(TEXT("Bad = 따뜻함"), FString(SnarePerfLog::ThermalName(ESeverity::Bad)), FString(TEXT("따뜻함")));
	TestEqual(TEXT("Critical = 위험"), FString(SnarePerfLog::ThermalName(ESeverity::Critical)), FString(TEXT("위험")));
	TestEqual(TEXT("애플 0 = 좋음"), SnarePerfLog::SeverityFromAppleThermalState(0), ESeverity::Good);
	TestEqual(TEXT("애플 2 = 심함"), SnarePerfLog::SeverityFromAppleThermalState(2), ESeverity::Serious);
	TestEqual(TEXT("애플 3 = 위험"), SnarePerfLog::SeverityFromAppleThermalState(3), ESeverity::Critical);
	TestEqual(TEXT("애플 범위 밖 = 모름"), SnarePerfLog::SeverityFromAppleThermalState(7), ESeverity::Unknown);
	TestEqual(TEXT("범위 밖 = 모름"), FString(SnarePerfLog::ThermalName(ESeverity::NumSeverities)), FString(TEXT("모름")));
	return true;
}

#endif
