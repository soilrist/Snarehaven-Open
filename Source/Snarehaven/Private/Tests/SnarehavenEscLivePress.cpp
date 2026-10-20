// INBOX 결함(2026-10-04 15:40) 감독 확인용 — 돌고 있는 PIE 에 **진짜 Slate 입력 길**로 ESC 를 한 번 넣는다.
// 자동 시험 틀은 시작할 때 PIE 를 꺼 버려서 콘솔 명령으로 둔다. 에디터 전용.
// M37-05: `Snare.Test.EscEveryPanel` 은 같은 길로 판마다 한 번씩 — 판을 열고 ESC 한 번 → 그 판 닫힘·일시정지 메뉴 안 뜸,
// 판이 없을 때 ESC 한 번 → 일시정지 메뉴 뜸을 `[EscLive]` 한 줄씩 남긴다. 구동은 `tools/unreal/run_pie_esc_check.py`.

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Equipment/EquipmentComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Inventory/BackpackComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Snarehaven.h"
#include "SnarehavenCharacter.h"
#include "UI/AchievementScreenWidget.h"
#include "UI/AttendanceScreenWidget.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/ChestScreenWidget.h"
#include "UI/CodexScreenWidget.h"
#include "UI/CraftingScreenWidget.h"
#include "UI/CrateScreenWidget.h"
#include "UI/GachaScreenWidget.h"
#include "UI/PauseMenuWidget.h"
#include "UI/QuestScreenWidget.h"
#include "UI/ShopScreenWidget.h"
#include "UI/SnareGuideBookWidget.h"
#include "UI/SnareScreenCloser.h"
#include "UI/SnareUiStyle.h"
#include "UI/WardrobeShopWidget.h"
#include "UI/WardrobeWidget.h"
#include "UI/WorldMapScreenWidget.h"

namespace
{
	/** 게임 화면에 포커스를 주고 ESC 를 한 번 누른다 — 사람 손과 같은 Slate 입력 길(등록된 전처리기가 받는다). */
	void PressEscOnce()
	{
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
		const FKeyEvent Event(EKeys::Escape, FModifierKeysState(), /*UserIndex=*/0, /*bIsRepeat=*/false, 0, 0);
		FSlateApplication::Get().ProcessKeyDownEvent(Event);
		FSlateApplication::Get().ProcessKeyUpEvent(Event);
	}

