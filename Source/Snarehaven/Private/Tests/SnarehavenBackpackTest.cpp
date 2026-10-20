// DESIGN 8.12 배낭·버리기·사망 가방 2층 검사 — 가짜 서버(`TestServerOverride`)를 꽂아 진짜 응답과 같은 길을 태운다.
// 이 검사가 잡는 것: ① 칸 수 20/28·스택 99 가 서버 계약과 다른 것 ② 펫·가챠가 배낭 칸에 끼는 것
// ③ **답이 오기 전에 칸이 바뀌는 것**(CLAUDE.md 8) ④ 서버가 `dropped:false` 라는데 가방을 두는 것
// ⑤ 같은 가방을 두 번 줍겠다고 묻는 것 ⑥ 가방 수명을 클라이언트 시계로 지어내는 것.
// 가방에 무엇이 들어가는지(코인·젬·도감·목장·펫·가챠 제외)는 서버 `DropOnDeath` 판정이라 여기선 **응답 그대로 담는지**만 본다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Backpack; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Building/SnareBaseBuildComponent.h"
#include "Building/SnareBaseSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/SceneComponent.h"
#include "Data/GachaPoolRow.h"
#include "Data/ItemRowLookup.h"
#include "Data/PlayerVitalsRow.h"
#include "Data/RecipeRow.h"
#include "Data/WaterActivityRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/EquipmentComponent.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/DropBagActor.h"
#include "Layout/ArrangedChildren.h"
#include "Layout/ArrangedWidget.h"
#include "Layout/Geometry.h"
#include "Inventory/InventoryComponent.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchSyncComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Player/PlayerVitalsComponent.h"
#include "SnarehavenCharacter.h"
#include "SnarehavenGameMode.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/CodexScreenWidget.h"
#include "UI/CraftingScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/RanchHudWidget.h"
#include "UI/SnareUiStyle.h"
#include "Widgets/SWidget.h"
#include "World/FishingSpotActor.h"
#include "World/SeaActor.h"
#include "World/WaterTravelComponent.h"
#include "World/WaterRules.h"

