// 가림 페이드 2층 검사 — 카메라→캐릭터 선을 막는 나무는 뚫리고, 옆·뒤·발치는 그대로, 0.2초 안에 켜지고 꺼진다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.World.ViewFade; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/SnareViewFade.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenViewFadeCoverageTest, "Snarehaven.World.ViewFade.Coverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenViewFadeCoverageTest::RunTest(const FString& Parameters)
{
	// 시험 전용 배치: 캐릭터 가슴 (0,0,90), 카메라는 55° 위·+X 쪽 4500cm.
	const FVector Target(0.f, 0.f, 90.f);
	const FVector Dir(FMath::Cos(FMath::DegreesToRadians(55.f)), 0.f, FMath::Sin(FMath::DegreesToRadians(55.f)));
	const FVector Camera = Target + Dir * 4500.f;
	const float Full = SnareViewFade::MaxCoverage;
	TestEqual(TEXT("선 위 500cm 앞 잎은 최대로 뚫림"), SnareViewFade::Coverage(Camera, Target, Target + Dir * 500.f, 1.f), Full, 0.001f);
	TestTrue(TEXT("선에서 150cm 옆 잎도 반 넘게 뚫림"), SnareViewFade::Coverage(Camera, Target, Target + Dir * 500.f + FVector(0.f, 150.f, 0.f), 1.f) > Full * 0.5f);
	TestEqual(TEXT("선에서 300cm 옆(반지름 밖)은 그대로"), SnareViewFade::Coverage(Camera, Target, Target + Dir * 500.f + FVector(0.f, 300.f, 0.f), 1.f), 0.f, 0.001f);
	TestEqual(TEXT("캐릭터 뒤(카메라 반대쪽) 잎은 그대로"), SnareViewFade::Coverage(Camera, Target, Target - Dir * 300.f, 1.f), 0.f, 0.001f);
	TestEqual(TEXT("캐릭터 바로 옆(앞 40cm 안)은 그대로 — 발치 땅·풀"), SnareViewFade::Coverage(Camera, Target, Target + Dir * 20.f, 1.f), 0.f, 0.001f);
	// 집 뒤: 캐릭터가 뒷벽에서 66cm 떨어져 서면 선이 벽 면을 지나는 점(높이 +94)은 Along ≈ 115 — 벽이 거의 다 뚫려야 몸이 보인다
	TestTrue(TEXT("집 뒷벽 한 겹(선 위 Along 115)도 뚫림"), SnareViewFade::Coverage(Camera, Target, Target + Dir * 115.f, 1.f) > Full * 0.9f);
	TestTrue(TEXT("지붕 발자국 안쪽은 집 안"), SnareViewFade::IsInsideRoofFootprint(FVector2D(580.f, -655.f), FVector2D(1060.f, -5.f), FVector(820.f, -330.f, 0.f)));
	TestFalse(TEXT("집 뒤 66cm 밖은 집 안이 아님"), SnareViewFade::IsInsideRoofFootprint(FVector2D(580.f, -655.f), FVector2D(1060.f, -5.f), FVector(1100.f, -450.f, 0.f)));
	TestFalse(TEXT("벽 바로 안쪽 가장자리(여유 안)는 아직 안 숨김"), SnareViewFade::IsInsideRoofFootprint(FVector2D(580.f, -655.f), FVector2D(1060.f, -5.f), FVector(590.f, -330.f, 0.f)));
	TestEqual(TEXT("꺼진 상태(0)에서는 선 위여도 그대로"), SnareViewFade::Coverage(Camera, Target, Target + Dir * 500.f, 0.f), 0.f, 0.001f);
	TestTrue(TEXT("최대 몫이 1 미만 — 나무가 아주 사라지지 않음"), Full < 1.f);

	float S = 0.f;
	S = SnareViewFade::StepStrength(S, true, SnareViewFade::FadeSeconds);
	TestEqual(TEXT("켜지는 데 FadeSeconds"), S, 1.f, 0.001f);
	S = SnareViewFade::StepStrength(S, false, SnareViewFade::FadeSeconds);
	TestEqual(TEXT("꺼지는 데 FadeSeconds"), S, 0.f, 0.001f);
	TestTrue(TEXT("페이드는 0.2초 이하"), SnareViewFade::FadeSeconds <= 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenViewFadeStoreShotTest, "Snarehaven.World.ViewFade.StoreShotOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenViewFadeStoreShotTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("보통 플레이는 뚫음"), SnareViewFade::WantFade(true, false));
	TestFalse(TEXT("스토어 촬영이면 안 뚫음"), SnareViewFade::WantFade(true, true));
	TestFalse(TEXT("보는 사람 없으면 안 뚫음"), SnareViewFade::WantFade(false, false));

	// 켜져 있던 페이드도 스토어 촬영이면 FadeSeconds 안에 0 — 카메라 선 위 잎도 그대로 그린다.
	const FVector Target(0.f, 0.f, 90.f);
	const FVector Camera(-1000.f, 0.f, 1000.f);
	const FVector OnLine = Target + (Camera - Target).GetSafeNormal() * 500.f;
	const float S = SnareViewFade::StepStrength(1.f, SnareViewFade::WantFade(true, true), SnareViewFade::FadeSeconds);
	TestEqual(TEXT("스토어 촬영 가림 몫 0"), SnareViewFade::Coverage(Camera, Target, OnLine, S), 0.f, 0.001f);
	return true;
}

#endif
