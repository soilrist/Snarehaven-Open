// ROADMAP M2-06a 2층 검사 — 목장 HUD 가 **백엔드 스냅샷만** 보고 그리는지:
// 빈 목장엔 알림이 없고, 스냅샷에 `Stock` 이 실려 와야 Ready 가 되며, 미수확 상한은
// `DT_RanchProductTable` 에서만 오고(코드에 5 를 적지 않는다), 목장 게임모드만 이 HUD 를 켠다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Data/RanchProductRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Data/AnimalRow.h"
#include "Inventory/InventoryComponent.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchGameMode.h"
#include "Ranch/RanchInteractComponent.h"
#include "Ranch/RanchSyncComponent.h"
#include "Backend/SnareBackendSubsystem.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "SnarehavenCharacter.h"
#include "SnarehavenGameMode.h"
#include "UI/RanchHudWidget.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr int32 TestStock = 3;
	constexpr int32 TestSlotIndex = 0;
	// 화면 가운데로 치는 네모 — 가로·세로 25~75%. 사냥 HUD 시트 대조와 같은 값을 쓴다(M15-02).
	constexpr float TestScreenEdgeFraction = 0.25f;

	// 실제 데이터 테이블 (M2-02). 미수확 상한 기대값을 코드에 적지 않고 여기서 읽는다.
	const TCHAR* RanchTablePath = TEXT("/Game/Game/Data/Ranch/DT_RanchProductTable.DT_RanchProductTable");

	// 실제 동물 표 (M1-01). 어떤 펫 아이템이 어떤 종인지·이름이 뭔지는 여기서만 온다.
	const TCHAR* AnimalTablePath = TEXT("/Game/Game/Data/Animals/DT_AnimalTable.DT_AnimalTable");

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

	// 뷰포트에 올리지 않은 HUD. 2층은 화면 없이 도는 검사라 위젯만 만들어 배선을 본다
	// (`SnarehavenHuntHudTest` 와 같은 길).
	URanchHudWidget* CreateTestHud(UWorld* World, ASnarehavenCharacter* Player)
	{
		URanchHudWidget* Hud = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
		if (Hud != nullptr)
		{
			Hud->BindToCharacter(Player);
		}
		return Hud;
	}

	// 표의 첫 행. 어떤 동물이 목장에 들어갈 수 있는지·상한이 몇인지는 표만 안다.
	const FRanchProductRow* FindFirstProductRow(const UDataTable* Table)
	{
		if (Table == nullptr)
		{
			return nullptr;
		}

		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			if (const FRanchProductRow* Row = reinterpret_cast<const FRanchProductRow*>(Pair.Value))
			{
				return Row;
			}
		}
		return nullptr;
	}

	// 백엔드가 보내 준 셈 치는 목장 스냅샷 — 칸 수는 목장과 같아야 하고, 한 칸만 채운다
	// (칸 내용을 바꾸는 유일한 길, `URanchComponent::ApplyBackendSnapshot`).
	TArray<FRanchSlotState> MakeSnapshotWithStock(const URanchComponent* Ranch, FName AnimalID, int32 Stock)
	{
		TArray<FRanchSlotState> Snapshot;
		Snapshot.SetNum(Ranch->GetSlotCount());
		Snapshot[TestSlotIndex].AnimalID = AnimalID;
		Snapshot[TestSlotIndex].OccupiedSinceUtc = FDateTime::UtcNow();
		Snapshot[TestSlotIndex].Stock = Stock;
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudEmptyTest, "Snarehaven.RanchHud.Notice.EmptyRanchHasNoReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchHudEmptyTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	URanchHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("플레이어 스폰"), Player) || !TestNotNull(TEXT("목장 HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestNotNull(TEXT("HUD 가 플레이어의 목장 캐시를 본다"), Hud->GetWatchedRanch());
	TestEqual(TEXT("스냅샷을 받기 전엔 수확 준비 칸이 없다"), Hud->GetReadySlotCount(), 0);
	TestFalse(TEXT("빈 칸은 Ready 가 아니다"), Hud->IsSlotReady(TestSlotIndex));
	TestEqual(TEXT("빈 칸엔 상한도 없다(미정 -1)"), Hud->GetSlotMaxStack(TestSlotIndex), -1);
	TestTrue(TEXT("빈 칸 줄에 '비어 있음' 이 적힌다"),
		Hud->GetSlotText(TestSlotIndex).ToString().Contains(TEXT("비어 있음")));
	TestFalse(TEXT("넣을 동물·씨앗이 없으면 빈 칸의 넣기 단추가 꺼진다 (M106-03)"), Hud->IsSlotActionEnabled(TestSlotIndex));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudReadyTest, "Snarehaven.RanchHud.Notice.StockFromSnapshotIsReady",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchHudReadyTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	URanchHudWidget* Hud = CreateTestHud(World, Player);
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, RanchTablePath);
	const FRanchProductRow* Row = FindFirstProductRow(Table);
	if (!TestNotNull(TEXT("목장 HUD 생성"), Hud) || !TestNotNull(TEXT("생산 표의 첫 행"), Row))
	{
		DestroyTestWorld(World);
		return false;
	}

	URanchComponent* Ranch = Hud->GetWatchedRanch();
	if (!TestNotNull(TEXT("목장 캐시"), Ranch))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("백엔드 스냅샷이 반영된다"),
		Ranch->ApplyBackendSnapshot(MakeSnapshotWithStock(Ranch, Row->AnimalID, TestStock)));

	TestTrue(TEXT("재고가 쌓인 칸은 Ready 다"), Hud->IsSlotReady(TestSlotIndex));
	TestEqual(TEXT("수확 준비 칸은 한 칸"), Hud->GetReadySlotCount(), 1);

	// 재고 0 짜리 스냅샷이 오면 Ready 가 꺼져야 한다 — 수확 뒤 화면이 그대로 남으면 거짓말이 된다.
	TestTrue(TEXT("재고 0 스냅샷도 반영된다"),
		Ranch->ApplyBackendSnapshot(MakeSnapshotWithStock(Ranch, Row->AnimalID, 0)));
	TestFalse(TEXT("재고가 없으면 Ready 가 아니다"), Hud->IsSlotReady(TestSlotIndex));
	TestEqual(TEXT("알림에 셀 칸이 없다"), Hud->GetReadySlotCount(), 0);

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudMaxStackTest, "Snarehaven.RanchHud.Table.MaxStackComesFromTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchHudMaxStackTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	URanchHudWidget* Hud = CreateTestHud(World, Player);
	const UDataTable* Table = LoadObject<UDataTable>(nullptr, RanchTablePath);
	const FRanchProductRow* Row = FindFirstProductRow(Table);
	if (!TestNotNull(TEXT("목장 HUD 생성"), Hud) || !TestNotNull(TEXT("생산 표의 첫 행"), Row))
	{
		DestroyTestWorld(World);
		return false;
	}

	URanchComponent* Ranch = Hud->GetWatchedRanch();
	if (!TestNotNull(TEXT("목장 캐시"), Ranch)
		|| !TestTrue(TEXT("백엔드 스냅샷이 반영된다"),
			Ranch->ApplyBackendSnapshot(MakeSnapshotWithStock(Ranch, Row->AnimalID, TestStock))))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 기대값을 코드에 적지 않는다 — 표의 값과 같기만 하면 된다 (DataDriven.md 3).
	TestEqual(TEXT("상한은 표의 MaxStack 그대로"), Hud->GetSlotMaxStack(TestSlotIndex), Row->MaxStack);

	const FString SlotText = Hud->GetSlotText(TestSlotIndex).ToString();
	TestTrue(TEXT("칸 줄에 미수확 개수가 보인다"), SlotText.Contains(FString::FromInt(TestStock)));
	TestTrue(TEXT("칸 줄에 표의 상한이 보인다"), SlotText.Contains(FString::FromInt(Row->MaxStack)));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudTreeTest, "Snarehaven.RanchHud.Tree.RowsMatchSlotCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchHudTreeTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	URanchHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("목장 HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// Slate 위젯을 실제로 만들어야 RebuildWidget 이 돌고, 그때 자리표시 트리가 세워진다.
	// **반환값을 붙잡고 있어야 한다** — 놓으면 그 자리에서 해제되며 `NativeDestruct` 가 돌아
	// 목장 연결이 끊긴다(첫 실행에서 실제로 그랬다). 화면에 올라가 있는 동안을 보는 검사다.
	const TSharedRef<SWidget> SlateWidget = Hud->TakeWidget();

	UVerticalBox* SlotList = Hud->WidgetTree->FindWidget<UVerticalBox>(TEXT("SlotList"));
	TestNotNull(TEXT("알림 줄이 있다"), Hud->WidgetTree->FindWidget<UTextBlock>(TEXT("ReadyNoticeText")));
	if (!TestNotNull(TEXT("칸 목록이 있다"), SlotList))
	{
		DestroyTestWorld(World);
		return false;
	}

	const URanchComponent* Ranch = Hud->GetWatchedRanch();
	if (TestNotNull(TEXT("목장 캐시"), Ranch))
	{
		// 줄 수는 목장 칸 수를 따라간다 — HUD 에 5 를 적어 두지 않는다 (DESIGN 3.2).
		TestEqual(TEXT("줄 수가 목장 칸 수와 같다"), SlotList->GetChildrenCount(), Ranch->GetSlotCount());
	}

	// M15-03 — 칸 목록은 화면에 늘어놓지 않고, 칸 카드는 칸 곁에 설 때만 뜬다(여기선 곁에 선 칸이 없다).
	TestEqual(TEXT("칸 목록은 접혀 있다"), SlotList->GetVisibility(), ESlateVisibility::Collapsed);
	if (UWidget* const SlotCard = Hud->WidgetTree->FindWidget(TEXT("SlotCard")); TestNotNull(TEXT("칸 카드가 있다"), SlotCard))
	{
		TestEqual(TEXT("곁에 선 칸이 없으면 칸 카드가 접힌다"), SlotCard->GetVisibility(), ESlateVisibility::Collapsed);
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudPetPickerTest, "Snarehaven.RanchHud.PetPicker.ShowsOwnedPet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchHudPetPickerTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	URanchHudWidget* Hud = CreateTestHud(World, Player);
	UDataTable* AnimalTable = LoadObject<UDataTable>(nullptr, AnimalTablePath);
	if (!TestNotNull(TEXT("목장 HUD 생성"), Hud) || !TestNotNull(TEXT("DT_AnimalTable"), AnimalTable)
		|| !TestNotNull(TEXT("플레이어 스폰"), Player))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 트리를 세워야 고르기 줄 위젯이 생긴다. 반환값을 놓으면 그 자리에서 해제된다 (Tree 검사와 같은 함정).
	const TSharedRef<SWidget> SlateWidget = Hud->TakeWidget();

	UButton* PickerButton = Hud->WidgetTree->FindWidget<UButton>(TEXT("PetPickerButton"));
	UTextBlock* PickerText = Hud->WidgetTree->FindWidget<UTextBlock>(TEXT("PetPickerText"));
	if (!TestNotNull(TEXT("고르기 버튼이 있다"), PickerButton) || !TestNotNull(TEXT("고르기 줄이 있다"), PickerText))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 가진 펫이 없을 때 — 종 이름을 지어내지 않고 버튼을 꺼 둔다 (숨기지 않는다).
	TestFalse(TEXT("펫이 없으면 고르기를 끈다"), Hud->IsPetPickerEnabled());
	TestFalse(TEXT("펫이 없으면 버튼이 꺼져 있다"), PickerButton->GetIsEnabled());

	// 표에서 펫이 되는 동물을 찾아 백엔드가 보내 준 셈 치고 캐시에 넣는다 — 기대값을 코드에 적지 않는다.
	FName ExpectedAnimalID = NAME_None;
	FText ExpectedDisplayName;
	FInventorySlot OwnedSlot;
	for (const TPair<FName, uint8*>& Pair : AnimalTable->GetRowMap())
	{
		const FAnimalRow* Row = reinterpret_cast<const FAnimalRow*>(Pair.Value);
		if (Row != nullptr && !Row->PetItemID.IsNone())
		{
			ExpectedAnimalID = Row->AnimalID;
			ExpectedDisplayName = Row->DisplayName;
			OwnedSlot.ItemID = Row->PetItemID;
			OwnedSlot.Count = 1;
			break;
		}
	}
	UInventoryComponent* Inventory = Player->GetInventory();
	if (!TestNotNull(TEXT("인벤토리 캐시"), Inventory)
		|| !TestFalse(TEXT("표에 펫이 되는 동물이 한 종은 있다"), ExpectedAnimalID.IsNone()))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("펫을 가진 캐시를 백엔드가 보내 준다"), Inventory->ApplyBackendSnapshot({ OwnedSlot }));

	// 인벤토리 스냅샷만으로 고르기 줄이 따라와야 한다 — 목장 알림만 듣고 있으면 옛 목록이 남는다.
	TestTrue(TEXT("펫이 생기면 고르기가 켜진다"), Hud->IsPetPickerEnabled());
	TestTrue(TEXT("고르기 줄에 표의 이름이 보인다"),
		PickerText->GetText().ToString().Contains(ExpectedDisplayName.ToString()));

	// 화면은 고르기만 바꾼다 — 칸은 백엔드 스냅샷이 올 때만 찬다 (CLAUDE.md 8).
	const URanchComponent* Ranch = Hud->GetWatchedRanch();
	if (TestNotNull(TEXT("목장 캐시"), Ranch))
	{
		TestTrue(TEXT("펫을 가져도 칸은 비어 있다"), Ranch->IsSlotEmpty(TestSlotIndex));
		TestEqual(TEXT("펫을 가져도 요청은 0건"), Ranch->GetPendingRequests().Num(), 0);
	}

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudGameModeTest, "Snarehaven.Ranch.GameMode.ShowsRanchHud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchHudGameModeTest::RunTest(const FString& Parameters)
{
	const ASnarehavenGameMode* HuntGameMode = GetDefault<ASnarehavenGameMode>();
	const ARanchGameMode* RanchGameMode = GetDefault<ARanchGameMode>();
	if (!TestNotNull(TEXT("사냥 게임모드 기본값"), HuntGameMode)
		|| !TestNotNull(TEXT("목장 게임모드 기본값"), RanchGameMode))
	{
		return false;
	}

	TestFalse(TEXT("사냥 맵엔 목장 HUD 를 띄우지 않는다"), HuntGameMode->ShouldShowRanchHud());
	TestTrue(TEXT("목장 맵엔 목장 HUD 를 띄운다"), RanchGameMode->ShouldShowRanchHud());
	TestFalse(TEXT("목장 맵엔 사냥 HUD 를 여전히 띄우지 않는다"), RanchGameMode->ShouldShowHuntHud());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudUpkeepTest, "Snarehaven.RanchHud.Upkeep.TextComesFromSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ROADMAP M12-05 2층 — 유지·명성 줄의 숫자가 **스냅샷 값에서만** 오는지. 받기 전엔 비어 있고,
// 받은 값을 바꾸면 글자도 바뀌며, 잠김·시작 전 문구도 서버 플래그만 보고 고른다.
bool FSnarehavenRanchHudUpkeepTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	URanchHudWidget* Hud = CreateTestHud(World, Player);
	URanchComponent* Ranch = Hud != nullptr ? Hud->GetWatchedRanch() : nullptr;
	if (!TestNotNull(TEXT("목장 HUD 생성"), Hud) || !TestNotNull(TEXT("목장 캐시"), Ranch))
	{
		DestroyTestWorld(World);
		return false;
	}

	TestTrue(TEXT("스냅샷 전엔 유지 줄이 비어 있다"), Hud->GetUpkeepText().IsEmpty());

	// 테스트 전용 값 (Verification.md 6).
	FRanchUpkeepState Upkeep;
	Upkeep.LeftHours = 37;
	Upkeep.Fame = 123;
	Upkeep.SlotCap = 7;
	Ranch->ApplyBackendUpkeep(Upkeep);
	const FString Left = Hud->GetUpkeepText().ToString();
	TestTrue(FString::Printf(TEXT("남은 시간·명성·상한이 스냅샷 값 (%s)"), *Left),
		Left.Contains(TEXT("37")) && Left.Contains(TEXT("123")) && Left.Contains(TEXT("/7")));
	TestFalse(TEXT("안 잠긴 목장엔 잠김 문구 없음"), Left.Contains(TEXT("잠김")));

	Upkeep.LeftHours = 0;
	Upkeep.bLocked = true;
	Upkeep.Fame = 45;
	Ranch->ApplyBackendUpkeep(Upkeep);
	const FString Locked = Hud->GetUpkeepText().ToString();
	TestTrue(FString::Printf(TEXT("서버가 잠갔다고 하면 잠김 (%s)"), *Locked), Locked.Contains(TEXT("잠김")));
	TestTrue(TEXT("새 명성 값이 바로 보인다"), Locked.Contains(TEXT("45")) && !Locked.Contains(TEXT("123")));

	Upkeep.LeftHours = -1;
	Upkeep.bLocked = false;
	Ranch->ApplyBackendUpkeep(Upkeep);
	TestTrue(TEXT("시작 전 문구"), Hud->GetUpkeepText().ToString().Contains(TEXT("시작")));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudSheetCornersTest, "Snarehaven.RanchHud.Tree.CornersMatchReferenceSheet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-03 — 목장 화면도 시트 `docs/reference/hud_ingame.md` 의 "네 귀퉁이만 쓰고 가운데는 비운다" 를
// 따르는지 2층에서 본다(사냥 HUD 의 `HuntHud.Tree.CornersMatchReferenceSheet` 와 같은 자).
// 목장 HUD 는 감독 커밋 `66127f8` 이 디버그 글자 목록·넓은 버튼 줄을 걷어냈는데 2층 자가 없었다 —
// 다음 조각이 화면 한가운데에 위젯을 얹으면 조용히 되돌아간다. 판정은 이름이 아니라
// **캔버스 슬롯의 앵커와 보임 상태**로 한다.
bool FSnarehavenRanchHudSheetCornersTest::RunTest(const FString& Parameters)
{
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	URanchHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("목장 HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}

	// 반환값을 붙잡고 있어야 트리가 산다 (같은 파일 `Tree.RowsMatchSlotCount` 의 함정 주석 참고).
	const TSharedRef<SWidget> SlateWidget = Hud->TakeWidget();
	Hud->RefreshDisplay();

	UCanvasPanel* const Root = Cast<UCanvasPanel>(Hud->WidgetTree->RootWidget);
	if (!TestNotNull(TEXT("목장 HUD 루트가 캔버스다"), Root))
	{
		DestroyTestWorld(World);
		return false;
	}

	// ① 가운데는 비운다 — 보이는 칸의 앵커 가운데가 화면 가운데 네모 안이면 이름을 대고 깨진다.
	TArray<FString> Middle;
	int32 Visible = 0;
	for (int32 Index = 0; Index < Root->GetChildrenCount(); ++Index)
	{
		const UWidget* const Child = Root->GetChildAt(Index);
		const UCanvasPanelSlot* const Slot = Child != nullptr ? Cast<UCanvasPanelSlot>(Child->Slot) : nullptr;
		if (Slot == nullptr || Child->GetVisibility() == ESlateVisibility::Collapsed
			|| Child->GetVisibility() == ESlateVisibility::Hidden)
		{
			continue;
		}
		++Visible;
		const FVector2D AnchorMid = (Slot->GetAnchors().Minimum + Slot->GetAnchors().Maximum) * 0.5f;
		if (AnchorMid.X > TestScreenEdgeFraction && AnchorMid.X < 1.f - TestScreenEdgeFraction
			&& AnchorMid.Y > TestScreenEdgeFraction && AnchorMid.Y < 1.f - TestScreenEdgeFraction)
		{
			Middle.Add(Child->GetName());
		}
	}
	TestEqual(TEXT("화면 가운데에 보이는 칸이 없다"), FString::Join(Middle, TEXT(", ")), FString());

	// ② 평소 화면은 비어 있다 — 칸 곁에 서기 전에는 칸 목록도 칸 카드도 접혀 있다
	//    (옛 화면은 칸 5줄을 늘 늘어놓았다. 시트에 그런 목록이 없다).
	const UWidget* const SlotList = Hud->WidgetTree->FindWidget(TEXT("SlotList"));
	const UWidget* const SlotCard = Hud->WidgetTree->FindWidget(TEXT("SlotCard"));
	if (TestNotNull(TEXT("칸 목록이 트리에 있다"), SlotList) && TestNotNull(TEXT("칸 카드가 트리에 있다"), SlotCard))
	{
		TestEqual(TEXT("칸 목록은 접혀 있다"), SlotList->GetVisibility(), ESlateVisibility::Collapsed);
		TestEqual(TEXT("곁에 선 칸이 없으면 칸 카드도 접힌다"), SlotCard->GetVisibility(), ESlateVisibility::Collapsed);
	}

	// ③ 오른쪽 위는 비운다 — 잔액 알약은 사냥 화면과 같은 한 곳(`SnareBalanceHud`)이 접는다.
	const UWidget* const BalancePill = Hud->WidgetTree->FindWidget(TEXT("BalancePill"));
	if (TestNotNull(TEXT("잔액 알약이 트리에 있다"), BalancePill))
	{
		TestEqual(TEXT("잔액 알약은 접혀 있다"), BalancePill->GetVisibility(), ESlateVisibility::Collapsed);
	}

	// ④ 왼쪽 위 알림 카드는 거둘 것이 있을 때만 (빈 목장에서 뜨면 늘 떠 있는 글자가 된다).
	const UWidget* const NoticeCard = Hud->WidgetTree->FindWidget(TEXT("ReadyNoticeCard"));
	if (TestNotNull(TEXT("알림 카드가 트리에 있다"), NoticeCard))
	{
		TestEqual(TEXT("거둘 것이 없으면 알림 카드가 접힌다"), NoticeCard->GetVisibility(), ESlateVisibility::Collapsed);
	}

	// ⑤ 오른쪽 아래 엄지 자리 — 포탈 버튼은 포탈 곁에서만 뜨고, 뜰 때 오른쪽 아래에 선다.
	const UWidget* const TravelButton = Hud->WidgetTree->FindWidget(TEXT("ZoneTravelButton"));
	if (TestNotNull(TEXT("포탈 버튼이 트리에 있다"), TravelButton))
	{
		if (const UCanvasPanelSlot* const TravelSlot = Cast<UCanvasPanelSlot>(TravelButton->Slot))
		{
			TestEqual(TEXT("포탈 버튼은 오른쪽 아래 귀퉁이"), TravelSlot->GetAnchors().Minimum, FVector2D(1.f, 1.f));
		}
	}

	AddInfo(FString::Printf(
		TEXT("[RanchHud] 시트 대조 - 가운데 칸 %d개 · 칸 목록 접힘 · 칸 카드 접힘 · 잔액 알약 접힘 · 보이는 칸 %d개"),
		Middle.Num(), Visible));

	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudCardButtonSizeTest, "Snarehaven.RanchHud.Tree.CardButtonsTouchSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 루프#1221 — 칸 카드 버튼 열한 개가 전부 누르는 칸 최소 크기(DESIGN 2 「누르는 칸 최소 크기」 72)를 지킨다.
// 카드는 접힌 채라 `Snarehaven.UI.TouchSize` 조사가 못 재서(숨김), 1334×616 캡처에서 18px 짜리 버튼이 통과해 있었다.
// 판정은 버튼 안 글자 상자의 최소 높이 값으로 한다 (테스트 기준은 시험 전용 72 — 설계 숫자 바뀌면 같이 본다).
bool FSnarehavenRanchHudCardButtonSizeTest::RunTest(const FString& Parameters)
{
	constexpr float TestTouchFloor = 72.f;
	UWorld* World = CreateTestWorld();
	ASnarehavenCharacter* Player = World->SpawnActor<ASnarehavenCharacter>();
	URanchHudWidget* Hud = CreateTestHud(World, Player);
	if (!TestNotNull(TEXT("목장 HUD 생성"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	// 반환값을 붙잡고 있어야 트리가 산다.
	const TSharedRef<SWidget> SlateWidget = Hud->TakeWidget();
	const TCHAR* const ButtonNames[] = {
		TEXT("SlotHarvestButton"), TEXT("PetPickerButton"), TEXT("RemovePetButton"), TEXT("RemovePetConfirmButton"),
		TEXT("RemovePetCancelButton"), TEXT("PetCosmeticPickerButton"), TEXT("PetCosmeticApplyButton"),
		TEXT("FeedUpkeepButton"), TEXT("ExpandSlotButton"), TEXT("PlacementAnimalTabButton"), TEXT("PlacementSeedTabButton")};
	for (const TCHAR* const Name : ButtonNames)
	{
		const UButton* const Button = Cast<UButton>(Hud->WidgetTree->FindWidget(Name));
		if (!TestNotNull(FString::Printf(TEXT("%s 가 트리에 있다"), Name), Button))
		{
			continue;
		}
		const USizeBox* const MinBox = Cast<USizeBox>(Button->GetContent());
		if (TestNotNull(FString::Printf(TEXT("%s 안에 최소 크기 상자가 있다"), Name), MinBox))
		{
			TestTrue(FString::Printf(TEXT("%s 최소 높이 %.0f ≥ %.0f"), Name, MinBox->GetMinDesiredHeight(), TestTouchFloor),
				MinBox->IsMinDesiredHeightOverride() && MinBox->GetMinDesiredHeight() >= TestTouchFloor);
		}
	}
	DestroyTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchHudRetryTest, "Snarehaven.RanchHud.Notice.BaseStateRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchHudRetryTest::RunTest(const FString& Parameters)
{
	// 거점 상태(`GetBaseState`)를 못 받으면 목장 화면에 이유와 「다시 시도」 — 누르면 다시 묻는다. 로그만 남기지 않는다.
	TArray<USnareBackendSubsystem::FServerCallDone> Waiting;
	TArray<FString> Names;
	USnareBackendSubsystem::TestServerOverride =
		[&Waiting, &Names](const FString& Name, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			Names.Add(Name);
			Waiting.Add(MoveTemp(OnDone));
		};
	UWorld* const World = CreateTestWorld();
	URanchHudWidget* const Hud = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
	Hud->TakeWidget();
	URanchSyncComponent* const Sync = NewObject<URanchSyncComponent>(GetTransientPackage());
	Sync->BindTargets(NewObject<URanchComponent>(GetTransientPackage()), nullptr,
		NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage())), nullptr, nullptr);
	Hud->BindToRanchSync(Sync);

	const UWidget* const RetryBox = Hud->WidgetTree->FindWidget(TEXT("RanchRetryBox"));
	UButton* const RetryButton = Cast<UButton>(Hud->WidgetTree->FindWidget(TEXT("RanchRetryButton")));
	TestNotNull(TEXT("다시 시도 단추가 있다"), RetryButton);
	if (RetryBox != nullptr && RetryButton != nullptr && TestTrue(TEXT("GetBaseState 를 묻는다"), Sync->RequestBaseState()))
	{
		TestFalse(TEXT("처음엔 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		USnareBackendSubsystem::FServerCallDone Done = MoveTemp(Waiting.Last());
		Done(false, nullptr, TEXT("net"));
		TestTrue(TEXT("실패 → 다시 시도 보임"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		TestFalse(TEXT("이유를 화면에 보인다"), Hud->GetFailureNoticeText().IsEmpty());
		const int32 Before = Names.Num();
		RetryButton->OnClicked.Broadcast();
		TestEqual(TEXT("다시 시도 → GetBaseState 다시"), Names.Num() > Before ? Names.Last() : FString(), URanchSyncComponent::GetBaseStateFunctionName());
		TestFalse(TEXT("누른 뒤 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
		TestTrue(TEXT("누른 뒤 이유 접힘"), Hud->GetFailureNoticeText().IsEmpty());
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