namespace SnareBackpackTest
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다). 서버 계약 값(20·8·99·600)은 아래에서 표·코드와 맞댄다.
	const FName WoodID = TEXT("item.wood");
	const FName BerryID = TEXT("item.berry");
	const FName HikingBackpackID = TEXT("item.hiking_backpack");
	const FName BoatContainerID = TEXT("item.boat_container");
	const FName TentID = TEXT("item.tent");
	const FName StorageBoxID = TEXT("item.storage_box");
	const FName LifeboatID = TEXT("item.lifeboat");
	const FName PetID = TEXT("item.pet_pig");
	const FName HatchetID = TEXT("item.hatchet");   // M15-08 — 손에 드는 장비 하나 (표의 `Tool` 행)
	const FName GachaID = TEXT("gacha.pet_ribbon");
	const TCHAR* TestOwnerId = TEXT("PFTESTOWNER");
	constexpr int32 ServerBackpackSlots = 20;  // cloudscript BACKPACK_SLOTS
	constexpr int32 ServerBonusSlots = 8;      // cloudscript BACKPACK_BONUS_SLOTS
	constexpr int32 ServerStackMax = 99;       // cloudscript STACK_MAX
	constexpr int32 ServerDropTtlSeconds = 600; // cloudscript DROP_TTL_SECONDS
	constexpr int32 TestWoodCount = 250;       // 99 + 99 + 52
	constexpr int32 TestDropWood = 5;
	constexpr int32 TestBerryCount = 3;
	constexpr float TestHeal = 20.f;
	constexpr float LifeSpanTolerance = 5.f;

	using FDone = USnareBackendSubsystem::FServerCallDone;

	struct FFakeServer
	{
		FString LastFunction;
		TSharedPtr<FJsonObject> LastArgs;
		TArray<FDone> Waiting;
		int32 Calls = 0;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone OnDone)
				{
					++Calls;
					LastFunction = Name;
					LastArgs = Args;
					Waiting.Add(MoveTemp(OnDone));
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		void Answer(const TSharedPtr<FJsonObject>& Response)
		{
			if (Waiting.IsEmpty())
			{
				return;
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			Done(true, Response, FString());
		}

		void AnswerFail(const FString& Code, const FString& Message)
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("error"), Message);
			Response->SetStringField(TEXT("code"), Code);
			Answer(Response);
		}
	};

	/** 서버 `DropItem`·`DropOnDeath` 성공 응답 모양 — `{dropId, items, expiresAtUtc[, dropped]}`. */
	TSharedPtr<FJsonObject> MakeDropResponse(const FString& DropId, FName ItemID, int32 Count, int32 TtlSeconds)
	{
		const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("dropId"), DropId);
		const TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("itemId"), ItemID.ToString());
		Item->SetNumberField(TEXT("count"), Count);
		TArray<TSharedPtr<FJsonValue>> Items;
		Items.Add(MakeShared<FJsonValueObject>(Item));
		Response->SetArrayField(TEXT("items"), Items);
		Response->SetStringField(TEXT("expiresAtUtc"), (FDateTime::UtcNow() + FTimespan::FromSeconds(TtlSeconds)).ToIso8601());
		return Response;
	}

	FSnarePlayerSnapshot MakeSnapshot(const TArray<FInventorySlot>& Slots)
	{
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TestOwnerId;
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		Snapshot.InventorySlots = Slots;
		return Snapshot;
	}

	FInventorySlot MakeSlot(FName ItemID, int32 Count)
	{
		FInventorySlot Slot;
		Slot.ItemID = ItemID;
		Slot.Count = Count;
		return Slot;
	}

	bool SameSlots(const TArray<FInventorySlot>& A, const TArray<FInventorySlot>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < A.Num(); ++Index)
		{
			if (A[Index].ItemID != B[Index].ItemID || A[Index].Count != B[Index].Count)
			{
				return false;
			}
		}
		return true;
	}

	int32 CountBags(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<ADropBagActor> It(World); It; ++It)
		{
			Count += IsValid(*It) ? 1 : 0;
		}
		return Count;
	}

	ADropBagActor* FirstBag(UWorld* World)
	{
		for (TActorIterator<ADropBagActor> It(World); It; ++It)
		{
			if (IsValid(*It))
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** 게임 월드 + 백엔드 + 배낭 컴포넌트를 단 움직이는 액터 하나. */
	struct FRig
	{
		UWorld* World = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		AActor* Owner = nullptr;
		UBackpackComponent* Backpack = nullptr;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
			Backend = NewObject<USnareBackendSubsystem>(GameInstance);

			Owner = World->SpawnActor<AActor>();
			USceneComponent* const Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
			Root->SetMobility(EComponentMobility::Movable);
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Backpack = NewObject<UBackpackComponent>(Owner, TEXT("Backpack"));
			Backpack->RegisterComponent();
			Backpack->BindTargets(Backend, nullptr);
		}

		~FRig()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

using namespace SnareBackpackTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackCapacityTest, "Snarehaven.Backpack.Slots.CapacityAndStackMatchServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackCapacityTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}
	const int32 BaseSlots = GetDefault<UInventoryComponent>()->GetSlotCount();
	TestEqual(TEXT("기본 칸 = 서버 BACKPACK_SLOTS"), BaseSlots, ServerBackpackSlots);

	const TArray<FInventorySlot> Plain = { MakeSlot(WoodID, 1) };
	const TArray<FInventorySlot> WithPack = { MakeSlot(WoodID, 1), MakeSlot(HikingBackpackID, 1) };
	const int32 PlainSlots = UBackpackScreenWidget::ComputeCapacity(Plain, ItemTable, BaseSlots);
	const int32 PackSlots = UBackpackScreenWidget::ComputeCapacity(WithPack, ItemTable, BaseSlots);
	TestEqual(TEXT("배낭 없으면 20칸"), PlainSlots, ServerBackpackSlots);
	TestEqual(TEXT("하이킹 배낭 있으면 28칸"), PackSlots, ServerBackpackSlots + ServerBonusSlots);
	// 반대 방향 — 0개 가진 배낭은 칸을 늘리지 않는다.
	TestEqual(TEXT("0개 배낭은 칸 안 늘림"),
		UBackpackScreenWidget::ComputeCapacity({ MakeSlot(HikingBackpackID, 0) }, ItemTable, BaseSlots), ServerBackpackSlots);
	// M32-02 보정 — 보트 컨테이너는 배 보관함(서버 `boat: true`)이라 배낭 칸을 안 늘린다.
	TestEqual(TEXT("보트 컨테이너만 = 그대로 20칸"),
		UBackpackScreenWidget::ComputeCapacity({ MakeSlot(BoatContainerID, 1) }, ItemTable, BaseSlots), ServerBackpackSlots);
	TestEqual(TEXT("배낭 + 보트 컨테이너 = 최대 28칸"),
		UBackpackScreenWidget::ComputeCapacity({ MakeSlot(HikingBackpackID, 1), MakeSlot(BoatContainerID, 1) }, ItemTable, BaseSlots),
		ServerBackpackSlots + ServerBonusSlots);

	const TArray<FInventorySlot> Owned = {
		MakeSlot(WoodID, TestWoodCount), MakeSlot(PetID, 1), MakeSlot(GachaID, 1), MakeSlot(BerryID, TestBerryCount) };
	const TArray<FInventorySlot> Shown = UBackpackScreenWidget::BuildVisibleSlots(Owned, ItemTable, ServerStackMax);
	TArray<int32> WoodStacks;
	bool bPetOrGachaShown = false;
	for (const FInventorySlot& Slot : Shown)
	{
		if (Slot.ItemID == WoodID)
		{
			WoodStacks.Add(Slot.Count);
		}
		bPetOrGachaShown |= Slot.ItemID == PetID || Slot.ItemID == GachaID;
	}
	TestEqual(TEXT("나무 250개 = 3칸"), WoodStacks.Num(), 3);
	TestTrue(TEXT("나무 칸 = 99, 99, 52 (가득 찬 칸 먼저)"),
		WoodStacks == TArray<int32>({ ServerStackMax, ServerStackMax, TestWoodCount - 2 * ServerStackMax }));
	TestFalse(TEXT("펫·가챠는 배낭 칸에 없다 (서버 isBackpackItem)"), bPetOrGachaShown);
	TestEqual(TEXT("보이는 칸 = 나무 3 + 산딸기 1"), Shown.Num(), 4);
	// 반대 방향 — 상한을 바꾸면 칸 수가 따라 바뀐다(99 를 어딘가 박아 둔 게 아니다).
	TestEqual(TEXT("상한 250 이면 나무 1칸"),
		UBackpackScreenWidget::BuildVisibleSlots({ MakeSlot(WoodID, TestWoodCount) }, ItemTable, TestWoodCount).Num(), 1);

	AddInfo(FString::Printf(TEXT("[Backpack] 칸 %d/%d · 나무 %d개 → %d칸 · 보이는 칸 %d"),
		PlainSlots, PackSlots, TestWoodCount, WoodStacks.Num(), Shown.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackNoChangeBeforeReplyTest, "Snarehaven.Backpack.Drop.ScreenWaitsForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackNoChangeBeforeReplyTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, TestDropWood), MakeSlot(BerryID, TestBerryCount) }), false);

	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
	Screen->SetPanelOpen(true);
	TestEqual(TEXT("칸 격자 = 20칸"), Screen->GetCellCount(), ServerBackpackSlots);
	// 빈 칸은 원하는 크기가 0 이라 격자 칸을 꽉 채우게 펴지 않으면 화면에서 사라진다(3층에서 실제로 0칸이 보였다).
	const UWidget* const LastCell = Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("Cell_%d"), ServerBackpackSlots - 1)));
	const UUniformGridSlot* const LastSlot = LastCell != nullptr ? Cast<UUniformGridSlot>(LastCell->Slot) : nullptr;
	TestTrue(TEXT("빈 칸이 격자 칸을 꽉 채운다"), LastSlot != nullptr
		&& LastSlot->GetHorizontalAlignment() == HAlign_Fill && LastSlot->GetVerticalAlignment() == VAlign_Fill);

	const TArray<FInventorySlot> Before = Screen->GetShownSlots();
	const int32 WoodIndex = Before.IndexOfByPredicate([](const FInventorySlot& Slot) { return Slot.ItemID == WoodID; });
	if (!TestTrue(TEXT("나무 칸이 보인다"), WoodIndex != INDEX_NONE))
	{
		return false;
	}
	Screen->SelectSlot(WoodIndex);
	Screen->PressDrop();
	TestEqual(TEXT("부른 함수 = DropItem"), Server.LastFunction, FString(TEXT("DropItem")));
	TestEqual(TEXT("보낸 itemId"), Server.LastArgs->GetStringField(TEXT("itemId")), WoodID.ToString());
	TestEqual(TEXT("보낸 count = 칸 통째"), static_cast<int32>(Server.LastArgs->GetNumberField(TEXT("count"))), TestDropWood);

	// 답 전: 칸·가방 그대로.
	TestTrue(TEXT("답 전 칸 그대로"), SameSlots(Screen->GetShownSlots(), Before));
	TestEqual(TEXT("답 전 가방 없음"), CountBags(Rig.World), 0);

	// 거절 — 칸 그대로, 사유가 상태 줄에, 다시 받지 않는다.
	Server.AnswerFail(TEXT("inventory_full"), TEXT("가방이 가득 찼다"));
	TestTrue(TEXT("거절 뒤 칸 그대로"), SameSlots(Screen->GetShownSlots(), Before));
	TestEqual(TEXT("거절 사유가 상태 줄에"), Screen->GetStatusText().ToString(), FString(TEXT("가방이 가득 찼다")));
	TestEqual(TEXT("거절엔 가방 없음"), CountBags(Rig.World), 0);
	TestEqual(TEXT("거절엔 다시 받지 않는다"), Rig.Backpack->GetRefetchCount(), 0);

	// 성공 — 가방이 서버 응답대로 생기고, 칸은 **여전히** 그대로다(스냅샷이 아직 안 왔다).
	Screen->PressDrop();
	Server.Answer(MakeDropResponse(TEXT("drop_a"), WoodID, TestDropWood, ServerDropTtlSeconds));
	TestEqual(TEXT("성공 뒤 가방 1개"), CountBags(Rig.World), 1);
	TestEqual(TEXT("성공 뒤 다시 받는다"), Rig.Backpack->GetRefetchCount(), 1);
	TestTrue(TEXT("스냅샷 전 칸 그대로"), SameSlots(Screen->GetShownSlots(), Before));

	// 스냅샷이 와야 칸이 바뀐다.
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(BerryID, TestBerryCount) }), false);
	const bool bWoodGone = !Screen->GetShownSlots().ContainsByPredicate([](const FInventorySlot& Slot) { return Slot.ItemID == WoodID; });
	TestTrue(TEXT("스냅샷 뒤 나무 칸 사라짐"), bWoodGone);

	// 화면이 실제로 쓰는 스택 상한 = 서버 STACK_MAX — 나무 250개면 3칸.
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, TestWoodCount) }), false);
	TestEqual(TEXT("화면에서 나무 250개 = 3칸 (스택 99)"), Screen->GetShownSlots().Num(), 3);
	AddInfo(FString::Printf(TEXT("[Backpack] 버리기 - 답 전 칸 %d · 거절 뒤 %d · 성공 뒤 가방 %d · 스냅샷 뒤 칸 %d"),
		Before.Num(), Before.Num(), CountBags(Rig.World), Screen->GetShownSlots().Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackDeathBagTest, "Snarehaven.Backpack.Death.BagOnlyWhenServerDropped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackDeathBagTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, TestDropWood) }), false);

	// 서버가 떨군 게 없다(배낭이 비었거나 지키는 것뿐) → 가방 없음.
	TestTrue(TEXT("사망 요청"), Rig.Backpack->RequestDropOnDeath());
	TestEqual(TEXT("부른 함수 = DropOnDeath"), Server.LastFunction, FString(TEXT("DropOnDeath")));
	TestEqual(TEXT("인자 없음"), Server.LastArgs->Values.Num(), 0);
	const TSharedPtr<FJsonObject> Nothing = MakeShared<FJsonObject>();
	Nothing->SetBoolField(TEXT("dropped"), false);
	Server.Answer(Nothing);
	TestEqual(TEXT("dropped:false 면 가방 없음"), CountBags(Rig.World), 0);

	// 떨궜다 → 죽은 자리(요청 순간)에 서버 목록 그대로. 답 전에 부활 자리로 옮겨도 가방은 죽은 자리.
	const FVector DeathSpot(100.f, 200.f, 0.f);
	Rig.Owner->SetActorLocation(DeathSpot);
	TestTrue(TEXT("두 번째 사망 요청"), Rig.Backpack->RequestDropOnDeath());
	Rig.Owner->SetActorLocation(FVector(5000.f, 0.f, 0.f));
	TSharedPtr<FJsonObject> Dropped = MakeDropResponse(TEXT("drop_death"), WoodID, TestDropWood, ServerDropTtlSeconds);
	Dropped->SetBoolField(TEXT("dropped"), true);
	Server.Answer(Dropped);

	ADropBagActor* const Bag = FirstBag(Rig.World);
	if (!TestNotNull(TEXT("dropped:true 면 가방 1개"), Bag))
	{
		return false;
	}
	TestEqual(TEXT("가방 수 1"), CountBags(Rig.World), 1);
	TestTrue(TEXT("가방은 죽은 자리"), Bag->GetActorLocation().Equals(DeathSpot, 1.f));
	TestEqual(TEXT("가방 주인 = 내 PlayFabId"), Bag->GetOwnerPlayFabId(), FString(TestOwnerId));
	TestEqual(TEXT("가방 ID = 서버 dropId"), Bag->GetDropId(), FString(TEXT("drop_death")));
	TestTrue(TEXT("가방 속 = 서버 items 그대로(나무 5)"), Bag->GetItems().Num() == 1
		&& Bag->GetItems()[0].ItemID == WoodID && Bag->GetItems()[0].Count == TestDropWood);
	TestEqual(TEXT("가방 수명 = 서버 만료까지(600초)"), Bag->GetLifeSpan(), static_cast<float>(ServerDropTtlSeconds), LifeSpanTolerance);
	TestEqual(TEXT("두 번 다 받아들여져 두 번 다시 받음"), Rig.Backpack->GetRefetchCount(), 2);

	// 반대 방향 — 만료 시각을 못 읽으면 초를 지어내지 않는다(수명 0 = 없음).
	TestEqual(TEXT("못 읽는 시각 → 0초"), ADropBagActor::SecondsUntil(TEXT("not-a-date"), FDateTime::UtcNow()), 0.f);
	const FDateTime Now(2026, 9, 24, 0, 0, 0);
	TestEqual(TEXT("10분 뒤 시각 → 600초"),
		ADropBagActor::SecondsUntil((Now + FTimespan::FromSeconds(ServerDropTtlSeconds)).ToIso8601(), Now),
		static_cast<float>(ServerDropTtlSeconds));
	TestEqual(TEXT("지난 시각 → 0초"), ADropBagActor::SecondsUntil(Now.ToIso8601(), Now + FTimespan::FromSeconds(1)), 0.f);
	AddInfo(FString::Printf(TEXT("[Backpack] 사망 - dropped:false 가방 0 · dropped:true 가방 %d · 수명 %.0f초"),
		CountBags(Rig.World), Bag->GetLifeSpan()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackDropSoundTest, "Snarehaven.Backpack.Sound.DropOnlyWhenAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackDropSoundTest::RunTest(const FString& Parameters)
{
	// M49-03 — 버리기·죽음 떨굼은 서버가 받아들여 가방을 둔 응답에만 `BagDrop` 한 번 (DESIGN 8.12 「가방·상자 소리」).
	const FName DropCue = TEXT("BagDrop");
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, TestDropWood) }), false);

	// 버리기 — 답 전 0 · 거절 0 · 받아들임 1.
	TestTrue(TEXT("버리기 요청"), Rig.Backpack->RequestDrop(WoodID, TestDropWood));
	TestEqual(TEXT("답 전 소리 0"), Rig.Backpack->GetBagCueCount(), 0);
	Server.AnswerFail(TEXT("not_enough"), TEXT("가진 게 모자라다"));
	TestEqual(TEXT("버리기 거절 → 소리 0"), Rig.Backpack->GetBagCueCount(), 0);
	Rig.Backpack->RequestDrop(WoodID, TestDropWood);
	Server.Answer(MakeDropResponse(TEXT("drop_sound_a"), WoodID, TestDropWood, ServerDropTtlSeconds));
	TestEqual(TEXT("버리기 받아들임 → 소리 1"), Rig.Backpack->GetBagCueCount(), 1);
	TestEqual(TEXT("버리기 소리 = BagDrop"), Rig.Backpack->GetLastBagCue(), DropCue);

	// 죽음 떨굼 — 거절 0 · 가방 없음(dropped:false) 0 · 가방 둠 1.
	Rig.Backpack->RequestDropOnDeath();
	Server.AnswerFail(TEXT("busy"), TEXT("잠시 뒤 다시"));
	TestEqual(TEXT("죽음 떨굼 거절 → 소리 그대로"), Rig.Backpack->GetBagCueCount(), 1);
	Rig.Backpack->RequestDropOnDeath();
	const TSharedPtr<FJsonObject> Nothing = MakeShared<FJsonObject>();
	Nothing->SetBoolField(TEXT("dropped"), false);
	Server.Answer(Nothing);
	TestEqual(TEXT("dropped:false → 소리 그대로"), Rig.Backpack->GetBagCueCount(), 1);
	Rig.Backpack->RequestDropOnDeath();
	TSharedPtr<FJsonObject> Dropped = MakeDropResponse(TEXT("drop_sound_death"), WoodID, TestDropWood, ServerDropTtlSeconds);
	Dropped->SetBoolField(TEXT("dropped"), true);
	Server.Answer(Dropped);
	TestEqual(TEXT("dropped:true → 소리 2 (한 응답 한 번)"), Rig.Backpack->GetBagCueCount(), 2);
	TestEqual(TEXT("죽음 떨굼 소리 = BagDrop 빌림"), Rig.Backpack->GetLastBagCue(), DropCue);
	TestEqual(TEXT("가방 수 = 소리 수"), CountBags(Rig.World), Rig.Backpack->GetBagCueCount());
	AddInfo(FString::Printf(TEXT("[Sound] 버리기·죽음 떨굼 - 응답 5 (받아들여 가방 둠 %d) → 소리 %d"),
		CountBags(Rig.World), Rig.Backpack->GetBagCueCount()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackBagOnGroundTest, "Snarehaven.Backpack.Death.BagRestsOnGround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackBagOnGroundTest::RunTest(const FString& Parameters)
{
	// M13-38 — 받은 자리(폰 캡슐 가운데)가 바닥 위에 떠 있어도 가방은 바닥 윗면에 놓인다. 바닥이 없으면 받은 자리 그대로.
	constexpr float TestFloorTopZ = 300.f;   // 테스트 전용 — 0 이 아니어야 "원점에 놓았다" 와 구별된다
	constexpr float TestDropAboveCm = 150.f; // 바닥 위 150cm 에서 떨군다 (캡슐 가운데 88cm 보다 높게)
	constexpr float GroundToleranceCm = 2.f;
	FFakeServer Server;
	Server.Install();
	FRig Rig;

	// 엔진 기본 큐브(한 변 100cm·중심 원점)를 10배로 편 판 — XY ±500cm, 윗면 z = TestFloorTopZ.
	AStaticMeshActor* const Floor = Rig.World->SpawnActor<AStaticMeshActor>(FVector(0.f, 0.f, TestFloorTopZ - 50.f), FRotator::ZeroRotator);
	UStaticMesh* const CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("검사용 바닥"), Floor) || !TestNotNull(TEXT("큐브 메시"), CubeMesh))
	{
		return false;
	}
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
	Floor->GetStaticMeshComponent()->SetWorldScale3D(FVector(10.f, 10.f, 1.f));

	auto DropAt = [&Server, &Rig](const FVector& Where, const TCHAR* DropId) -> ADropBagActor*
	{
		Rig.Owner->SetActorLocation(Where);
		Rig.Backpack->RequestDropOnDeath();
		TSharedPtr<FJsonObject> Dropped = MakeDropResponse(DropId, WoodID, TestDropWood, ServerDropTtlSeconds);
		Dropped->SetBoolField(TEXT("dropped"), true);
		Server.Answer(Dropped);
		for (TActorIterator<ADropBagActor> It(Rig.World); It; ++It)
		{
			if (IsValid(*It) && It->GetDropId() == DropId)
			{
				return *It;
			}
		}
		return nullptr;
	};

	const FVector OverFloor(100.f, 200.f, TestFloorTopZ + TestDropAboveCm);
	ADropBagActor* const Grounded = DropAt(OverFloor, TEXT("drop_floor"));
	if (!TestNotNull(TEXT("바닥 위 가방"), Grounded))
	{
		return false;
	}
	TestEqual(TEXT("가방 z = 바닥 윗면 (±2cm)"), static_cast<float>(Grounded->GetActorLocation().Z), TestFloorTopZ, GroundToleranceCm);
	TestTrue(TEXT("가방 xy = 받은 자리"), FVector2D(Grounded->GetActorLocation()).Equals(FVector2D(OverFloor), 1.f));

	// 반대 방향 — 바닥 밖(판 XY ±500 밖)이면 받은 자리 그대로.
	const FVector NoFloor(5000.f, 0.f, TestFloorTopZ + TestDropAboveCm);
	ADropBagActor* const Floating = DropAt(NoFloor, TEXT("drop_nofloor"));
	if (!TestNotNull(TEXT("바닥 없는 가방"), Floating))
	{
		return false;
	}
	TestTrue(TEXT("바닥 없으면 받은 자리 그대로"), Floating->GetActorLocation().Equals(NoFloor, 1.f));
	AddInfo(FString::Printf(TEXT("[Backpack] 바닥 맞추기 - 받은 z %.1f · 바닥 윗면 %.1f · 가방 z %.1f · 바닥 없음 가방 z %.1f"),
		OverFloor.Z, TestFloorTopZ, Grounded->GetActorLocation().Z, Floating->GetActorLocation().Z));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackPickupOnceTest, "Snarehaven.Backpack.Pickup.AsksOnceAndClearsTakenBag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackPickupOnceTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	const TArray<FInventorySlot> Items = { MakeSlot(WoodID, TestDropWood) };
	const FString Expires = (FDateTime::UtcNow() + FTimespan::FromSeconds(ServerDropTtlSeconds)).ToIso8601();
	ADropBagActor* const Taken = Rig.World->SpawnActor<ADropBagActor>();
	ADropBagActor* const Full = Rig.World->SpawnActor<ADropBagActor>();
	ADropBagActor* const Mine = Rig.World->SpawnActor<ADropBagActor>();
	Taken->InitFromServer(TEXT("PFOTHER"), TEXT("drop_taken"), Items, Expires);
	Full->InitFromServer(TEXT("PFOTHER"), TEXT("drop_full"), Items, Expires);
	Mine->InitFromServer(TEXT("PFOTHER"), TEXT("drop_ok"), Items, Expires);

	// 답 전 두 번 누르면 한 번만 묻는다.
	TestTrue(TEXT("첫 줍기 요청"), Rig.Backpack->RequestPickup(Taken));
	TestFalse(TEXT("답 전 같은 가방 또 안 묻는다"), Rig.Backpack->RequestPickup(Taken));
	TestEqual(TEXT("서버 호출 1번"), Server.Calls, 1);
	TestEqual(TEXT("부른 함수 = PickupDrop"), Server.LastFunction, FString(TEXT("PickupDrop")));
	TestEqual(TEXT("ownerId = 가방 주인"), Server.LastArgs->GetStringField(TEXT("ownerId")), FString(TEXT("PFOTHER")));
	TestEqual(TEXT("dropId = 가방 ID"), Server.LastArgs->GetStringField(TEXT("dropId")), FString(TEXT("drop_taken")));

	// 남이 먼저 주웠다 → 치운다.
	Server.AnswerFail(TEXT("already_taken"), TEXT("이미 누가 주웠다"));
	TestFalse(TEXT("already_taken 가방은 치운다"), IsValid(Taken));
	TestEqual(TEXT("거절엔 다시 받지 않는다"), Rig.Backpack->GetRefetchCount(), 0);

	// 반대 방향 — 가득 참은 가방을 남기고, 다시 물을 수 있다.
	TestTrue(TEXT("가득 찬 줍기 요청"), Rig.Backpack->RequestPickup(Full));
	Server.AnswerFail(TEXT("inventory_full"), TEXT("가방이 가득 찼다"));
	TestTrue(TEXT("inventory_full 가방은 남는다"), IsValid(Full));
	TestTrue(TEXT("답이 온 뒤엔 다시 물을 수 있다"), Rig.Backpack->RequestPickup(Full));
	Server.AnswerFail(TEXT("inventory_full"), TEXT("가방이 가득 찼다"));

	// 성공 → 치우고 다시 받는다.
	TestTrue(TEXT("성공 줍기 요청"), Rig.Backpack->RequestPickup(Mine));
	const TSharedPtr<FJsonObject> Picked = MakeShared<FJsonObject>();
	Picked->SetBoolField(TEXT("picked"), true);
	Picked->SetStringField(TEXT("dropId"), TEXT("drop_ok"));
	Server.Answer(Picked);
	TestFalse(TEXT("주운 가방은 치운다"), IsValid(Mine));
	TestEqual(TEXT("성공 뒤 다시 받는다"), Rig.Backpack->GetRefetchCount(), 1);
	TestEqual(TEXT("남은 가방 = 가득 찬 것 1개"), CountBags(Rig.World), 1);
	AddInfo(FString::Printf(TEXT("[Backpack] 줍기 - 호출 %d번 · 남은 가방 %d · 다시받기 %d"),
		Server.Calls, CountBags(Rig.World), Rig.Backpack->GetRefetchCount()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackPickupSoundTest, "Snarehaven.Backpack.Sound.PickupOnlyWhenAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackPickupSoundTest::RunTest(const FString& Parameters)
{
	// M49-04 — 줍기는 서버가 받아들인 응답에만 `BagPickup` 한 번, 가득 참·already_taken·expired 는 무음 (DESIGN 8.12).
	const FName PickupCue = TEXT("BagPickup");
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	const TArray<FInventorySlot> Items = { MakeSlot(WoodID, TestDropWood) };
	const FString Expires = (FDateTime::UtcNow() + FTimespan::FromSeconds(ServerDropTtlSeconds)).ToIso8601();
	ADropBagActor* const Full = Rig.World->SpawnActor<ADropBagActor>();
	ADropBagActor* const Taken = Rig.World->SpawnActor<ADropBagActor>();
	ADropBagActor* const Old = Rig.World->SpawnActor<ADropBagActor>();
	ADropBagActor* const Mine = Rig.World->SpawnActor<ADropBagActor>();
	Full->InitFromServer(TEXT("PFOTHER"), TEXT("drop_snd_full"), Items, Expires);
	Taken->InitFromServer(TEXT("PFOTHER"), TEXT("drop_snd_taken"), Items, Expires);
	Old->InitFromServer(TEXT("PFOTHER"), TEXT("drop_snd_old"), Items, Expires);
	Mine->InitFromServer(TEXT("PFOTHER"), TEXT("drop_snd_ok"), Items, Expires);

	TestTrue(TEXT("가득 찬 줍기 요청"), Rig.Backpack->RequestPickup(Full));
	TestEqual(TEXT("답 전 소리 0"), Rig.Backpack->GetBagCueCount(), 0);
	Server.AnswerFail(TEXT("inventory_full"), TEXT("가방이 가득 찼다"));
	TestEqual(TEXT("inventory_full → 소리 0"), Rig.Backpack->GetBagCueCount(), 0);
	Rig.Backpack->RequestPickup(Taken);
	Server.AnswerFail(TEXT("already_taken"), TEXT("이미 누가 주웠다"));
	TestEqual(TEXT("already_taken → 소리 0"), Rig.Backpack->GetBagCueCount(), 0);
	Rig.Backpack->RequestPickup(Old);
	Server.AnswerFail(TEXT("expired"), TEXT("가방이 사라졌다"));
	TestEqual(TEXT("expired → 소리 0"), Rig.Backpack->GetBagCueCount(), 0);

	// 받아들임 — 가방이 치워져도 소리는 한 번 (가방 안 아이템 수와 무관).
	Rig.Backpack->RequestPickup(Mine);
	const TSharedPtr<FJsonObject> Picked = MakeShared<FJsonObject>();
	Picked->SetBoolField(TEXT("picked"), true);
	Picked->SetStringField(TEXT("dropId"), TEXT("drop_snd_ok"));
	Server.Answer(Picked);
	TestFalse(TEXT("주운 가방은 치운다"), IsValid(Mine));
	TestEqual(TEXT("받아들임 → 소리 1"), Rig.Backpack->GetBagCueCount(), 1);
	TestEqual(TEXT("줍기 소리 = BagPickup"), Rig.Backpack->GetLastBagCue(), PickupCue);
	AddInfo(FString::Printf(TEXT("[Sound] 줍기 - 응답 4 (받아들임 1) → 소리 %d · 남은 가방 %d"),
		Rig.Backpack->GetBagCueCount(), CountBags(Rig.World)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackHealTest, "Snarehaven.Backpack.Consume.HealsByServerAmount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackHealTest::RunTest(const FString& Parameters)
{
	const UDataTable* const VitalsTable =
		LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Balance/DT_PlayerVitalsTable.DT_PlayerVitalsTable"));
	const FPlayerVitalsRow* const Row = VitalsTable != nullptr
		? VitalsTable->FindRow<FPlayerVitalsRow>(TEXT("Player"), TEXT("BackpackHeal")) : nullptr;
	if (!TestNotNull(TEXT("체력 표 Player 행"), Row))
	{
		return false;
	}

	FFakeServer Server;
	Server.Install();
	FRig Rig;
	UPlayerVitalsComponent* const Vitals = NewObject<UPlayerVitalsComponent>(Rig.Owner);
	Vitals->InitializeFromRow(*Row);
	Vitals->KillForCheck();
	TestFalse(TEXT("죽은 채로는 안 찬다"), Vitals->ApplyHeal(TestHeal));
	Vitals->ReviveAtRespawnValues();
	Rig.Backpack->BindTargets(Rig.Backend, Vitals);
	const float Before = Vitals->GetHealth();

	TestTrue(TEXT("먹기 요청"), Rig.Backpack->RequestConsume(BerryID));
	TestEqual(TEXT("부른 함수 = ConsumeItem"), Server.LastFunction, FString(TEXT("ConsumeItem")));
	TestEqual(TEXT("보낸 itemId"), Server.LastArgs->GetStringField(TEXT("itemId")), BerryID.ToString());
	TestEqual(TEXT("답 전 체력 그대로"), Vitals->GetHealth(), Before);

	const TSharedPtr<FJsonObject> Healed = MakeShared<FJsonObject>();
	Healed->SetStringField(TEXT("itemId"), BerryID.ToString());
	Healed->SetNumberField(TEXT("heal"), TestHeal);
	Server.Answer(Healed);
	TestEqual(TEXT("응답 heal 만큼 찬다"), Vitals->GetHealth(), FMath::Min(Before + TestHeal, Vitals->GetMaxHealth()));
	TestEqual(TEXT("성공 뒤 다시 받는다"), Rig.Backpack->GetRefetchCount(), 1);

	// 반대 방향 — 거절이면 안 찬다, 최대치를 넘지 않는다.
	const float AfterHeal = Vitals->GetHealth();
	Rig.Backpack->RequestConsume(BerryID);
	Server.AnswerFail(FString(), TEXT("먹을 수 없는 아이템"));
	TestEqual(TEXT("거절이면 체력 그대로"), Vitals->GetHealth(), AfterHeal);
	Vitals->ApplyHeal(Vitals->GetMaxHealth() * 10.f);
	TestEqual(TEXT("최대치를 넘지 않는다"), Vitals->GetHealth(), Vitals->GetMaxHealth());
	AddInfo(FString::Printf(TEXT("[Backpack] 먹기 - 체력 %.0f → %.0f (heal %.0f)"), Before, AfterHeal, TestHeal));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackBuffTest, "Snarehaven.Backpack.Consume.AppliesTableBuffAfterServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackBuffTest::RunTest(const FString& Parameters)
{
	// DESIGN 13.6 — 버프만 있는 것(핫팩, heal 0)도 먹히고, 값·시간은 DT_ItemTable 행 그대로, 답 전·거절이면 안 걸린다.
	const FName HeatPackID = TEXT("item.heat_pack");
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, HeatPackID);
	const UDataTable* const VitalsTable =
		LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Balance/DT_PlayerVitalsTable.DT_PlayerVitalsTable"));
	const FPlayerVitalsRow* const VitalsRow = VitalsTable != nullptr
		? VitalsTable->FindRow<FPlayerVitalsRow>(TEXT("Player"), TEXT("BackpackBuff")) : nullptr;
	if (!TestNotNull(TEXT("핫팩 행"), Row) || !TestNotNull(TEXT("체력 표 Player 행"), VitalsRow)
		|| !TestTrue(TEXT("핫팩에 버프가 있다"), Row->BuffKind != EBuffKind::None && Row->BuffSeconds > 0.f))
	{
		return false;
	}

	FFakeServer Server;
	Server.Install();
	FRig Rig;
	UPlayerVitalsComponent* const Vitals = NewObject<UPlayerVitalsComponent>(Rig.Owner);
	Vitals->InitializeFromRow(*VitalsRow);
	Rig.Backpack->BindTargets(Rig.Backend, Vitals);

	const TSharedPtr<FJsonObject> Ate = MakeShared<FJsonObject>();
	Ate->SetStringField(TEXT("itemId"), HeatPackID.ToString());
	Ate->SetNumberField(TEXT("heal"), 0);

	Rig.Backpack->RequestConsume(HeatPackID);
	TestEqual(TEXT("답 전엔 버프 없음"), Vitals->GetBuffValue(Row->BuffKind), 0.f);
	Server.AnswerFail(FString(), TEXT("가지고 있지 않다"));
	TestEqual(TEXT("거절이면 버프 없음"), Vitals->GetBuffValue(Row->BuffKind), 0.f);

	Rig.Backpack->RequestConsume(HeatPackID);
	Server.Answer(Ate);
	TestEqual(TEXT("버프 값 = 표"), Vitals->GetBuffValue(Row->BuffKind), Row->BuffValue);
	TestEqual(TEXT("버프 시간 = 표"), Vitals->GetBuffSecondsLeft(Row->BuffKind), Row->BuffSeconds);

	// 버프 없는 것(산딸기)을 먹어도 걸린 버프는 그대로
	const TSharedPtr<FJsonObject> Berry = MakeShared<FJsonObject>();
	Berry->SetStringField(TEXT("itemId"), BerryID.ToString());
	Berry->SetNumberField(TEXT("heal"), TestHeal);
	Rig.Backpack->RequestConsume(BerryID);
	Server.Answer(Berry);
	TestEqual(TEXT("버프 없는 음식은 버프를 안 건드린다"), Vitals->GetBuffValue(Row->BuffKind), Row->BuffValue);
	AddInfo(FString::Printf(TEXT("[Backpack] 핫팩 먹기 - 버프 %d 값 %.2f · %.0f초"), static_cast<int32>(Row->BuffKind), Row->BuffValue, Row->BuffSeconds));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackHatOffTest, "Snarehaven.Backpack.Hat.WearAndTakeOffWaitForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackHatOffTest::RunTest(const FString& Parameters)
{
	// #0198 모자 벗기(13.5) — 메시 없는 모자도 서버에 적고, 쓴 모자를 다시 누르면 `itemId ""` 로 벗는다.
	// 버튼 글과 "쓴 모자" 는 서버가 받아들인 뒤에만 바뀐다(CLAUDE.md 8).
	const FName HatID = TEXT("item.farmer_hat");
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(HatID, 1) }), false);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr); // 겉모습(장비 컴포넌트) 없이도 서버에는 적는다
	Screen->SetPanelOpen(true);
	const int32 HatIndex = Screen->GetShownSlots().IndexOfByPredicate([&HatID](const FInventorySlot& Slot) { return Slot.ItemID == HatID; });
	if (!TestTrue(TEXT("모자 칸이 보인다"), HatIndex != INDEX_NONE))
	{
		return false;
	}
	Screen->SelectSlot(HatIndex);
	const UButton* const Equip = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("EquipButton")));
	const UTextBlock* const EquipText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("EquipButtonText")));
	const auto Label = [EquipText]()
	{
		const UTextBlock* const Text = EquipText;
		return Text != nullptr ? Text->GetText().ToString() : FString();
	};
	TestEqual(TEXT("처음엔 장착"), Label(), FString(TEXT("장착")));

	// 쓰기 — 답 전엔 쓴 모자 없음·글 그대로.
	Screen->PressEquip();
	TestEqual(TEXT("쓰기 = EquipHat"), Server.LastFunction, FString(TEXT("EquipHat")));
	TestEqual(TEXT("보낸 itemId = 모자"), Server.LastArgs.IsValid() ? Server.LastArgs->GetStringField(TEXT("itemId")) : FString(), HatID.ToString());
	TestTrue(TEXT("답 전엔 쓴 모자 없음"), Rig.Backpack->GetWornHatID().IsNone());
	TestEqual(TEXT("답 전 글 그대로"), Label(), FString(TEXT("장착")));
	const TSharedPtr<FJsonObject> Worn = MakeShared<FJsonObject>();
	Worn->SetStringField(TEXT("hat"), HatID.ToString());
	Server.Answer(Worn);
	TestEqual(TEXT("받아들인 뒤 쓴 모자"), Rig.Backpack->GetWornHatID(), HatID);
	TestEqual(TEXT("받아들인 뒤 벗기"), Label(), FString(TEXT("벗기")));

	// 벗기 거절 — 그대로 쓰고 있다.
	Screen->PressEquip();
	TestEqual(TEXT("벗기도 EquipHat"), Server.LastFunction, FString(TEXT("EquipHat")));
	TestEqual(TEXT("벗기 itemId = 빈 문자열"), Server.LastArgs.IsValid() ? Server.LastArgs->GetStringField(TEXT("itemId")) : FString(TEXT("?")), FString());
	TestEqual(TEXT("답 전엔 아직 쓰고 있다"), Rig.Backpack->GetWornHatID(), HatID);
	// 표에 없는 code 라 서버 사유 그대로 — `network` 는 이제 표 문구로 바뀐다(M26-02, `Snarehaven.UI.ServerFailureText`).
	Server.AnswerFail(TEXT("busy"), TEXT("서버에 닿지 못했다"));
	TestEqual(TEXT("거절이면 그대로 쓰고 있다"), Rig.Backpack->GetWornHatID(), HatID);
	TestEqual(TEXT("거절이면 글도 벗기"), Label(), FString(TEXT("벗기")));
	TestEqual(TEXT("거절 사유가 상태 줄에"), Screen->GetStatusText().ToString(), FString(TEXT("서버에 닿지 못했다")));

	// 벗기 성공.
	Screen->PressEquip();
	const TSharedPtr<FJsonObject> Off = MakeShared<FJsonObject>();
	Off->SetStringField(TEXT("hat"), FString());
	Server.Answer(Off);
	TestTrue(TEXT("받아들인 뒤 쓴 모자 없음"), Rig.Backpack->GetWornHatID().IsNone());
	TestEqual(TEXT("받아들인 뒤 다시 장착"), Label(), FString(TEXT("장착")));
	TestEqual(TEXT("서버 호출 = 쓰기 1 + 벗기 2"), Server.Calls, 3);
	AddInfo(FString::Printf(TEXT("[Backpack] 모자 - 호출 %d번 · 벗은 뒤 버튼 \"%s\""), Server.Calls, *Label()));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackCardFitTest, "Snarehaven.Backpack.Card.ButtonsFitNarrow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackCardFitTest::RunTest(const FString& Parameters)
{
	// #0199 카드 버튼 줄(사용·장착·놓기·버리기 — 빈 병이면 「사용」이 물 채우기)이 좁은 카드(312px, 버튼 ≈ 66px)에서
	// 글이 버튼 밖으로 넘치거나 잘리지 않는다. 슬레이트가 실제로 배치한 자리로 잰다(도감 ⑦과 같은 방식).
	const FName HatID = TEXT("item.farmer_hat");
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(HatID, 1) }), false);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
	Screen->SetPanelOpen(true);
	Screen->SelectSlot(0);
	UWidget* const CardWidget = Screen->WidgetTree->FindWidget(TEXT("SlotCard"));
	const TCHAR* const Names[] = { TEXT("UseButton"), TEXT("EquipButton"), TEXT("PlaceButton"), TEXT("DropButton") };
	for (const FVector2D ViewSize : {FVector2D(1440.f, 1080.f), FVector2D(1920.f, 1080.f), FVector2D(2340.f, 1080.f)})
	{
		TMap<TSharedRef<SWidget>, FArrangedWidget> Arranged;
		Slate->SlatePrepass(1.f);
		TFunction<void(const FArrangedWidget&)> Walk = [&Walk, &Arranged](const FArrangedWidget& Node)
		{
			Arranged.Add(Node.Widget, Node);
			FArrangedChildren Children(EVisibility::Visible);
			Node.Widget->ArrangeChildren(Node.Geometry, Children);
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				Walk(Children[Index]);
			}
		};
		Walk(FArrangedWidget(Slate, FGeometry::MakeRoot(ViewSize, FSlateLayoutTransform())));
		auto Find = [&Arranged](const UWidget* Target) -> const FArrangedWidget*
		{
			return Target != nullptr && Target->GetCachedWidget().IsValid() ? Arranged.Find(Target->GetCachedWidget().ToSharedRef()) : nullptr;
		};
		const FArrangedWidget* const CardNode = Find(CardWidget);
		if (!TestNotNull(*FString::Printf(TEXT("%.0f 폭 카드가 배치된다"), ViewSize.X), CardNode))
		{
			continue;
		}
		const FSlateRect CardRect = CardNode->Geometry.GetLayoutBoundingRect();
		TestTrue(*FString::Printf(TEXT("%.0f 폭 카드가 화면 안"), ViewSize.X),
			CardRect.Left >= 0.f && CardRect.Right <= ViewSize.X && CardRect.Top >= 0.f && CardRect.Bottom <= ViewSize.Y);
		for (const TCHAR* const Name : Names)
		{
			const FArrangedWidget* const ButtonNode = Find(Screen->WidgetTree->FindWidget(Name));
			const FArrangedWidget* const TextNode = Find(Screen->WidgetTree->FindWidget(FName(FString(Name) + TEXT("Text"))));
			if (!TestTrue(*FString::Printf(TEXT("%.0f 폭 %s 배치"), ViewSize.X, Name), ButtonNode != nullptr && TextNode != nullptr))
			{
				continue;
			}
			const FSlateRect ButtonRect = ButtonNode->Geometry.GetLayoutBoundingRect();
			const FSlateRect TextRect = TextNode->Geometry.GetLayoutBoundingRect();
			const FVector2D Want = TextNode->Widget->GetDesiredSize();
			const FVector2D Got = TextNode->Geometry.GetLocalSize();
			AddInfo(FString::Printf(TEXT("%.0f 폭: %sText 바람 %.0fx%.0f 받음 %.0fx%.0f 화면 [%.1f~%.1f] 버튼 [%.1f~%.1f]"),
				ViewSize.X, Name, Want.X, Want.Y, Got.X, Got.Y, TextRect.Left, TextRect.Right, ButtonRect.Left, ButtonRect.Right));
			TestTrue(*FString::Printf(TEXT("%.0f 폭 %s 글이 안 잘린다"), ViewSize.X, Name), Got.X + 0.5f >= Want.X && Got.Y + 0.5f >= Want.Y);
			TestTrue(*FString::Printf(TEXT("%.0f 폭 %s 글이 버튼 안"), ViewSize.X, Name),
				TextRect.Left >= ButtonRect.Left - 0.5f && TextRect.Right <= ButtonRect.Right + 0.5f
				&& TextRect.Top >= ButtonRect.Top - 0.5f && TextRect.Bottom <= ButtonRect.Bottom + 0.5f);
			TestTrue(*FString::Printf(TEXT("%.0f 폭 %s 버튼이 카드 안"), ViewSize.X, Name),
				ButtonRect.Left >= CardRect.Left - 0.5f && ButtonRect.Right <= CardRect.Right + 0.5f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackScrollTest, "Snarehaven.Backpack.Screen.ScrollKeepsPanelOnScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackScrollTest::RunTest(const FString& Parameters)
{
	// #0203 칸이 8줄(32칸)이어도 판·카드·상태 줄은 세로 1080 안이고, 마지막 칸은 스크롤 안에서 끝까지 내리면 보인다.
	// 전에는 판이 줄 수만큼 자라서 7줄(하이킹 배낭 28칸)부터 화면 밖으로 나갔다(#0199 남은 결함).
	constexpr int32 TestRows = 8;
	const int32 TestCells = TestRows * 4;
	const FVector2D ViewSize(1920.f, 1080.f);
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	// 나무 99개씩 32칸 — 서버 칸(20)보다 많으면 화면은 칸을 늘려 전부 보인다(넘친 배낭).
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, ServerStackMax * TestCells) }), false);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
	Screen->SetPanelOpen(true);
	Screen->SelectSlot(TestCells - 1); // 카드도 띄워 바닥을 잰다
	TestEqual(TEXT("칸 32개"), Screen->GetCellCount(), TestCells);

	UScrollBox* const Scroll = Cast<UScrollBox>(Screen->WidgetTree->FindWidget(TEXT("SlotScroll")));
	UWidget* const LastCell = Screen->WidgetTree->FindWidget(*FString::Printf(TEXT("Cell_%d"), TestCells - 1));
	if (!TestNotNull(TEXT("칸 스크롤"), Scroll) || !TestNotNull(TEXT("마지막 칸"), LastCell))
	{
		return false;
	}
	TestTrue(TEXT("마지막 칸이 스크롤 안에 있다"), LastCell->IsChildOf(Scroll));

	TMap<TSharedRef<SWidget>, FArrangedWidget> Arranged;
	auto Measure = [&Slate, &Arranged, &ViewSize]()
	{
		Arranged.Reset();
		Slate->SlatePrepass(1.f);
		TFunction<void(const FArrangedWidget&)> Walk = [&Walk, &Arranged](const FArrangedWidget& Node)
		{
			Arranged.Add(Node.Widget, Node);
			FArrangedChildren Children(EVisibility::Visible);
			Node.Widget->ArrangeChildren(Node.Geometry, Children);
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				Walk(Children[Index]);
			}
		};
		Walk(FArrangedWidget(Slate, FGeometry::MakeRoot(ViewSize, FSlateLayoutTransform())));
	};
	auto RectOf = [&Arranged](const UWidget* Target, FSlateRect& Out)
	{
		const FArrangedWidget* const Node = Target != nullptr && Target->GetCachedWidget().IsValid()
			? Arranged.Find(Target->GetCachedWidget().ToSharedRef()) : nullptr;
		if (Node != nullptr)
		{
			Out = Node->Geometry.GetLayoutBoundingRect();
		}
		return Node != nullptr;
	};

	Measure();
	for (const TCHAR* const Name : { TEXT("BackpackPanel"), TEXT("SlotCard") })
	{
		FSlateRect Rect;
		if (TestTrue(*FString::Printf(TEXT("%s 배치"), Name), RectOf(Screen->WidgetTree->FindWidget(Name), Rect)))
		{
			AddInfo(FString::Printf(TEXT("[Backpack] 8줄 %s 세로 %.0f~%.0f"), Name, Rect.Top, Rect.Bottom));
			TestTrue(*FString::Printf(TEXT("%s 바닥 ≤ 화면 높이"), Name), Rect.Bottom <= ViewSize.Y + 0.5f);
		}
	}
	FSlateRect ScrollRect;
	FSlateRect CellRect;
	if (!TestTrue(TEXT("스크롤 배치"), RectOf(Scroll, ScrollRect)) || !TestTrue(TEXT("마지막 칸 배치"), RectOf(LastCell, CellRect)))
	{
		return false;
	}
	TestTrue(TEXT("처음엔 마지막 칸이 스크롤 아래에 가려져 있다"), CellRect.Bottom > ScrollRect.Bottom + 0.5f);
	FSlateRect FirstRect;
	if (RectOf(Screen->WidgetTree->FindWidget(TEXT("Cell_0")), FirstRect))
	{
		AddInfo(FString::Printf(TEXT("[Backpack] 첫 칸 %.1fx%.1f (%.1f,%.1f) · 스크롤 %.1fx%.1f (%.1f,%.1f)"), FirstRect.GetSize().X, FirstRect.GetSize().Y,
			FirstRect.Left, FirstRect.Top, ScrollRect.GetSize().X, ScrollRect.GetSize().Y, ScrollRect.Left, ScrollRect.Top));
		// 시트 칸 52px (docs/reference/inventory_backpack.md) — 스크롤에 넣어도 칸이 줄지 않는다.
		TestTrue(TEXT("칸 크기 = 시트 52px"), FMath::IsNearlyEqual(FirstRect.GetSize().X, 52.f, 0.5f) && FMath::IsNearlyEqual(FirstRect.GetSize().Y, 52.f, 0.5f));
	}

	// 끝까지 내린다 — 오프셋은 슬레이트 Tick 에서 적용된다.
	Scroll->ScrollToEnd();
	const FArrangedWidget* const ScrollNode = Arranged.Find(Scroll->GetCachedWidget().ToSharedRef());
	for (int32 Step = 0; Step < 3; ++Step)
	{
		Scroll->GetCachedWidget()->Tick(ScrollNode->Geometry, Step, 1.f);
	}
	Measure();
	if (RectOf(Scroll, ScrollRect) && RectOf(LastCell, CellRect))
	{
		AddInfo(FString::Printf(TEXT("[Backpack] 끝까지 내린 뒤 마지막 칸 %.0f~%.0f · 스크롤 %.0f~%.0f"),
			CellRect.Top, CellRect.Bottom, ScrollRect.Top, ScrollRect.Bottom));
		TestTrue(TEXT("끝까지 내리면 마지막 칸이 스크롤 안에 보인다"),
			CellRect.Top >= ScrollRect.Top - 0.5f && CellRect.Bottom <= ScrollRect.Bottom + 0.5f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackFillHostTest, "Snarehaven.Backpack.Fill.HostChecksWater",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackFillHostTest::RunTest(const FString& Parameters)
{
	// #0203 물가 판정은 호스트가 자기 월드의 폰 위치로 한다 — 멀면 서버(`FillBottle`)를 부르지 않고 거절, 가까우면 1번.
	const FName BottleID = TEXT("item.empty_bottle");
	constexpr float WaterRangeCm = 400.f;   // DESIGN 13.6 물가 = `CastRangeCm`
	constexpr float TestMarginCm = 100.f;
	const FWaterActivityRow* const Water = SnareWater::LoadRow();
	if (!TestNotNull(TEXT("물 활동 표"), Water))
	{
		return false;
	}
	TestEqual(TEXT("물가 범위 = 400cm"), Water->CastRangeCm, WaterRangeCm);
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	const FVector SpotAt(Water->CastRangeCm + TestMarginCm, 0.f, 0.f);
	AFishingSpotActor* const Spot = Rig.World->SpawnActorDeferred<AFishingSpotActor>(AFishingSpotActor::StaticClass(), FTransform(SpotAt));
	if (!TestNotNull(TEXT("낚시 포인트"), Spot))
	{
		return false;
	}
	Spot->SpotId = TEXT("pond_1");
	Spot->FinishSpawning(FTransform(SpotAt));
	// 거절 사유는 화면 상태 줄로 본다(`OnReply` → `HandleReply`).
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);

	// 400cm 밖 — 청은 받아들이지만 호스트가 거절, 서버 호출 없음.
	Rig.Owner->SetActorLocation(FVector::ZeroVector);
	TestTrue(TEXT("멀어도 청은 보낸다"), Rig.Backpack->RequestFill(BottleID));
	TestEqual(TEXT("400cm 밖이면 서버 호출 없음"), Server.Calls, 0);
	TestEqual(TEXT("거절이면 다시 받기 없음"), Rig.Backpack->GetRefetchCount(), 0);
	TestEqual(TEXT("거절 사유가 상태 줄에"), Screen->GetStatusText().ToString(), FString(TEXT("물가에서만 채울 수 있다")));

	// 400cm 안 — 호스트가 인정하고 FillBottle 1번.
	Rig.Owner->SetActorLocation(FVector(2.f * TestMarginCm, 0.f, 0.f));
	TestTrue(TEXT("가까우면 청을 보낸다"), Rig.Backpack->RequestFill(BottleID));
	TestEqual(TEXT("400cm 안이면 서버 호출 1번"), Server.Calls, 1);
	TestEqual(TEXT("부른 함수 = FillBottle"), Server.LastFunction, FString(TEXT("FillBottle")));
	TestEqual(TEXT("보낸 itemId = 빈 병"), Server.LastArgs.IsValid() ? Server.LastArgs->GetStringField(TEXT("itemId")) : FString(), BottleID.ToString());
	TestEqual(TEXT("답 전엔 다시 받기 없음"), Rig.Backpack->GetRefetchCount(), 0);
	Server.Answer(MakeShared<FJsonObject>());
	TestEqual(TEXT("받아들인 뒤 다시 받기 1번"), Rig.Backpack->GetRefetchCount(), 1);
	AddInfo(FString::Printf(TEXT("[Backpack] 물 채우기 - 포인트 %.0fcm · 호출 %d번"), SpotAt.X, Server.Calls));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackTapSlopTest, "Snarehaven.Backpack.Screen.SelectOnReleaseWithinSlop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackTapSlopTest::RunTest(const FString& Parameters)
{
	// M13-20 터치로 칸 위에서 스크롤을 시작하면 그 칸이 먼저 골라졌다 — 누름은 기억만, 뗄 때 문턱(DESIGN 8.12) 안이면 고른다.
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, ServerStackMax * 2) }), false);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
	Screen->SetPanelOpen(true);
	const float Slop = UBackpackScreenWidget::GetTapSlopPx();
	TestTrue(TEXT("문턱 > 0"), Slop > 0.f);
	const FVector2D Press(400.f, 300.f);

	// 스크롤: 누른 뒤 문턱을 넘게 움직였다 뗌 → 선택 없음.
	Screen->BeginSlotPress(1, Press);
	TestEqual(TEXT("누름만으로는 안 골라진다"), Screen->GetSelectedIndex(), static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("문턱을 넘긴 뗌은 고르지 않는다"), Screen->EndSlotPress(Press + FVector2D(0.f, Slop + 1.f)));
	TestEqual(TEXT("스크롤한 뒤 선택 없음"), Screen->GetSelectedIndex(), static_cast<int32>(INDEX_NONE));

	// 탭: 문턱 안에서 뗌 → 그 칸. 같은 아이템(나무) 두 칸의 **뒤 칸**을 고른다 — M13-22 전엔 Redraw 가 앞 칸(0)으로 옮겼다.
	Screen->BeginSlotPress(1, Press);
	TestEqual(TEXT("누름만으로는 안 골라진다 (2)"), Screen->GetSelectedIndex(), static_cast<int32>(INDEX_NONE));
	TestTrue(TEXT("문턱 안 뗌은 고른다"), Screen->EndSlotPress(Press + FVector2D(Slop * 0.5f, 0.f)));
	TestEqual(TEXT("뗄 때 그 칸(뒤 칸 1)"), Screen->GetSelectedIndex(), 1);

	// 누름 없이 뗌(스크롤이 잡았다 놓은 것)은 아무것도 안 바꾼다.
	TestFalse(TEXT("누름 없는 뗌"), Screen->EndSlotPress(Press));
	TestEqual(TEXT("선택 그대로"), Screen->GetSelectedIndex(), 1);

	// 스냅샷이 다시 와도(Redraw) 뒤 칸 그대로. 칸이 하나로 줄면 같은 아이템을 따라 앞 칸으로.
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, ServerStackMax * 2) }), false);
	TestEqual(TEXT("Redraw 뒤에도 뒤 칸"), Screen->GetSelectedIndex(), 1);
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, ServerStackMax) }), false);
	TestEqual(TEXT("칸이 하나로 줄면 같은 아이템 칸"), Screen->GetSelectedIndex(), 0);
	AddInfo(FString::Printf(TEXT("[Backpack] 탭 문턱 %.1fpx · 뒤 칸 선택 유지"), Slop));
	return true;
}

