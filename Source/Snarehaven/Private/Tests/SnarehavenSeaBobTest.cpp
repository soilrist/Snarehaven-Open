// 정박한 배 흔들림 2층 검사 — 폭·주기·크기 몫이 DESIGN 8.9 「항구」 표대로인가.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/SeaActor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenSeaBobTest, "Snarehaven.World.Sea.Bob",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenSeaBobTest::RunTest(const FString& Parameters)
{
	using namespace SnareSea;
	TestEqual(TEXT("모터보트(500cm)는 다 흔들린다"), BobScale(500.f), 1.f);
	TestEqual(TEXT("요트(1200cm)는 절반"), BobScale(1200.f), 0.5f);
	TestEqual(TEXT("대형선(6000cm)도 0.2 는 남는다"), BobScale(6000.f), 0.2f);
	TestTrue(TEXT("0초 위상 0 은 제자리 높이"), FMath::IsNearlyZero(BobOffset(0.0, 0.f).X, 0.01f));
	TestTrue(TEXT("주기 ¼(1초)에 가장 높다"), FMath::IsNearlyEqual(BobOffset(1.0, 0.f).X, BobHeightCm, 0.01f));
	TestTrue(TEXT("한 주기(4초) 뒤 같은 높이"), FMath::IsNearlyEqual(BobOffset(4.0, 0.5f).X, BobOffset(0.0, 0.5f).X, 0.01f));
	bool bInRange = true;
	for (double T = 0.0; T < 60.0; T += 0.37)
	{
		const FVector2f Bob = BobOffset(T, 1.f);
		bInRange &= FMath::Abs(Bob.X) <= BobHeightCm + 0.01f && FMath::Abs(Bob.Y) <= BobRollDeg + 0.01f;
	}
	TestTrue(TEXT("60초 동안 폭·기울기가 한도 안"), bInRange);
	return true;
}

#endif
