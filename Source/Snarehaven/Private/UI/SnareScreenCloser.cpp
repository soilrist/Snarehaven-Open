#include "UI/SnareScreenCloser.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Snarehaven.h"
#include "SnarehavenCharacter.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "UI/AchievementScreenWidget.h"
#include "UI/AttendanceScreenWidget.h"
#include "UI/GiftScreenWidget.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/ChestScreenWidget.h"
#include "UI/CodexScreenWidget.h"
#include "UI/CraftingScreenWidget.h"
#include "UI/CrateScreenWidget.h"
#include "UI/GachaScreenWidget.h"
#include "UI/PauseMenuWidget.h"
#include "UI/QuestScreenWidget.h"
#include "UI/SnareGuideBookWidget.h"
#include "UI/PlayerSafetyWidget.h"
#include "UI/ShopScreenWidget.h"
#include "UI/WardrobeShopWidget.h"
#include "UI/WardrobeWidget.h"
#include "UI/WorldMapScreenWidget.h"
#include "World/SnareSound.h"

namespace
{
	// 열림 여부가 bOpen 이 아니라 "화면에 붙어 있고 보이는가" 인 판(도감·업적·뽑기·일시정지).
	// 닫기가 RemoveFromParent 라 떼어 낸 뒤엔 뷰포트에도 부모에도 없다.
	bool IsShown(const UUserWidget* Widget)
	{
		return (Widget->IsInViewport() || Widget->GetParent() != nullptr)
			&& Widget->GetVisibility() != ESlateVisibility::Collapsed
			&& Widget->GetVisibility() != ESlateVisibility::Hidden;
	}
}

bool FSnareScreenCloser::IsCloseKey(const FKey& Key)
{
	return Key == EKeys::Escape || Key == EKeys::Android_Back;
}