namespace SnareBackpackTest
{
	// 슬레이트가 실제로 배치한 자리 — 위 시험들의 Measure 와 같은 방식(프리패스 → 자식 배치를 재귀로).
	void Arrange(const TSharedRef<SWidget>& Slate, const FVector2D& ViewSize, TMap<TSharedRef<SWidget>, FArrangedWidget>& Out)
	{
		Out.Reset();
		Slate->SlatePrepass(1.f);
		TFunction<void(const FArrangedWidget&)> Walk = [&Walk, &Out](const FArrangedWidget& Node)
		{
			Out.Add(Node.Widget, Node);
			FArrangedChildren Children(EVisibility::Visible);
			Node.Widget->ArrangeChildren(Node.Geometry, Children);
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				Walk(Children[Index]);
			}
		};
		Walk(FArrangedWidget(Slate, FGeometry::MakeRoot(ViewSize, FSlateLayoutTransform())));
	}

	bool RectOf(const TMap<TSharedRef<SWidget>, FArrangedWidget>& Arranged, const UWidget* Target, FSlateRect& Out)
	{
		const FArrangedWidget* const Node = Target != nullptr && Target->GetCachedWidget().IsValid()
			? Arranged.Find(Target->GetCachedWidget().ToSharedRef()) : nullptr;
		if (Node != nullptr)
		{
			Out = Node->Geometry.GetLayoutBoundingRect();
		}
		return Node != nullptr;
	}
}

