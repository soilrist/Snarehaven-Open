// INBOX 2026-10-15 감사 (d)(e) — 「아이템·행동 전부 싹」: 사람이 누르는 실제 함수를 불러 몸 동작(`ItemActionCount`)이 느는지 단정한다.
// 공격 버튼 뒤 클립만 보는 `Snarehaven.Items.Sweep` 으로는 「요청만 보내고 끝나는 길」(과일나무 베기 결함)을 못 잡는다.
// 서버는 `TestServerOverride` 로 받아 두기만 한다 — 동작은 겉모습이라 서버 답을 기다리지 않아야 한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Items.ActionPaths; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Inventory/BackpackComponent.h"
#include "Ranch/RanchSyncComponent.h"
#include "SnarehavenCharacter.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenActionPathsTest,
	"Snarehaven.Items.ActionPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenActionPathsTest::RunTest(const FString& Parameters)
{
	int32 ServerCalls = 0;
	USnareBackendSubsystem::TestServerOverride =
		[&ServerCalls](const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone) { ++ServerCalls; };
	ON_SCOPE_EXIT { USnareBackendSubsystem::TestServerOverride = nullptr; };

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	if (!TestNotNull(TEXT("월드"), World))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	UBackpackComponent* const Backpack = IsValid(Player) ? Player->FindComponentByClass<UBackpackComponent>() : nullptr;
	URanchSyncComponent* const RanchSync = IsValid(Player) ? Player->FindComponentByClass<URanchSyncComponent>() : nullptr;
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("배낭"), Backpack) || !TestNotNull(TEXT("목장 동기"), RanchSync))
	{
		GameInstance->Shutdown();
		return false;
	}
	Player->DispatchBeginPlay();
	Player->ApplyPlayerBody();
	RanchSync->BindTargets(nullptr, nullptr, GameInstance->GetSubsystem<USnareBackendSubsystem>(), nullptr, nullptr, nullptr);

	// 행 = 사람이 누르는 실제 함수. 각 행은 동작이 늘고 줍기 클립(Gather)이어야 한다.
	// 컨트롤러 없는 시험 폰은 Multicast 가 한 번 더 틀어 +2 가 된다(실제 게임은 내 폰이라 +1).
	const TArray<TPair<FString, TFunction<bool()>>> Rows = {
		{ TEXT("버리기"), [&]() { return Backpack->RequestDrop(TEXT("item.wood"), 1); } },
		{ TEXT("물 채우기"), [&]() { return Backpack->RequestFill(TEXT("item.water_bottle")); } },
		{ TEXT("보물지도"), [&]() { return Backpack->RequestUseTreasureMap(); } },
		{ TEXT("목장 장식 놓기"), [&]() { return RanchSync->RequestSetRanchDeco(TEXT("item.ranch_deco_test"), NAME_None); } },
	};
	TArray<FString> Lines;
	for (const TPair<FString, TFunction<bool()>>& Row : Rows)
	{
		const int32 Before = Player->GetItemActionCount();
		const bool bSent = Row.Value();
		const bool bPlayed = Player->GetItemActionCount() > Before && Player->GetLastItemAction() == EItemAction::Gather;
		Lines.Add(FString::Printf(TEXT("%s | 요청 %s | 몸 동작 %s"), *Row.Key, bSent ? TEXT("나감") : TEXT("안 나감"),
			bPlayed ? TEXT("됨") : TEXT("안 나옴")));
		TestTrue(FString::Printf(TEXT("%s 요청이 나간다"), *Row.Key), bSent);
		TestTrue(FString::Printf(TEXT("%s 누르자마자 줍기 동작"), *Row.Key), bPlayed);
	}
	for (const FString& Line : Lines)
	{
		AddInfo(Line);
	}
	TestTrue(TEXT("서버에는 요청만 갔다(답 전)"), ServerCalls >= 3);

	GameInstance->Shutdown();
	return true;
}

#endif