bool FSnareScreenCloser::CloseOpenScreens(UWorld* InWorld)
{
	if (InWorld == nullptr)
	{
		return false;
	}

	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(InWorld, Widgets, UUserWidget::StaticClass(), /*TopLevelOnly=*/false);

	// 맨 위부터: 일시정지 메뉴는 모든 판 위에 뜬다 — 그것만 닫는다.
	for (UUserWidget* const Widget : Widgets)
	{
		UPauseMenuWidget* const Pause = Cast<UPauseMenuWidget>(Widget);
		if (Pause != nullptr && IsShown(Pause))
		{
			Pause->CloseMenu();
			return true;
		}
	}
	// 업적은 도감을 접고 그 위에 뜬다 — 닫기 버튼처럼 도감을 되살리고 끝낸다(다음 ESC 가 도감을 닫는다).
	for (UUserWidget* const Widget : Widgets)
	{
		UAchievementScreenWidget* const Achievement = Cast<UAchievementScreenWidget>(Widget);
		if (Achievement != nullptr && IsShown(Achievement))
		{
			Achievement->Close();
			return true;
		}
	}
	// 안내서는 배낭·일시정지 위에서 열린다(M28-06b) — 안내서만 닫아 열던 판으로 돌아간다.
	for (UUserWidget* const Widget : Widgets)
	{
		USnareGuideBookWidget* const GuideBook = Cast<USnareGuideBookWidget>(Widget);
		if (GuideBook != nullptr && IsShown(GuideBook))
		{
			GuideBook->Close();
			return true;
		}
	}
	// 신고·차단 판도 일시정지 메뉴를 닫고 뜬다 — 판만 닫아 게임으로 돌아간다.
	for (UUserWidget* const Widget : Widgets)
	{
		UPlayerSafetyWidget* const Safety = Cast<UPlayerSafetyWidget>(Widget);
		if (Safety != nullptr && IsShown(Safety))
		{
			Safety->Close();
			return true;
		}
	}

	// 옷 상점은 배낭 「장비」 탭 위에서 열린다(M37-01) — 맨 위 하나만: 구매 완료 창 → 구매 확인 창 → 판.
	for (UUserWidget* const Widget : Widgets)
	{
		UWardrobeShopWidget* const WardrobeShop = Cast<UWardrobeShopWidget>(Widget);
		if (WardrobeShop != nullptr && IsShown(WardrobeShop))
		{
			if (WardrobeShop->IsCompleteWindowOpen())
			{
				WardrobeShop->CloseCompleteWindow();
			}
			else if (WardrobeShop->IsBuyWindowOpen())
			{
				// 보내는 중이면 CancelBuy 가 아무것도 안 한다 — 창은 응답이 닫는다. 키만 먹는다.
				WardrobeShop->CancelBuy();
			}
			else
			{
				WardrobeShop->Close();
			}
			return true;
		}
	}

	// 옷장은 배낭 「장비」 탭 위에서 열린다(M37-02) — 옷장만 닫아 배낭으로 돌아간다. 옷 상점이 위에 있으면 위 차례가 먼저 닫는다.
	for (UUserWidget* const Widget : Widgets)
	{
		UWardrobeWidget* const Wardrobe = Cast<UWardrobeWidget>(Widget);
		if (Wardrobe != nullptr && IsShown(Wardrobe))
		{
			Wardrobe->Close();
			return true;
		}
	}

	// 상자 열기 판은 배낭에서 상자를 눌러 그 위에 뜬다(M37-03) — 닫기 버튼과 같은 길(떼어 냄)로 상자 판만 닫아 배낭으로 돌아간다.
	// 응답 대기 중에 닫아도 응답은 떼어 낸 판의 글자만 바꾸고 다시 붙이지 않는다. 지급은 서버 값 그대로(CLAUDE.md 8).
	for (UUserWidget* const Widget : Widgets)
	{
		UCrateScreenWidget* const Crate = Cast<UCrateScreenWidget>(Widget);
		if (Crate != nullptr && IsShown(Crate))
		{
			Crate->RemoveFromParent();
			return true;
		}
	}

	bool bClosed = false;
	// 상점을 먼저 닫는다 — 같이 열린 배낭도 상점이 닫는다(SetOpen(false)). 배낭 차례엔 이미 닫혀 있다.
	for (UUserWidget* const Widget : Widgets)
	{
		UShopScreenWidget* const Shop = Cast<UShopScreenWidget>(Widget);
		if (Shop != nullptr && Shop->IsOpen())
		{
			Shop->SetOpen(false);
			bClosed = true;
		}
	}
	for (UUserWidget* const Widget : Widgets)
	{
		if (UBackpackScreenWidget* const Backpack = Cast<UBackpackScreenWidget>(Widget))
		{
			if (Backpack->IsPanelOpen())
			{
				Backpack->SetPanelOpen(false);
				bClosed = true;
			}
		}
		else if (UChestScreenWidget* const Chest = Cast<UChestScreenWidget>(Widget))
		{
			if (Chest->IsOpen())
			{
				Chest->SetOpen(false);
				bClosed = true;
			}
		}
		else if (UQuestScreenWidget* const Quest = Cast<UQuestScreenWidget>(Widget))
		{
			if (Quest->IsOpen())
			{
				Quest->SetOpen(false);
				bClosed = true;
			}
		}
		else if (UAttendanceScreenWidget* const Attendance = Cast<UAttendanceScreenWidget>(Widget))
		{
			if (Attendance->IsOpen())
			{
				Attendance->SetOpen(false);
				bClosed = true;
			}
		}
		else if (UGiftScreenWidget* const Gift = Cast<UGiftScreenWidget>(Widget))
		{
			if (Gift->IsOpen())
			{
				Gift->SetOpen(false);
				bClosed = true;
			}
		}
		else if (UCraftingScreenWidget* const Crafting = Cast<UCraftingScreenWidget>(Widget))
		{
			if (Crafting->IsOpen())
			{
				Crafting->Close();
				bClosed = true;
			}
		}
		else if (UWorldMapScreenWidget* const Map = Cast<UWorldMapScreenWidget>(Widget))
		{
			if (Map->IsMapOpen())
			{
				Map->SetMapOpen(false);
				bClosed = true;
			}
		}
		else if (Widget->IsA<UCodexScreenWidget>() || Widget->IsA<UGachaScreenWidget>())
		{
			if (IsShown(Widget))
			{
				Widget->RemoveFromParent();
				bClosed = true;
			}
		}
	}
	return bClosed;
}

