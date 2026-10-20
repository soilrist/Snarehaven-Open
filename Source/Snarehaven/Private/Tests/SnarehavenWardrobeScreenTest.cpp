// 스킨 조각 e (`docs/specs/skin-parts.md` 5장 5 · 6장 e) 2층 검사 — 옷장 화면 `UWardrobeWidget`.
//  ① 칸 버튼 → 격자가 그 칸 꾸미기만 · `내 꾸미기` 탭 = 서버 스냅샷 배낭에 있는 것만 · `전체` = 다
//  ② 원판 바탕 = 등급 색 (원본 옷장 `steam_06.jpg`) · 등급 순
//  ③ 가진 원판 누름 = `SetOutfit` 한 번, 답 전엔 겉모습 그대로 · 입은 것 다시 누름 = 벗기 · 안 가진 것 = 서버 호출 0
//  ④ 회전은 몸 메시만 돌리고 닫으면 되돌린다
// 가짜 서버(`TestServerOverride`)로 진짜 응답 길을 태운다. 꾸미기 행은 아직 표에 없다(메시는 조각 f~l) —
// 표 사본에 **검사 전용 행**을 더한다 (Verification.md 6, `SnarehavenOutfitRequestTest` 와 같은 방식).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/ItemRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Inventory/BackpackComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/SnareUiStyle.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/SnareStoreSettings.h"
#include "UI/WardrobeWidget.h"
#include "UI/WorldMapScreenWidget.h"

namespace SnareWardrobeScreenTest
{
	const TCHAR* ItemTablePath = TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable");
	const FName MeshSourceRow(TEXT("Item_CookHat"));

	// 검사 전용 행 (Verification.md 6 — 설계 데이터가 아니다). 모자 셋(가진 일반·가진 희귀·안 가진 에픽) + 신발 하나.
	const FName HatCommonRow(TEXT("Item_TestWardrobeHatA"));
	const FName HatCommonID(TEXT("test.wardrobe_hat_a"));
	const FName HatRareRow(TEXT("Item_TestWardrobeHatC"));
	const FName HatRareID(TEXT("test.wardrobe_hat_c"));
	const FName HatEpicRow(TEXT("Item_TestWardrobeHatB"));
	const FName HatEpicID(TEXT("test.wardrobe_hat_b"));
	const FName ShoesRow(TEXT("Item_TestWardrobeShoes"));
	const FName ShoesID(TEXT("test.wardrobe_shoes"));

	using FDone = USnareBackendSubsystem::FServerCallDone;

