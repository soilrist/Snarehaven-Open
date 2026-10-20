// 부활 자리 = 내 집 문 앞 (DESIGN 8.12 「사망」 표 「부활 자리」, INBOX 지시 2026-10-13).
// ① 규칙 함수 `SnareRevive::HomeFrontLocation` — 집 가운데에서 문 쪽으로 (긴 변의 반 + 여유).
// ② 연결 — 집이 있으면 집 앞, 없으면(또는 막히면) 시작 자리. 호스트가 주인 ID 로 내 집을 가린다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.World.ReviveHome; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Player/SnareRevive.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnareReviveHomeTest, "Snarehaven.World.ReviveHome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSnareReviveHomeTest::RunTest(const FString& Parameters)
{
	// 테스트 전용 값 — 칸 100cm, 발자국 4x2, 집 가운데 (1000, 2000, 50).
	const FVector Home(1000.f, 2000.f, 50.f);
	const FIntPoint Footprint(4, 2);
	const float Cell = 100.f;
	const float Reach = 0.5f * Cell * 4.f + SnareRevive::HomeFrontMarginCm;

	const FVector East = SnareRevive::HomeFrontLocation(Home, 0.f, Footprint, Cell);
	TestTrue(TEXT("방향 0도 = +X 쪽 문 앞"), East.Equals(Home + FVector(Reach, 0.f, 0.f), 0.1f));
	const FVector North = SnareRevive::HomeFrontLocation(Home, 90.f, Footprint, Cell);
	TestTrue(TEXT("방향 90도 = +Y 쪽 문 앞"), North.Equals(Home + FVector(0.f, Reach, 0.f), 0.1f));
	TestTrue(TEXT("높이는 집과 같다"), FMath::IsNearlyEqual(East.Z, Home.Z));
	// 역검증: 발자국이 더 크면 더 멀리 — 벽 안에 끼지 않는다.
	const FVector Far = SnareRevive::HomeFrontLocation(Home, 0.f, FIntPoint(8, 2), Cell);
	TestTrue(TEXT("역검증: 발자국이 크면 문 앞이 더 멀다"), Far.X > East.X);
	// 발자국 0 이어도 최소 1칸으로 본다.
	TestTrue(TEXT("발자국 0 도 집 안이 아니다"), SnareRevive::HomeFrontLocation(Home, 0.f, FIntPoint(0, 0), Cell).X > Home.X);

	FString Source;
	const FString Path = FPaths::ProjectDir() / TEXT("Source/Snarehaven/Private/SnarehavenCharacter.cpp");
	if (!TestTrue(TEXT("캐릭터 소스를 읽는다"), FFileHelper::LoadFileToString(Source, *Path)))
	{
		return false;
	}
	const int32 RespawnAt = Source.Find(TEXT("void ASnarehavenCharacter::Server_RespawnAtPlayerStart_Implementation()"));
	const int32 HomeAt = Source.Find(TEXT("bool ASnarehavenCharacter::TryRespawnAtOwnHome()"));
	if (!TestTrue(TEXT("부활 두 함수 자리를 찾았다"), RespawnAt >= 0 && HomeAt > RespawnAt))
	{
		return false;
	}
	const FString RespawnBody = Source.Mid(RespawnAt, HomeAt - RespawnAt);
	const FString HomeBody = Source.Mid(HomeAt, 2500);
	const int32 TryAt = RespawnBody.Find(TEXT("TryRespawnAtOwnHome()"));
	const int32 StartAt = RespawnBody.Find(TEXT("FindPlayerStart("));
	TestTrue(TEXT("집 앞을 먼저 시도하고 없으면 시작 자리"), TryAt >= 0 && StartAt > TryAt);
	TestTrue(TEXT("내 집은 주인 ID 로 가린다"), HomeBody.Contains(TEXT("GetOwnerPlayFabId() == KnownPlayFabId")));
	TestTrue(TEXT("집 앞 자리는 규칙 함수로 정한다"), HomeBody.Contains(TEXT("SnareRevive::HomeFrontLocation(")));
	TestTrue(TEXT("막히면 false 를 돌려 시작 자리로 간다"), HomeBody.Contains(TEXT("return bMoved;")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