int32 FSnareScreenCloser::CountOpenScreens(UWorld* InWorld)
{
	if (InWorld == nullptr)
	{
		return 0;
	}
	int32 Count = 0;
	TArray<UUserWidget*> Widgets;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(InWorld, Widgets, UUserWidget::StaticClass(), /*TopLevelOnly=*/false);
	// `CloseOpenScreens` 와 같은 목록이다 — 거기 판을 더하면 여기도 더한다.
	for (const UUserWidget* const Widget : Widgets)
	{
		const UShopScreenWidget* const Shop = Cast<UShopScreenWidget>(Widget);
		const UBackpackScreenWidget* const Backpack = Cast<UBackpackScreenWidget>(Widget);
		const UChestScreenWidget* const Chest = Cast<UChestScreenWidget>(Widget);
		const UQuestScreenWidget* const Quest = Cast<UQuestScreenWidget>(Widget);
		const UAttendanceScreenWidget* const Attendance = Cast<UAttendanceScreenWidget>(Widget);
		const UGiftScreenWidget* const Gift = Cast<UGiftScreenWidget>(Widget);
		const UCraftingScreenWidget* const Crafting = Cast<UCraftingScreenWidget>(Widget);
		const UWorldMapScreenWidget* const Map = Cast<UWorldMapScreenWidget>(Widget);
		if ((Shop != nullptr && Shop->IsOpen()) || (Backpack != nullptr && Backpack->IsPanelOpen())
			|| (Chest != nullptr && Chest->IsOpen()) || (Quest != nullptr && Quest->IsOpen())
			|| (Attendance != nullptr && Attendance->IsOpen()) || (Gift != nullptr && Gift->IsOpen()) || (Crafting != nullptr && Crafting->IsOpen())
			|| (Map != nullptr && Map->IsMapOpen()))
		{
			++Count;
		}
		if ((Widget->IsA<UPauseMenuWidget>() || Widget->IsA<UAchievementScreenWidget>()
			|| Widget->IsA<UCodexScreenWidget>() || Widget->IsA<UGachaScreenWidget>()
			|| Widget->IsA<USnareGuideBookWidget>() || Widget->IsA<UPlayerSafetyWidget>() || Widget->IsA<UWardrobeShopWidget>()
			|| Widget->IsA<UWardrobeWidget>() || Widget->IsA<UCrateScreenWidget>()) && IsShown(Widget))
		{
			++Count;
		}
	}
	return Count;
}

FName FSnareScreenCloser::PanelSoundCue(int32 OpenBefore, int32 OpenAfter)
{
	if (OpenAfter > OpenBefore)
	{
		return FName(TEXT("Ui_Open"));
	}
	return OpenAfter < OpenBefore ? FName(TEXT("Ui_Close")) : NAME_None;
}

FName FSnareScreenCloser::UpdatePanelSound(UWorld* InWorld)
{
	if (PanelSoundWorld.Get() != InWorld)
	{
		PanelSoundWorld = InWorld;
		LastOpenCount = 0;
	}
	const int32 OpenCount = CountOpenScreens(InWorld);
	const FName Cue = PanelSoundCue(LastOpenCount, OpenCount);
	LastOpenCount = OpenCount;
	if (!Cue.IsNone())
	{
		USnareSoundSubsystem::Play2D(InWorld, Cue, PanelSoundVolume());
		UE_LOG(LogSnarehaven, Log, TEXT("[PanelSound] %s — 열린 창 %d"), *Cue.ToString(), OpenCount);
	}
	return Cue;
}

void FSnareScreenCloser::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
	// 0.1초마다 센다 — 판 목록 훑기를 매 프레임 하지 않는다(소리 늦음 0.1초 이하).
	PanelSoundWait -= DeltaTime;
	if (PanelSoundWait > 0.f)
	{
		return;
	}
	PanelSoundWait = 0.1f;
	UWorld* const GameWorld = FindGameWorld();
	if (GameWorld == nullptr)
	{
		PanelSoundWorld.Reset();
		LastOpenCount = 0;
		return;
	}
	UpdatePanelSound(GameWorld);
}

UWorld* FSnareScreenCloser::FindGameWorld() const
{
	if (World.IsValid())
	{
		return World.Get();
	}
	if (GEngine == nullptr)
	{
		return nullptr;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if ((Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game) && Context.World() != nullptr)
		{
			return Context.World();
		}
	}
	return nullptr;
}

bool FSnareScreenCloser::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent)
{
	if (!IsCloseKey(InKeyEvent.GetKey()))
	{
		return false;
	}
	UWorld* const GameWorld = FindGameWorld();
	if (GameWorld == nullptr)
	{
		// 게임이 안 돌 때(에디터 편집 중)는 에디터 몫이다.
		return false;
	}
	// 여기부터는 무슨 일이 있어도 먹는다 — 흘려보내면 에디터 PIE 의 ESC=플레이 중지가 게임을 끈다.
	if (InKeyEvent.IsRepeat())
	{
		return true;
	}
	if (CloseOpenScreens(GameWorld))
	{
		UE_LOG(LogSnarehaven, Log, TEXT("[Esc] %s — 열린 창을 닫았다"), *InKeyEvent.GetKey().ToString());
		return true;
	}
	ASnarehavenCharacter* const Character = Cast<ASnarehavenCharacter>(UGameplayStatics::GetPlayerPawn(GameWorld, 0));
	if (Character != nullptr)
	{
		Character->TogglePauseMenu();
	}
	UE_LOG(LogSnarehaven, Log, TEXT("[Esc] %s — 열린 창 없음, %s (게임은 계속)"), *InKeyEvent.GetKey().ToString(),
		Character != nullptr ? TEXT("일시정지 메뉴") : TEXT("먹기만"));
	return true;
}
