// ⑩ 씨앗 소모 2층 검사 — 작물 칸에 심기(`PlantCrop`)·거두기(`HarvestCrop`)가 서버 권위대로 가는가.
// ① 가방 캐시에 씨앗이 없으면 심기 요청을 쌓지 않는다. 씨앗이 있으면 요청만 쌓이고 칸·가방은 그대로다.
// ② 전송기는 `PlantCrop(slot, seedItemId)` 를 보내고, 거절이면 칸 그대로, 라이브에 함수가 없으면 "서버 준비 중" 으로 꺼진다.
// ③ `GetBaseState` 의 `cropItemId` 칸은 작물 칸으로 서고, 그 칸의 수확은 `HarvestCrop` 으로 간다.
// ④ 거두면(원본대로) 칸은 다시 받은 값으로 빈다. ⑤ 목장 HUD 는 작물 칸에 상한 대신 자람 상태를 적는다.
// ⑥ 심기·거두기 실패는 작물 전용 문구 행(`PlantCrop_*`·`HarvestCrop_empty_slot`)으로 뜬다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Ranch.Crop; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Building/SnareBaseSubsystem.h"
#include "Building/SnareBuildRules.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/TextBlock.h"
#include "Data/AnimalRow.h"
#include "Data/CropProductRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Inventory/InventoryComponent.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchInteractComponent.h"
#include "Ranch/RanchSlotActor.h"
#include "Ranch/RanchSyncComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SnarehavenCharacter.h"
#include "UI/RanchHudWidget.h"
#include "World/WaterTravelComponent.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestAttackDamage = 10.f;
	constexpr float TestRangeCm = 300.f;
	constexpr float TestIntervalSeconds = 1.f;
	constexpr float TestNearDistanceCm = 100.f;
	constexpr int32 TestSlot = 0;
	constexpr int32 TestFilledSlot = 1;
	const FName TestAnimal = TEXT("animal.pig");
	const FName TestSeed = TEXT("item.seed_carrot");
	const FName TestCrop = TEXT("item.carrot");

	const TCHAR* CropTablePath = TEXT("/Game/Game/Data/Ranch/DT_CropProductTable.DT_CropProductTable");
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");

	using FDone = USnareBackendSubsystem::FServerCallDone;

	/** 가짜 서버 — `SnarehavenRanchSyncTest` 와 같은 짜임에 오류 문장까지 고를 수 있게 했다(`CloudScriptNotFound`). */
	struct FFakeServer
	{
		TArray<FString> Functions;
		TArray<TSharedPtr<FJsonObject>> Args;
		TArray<FDone> Waiting;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& InArgs, FDone OnDone)
				{
					Functions.Add(Name);
					Args.Add(InArgs);
					Waiting.Add(MoveTemp(OnDone));
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		int32 CountCalls(const FString& Name) const
		{
			return Functions.FilterByPredicate([&Name](const FString& F) { return F == Name; }).Num();
		}

		void Answer(bool bOk, const TSharedPtr<FJsonObject>& Response, const FString& Error = FString())
		{
			if (Waiting.IsEmpty())
			{
				return;
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			Done(bOk, Response, Error);
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

	// 칸 번호는 커맨드릿이 `set_editor_property` 로 넣는 값이라 런타임 setter 가 없다 — 같은 길을 쓴다.
	ARanchSlotActor* SpawnTestSlot(UWorld* World, float DistanceCm, int32 SlotIndex)
	{
		ARanchSlotActor* Slot = World->SpawnActor<ARanchSlotActor>(FVector(DistanceCm, 0.f, 0.f), FRotator::ZeroRotator);
		if (Slot == nullptr)
		{
			return nullptr;
		}
		if (FIntProperty* IndexProperty =
			CastField<FIntProperty>(ARanchSlotActor::StaticClass()->FindPropertyByName(TEXT("SlotIndex"))))
		{
			IndexProperty->SetPropertyValue_InContainer(Slot, SlotIndex);
		}
		return Slot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCropPlantInteractTest, "Snarehaven.Ranch.Crop.Plant.RefusesWithoutSeedAndKeepsSlot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCropPlantInteractTest::RunTest(const FString& Parameters)
{
	// 씨앗 ID 는 작물 표 칸에서 읽는다 — 기대값을 코드에 적지 않는다(`SeedItemID`).
	const UDataTable* const CropTable = LoadObject<UDataTable>(nullptr, CropTablePath);
	if (!TestNotNull(TEXT("DT_CropProductTable 로드"), CropTable) || CropTable->GetRowMap().IsEmpty())
	{
		return false;
	}
	const FCropProductRow* const FirstCrop = reinterpret_cast<const FCropProductRow*>(CropTable->GetRowMap().CreateConstIterator().Value());
	const FName SeedItemID = FirstCrop->SeedItemID;

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	ARanchSlotActor* Slot = SpawnTestSlot(World, TestNearDistanceCm, TestSlot);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("슬롯 스폰"), Slot))
	{
		DestroyTestWorld(World);
		return false;
	}
	Player->GetMeleeAttack()->SetAttackSettings(TestAttackDamage, TestRangeCm, TestIntervalSeconds);
	URanchInteractComponent* const Interact = Player->GetRanchInteract();
	URanchComponent* const Ranch = Player->GetRanch();
	UInventoryComponent* const Inventory = Player->GetInventory();

	// ① 씨앗(과 펫)이 없으면 심기 요청을 쌓지 않는다 — 씨앗 없이 심기 거부.
	TestTrue(TEXT("가방이 비면 씨앗 목록도 비어 있다"), Interact->GetOwnedSeedItemIDs().IsEmpty());
	TestFalse(TEXT("씨앗이 없으면 빈 칸을 눌러도 요청이 없다"), Interact->TryInteractNearest());
	TestEqual(TEXT("씨앗이 없으면 대기 요청 0건"), Ranch->GetPendingRequests().Num(), 0);

	// 백엔드가 "씨앗 1개를 가졌다" 고 보내 준 셈 (캐시를 바꾸는 유일한 길).
	FInventorySlot SeedSlot;
	SeedSlot.ItemID = SeedItemID;
	SeedSlot.Count = 1;
	TestTrue(TEXT("씨앗을 가진 캐시를 백엔드가 보내 준다"), Inventory->ApplyBackendSnapshot({ SeedSlot }));
	TestTrue(TEXT("가진 씨앗이 목록에 있다"), Interact->GetOwnedSeedItemIDs().Contains(SeedItemID));
	TestEqual(TEXT("펫이 없으면 첫 선택지가 씨앗"), Interact->GetSelectedPlacementID(), SeedItemID);
	TestFalse(TEXT("씨앗 이름은 아이템 표에서 온다"), Interact->GetPlacementDisplayName(SeedItemID).IsEmpty());

	TestTrue(TEXT("씨앗을 고르고 빈 칸을 누르면 심기 요청이 쌓인다"), Interact->TryInteractNearest());
	if (!TestEqual(TEXT("대기 중인 요청 1건"), Ranch->GetPendingRequests().Num(), 1))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestTrue(TEXT("요청 종류 = PlantCrop"), Ranch->GetPendingRequests()[0].Kind == ERanchRequestKind::PlantCrop);
	TestEqual(TEXT("요청에 실린 씨앗"), Ranch->GetPendingRequests()[0].SeedItemID, SeedItemID);

	// 응답 전: 칸도 가방도 그대로 (CLAUDE.md 8).
	TestTrue(TEXT("응답 전 칸은 비어 있다"), Ranch->IsSlotEmpty(TestSlot));
	TestEqual(TEXT("응답 전 씨앗은 그대로 1개"), Inventory->GetItemCount(SeedItemID), 1);
	TestFalse(TEXT("같은 칸에 또 심지 않는다(대기 중)"), Ranch->QueuePlantCrop(TestSlot, SeedItemID));

	// 찬 칸(펫이 선 칸)에는 심지 않는다.
	TArray<FRanchSlotState> Snapshot;
	Snapshot.SetNum(Ranch->GetSlotCount());
	Snapshot[TestFilledSlot].AnimalID = TestAnimal;
	Snapshot[TestFilledSlot].OccupiedSinceUtc = FDateTime::UtcNow();
	TestTrue(TEXT("펫이 선 칸을 백엔드가 보내 준다"), Ranch->ApplyBackendSnapshot(Snapshot));
	TestFalse(TEXT("찬 칸에는 심기 요청을 쌓지 않는다"), Ranch->QueuePlantCrop(TestFilledSlot, SeedItemID));
	TestEqual(TEXT("찬 칸의 펫은 그대로"), Ranch->GetSlot(TestFilledSlot)->AnimalID, TestAnimal);

	AddInfo(FString::Printf(TEXT("[Crop] 심기 - 씨앗 %s · 대기 %d건 · 0번 칸 %s"), *SeedItemID.ToString(),
		Ranch->GetPendingRequests().Num(), Ranch->IsSlotEmpty(TestSlot) ? TEXT("빈 칸") : TEXT("찬 칸")));
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCropPlantSyncTest, "Snarehaven.Ranch.Crop.Sync.PlantWaitsForServerAndNotReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCropPlantSyncTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
	URanchComponent* const Ranch = NewObject<URanchComponent>(GetTransientPackage());
	URanchComponent* const Fishing = NewObject<URanchComponent>(GetTransientPackage());
	Fishing->SetStationKind(ERanchStationKind::Fishing);
	URanchSyncComponent* const Sync = NewObject<URanchSyncComponent>(GetTransientPackage());
	Sync->BindTargets(Ranch, Fishing, Backend, NewObject<USnareBaseSubsystem>(GameInstance),
		NewObject<UWaterTravelComponent>(GetTransientPackage()));

	// 등록 안 한 컴포넌트라 칸 캐시가 없다 — 백엔드가 빈 목장을 보내 준 셈으로 칸 수를 세운다.
	TArray<FRanchSlotState> EmptySlots;
	EmptySlots.SetNum(Ranch->GetSlotCount());
	TestTrue(TEXT("빈 목장 스냅샷"), Ranch->ApplyBackendSnapshot(EmptySlots));

	// ② 심기 요청은 곧바로 `PlantCrop(slot, seedItemId)` 로 간다.
	TestTrue(TEXT("심기 요청이 쌓인다"), Ranch->QueuePlantCrop(TestSlot, TestSeed));
	TestEqual(TEXT("PlantCrop 한 번"), Server.CountCalls(TEXT("PlantCrop")), 1);
	TestEqual(TEXT("인자 seedItemId"), Server.Args.IsEmpty() ? FString() : Server.Args[0]->GetStringField(TEXT("seedItemId")),
		TestSeed.ToString());

	// 거절(씨앗 없음): 칸 그대로, 다시 받지도 않는다.
	const TSharedPtr<FJsonObject> Fail = MakeShared<FJsonObject>();
	Fail->SetStringField(TEXT("error"), TEXT("씨앗이 없다"));
	Fail->SetStringField(TEXT("code"), TEXT("not_enough_items"));
	Server.Answer(true, Fail);
	TestTrue(TEXT("거절 뒤 칸은 비어 있다"), Ranch->IsSlotEmpty(TestSlot));
	TestEqual(TEXT("거절 뒤 다시 받지 않는다"), Sync->GetRefetchCount(), 0);
	TestFalse(TEXT("보통 거절은 '서버 준비 중' 이 아니다"), Sync->IsFunctionUnavailable(TEXT("PlantCrop")));
	TestEqual(TEXT("거절 뒤 심기 소리 0 (M28-03)"), Sync->GetLastPlayedCue(), FName());

	// 라이브에 함수가 없다 — PlayFab 이 `CloudScriptNotFound` 로 답한다.
	TestTrue(TEXT("다시 심기 요청이 쌓인다"), Ranch->QueuePlantCrop(TestSlot, TestSeed));
	Server.Answer(false, nullptr, TEXT("PlantCrop: CloudScriptNotFound: No function named PlantCrop was found"));
	TestTrue(TEXT("PlantCrop 을 '없음' 으로 기억한다"), Sync->IsFunctionUnavailable(TEXT("PlantCrop")));
	TestEqual(TEXT("안내 = 서버 준비 중"), Sync->GetLastMessage().ToString(), FString(TEXT("서버 준비 중")));
	TestTrue(TEXT("없는 함수 뒤에도 칸은 비어 있다"), Ranch->IsSlotEmpty(TestSlot));

	// ③ `GetBaseState` 가 작물 칸을 돌려주면 작물 칸으로 선다.
	TestTrue(TEXT("GetBaseState 를 묻는다"), Sync->RequestBaseState());
	const FString Json = FString::Printf(TEXT(R"({"ranch": [{"slot":%d,"cropItemId":"%s","occupiedSinceUtc":"2026-09-24T01:00:00.000Z","stock":0}, null, null, null, null]})"),
		TestSlot, *TestCrop.ToString());
	TSharedPtr<FJsonObject> BaseState;
	TestTrue(TEXT("응답 JSON 해석"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), BaseState));
	Server.Answer(true, BaseState);
	const FRanchSlotState* const CropSlot = Ranch->GetSlot(TestSlot);
	TestTrue(TEXT("작물 칸 종류 = Crop"), CropSlot != nullptr && CropSlot->Kind == ERanchSlotKind::Crop);
	TestEqual(TEXT("작물 칸의 작물"), CropSlot != nullptr ? CropSlot->CropItemID : NAME_None, TestCrop);

	TestEqual(TEXT("작물 칸 수확 함수 = HarvestCrop"),
		URanchSyncComponent::GetFunctionName(ERanchStationKind::Ranch, ERanchRequestKind::Harvest, ERanchSlotKind::Crop),
		FString(TEXT("HarvestCrop")));
	TestTrue(TEXT("작물 칸 수확 요청이 쌓인다"), Ranch->QueueHarvest(TestSlot));
	TestEqual(TEXT("HarvestCrop 한 번"), Server.CountCalls(TEXT("HarvestCrop")), 1);
	TestEqual(TEXT("HarvestRanch 는 안 부른다"), Server.CountCalls(TEXT("HarvestRanch")), 0);
	TestEqual(TEXT("응답 전 작물 칸 그대로"), CropSlot != nullptr ? CropSlot->CropItemID : NAME_None, TestCrop);

	// ④ 원본대로 한 번 거두면 칸이 빈다 — 비우는 것도 서버다. 성공 응답만으로는 칸을 안 건드리고,
	// 다시 받은 `GetBaseState` 가 그 칸을 null 로 줄 때 빈다 (CLAUDE.md 8).
	const TSharedPtr<FJsonObject> Harvested = MakeShared<FJsonObject>();
	Harvested->SetNumberField(TEXT("harvested"), 1);
	Harvested->SetStringField(TEXT("itemId"), TestCrop.ToString());
	Server.Answer(true, Harvested);
	TestEqual(TEXT("수확 성공 뒤 다시 받는다"), Sync->GetRefetchCount(), 1);
	TestEqual(TEXT("다시 받기 전엔 작물 칸 그대로"), Ranch->GetSlot(TestSlot)->CropItemID, TestCrop);
	TSharedPtr<FJsonObject> Emptied;
	TestTrue(TEXT("빈 거점 응답 해석"), FJsonSerializer::Deserialize(
		TJsonReaderFactory<>::Create(TEXT(R"({"ranch": [null, null, null, null, null]})")), Emptied));
	Server.Answer(true, Emptied);
	TestTrue(TEXT("거둔 칸은 다시 받은 값으로 빈다"), Ranch->IsSlotEmpty(TestSlot));

	// ⑦ M28-03 — 서버가 심기를 받은 뒤에만 설치와 같은 「쿵」 한 번. 답 전에는 아무 소리도 안 고른다.
	TestTrue(TEXT("빈 칸에 다시 심는다"), Ranch->QueuePlantCrop(TestSlot, TestSeed));
	TestNotEqual(TEXT("답 전 심기 소리 0"), Sync->GetLastPlayedCue(), SnareBuildRules::PlaceConfirmCue());
	Server.Answer(true, MakeShared<FJsonObject>());
	TestEqual(TEXT("답 뒤 심기 소리 = 설치 소리"), Sync->GetLastPlayedCue(), SnareBuildRules::PlaceConfirmCue());

	AddInfo(FString::Printf(TEXT("[Crop] 전송 - 호출 %s · 안내 '%s' · 거둔 뒤 0번 칸 %s"),
		*FString::Join(Server.Functions, TEXT(",")), *Sync->GetLastMessage().ToString(),
		Ranch->IsSlotEmpty(TestSlot) ? TEXT("빈 칸") : TEXT("찬 칸")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCropHudTest, "Snarehaven.Ranch.Crop.Hud.ShowsGrowthNotMaxStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCropHudTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	URanchHudWidget* const Hud = Player != nullptr ? CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass()) : nullptr;
	if (!TestNotNull(TEXT("목장 HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->BindToCharacter(Player);
	URanchComponent* const Ranch = Hud->GetWatchedRanch();
	if (!TestNotNull(TEXT("목장 캐시"), Ranch))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ⑤ 백엔드가 "덜 자란 작물 칸(stock 0)" 을 보내 준 셈.
	TArray<FRanchSlotState> Snapshot;
	Snapshot.SetNum(Ranch->GetSlotCount());
	Snapshot[TestSlot].Kind = ERanchSlotKind::Crop;
	Snapshot[TestSlot].CropItemID = TestCrop;
	Snapshot[TestSlot].OccupiedSinceUtc = FDateTime::UtcNow();
	TestTrue(TEXT("덜 자란 작물 칸 스냅샷"), Ranch->ApplyBackendSnapshot(Snapshot));
	const FString Growing = Hud->GetSlotText(TestSlot).ToString();
	TestTrue(TEXT("덜 자란 칸은 '자라는 중'"), Growing.Contains(TEXT("자라는 중")));
	TestFalse(TEXT("작물 칸엔 개수/상한이 없다"), Growing.Contains(TEXT("/")));
	TestFalse(TEXT("덜 자란 칸은 Ready 가 아니다"), Hud->IsSlotReady(TestSlot));
	TestEqual(TEXT("작물 칸 상한은 없다(-1)"), Hud->GetSlotMaxStack(TestSlot), -1);

	// 서버가 "다 자랐다(stock 1)" 고 보내 준 셈.
	Snapshot[TestSlot].Stock = 1;
	TestTrue(TEXT("다 자란 작물 칸 스냅샷"), Ranch->ApplyBackendSnapshot(Snapshot));
	const FString Ripe = Hud->GetSlotText(TestSlot).ToString();
	TestTrue(TEXT("다 자란 칸은 '다 자람'"), Ripe.Contains(TEXT("다 자람")));
	TestTrue(TEXT("다 자란 칸은 Ready"), Hud->IsSlotReady(TestSlot));

	AddInfo(FString::Printf(TEXT("[Crop] HUD - '%s' -> '%s'"), *Growing, *Ripe));
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCropFailureTest, "Snarehaven.Ranch.Crop.Failure.OwnMessagesNotWood",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCropFailureTest::RunTest(const FString& Parameters)
{
	// ⑥ 심기·거두기 실패는 `DT_RanchFailureTable` 의 함수 행 문구로 뜬다 — 씨앗 부족이 "나무가 모자란다" 로 뜨면 안 된다.
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr,
		TEXT("/Game/Game/Data/Ranch/DT_RanchFailureTable.DT_RanchFailureTable"));
	if (!TestNotNull(TEXT("DT_RanchFailureTable"), Table))
	{
		return false;
	}
	auto Describe = [Table](const TCHAR* Function, const TCHAR* Code)
	{
		const TSharedPtr<FJsonObject> Fail = MakeShared<FJsonObject>();
		Fail->SetStringField(TEXT("error"), TEXT("서버 사유 문장"));
		Fail->SetStringField(TEXT("code"), Code);
		return URanchSyncComponent::DescribeRanchFailure(Table, Function, true, Fail, FString()).ToString();
	};
	const FString NoSeed = Describe(TEXT("PlantCrop"), TEXT("not_enough_items"));
	const FString NoWood = Describe(TEXT("FeedRanchUpkeep"), TEXT("not_enough_items"));
	TestNotEqual(TEXT("씨앗 부족 문구 ≠ 나무 부족 문구"), NoSeed, NoWood);
	for (const TCHAR* Code : { TEXT("not_enough_items"), TEXT("slot_full"), TEXT("bad_slot"), TEXT("bad_args") })
	{
		const FString Message = Describe(TEXT("PlantCrop"), Code);
		TestTrue(*FString::Printf(TEXT("PlantCrop %s 는 표 문구 (실제 '%s')"), Code, *Message),
			!Message.IsEmpty() && Message != TEXT("서버 사유 문장"));
	}
	const FString Empty = Describe(TEXT("HarvestCrop"), TEXT("empty_slot"));
	TestTrue(*FString::Printf(TEXT("HarvestCrop empty_slot 은 표 문구 (실제 '%s')"), *Empty),
		!Empty.IsEmpty() && Empty != TEXT("서버 사유 문장"));

	AddInfo(FString::Printf(TEXT("[Crop] 실패 문구 - 씨앗 '%s' · 빈 칸 '%s'"), *NoSeed, *Empty));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlacementTabsTest, "Snarehaven.Ranch.Crop.PlacementTabs.SplitAnimalsAndSeeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlacementTabsTest::RunTest(const FString& Parameters)
{
	// M28-08 — 넣기 판에서 씨앗이 동물 사이에 섞여 넘겨야 나오지 않게 「동물 / 씨앗」 두 탭으로 나눈다.
	// 펫·씨앗 ID 는 표에서 읽는다 — 기대값을 코드에 적지 않는다.
	const UDataTable* const CropTable = LoadObject<UDataTable>(nullptr, CropTablePath);
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("DT_CropProductTable 로드"), CropTable) || !TestNotNull(TEXT("DT_AnimalTable 로드"), AnimalTable))
	{
		return false;
	}
	TArray<FName> TableSeeds;
	for (const TPair<FName, uint8*>& Pair : CropTable->GetRowMap())
	{
		const FCropProductRow* const Row = reinterpret_cast<const FCropProductRow*>(Pair.Value);
		if (Row != nullptr && !Row->SeedItemID.IsNone() && !TableSeeds.Contains(Row->SeedItemID) && TableSeeds.Num() < 2)
		{
			TableSeeds.Add(Row->SeedItemID);
		}
	}
	TArray<FName> TablePetAnimals;
	TArray<FName> TablePetItems;
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* const Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (Row != nullptr && !Row->PetItemID.IsNone() && TablePetAnimals.Num() < 2)
		{
			TablePetAnimals.Add(Row->AnimalID);
			TablePetItems.Add(Row->PetItemID);
		}
	}
	if (!TestEqual(TEXT("표에 씨앗 두 종"), TableSeeds.Num(), 2) || !TestEqual(TEXT("표에 펫 두 종"), TablePetAnimals.Num(), 2))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	URanchHudWidget* const Hud = Player != nullptr ? CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass()) : nullptr;
	if (!TestNotNull(TEXT("목장 HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	Hud->BindToCharacter(Player);
	// 트리를 세워야 탭 줄이 생긴다. 반환값을 놓으면 그 자리에서 해제된다.
	const TSharedRef<SWidget> SlateWidget = Hud->TakeWidget();
	URanchInteractComponent* const Interact = Player->GetRanchInteract();
	URanchComponent* const Ranch = Player->GetRanch();
	UInventoryComponent* const Inventory = Player->GetInventory();
	UHorizontalBox* const TabRow = Hud->WidgetTree->FindWidget<UHorizontalBox>(TEXT("PlacementTabRow"));
	UTextBlock* const PickerText = Hud->WidgetTree->FindWidget<UTextBlock>(TEXT("PetPickerText"));
	if (!TestNotNull(TEXT("상호작용"), Interact) || !TestNotNull(TEXT("목장 캐시"), Ranch) || !TestNotNull(TEXT("인벤토리"), Inventory)
		|| !TestNotNull(TEXT("탭 줄이 트리에 있다"), TabRow) || !TestNotNull(TEXT("고르기 줄"), PickerText)
		|| !TestNotNull(TEXT("동물 탭 버튼"), Hud->WidgetTree->FindWidget<UButton>(TEXT("PlacementAnimalTabButton")))
		|| !TestNotNull(TEXT("씨앗 탭 버튼"), Hud->WidgetTree->FindWidget<UButton>(TEXT("PlacementSeedTabButton"))))
	{
		DestroyTestWorld(World);
		return false;
	}
	auto Owned = [](const TArray<FName>& Items)
	{
		TArray<FInventorySlot> Slots;
		for (const FName& Item : Items)
		{
			FInventorySlot Slot;
			Slot.ItemID = Item;
			Slot.Count = 1;
			Slots.Add(Slot);
		}
		return Slots;
	};

	// ① 씨앗만 가짐 → 씨앗 줄만, 탭 없음. 고르기 버튼은 씨앗끼리만 돈다.
	TestTrue(TEXT("씨앗만 가진 캐시"), Inventory->ApplyBackendSnapshot(Owned(TableSeeds)));
	TestFalse(TEXT("씨앗만 → 탭 없음"), Interact->HasPlacementTabs());
	TestFalse(TEXT("씨앗만 → HUD 탭 안 세움"), Hud->ArePlacementTabsShown());
	TestEqual(TEXT("씨앗만 → 탭 줄 접힘"), TabRow->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("씨앗만 → 씨앗 탭 = 씨앗 두 종"), Interact->GetPlacementTabOptions(true), TableSeeds);
	TestTrue(TEXT("씨앗만 → 동물 탭 비어 있음"), Interact->GetPlacementTabOptions(false).IsEmpty());
	TestTrue(TEXT("씨앗만 → 고른 것은 씨앗 탭"), Interact->IsSeedTabSelected());
	TestFalse(TEXT("씨앗만 → 빈 동물 탭은 못 고른다"), Interact->SelectPlacementTab(false));
	TestEqual(TEXT("씨앗만 → 다음은 둘째 씨앗"), Interact->SelectNextPlacement(), TableSeeds[1]);
	TestTrue(TEXT("씨앗만 → 고르기 줄 (2/2)"), PickerText->GetText().ToString().Contains(TEXT("(2/2)")));

	// ② 둘 다 가짐 → 탭 둘. 첫 선택은 동물(펫이 앞, DESIGN 3.2), 고르기 버튼은 동물끼리만 돈다.
	TArray<FName> Both = TablePetItems;
	Both.Append(TableSeeds);
	TestTrue(TEXT("펫·씨앗 둘 다 가진 캐시"), Inventory->ApplyBackendSnapshot(Owned(Both)));
	TestTrue(TEXT("둘 다 → 탭 둘"), Interact->HasPlacementTabs());
	TestTrue(TEXT("둘 다 → HUD 탭 세움"), Hud->ArePlacementTabsShown());
	TestEqual(TEXT("둘 다 → 탭 줄 펴짐"), TabRow->GetVisibility(), ESlateVisibility::Visible);
	Interact->SelectPlacement(TablePetAnimals[0]);
	TestFalse(TEXT("동물을 고르면 동물 탭"), Interact->IsSeedTabSelected());
	TestEqual(TEXT("동물 탭 = 펫 두 종"), Interact->GetPlacementTabOptions(false), TablePetAnimals);
	TestEqual(TEXT("동물 탭에서 다음 = 둘째 펫"), Interact->SelectNextPlacement(), TablePetAnimals[1]);
	TestEqual(TEXT("동물 탭 끝에서 다음 = 첫 펫(씨앗으로 안 넘어감)"), Interact->SelectNextPlacement(), TablePetAnimals[0]);
	TestTrue(TEXT("동물 탭 고르기 줄 (1/2) — 씨앗은 세지 않는다"), PickerText->GetText().ToString().Contains(TEXT("(1/2)")));

	// ③ 탭 바꾸기 → 줄 종류만 바뀐다. 전체 선택지·칸·요청은 그대로 (CLAUDE.md 8).
	const int32 AllCount = Interact->GetPlacementOptions().Num();
	Hud->SelectPlacementTab(true);
	TestTrue(TEXT("씨앗 탭을 누르면 씨앗을 고른다"), Interact->IsSeedTabSelected());
	TestEqual(TEXT("씨앗 탭 첫 것 = 첫 씨앗"), Interact->GetSelectedPlacementID(), TableSeeds[0]);
	TestEqual(TEXT("씨앗 탭에서 다음 = 둘째 씨앗"), Interact->SelectNextPlacement(), TableSeeds[1]);
	TestEqual(TEXT("씨앗 탭 끝에서 다음 = 첫 씨앗(동물로 안 넘어감)"), Interact->SelectNextPlacement(), TableSeeds[0]);
	TestTrue(TEXT("이미 씨앗 탭이면 그대로"), Interact->SelectPlacementTab(true));
	TestEqual(TEXT("이미 씨앗 탭이면 고른 것 그대로"), Interact->GetSelectedPlacementID(), TableSeeds[0]);
	Hud->SelectPlacementTab(false);
	TestEqual(TEXT("동물 탭으로 돌아가면 첫 펫"), Interact->GetSelectedPlacementID(), TablePetAnimals[0]);
	TestEqual(TEXT("탭은 전체 선택지를 줄이지 않는다"), Interact->GetPlacementOptions().Num(), AllCount);
	TestEqual(TEXT("탭 고르기만으로 요청 0건"), Ranch->GetPendingRequests().Num(), 0);

	AddInfo(FString::Printf(TEXT("[Crop] 넣기 탭 - 동물 %d · 씨앗 %d · 탭 줄 %s · 고르기 '%s'"),
		Interact->GetPlacementTabOptions(false).Num(), Interact->GetPlacementTabOptions(true).Num(),
		TabRow->GetVisibility() == ESlateVisibility::Visible ? TEXT("펴짐") : TEXT("접힘"), *PickerText->GetText().ToString()));
	DestroyTestWorld(World);
	return true;
}

#endif