namespace
{
	// 배낭 화면 위젯 하나(`WidgetName`)의 사각형이 사냥 HUD 공격·포획 버튼·체력·스태미나 막대와 몇 번 겹치는지
	// 세 가로 폭에서 잰다 (M13-22 알림 · M13-23 카드가 같은 잣대).
	int32 CountHudOverlaps(FAutomationTestBase& Test, const TCHAR* WidgetName, const TCHAR* Label)
	{
		FFakeServer Server;
		Server.Install();
		FRig Rig;
		Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, ServerStackMax * 20) }), false);
		UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
		UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(Rig.World, UHuntHudWidget::StaticClass());
		if (!Test.TestNotNull(TEXT("배낭 화면"), Screen) || !Test.TestNotNull(TEXT("사냥 HUD"), Hud))
		{
			return INDEX_NONE;
		}
		const TSharedRef<SWidget> ScreenSlate = Screen->TakeWidget();
		const TSharedRef<SWidget> HudSlate = Hud->TakeWidget();
		Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
		Screen->SetPanelOpen(true);
		Screen->SelectSlot(0);
		UWidget* const Target = Screen->WidgetTree->FindWidget(WidgetName);
		const TCHAR* const HudNames[] = { TEXT("AttackButton"), TEXT("CaptureButton"), TEXT("PlayerHealthBar"), TEXT("PlayerStaminaBar") };
		int32 Overlaps = 0;
		for (const FVector2D ViewSize : {FVector2D(1440.f, 1080.f), FVector2D(1920.f, 1080.f), FVector2D(2340.f, 1080.f)})
		{
			TMap<TSharedRef<SWidget>, FArrangedWidget> ScreenArranged;
			TMap<TSharedRef<SWidget>, FArrangedWidget> HudArranged;
			Arrange(ScreenSlate, ViewSize, ScreenArranged);
			Arrange(HudSlate, ViewSize, HudArranged);
			FSlateRect TargetRect;
			if (!Test.TestTrue(*FString::Printf(TEXT("%.0f 폭 %s 배치"), ViewSize.X, Label), RectOf(ScreenArranged, Target, TargetRect)))
			{
				++Overlaps;
				continue;
			}
			Test.TestTrue(*FString::Printf(TEXT("%.0f 폭 %s 화면 안"), ViewSize.X, Label), TargetRect.Top >= 0.f && TargetRect.Bottom <= ViewSize.Y
				&& TargetRect.Left >= 0.f && TargetRect.Right <= ViewSize.X);
			for (const TCHAR* const Name : HudNames)
			{
				FSlateRect HudRect;
				if (!Test.TestTrue(*FString::Printf(TEXT("%.0f 폭 %s 배치"), ViewSize.X, Name), RectOf(HudArranged, Hud->WidgetTree->FindWidget(Name), HudRect)))
				{
					continue;
				}
				const bool bOverlap = FSlateRect::DoRectanglesIntersect(TargetRect, HudRect);
				Overlaps += bOverlap ? 1 : 0;
				Test.AddInfo(FString::Printf(TEXT("[Backpack] %.0f 폭 %s (%.0f,%.0f)~(%.0f,%.0f) · %s (%.0f,%.0f)~(%.0f,%.0f) 교차 %d"),
					ViewSize.X, Label, TargetRect.Left, TargetRect.Top, TargetRect.Right, TargetRect.Bottom,
					Name, HudRect.Left, HudRect.Top, HudRect.Right, HudRect.Bottom, bOverlap ? 1 : 0));
			}
		}
		return Overlaps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackStatusClearsHudTest, "Snarehaven.Backpack.Screen.StatusLeavesNothingBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackStatusClearsHudTest::RunTest(const FString& Parameters)
{
	// INBOX 2026-09-29 ② — 배낭 위쪽에 결과 글자를 따로 띄웠더니 **배낭을 닫아도 남았다**(폰 캡처의 "장착했다").
	// 이제 결과 글은 판 안에 글자로 두지 않고 HUD 왼쪽 위 알림 카드(M15-02)로만 간다.
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(HatchetID, 1) }), false);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(Rig.World, UHuntHudWidget::StaticClass());
	UEquipmentComponent* const Equipment = NewObject<UEquipmentComponent>(Rig.Owner, TEXT("Equipment"));
	Equipment->RegisterComponent();
	if (!TestNotNull(TEXT("배낭 화면"), Screen) || !TestNotNull(TEXT("사냥 HUD"), Hud))
	{
		return false;
	}
	const TSharedRef<SWidget> ScreenSlate = Screen->TakeWidget();
	const TSharedRef<SWidget> HudSlate = Hud->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, Equipment);
	Screen->SetNoticeHud(Hud);
	Screen->SetPanelOpen(true);
	Screen->SelectSlot(0);
	TestNull(TEXT("판 위에 떠 있는 글자 위젯은 없다"), Screen->WidgetTree->FindWidget(TEXT("StatusText")));

	Screen->PressEquip();   // 몸이 없는 시험판 — 어느 쪽이든 결과 글이 한 줄 나온다
	const FString Message = Screen->GetStatusText().ToString();
	TestFalse(TEXT("결과 글이 있다"), Message.IsEmpty());
	// 성공 글은 크림 카드, 실패 글은 빨간 오류 알약(M26-04) — 몸 없는 시험판은 어느 쪽이든 나올 수 있다.
	const FString Shown = Hud->IsErrorBannerVisible() ? Hud->GetErrorBannerText().ToString() : Hud->GetCaptureBannerText().ToString();
	TestEqual(TEXT("결과 글이 HUD 왼쪽 위로 간다"), Shown, Message);

	Screen->SetPanelOpen(false);
	int32 Leftover = 0;
	Screen->WidgetTree->ForEachWidget([&Leftover, &Message](UWidget* Widget)
	{
		const UTextBlock* const Text = Cast<UTextBlock>(Widget);
		Leftover += (Text != nullptr && Text->GetText().ToString() == Message) ? 1 : 0;
	});
	TestEqual(TEXT("배낭을 닫으면 그 글자가 화면 트리에 남지 않는다"), Leftover, 0);

	Hud->ClearCaptureBanner();   // 알림 카드는 몇 초 뒤 스스로 접힌다(타이머는 시험에서 못 돌리므로 직접)
	Hud->ClearErrorBanner();
	TestTrue(TEXT("알림이 지나면 카드가 비었다"), Hud->GetCaptureBannerText().IsEmpty() && Hud->GetErrorBannerText().IsEmpty());
	AddInfo(FString::Printf(TEXT("[Backpack] 결과 글 '%s' → 알림 카드 · 닫은 뒤 남은 글자 위젯 %d"), *Message, Leftover));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackRanchHudShowsRefusalTest, "Snarehaven.Backpack.Screen.RanchHudShowsRefusal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackRanchHudShowsRefusalTest::RunTest(const FString& Parameters)
{
	// #0517 빈 칸 — 거점(`L_Ranch`)은 사냥 HUD 를 안 띄워 배낭 판 거절 글이 화면에 하나도 없었다.
	// 사냥 HUD 가 없으면 목장 HUD 거절 줄로 가고, 사냥 HUD 가 있으면 목장 HUD 엔 안 간다(한 글이 두 번 뜨지 않게).
	FRig Rig;
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	URanchHudWidget* const RanchHud = CreateWidget<URanchHudWidget>(Rig.World, URanchHudWidget::StaticClass());
	UHuntHudWidget* const HuntHud = CreateWidget<UHuntHudWidget>(Rig.World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen) || !TestNotNull(TEXT("목장 HUD"), RanchHud) || !TestNotNull(TEXT("사냥 HUD"), HuntHud))
	{
		return false;
	}
	const TSharedRef<SWidget> ScreenSlate = Screen->TakeWidget();
	const TSharedRef<SWidget> RanchSlate = RanchHud->TakeWidget();
	const TSharedRef<SWidget> HuntSlate = HuntHud->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
	Screen->SetNoticeRanchHud(RanchHud);

	Screen->PressReleasePet();   // 시험판 주인엔 목장 상호작용이 없다 — 판이 내는 거절 글 하나
	const FString Refusal = Screen->GetStatusText().ToString();
	TestFalse(TEXT("거절 글이 있다"), Refusal.IsEmpty());
	TestEqual(TEXT("사냥 HUD 가 없으면 거절 글이 목장 HUD 거절 줄로 간다"), RanchHud->GetFailureNoticeText().ToString(), Refusal);

	URanchHudWidget* const QuietRanchHud = CreateWidget<URanchHudWidget>(Rig.World, URanchHudWidget::StaticClass());
	const TSharedRef<SWidget> QuietSlate = QuietRanchHud->TakeWidget();
	Screen->SetNoticeRanchHud(QuietRanchHud);
	Screen->SetNoticeHud(HuntHud);
	Screen->PressReleasePet();
	TestEqual(TEXT("사냥 HUD 가 있으면 오류 알약으로 간다"), HuntHud->GetErrorBannerText().ToString(), Refusal);
	TestTrue(TEXT("사냥 HUD 가 있으면 목장 HUD 엔 안 간다"), QuietRanchHud->GetFailureNoticeText().IsEmpty());
	AddInfo(FString::Printf(TEXT("[Backpack] 거점 거절 글 '%s' → 목장 HUD 거절 줄"), *Refusal));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackCardClearsHudTest, "Snarehaven.Backpack.Screen.CardClearsHudBars",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackCardClearsHudTest::RunTest(const FString& Parameters)
{
	// M13-23 ① 칸을 고르면 뜨는 카드가 판 밑(1920 폭 y 784~934)에서 체력·스태미나 막대·공격 버튼을 덮었다.
	TestEqual(TEXT("카드와 HUD 버튼·막대 교차 0"), CountHudOverlaps(*this, TEXT("SlotCard"), TEXT("카드")), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackLastRowGapTest, "Snarehaven.Backpack.Screen.LastRowClearsScrollEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackLastRowGapTest::RunTest(const FString& Parameters)
{
	// M13-22 ④ 5줄 판의 5번째 줄 칸이 스크롤 아래 구분선(더 내릴 게 있을 때 뜨는 그림자)에 닿았다 —
	// 7줄(28칸)을 넣어 그림자가 뜨는 판에서 5번째 줄 칸 바닥 < 그림자 위 인지 잰다.
	constexpr int32 TestRows = 7;
	constexpr int32 VisibleRows = 5;
	const int32 TestCells = TestRows * 4;
	const FVector2D ViewSize(1920.f, 1080.f);
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, ServerStackMax * TestCells) }), false);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
	Screen->SetPanelOpen(true);
	UScrollBox* const Scroll = Cast<UScrollBox>(Screen->WidgetTree->FindWidget(TEXT("SlotScroll")));
	UWidget* const LastVisible = Screen->WidgetTree->FindWidget(*FString::Printf(TEXT("Cell_%d"), VisibleRows * 4 - 1));
	UWidget* const Bar = Screen->WidgetTree->FindWidget(TEXT("BalanceBarBox"));
	if (!TestNotNull(TEXT("칸 스크롤"), Scroll) || !TestNotNull(TEXT("5번째 줄 칸"), LastVisible) || !TestNotNull(TEXT("재화 바"), Bar))
	{
		return false;
	}
	TMap<TSharedRef<SWidget>, FArrangedWidget> Arranged;
	Arrange(Slate, ViewSize, Arranged);
	FSlateRect ScrollRect;
	FSlateRect CellRect;
	FSlateRect BarRect;
	if (!TestTrue(TEXT("배치"), RectOf(Arranged, Scroll, ScrollRect) && RectOf(Arranged, LastVisible, CellRect) && RectOf(Arranged, Bar, BarRect)))
	{
		return false;
	}
	// 구분선 = 스크롤 바닥에 붙은 그림자 이미지(SImage). 실제 배치에서 찾는다.
	float EdgeTop = TNumericLimits<float>::Max();
	for (const TPair<TSharedRef<SWidget>, FArrangedWidget>& Pair : Arranged)
	{
		const FSlateRect Rect = Pair.Value.Geometry.GetLayoutBoundingRect();
		if (Pair.Key->GetType() == FName(TEXT("SImage")) && FMath::IsNearlyEqual(Rect.Bottom, ScrollRect.Bottom, 0.5f)
			&& Rect.Left >= ScrollRect.Left - 0.5f && Rect.Right <= ScrollRect.Right + 0.5f && Rect.GetSize().Y > 0.f && Rect.GetSize().Y < CellRect.GetSize().Y)
		{
			EdgeTop = FMath::Min(EdgeTop, Rect.Top);
		}
	}
	if (!TestTrue(TEXT("스크롤 아래 구분선(그림자)을 찾았다"), EdgeTop < TNumericLimits<float>::Max()))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("[Backpack] 5번째 줄 칸 바닥 %.1f · 구분선 위 %.1f (틈 %.1f) · 스크롤 바닥 %.1f · 재화 바 위 %.1f"),
		CellRect.Bottom, EdgeTop, EdgeTop - CellRect.Bottom, ScrollRect.Bottom, BarRect.Top));
	TestTrue(TEXT("5번째 줄 칸 바닥 < 구분선 위"), CellRect.Bottom < EdgeTop);
	TestTrue(TEXT("코인 칩은 스크롤 위(머리 줄)"), BarRect.Bottom <= ScrollRect.Top + 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackSideTabsTest, "Snarehaven.Backpack.Screen.SideTabsOpenScreens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackSideTabsTest::RunTest(const FString& Parameters)
{
	// M13-29 — 세로 탭 장비·도감·제작이 켜져 있고, 누르면 있는 화면이 뜬다(새 화면 없음).
	// 도감은 서버 보상 조회(`GetCodexRewards`)까지 가야 백엔드가 붙은 것이다. 제작은 배낭 제작(어디서나, 8.10).
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, TestDropWood), MakeSlot(HikingBackpackID, 1) }), false);

	// 제작 탭은 폰의 `USnareBaseBuildComponent` 가 로컬 컨트롤러로 판을 만든다 — 폰·로컬 플레이어를 단다.
	APawn* const Pawn = Rig.World->SpawnActor<APawn>();
	APlayerController* const Controller = Rig.World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("폰"), Pawn) || !TestNotNull(TEXT("컨트롤러"), Controller))
	{
		return false;
	}
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Pawn->Controller = Controller;
	UBackpackComponent* const PawnBackpack = NewObject<UBackpackComponent>(Pawn, TEXT("PawnBackpack"));
	PawnBackpack->RegisterComponent();
	USnareBaseBuildComponent* const Build = NewObject<USnareBaseBuildComponent>(Pawn, TEXT("PawnBuild"));
	Build->RegisterComponent();

	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		Controller->Player = nullptr;
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, PawnBackpack, nullptr);
	UButton* Tabs[5] = {};
	for (int32 Index = 0; Index < 5; ++Index)
	{
		Tabs[Index] = Cast<UButton>(Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("Tab_%d"), Index))));
		TestTrue(FString::Printf(TEXT("탭 %d 이 켜져 있다"), Index), Tabs[Index] != nullptr && Tabs[Index]->GetIsEnabled());
	}
	if (Tabs[1] == nullptr || Tabs[2] == nullptr || Tabs[3] == nullptr)
	{
		Controller->Player = nullptr;
		return false;
	}

	// 장비 — 같은 판을 장착할 수 있는 것만 보이게 연다. 다시 누르면 닫힌다.
	Tabs[1]->OnClicked.Broadcast();
	const auto Shows = [Screen](FName ItemID)
	{
		return Screen->GetShownSlots().ContainsByPredicate([ItemID](const FInventorySlot& Slot) { return Slot.ItemID == ItemID; });
	};
	TestTrue(TEXT("장비 탭 = 판 열림"), Screen->IsPanelOpen() && Screen->IsGearView());
	TestTrue(TEXT("장비 보기에 등산 가방"), Shows(HikingBackpackID));
	TestFalse(TEXT("장비 보기에 나무 없음"), Shows(WoodID));
	const int32 GearShown = Screen->GetShownSlots().Num();
	const UTextBlock* const Title = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("TitleText")));
	TestEqual(TEXT("제목 = 장비"), Title != nullptr ? Title->GetText().ToString() : FString(), FString(TEXT("장비")));
	Tabs[0]->OnClicked.Broadcast();
	TestTrue(TEXT("배낭 탭 = 배낭 보기로 바뀐다"), Screen->IsPanelOpen() && !Screen->IsGearView() && Shows(WoodID));
	Tabs[0]->OnClicked.Broadcast();
	TestFalse(TEXT("같은 탭 다시 = 닫힘"), Screen->IsPanelOpen());

	// 도감 — 도감 화면을 만들어 백엔드를 붙인다(보상 조회가 서버로 간다).
	Tabs[2]->OnClicked.Broadcast();
	TestNotNull(TEXT("도감 탭 = 도감 화면"), Screen->GetCodexScreen());
	TestEqual(TEXT("도감 화면이 서버에 보상을 묻는다"), Server.LastFunction, FString(TEXT("GetCodexRewards")));

	// 제작 — 배낭 제작 목록(맨손)으로 연다. 배낭 판이 열린 채 눌러도 제작 판 뒤에 겹쳐 남지 않는다 (M22 ③).
	Tabs[0]->OnClicked.Broadcast();
	TestTrue(TEXT("제작 누르기 전 배낭 판 열림"), Screen->IsPanelOpen());
	Tabs[3]->OnClicked.Broadcast();
	const UCraftingScreenWidget* const Craft = Build->GetCraftingScreen();
	TestTrue(TEXT("제작 탭 = 제작 판 열림"), Craft != nullptr && Craft->IsOpen());
	TestFalse(TEXT("제작 판이 뜨면 배낭 판은 접힌다"), Screen->IsPanelOpen());
	TestTrue(TEXT("제작 판 = 배낭 제작"), Craft != nullptr && Craft->GetCraftPlace() == ESnareCraftPlace::Backpack);
	AddInfo(FString::Printf(TEXT("[Backpack] 세로 탭 - 장비 보기 %d칸 · 서버 호출 %d번 · 제작 판 %s"),
		GearShown, Server.Calls, Craft != nullptr && Craft->IsOpen() ? TEXT("열림") : TEXT("없음")));
	Controller->Player = nullptr;
	return true;
}

