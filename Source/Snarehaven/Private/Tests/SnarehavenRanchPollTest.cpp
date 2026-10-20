// M19-06 2층 검사 — 목장 존에 가만히 서 있어도 수확 준비 표시가 켜지는가.
// 가짜 서버 시계를 앞당긴 뒤 존 재진입 없이 주기(`DefaultGame.ini` BaseStatePollSeconds)를 넘기면
// `GetBaseState` 를 다시 묻고, 그 답(서버가 센 재고)만으로 `IsSlotReady` 가 켜져야 한다. 사냥존에선 묻지 않는다.
// 역검증: `-ini:Game:[/Script/Snarehaven.RanchSyncComponent]:BaseStatePollSeconds=0` 으로 돌리면 실패해야 한다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.RanchSync.Poll; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchGameMode.h"
#include "Ranch/RanchSyncComponent.h"
#include "SnarehavenCharacter.h"
#include "SnarehavenGameMode.h"
#include "UI/RanchHudWidget.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	const FName TestAnimal = TEXT("animal.pig");
	constexpr int32 TestSlot = 0;
	constexpr double TestProduceSeconds = 600.0; // 가짜 서버가 재고 1개를 만드는 데 걸리는 시간
	const FDateTime TestPlacedUtc(2026, 10, 2, 12, 0, 0);

	using FDone = USnareBackendSubsystem::FServerCallDone;

	/** `GetBaseState` 만 받는 가짜 서버 — 재고는 서버 시계로만 센다(진짜 CloudScript 와 같은 자리). */
	struct FFakeClockServer
	{
		int32 SlotCount = 0;
		FDateTime ServerNow = TestPlacedUtc;
		TArray<FDone> Waiting;
		int32 BaseStateCalls = 0;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>&, FDone OnDone)
				{
					if (Name == URanchSyncComponent::GetBaseStateFunctionName())
					{
						++BaseStateCalls;
						Waiting.Add(MoveTemp(OnDone));
					}
				};
		}

		~FFakeClockServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		void AnswerAll()
		{
			const int32 Stock = static_cast<int32>((ServerNow - TestPlacedUtc).GetTotalSeconds() / TestProduceSeconds);
			TArray<TSharedPtr<FJsonValue>> Ranch;
			for (int32 Index = 0; Index < SlotCount; ++Index)
			{
				if (Index != TestSlot)
				{
					Ranch.Add(MakeShared<FJsonValueNull>());
					continue;
				}
				const TSharedPtr<FJsonObject> Slot = MakeShared<FJsonObject>();
				Slot->SetStringField(TEXT("animalId"), TestAnimal.ToString());
				Slot->SetStringField(TEXT("occupiedSinceUtc"), TestPlacedUtc.ToIso8601());
				Slot->SetNumberField(TEXT("stock"), Stock);
				Ranch.Add(MakeShared<FJsonValueObject>(Slot));
			}
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetArrayField(TEXT("ranch"), Ranch);

			TArray<FDone> Done = MoveTemp(Waiting);
			for (FDone& Callback : Done)
			{
				Callback(true, Response, FString());
			}
		}
	};

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchPollTest, "Snarehaven.RanchSync.Poll.ReadyWithoutReentry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchPollTest::RunTest(const FString& Parameters)
{
	const float PollSeconds = URanchSyncComponent::GetBaseStatePollSeconds();
	AddInfo(FString::Printf(TEXT("[RanchPoll] BaseStatePollSeconds=%.1f"), PollSeconds));
	if (!TestTrue(TEXT("주기 조회가 켜져 있다 (DefaultGame.ini BaseStatePollSeconds > 0)"), PollSeconds > 0.f))
	{
		return false;
	}

	UWorld* const World = CreateTestWorld();
	AGameStateBase* const GameState = World->SpawnActor<AGameStateBase>();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>();
	URanchHudWidget* const Hud = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
	URanchSyncComponent* const Sync = Player != nullptr ? Player->FindComponentByClass<URanchSyncComponent>() : nullptr;
	URanchComponent* const Ranch = URanchComponent::FindStation(Player, ERanchStationKind::Ranch);
	if (!TestNotNull(TEXT("게임 상태"), GameState) || !TestNotNull(TEXT("플레이어"), Player)
		|| !TestNotNull(TEXT("목장 HUD"), Hud) || !TestNotNull(TEXT("목장 전송기"), Sync) || !TestNotNull(TEXT("목장"), Ranch))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 목장 존 = 게임 상태가 복제해 주는 게임모드 클래스 (`USnareBaseBuildComponent::GetTentZone`).
	GameState->GameModeClass = ARanchGameMode::StaticClass();
	World->SetGameState(GameState);
	Hud->BindToCharacter(Player);

	FFakeClockServer Server;
	Server.SlotCount = Ranch->GetSlotCount();
	Server.Install();

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
	Sync->BindTargets(Ranch, nullptr, Backend, nullptr, nullptr);

	// 존에 들어옴: 로그인 스냅샷이 오면 한 번 묻는다. 막 넣은 칸이라 재고 0.
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Backend->ApplyPlayerSnapshot(Snapshot, true);
	TestEqual(TEXT("존에 들어오면 GetBaseState 한 번"), Server.BaseStateCalls, 1);
	Server.AnswerAll();
	TestFalse(TEXT("막 넣은 칸은 Ready 가 아니다"), Hud->IsSlotReady(TestSlot));

	FTSTicker Ticker;
	Sync->StartBaseStatePolling(Ticker);

	// 서버 시계만 생산 한 번 넘게 앞당긴다 — 클라이언트는 시간을 세지 않으니 다시 묻기 전엔 그대로다.
	Server.ServerNow += FTimespan::FromSeconds(TestProduceSeconds + 1.0);
	Ticker.Tick(PollSeconds - 1.f);
	TestEqual(TEXT("주기 전엔 다시 안 묻는다"), Server.BaseStateCalls, 1);
	TestFalse(TEXT("다시 묻기 전엔 Ready 가 아니다(클라이언트가 시간을 안 센다)"), Hud->IsSlotReady(TestSlot));

	Ticker.Tick(2.f);
	TestEqual(TEXT("주기를 넘기면 존 재진입 없이 GetBaseState 를 다시 묻는다"), Server.BaseStateCalls, 2);
	TestFalse(TEXT("답이 오기 전엔 아직 Ready 가 아니다"), Hud->IsSlotReady(TestSlot));
	Server.AnswerAll();
	TestTrue(TEXT("서버가 센 재고가 오면 Ready 가 켜진다"), Hud->IsSlotReady(TestSlot));

	// 사냥존에선 묻지 않는다 — 목장 존에 있는 동안만.
	GameState->GameModeClass = ASnarehavenGameMode::StaticClass();
	Ticker.Tick(PollSeconds + 1.f);
	TestEqual(TEXT("사냥존에선 다시 안 묻는다"), Server.BaseStateCalls, 2);

	AddInfo(FString::Printf(TEXT("[RanchPoll] GetBaseState %d번 · Ready=%d"), Server.BaseStateCalls,
		Hud->IsSlotReady(TestSlot) ? 1 : 0));
	Sync->StopBaseStatePolling();
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
