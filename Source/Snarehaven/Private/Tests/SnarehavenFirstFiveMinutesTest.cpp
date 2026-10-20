// ROADMAP M18-08 2층 검사 — 새 계정의 처음 5분: 포획 → 배낭에 펫 → 목장 칸에 풀기 → (첫 배치 기다림) → 수확 이
// 사람 손 없이 한 줄로 이어지는가. 단계 사이마다 첫 실행 안내(M17-01)가 **다음 할 일**을 가리키는지도 본다.
// 서버는 가짜(`USnareBackendSubsystem::TestServerOverride`)지만 CloudScript 와 같은 판정(펫 1개 빼기·첫 배치
// `FIRST_BATCH_MINUTES` 뒤 첫 단위·그 뒤 주기마다)을 흉내 내고, 보유 수량은 **응답 뒤 다시 받은 스냅샷으로만** 바뀐다 (CLAUDE.md 8).
// M21-01b: 시계는 **앞당기지 않는다** — 행동마다 걸리는 초만큼만 흐르고, 수확 준비는 목장 주기 조회(`BaseStatePollSeconds`)가
// 스스로 알아내야 한다. 처음부터 수확까지 300초 안이어야 통과. 클라이언트 코드는 진짜 길 그대로다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.FirstFiveMinutes; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animals/AnimalCharacter.h"
#include "Animals/AnimalStateComponent.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/CaptureComponent.h"
#include "Combat/MeleeAttackComponent.h"
#include "Containers/Ticker.h"
#include "Components/TextBlock.h"
#include "Data/AnimalRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Inventory/InventoryComponent.h"
#include "Inventory/LootClaimComponent.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchGameMode.h"
#include "Ranch/RanchInteractComponent.h"
#include "Ranch/RanchSlotActor.h"
#include "Ranch/RanchSyncComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/SnareFirstRunGuideWidget.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestRangeCm = 300.f;
	constexpr float TestIntervalSeconds = 1.f;
	constexpr float TestMaxHP = 20.f;
	constexpr float TestCaptureThresholdPct = 50.f;
	constexpr float TestAnimalDistanceCm = 100.f;
	constexpr float TestSlotDistanceCm = -100.f;
	constexpr int32 TestSlotIndex = 0;
	constexpr int32 TestCycleMinutes = 30;     // 가짜 서버의 생산 주기(첫 배치 뒤)
	// 사람이 행동마다 쓰는 초 — 시험 값(설계 숫자 아님). 시작점 15m 안 동물(M21-01b)·거점에서 목장 존 이동을 넉넉히 잡았다.
	constexpr double TestWalkToAnimalSeconds = 15.0;
	constexpr double TestCaptureSeconds = 3.0;
	constexpr double TestOpenBackpackSeconds = 5.0;
	constexpr double TestTravelToRanchSeconds = 60.0;
	constexpr double TestPlaceSeconds = 5.0;
	constexpr double TestHarvestSeconds = 3.0;
	constexpr double FirstFiveBudgetSeconds = 300.0;   // ROADMAP M21-01 합격선 — 처음 5분
	const TCHAR* CloudScriptPath = TEXT("tools/playfab/cloudscript_main.js");
	const FName TestProductID = TEXT("item.truffle");
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* PigRowName = TEXT("Animal_Pig");

	using FDone = USnareBackendSubsystem::FServerCallDone;

	/** CloudScript 의 ClaimLoot·RegisterPet·GetBaseState·HarvestRanch 를 메모리로 흉내 내는 가짜 서버. 응답은 `Pump` 때만 간다. */
	struct FFakeServer
	{
		struct FCall { FString Name; TSharedPtr<FJsonObject> Args; FDone Done; };

		USnareBackendSubsystem* Backend = nullptr;
		UInventoryComponent* Inventory = nullptr;
		TMap<FName, int32> Items;
		TArray<FName> SlotAnimal;
		TArray<FDateTime> SlotSince;
		TArray<bool> SlotFirstBatch;
		int32 FirstBatchMinutes = TestCycleMinutes;
		bool bFirstBatchUsed = false;
		FDateTime Now = FDateTime(2026, 10, 2, 9, 0, 0);
		TArray<FCall> Waiting;
		TArray<FString> Log;

		void Install(int32 SlotCount)
		{
			SlotAnimal.SetNum(SlotCount);
			SlotSince.SetNum(SlotCount);
			SlotFirstBatch.SetNum(SlotCount);
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone Done)
				{
					Waiting.Add({ Name, Args, MoveTemp(Done) });
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		int32 FirstUnitMinutes(int32 Slot) const { return SlotFirstBatch[Slot] ? FirstBatchMinutes : TestCycleMinutes; }

		/** cloudscript_main.js `animalHarvestUnits` 와 같은 식 — 첫 단위 분 + 그 뒤 주기마다 하나. */
		int32 Units(int32 Slot) const
		{
			const double Minutes = (Now - SlotSince[Slot]).GetTotalMinutes();
			const int32 First = FirstUnitMinutes(Slot);
			return Minutes < First ? 0 : 1 + FMath::FloorToInt((Minutes - First) / TestCycleMinutes);
		}

		/** 쓰기 성공 뒤 클라이언트가 `FetchPlayerState` 로 받아 올 값 — 로그인 없는 시험이라 여기서 대신 내려준다. */
		void PushSnapshot()
		{
			FSnarePlayerSnapshot Snapshot;
			Snapshot.PlayFabId = TEXT("TEST_FIRST_FIVE");
			for (const TPair<FName, int32>& Pair : Items)
			{
				if (Pair.Value > 0)
				{
					FInventorySlot Slot;
					Slot.ItemID = Pair.Key;
					Slot.Count = Pair.Value;
					Snapshot.InventorySlots.Add(Slot);
				}
			}
			Backend->ApplyPlayerSnapshot(Snapshot, true);
			Inventory->ApplyBackendSnapshot(Snapshot.InventorySlots);
		}

		TSharedPtr<FJsonObject> Handle(const FCall& Call, bool& bWrote)
		{
			const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
			bWrote = false;
			if (Call.Name == TEXT("ClaimLoot"))
			{
				for (const TSharedPtr<FJsonValue>& Value : Call.Args->GetArrayField(TEXT("items")))
				{
					const TSharedPtr<FJsonObject> Line = Value->AsObject();
					Items.FindOrAdd(FName(*Line->GetStringField(TEXT("itemId")))) += static_cast<int32>(Line->GetNumberField(TEXT("count")));
				}
				bWrote = true;
			}
			else if (Call.Name == TEXT("RegisterPet"))
			{
				const int32 Slot = static_cast<int32>(Call.Args->GetNumberField(TEXT("slot")));
				const FString AnimalID = Call.Args->GetStringField(TEXT("animalId"));
				const FName PetID(*(TEXT("item.pet_") + AnimalID.RightChop(7)));   // animal.<종> ↔ item.pet_<종>
				if (!SlotAnimal.IsValidIndex(Slot) || !SlotAnimal[Slot].IsNone() || Items.FindRef(PetID) < 1)
				{
					Reply->SetStringField(TEXT("error"), TEXT("가짜 서버 거절"));
					return Reply;
				}
				Items[PetID] -= 1;
				SlotAnimal[Slot] = FName(*AnimalID);
				SlotSince[Slot] = Now;
				SlotFirstBatch[Slot] = !bFirstBatchUsed;   // 계정 첫 펫 한 번만 (RegisterPet `firstBatch`)
				bFirstBatchUsed = true;
				Reply->SetBoolField(TEXT("success"), true);
				bWrote = true;
			}
			else if (Call.Name == TEXT("HarvestRanch"))
			{
				const int32 Slot = static_cast<int32>(Call.Args->GetNumberField(TEXT("slot")));
				const int32 Got = SlotAnimal.IsValidIndex(Slot) && !SlotAnimal[Slot].IsNone() ? Units(Slot) : 0;
				if (Got > 0)
				{
					Items.FindOrAdd(TestProductID) += Got;
					SlotSince[Slot] += FTimespan::FromMinutes(FirstUnitMinutes(Slot) + (Got - 1) * TestCycleMinutes);
					SlotFirstBatch[Slot] = false;
					bWrote = true;
				}
				Reply->SetNumberField(TEXT("harvested"), Got);
			}
			else if (Call.Name == TEXT("GetBaseState"))
			{
				// cloudscript_main.js GetBaseState 와 같은 모양 — 동물 칸 stock 은 지난 시간으로 채운다(M18-08 서버 수정).
				TArray<TSharedPtr<FJsonValue>> Ranch;
				for (int32 Slot = 0; Slot < SlotAnimal.Num(); ++Slot)
				{
					if (SlotAnimal[Slot].IsNone())
					{
						Ranch.Add(MakeShared<FJsonValueNull>());
						continue;
					}
					const TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
					Entry->SetNumberField(TEXT("slot"), Slot);
					Entry->SetStringField(TEXT("animalId"), SlotAnimal[Slot].ToString());
					Entry->SetStringField(TEXT("occupiedSinceUtc"), SlotSince[Slot].ToIso8601());
					Entry->SetNumberField(TEXT("stock"), Units(Slot));
					Ranch.Add(MakeShared<FJsonValueObject>(Entry));
				}
				Reply->SetArrayField(TEXT("ranch"), Ranch);
			}
			else
			{
				Reply->SetStringField(TEXT("error"), TEXT("가짜 서버에 없는 함수"));
			}
			return Reply;
		}

		/** 기다리는 요청에 차례로 답한다. 답이 새 요청을 부르면(다시 받기·GetBaseState) 그것까지. */
		void Pump()
		{
			for (int32 Guard = 0; Guard < 32 && !Waiting.IsEmpty(); ++Guard)
			{
				FCall Call = MoveTemp(Waiting[0]);
				Waiting.RemoveAt(0);
				Log.Add(Call.Name);
				bool bWrote = false;
				const TSharedPtr<FJsonObject> Reply = Handle(Call, bWrote);
				const bool bOk = !Reply->HasField(TEXT("error"));
				Call.Done(bOk, Reply, FString());
				if (bWrote)
				{
					PushSnapshot();
				}
			}
		}
	};

	/** 진짜 서버의 첫 배치 분 — 가짜 서버가 숫자를 따로 갖지 않게 CloudScript 원본에서 읽는다. 못 읽으면 -1. */
	int32 ReadServerFirstBatchMinutes()
	{
		FString Script;
		if (!FFileHelper::LoadFileToString(Script, *FPaths::Combine(FPaths::ProjectDir(), CloudScriptPath)))
		{
			return -1;
		}
		FRegexMatcher Matcher(FRegexPattern(TEXT("var FIRST_BATCH_MINUTES = (\\d+);")), Script);
		return Matcher.FindNext() ? FCString::Atoi(*Matcher.GetCaptureGroup(1)) : -1;
	}

	FString GuideLine(USnareFirstRunGuideWidget& Guide)
	{
		const UTextBlock* const Text = Cast<UTextBlock>(Guide.WidgetTree->FindWidget(TEXT("GuideStepText")));
		return Text != nullptr ? Text->GetText().ToString() : FString();
	}

	/** 안내가 손 넘김 없이 `Expected` 장에 와 있고 그 글에 `Keyword`(다음에 누를 것)가 있는지 (M19-08). */
	bool GuideAt(FAutomationTestBase& Test, USnareFirstRunGuideWidget& Guide, int32 Expected, const TCHAR* Keyword)
	{
		const FString Line = GuideLine(Guide);
		UE_LOG(LogTemp, Display, TEXT("[FirstFive] 안내 %d/%d장: %s"), Guide.GetStepIndex() + 1,
			USnareFirstRunGuideWidget::GetSteps().Num(), *Line);
		return Test.TestEqual(TEXT("안내 장 번호"), Guide.GetStepIndex(), Expected)
			&& Test.TestTrue(FString::Printf(TEXT("안내가 '%s' 를 가리킨다"), Keyword), Line.Contains(Keyword));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFirstFiveMinutesTest, "Snarehaven.FirstFiveMinutes.CaptureToHarvest.NoHands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenFirstFiveMinutesTest::RunTest(const FString& Parameters)
{
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	const FAnimalRow* const PigRow = AnimalTable != nullptr ? AnimalTable->FindRow<FAnimalRow>(PigRowName, TEXT("M18-08")) : nullptr;
	if (!TestNotNull(TEXT("DT_AnimalTable 돼지 줄"), PigRow) || !TestFalse(TEXT("돼지 펫 아이템"), PigRow->PetItemID.IsNone()))
	{
		return false;
	}
	const FName PetID = PigRow->PetItemID;

	const int32 ServerFirstBatchMinutes = ReadServerFirstBatchMinutes();
	const float PollSeconds = URanchSyncComponent::GetBaseStatePollSeconds();
	if (!TestTrue(TEXT("CloudScript FIRST_BATCH_MINUTES 를 읽었다"), ServerFirstBatchMinutes > 0)
		|| !TestTrue(TEXT("목장 주기 조회가 켜져 있다 (DefaultGame.ini BaseStatePollSeconds > 0)"), PollSeconds > 0.f))
	{
		return false;
	}

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	// 주기 조회는 목장 존에서만 묻는다(`PollBaseState`) — 이 시험의 칸 배치·수확은 목장 존에서 일어난다.
	AGameStateBase* const GameState = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("게임 상태"), GameState))
	{
		return false;
	}
	GameState->GameModeClass = ARanchGameMode::StaticClass();
	World->SetGameState(GameState);

	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	AAnimalCharacter* const Animal = World->SpawnActor<AAnimalCharacter>(FVector(TestAnimalDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("동물"), Animal))
	{
		return false;
	}
	Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestRangeCm, TestIntervalSeconds);
	FAnimalRow Row = *PigRow;          // 종·펫 아이템은 실제 표, 체력·임계값만 시험 값
	Row.MaxHP = TestMaxHP;
	Row.CaptureThresholdPct = TestCaptureThresholdPct;
	Animal->GetAnimalState()->InitializeFromRow(Row);

	UInventoryComponent* const Inventory = Player->GetInventory();
	URanchComponent* const Ranch = Player->GetRanch();
	URanchInteractComponent* const Interact = Player->GetRanchInteract();
	ULootClaimComponent* const Claim = Player->FindComponentByClass<ULootClaimComponent>();
	URanchSyncComponent* const Sync = Player->FindComponentByClass<URanchSyncComponent>();
	if (!TestNotNull(TEXT("인벤토리"), Inventory) || !TestNotNull(TEXT("목장"), Ranch) || !TestNotNull(TEXT("목장 상호작용"), Interact)
		|| !TestNotNull(TEXT("지급 요청기"), Claim) || !TestNotNull(TEXT("목장 전송기"), Sync))
	{
		return false;
	}

	// 새 계정 — 가짜 서버는 빈 가방·빈 목장에서 시작한다. BeginPlay 가 없어 묶기만 손으로 한다.
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
	FFakeServer Server;
	Server.Backend = Backend;
	Server.Inventory = Inventory;
	Server.FirstBatchMinutes = ServerFirstBatchMinutes;
	Server.Install(Ranch->GetSlotCount());
	Claim->BindTargets(Inventory, Backend);
	Sync->BindTargets(Ranch, nullptr, Backend, nullptr, nullptr);

	// 시계 하나 — 서버 시각과 클라이언트 주기 조회가 같은 초만큼 같이 흐른다. 건너뛰기 없음.
	FTSTicker Ticker;
	Sync->StartBaseStatePolling(Ticker);
	ON_SCOPE_EXIT { Sync->StopBaseStatePolling(); };
	double Elapsed = 0.0;
	auto Spend = [&](double Seconds)
	{
		for (double Left = Seconds; Left > 0.0; Left -= 1.0)
		{
			const double Step = FMath::Min(1.0, Left);
			Elapsed += Step;
			Server.Now += FTimespan::FromSeconds(Step);
			Ticker.Tick(static_cast<float>(Step));
			Server.Pump();
		}
	};

	USnareFirstRunGuideWidget* const Guide = CreateWidget<USnareFirstRunGuideWidget>(World, USnareFirstRunGuideWidget::StaticClass());
	if (!TestNotNull(TEXT("첫 실행 안내"), Guide))
	{
		return false;
	}
	Guide->bPersistSeen = false;   // 사용자 저장을 건드리지 않는다
	Guide->TakeWidget();
	Guide->BindToCharacter(Player);
	TestTrue(TEXT("안내 1장 = 걷기"), GuideLine(*Guide).Contains(TEXT("조이스틱")));
	Guide->Advance();   // 걷기 장만 손으로 — 아래 네 행동(공격·포획·칸 배치·수확)은 손 넘김 없다 (M19-08)
	GuideAt(*this, *Guide, 1, TEXT("마른 가지"));   // 채집 3장(INBOX 18:10 ③) — 사냥부터 하면 공격이 그 장들을 건너뛴다

	// ① 공격 → 포획. 펫은 요청 대기열에만 선다.
	Spend(TestWalkToAnimalSeconds);
	bool bCapturable = false;
	for (int32 Hit = 0; Hit < 16 && !bCapturable; ++Hit)
	{
		Spend(TestIntervalSeconds);
		Player->GetMeleeAttack()->AttackOnce();
		bCapturable = Animal->GetAnimalState()->GetState() == EAnimalState::Capturable;
	}
	if (!TestTrue(TEXT("① 때려서 포획 창이 열린다"), bCapturable))
	{
		return false;
	}
	GuideAt(*this, *Guide, 5, TEXT("포획 버튼"));
	Spend(TestCaptureSeconds);
	TestTrue(TEXT("① 포획 성공"), Player->GetCapture()->TryCaptureInWindowForTest());
	TestEqual(TEXT("① 펫은 지급 요청 대기열에"), Inventory->GetPendingCount(PetID), 1);
	TestEqual(TEXT("① 응답 전 보유 0"), Inventory->GetItemCount(PetID), 0);

	// ② 배낭에 펫 — ClaimLoot 응답 뒤 다시 받은 스냅샷으로만 보유가 는다.
	Spend(TestOpenBackpackSeconds);
	TestTrue(TEXT("② ClaimLoot 를 보낸다"), Claim->TryClaimNow());
	TestEqual(TEXT("② 응답 전 보유 그대로 0"), Inventory->GetItemCount(PetID), 0);
	Server.Pump();
	TestEqual(TEXT("② 펫 1마리가 가방에"), Inventory->GetItemCount(PetID), 1);
	TestEqual(TEXT("② 대기열은 비었다"), Inventory->GetPendingCount(PetID), 0);
	GuideAt(*this, *Guide, 6, TEXT("목장 칸"));

	// ③ 목장 칸에 풀기 — 칸도 서버가 받은 뒤 GetBaseState 로만 찬다.
	Spend(TestTravelToRanchSeconds + TestPlaceSeconds);
	ARanchSlotActor* const SlotActor = World->SpawnActor<ARanchSlotActor>(FVector(TestSlotDistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("③ 목장 칸"), SlotActor))
	{
		return false;
	}
	if (FIntProperty* const IndexProperty = CastField<FIntProperty>(ARanchSlotActor::StaticClass()->FindPropertyByName(TEXT("SlotIndex"))))
	{
		IndexProperty->SetPropertyValue_InContainer(SlotActor, TestSlotIndex);
	}
	TestEqual(TEXT("③ 가까운 칸을 고른다"), Interact->RefreshTarget(), SlotActor);
	TestEqual(TEXT("③ 넣을 동물 = 잡은 종"), Interact->GetSelectedPlacementID(), PigRow->AnimalID);
	TestTrue(TEXT("③ 빈 칸을 누르면 배치 요청"), Interact->TryInteractNearest());
	TestTrue(TEXT("③ 응답 전 칸은 빈 그대로"), Ranch->IsSlotEmpty(TestSlotIndex));
	Server.Pump();
	TestFalse(TEXT("③ 서버가 받은 뒤 칸이 찬다"), Ranch->IsSlotEmpty(TestSlotIndex));
	TestEqual(TEXT("③ 펫 아이템은 서버가 뺐다"), Inventory->GetItemCount(PetID), 0);
	const FRanchSlotState* const Slot = Ranch->GetSlot(TestSlotIndex);
	TestEqual(TEXT("③ 넣자마자 재고 0"), Slot != nullptr ? Slot->Stock : -1, 0);
	GuideAt(*this, *Guide, 7, TEXT("수확 버튼"));

	const double PlacedAt = Elapsed;

	// ④ 첫 배치 기다림 — 손으로 다시 묻지 않는다. 주기 조회가 서버 재고를 받아 와야 수확 준비가 보인다.
	while (Slot != nullptr && Slot->Stock < 1 && Elapsed < FirstFiveBudgetSeconds)
	{
		Spend(1.0);
	}
	UE_LOG(LogTemp, Display, TEXT("[FirstFive] 칸에 넣고 %.0f초 기다림 · 재고 %d (첫 배치 %d분 · 조회 %.0f초) · 처음부터 %.0f초"),
		Elapsed - PlacedAt, Slot != nullptr ? Slot->Stock : -1, ServerFirstBatchMinutes, PollSeconds, Elapsed);
	TestEqual(TEXT("④ 첫 배치 1단위가 보인다(수확 준비)"), Slot != nullptr ? Slot->Stock : -1, 1);

	// ⑤ 수확 — 생산물은 HarvestRanch 응답 뒤 스냅샷으로만 들어온다.
	Spend(TestHarvestSeconds);
	TestTrue(TEXT("⑤ 찬 칸을 누르면 수확 요청"), Interact->TryInteractNearest());
	TestEqual(TEXT("⑤ 응답 전 생산물 0"), Inventory->GetItemCount(TestProductID), 0);
	Server.Pump();
	TestEqual(TEXT("⑤ 생산물이 가방에"), Inventory->GetItemCount(TestProductID), 1);
	TestEqual(TEXT("⑤ 거둔 뒤 재고 0"), Slot != nullptr ? Slot->Stock : -1, 0);
	UE_LOG(LogTemp, Display, TEXT("[FirstFive] 수확 뒤 안내 %d/%d장 (끝=%s)"), Guide->GetStepIndex() + 1,
		USnareFirstRunGuideWidget::GetSteps().Num(), Guide->IsFinished() ? TEXT("예") : TEXT("아니오"));
	// 수확 뒤는 집·제작 3장(M28-07) — 텐트는 코인을 모아야 해서 첫 5분 밖이다.
	GuideAt(*this, *Guide, 8, TEXT("텐트"));
	UE_LOG(LogTemp, Display, TEXT("[FirstFive] 처음부터 수확까지 %.0f초 (상한 %.0f초)"), Elapsed, FirstFiveBudgetSeconds);
	TestTrue(TEXT("처음부터 수확까지 300초 안 (시간 앞당김 없음)"), Elapsed <= FirstFiveBudgetSeconds);

	// 첫 배치는 한 번뿐 — 거둔 칸의 다음 단위는 다시 주기 하나를 기다린다.
	TestEqual(TEXT("⑥ 거둔 칸은 첫 배치 표시가 지워졌다"), Server.SlotFirstBatch[TestSlotIndex], false);

	AddInfo(FString::Printf(TEXT("[FirstFive] 서버 호출 %d번: %s"), Server.Log.Num(), *FString::Join(Server.Log, TEXT(","))));
	return true;
}

#endif