// #0503 3층 결함 — 거점 상인 곁에서 배낭을 열면 상인 「상점 열기」 카드(HUD Z 로 붙음)가 세로 탭 위에 겹쳤다.
// 연 배낭 판은 전체 화면 판 Z 라 카드 위, 닫힌 배낭은 HUD Z(지도와 같은 꼴). 시험 월드엔 뷰포트가 없어 붙이기는 못 하고
// `AttachTo`·`SetPanelOpen` 이 붙일 때 쓰는 Z 값을 연·닫힌 상태로 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackOpenAboveVendorCardTest, "Snarehaven.Backpack.Screen.OpenPanelAboveVendorCard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackOpenAboveVendorCardTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	Screen->TakeWidget();
	const int32 ClosedZ = Screen->GetViewportZOrder();
	Screen->SetPanelOpen(true);
	const int32 OpenZ = Screen->GetViewportZOrder();
	Screen->SetPanelOpen(false);
	const int32 ReclosedZ = Screen->GetViewportZOrder();

	// 상인 카드 = `AShopVendorActor` 가 Z 없이 붙이는 상점 화면(기본 Z 0 = HUD Z).
	const int32 VendorCardZ = SnareUiStyle::HudViewportZOrder;
	TestEqual(TEXT("닫힌 배낭 = HUD Z"), ClosedZ, SnareUiStyle::HudViewportZOrder);
	TestEqual(TEXT("연 배낭 = 전체 화면 판 Z(도감·안내서·지도와 같은 값)"), OpenZ, SnareUiStyle::ScreenPanelViewportZOrder);
	TestTrue(TEXT("연 배낭 Z > 상인 「상점 열기」 카드 Z"), OpenZ > VendorCardZ);
	TestEqual(TEXT("다시 닫으면 HUD Z"), ReclosedZ, SnareUiStyle::HudViewportZOrder);

	AddInfo(FString::Printf(TEXT("[BackpackZ] 닫힘 %d · 열림 %d · 다시 닫힘 %d · 상인 카드 %d"),
		ClosedZ, OpenZ, ReclosedZ, VendorCardZ));
	return true;
}

// M24-01 세로 탭 = 52 크림 네모 안 그림 + 지금 탭만 노란 테 (시트 `inventory_backpack.md` 「왼쪽 세로 탭」).
// 글자 버튼이면 그림이 0 이고, 테를 안 옮기면 장비 탭을 눌러도 노랑이 배낭 탭에 남는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackSideTabIconsTest, "Snarehaven.Backpack.Screen.SideTabsShowIcons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackSideTabIconsTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, TestDropWood) }), false);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);

	const FLinearColor Yellow = SnareUiStyle::UiBoltYellow();
	const auto FrameOf = [Screen](int32 Index)
	{
		return Cast<UBorder>(Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("TabFrame_%d"), Index))));
	};
	const auto YellowFrames = [&FrameOf, &Yellow](int32& OutIndex)
	{
		int32 Count = 0;
		OutIndex = INDEX_NONE;
		for (int32 Index = 0; Index < 5; ++Index)
		{
			const UBorder* const Frame = FrameOf(Index);
			if (Frame != nullptr && Frame->GetBrushColor().Equals(Yellow, 0.001f))
			{
				++Count;
				OutIndex = Index;
			}
		}
		return Count;
	};

	int32 Icons = 0;
	int32 Boxes52 = 0;
	for (int32 Index = 0; Index < 5; ++Index)
	{
		const UImage* const Icon = Cast<UImage>(Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("TabIcon_%d"), Index))));
		const UTexture2D* const Texture = Icon != nullptr ? Cast<UTexture2D>(Icon->GetBrush().GetResourceObject()) : nullptr;
		TestNotNull(FString::Printf(TEXT("탭 %d 그림 텍스처"), Index), Texture);
		Icons += Texture != nullptr ? 1 : 0;
		const USizeBox* const Box = Cast<USizeBox>(Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("TabBox_%d"), Index))));
		// 10-16 시트 52 → 82: 테 안 버튼 76 × 판 배율 1.6 ≥ 애플 44pt(121) (DESIGN 2).
		const bool b52 = Box != nullptr && FMath::IsNearlyEqual(Box->GetWidthOverride(), 82.f) && FMath::IsNearlyEqual(Box->GetHeightOverride(), 82.f);
		TestTrue(FString::Printf(TEXT("탭 %d 네모 82"), Index), b52);
		Boxes52 += b52 ? 1 : 0;
		const UButton* const Tab = Cast<UButton>(Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("Tab_%d"), Index))));
		TestTrue(FString::Printf(TEXT("탭 %d 버튼 안이 글자가 아니라 그림"), Index), Tab != nullptr && Tab->GetContent() == Icon);
	}

	UButton* const BagTab = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("Tab_0")));
	UButton* const GearTab = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("Tab_1")));
	if (!TestNotNull(TEXT("배낭 탭"), BagTab) || !TestNotNull(TEXT("장비 탭"), GearTab))
	{
		return false;
	}
	int32 Lit = INDEX_NONE;
	BagTab->OnClicked.Broadcast();
	const int32 BagCount = YellowFrames(Lit);
	TestEqual(TEXT("배낭 보기 = 노란 테 하나"), BagCount, 1);
	TestEqual(TEXT("배낭 보기 = 노란 테는 배낭 탭"), Lit, 0);
	const int32 BagLit = Lit;
	GearTab->OnClicked.Broadcast();
	const int32 GearCount = YellowFrames(Lit);
	TestEqual(TEXT("장비 보기 = 노란 테 하나"), GearCount, 1);
	TestEqual(TEXT("장비 탭 누르면 테가 장비 탭으로"), Lit, 1);
	TestEqual(TEXT("GetCurrentTabIndex = 장비"), Screen->GetCurrentTabIndex(), 1);
	AddInfo(FString::Printf(TEXT("[SideTabIcons] 그림 %d · 52 네모 %d · 배낭 보기 노란 테 %d개(탭 %d) · 장비 보기 노란 테 %d개(탭 %d)"),
		Icons, Boxes52, BagCount, BagLit, GearCount, Lit));
	return true;
}

// ⑧ M15-07 설치물 카드 「놓기」 (INBOX 2026-09-25 ①) — 카드 버튼이 놓기 모드로 가고, 못 놓으면 까닭 한 줄이 남는다.
// 전에는 `PlaceButton->SetIsEnabled(false)` 로 늘 꺼져 있어 텐트·불·보관함을 제작 → 놓기 탭으로만 놓을 수 있었다.
// 놓기 판정은 게임 인스턴스의 `USnareBaseSubsystem` 을 거치므로 월드를 게임 인스턴스가 만들게 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackPlaceTest, "Snarehaven.Backpack.Place.CardStartsPlacingOrTellsWhy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackPlaceTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	USnareBackendSubsystem* const Backend = GameInstance->GetSubsystem<USnareBackendSubsystem>();
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("월드"), World) || !TestNotNull(TEXT("백엔드 창구"), Backend) || !TestNotNull(TEXT("기지 창구"), Base))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("플레이어"), Player))
	{
		return false;
	}
	// 조종을 붙이기 전에 BeginPlay 를 돌린다 — 물 표는 BeginPlay 가 읽고, 건축 HUD 는 조종이 있어야 뜬다(검사엔 필요 없다).
	Player->DispatchBeginPlay();
	APlayerController* const Controller = World->SpawnActor<APlayerController>(Params);
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Player->Controller = Controller;

	USnareBaseBuildComponent* const Build = Player->FindComponentByClass<USnareBaseBuildComponent>();
	UBackpackComponent* const Backpack = Player->FindComponentByClass<UBackpackComponent>();
	UWaterTravelComponent* const Water = Player->FindComponentByClass<UWaterTravelComponent>();
	if (!TestNotNull(TEXT("건축 컴포넌트"), Build) || !TestNotNull(TEXT("배낭 컴포넌트"), Backpack)
		|| !TestNotNull(TEXT("물 컴포넌트"), Water))
	{
		Controller->Player = nullptr;
		return false;
	}
	Backpack->BindTargets(Backend, nullptr);
	Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(TentID, 1), MakeSlot(StorageBoxID, 1),
		MakeSlot(LifeboatID, 1), MakeSlot(WoodID, TestDropWood) }), false);

	// ── 까닭 (선 자리·텐트 유무·권한에 따라 다른 한 줄) ──
	TestFalse(TEXT("게임모드가 없으면 못 짓는 까닭이 있다"), Build->GetPlaceBlockReason(TentID).IsEmpty());
	AGameStateBase* const GameState = World->SpawnActor<AGameStateBase>(Params);
	World->SetGameState(GameState);
	GameState->GameModeClass = ASnarehavenGameMode::StaticClass();
	TestEqual(TEXT("사냥 섬이면 존이 hunt"), USnareBaseBuildComponent::GetTentZone(World), SnareBaseZones::Hunt);

	const FText TentReason = Build->GetPlaceBlockReason(TentID);
	const FText BoxNoTentReason = Build->GetPlaceBlockReason(StorageBoxID);
	TestTrue(TEXT("텐트는 땅만 있으면 놓는다"), TentReason.IsEmpty());
	TestTrue(TEXT("텐트가 없으면 보관함은 텐트를 먼저 하라고 한다"),
		BoxNoTentReason.ToString().Contains(TEXT("텐트")));

	// ── 카드 「놓기」 (못 놓을 때: 까닭이 화면 글로) ──
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		Controller->Player = nullptr;
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Backend, Backpack, nullptr);
	Screen->SetPanelOpen(true);
	UButton* const PlaceButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("PlaceButton")));
	if (!TestNotNull(TEXT("놓기 버튼"), PlaceButton))
	{
		Controller->Player = nullptr;
		return false;
	}
	const auto Select = [Screen](FName ItemID)
	{
		const int32 Index = Screen->GetShownSlots().IndexOfByPredicate(
			[ItemID](const FInventorySlot& Slot) { return Slot.ItemID == ItemID; });
		if (Index != Screen->GetSelectedIndex())
		{
			Screen->SelectSlot(Index);   // 같은 칸을 다시 누르면 카드가 닫힌다 — 이미 골랐으면 그대로 둔다
		}
		return Index != INDEX_NONE && Screen->GetSelectedIndex() == Index;
	};

	TestTrue(TEXT("나무 칸을 고른다"), Select(WoodID));
	TestFalse(TEXT("재료 카드의 놓기는 꺼져 있다"), PlaceButton->GetIsEnabled());

	TestTrue(TEXT("보관함 칸을 고른다"), Select(StorageBoxID));
	TestTrue(TEXT("설치물 카드의 놓기는 켜져 있다"), PlaceButton->GetIsEnabled());
	PlaceButton->OnClicked.Broadcast();
	TestFalse(TEXT("텐트 없이는 놓기 모드로 안 간다"), Build->IsPlacing());
	TestEqual(TEXT("까닭이 화면 글로 남는다"), Screen->GetStatusText().ToString(), BoxNoTentReason.ToString());
	TestTrue(TEXT("까닭이 남으면 판은 열린 채"), Screen->IsPanelOpen());

	// ── 카드 「놓기」 (놓을 수 있을 때: 미리보기 시작 + 판 닫기) ──
	TestTrue(TEXT("텐트 칸을 고른다"), Select(TentID));
	const int32 CallsBeforePlace = Server.Calls;
	PlaceButton->OnClicked.Broadcast();
	TestTrue(TEXT("놓기 모드가 켜진다"), Build->IsPlacing());
	TestEqual(TEXT("놓는 것은 고른 칸의 아이템"), Build->GetPlacingItemID(), TentID);
	TestFalse(TEXT("미리보기를 보게 판을 닫는다"), Screen->IsPanelOpen());
	TestEqual(TEXT("놓기 모드만으로는 서버를 부르지 않는다 (확정이 부른다)"), Server.Calls, CallsBeforePlace);
	Build->CancelPlacement();

	// 텐트가 서면 보관함도 까닭이 사라진다 (서버 `PlaceStructure` 와 같은 줄).
	FSnareTentMirror Tent;
	Tent.TentId = TEXT("tent_place_test");
	Tent.Zone = SnareBaseZones::Hunt;
	Tent.PlacedAtUtc = FDateTime::UtcNow().ToIso8601();
	Base->AddTentMirrorForTest(Tent);
	TestTrue(TEXT("텐트가 서면 보관함도 놓인다"), Build->GetPlaceBlockReason(StorageBoxID).IsEmpty());

	// ── 물 위 · 내 몸이 아님 ──
	ASeaActor* const Sea = World->SpawnActor<ASeaActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	if (Sea != nullptr && !Sea->HasActorBegunPlay())
	{
		Sea->DispatchBeginPlay();
	}
	const FVector OverSea(Sea != nullptr ? Sea->IslandHalfExtentCm + 500.f : 3000.f, 0.f, 0.f);
	Player->SetActorLocation(OverSea);
	TestTrue(TEXT("물 위에서는 까닭이 물 얘기다"), Build->GetPlaceBlockReason(TentID).ToString().Contains(TEXT("물")));
	Player->SetActorLocation(FVector::ZeroVector);
	TestTrue(TEXT("뭍으로 돌아오면 다시 놓인다"), Build->GetPlaceBlockReason(TentID).IsEmpty());
	// M15-14 부터 게스트도 놓는다 — 막히는 것은 **내가 조종하지 않는 몸**(남의 복제본)뿐이다.
	Player->Controller = nullptr;
	TestTrue(TEXT("남의 몸이면 내 캐릭터 얘기다"), Build->GetPlaceBlockReason(TentID).ToString().Contains(TEXT("내 캐릭터")));
	Player->Controller = Controller;
	Player->SetRole(ROLE_AutonomousProxy);
	TestTrue(TEXT("게스트는 막지 않는다"), Build->GetPlaceBlockReason(TentID).IsEmpty());
	Player->SetRole(ROLE_Authority);

	// ── 구명보트 = 물 위에 띄운다 (DESIGN 8.9) ──
	TestTrue(TEXT("구명보트 칸을 고른다"), Select(LifeboatID));
	TestTrue(TEXT("배 카드의 놓기도 켜져 있다 (격자 칸 0 이어도)"), PlaceButton->GetIsEnabled());
	const int32 CallsBeforeBoat = Server.Calls;
	PlaceButton->OnClicked.Broadcast();
	TestEqual(TEXT("뭍에서는 서버를 안 부른다"), Server.Calls, CallsBeforeBoat);
	TestTrue(TEXT("뭍에서는 물가에서만 띄운다고 말한다"), Screen->GetStatusText().ToString().Contains(TEXT("물가")));

	Player->SetActorLocation(OverSea);
	Screen->SetPanelOpen(true);
	TestTrue(TEXT("물 위에서 구명보트 칸을 다시 고른다"), Select(LifeboatID));
	TestTrue(TEXT("물 위면 띄울 수 있다"), Water->CanLaunchBoat());
	PlaceButton->OnClicked.Broadcast();
	TestEqual(TEXT("배는 서버 `LaunchBoat` 가 판정한다"), Server.LastFunction, FString(TEXT("LaunchBoat")));
	TestEqual(TEXT("배 요청은 한 번"), Server.Calls, CallsBeforeBoat + 1);
	AddInfo(FString::Printf(TEXT("[Backpack] 놓기 - 보관함 까닭 '%s' · 텐트 놓기 모드 %s · 배 서버 호출 %s"),
		*BoxNoTentReason.ToString(), Build->GetPlacingItemID().IsNone() ? TEXT("걷힘") : TEXT("켜짐"), *Server.LastFunction));

	Build->CancelPlacement();
	Controller->Player = nullptr;
	Player->Controller = nullptr;
	GameInstance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// M15-08(INBOX ②) — 가방 카드에서 **든 도구를 다시 누르면 내려놓는지**. 전에는 같은 카드를 눌러도
// 또 장착이라 한 번 든 도구를 가방에서 내릴 길이 없었다 (모자만 벗겨졌다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackEquipToggleTest, "Snarehaven.Backpack.Equip.CardTogglesGear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackEquipToggleTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	USnareBackendSubsystem* const Backend = GameInstance->GetSubsystem<USnareBackendSubsystem>();
	if (!TestNotNull(TEXT("월드"), World) || !TestNotNull(TEXT("백엔드 창구"), Backend))
	{
		return false;
	}
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	UEquipmentComponent* const Equipment = Player != nullptr ? Player->GetEquipment() : nullptr;
	UBackpackComponent* const Backpack = Player != nullptr ? Player->FindComponentByClass<UBackpackComponent>() : nullptr;
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("장비 컴포넌트"), Equipment)
		|| !TestNotNull(TEXT("배낭 컴포넌트"), Backpack))
	{
		GameInstance->Shutdown();
		return false;
	}
	TestTrue(TEXT("사람 메시를 올렸다"), Player->ApplyPlayerBody());   // 소켓이 없으면 손에 붙지 않는다
	Backpack->BindTargets(Backend, nullptr);
	Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(HatchetID, 1), MakeSlot(WoodID, TestDropWood) }), false);

	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		GameInstance->Shutdown();
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Backend, Backpack, Equipment);
	Screen->SetPanelOpen(true);
	UButton* const EquipButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("EquipButton")));
	UTextBlock* const EquipText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("EquipButtonText")));
	if (!TestNotNull(TEXT("장착 버튼"), EquipButton) || !TestNotNull(TEXT("장착 버튼 글자"), EquipText))
	{
		GameInstance->Shutdown();
		return false;
	}
	const auto Select = [Screen](FName ItemID)
	{
		const int32 Index = Screen->GetShownSlots().IndexOfByPredicate(
			[ItemID](const FInventorySlot& Slot) { return Slot.ItemID == ItemID; });
		if (Index != Screen->GetSelectedIndex())
		{
			Screen->SelectSlot(Index);   // 같은 칸을 다시 누르면 카드가 닫힌다
		}
		return Index != INDEX_NONE && Screen->GetSelectedIndex() == Index;
	};

	TestTrue(TEXT("나무 칸을 고른다"), Select(WoodID));
	TestFalse(TEXT("재료 카드의 장착은 꺼져 있다"), EquipButton->GetIsEnabled());

	TestTrue(TEXT("손도끼 칸을 고른다"), Select(HatchetID));
	TestTrue(TEXT("장비 카드의 장착은 켜져 있다"), EquipButton->GetIsEnabled());
	TestEqual(TEXT("들기 전 글자는 `장착`"), EquipText->GetText().ToString(), FString(TEXT("장착")));

	const int32 CallsBeforeEquip = Server.Calls;
	EquipButton->OnClicked.Broadcast();
	const FName EquippedRow = Equipment->GetEquippedItem(EEquipSlot::Tool);
	const FString TextWhileHeld = EquipText->GetText().ToString();
	TestFalse(TEXT("누르면 도구 칸에 들린다"), EquippedRow.IsNone());
	TestEqual(TEXT("겉모습뿐이라 서버를 안 부른다"), Server.Calls, CallsBeforeEquip);
	TestEqual(TEXT("든 뒤 글자는 `내리기`"), EquipText->GetText().ToString(), FString(TEXT("내리기")));
	// INBOX 2026-09-29 ②-b — 든 것은 칸에 `착용` 딱지, 카드 이름에 (착용중).
	const FName WornTagName(*FString::Printf(TEXT("CellWornTag_%d"), Screen->GetSelectedIndex()));
	TestNotNull(TEXT("든 칸에 착용 딱지"), Screen->WidgetTree->FindWidget(WornTagName));
	const UTextBlock* const CardName = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("CardNameText")));
	TestTrue(TEXT("카드 이름에 착용중"), CardName != nullptr && CardName->GetText().ToString().Contains(TEXT("착용중")));

	EquipButton->OnClicked.Broadcast();
	TestTrue(TEXT("다시 누르면 내려놓는다"), Equipment->GetEquippedItem(EEquipSlot::Tool).IsNone());
	TestEqual(TEXT("내린 뒤 글자는 다시 `장착`"), EquipText->GetText().ToString(), FString(TEXT("장착")));
	TestNull(TEXT("내리면 착용 딱지가 사라진다"), Screen->WidgetTree->FindWidget(WornTagName));
	TestEqual(TEXT("내릴 때도 서버는 안 부른다"), Server.Calls, CallsBeforeEquip);
	AddInfo(FString::Printf(TEXT("[Backpack] 장착 토글 - 누름1 → 도구 칸 %s · 버튼 글자 \"%s\" · 누름2 → 도구 칸 %s · 버튼 글자 \"%s\" · 서버 호출 %d회"),
		*EquippedRow.ToString(), *TextWhileHeld,
		Equipment->GetEquippedItem(EEquipSlot::Tool).IsNone() ? TEXT("빔") : TEXT("남음"),
		*EquipText->GetText().ToString(), Server.Calls - CallsBeforeEquip));

	GameInstance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// M15-09(INBOX ③) — 아무 버튼도 못 누르는 칸에서 **왜 못 누르는지 카드가 말하는지**. 폰에는 꺼진 버튼의
