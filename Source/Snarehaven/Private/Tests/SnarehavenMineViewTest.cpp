// 광산 굴 안 보기 2층 검사 — 굴 안 판정과 카메라→플레이어 가림 판정.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.World.MineView; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/SnareMineView.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMineViewTest, "Snarehaven.World.MineView.Occlusion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMineViewTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("큰 방 한가운데(72030, -600)는 굴 안"), SnareMineView::IsInMine(FVector(72030.f, -600.f, 90.f)));
	TestFalse(TEXT("설원 입구(28350, -34300)는 굴 밖"), SnareMineView::IsInMine(FVector(28350.f, -34300.f, 90.f)));
	TestFalse(TEXT("굴 옆 바다 쪽(70000, 5000)은 굴 밖"), SnareMineView::IsInMine(FVector(70000.f, 5000.f, 90.f)));

	// 카메라는 플레이어 남쪽 위(X 작은 쪽)에서 북쪽을 본다고 친다.
	const FVector Target(0.f, 0.f, 90.f);
	const FVector Camera(-600.f, 0.f, 500.f);
	const FBox Between(FVector(-300.f, -100.f, 0.f), FVector(-200.f, 100.f, 300.f));
	const FBox Beside(FVector(-300.f, 400.f, 0.f), FVector(-200.f, 600.f, 300.f));
	const FBox Behind(FVector(200.f, -100.f, 0.f), FVector(300.f, 100.f, 300.f));
	const FBox Around(FVector(-50.f, -50.f, 0.f), FVector(50.f, 50.f, 300.f));
	TestTrue(TEXT("카메라와 플레이어 사이 바위는 가린다"), SnareMineView::OccludesView(Between, Camera, Target));
	TestFalse(TEXT("옆 바위는 안 가린다"), SnareMineView::OccludesView(Beside, Camera, Target));
	TestFalse(TEXT("플레이어 뒤 바위는 안 가린다"), SnareMineView::OccludesView(Behind, Camera, Target));
	TestTrue(TEXT("플레이어를 품은 바위(충돌 없는 벽 바위가 겹친 경우)도 가린다"), SnareMineView::OccludesView(Around, Camera, Target));
	TestFalse(TEXT("빈 바운드는 안 가린다"), SnareMineView::OccludesView(FBox(ForceInit), Camera, Target));
	return true;
}

#endif