	UWorld* FindPieWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World() != nullptr)
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	bool IsPauseMenuShown(UWorld* World)
	{
		TArray<UUserWidget*> Menus;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Menus, UPauseMenuWidget::StaticClass(), /*TopLevelOnly=*/false);
		for (const UUserWidget* const Menu : Menus)
		{
			if (Menu->IsInViewport() && Menu->GetVisibility() != ESlateVisibility::Collapsed)
			{
				return true;
			}
		}
		return false;
	}

	template <typename TWidget>
	TWidget* NewPanel(APlayerController* Controller)
	{
		TWidget* const Panel = CreateWidget<TWidget>(Controller, TWidget::StaticClass());
		if (Panel != nullptr)
		{
			Panel->AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);
		}
		return Panel;
	}

	/** 판을 열고 ESC 한 번 — 열림·닫힘은 판 자신의 상태 함수로 본다(이름·로그 문구로 판정하지 않는다). */
	bool CheckPanel(UWorld* World, const TCHAR* Name, const TFunctionRef<bool()>& Open, const TFunctionRef<bool()>& IsOpen)
	{
		const bool bOpened = Open() && IsOpen() && FSnareScreenCloser::HasOpenScreen(World);
		PressEscOnce();
		const bool bClosed = !IsOpen();
		const bool bNoPause = !IsPauseMenuShown(World);
		const bool bPass = bOpened && bClosed && bNoPause;
		UE_LOG(LogSnarehaven, Log, TEXT("[EscLive] %s: 열림 %s → ESC 한 번 → 판 닫힘 %s · 일시정지 메뉴 안 뜸 %s → %s"), Name,
			bOpened ? TEXT("예") : TEXT("아니오"), bClosed ? TEXT("예") : TEXT("아니오"), bNoPause ? TEXT("예") : TEXT("아니오"),
			bPass ? TEXT("통과") : TEXT("실패"));
		// 실패한 판이 다음 판 판정을 흐리지 않게 남은 것을 치운다(사람 키가 아니라 정리).
		while (FSnareScreenCloser::CloseOpenScreens(World))
		{
		}
		return bPass;
	}

	bool IsShownOnScreen(const UUserWidget* Widget)
	{
		return IsValid(Widget) && Widget->IsInViewport() && Widget->GetVisibility() != ESlateVisibility::Collapsed;
	}

	void RunEscEveryPanel()
	{
		UWorld* const World = FindPieWorld();
		ASnarehavenCharacter* const Character =
			World != nullptr ? Cast<ASnarehavenCharacter>(UGameplayStatics::GetPlayerPawn(World, 0)) : nullptr;
		APlayerController* const Controller = Character != nullptr ? Cast<APlayerController>(Character->GetController()) : nullptr;
		if (Controller == nullptr)
		{
			UE_LOG(LogSnarehaven, Error, TEXT("[EscLive] PIE 월드·내 캐릭터가 없다 — 실행 못 함"));
			return;
		}
		UGameInstance* const GameInstance = World->GetGameInstance();
		USnareBackendSubsystem* const Backend = GameInstance != nullptr ? GameInstance->GetSubsystem<USnareBackendSubsystem>() : nullptr;
		UBackpackComponent* const BackpackComp = Character->FindComponentByClass<UBackpackComponent>();
		UEquipmentComponent* const Equipment = Character->FindComponentByClass<UEquipmentComponent>();
		// 상자 판은 이름만 보여 준다 — 열기(서버 요청)는 안 누른다. 검사 전용 값.
		const FName TestCrateID(TEXT("item.crate_autumn26"));

		const bool bStartOpen = FSnareScreenCloser::HasOpenScreen(World);
		while (FSnareScreenCloser::CloseOpenScreens(World))
		{
		}
		UE_LOG(LogSnarehaven, Log, TEXT("[EscLive] 시작 — 맵 %s · 시작 때 열린 판 %s(치움)"), *World->GetMapName(),
			bStartOpen ? TEXT("있음") : TEXT("없음"));

		int32 Passed = 0;
		int32 Failed = 0;
		const auto Count = [&Passed, &Failed](bool bPass) { bPass ? ++Passed : ++Failed; };

		UBackpackScreenWidget* Backpack = Character->GetBackpackScreen();
		if (Backpack == nullptr)
		{
			Backpack = UBackpackScreenWidget::AttachTo(Character);
		}
		Count(CheckPanel(World, TEXT("배낭"),
			[Backpack]() { if (Backpack == nullptr) { return false; } Backpack->SetPanelOpen(true); return true; },
			[Backpack]() { return Backpack != nullptr && Backpack->IsPanelOpen(); }));

		UShopScreenWidget* const Shop = NewPanel<UShopScreenWidget>(Controller);
		Count(CheckPanel(World, TEXT("상점(+배낭)"),
			[Shop, Backend, Backpack]()
			{
				if (Shop == nullptr) { return false; }
				Shop->BindToBackend(Backend);
				Shop->BindBackpackScreen(Backpack);
				Shop->SetOpen(true);
				return true;
			},
			[Shop, Backpack]() { return (Shop != nullptr && Shop->IsOpen()) || (Backpack != nullptr && Backpack->IsPanelOpen()); }));

		UChestScreenWidget* const Chest = NewPanel<UChestScreenWidget>(Controller);
		Count(CheckPanel(World, TEXT("보관함"),
			[Chest]() { if (Chest == nullptr) { return false; } Chest->SetOpen(true); return true; },
			[Chest]() { return Chest != nullptr && Chest->IsOpen(); }));

		UQuestScreenWidget* const Quest = NewPanel<UQuestScreenWidget>(Controller);
		Count(CheckPanel(World, TEXT("퀘스트"),
			[Quest]() { if (Quest == nullptr) { return false; } Quest->SetOpen(true); return true; },
			[Quest]() { return Quest != nullptr && Quest->IsOpen(); }));

		UAttendanceScreenWidget* const Attendance = NewPanel<UAttendanceScreenWidget>(Controller);
		Count(CheckPanel(World, TEXT("출석"),
			[Attendance]() { if (Attendance == nullptr) { return false; } Attendance->SetOpen(true); return true; },
			[Attendance]() { return Attendance != nullptr && Attendance->IsOpen(); }));

		UCraftingScreenWidget* const Crafting = NewPanel<UCraftingScreenWidget>(Controller);
		Count(CheckPanel(World, TEXT("제작"),
			[Crafting]() { if (Crafting == nullptr) { return false; } Crafting->OpenBackpack(); return true; },
			[Crafting]() { return Crafting != nullptr && Crafting->IsOpen(); }));

		UWorldMapScreenWidget* const Map = UWorldMapScreenWidget::AttachTo(Character);
		Count(CheckPanel(World, TEXT("지도"),
			[Map]() { if (Map == nullptr) { return false; } Map->SetMapOpen(true); return true; },
			[Map]() { return Map != nullptr && Map->IsMapOpen(); }));

		UCodexScreenWidget* Codex = nullptr;
		Count(CheckPanel(World, TEXT("도감"),
			[&Codex, Controller, World, Backend]() { Codex = UCodexScreenWidget::OpenOnViewport(Controller, World, Backend); return Codex != nullptr; },
			[&Codex]() { return IsShownOnScreen(Codex); }));

		// 업적은 도감 위에 뜬다 — ESC 한 번 = 업적만 닫히고 도감이 돌아온다(그 뒤 정리가 도감을 닫는다).
		UCodexScreenWidget* CodexUnder = nullptr;
		UAchievementScreenWidget* Achievement = nullptr;
		Count(CheckPanel(World, TEXT("업적(도감 위)"),
			[&CodexUnder, &Achievement, Controller, World, Backend]()
			{
				CodexUnder = UCodexScreenWidget::OpenOnViewport(Controller, World, Backend);
				Achievement = CodexUnder != nullptr ? CodexUnder->OpenAchievementScreen() : nullptr;
				return Achievement != nullptr;
			},
			[&Achievement, &CodexUnder]()
			{
				return IsShownOnScreen(Achievement) || !IsShownOnScreen(CodexUnder);
			}));

		UGachaScreenWidget* const Gacha = NewPanel<UGachaScreenWidget>(Controller);
		Count(CheckPanel(World, TEXT("뽑기"),
			[Gacha]() { return Gacha != nullptr; },
			[Gacha]() { return IsShownOnScreen(Gacha); }));

		USnareGuideBookWidget* GuideBook = nullptr;
		Count(CheckPanel(World, TEXT("안내서"),
			[&GuideBook, Controller, World]() { GuideBook = USnareGuideBookWidget::OpenOnViewport(Controller, World); return GuideBook != nullptr; },
			[&GuideBook]() { return IsShownOnScreen(GuideBook); }));

		UWardrobeShopWidget* WardrobeShop = nullptr;
		Count(CheckPanel(World, TEXT("옷 상점"),
			[&WardrobeShop, Controller, World, Backend]()
			{
				WardrobeShop = UWardrobeShopWidget::OpenOnViewport(Controller, World, Backend);
				return WardrobeShop != nullptr;
			},
			[&WardrobeShop]() { return IsShownOnScreen(WardrobeShop); }));

		// 옷장은 배낭 위에서 열린다 — ESC 한 번 = 옷장만, 밑의 배낭은 남는다.
		UWardrobeWidget* Wardrobe = nullptr;
		bool bBackpackKept = false;
		Count(CheckPanel(World, TEXT("옷장(배낭 위)"),
			[&Wardrobe, Backpack, Controller, World, Backend, BackpackComp, Equipment]()
			{
				if (Backpack != nullptr) { Backpack->SetPanelOpen(true); }
				Wardrobe = UWardrobeWidget::OpenOnViewport(Controller, World, Backend, BackpackComp, Equipment);
				return Wardrobe != nullptr;
			},
			[&Wardrobe, &bBackpackKept, Backpack]()
			{
				bBackpackKept = Backpack != nullptr && Backpack->IsPanelOpen();
				return IsShownOnScreen(Wardrobe);
			}));
		UE_LOG(LogSnarehaven, Log, TEXT("[EscLive] 옷장 닫은 뒤 배낭 남음 %s"), bBackpackKept ? TEXT("예") : TEXT("아니오"));
		Count(bBackpackKept);

		UCrateScreenWidget* Crate = nullptr;
		bool bBackpackKeptCrate = false;
		Count(CheckPanel(World, TEXT("상자 열기(배낭 위)"),
			[&Crate, Backpack, Controller, Backend, BackpackComp, TestCrateID]()
			{
				if (Backpack != nullptr) { Backpack->SetPanelOpen(true); }
				Crate = UCrateScreenWidget::OpenFor(Controller, Backend, BackpackComp, TestCrateID);
				return Crate != nullptr;
			},
			[&Crate, &bBackpackKeptCrate, Backpack]()
			{
				bBackpackKeptCrate = Backpack != nullptr && Backpack->IsPanelOpen();
				return IsShownOnScreen(Crate);
			}));
		UE_LOG(LogSnarehaven, Log, TEXT("[EscLive] 상자 판 닫은 뒤 배낭 남음 %s"), bBackpackKeptCrate ? TEXT("예") : TEXT("아니오"));
		Count(bBackpackKeptCrate);

		// 판이 하나도 없을 때 ESC 한 번 = 일시정지 메뉴가 뜬다, 한 번 더 = 메뉴가 닫힌다.
		const bool bEmpty = !FSnareScreenCloser::HasOpenScreen(World);
		PressEscOnce();
		const bool bPauseShown = IsPauseMenuShown(World);
		PressEscOnce();
		const bool bPauseClosed = !IsPauseMenuShown(World);
		const bool bPausePass = bEmpty && bPauseShown && bPauseClosed;
		UE_LOG(LogSnarehaven, Log, TEXT("[EscLive] 판 없음: ESC 한 번 → 일시정지 메뉴 뜸 %s · 한 번 더 → 메뉴 닫힘 %s → %s"),
			bPauseShown ? TEXT("예") : TEXT("아니오"), bPauseClosed ? TEXT("예") : TEXT("아니오"), bPausePass ? TEXT("통과") : TEXT("실패"));
		Count(bPausePass);

		UE_LOG(LogSnarehaven, Log, TEXT("[EscLive] 끝 — 줄 %d개 · 통과 %d · 실패 %d"), Passed + Failed, Passed, Failed);
	}
}

static FAutoConsoleCommand GSnareTestPressEsc(
	TEXT("Snare.Test.PressEsc"),
	TEXT("게임 화면에 포커스를 주고 ESC 를 한 번 누른다 (옛 코드에선 에디터가 이걸 「플레이 중지」로 받았다)."),
	FConsoleCommandDelegate::CreateStatic(&PressEscOnce));

static FAutoConsoleCommand GSnareTestEscEveryPanel(
	TEXT("Snare.Test.EscEveryPanel"),
	TEXT("PIE 에서 판마다 열고 ESC 한 번(진짜 Slate 입력 길) — 판 닫힘·일시정지 메뉴 안 뜸, 판 없을 때 메뉴 뜸을 [EscLive] 줄로 (M37-05)."),
	FConsoleCommandDelegate::CreateStatic(&RunEscEveryPanel));

#endif
