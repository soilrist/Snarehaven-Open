// 레퍼런스 실내 보기 2층 검사 — 발이 집 안(처마 뺀 XY, 지붕 아래)일 때만 덮개를 숨긴다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/SnareHouseRoof.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHouseRoofInsideTest, "Snarehaven.World.HouseRoof.Inside",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHouseRoofInsideTest::RunTest(const FString& Parameters)
{
	// 오두막 덮개 바운드 ≈ x ±240(앞뒤 내밈 40) · y ±325(처마 55) · 높이 -10~650
	const FBox Roof(FVector(-240.f, -325.f, -10.f), FVector(240.f, 325.f, 650.f));
	TestTrue(TEXT("방 한가운데는 안"), SnareHouseRoof::IsUnderRoof(Roof, FVector(0.f, 0.f, 90.f)));
	TestTrue(TEXT("문 바로 안쪽(앞벽 -200 + 30)도 안"), SnareHouseRoof::IsUnderRoof(Roof, FVector(-170.f, 0.f, 90.f)));
	TestFalse(TEXT("처마 밑 문 앞 디딤돌(-220)은 밖"), SnareHouseRoof::IsUnderRoof(Roof, FVector(-220.f, 0.f, 90.f)));
	TestFalse(TEXT("옆 처마 밑(y 300)은 밖"), SnareHouseRoof::IsUnderRoof(Roof, FVector(0.f, 300.f, 90.f)));
	TestFalse(TEXT("지붕 위(z 700)는 밖"), SnareHouseRoof::IsUnderRoof(Roof, FVector(0.f, 0.f, 700.f)));
	TestFalse(TEXT("빈 바운드는 늘 밖"), SnareHouseRoof::IsUnderRoof(FBox(ForceInit), FVector::ZeroVector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHouseInteriorVoidTest, "Snarehaven.World.HouseRoof.InteriorVoid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHouseInteriorVoidTest::RunTest(const FString& Parameters)
{
	const FBox Roof(FVector(-240.f, -325.f, -10.f), FVector(240.f, 325.f, 650.f));
	TestTrue(TEXT("집 몸통(원점 안, 집 크기)은 남는다"), SnareHouseRoof::KeepsVisibleInInterior(Roof, FVector(0.f, 0.f, 0.f), FVector(230.f, 300.f, 200.f)));
	TestTrue(TEXT("방 안 탁자는 남는다"), SnareHouseRoof::KeepsVisibleInInterior(Roof, FVector(50.f, 40.f, 40.f), FVector(70.f, 40.f, 40.f)));
	TestFalse(TEXT("집 밖 나무는 가린다"), SnareHouseRoof::KeepsVisibleInInterior(Roof, FVector(600.f, 0.f, 0.f), FVector(50.f, 50.f, 300.f)));
	TestFalse(TEXT("원점이 안이어도 집보다 훨씬 큰 땅판은 가린다"), SnareHouseRoof::KeepsVisibleInInterior(Roof, FVector(0.f, 0.f, 0.f), FVector(5000.f, 5000.f, 10.f)));
	TestFalse(TEXT("빈 바운드는 아무것도 안 남긴다"), SnareHouseRoof::KeepsVisibleInInterior(FBox(ForceInit), FVector::ZeroVector, FVector::ZeroVector));
	TestTrue(TEXT("페이드는 0.3초 이하"), SnareHouseRoof::InteriorFadeSeconds <= 0.3f);
	return true;
}

#endif