	/** 부른 함수·인자를 적어 두고 답은 검사가 줄 때까지 붙잡는다 (`SnarehavenOutfitRequestTest` 와 같은 꼴). */
	struct FFakeServer
	{
		TArray<FString> Functions;
		TSharedPtr<FJsonObject> LastArgs;
		TArray<FDone> Waiting;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone OnDone)
				{
					Functions.Add(Name);
					LastArgs = Args;
					Waiting.Add(MoveTemp(OnDone));
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		int32 Count(const TCHAR* Name) const
		{
			return Functions.FilterByPredicate([Name](const FString& Each) { return Each == Name; }).Num();
		}

		void AnswerAccepted()
		{
			if (Waiting.IsEmpty())
			{
				return;
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
			Reply->SetStringField(TEXT("slot"), LastArg(TEXT("slot")));
			Reply->SetStringField(TEXT("itemId"), LastArg(TEXT("itemId")));
			Done(true, Reply, FString());
		}

		FString LastArg(const TCHAR* Field) const
		{
			return LastArgs.IsValid() ? LastArgs->GetStringField(Field) : FString(TEXT("?"));
		}
	};

	/** 표 사본(검사 전용 꾸미기 행 넷)을 건 플레이어 + 백엔드에 묶은 배낭 + 옷장 화면. */
	struct FRig
	{
		UWorld* World = nullptr;
		UDataTable* Table = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		ASnarehavenCharacter* Player = nullptr;
		UEquipmentComponent* Equipment = nullptr;
		UBackpackComponent* Backpack = nullptr;
		UWardrobeWidget* Screen = nullptr;

		void AddCosmetic(const FItemRow& Source, FName Row, FName ItemID, EEquipSlot Slot, EItemGrade Grade)
		{
			FItemRow Cosmetic = Source;
			Cosmetic.ItemID = ItemID;
			Cosmetic.ItemType = EItemType::Cosmetic;
			Cosmetic.EquipSlot = Slot;
			Cosmetic.Grade = Grade;
			Cosmetic.DisplayName = FText::FromName(ItemID);
			Table->AddRow(Row, Cosmetic);
		}

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			const UDataTable* const Original = LoadObject<UDataTable>(nullptr, ItemTablePath);
			const FItemRow* const Source = Original != nullptr ? Original->FindRow<FItemRow>(MeshSourceRow, TEXT("WardrobeTest")) : nullptr;
			if (Source == nullptr)
			{
				return;
			}
			Table = DuplicateObject<UDataTable>(Original, GetTransientPackage());
			AddCosmetic(*Source, HatCommonRow, HatCommonID, EEquipSlot::Head, EItemGrade::Common);
			AddCosmetic(*Source, HatEpicRow, HatEpicID, EEquipSlot::Head, EItemGrade::Epic);
			AddCosmetic(*Source, HatRareRow, HatRareID, EEquipSlot::Head, EItemGrade::Rare);
			AddCosmetic(*Source, ShoesRow, ShoesID, EEquipSlot::Shoes, EItemGrade::Rare);

			Player = World->SpawnActor<ASnarehavenCharacter>();
			if (Player == nullptr || !Player->ApplyPlayerBody())
			{
				Player = nullptr;
				return;
			}
			Equipment = Player->GetEquipment();
			const FObjectProperty* const EquipTableProp =
				FindFProperty<FObjectProperty>(UEquipmentComponent::StaticClass(), TEXT("ItemTable"));
			const FObjectProperty* const ScreenTableProp =
				FindFProperty<FObjectProperty>(UWardrobeWidget::StaticClass(), TEXT("ItemTable"));
			if (Equipment == nullptr || EquipTableProp == nullptr || ScreenTableProp == nullptr)
			{
				Player = nullptr;
				return;
			}
			EquipTableProp->SetObjectPropertyValue_InContainer(Equipment, Table);

			UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
			Backend = NewObject<USnareBackendSubsystem>(GameInstance);
			Backpack = Player->FindComponentByClass<UBackpackComponent>();
			if (Backpack == nullptr)
			{
				Backpack = NewObject<UBackpackComponent>(Player, TEXT("WardrobeTestBackpack"));
				Backpack->RegisterComponent();
			}
			Backpack->BindTargets(Backend, nullptr);

			// 서버 스냅샷 배낭 — 일반·희귀 모자와 신발만 가졌다. 에픽 모자는 없다.
			FSnarePlayerSnapshot Snapshot;
			Snapshot.PlayFabId = TEXT("PFTESTOWNER");
			Snapshot.FetchedAtUtc = FDateTime::UtcNow();
			Snapshot.AddItem(HatCommonID, 1);
			Snapshot.AddItem(HatRareID, 1);
			Snapshot.AddItem(ShoesID, 1);
			Backend->ApplyPlayerSnapshot(Snapshot, true);

			Screen = CreateWidget<UWardrobeWidget>(World, UWardrobeWidget::StaticClass());
			ScreenTableProp->SetObjectPropertyValue_InContainer(Screen, Table);
			Screen->TakeWidget();
			Screen->BindTargets(Backend, Backpack, Equipment);
		}

		/** 화면 격자에서 검사 전용 행만 (표에 진짜 꾸미기가 들어와도 안 깨지게). */
		TArray<FName> TestTileIDs() const
		{
			TArray<FName> IDs;
			for (const FWardrobeTile& Tile : Screen->GetShownTiles())
			{
				if (Tile.ItemID.ToString().StartsWith(TEXT("test.wardrobe_")))
				{
					IDs.Add(Tile.ItemID);
				}
			}
			return IDs;
		}

		int32 TileIndex(FName ItemID) const
		{
			return Screen->GetShownTiles().IndexOfByPredicate([ItemID](const FWardrobeTile& Tile) { return Tile.ItemID == ItemID; });
		}

		void Click(const TCHAR* WidgetName) const
		{
			if (UButton* const Button = Cast<UButton>(Screen->WidgetTree->FindWidget(FName(WidgetName))))
			{
				Button->OnClicked.Broadcast();
			}
		}

		void ClickTile(FName ItemID) const
		{
			if (UButton* const Button = Screen->GetTileButton(TileIndex(ItemID)))
			{
				Button->OnClicked.Broadcast();
			}
		}

		~FRig()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	FString Join(const TArray<FName>& IDs)
	{
		return FString::JoinBy(IDs, TEXT(","), [](FName ID) { return ID.ToString(); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWardrobeScreenFilterTest, "Snarehaven.UI.Wardrobe.SlotAndTabFilter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWardrobeScreenFilterTest::RunTest(const FString& Parameters)
{
	using namespace SnareWardrobeScreenTest;

	FFakeServer Server;
	Server.Install();
	FRig Rig;
	if (!TestNotNull(TEXT("표 사본을 건 플레이어"), Rig.Player) || !TestNotNull(TEXT("옷장 화면"), Rig.Screen))
	{
		return false;
	}

	TestEqual(TEXT("칸 버튼 7"), UWardrobeWidget::GetSlotOrder().Num(), 7);
	int32 SlotButtons = 0;
	while (Rig.Screen->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("WardrobeSlot_%d"), SlotButtons))) != nullptr)
	{
		++SlotButtons;
	}
	TestEqual(TEXT("화면 칸 버튼 7"), SlotButtons, 7);
	TestNotNull(TEXT("회전 버튼"), Rig.Screen->WidgetTree->FindWidget(TEXT("WardrobeRotate")));

	// ① 처음 = 모자 칸 · `내 꾸미기` — 가진 일반·희귀만, 등급 순.
	TestEqual(TEXT("처음 칸 = 모자"), Rig.Screen->GetSelectedSlot(), EEquipSlot::Head);
	TestTrue(TEXT("처음 탭 = 내 꾸미기"), Rig.Screen->IsOwnedOnly());
	const FString OwnedHats = Join(Rig.TestTileIDs());
	TestEqual(TEXT("내 꾸미기 모자 = 가진 것만"), OwnedHats, Join({ HatCommonID, HatRareID }));

	// ② 원판 바탕 = 등급 색.
	TestEqual(TEXT("일반 원판 = 초록"), Rig.Screen->GetTileColor(Rig.TileIndex(HatCommonID)), SnareUiStyle::ItemGradeColor(EItemGrade::Common));
	TestEqual(TEXT("희귀 원판 = 주황"), Rig.Screen->GetTileColor(Rig.TileIndex(HatRareID)), SnareUiStyle::ItemGradeColor(EItemGrade::Rare));

	// `전체` 탭 — 안 가진 에픽까지, 에픽은 끝.
	Rig.Click(TEXT("WardrobeAllTab"));
	TestFalse(TEXT("전체 탭으로"), Rig.Screen->IsOwnedOnly());
	const FString AllHats = Join(Rig.TestTileIDs());
	TestEqual(TEXT("전체 모자 = 셋, 등급 순"), AllHats, Join({ HatCommonID, HatRareID, HatEpicID }));
	TestEqual(TEXT("에픽 원판 = 보라"), Rig.Screen->GetTileColor(Rig.TileIndex(HatEpicID)), SnareUiStyle::ItemGradeColor(EItemGrade::Epic));

	// 칸 버튼 → 그 칸만. 신발 칸(차례 4)엔 신발 하나, 장갑 칸(차례 5)엔 없다.
	Rig.Click(TEXT("WardrobeSlot_4"));
	TestEqual(TEXT("신발 칸 고름"), Rig.Screen->GetSelectedSlot(), EEquipSlot::Shoes);
	const FString Shoes = Join(Rig.TestTileIDs());
	TestEqual(TEXT("신발 칸 = 신발만"), Shoes, Join({ ShoesID }));
	Rig.Click(TEXT("WardrobeSlot_5"));
	const FString Gloves = Join(Rig.TestTileIDs());
	TestEqual(TEXT("장갑 칸 = 검사 행 없음"), Gloves, FString());
	Rig.Click(TEXT("WardrobeOwnedTab"));
	TestTrue(TEXT("내 꾸미기로 돌아옴"), Rig.Screen->IsOwnedOnly());
	TestEqual(TEXT("화면 여는 데 서버 호출 0"), Server.Functions.Num(), 0);

	AddInfo(FString::Printf(TEXT("[Wardrobe] 칸 버튼 %d · 내 꾸미기 모자 [%s] · 전체 모자 [%s] · 신발 [%s] · 장갑 [%s]"),
		SlotButtons, *OwnedHats, *AllHats, *Shoes, *Gloves));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWardrobeScreenPressTest, "Snarehaven.UI.Wardrobe.PressWaitsForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWardrobeScreenPressTest::RunTest(const FString& Parameters)
{
	using namespace SnareWardrobeScreenTest;

	FFakeServer Server;
	Server.Install();
	FRig Rig;
	if (!TestNotNull(TEXT("표 사본을 건 플레이어"), Rig.Player) || !TestNotNull(TEXT("옷장 화면"), Rig.Screen))
	{
		return false;
	}

	// ③ 안 가진 에픽 — 서버에 안 묻고 상점 안내 줄만.
	Rig.Click(TEXT("WardrobeAllTab"));
	Rig.ClickTile(HatEpicID);
	TestEqual(TEXT("안 가진 것 누름 = 서버 호출 0"), Server.Functions.Num(), 0);
	TestFalse(TEXT("안 가진 것 누름 = 안내 줄"), Rig.Screen->GetMessage().IsEmpty());
	TestTrue(TEXT("안 가진 것 = 덮개 없음"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head).IsNone());

	// 가진 일반 — `SetOutfit` 한 번, 답 전엔 그대로, 기다리는 동안 다른 원판은 안 눌림.
	Rig.ClickTile(HatCommonID);
	TestEqual(TEXT("가진 것 누름 = SetOutfit 한 번"), Server.Count(TEXT("SetOutfit")), 1);
	TestEqual(TEXT("보낸 slot"), Server.LastArg(TEXT("slot")), FString(TEXT("head")));
	TestEqual(TEXT("보낸 itemId"), Server.LastArg(TEXT("itemId")), HatCommonID.ToString());
	TestTrue(TEXT("답 기다림"), Rig.Screen->IsPending());
	TestTrue(TEXT("답 전엔 덮개 없음"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head).IsNone());
	TestFalse(TEXT("기다리는 동안 또 누름 = 안 보냄"), Rig.Screen->PressTile(Rig.TileIndex(HatRareID)));
	TestEqual(TEXT("여전히 한 번"), Server.Count(TEXT("SetOutfit")), 1);

	Server.AnswerAccepted();
	TestFalse(TEXT("답 뒤 기다림 끝"), Rig.Screen->IsPending());
	TestEqual(TEXT("받아들인 뒤 덮개 = 일반 모자 행"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head), HatCommonRow);

	// 입은 것을 다시 누름 = 벗기(빈 itemId), 답 뒤에만.
	Rig.ClickTile(HatCommonID);
	TestEqual(TEXT("벗기 요청"), Server.Count(TEXT("SetOutfit")), 2);
	TestEqual(TEXT("벗기 itemId = 빈 문자열"), Server.LastArg(TEXT("itemId")), FString());
	TestEqual(TEXT("답 전엔 아직 입음"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head), HatCommonRow);
	Server.AnswerAccepted();
	TestTrue(TEXT("답 뒤 벗음"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head).IsNone());

	// ④ 회전 — 몸 메시만 180°, 닫으면 처음대로.
	USkeletalMeshComponent* const Mesh = Rig.Player->GetMesh();
	const FRotator Before = Mesh->GetRelativeRotation();
	Rig.Click(TEXT("WardrobeRotate"));
	TestTrue(TEXT("회전 켜짐"), Rig.Screen->IsRotated());
	const float TurnedYaw = FRotator::NormalizeAxis(Mesh->GetRelativeRotation().Yaw - Before.Yaw);
	TestEqual(TEXT("몸 메시 180° 돌아감"), FMath::Abs(TurnedYaw), 180.f, 0.01f);
	const FRotator ActorBefore = Rig.Player->GetActorRotation();
	Rig.Click(TEXT("WardrobeQuit"));
	TestFalse(TEXT("나가면 회전 꺼짐"), Rig.Screen->IsRotated());
	TestTrue(TEXT("나가면 몸 메시 처음대로"), Mesh->GetRelativeRotation().Equals(Before, 0.01f));
	TestTrue(TEXT("액터 방향은 손 안 댐"), Rig.Player->GetActorRotation().Equals(ActorBefore, 0.01f));

	AddInfo(FString::Printf(TEXT("[Wardrobe] 안 가진 것 호출 0 · SetOutfit %d번(입기·벗기) · 답 전 덮개 0 · 회전 %.1f° → 나가기 뒤 0"),
		Server.Count(TEXT("SetOutfit")), TurnedYaw));
	return true;
}

// 옷장 보정(레퍼런스 대조) — ① 옷장이 열리면 뒤 배낭 판을 숨기고 닫으면 되살린다 ③ 젬이 꺼지면 빈 칸 글·상점 단추가 닿는 길만 말한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWardrobeScreenCoverTest, "Snarehaven.UI.Wardrobe.HidesBackpackAndNoGemsText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWardrobeScreenCoverTest::RunTest(const FString& Parameters)
{
	using namespace SnareWardrobeScreenTest;

	FFakeServer Server;
	Server.Install();
	FRig Rig;
	if (!TestNotNull(TEXT("표 사본을 건 플레이어"), Rig.Player) || !TestNotNull(TEXT("옷장 화면"), Rig.Screen))
	{
		return false;
	}

	// ① 배낭 판 ↔ 옷장.
	UBackpackScreenWidget* const Pack = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	Pack->TakeWidget();
	Pack->SetPanelOpen(true);
	TestTrue(TEXT("옷장 전: 배낭 판 보임"), Pack->IsPanelShown());
	UWorldMapScreenWidget* const SideRail = CreateWidget<UWorldMapScreenWidget>(Rig.World, UWorldMapScreenWidget::StaticClass());
	SideRail->TakeWidget();
	const ESlateVisibility RailBefore = SideRail->GetVisibility();
	TestNotEqual(TEXT("옷장 전: 세로 단추 줄이 접혀 있지 않음"), RailBefore, ESlateVisibility::Collapsed);
	Pack->PressWardrobe();
	TestNotNull(TEXT("옷장 열림"), Pack->GetWardrobe());
	TestFalse(TEXT("옷장 열림: 배낭 판 숨김"), Pack->IsPanelShown());
	TestEqual(TEXT("옷장 열림: 오른쪽 세로 단추 줄 숨김"), SideRail->GetVisibility(), ESlateVisibility::Collapsed);
	Pack->GetWardrobe()->Close();
	TestEqual(TEXT("옷장 닫힘: 세로 단추 줄 되살아남"), SideRail->GetVisibility(), RailBefore);
	TestTrue(TEXT("옷장 닫힘: 배낭 판 되살아남"), Pack->IsPanelShown());

	// ③ 젬 꺼짐: 빈 칸 글에 「상점 열기」 가 없고 「얻는다」 길을 쓴다. 젬 켜짐은 옛 글.
	{
		const FScopedGemsEnabled GemsOff(false);
		Rig.Click(TEXT("WardrobeSlot_5"));   // 장갑 칸 — 검사 행 없음
		Rig.Screen->SelectOwnedTab(true);
		const FString Off = Rig.Screen->GetShownTiles().IsEmpty() ? Rig.Screen->GetMessageShown() : FString();
		TestFalse(TEXT("젬 꺼짐: 빈 칸 글에 상점 열기 없음"), Off.Contains(TEXT("상점")));
		TestTrue(TEXT("젬 꺼짐: 빈 칸 글에 얻는 길"), Off.Contains(TEXT("얻는다")));
	}
	{
		const FScopedGemsEnabled GemsOn(true);
		Rig.Screen->SelectOwnedTab(true);
		TestTrue(TEXT("젬 켜짐: 빈 칸 글 = 상점 열기"), Rig.Screen->GetMessageShown().Contains(TEXT("상점 열기")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWardrobeIconsTest, "Snarehaven.UI.Wardrobe.EveryCosmeticHasIcon",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWardrobeIconsTest::RunTest(const FString& Parameters)
{
	// 옷장 칸은 아이콘이 주인공 — 진짜 표의 꾸미기 행(옷·모자·펫 장식)에 아이콘 빈 행이 0 이고 실제로 불러와진다.
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, SnareWardrobeScreenTest::ItemTablePath);
	if (!TestNotNull(TEXT("아이템 표"), Table))
	{
		return false;
	}
	int32 Checked = 0;
	TArray<FString> Missing;
	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FItemRow* const Row = reinterpret_cast<const FItemRow*>(Pair.Value);
		if (Row == nullptr || (Row->ItemType != EItemType::Cosmetic && Row->ItemType != EItemType::PetCosmetic))
		{
			continue;
		}
		++Checked;
		if (Row->IconRef.IsNull() || Row->IconRef.LoadSynchronous() == nullptr)
		{
			Missing.Add(Pair.Key.ToString());
		}
	}
	TestEqual(*FString::Printf(TEXT("아이콘 없는 꾸미기 행 [%s]"), *FString::Join(Missing, TEXT(","))), Missing.Num(), 0);
	AddInfo(FString::Printf(TEXT("[Wardrobe] 꾸미기 행 %d · 아이콘 빈 행 %d"), Checked, Missing.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWardrobeScreenRetryTest, "Snarehaven.UI.Wardrobe.Retry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWardrobeScreenRetryTest::RunTest(const FString& Parameters)
{
	// 갈아입기가 서버에 닿지 못하면 「다시 시도」 — 누르면 같은 칸·같은 옷으로 `SetOutfit` 을 다시.
	using namespace SnareWardrobeScreenTest;

	FFakeServer Server;
	Server.Install();
	FRig Rig;
	if (!TestNotNull(TEXT("표 사본을 건 플레이어"), Rig.Player) || !TestNotNull(TEXT("옷장 화면"), Rig.Screen))
	{
		return false;
	}
	const UWidget* const RetryBox = Rig.Screen->WidgetTree->FindWidget(TEXT("WardrobeRetryBox"));
	if (!TestNotNull(TEXT("다시 시도 단추가 있다"), Rig.Screen->WidgetTree->FindWidget(TEXT("WardrobeRetryButton"))) || RetryBox == nullptr)
	{
		return false;
	}
	Rig.Click(TEXT("WardrobeAllTab"));
	Rig.ClickTile(HatCommonID);
	TestEqual(TEXT("SetOutfit 한 번"), Server.Count(TEXT("SetOutfit")), 1);
	TestFalse(TEXT("처음엔 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	if (Server.Waiting.Num() > 0)
	{
		FDone Done = MoveTemp(Server.Waiting[0]);
		Server.Waiting.RemoveAt(0);
		Done(false, nullptr, TEXT("net"));
	}
	TestTrue(TEXT("네트워크 실패 → 다시 시도 보임"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	TestFalse(TEXT("이유를 보인다"), Rig.Screen->GetMessage().IsEmpty());
	Rig.Click(TEXT("WardrobeRetryButton"));
	TestEqual(TEXT("다시 시도 → SetOutfit 다시"), Server.Count(TEXT("SetOutfit")), 2);
	TestEqual(TEXT("같은 slot"), Server.LastArg(TEXT("slot")), FString(TEXT("head")));
	TestEqual(TEXT("같은 itemId"), Server.LastArg(TEXT("itemId")), HatCommonID.ToString());
	TestFalse(TEXT("누른 뒤 숨김"), RetryBox->GetVisibility() == ESlateVisibility::Visible);
	Server.AnswerAccepted();
	TestEqual(TEXT("다시 보낸 뒤 입음"), Rig.Equipment->GetCosmeticItem(EEquipSlot::Head), HatCommonRow);
	return true;
}

// 격자 잘림 보정(INBOX 감독 10-17 01:30) — 판이 자기 테두리에서 자르고, 격자 칸은 판 안 남은 높이를 채운다(고정 높이 아님).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWardrobeGridClipTest, "Snarehaven.UI.Wardrobe.GridStaysInsidePanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWardrobeGridClipTest::RunTest(const FString& Parameters)
{
	using namespace SnareWardrobeScreenTest;

	FFakeServer Server;
	Server.Install();
	FRig Rig;
	if (!TestNotNull(TEXT("옷장 화면"), Rig.Screen))
	{
		return false;
	}
	UWidgetTree* const Tree = Rig.Screen->WidgetTree;
	UWidget* const Panel = Tree->FindWidget(TEXT("WardrobePanel"));
	UWidget* const GridBox = Tree->FindWidget(TEXT("WardrobeGridBox"));
	if (!TestNotNull(TEXT("옷장 판"), Panel) || !TestNotNull(TEXT("격자 상자"), GridBox))
	{
		return false;
	}
	TestEqual(TEXT("판이 테두리에서 자른다"), Panel->GetClipping(), EWidgetClipping::ClipToBounds);
	const UVerticalBoxSlot* const GridSlot = Cast<UVerticalBoxSlot>(GridBox->Slot);
	if (TestNotNull(TEXT("격자 세로 칸"), GridSlot))
	{
		TestEqual(TEXT("격자가 남은 높이를 채운다"), GridSlot->GetSize().SizeRule, ESlateSizeRule::Fill);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
