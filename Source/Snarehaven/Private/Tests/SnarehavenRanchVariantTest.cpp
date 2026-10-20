// M21-07c2 변종 목장 2층 검사 — 색 변종 펫을 목장에 넣고 칸 카드에 `변종` 이 보이는가 (DESIGN 10.6 · 명세 variant-boss §5-3).
// ① 가방 캐시에 변종 펫이 있으면 배치 선택지에 그 아이템 ID 가 서고 이름은 `변종 ○○`, 넣으면 같은 종 + 변종 요청이 쌓인다.
// ② 전송기는 `RegisterPet {animalId, slot, variant: true}` 를 보내고(일반 펫엔 `variant` 없음), `GetBaseState` 칸 `variant` 를 읽는다.
// ③ 목장 칸 카드 제목은 변종 칸이면 `변종 ○○`, 일반 칸이면 이름 그대로 · 가방 카드 이름(아이템 표)에 `변종` 이 있다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Ranch.Variant; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Building/SnareBaseSubsystem.h"
#include "Data/AnimalRow.h"
#include "Data/ItemRowLookup.h"
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
	constexpr float TestNearDistanceCm = 100.f;
	constexpr int32 TestSlot = 0;
	constexpr int32 TestPlainSlot = 1;
	const FName TestAnimal = TEXT("animal.test_fox");

	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");

	using FDone = USnareBackendSubsystem::FServerCallDone;

	/** 가짜 서버 — `SnarehavenCropPlantTest` 와 같은 짜임. */
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

		void Answer(bool bOk, const TSharedPtr<FJsonObject>& Response)
		{
			if (Waiting.IsEmpty())
			{
				return;
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			Done(bOk, Response, FString());
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

	/** 동물 표에서 변종 펫이 있는 첫 행 — 기대값을 코드에 적지 않고 표에서 읽는다 (DataDriven). */
	const FAnimalRow* FindFirstVariantRow(const UDataTable* AnimalTable)
	{
		for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
		{
			const FAnimalRow* Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
			if (Row != nullptr && !Row->VariantPetItemID.IsNone() && !Row->PetItemID.IsNone())
			{
				return Row;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchVariantPlaceTest, "Snarehaven.Ranch.Variant.Place.QueuesSameSpeciesAsVariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchVariantPlaceTest::RunTest(const FString& Parameters)
{
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	const FAnimalRow* const Row = IsValid(AnimalTable) ? FindFirstVariantRow(AnimalTable) : nullptr;
	if (!TestNotNull(TEXT("변종 펫이 있는 동물 행"), Row))
	{
		return false;
	}

	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	ARanchSlotActor* Slot = SpawnTestSlot(World, TestNearDistanceCm, TestSlot);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("슬롯 스폰"), Slot))
	{
		DestroyTestWorld(World);
		return false;
	}
	URanchInteractComponent* const Interact = Player->GetRanchInteract();
	URanchComponent* const Ranch = Player->GetRanch();
	UInventoryComponent* const Inventory = Player->GetInventory();

	// ① 백엔드가 "변종 펫 1마리를 가졌다" 고 보내 준 셈 (캐시를 바꾸는 유일한 길).
	FInventorySlot VariantSlot;
	VariantSlot.ItemID = Row->VariantPetItemID;
	VariantSlot.Count = 1;
	TestTrue(TEXT("변종 펫 캐시"), Inventory->ApplyBackendSnapshot({ VariantSlot }));
	TestTrue(TEXT("가진 변종 펫이 목록에 있다"), Interact->GetOwnedVariantPetItemIDs().Contains(Row->VariantPetItemID));
	TestFalse(TEXT("일반 펫 목록엔 없다"), Interact->GetOwnedPetAnimalIDs().Contains(Row->AnimalID));
	TestEqual(TEXT("첫 선택지 = 변종 펫 아이템"), Interact->GetSelectedPlacementID(), Row->VariantPetItemID);
	TestEqual(TEXT("변종 펫 → 같은 종"), Interact->FindVariantAnimalID(Row->VariantPetItemID), Row->AnimalID);
	TestTrue(TEXT("일반 펫 아이템은 변종이 아니다"), Interact->FindVariantAnimalID(Row->PetItemID).IsNone());
	const FString PickName = Interact->GetPlacementDisplayName(Row->VariantPetItemID).ToString();
	TestEqual(TEXT("선택지 이름 = 변종 ○○"), PickName, FString::Printf(TEXT("변종 %s"), *Row->DisplayName.ToString()));

	TestTrue(TEXT("빈 칸을 누르면 배치 요청이 쌓인다"), Interact->TryInteractNearest());
	if (!TestEqual(TEXT("대기 요청 1건"), Ranch->GetPendingRequests().Num(), 1))
	{
		DestroyTestWorld(World);
		return false;
	}
	const FRanchRequest& Request = Ranch->GetPendingRequests()[0];
	TestTrue(TEXT("요청 종류 = RegisterPet"), Request.Kind == ERanchRequestKind::RegisterPet);
	TestEqual(TEXT("요청 동물 = 같은 종 동물 ID"), Request.AnimalID, Row->AnimalID);
	TestTrue(TEXT("요청은 변종"), Request.bVariant);
	TestTrue(TEXT("응답 전 칸은 비어 있다"), Ranch->IsSlotEmpty(TestSlot));
	TestEqual(TEXT("응답 전 변종 펫 그대로"), Inventory->GetItemCount(Row->VariantPetItemID), 1);

	AddInfo(FString::Printf(TEXT("[Variant] 배치 - 선택 '%s' · 요청 %s variant=%s"), *PickName,
		*Request.AnimalID.ToString(), Request.bVariant ? TEXT("true") : TEXT("false")));
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchVariantSyncTest, "Snarehaven.Ranch.Variant.Sync.SendsAndReadsVariant",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchVariantSyncTest::RunTest(const FString& Parameters)
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

	TArray<FRanchSlotState> EmptySlots;
	EmptySlots.SetNum(Ranch->GetSlotCount());
	TestTrue(TEXT("빈 목장 스냅샷"), Ranch->ApplyBackendSnapshot(EmptySlots));

	// ② 변종 배치는 `variant: true` 를 싣는다.
	TestTrue(TEXT("변종 배치 요청"), Ranch->QueueRegisterPet(TestSlot, TestAnimal, true));
	if (!TestEqual(TEXT("RegisterPet 한 번"), Server.Functions.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("함수 = RegisterPet"), Server.Functions[0], FString(TEXT("RegisterPet")));
	TestEqual(TEXT("인자 animalId"), Server.Args[0]->GetStringField(TEXT("animalId")), TestAnimal.ToString());
	bool bSentVariant = false;
	TestTrue(TEXT("인자 variant 가 있다"), Server.Args[0]->TryGetBoolField(TEXT("variant"), bSentVariant));
	TestTrue(TEXT("인자 variant = true"), bSentVariant);
	// 답은 거절로 준다 — 성공이면 다시 받기(`GetBaseState`)가 끼어 다음 요청 인자 자리가 밀린다. 인자만 보는 시험이다.
	const TSharedPtr<FJsonObject> Refused = MakeShared<FJsonObject>();
	Refused->SetStringField(TEXT("error"), TEXT("시험 거절"));
	Refused->SetStringField(TEXT("code"), TEXT("not_enough_items"));
	Server.Answer(true, Refused);

	// 일반 배치엔 `variant` 가 없다 — 서버 기본(일반 펫)과 같다.
	TestTrue(TEXT("일반 배치 요청"), Ranch->QueueRegisterPet(TestPlainSlot, TestAnimal));
	const TSharedPtr<FJsonObject> PlainArgs = Server.Args.Last();
	TestEqual(TEXT("두 번째 호출도 RegisterPet"), Server.Functions.Last(), FString(TEXT("RegisterPet")));
	TestFalse(TEXT("일반 배치엔 variant 없음"), PlainArgs.IsValid() && PlainArgs->HasField(TEXT("variant")));
	Server.Answer(true, Refused);

	// `GetBaseState` 칸 `variant` 를 읽는다.
	TestTrue(TEXT("GetBaseState 를 묻는다"), Sync->RequestBaseState());
	const FString Json = FString::Printf(TEXT(R"({"ranch": [{"slot":%d,"animalId":"%s","occupiedSinceUtc":"2026-10-03T01:00:00.000Z","stock":0,"variant":true}, {"slot":%d,"animalId":"%s","occupiedSinceUtc":"2026-10-03T01:00:00.000Z","stock":0}, null, null, null]})"),
		TestSlot, *TestAnimal.ToString(), TestPlainSlot, *TestAnimal.ToString());
	TSharedPtr<FJsonObject> BaseState;
	TestTrue(TEXT("응답 JSON 해석"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), BaseState));
	Server.Answer(true, BaseState);
	const FRanchSlotState* const VariantSlot = Ranch->GetSlot(TestSlot);
	const FRanchSlotState* const PlainSlot = Ranch->GetSlot(TestPlainSlot);
	TestTrue(TEXT("변종 칸 bVariant"), VariantSlot != nullptr && VariantSlot->bVariant);
	TestTrue(TEXT("일반 칸은 변종 아님"), PlainSlot != nullptr && !PlainSlot->bVariant);
	TestEqual(TEXT("변종 칸 동물 = 같은 종"), VariantSlot != nullptr ? VariantSlot->AnimalID : NAME_None, TestAnimal);

	AddInfo(FString::Printf(TEXT("[Variant] 전송 - 호출 %s · 칸0 variant=%s · 칸1 variant=%s"),
		*FString::Join(Server.Functions, TEXT(",")),
		VariantSlot != nullptr && VariantSlot->bVariant ? TEXT("true") : TEXT("false"),
		PlainSlot != nullptr && PlainSlot->bVariant ? TEXT("true") : TEXT("false")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchVariantCardTest, "Snarehaven.Ranch.Variant.Card.TitleAndBackpackName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchVariantCardTest::RunTest(const FString& Parameters)
{
	const UDataTable* const AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, ItemTablePath);
	const FAnimalRow* const Row = IsValid(AnimalTable) ? FindFirstVariantRow(AnimalTable) : nullptr;
	if (!TestNotNull(TEXT("변종 펫이 있는 동물 행"), Row) || !TestNotNull(TEXT("DT_ItemTable"), ItemTable))
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
	URanchComponent* const Ranch = Hud->GetWatchedRanch();
	if (!TestNotNull(TEXT("목장 캐시"), Ranch))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ③ 백엔드가 "변종 칸 하나·일반 칸 하나" 를 보내 준 셈.
	TArray<FRanchSlotState> Snapshot;
	Snapshot.SetNum(Ranch->GetSlotCount());
	Snapshot[TestSlot].AnimalID = Row->AnimalID;
	Snapshot[TestSlot].OccupiedSinceUtc = FDateTime::UtcNow();
	Snapshot[TestSlot].bVariant = true;
	Snapshot[TestPlainSlot].AnimalID = Row->AnimalID;
	Snapshot[TestPlainSlot].OccupiedSinceUtc = FDateTime::UtcNow();
	TestTrue(TEXT("칸 스냅샷"), Ranch->ApplyBackendSnapshot(Snapshot));

	const FString VariantTitle = Hud->GetSlotCardTitle(TestSlot).ToString();
	const FString PlainTitle = Hud->GetSlotCardTitle(TestPlainSlot).ToString();
	TestEqual(TEXT("변종 칸 카드 = 변종 ○○"), VariantTitle, FString::Printf(TEXT("변종 %s"), *Row->DisplayName.ToString()));
	TestEqual(TEXT("일반 칸 카드 = 이름 그대로"), PlainTitle, Row->DisplayName.ToString());
	TestFalse(TEXT("빈 칸 카드엔 변종이 없다"), Hud->GetSlotCardTitle(TestPlainSlot + 1).ToString().Contains(TEXT("변종")));

	// 가방 카드 이름은 아이템 표 `DisplayName` 이다 (`BackpackScreenWidget` 카드) — 변종 펫 행에 `변종` 이 있어야 한다.
	const FItemRow* const VariantItem = SnareItemRows::FindByItemID(ItemTable, Row->VariantPetItemID);
	const FItemRow* const PlainItem = SnareItemRows::FindByItemID(ItemTable, Row->PetItemID);
	const FString VariantItemName = VariantItem != nullptr ? VariantItem->DisplayName.ToString() : FString();
	TestTrue(TEXT("가방 카드 이름에 변종"), VariantItemName.Contains(TEXT("변종")));
	TestFalse(TEXT("일반 펫 가방 카드엔 변종 없음"),
		PlainItem == nullptr || PlainItem->DisplayName.ToString().Contains(TEXT("변종")));

	AddInfo(FString::Printf(TEXT("[Variant] 칸 카드 '%s' · 일반 '%s' · 가방 카드 '%s'"), *VariantTitle, *PlainTitle, *VariantItemName));
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
