// INBOX 지시 M94 ④ 2층 검사 — 1.0 은 젬 화면을 전부 숨긴다 (서버에 젬을 주는 길이 0개라 막다른 칸이 된다).
// 이 검사가 잡는 것: ① 끄면 잔액 줄·뽑기 화면·옷장 화면에 젬 글자·젬 버튼이 하나도 안 보인다
// ② 코인 뽑기 줄은 그대로 보인다 ③ 켜면 젬 칸이 다시 나온다(역검증 — 끄는 코드가 아무것도 안 해도 ①이 통과하는 일을 막는다).
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.Store; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UI/GachaScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/SnareStoreSettings.h"
#include "UI/WardrobeWidget.h"

namespace
{
	// 검사 전용 값 — 설계 숫자가 아니다.
	constexpr int32 TestCoinBalance = 120;
	constexpr int32 TestGemBalance = 34;

	/** 그 위젯 자신이나 조상이 접혀 있으면 화면에 안 보인다. */
	bool IsShownOnScreen(const UWidget* Widget)
	{
		for (const UWidget* Walk = Widget; Walk != nullptr; Walk = Walk->GetParent())
		{
			if (!Walk->IsVisible())
			{
				return false;
			}
		}
		return Widget != nullptr;
	}

	/** 화면에 보이는 글자 중 「젬」 이 들어간 것의 수. 접힌 칸 안의 글자는 세지 않는다. */
	int32 CountVisibleGemTexts(const UUserWidget& Screen)
	{
		int32 Count = 0;
		Screen.WidgetTree->ForEachWidget([&Count](UWidget* Child)
		{
			const UTextBlock* const Text = Cast<UTextBlock>(Child);
			if (Text != nullptr && IsShownOnScreen(Text) && Text->GetText().ToString().Contains(TEXT("젬")))
			{
				++Count;
			}
		});
		return Count;
	}

	/** 한 번에 한 설정으로 세 화면을 만들어 젬 흔적을 센다. */
	struct FGemTrace
	{
		int32 BalanceGemTexts = 0;
		int32 GachaGemTexts = 0;
		bool bGachaGemButtonsShown = false;
		bool bGachaCoinButtonShown = false;
		int32 WardrobeGemTexts = 0;
		bool bWardrobeShopEntryShown = false;
	};

	FGemTrace Trace(bool bGemsEnabled)
	{
		const FScopedGemsEnabled Scope(bGemsEnabled);
		FGemTrace Result;

		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
		Context.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());

		UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
		USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TEXT("PFTESTOWNER");
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		Snapshot.CoinBalance = TestCoinBalance;
		Snapshot.GemBalance = TestGemBalance;
		Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/false);

		UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(World, UHuntHudWidget::StaticClass());
		const TSharedRef<SWidget> HudSlate = Hud->TakeWidget();
		Hud->BindToBackend(Backend);
		// 잔액 알약은 접혀 있다가 서버 응답 뒤에 펴진다 — 접힘과 상관없이 글에 젬이 있는지를 본다.
		const UTextBlock* const BalanceText = Cast<UTextBlock>(Hud->WidgetTree->FindWidget(TEXT("BalanceText")));
		Result.BalanceGemTexts = BalanceText != nullptr && BalanceText->GetText().ToString().Contains(TEXT("젬")) ? 1 : 0;

		UGachaScreenWidget* const Gacha = CreateWidget<UGachaScreenWidget>(World, UGachaScreenWidget::StaticClass());
		const TSharedRef<SWidget> GachaSlate = Gacha->TakeWidget();
		Result.GachaGemTexts = CountVisibleGemTexts(*Gacha);
		Result.bGachaGemButtonsShown = IsShownOnScreen(Gacha->WidgetTree->FindWidget(TEXT("SinglePullButton")))
			|| IsShownOnScreen(Gacha->WidgetTree->FindWidget(TEXT("TenPullButton")));
		Result.bGachaCoinButtonShown = IsShownOnScreen(Gacha->WidgetTree->FindWidget(TEXT("CoinPullButton")));

		UWardrobeWidget* const Wardrobe = CreateWidget<UWardrobeWidget>(World, UWardrobeWidget::StaticClass());
		const TSharedRef<SWidget> WardrobeSlate = Wardrobe->TakeWidget();
		Result.WardrobeGemTexts = CountVisibleGemTexts(*Wardrobe);
		Result.bWardrobeShopEntryShown = IsShownOnScreen(Wardrobe->WidgetTree->FindWidget(TEXT("WardrobeOpenShop")));

		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStoreGemHideTest, "Snarehaven.Store.GemsHidden.ScreensHaveNoGem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStoreGemHideTest::RunTest(const FString& Parameters)
{
	// 역검증 먼저: 켜면 젬 칸이 있어야 한다. 없으면 아래 「끄면 0」 이 아무것도 증명하지 못한다.
	const FGemTrace On = Trace(true);
	TestTrue(TEXT("켜면 잔액 줄에 젬이 있다"), On.BalanceGemTexts > 0);
	TestTrue(TEXT("켜면 뽑기 화면에 젬 글자가 있다"), On.GachaGemTexts > 0);
	TestTrue(TEXT("켜면 젬 뽑기 버튼이 보인다"), On.bGachaGemButtonsShown);
	TestTrue(TEXT("켜면 옷장 상점 입구가 보인다"), On.bWardrobeShopEntryShown);

	const FGemTrace Off = Trace(false);
	TestEqual(TEXT("끄면 잔액 줄 젬 글자 0"), Off.BalanceGemTexts, 0);
	TestEqual(TEXT("끄면 뽑기 화면 젬 글자 0"), Off.GachaGemTexts, 0);
	TestFalse(TEXT("끄면 젬 뽑기 버튼(1회·10연차) 접힘"), Off.bGachaGemButtonsShown);
	TestTrue(TEXT("끄면 코인 뽑기 줄은 그대로"), Off.bGachaCoinButtonShown);
	TestEqual(TEXT("끄면 옷장 화면 젬 글자 0"), Off.WardrobeGemTexts, 0);
	TestFalse(TEXT("끄면 옷장 상점 입구 접힘"), Off.bWardrobeShopEntryShown);
	return true;
}

#endif