// 툴팁이 안 보여, 테스터 키트 24종을 눌러 본 사용자에게는 "쓰이는 게 없다" 로만 보였다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackCardHintTest, "Snarehaven.Backpack.Card.TellsWhyButtonsAreOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackCardHintTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();      // 레시피 표를 든 `USnareBaseSubsystem` 도 이때 선다
	UWorld* const World = GameInstance->GetWorld();
	USnareBackendSubsystem* const Backend = GameInstance->GetSubsystem<USnareBackendSubsystem>();
	USnareBaseSubsystem* const Base = GameInstance->GetSubsystem<USnareBaseSubsystem>();
	if (!TestNotNull(TEXT("월드"), World) || !TestNotNull(TEXT("백엔드 창구"), Backend)
		|| !TestNotNull(TEXT("거점 서브시스템"), Base) || !TestNotNull(TEXT("레시피 표"), Base->GetRecipeTable()))
	{
		GameInstance->Shutdown();
		return false;
	}
	World->InitializeActorsForPlay(FURL());

	// 테스터 키트(24종)에서 종류를 대표하는 다섯 — 재료 둘(제작에 쓰는 나뭇가지 · 팔기만 하는 안장) + 누를 것이 있는 셋.
	const FName SaddleID = TEXT("item.saddle_hunting");
	const FName CookedID = TEXT("item.cooked_reindeer");
	Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, TestDropWood), MakeSlot(SaddleID, 1),
		MakeSlot(HatchetID, 1), MakeSlot(TentID, 1), MakeSlot(CookedID, 1) }), false);

	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		GameInstance->Shutdown();
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Backend, nullptr, nullptr);
	Screen->SetPanelOpen(true);
	UTextBlock* const HintText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("CardHintText")));
	UButton* const UseButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("UseButton")));
	UButton* const EquipButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("EquipButton")));
	UButton* const PlaceButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("PlaceButton")));
	if (!TestNotNull(TEXT("까닭 줄"), HintText) || !TestNotNull(TEXT("사용 버튼"), UseButton)
		|| !TestNotNull(TEXT("장착 버튼"), EquipButton) || !TestNotNull(TEXT("놓기 버튼"), PlaceButton))
	{
		GameInstance->Shutdown();
		return false;
	}
	const auto Select = [Screen](FName ItemID)
	{
		const int32 Index = Screen->GetShownSlots().IndexOfByPredicate(
			[ItemID](const FInventorySlot& Slot) { return Slot.ItemID == ItemID; });
		if (Index != Screen->GetSelectedIndex())
		{
			Screen->SelectSlot(Index);   // 같은 칸을 다시 누르면 카드가 닫힌다
		}
		return Index != INDEX_NONE && Screen->GetSelectedIndex() == Index;
	};

	// ── 재료: 버릴 수밖에 없는 칸이라 까닭이 뜬다 ──
	TestTrue(TEXT("나뭇가지 칸을 고른다"), Select(WoodID));
	TestFalse(TEXT("재료는 사용이 꺼져 있다"), UseButton->GetIsEnabled());
	TestFalse(TEXT("재료는 장착이 꺼져 있다"), EquipButton->GetIsEnabled());
	TestFalse(TEXT("재료는 놓기가 꺼져 있다"), PlaceButton->GetIsEnabled());
	const FString WoodHint = Screen->GetCardHintText().ToString();
	TestTrue(TEXT("나뭇가지는 제작에 쓴다고 말한다"), WoodHint.Contains(TEXT("제작")));
	TestFalse(TEXT("팔 수 없는 재료에 판매 얘기를 하지 않는다"), WoodHint.Contains(TEXT("잡화상")));
	TestEqual(TEXT("까닭이 있으면 줄이 보인다"), HintText->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);

	// M15-11 (INBOX ⑤) — 안장·목줄은 배낭에서 누를 것이 없다. "판다" 가 아니라 **잡화상에서 붙인다** 고 말해야 한다.
	TestTrue(TEXT("사냥 안장 칸을 고른다"), Select(SaddleID));
	const FString SaddleHint = Screen->GetCardHintText().ToString();
	TestTrue(TEXT("안장은 잡화상 얘기를 한다"), SaddleHint.Contains(TEXT("잡화상")));
	TestTrue(TEXT("안장은 동물에 붙인다고 말한다"), SaddleHint.Contains(TEXT("붙")));
	TestFalse(TEXT("제작에 안 쓰는 것에 제작 얘기를 하지 않는다"), SaddleHint.Contains(TEXT("제작")));
	// 목줄은 파는 값이 0 이라(도감 보상) 예전 규칙으로는 "쓸 데가 없다" 가 됐다 — 같은 잡화상 줄로 간다.
	FItemRow Collar;
	Collar.ItemID = TEXT("item.collar_hunting");
	Collar.ItemType = EItemType::Material;
	const FString CollarHint = UBackpackScreenWidget::MakeDisabledHint(&Collar, nullptr).ToString();
	TestTrue(TEXT("목줄도 잡화상 얘기를 한다"), CollarHint.Contains(TEXT("잡화상")));
	TestFalse(TEXT("목줄에 쓸 데가 없다고 말하지 않는다"), CollarHint.Contains(TEXT("쓸 데")));

	// ── 누를 것이 하나라도 있으면 까닭 줄은 접힌다 ──
	TestTrue(TEXT("손도끼 칸을 고른다"), Select(HatchetID));
	TestTrue(TEXT("장비는 장착이 켜져 있다"), EquipButton->GetIsEnabled());
	const bool bHatchetHintEmpty = Screen->GetCardHintText().IsEmpty();
	TestTrue(TEXT("누를 것이 있으면 까닭이 없다"), bHatchetHintEmpty);
	TestEqual(TEXT("까닭이 없으면 줄이 접힌다"), HintText->GetVisibility(), ESlateVisibility::Collapsed);

	TestTrue(TEXT("텐트 칸을 고른다"), Select(TentID));
	TestTrue(TEXT("설치물은 놓기가 켜져 있다"), PlaceButton->GetIsEnabled());
	const bool bTentHintEmpty = Screen->GetCardHintText().IsEmpty();
	TestTrue(TEXT("설치물에는 까닭이 없다"), bTentHintEmpty);

	TestTrue(TEXT("구운 고기 칸을 고른다"), Select(CookedID));
	TestTrue(TEXT("음식은 사용이 켜져 있다"), UseButton->GetIsEnabled());
	TestTrue(TEXT("음식에는 까닭이 없다"), Screen->GetCardHintText().IsEmpty());

	// ── 종류별 문구 (표 값만 바꿔 가며, 테스트 전용 행으로) ──
	UDataTable* const FakeRecipes = NewObject<UDataTable>(GetTransientPackage());
	FakeRecipes->RowStruct = FRecipeRow::StaticStruct();
	FRecipeRow TestRecipe;
	TestRecipe.RecipeID = TEXT("recipe.hint_test");
	FRecipeIngredient TestIngredient;
	TestIngredient.Tag = ECraftTag::Meat;      // 테스트 전용 조합 — 표가 바뀌어도 안 깨진다 (Verification 6)
	TestIngredient.Count = 1;
	TestRecipe.Ingredients.Add(TestIngredient);
	FakeRecipes->AddRow(TEXT("Recipe_HintTest"), TestRecipe);

	FItemRow Fake;
	Fake.ItemID = TEXT("item.hint_test");
	Fake.ItemType = EItemType::Material;
	Fake.CraftTag = ECraftTag::Meat;
	Fake.SellPrice = 1;
	const FString BothHint = UBackpackScreenWidget::MakeDisabledHint(&Fake, FakeRecipes).ToString();
	TestTrue(TEXT("제작에도 쓰고 팔 수도 있으면 둘 다 말한다"),
		BothHint.Contains(TEXT("제작")) && BothHint.Contains(TEXT("잡화상")));
	Fake.SellPrice = 0;
	Fake.CraftTag = ECraftTag::None;
	const FString NoUseHint = UBackpackScreenWidget::MakeDisabledHint(&Fake, FakeRecipes).ToString();
	TestTrue(TEXT("제작에도 안 쓰고 못 팔면 쓸 데가 없다고 말한다"), NoUseHint.Contains(TEXT("쓸 데")));
	Fake.CraftTag = ECraftTag::Meat;
	TestFalse(TEXT("레시피 표가 없으면 제작 얘기를 지어내지 않는다"),
		UBackpackScreenWidget::MakeDisabledHint(&Fake, nullptr).ToString().Contains(TEXT("제작")));

	Fake.ItemType = EItemType::Placeable;      // 칸 수 0 = 표가 덜 채워진 설치물
	TestTrue(TEXT("설치물인데 놓기가 꺼지면 칸 수 얘기"),
		UBackpackScreenWidget::MakeDisabledHint(&Fake, FakeRecipes).ToString().Contains(TEXT("칸 수")));
	Fake.ItemType = EItemType::Equipment;
	TestTrue(TEXT("장비인데 장착이 꺼지면 부위 얘기"),
		UBackpackScreenWidget::MakeDisabledHint(&Fake, FakeRecipes).ToString().Contains(TEXT("입을 자리")));
	Fake.ItemType = EItemType::Food;
	TestTrue(TEXT("음식인데 사용이 꺼지면 못 먹는다고 말한다"),
		UBackpackScreenWidget::MakeDisabledHint(&Fake, FakeRecipes).ToString().Contains(TEXT("먹을 수 없")));
	Fake.ItemType = EItemType::Pet;
	TestTrue(TEXT("펫은 목장 칸 얘기"),
		UBackpackScreenWidget::MakeDisabledHint(&Fake, FakeRecipes).ToString().Contains(TEXT("목장")));
	TestTrue(TEXT("표에 없는 물건은 그 사실을 말한다"),
		UBackpackScreenWidget::MakeDisabledHint(nullptr, FakeRecipes).ToString().Contains(TEXT("표에 없는")));

	AddInfo(FString::Printf(TEXT("[Backpack] 까닭 줄 - 나뭇가지 '%s' · 사냥 안장 '%s' · 사냥 목줄 '%s' · 손도끼 %s · 텐트 %s · 구운 고기 %s · 제작+판매 '%s' · 표 밖 '%s'"),
		*WoodHint, *SaddleHint, *CollarHint, bHatchetHintEmpty ? TEXT("없음(접힘)") : TEXT("있음"),
		bTentHintEmpty ? TEXT("없음(접힘)") : TEXT("있음"),
		Screen->GetCardHintText().IsEmpty() ? TEXT("없음(접힘)") : TEXT("있음"),
		*BothHint, *UBackpackScreenWidget::MakeDisabledHint(nullptr, FakeRecipes).ToString()));

	GameInstance->Shutdown();
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackGuestEquipTest, "Snarehaven.Backpack.Equip.GuestSeesRequestSent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackGuestEquipTest::RunTest(const FString& Parameters)
{
	// M15-11 (INBOX 2026-09-25 ⑤) — 멀티 게스트는 장착할 때마다 "장착할 수 없다" 가 떴다.
	// 게스트가 받은 false 는 "못 한다" 가 아니라 **"호스트에게 보냈다"** 다 (DESIGN 4.1 권위 표).
	// 권위가 있는 쪽(호스트·혼자 놀기)에서는 옛 글이 그대로여야 한다 — 실패를 성공으로 덮지 않는다.
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(HatchetID, 1), MakeSlot(WoodID, 1) }), false);
	UEquipmentComponent* const Equipment = NewObject<UEquipmentComponent>(Rig.Owner, TEXT("Equipment"));
	Equipment->RegisterComponent();
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, Equipment);
	Screen->SetPanelOpen(true);
	const auto Select = [Screen](FName ItemID)
	{
		const int32 Index = Screen->GetShownSlots().IndexOfByPredicate(
			[ItemID](const FInventorySlot& Slot) { return Slot.ItemID == ItemID; });
		if (Index != Screen->GetSelectedIndex())
		{
			Screen->SelectSlot(Index);
		}
		return Index != INDEX_NONE && Screen->GetSelectedIndex() == Index;
	};
	if (!TestTrue(TEXT("손도끼 칸을 고른다"), Select(HatchetID)))
	{
		return false;
	}

	// ── 권위 있는 쪽: 몸(메시)이 없어 진짜로 못 건다 — 그 사실을 그대로 말한다 ──
	TestTrue(TEXT("혼자 놀기 폰은 장착 권위가 있다"), Equipment->HasEquipAuthority());
	Screen->PressEquip();
	const FString AloneStatus = Screen->GetStatusText().ToString();
	TestEqual(TEXT("못 건 것을 보냈다고 말하지 않는다"), AloneStatus, FString(TEXT("장착할 수 없다")));

	// ── 게스트: 요청만 간다(넷드라이버가 없어 아무 데도 안 가고, 그래서 목록이 빈 채로 남는다) ──
	Rig.Owner->SetRole(ROLE_AutonomousProxy);
	TestFalse(TEXT("게스트 폰은 장착 권위가 없다"), Equipment->HasEquipAuthority());
	Screen->PressEquip();
	const FString GuestStatus = Screen->GetStatusText().ToString();
	TestEqual(TEXT("게스트는 보냈다고 말한다"), GuestStatus, FString(TEXT("장착하는 중")));
	TestEqual(TEXT("게스트 화면은 혼자 안 걸린다"), Equipment->GetEquippedSlots().Num(), 0);

	// ── 장비가 아닌 것은 게스트라도 보냈다고 말하지 않는다 (「장착」 은 꺼져 있는 칸이다) ──
	if (TestTrue(TEXT("나뭇가지 칸을 고른다"), Select(WoodID)))
	{
		Screen->PressEquip();
		TestEqual(TEXT("재료를 눌러도 보냈다고 하지 않는다"), Screen->GetStatusText().ToString(),
			FString(TEXT("장착할 수 없다")));
	}

	AddInfo(FString::Printf(TEXT("[Backpack] 장착 글 - 혼자 '%s' · 게스트 '%s' · 재료 '%s'"),
		*AloneStatus, *GuestStatus, *Screen->GetStatusText().ToString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackSheetTest, "Snarehaven.Backpack.Tree.MatchesReferenceSheet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-17 — 레퍼런스 시트 `docs/reference/inventory_backpack.md` 의 「검사 항목(3층)」 여섯 줄을 2층 숫자로 옮긴다.
// 눈으로 캡처를 대조하던 항목이라 한 번 맞춰 놓아도 다음 조각이 판을 옮기면 조용히 어긋났다(실제로 판이
// 화면 가운데로 옮겨 앉아 있었다 — 아래 ①).
// 재는 자리:
//   * **화면 자리**(오른쪽 가장자리)는 `GetRenderBoundingRect` — 판을 1.6배로 키우는 렌더 변환까지 들어간 자리다.
//   * **시트 px**(칸 52·제목 탭 20·재화 바 260×48)는 `GetLayoutBoundingRect` — 시트가 적은 1920 판 기준 값이다.
// 폰 비율(2340×1080)도 같이 본다 — 시트는 1920 기준이라 폰에서 판이 가장자리를 놓치는지는 숫자로만 알 수 있다.
bool FSnarehavenBackpackSheetTest::RunTest(const FString& Parameters)
{
	// 시트가 적은 값 (테스트 전용 사본 — 게임 규칙 숫자가 아니다).
	constexpr float SheetViewWidth = 1920.f;
	constexpr float SheetPanelRight = 1595.f;   // 판 오른쪽 끝 = 폭의 83.1%
	constexpr float SheetPanelWidth = 312.f;
	constexpr float SheetCellSize = 52.f;
	constexpr int32 SheetColumns = 4;
	constexpr int32 SheetRows = 5;
	constexpr float SheetPillRise = 20.f;       // 제목 탭이 판 위로 튀어나온 만큼
	constexpr int32 SheetSideTabs = 3;          // 시트는 배낭·장비·도감 — 우리는 제작(M13-29)·안내서(M28-06b2b)가 더
	constexpr int32 OurSideTabs = 5;
	constexpr float OurBarWidth = 170.f;        // 코인 칩 — 시트의 하단 바(260×48)를 머리 줄로 옮김 (보정 10-11 23:50)
	constexpr float OurBarHeight = 36.f;
	constexpr float SheetRightMargin = SheetViewWidth - SheetPanelRight;  // 325
	constexpr float Tolerance = 0.5f;

	// 등급이 서로 다른 아이템 둘을 표에서 고른다 — 원판 색이 코드 상수가 아니라 `DT_ItemTable` 등급에서
	// 온다는 것을 보려면 한 색만으로는 모자란다 (DataDriven).
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	FName CommonID;
	FName FancyID;
	EItemGrade FancyGrade = EItemGrade::Common;
	bool bCommonHasIcon = false;
	bool bFancyHasIcon = false;
	if (TestNotNull(TEXT("아이템 표를 읽었다"), ItemTable))
	{
		for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
		{
			const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
			const FString Id = Row != nullptr ? Row->ItemID.ToString() : FString();
			if (Row == nullptr || Id.IsEmpty() || Id.StartsWith(TEXT("gacha.")) || Id.StartsWith(TEXT("item.pet_")))
			{
				continue;   // 배낭 칸을 안 차지하는 것 (서버 `isBackpackItem`)
			}
			// 그림이 있는 행을 먼저 고른다 — 원판 **위** 그림을 보는 검사라 아이콘이 빈 행으로는 반쪽만 본다
			// (아이콘이 빠진 행 자체는 `Snarehaven.Data.Items.IconMatchesItemID` 가 본다).
			const bool bHasIcon = !Row->IconRef.IsNull();
			if (Row->Grade == EItemGrade::Common)
			{
				if (CommonID.IsNone() || (bHasIcon && !bCommonHasIcon))
				{
					CommonID = Row->ItemID;
					bCommonHasIcon = bHasIcon;
				}
			}
			else if (FancyID.IsNone() || (bHasIcon && !bFancyHasIcon))
			{
				FancyID = Row->ItemID;
				FancyGrade = Row->Grade;
				bFancyHasIcon = bHasIcon;
			}
		}
	}
	if (!TestFalse(TEXT("표에 일반 등급 아이템이 있다"), CommonID.IsNone()))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("[Backpack] 시트 대조 아이템 - 일반 '%s' · 다른 등급 '%s'(%d)"),
		*CommonID.ToString(), FancyID.IsNone() ? TEXT("없음") : *FancyID.ToString(), static_cast<int32>(FancyGrade)));

	FFakeServer Server;
	Server.Install();
	FRig Rig;
	TArray<FInventorySlot> Slots{ MakeSlot(CommonID, 1) };
	if (!FancyID.IsNone())
	{
		Slots.Add(MakeSlot(FancyID, 1));
	}
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot(Slots), false);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
	Screen->SetPanelOpen(true);

	TMap<TSharedRef<SWidget>, FArrangedWidget> Arranged;
	FVector2D ViewSize(SheetViewWidth, 1080.f);
	auto Measure = [&Slate, &Arranged, &ViewSize]()
	{
		Arranged.Reset();
		Slate->SlatePrepass(1.f);
		TFunction<void(const FArrangedWidget&)> Walk = [&Walk, &Arranged](const FArrangedWidget& Node)
		{
			Arranged.Add(Node.Widget, Node);
			FArrangedChildren Children(EVisibility::Visible);
			Node.Widget->ArrangeChildren(Node.Geometry, Children);
			for (int32 Index = 0; Index < Children.Num(); ++Index)
			{
				Walk(Children[Index]);
			}
		};
		Walk(FArrangedWidget(Slate, FGeometry::MakeRoot(ViewSize, FSlateLayoutTransform())));
	};
	auto NodeOf = [&Arranged, Screen](const TCHAR* Name) -> const FArrangedWidget*
	{
		const UWidget* const Widget = Screen->WidgetTree->FindWidget(Name);
		return Widget != nullptr && Widget->GetCachedWidget().IsValid()
			? Arranged.Find(Widget->GetCachedWidget().ToSharedRef()) : nullptr;
	};
	// 화면에 실제로 보이는 자리 — 판을 키우는 렌더 변환이 들어간다.
	auto ScreenRect = [&NodeOf](const TCHAR* Name, FSlateRect& Out)
	{
		const FArrangedWidget* const Node = NodeOf(Name);
		if (Node != nullptr)
		{
			Out = Node->Geometry.GetRenderBoundingRect();
		}
		return Node != nullptr;
	};
	// 시트가 적은 1920 판 기준 크기 — 렌더 변환 전 자리.
	auto SheetRect = [&NodeOf](const TCHAR* Name, FSlateRect& Out)
	{
		const FArrangedWidget* const Node = NodeOf(Name);
		if (Node != nullptr)
		{
			Out = Node->Geometry.GetLayoutBoundingRect();
		}
		return Node != nullptr;
	};

	// ── ① 판이 화면 오른쪽 가장자리 (시트 x 1283~1595 = 폭의 66.8~83.1%) ──
	// 1.6배(감독 2026-09-25 "창이 작다")로 키운 만큼 왼쪽 끝은 시트보다 왼쪽으로 나간다 — 그래서 **오른쪽 끝**과
	// "판 전체가 오른쪽 절반 안" 으로 잰다. 폰 비율에서도 가장자리를 놓치지 않아야 한다.
	for (const FVector2D View : {FVector2D(SheetViewWidth, 1080.f), FVector2D(2340.f, 1080.f)})
	{
		ViewSize = View;
		Measure();
		FSlateRect Panel;
		if (!TestTrue(*FString::Printf(TEXT("%.0f 폭 판 배치"), View.X), ScreenRect(TEXT("BackpackPanel"), Panel)))
		{
			continue;
		}
		AddInfo(FString::Printf(TEXT("[Backpack] %.0f 폭 판 가로 %.1f~%.1f (%.1f%%~%.1f%%) · 세로 %.1f~%.1f"),
			View.X, Panel.Left, Panel.Right, Panel.Left / View.X * 100.f, Panel.Right / View.X * 100.f, Panel.Top, Panel.Bottom));
		TestTrue(*FString::Printf(TEXT("%.0f 폭 판이 화면 안"), View.X),
			Panel.Left >= -Tolerance && Panel.Right <= View.X + Tolerance
			&& Panel.Top >= -Tolerance && Panel.Bottom <= View.Y + Tolerance);
		TestTrue(*FString::Printf(TEXT("%.0f 폭 판 전체가 오른쪽 절반"), View.X), Panel.Left >= View.X * 0.5f);
		TestTrue(*FString::Printf(TEXT("%.0f 폭 판 오른쪽 여백이 시트 여백 이하"), View.X),
			View.X - Panel.Right <= SheetRightMargin + Tolerance);
		TestTrue(*FString::Printf(TEXT("%.0f 폭 판이 화면 변에 딱 붙지는 않는다"), View.X), View.X - Panel.Right > 0.f);
	}
	ViewSize = FVector2D(SheetViewWidth, 1080.f);
	Measure();
	FSlateRect PanelRect;
	if (!TestTrue(TEXT("판 배치"), ScreenRect(TEXT("BackpackPanel"), PanelRect)))
	{
		return false;
	}
	TestEqual(TEXT("1920 폭에서 판 오른쪽 끝이 시트 자리(83.1%)"), PanelRect.Right / SheetViewWidth,
		SheetPanelRight / SheetViewWidth, 0.005f);

	// ── ② 격자 4열 × 5행 = 20칸, 칸 52px ──
	const UUniformGridPanel* const Grid = Cast<UUniformGridPanel>(Screen->WidgetTree->FindWidget(TEXT("SlotGrid")));
	if (TestNotNull(TEXT("칸 격자가 있다"), Grid))
	{
		int32 MaxRow = -1;
		int32 MaxColumn = -1;
		for (int32 Index = 0; Index < Grid->GetChildrenCount(); ++Index)
		{
			const UUniformGridSlot* const Slot = Cast<UUniformGridSlot>(Grid->GetChildAt(Index)->Slot);
			if (Slot != nullptr)
			{
				MaxRow = FMath::Max(MaxRow, Slot->GetRow());
				MaxColumn = FMath::Max(MaxColumn, Slot->GetColumn());
			}
		}
		AddInfo(FString::Printf(TEXT("[Backpack] 격자 %d칸 · %d열 × %d행"), Grid->GetChildrenCount(), MaxColumn + 1, MaxRow + 1));
		TestEqual(TEXT("칸 수 = 시트 4×5"), Grid->GetChildrenCount(), SheetColumns * SheetRows);
		TestEqual(TEXT("열 수 = 시트 4"), MaxColumn + 1, SheetColumns);
		TestEqual(TEXT("행 수 = 시트 5"), MaxRow + 1, SheetRows);
	}
	FSlateRect FirstCell;
	FSlateRect LastCell;
	FSlateRect ScrollRect;
	if (TestTrue(TEXT("첫 칸·마지막 칸·스크롤 배치"),
		SheetRect(TEXT("Cell_0"), FirstCell) && SheetRect(TEXT("Cell_19"), LastCell) && SheetRect(TEXT("SlotScroll"), ScrollRect)))
	{
		TestTrue(TEXT("칸 = 시트 52px 정사각"),
			FMath::IsNearlyEqual(FirstCell.GetSize().X, SheetCellSize, Tolerance)
			&& FMath::IsNearlyEqual(FirstCell.GetSize().Y, SheetCellSize, Tolerance));
		// 5행이 **스크롤 없이** 다 보인다 — 20번째 칸이 스크롤 창 안에 있어야 "4×5 판" 이다.
		TestTrue(TEXT("20번째 칸이 안 내려도 보인다"),
			LastCell.Top >= ScrollRect.Top - Tolerance && LastCell.Bottom <= ScrollRect.Bottom + Tolerance);
	}

	// ── ③ 제목 탭이 판 위로 걸침 ──
	FSlateRect Pill;
	FSlateRect PanelSheet;
	if (TestTrue(TEXT("제목 탭·판 배치"), SheetRect(TEXT("TitlePill"), Pill) && SheetRect(TEXT("BackpackPanel"), PanelSheet)))
	{
		AddInfo(FString::Printf(TEXT("[Backpack] 제목 탭 %.0fx%.0f · 판 위로 %.1f px · 왼쪽 차 %.1f px"),
			Pill.GetSize().X, Pill.GetSize().Y, PanelSheet.Top - Pill.Top, Pill.Left - PanelSheet.Left));
		TestEqual(TEXT("제목 탭이 판보다 시트만큼 위로"), PanelSheet.Top - Pill.Top, SheetPillRise, Tolerance);
		TestTrue(TEXT("제목 탭이 판에 걸쳐 있다(아래가 판 안)"), Pill.Bottom > PanelSheet.Top + Tolerance);
		TestEqual(TEXT("제목 탭이 판 왼쪽 위 모서리에"), Pill.Left, PanelSheet.Left, Tolerance);
	}

	// ── ④ 왼쪽 세로 탭 (시트 3개 · 우리는 제작·안내서가 더 = 5개, M13-29 · M28-06b2b) ──
	const UVerticalBox* const Tabs = Cast<UVerticalBox>(Screen->WidgetTree->FindWidget(TEXT("TabColumn")));
	FSlateRect TabRect;
	if (TestNotNull(TEXT("세로 탭 기둥이 있다"), Tabs) && TestTrue(TEXT("세로 탭 배치"), SheetRect(TEXT("TabColumn"), TabRect)))
	{
		AddInfo(FString::Printf(TEXT("[Backpack] 세로 탭 %d개(시트 %d + 제작·안내서) · 가로 %.0f~%.0f · 판 왼쪽 %.0f"),
			Tabs->GetChildrenCount(), SheetSideTabs, TabRect.Left, TabRect.Right, PanelSheet.Left));
		TestEqual(TEXT("세로 탭 수 = 시트 3 + 제작·안내서 2"), Tabs->GetChildrenCount(), OurSideTabs);
		TestTrue(TEXT("세로 탭이 판 밖 왼쪽"), TabRect.Right <= PanelSheet.Left + Tolerance);
		TestTrue(TEXT("세로 탭 한 칸이 82px (시트 52 → 손가락 크기 10-16)"),
			FMath::IsNearlyEqual(TabRect.GetSize().X, 82.f, Tolerance));
	}

	// ── ⑤ 머리 줄 코인 칩 (판 아래 줄 아님) ──
	FSlateRect Bar;
	if (TestTrue(TEXT("코인 칩 배치"), SheetRect(TEXT("BalanceBarBox"), Bar)))
	{
		AddInfo(FString::Printf(TEXT("[Backpack] 코인 칩 %.0fx%.0f · 세로 %.0f~%.0f · 가로 오른쪽 %.0f (판 위 %.0f · 오른쪽 %.0f)"),
			Bar.GetSize().X, Bar.GetSize().Y, Bar.Top, Bar.Bottom, Bar.Right, PanelSheet.Top, PanelSheet.Right));
		TestEqual(TEXT("코인 칩 폭"), Bar.GetSize().X, OurBarWidth, Tolerance);
		TestEqual(TEXT("코인 칩 높이"), Bar.GetSize().Y, OurBarHeight, Tolerance);
		TestEqual(TEXT("코인 칩이 판 오른쪽 끝에"), Bar.Right, PanelSheet.Right, Tolerance);
		TestTrue(TEXT("코인 칩이 머리 줄(판 위 가장자리에 걸침)"), Bar.Top < PanelSheet.Top && Bar.Bottom > PanelSheet.Top - SheetPillRise);
	}
	const UTextBlock* const Balance = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("BalanceText")));
	if (TestNotNull(TEXT("재화 글이 있다"), Balance))
	{
		// 글이 칩 밖으로 삐져나오지 않는다 (감독 보정 10-13) — 모를 때 글 셋 다 칩 오른쪽 끝(좌우 16 안쪽)을 넘지 않는다.
		const FText OriginalBalance = Balance->GetText();
		for (const TCHAR* const Line : { TEXT("서버 연결 안 됨"), TEXT("불러오는 중…"), TEXT("코인 99999") })
		{
			const_cast<UTextBlock*>(Balance)->SetText(FText::FromString(Line));
			Measure();
			FSlateRect TextRect;
			FSlateRect ChipRect;
			if (TestTrue(TEXT("재화 글 배치"), SheetRect(TEXT("BalanceText"), TextRect) && SheetRect(TEXT("BalanceBarBox"), ChipRect)))
			{
				TestTrue(*FString::Printf(TEXT("재화 글 `%s` 이 칩 안에 들어감 (글 오른쪽 %.0f ≤ 칩 오른쪽 %.0f - 16)"), Line, TextRect.Right, ChipRect.Right),
					TextRect.Right <= ChipRect.Right - 16.f + Tolerance);
			}
		}
		const_cast<UTextBlock*>(Balance)->SetText(OriginalBalance);
		Measure();
		TestNotEqual(TEXT("재화 바가 접혀 있지 않다"), Balance->GetVisibility(), ESlateVisibility::Collapsed);
	}
	// 시트 「하단 바」 — 왼쪽 재화 아이콘 28px(그림이 실제로 붙어 있다) · 오른쪽 끝 `>` (M15-25)
	const UImage* const BalanceIcon = Cast<UImage>(Screen->WidgetTree->FindWidget(TEXT("BalanceIcon")));
	if (TestNotNull(TEXT("재화 아이콘이 있다"), BalanceIcon))
	{
		TestNotNull(TEXT("재화 아이콘에 그림이 붙어 있다"), BalanceIcon->GetBrush().GetResourceObject());
	}
	const USizeBox* const BalanceIconBox = Cast<USizeBox>(Screen->WidgetTree->FindWidget(TEXT("BalanceIconBox")));
	if (TestNotNull(TEXT("재화 아이콘 상자가 있다"), BalanceIconBox))
	{
		TestEqual(TEXT("재화 아이콘 = 시트 28px"), BalanceIconBox->GetWidthOverride(), 28.f);
	}
	TestNull(TEXT("`>` 줄은 없앴다(레퍼런스 게임와 다르게)"), Screen->WidgetTree->FindWidget(TEXT("BalanceChevron")));

	// ── ⑥ 아이템은 **색 원판 위 그림** (네모 아이콘 아님) · 원판 색은 표의 등급에서 온다 ──
	auto CellOf = [Screen](FName ItemID)
	{
		return Screen->GetShownSlots().IndexOfByPredicate(
			[ItemID](const FInventorySlot& Slot) { return Slot.ItemID == ItemID; });
	};
	TArray<TPair<FName, EItemGrade>> Wanted{ {CommonID, EItemGrade::Common} };
	if (!FancyID.IsNone())
	{
		Wanted.Add({ FancyID, FancyGrade });
	}
	for (const TPair<FName, EItemGrade>& Want : Wanted)
	{
		const int32 Index = CellOf(Want.Key);
		if (!TestTrue(*FString::Printf(TEXT("%s 가 칸에 있다"), *Want.Key.ToString()), Index != INDEX_NONE))
		{
			continue;
		}
		const UBorder* const Band = Cast<UBorder>(Screen->WidgetTree->FindWidget(
			*FString::Printf(TEXT("CellGrade_%d"), Index)));
		if (!TestNotNull(*FString::Printf(TEXT("%s 칸에 등급 띠가 있다"), *Want.Key.ToString()), Band))
		{
			continue;
		}
		const FLinearColor Tint = Band->Background.TintColor.GetSpecifiedColor();
		const FLinearColor WantTint = SnareUiStyle::ItemGradeBandColor(Want.Value);
		AddInfo(FString::Printf(TEXT("[Backpack] %s 칸 %d - 띠 색 %s (등급 띠 색 %s)"),
			*Want.Key.ToString(), Index, *Tint.ToFColor(true).ToHex(), *WantTint.ToFColor(true).ToHex()));
		TestEqual(TEXT("띠 색 = 표 등급 띠 색"), Tint.ToFColor(true).ToHex(), WantTint.ToFColor(true).ToHex());
		// 색 동그라미는 없다 — 칸 틀은 속이 빈 테두리이고 등급은 칸 아래 1/5 띠로만 보인다 (레퍼런스 게임와 달리, Reference 6절).
		const UBorder* const Frame = Cast<UBorder>(Screen->WidgetTree->FindWidget(*FString::Printf(TEXT("CellDisc_%d"), Index)));
		if (TestNotNull(TEXT("칸 틀이 있다"), Frame))
		{
			TestEqual(TEXT("칸 틀 속은 투명(색 동그라미 없음)"), Frame->Background.TintColor.GetSpecifiedColor().A, 0.f, 0.001f);
			TestTrue(TEXT("칸 틀 모서리는 원이 아니다"), Frame->Background.OutlineSettings.CornerRadii.X < SheetCellSize * 0.25f);
		}
		FSlateRect DiscRect;
		FSlateRect BandRect;
		FSlateRect IconRect;
		if (TestTrue(TEXT("칸 틀 배치"), SheetRect(*FString::Printf(TEXT("CellDisc_%d"), Index), DiscRect))
			&& TestTrue(TEXT("띠 배치"), SheetRect(*FString::Printf(TEXT("CellGrade_%d"), Index), BandRect)))
		{
			TestTrue(TEXT("칸 틀이 칸을 꽉 채운다"),
				FMath::IsNearlyEqual(DiscRect.GetSize().X, SheetCellSize, Tolerance)
				&& FMath::IsNearlyEqual(DiscRect.GetSize().Y, SheetCellSize, Tolerance));
			TestEqual(TEXT("띠 높이 = 칸 1/5"), BandRect.GetSize().Y, SheetCellSize / 5.f, Tolerance);
			TestTrue(TEXT("띠가 칸 아래쪽 안"), BandRect.Bottom <= DiscRect.Bottom + Tolerance && BandRect.Left >= DiscRect.Left - Tolerance
				&& BandRect.Right <= DiscRect.Right + Tolerance && BandRect.Top > DiscRect.Top + DiscRect.GetSize().Y * 0.5f);
		}
		const FItemRow* const Row = SnareItemRows::FindByItemID(ItemTable, Want.Key);
		const bool bHasIcon = Row != nullptr && !Row->IconRef.IsNull();
		if (!bHasIcon)
		{
			// 표에 그림이 없는 행 — 이 검사는 판 모양을 보는 자리다(빠진 아이콘은 `Data.Items.IconMatchesItemID` 몫).
			AddInfo(FString::Printf(TEXT("[Backpack] %s 는 표에 아이콘이 없어 원판 위 그림은 못 봤다"), *Want.Key.ToString()));
			continue;
		}
		if (TestTrue(TEXT("그림 배치"), SheetRect(*FString::Printf(TEXT("CellIcon_%d"), Index), IconRect)))
		{
			TestTrue(TEXT("그림이 칸 안"), IconRect.Left >= DiscRect.Left - Tolerance && IconRect.Right <= DiscRect.Right + Tolerance
				&& IconRect.Top >= DiscRect.Top - Tolerance && IconRect.Bottom <= DiscRect.Bottom + Tolerance);
			TestTrue(TEXT("그림이 칸보다 작다"),
				IconRect.GetSize().X < DiscRect.GetSize().X - Tolerance && IconRect.GetSize().Y < DiscRect.GetSize().Y - Tolerance);
		}
	}
	if (FancyID.IsNone())
	{
		AddWarning(TEXT("표에 일반 아닌 등급 아이템이 없어 등급 색 두 가지를 못 견줬다"));
	}
	else
	{
		TestNotEqual(TEXT("등급이 다르면 띠 색도 다르다"),
			SnareUiStyle::ItemGradeBandColor(EItemGrade::Common).ToFColor(true).ToHex(),
			SnareUiStyle::ItemGradeBandColor(FancyGrade).ToFColor(true).ToHex());
	}
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackPetSummaryTest, "Snarehaven.Backpack.Pets.SummaryCountsOwnedAndPending",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackPetSummaryTest::RunTest(const FString& Parameters)
{
	// 포획한 펫은 격자에 없다 — 바의 한 줄이 유일한 표시다(INBOX 폰 확인 09-29 22:50).
	const FString None = UBackpackScreenWidget::FormatPetSummary({ MakeSlot(WoodID, 5) }, 0).ToString();
	TestTrue(TEXT("펫도 대기도 없으면 빈 글"), None.IsEmpty());
	const FString Owned = UBackpackScreenWidget::FormatPetSummary({ MakeSlot(PetID, 2), MakeSlot(WoodID, 5) }, 0).ToString();
	TestEqual(TEXT("가진 펫만"), Owned, FString(TEXT("펫 2마리 · 목장 칸 앞에서 넣기")));
	const FString Pending = UBackpackScreenWidget::FormatPetSummary({}, 1).ToString();
	TestEqual(TEXT("대기만 (오프라인 포획)"), Pending, FString(TEXT("펫 0마리 · 등록 대기 1 · 목장 칸 앞에서 넣기")));
	AddInfo(FString::Printf(TEXT("[Backpack] 펫 줄 '%s' / '%s'"), *Owned, *Pending));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackReleasePetPickTest, "Snarehaven.Backpack.Pets.ReleasePickPrefersSelectedPet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackReleasePetPickTest::RunTest(const FString& Parameters)
{
	const TArray<FName> Pets = { TEXT("animal.a"), TEXT("animal.b") };
	TestEqual(TEXT("고른 것이 가진 펫이면 그대로"), UBackpackScreenWidget::PickReleasePet(Pets, TEXT("animal.b")), FName(TEXT("animal.b")));
	TestEqual(TEXT("씨앗 등 가진 펫이 아니면 첫 마리"), UBackpackScreenWidget::PickReleasePet(Pets, TEXT("item.seed")), FName(TEXT("animal.a")));
	TestEqual(TEXT("펫이 없으면 NAME_None"), UBackpackScreenWidget::PickReleasePet({}, TEXT("animal.a")), FName(NAME_None));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackPendingPetRedrawTest, "Snarehaven.Backpack.Pets.QueuedPetRedrawsLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackPendingPetRedrawTest::RunTest(const FString& Parameters)
{
	// 오프라인 포획은 스냅샷이 안 와서 대기열 신호만이 하단 바를 다시 그린다 (캡처 loop235 에서 바가 `코인 -` 뿐이었다).
	FRig Rig;
	UInventoryComponent* const Inventory = NewObject<UInventoryComponent>(Rig.Owner, TEXT("Inventory"));
	Inventory->RegisterComponent();

	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
	const UTextBlock* const Bar = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("PetText")));
	if (!TestNotNull(TEXT("펫 줄 글"), Bar))
	{
		return false;
	}
	TestFalse(TEXT("포획 전엔 펫 줄이 접혀 있다"), Bar->GetVisibility() != ESlateVisibility::Collapsed);

	Inventory->QueuePendingGrant(PetID, 1);
	const FString After = Bar->GetText().ToString();
	TestTrue(TEXT("대기열에 펫이 쌓이면 펫 줄이 등록 대기를 보인다"), After.Contains(TEXT("등록 대기 1")));
	AddInfo(FString::Printf(TEXT("[Backpack] 포획 뒤 펫 줄 '%s'"), *After));

	// M17-09 — 대기 펫이 격자 안 칸(`대기` 딱지)으로도 보이고, 누르면 고르지 않고 안내만 한다.
	TestNotNull(TEXT("대기 펫 칸의 `대기` 딱지"), Screen->WidgetTree->FindWidget(TEXT("CellPendingTag_0")));
	TestNotNull(TEXT("대기 펫 칸의 이름"), Screen->WidgetTree->FindWidget(TEXT("CellPendingName_0")));
	Screen->SelectSlot(0);
	TestEqual(TEXT("대기 펫 칸은 고르지 않는다"), Screen->GetSelectedIndex(), static_cast<int32>(INDEX_NONE));
	return true;
}

