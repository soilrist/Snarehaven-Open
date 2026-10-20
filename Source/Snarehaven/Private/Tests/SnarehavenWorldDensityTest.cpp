// ROADMAP M15-22 2층 검사 — 밀도 자가 시트 폭 안·밖을 둘 다 잡는다. 시험용 배치(맵 값 아님)로 돌려
// 나중에 맵 배치가 바뀌어도 이 시험은 안 깨진다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "World/SnareWorldDensity.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldDensityTest, "Snarehaven.World.Density.MatchesReferenceSheet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldDensityTest::RunTest(const FString& Parameters)
{
	using namespace SnareWorldDensity;

	TestTrue(TEXT("Pine → 나무"), ClassifyMeshName(TEXT("SM_Nature_Pine_03")) == EKind::Tree);
	TestTrue(TEXT("Tree → 나무"), ClassifyMeshName(TEXT("SM_Nature_Tree_01")) == EKind::Tree);
	TestTrue(TEXT("Boulder → 바위"), ClassifyMeshName(TEXT("SM_Nature_Boulder_02")) == EKind::Rock);
	TestTrue(TEXT("Stump → 기타"), ClassifyMeshName(TEXT("SM_Nature_Stump_01")) == EKind::Other);
	TestTrue(TEXT("Bush → 기타"), ClassifyMeshName(TEXT("SM_Nature_Bush_01")) == EKind::Other);

	const FBox2D View(FVector2D(0.0, 0.0), FVector2D(4000.0, 4000.0));

	// 시트 안: 나무 15 그루(격자, 서로 800cm) + 바위 4 무리(서로 멀리, 무리마다 둘씩).
	TArray<FPlacement> InSheet;
	for (int32 I = 0; I < 15; ++I)
	{
		InSheet.Add({EKind::Tree, FVector2D(500.0 + (I % 5) * 800.0, 500.0 + (I / 5) * 800.0)});
	}
	const FVector2D RockSpots[4] = {FVector2D(3700, 300), FVector2D(300, 3700), FVector2D(3700, 3700), FVector2D(2000, 3500)};
	for (const FVector2D& Spot : RockSpots)
	{
		InSheet.Add({EKind::Rock, Spot});
		InSheet.Add({EKind::Rock, Spot + FVector2D(200.0, 0.0)});
	}
	InSheet.Add({EKind::Other, FVector2D(1000, 3000)}); // 덤불은 세지 않는다
	InSheet.Add({EKind::Tree, FVector2D(9000, 9000)});  // 화면 밖은 세지 않는다
	const FResult Good = Measure(InSheet, View);
	TestEqual(TEXT("나무 수"), Good.Trees, 15);
	TestEqual(TEXT("바위 무리 수(둘씩 붙은 4곳)"), Good.RockClusters, 4);
	TestTrue(TEXT("빈 공간 절반 이상"), Good.EmptyShare >= SheetEmptyShareMin);
	TestTrue(TEXT("시트 폭 안"), IsWithinSheet(Good));

	// 시트 밖 ① 나무 30 그루 촘촘히 → 나무 수·빈 공간 둘 다 밖.
	TArray<FPlacement> Dense;
	for (int32 I = 0; I < 30; ++I)
	{
		Dense.Add({EKind::Tree, FVector2D(200.0 + (I % 6) * 200.0, 200.0 + (I / 6) * 200.0)});
	}
	TestFalse(TEXT("나무 30 그루는 폭 밖"), IsWithinSheet(Measure(Dense, View)));

	// 시트 밖 ② 아무것도 없음 → 나무·바위 부족, 빈 공간은 1.
	const FResult Empty = Measure({}, View);
	TestEqual(TEXT("빈 화면의 빈 공간 몫"), Empty.EmptyShare, 1.0f);
	TestFalse(TEXT("빈 화면은 폭 밖(나무 부족)"), IsWithinSheet(Empty));

	// 시트 밖 ③ 바위 한 무리뿐 → 무리 수 부족.
	TArray<FPlacement> OneRock = InSheet;
	OneRock.RemoveAll([](const FPlacement& P) { return P.Kind == EKind::Rock; });
	OneRock.Add({EKind::Rock, FVector2D(3700, 300)});
	const FResult Few = Measure(OneRock, View);
	TestEqual(TEXT("바위 무리 1"), Few.RockClusters, 1);
	TestFalse(TEXT("바위 무리 1 은 폭 밖"), IsWithinSheet(Few));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
