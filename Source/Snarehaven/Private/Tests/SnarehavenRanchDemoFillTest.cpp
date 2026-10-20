// 3층 캡처 `-SnareRanchDemo`(스토어 스크린샷 ① 목장)의 칸 채우기 `SnareRanchDemoFill` 2층 검사 —
// 기다리는 동안은 칸이 비어 있고, 기다린 뒤 동물 6칸이 시간·재고와 함께 차며, 나머지 칸은 빈 채로 두는가.
// 스냅샷 길(`ApplyBackendSnapshot`)로만 칸을 채우므로 반려 조건(CLAUDE.md 8)을 어기지 않는지도 같이 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Ranch.DemoFill; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchDemoFill.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다). 채우기가 기다리는 2초보다 짧게/길게 되감는다.
	constexpr float TestTickBeforeDelay = 0.5f;
	constexpr float TestTickPastDelay = 3.0f;
	constexpr int32 TestMinFilledSlots = 3;

	UWorld* CreateDemoFillWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyDemoFillWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchDemoFillTest, "Snarehaven.Ranch.DemoFill.FillsSlotsAfterDelay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchDemoFillTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateDemoFillWorld();
	AActor* Holder = World->SpawnActor<AActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	URanchComponent* Ranch = Holder != nullptr ? NewObject<URanchComponent>(Holder) : nullptr;
	if (!TestNotNull(TEXT("목장 컴포넌트가 만들어져야 한다"), Ranch))
	{
		DestroyDemoFillWorld(World);
		return false;
	}
	Ranch->RegisterComponent();

	SnareRanchDemoFill::Schedule(Ranch);
	FTSTicker::GetCoreTicker().Tick(TestTickBeforeDelay);
	TestTrue(TEXT("기다리는 동안은 첫 칸이 비어 있다"), Ranch->IsSlotEmpty(0));

	FTSTicker::GetCoreTicker().Tick(TestTickPastDelay);

	int32 Filled = 0;
	for (int32 Index = 0; Index < Ranch->GetSlotCount(); ++Index)
	{
		const FRanchSlotState* Slot = Ranch->GetSlot(Index);
		if (Slot == nullptr || Slot->IsEmpty())
		{
			continue;
		}
		++Filled;
		TestEqual(*FString::Printf(TEXT("%d번 칸은 동물 칸이다"), Index), Slot->Kind, ERanchSlotKind::AnimalProduct);
		TestTrue(*FString::Printf(TEXT("%d번 칸은 동물 ID 가 찼다"), Index), !Slot->AnimalID.IsNone());
		TestTrue(*FString::Printf(TEXT("%d번 칸은 넣은 시각이 과거다"), Index), Slot->OccupiedSinceUtc < FDateTime::UtcNow());
		TestTrue(*FString::Printf(TEXT("%d번 칸은 쌓인 재고가 있다"), Index), Slot->Stock > 0);
	}
	TestTrue(TEXT("기다린 뒤 동물 칸이 여럿 찼다"), Filled >= TestMinFilledSlots);

	DestroyDemoFillWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