// M35-06 — 뽑은 가챠 경품이 결과 화면 뒤에도 배낭에 보인다. 칸 수에는 안 들어가고(서버 `isBackpackItem`),
// 코인 환급은 칸 0(돈으로 나감), 이름·등급은 공개 문서 표(`DT_GachaPool`)에서, 버리기는 꺼진다(서버 `protected`).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackGachaPrizesTest, "Snarehaven.Backpack.Slots.GachaPrizesShown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackGachaPrizesTest::RunTest(const FString& Parameters)
{
	// 테스트 전용 값 — 경품 ID 는 공개 문서 표의 실제 줄(소재 · 펫 의상 · 데코 · 코인 환급 하나씩).
	const FName HideID = TEXT("gacha.mat_hide_5");
	const FName DecoID = TEXT("gacha.deco_wood_fence");
	const FName CoinID = TEXT("gacha.refund_coin_50");
	constexpr int32 TestHideCount = 5;

	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath());
	const UDataTable* const PoolTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Gacha/DT_GachaPool.DT_GachaPool"));
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable) || !TestNotNull(TEXT("DT_GachaPool"), PoolTable))
	{
		return false;
	}
	const FGachaPoolRow* const HidePool = UBackpackScreenWidget::FindGachaPoolRow(PoolTable, HideID);
	if (!TestNotNull(TEXT("가죽 조각 x5 가 풀 표에 있다"), HidePool))
	{
		return false;
	}

	// ── 순수 함수: 무엇이 칸이 되나 ──
	const TArray<FInventorySlot> Owned = { MakeSlot(WoodID, 1), MakeSlot(PetID, 1), MakeSlot(GachaID, 1),
		MakeSlot(HideID, TestHideCount), MakeSlot(DecoID, 1), MakeSlot(CoinID, 1) };
	const TArray<FInventorySlot> Gacha = UBackpackScreenWidget::BuildGachaSlots(Owned, PoolTable, ServerStackMax);
	TestEqual(TEXT("가챠 칸 = 리본 + 가죽 조각 + 울타리 (코인 환급·펫·나무 빠짐)"), Gacha.Num(), 3);
	TestFalse(TEXT("코인 환급은 칸 0"), Gacha.ContainsByPredicate([CoinID](const FInventorySlot& S) { return S.ItemID == CoinID; }));
	const FInventorySlot* const HideSlot = Gacha.FindByPredicate([HideID](const FInventorySlot& S) { return S.ItemID == HideID; });
	TestTrue(TEXT("가죽 조각 5개가 한 칸에 5"), HideSlot != nullptr && HideSlot->Count == TestHideCount);
	TestEqual(TEXT("배낭 칸 목록은 그대로 나무 1칸 (가챠는 칸 수에 안 듦)"),
		UBackpackScreenWidget::BuildVisibleSlots(Owned, ItemTable, ServerStackMax).Num(), 1);
	// 반대 방향 — 상한이 작으면 경품도 나뉜다(셈을 배낭 칸과 같은 규칙으로 한다).
	TestEqual(TEXT("상한 2 면 가죽 조각 5개 = 3칸"),
		UBackpackScreenWidget::BuildGachaSlots({ MakeSlot(HideID, TestHideCount) }, PoolTable, 2).Num(), 3);

	// ── 화면: 칸이 더 그려지고 카드가 이름·등급을 공개 문서 표에서 읽는다 ──
	FFakeServer Server;
	Server.Install();
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	USnareBackendSubsystem* const Backend = GameInstance->GetSubsystem<USnareBackendSubsystem>();
	if (!TestNotNull(TEXT("월드"), World) || !TestNotNull(TEXT("백엔드 창구"), Backend))
	{
		GameInstance->Shutdown();
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, 1), MakeSlot(HideID, TestHideCount),
		MakeSlot(DecoID, 1), MakeSlot(CoinID, 1) }), false);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		GameInstance->Shutdown();
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Backend, nullptr, nullptr);
	Screen->SetPanelOpen(true);
	const int32 BaseSlots = GetDefault<UInventoryComponent>()->GetSlotCount();
	TestEqual(TEXT("칸 = 기본 칸 + 가챠 2칸 (빈 칸이 줄지 않는다)"), Screen->GetCellCount(), BaseSlots + 2);
	TestEqual(TEXT("보이는 칸 = 나무 1 + 가챠 2"), Screen->GetShownSlots().Num(), 3);
	const int32 HideIndex = Screen->GetShownSlots().IndexOfByPredicate(
		[HideID](const FInventorySlot& S) { return S.ItemID == HideID; });
	TestTrue(TEXT("가챠 칸은 배낭 물건 뒤"), HideIndex >= 1);
	TestNotNull(TEXT("가챠 칸 `가챠` 딱지"), Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("CellGachaTag_%d"), HideIndex))));
	const UTextBlock* const CellName = Cast<UTextBlock>(
		Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("CellGachaName_%d"), HideIndex))));
	TestTrue(TEXT("그림 없는 소재 칸에 공개 문서 이름"),
		CellName != nullptr && CellName->GetText().EqualTo(HidePool->DisplayName));

	Screen->SelectSlot(HideIndex);
	const UTextBlock* const CardName = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("CardNameText")));
	const UTextBlock* const CardInfo = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("CardInfoText")));
	const UButton* const DropButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("DropButton")));
	TestTrue(TEXT("카드 이름 = 공개 문서 이름"), CardName != nullptr && CardName->GetText().EqualTo(HidePool->DisplayName));
	TestTrue(TEXT("카드 수량 = 5개"), CardInfo != nullptr && CardInfo->GetText().ToString().Contains(TEXT("5개")));
	TestTrue(TEXT("가챠 경품은 버리기가 꺼져 있다"), DropButton != nullptr && !DropButton->GetIsEnabled());
	const FString Hint = Screen->GetCardHintText().ToString();
	TestTrue(TEXT("소재 카드는 제작에 쓴다고 말한다"), Hint.Contains(TEXT("제작")));

	// 반대 방향 — 배낭 물건(나무)은 그대로 버릴 수 있다.
	const int32 WoodIndex = Screen->GetShownSlots().IndexOfByPredicate(
		[](const FInventorySlot& S) { return S.ItemID == WoodID; });
	Screen->SelectSlot(WoodIndex);
	TestTrue(TEXT("나무는 버리기가 켜져 있다"), DropButton != nullptr && DropButton->GetIsEnabled());

	AddInfo(FString::Printf(TEXT("[BackpackGacha] 칸 %d (기본 %d + 가챠 2) · 소재 칸 이름 '%s' · 카드 '%s' / '%s' · 힌트 '%s'"),
		Screen->GetCellCount(), BaseSlots, CellName != nullptr ? *CellName->GetText().ToString() : TEXT("-"),
		CardName != nullptr ? *CardName->GetText().ToString() : TEXT("-"),
		CardInfo != nullptr ? *CardInfo->GetText().ToString() : TEXT("-"), *Hint));
	GameInstance->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackRanchDecoButtonTest, "Snarehaven.Backpack.Slots.RanchDecoApplyButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackRanchDecoButtonTest::RunTest(const FString& Parameters)
{
	// M35-05d — 데코 카드 사용 칸 「목장 적용」 → `SetRanchDeco` 요청만, 글자·캐시는 `GetBaseState` 답 뒤에만 (CLAUDE.md 8).
	// 테스트 전용 값 — 데코 ID·종류는 공개 문서·서버 `GACHA_RANCH_DECOS` 의 실제 줄.
	const FName DecoID = TEXT("gacha.deco_wood_fence");
	const FString DecoKind = TEXT("fence");
	const FString DecoFunction = URanchSyncComponent::GetSetRanchDecoFunctionName();
	auto MakeJson = [](const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	};

	FFakeServer Server;
	Server.Install();
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GEngine);
	GameInstance->InitializeStandalone();
	UWorld* const World = GameInstance->GetWorld();
	USnareBackendSubsystem* const Backend = GameInstance->GetSubsystem<USnareBackendSubsystem>();
	if (!TestNotNull(TEXT("월드"), World) || !TestNotNull(TEXT("백엔드 창구"), Backend))
	{
		GameInstance->Shutdown();
		return false;
	}
	World->InitializeActorsForPlay(FURL());
	Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, 1), MakeSlot(DecoID, 1) }), false);
	AActor* const Owner = World->SpawnActor<AActor>();
	URanchComponent* const Ranch = NewObject<URanchComponent>(Owner);
	URanchSyncComponent* const Sync = NewObject<URanchSyncComponent>(Owner);
	Sync->BindTargets(Ranch, nullptr, Backend, nullptr, nullptr);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Screen))
	{
		GameInstance->Shutdown();
		return false;
	}
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->SetRanchSync(Sync);
	Screen->BindTargets(Backend, nullptr, nullptr);
	Screen->SetPanelOpen(true);
	const int32 DecoIndex = Screen->GetShownSlots().IndexOfByPredicate([DecoID](const FInventorySlot& S) { return S.ItemID == DecoID; });
	if (!TestTrue(TEXT("데코가 배낭 가챠 칸에 있다"), DecoIndex != INDEX_NONE))
	{
		GameInstance->Shutdown();
		return false;
	}
	Screen->SelectSlot(DecoIndex);
	const UButton* const UseButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("UseButton")));
	const UTextBlock* const UseText = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("UseButtonText")));
	auto Label = [UseText]() { return UseText != nullptr ? UseText->GetText().ToString() : FString(); };
	auto Enabled = [UseButton]() { return UseButton != nullptr && UseButton->GetIsEnabled(); };
	TestEqual(TEXT("적용 전 버튼 = 목장 적용"), Label(), FString(TEXT("목장 적용")));
	TestTrue(TEXT("적용 전 버튼이 켜져 있다"), Enabled());
	const FString FirstLabel = Label();

	// ① 적용 — 요청만 나가고 캐시·글자는 그대로, 답을 기다리는 동안 버튼이 꺼진다.
	Screen->PressUse();
	TestEqual(TEXT("누르면 SetRanchDeco 를 묻는다"), Server.LastFunction, DecoFunction);
	TestEqual(TEXT("itemId = 데코"), Server.LastArgs.IsValid() ? Server.LastArgs->GetStringField(TEXT("itemId")) : FString(), DecoID.ToString());
	TestEqual(TEXT("kind = fence"), Server.LastArgs.IsValid() ? Server.LastArgs->GetStringField(TEXT("kind")) : FString(), DecoKind);
	TestEqual(TEXT("응답 전 데코 캐시 0"), Ranch->GetDeco().Num(), 0);
	TestEqual(TEXT("응답 전 글자 그대로"), Label(), FString(TEXT("목장 적용")));
	TestFalse(TEXT("응답 전 버튼 꺼짐"), Enabled());
	Server.Answer(MakeJson(FString::Printf(TEXT(R"({"kind":"fence","itemId":"%s","replaced":""})"), *DecoID.ToString())));
	TestEqual(TEXT("성공 뒤 GetBaseState 를 다시 묻는다"), Server.LastFunction, URanchSyncComponent::GetBaseStateFunctionName());
	TestEqual(TEXT("거점 답 전엔 아직 목장 적용"), Label(), FString(TEXT("목장 적용")));
	Server.Answer(MakeJson(FString::Printf(TEXT(R"({"ranchDeco":{"fence":"%s"}})"), *DecoID.ToString())));
	TestEqual(TEXT("거점 답 뒤 캐시 fence = 데코"), Ranch->GetDeco().FindRef(TEXT("fence")), DecoID);
	TestTrue(TEXT("적용 뒤 IsSelectedDecoApplied"), Screen->IsSelectedDecoApplied());
	TestEqual(TEXT("적용 뒤 버튼 = 적용 해제"), Label(), FString(TEXT("적용 해제")));
	TestTrue(TEXT("적용 뒤 버튼 다시 켜짐"), Enabled());
	const FString AppliedStatus = Screen->GetStatusText().ToString();
	TestTrue(TEXT("성공 알림 「목장에 적용했다」"), AppliedStatus.Contains(TEXT("목장에 적용했다")));
	const FString SecondLabel = Label();

	// ② 해제 — 빈 itemId + kind. 거절이면 캐시·글자 그대로, 거절 글이 남는다.
	Screen->PressUse();
	TestEqual(TEXT("해제도 SetRanchDeco"), Server.LastFunction, DecoFunction);
	TestEqual(TEXT("해제 itemId = 빈 글"), Server.LastArgs.IsValid() ? Server.LastArgs->GetStringField(TEXT("itemId")) : TEXT("x"), FString());
	TestEqual(TEXT("해제 kind = fence"), Server.LastArgs.IsValid() ? Server.LastArgs->GetStringField(TEXT("kind")) : FString(), DecoKind);
	const int32 CallsBeforeFail = Server.Calls;
	Server.AnswerFail(TEXT("already"), TEXT("적용한 데코가 없다"));
	TestEqual(TEXT("거절이면 다시 묻지 않는다"), Server.Calls, CallsBeforeFail);
	TestEqual(TEXT("거절 뒤 캐시 그대로"), Ranch->GetDeco().FindRef(TEXT("fence")), DecoID);
	TestEqual(TEXT("거절 뒤 글자 그대로 적용 해제"), Label(), FString(TEXT("적용 해제")));
	TestFalse(TEXT("거절 글이 남는다"), Screen->GetStatusText().IsEmpty());

	// ③ 반대 방향 — 데코가 아닌 나무 칸은 사용 칸 글자가 「사용」 이고 SetRanchDeco 를 묻지 않는다.
	const int32 WoodIndex = Screen->GetShownSlots().IndexOfByPredicate([](const FInventorySlot& S) { return S.ItemID == WoodID; });
	Screen->SelectSlot(WoodIndex);
	TestEqual(TEXT("나무 카드 = 사용"), Label(), FString(TEXT("사용")));

	AddInfo(FString::Printf(TEXT("[BackpackDeco] 버튼 '%s' → 요청 %s{itemId:%s, kind:%s} → 거점 답 뒤 '%s' · 알림 '%s' · 나무 칸 '%s'"),
		*FirstLabel, *DecoFunction, *DecoID.ToString(), *DecoKind, *SecondLabel, *AppliedStatus, *Label()));
	GameInstance->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackpackRetryTest, "Snarehaven.Backpack.Drop.Retry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackpackRetryTest::RunTest(const FString& Parameters)
{
	// 버리기가 서버에 닿지 못하면 카드에 이유와 「다시 시도」 — 누르면 같은 칸을 다시 보낸다. 서버 거절엔 없다.
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Backend->ApplyPlayerSnapshot(MakeSnapshot({ MakeSlot(WoodID, TestDropWood) }), false);
	UBackpackScreenWidget* const Screen = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	Screen->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
	Screen->SetPanelOpen(true);

	const UWidget* const RetryBox = Screen->WidgetTree->FindWidget(TEXT("BackpackRetryBox"));
	UButton* const RetryButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("BackpackRetryButton")));
	const int32 WoodIndex = Screen->GetShownSlots().IndexOfByPredicate([](const FInventorySlot& Slot) { return Slot.ItemID == WoodID; });
	if (!TestNotNull(TEXT("다시 시도 단추가 있다"), RetryButton) || RetryBox == nullptr || !TestTrue(TEXT("나무 칸"), WoodIndex != INDEX_NONE))
	{
		return false;
	}
	Screen->SelectSlot(WoodIndex);
	Screen->PressDrop();
	TestEqual(TEXT("DropItem 한 번"), Server.Calls, 1);
	TestFalse(TEXT("처음엔 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	if (!Server.Waiting.IsEmpty())
	{
		FDone Done = MoveTemp(Server.Waiting[0]);
		Server.Waiting.RemoveAt(0);
		Done(false, nullptr, TEXT("net"));
	}
	TestTrue(TEXT("네트워크 실패 → 다시 시도 보임"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	TestFalse(TEXT("이유를 보인다"), Screen->GetStatusText().IsEmpty());
	RetryButton->OnClicked.Broadcast();
	TestEqual(TEXT("다시 시도 → DropItem 다시"), Server.Calls, 2);
	TestEqual(TEXT("같은 함수"), Server.LastFunction, FString(TEXT("DropItem")));
	TestEqual(TEXT("같은 itemId"), Server.LastArgs.IsValid() ? Server.LastArgs->GetStringField(TEXT("itemId")) : FString(), WoodID.ToString());
	TestFalse(TEXT("누른 뒤 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	Server.AnswerFail(TEXT("inventory_full"), TEXT("가방이 가득 찼다"));
	TestFalse(TEXT("서버 거절엔 다시 시도 없음"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
