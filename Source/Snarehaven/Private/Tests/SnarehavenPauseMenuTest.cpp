// ROADMAP M1-10b 2층 검사 — ESC 일시정지 메뉴.
// 이 검사가 잡는 것: ① `IA_Pause` 가 없거나 Escape·패드 Start 가 안 걸려 ESC 를 눌러도 아무 일이 없는 것
// ② 자리표시 트리에 버튼 다섯이 안 들어가거나 **순서가 뒤바뀌는** 것(감독이 순서를 정했다)
// ③ 고를 항목이 없는 옵션 버튼이 눌리게 열려 있는 것
// ④ 로비 화면에서도 메뉴가 떠 로비 버튼 위에 겹치는 것.
// 실제로 ESC 를 눌러 메뉴가 뜨는 화면은 3층(`-SnarePauseMenu` 캡처)이 본다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/PanelWidget.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "Online/SnareOnlineSubsystem.h"
#include "Player/SnareHaptics.h"
#include "Ranch/SnareLocalNotify.h"
#include "Misc/ConfigCacheIni.h"
#include "SnarehavenGameMode.h"
#include "UI/PauseMenuWidget.h"
#include "UI/SnareCredits.h"
#include "UI/SnareUiStyle.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPauseActionMappingTest, "Snarehaven.Input.DefaultContext.MapsPauseKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPauseActionMappingTest::RunTest(const FString& Parameters)
{
	const UInputAction* PauseAction = LoadObject<UInputAction>(nullptr, TEXT("/Game/Game/Input/IA_Pause.IA_Pause"));
	const UInputMappingContext* Context =
		LoadObject<UInputMappingContext>(nullptr, TEXT("/Game/Game/Input/IMC_Default.IMC_Default"));
	if (!TestNotNull(TEXT("IA_Pause 로드"), PauseAction) || !TestNotNull(TEXT("IMC_Default 로드"), Context))
	{
		return false;
	}

	// 여닫는 동작이라 눌림/안 눌림 둘뿐이다 — Axis 로 만들면 Started 가 원하는 때 오지 않는다.
	TestEqual(TEXT("IA_Pause 는 Boolean"), PauseAction->ValueType, EInputActionValueType::Boolean);

	// 감독 결정 2026-09-20: 키보드 Escape + 패드 Start(Gamepad_Special_Right) 둘.
	for (const FKey& ExpectedKey : { EKeys::Escape, EKeys::Gamepad_Special_Right })
	{
		bool bFound = false;
		Context->ForEachKeyMapping([&bFound, PauseAction, &ExpectedKey](const FEnhancedActionKeyMapping& Mapping)
		{
			if (Mapping.Action == PauseAction && Mapping.Key == ExpectedKey)
			{
				bFound = true;
			}
		});
		TestTrue(FString::Printf(TEXT("IMC_Default 에 IA_Pause <- %s 매핑이 있다"), *ExpectedKey.ToString()), bFound);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPauseMenuLobbyRuleTest, "Snarehaven.PauseMenu.NotShownInLobby",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPauseMenuLobbyRuleTest::RunTest(const FString& Parameters)
{
	// 로비엔 이미 버튼이 넷 있다(`ULobbyWidget`) — 그 위에 메뉴가 겹치면 어느 쪽을 눌러야 할지 알 수 없다.
	const FString LobbyMap = USnareOnlineSubsystem::LobbyLevelName.ToString();
	TestFalse(TEXT("로비 맵에서는 메뉴가 안 뜬다"), UPauseMenuWidget::ShouldAllowPauseMenu(LobbyMap));
	// PIE 는 맵 이름 앞에 "UEDPIE_0_" 를 붙인다 — 접두어가 붙어도 로비는 로비다.
	TestFalse(TEXT("PIE 접두어가 붙은 로비 맵도 로비다"),
		UPauseMenuWidget::ShouldAllowPauseMenu(TEXT("UEDPIE_0_") + LobbyMap));
	TestTrue(TEXT("사냥 맵에서는 메뉴가 뜬다"),
		UPauseMenuWidget::ShouldAllowPauseMenu(ASnarehavenGameMode::HuntLevelName.ToString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPauseMenuPlaceholderTreeTest, "Snarehaven.PauseMenu.PlaceholderTreeHasButtonsInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPauseMenuPlaceholderTreeTest::RunTest(const FString& Parameters)
{
	// WBP 에 디자인이 없어도 메뉴는 화면에 떠야 한다(3층 캡처가 이걸 찍는다).
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	UPauseMenuWidget* Menu = CreateWidget<UPauseMenuWidget>(World, UPauseMenuWidget::StaticClass());
	if (Menu == nullptr)
	{
		AddError(TEXT("일시정지 메뉴 위젯을 만들지 못했다"));
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	// Slate 위젯을 실제로 만들어야 RebuildWidget 이 돌고, 그때 자리표시 트리가 세워진다.
	Menu->TakeWidget();

	const TArray<FName>& ExpectedOrder = UPauseMenuWidget::GetButtonOrder();
	TestEqual(TEXT("버튼은 여섯이다(안내서 M28-06b2·플레이어 10-16)"), ExpectedOrder.Num(), 6);

	// 화면에 보이는 **차례**를 본다 — 이름만 맞고 순서가 뒤바뀌면 "이어하기" 를 누르려다 게임이 꺼진다.
	TArray<FName> ActualOrder;
	if (const UPanelWidget* Root = Cast<UPanelWidget>(Menu->WidgetTree->RootWidget))
	{
		for (int32 Index = 0; Index < Root->GetChildrenCount(); ++Index)
		{
			if (const UButton* Button = Cast<UButton>(Root->GetChildAt(Index)))
			{
				ActualOrder.Add(Button->GetFName());
			}
		}
	}
	if (TestEqual(TEXT("자리표시에 버튼이 순서 배열만큼 들어갔다"), ActualOrder.Num(), ExpectedOrder.Num()))
	{
		for (int32 Index = 0; Index < ExpectedOrder.Num(); ++Index)
		{
			TestEqual(FString::Printf(TEXT("%d번째 버튼"), Index), ActualOrder[Index].ToString(),
				ExpectedOrder[Index].ToString());
		}
	}

	// 옵션 = 소리 크기 카드. 펴면 메뉴 버튼은 숨고, 접으면 돌아온다.
	const UButton* OptionsButton = Menu->WidgetTree->FindWidget<UButton>(TEXT("OptionsButton"));
	if (TestNotNull(TEXT("자리표시에 옵션 버튼이 있다"), OptionsButton))
	{
		TestTrue(TEXT("옵션 버튼이 켜져 있다 (소리 크기 카드)"), OptionsButton->GetIsEnabled());
		Menu->SetOptionsOpen(true);
		TestTrue(TEXT("옵션 카드가 펴졌다"), Menu->IsOptionsOpen());
		TestEqual(TEXT("펴면 메뉴 버튼이 숨는다"), OptionsButton->GetVisibility(), ESlateVisibility::Collapsed);
		TestNotNull(TEXT("소리 막대가 있다"), Menu->WidgetTree->FindWidget(TEXT("OptionsSlider2")));
		// M21-03b 진동 줄 — 단추 글이 저장값을 따르고, 누르면 뒤집혀 저장된다. 원래 값으로 돌려놓는다.
		const bool bWasEnabled = SnareHaptics::IsEnabled();
		TestNotNull(TEXT("진동 단추가 있다"), Menu->WidgetTree->FindWidget(TEXT("OptionsVibrationButton")));
		TestEqual(TEXT("진동 단추 글 = 저장값"), Menu->GetVibrationLabel().ToString(),
			UPauseMenuWidget::FormatVibrationLabel(bWasEnabled).ToString());
		Menu->ToggleVibration();
		TestEqual(TEXT("누르면 저장값이 뒤집힌다"), SnareHaptics::IsEnabled(), !bWasEnabled);
		TestEqual(TEXT("누르면 글도 바뀐다"), Menu->GetVibrationLabel().ToString(),
			UPauseMenuWidget::FormatVibrationLabel(!bWasEnabled).ToString());
		AddInfo(FString::Printf(TEXT("[Vibration] 단추 '%s' → '%s'"), *UPauseMenuWidget::FormatVibrationLabel(bWasEnabled).ToString(),
			*Menu->GetVibrationLabel().ToString()));
		SnareHaptics::SetEnabled(bWasEnabled);
		// M21-04b2 알림 줄 — 누르면 저장값이 뒤집히고, 끄는 쪽이면 걸어 둔 알림을 지운다(가짜 싱크로 셈).
		struct FCountSink : SnareLocalNotify::ISink
		{
			int32 Clears = 0;
			int32 Schedules = 0;
			virtual void ClearAll() override { ++Clears; }
			virtual void Schedule(const SnareLocalNotify::FPlanned&) override { ++Schedules; }
		} Sink;
		const TCHAR* const NotifySection = TEXT("/Script/Snarehaven.SnareLocalNotify");
		TArray<FString> SavedSent;
		GConfig->GetArray(NotifySection, TEXT("SentLocal"), SavedSent, GGameUserSettingsIni);
		const bool bNotifyWas = SnareLocalNotify::IsEnabled();
		SnareLocalNotify::SetSinkForTest(&Sink);
		TestNotNull(TEXT("알림 단추가 있다"), Menu->WidgetTree->FindWidget(TEXT("OptionsNotifyButton")));
		TestEqual(TEXT("알림 단추 글 = 저장값"), Menu->GetNotifyLabel().ToString(),
			UPauseMenuWidget::FormatVibrationLabel(bNotifyWas).ToString());
		for (int32 Press = 0; Press < 2; ++Press)
		{
			const bool bBefore = SnareLocalNotify::IsEnabled();
			const int32 ClearsBefore = Sink.Clears;
			Menu->ToggleNotify();
			bool bIni = bBefore;
			GConfig->GetBool(NotifySection, TEXT("Enabled"), bIni, GGameUserSettingsIni);
			TestEqual(TEXT("누르면 알림 저장값이 뒤집힌다"), SnareLocalNotify::IsEnabled(), !bBefore);
			TestEqual(TEXT("ini 값도 뒤집힌다"), bIni, !bBefore);
			TestEqual(TEXT("알림 단추 글도 바뀐다"), Menu->GetNotifyLabel().ToString(),
				UPauseMenuWidget::FormatVibrationLabel(!bBefore).ToString());
			TestEqual(TEXT("끄면 한 번 지우고 켜면 안 지운다"), Sink.Clears - ClearsBefore, bBefore ? 1 : 0);
			AddInfo(FString::Printf(TEXT("[Notify] 단추 %s → %s · ini %s · 지우기 %d번"), bBefore ? TEXT("켬") : TEXT("끔"),
				*Menu->GetNotifyLabel().ToString(), bIni ? TEXT("True") : TEXT("False"), Sink.Clears - ClearsBefore));
		}
		TestEqual(TEXT("단추로는 알림을 걸지 않는다"), Sink.Schedules, 0);
		SnareLocalNotify::SetSinkForTest(nullptr);
		SnareLocalNotify::SetEnabled(bNotifyWas);
		GConfig->SetArray(NotifySection, TEXT("SentLocal"), SavedSent, GGameUserSettingsIni);
		GConfig->Flush(false, GGameUserSettingsIni);
		Menu->SetOptionsOpen(false);
		TestFalse(TEXT("닫으면 접힌다"), Menu->IsOptionsOpen());
		TestEqual(TEXT("닫으면 메뉴 버튼이 돌아온다"), OptionsButton->GetVisibility(), ESlateVisibility::Visible);
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPauseMenuPanelSpecTest, "Snarehaven.PauseMenu.Tree.FollowsPanelSpec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M15-24 — 시트가 없는 화면이라 `hud_ingame`·`inventory_backpack` 규격을 준용한다(README "아직 없는 시트").
// 판 색·모서리·테두리 없음·제목 탭이 판 위로 걸침·버튼이 판 안에 듦을 **실제 트리 값**으로 잰다.
bool FSnarehavenPauseMenuPanelSpecTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	UPauseMenuWidget* Screen = CreateWidget<UPauseMenuWidget>(World, UPauseMenuWidget::StaticClass());
	if (!TestNotNull(TEXT("일시정지 위젯"), Screen))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Screen->TakeWidget();

	const UBorder* const Panel = Screen->WidgetTree->FindWidget<UBorder>(TEXT("MenuPanel"));
	const UBorder* const Pill = Screen->WidgetTree->FindWidget<UBorder>(TEXT("MenuTitlePill"));
	const UTextBlock* const Title = Screen->WidgetTree->FindWidget<UTextBlock>(TEXT("TitleText"));
	if (TestNotNull(TEXT("크림 판"), Panel) && TestNotNull(TEXT("제목 탭"), Pill) && TestNotNull(TEXT("제목 글자"), Title))
	{
		// 시트 값 (검사 전용 사본): 판 #F5EBD2 · 탭 #5A3E36 · 모서리 12~16px.
		const FLinearColor SheetCream = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#F5EBD2")));
		const FLinearColor SheetTab = FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#5A3E36")));
		TestTrue(TEXT("판 색 #F5EBD2"), Panel->GetBrushColor().Equals(SheetCream, 0.001f));
		const FSlateBrush& PanelBrush = Panel->Background;
		TestEqual(TEXT("판 모양은 둥근 네모"), PanelBrush.DrawAs, ESlateBrushDrawType::RoundedBox);
		const float Radius = PanelBrush.OutlineSettings.CornerRadii.X;
		TestTrue(FString::Printf(TEXT("판 모서리 6px — DESIGN 7.1 (지금 %.1f)"), Radius), FMath::IsNearlyEqual(Radius, SnareUiStyle::ScreenPanelRadius));
		TestEqual(TEXT("판 테두리 없음"), PanelBrush.OutlineSettings.Width, 0.f);
		TestTrue(TEXT("탭 색 #5A3E36"), Pill->Background.TintColor.GetSpecifiedColor().Equals(SheetTab, 0.001f));
		TestTrue(TEXT("제목 글자는 탭 안에 든다"), Title->GetParent() == Pill);

		const UCanvasPanelSlot* const PanelSlot = Cast<UCanvasPanelSlot>(Panel->Slot);
		const UCanvasPanelSlot* const PillSlot = Cast<UCanvasPanelSlot>(Pill->Slot);
		if (TestNotNull(TEXT("판 캔버스 자리"), PanelSlot) && TestNotNull(TEXT("탭 캔버스 자리"), PillSlot))
		{
			const float PanelTop = PanelSlot->GetPosition().Y;
			const float PillTop = PillSlot->GetPosition().Y;
			const float PillBottom = PillTop + PillSlot->GetSize().Y;
			TestTrue(FString::Printf(TEXT("탭이 판 위로 걸친다 (탭 %.0f~%.0f · 판 위 %.0f)"), PillTop, PillBottom, PanelTop),
				PillTop < PanelTop && PillBottom > PanelTop);

			// 버튼은 전부 판 안에 든다 (가로 가운데 정렬이라 폭과 세로 범위로 잰다).
			const float PanelBottom = PanelTop + PanelSlot->GetSize().Y;
			int32 ButtonCount = 0;
			Screen->WidgetTree->ForEachWidget([&](UWidget* Child)
			{
				const UButton* const Button = Cast<UButton>(Child);
				const UCanvasPanelSlot* const ButtonSlot = Button != nullptr ? Cast<UCanvasPanelSlot>(Button->Slot) : nullptr;
				if (ButtonSlot == nullptr)
				{
					return;
				}
				++ButtonCount;
				const float Top = ButtonSlot->GetPosition().Y;
				TestTrue(FString::Printf(TEXT("%s 가 판 세로 범위 안"), *Button->GetName()),
					Top >= PanelTop && Top + ButtonSlot->GetSize().Y <= PanelBottom);
				TestTrue(FString::Printf(TEXT("%s 가 판 가로 폭 안"), *Button->GetName()),
					ButtonSlot->GetSize().X <= PanelSlot->GetSize().X);
			});
			TestEqual(TEXT("판 위 버튼 수 = 순서 배열"), ButtonCount, UPauseMenuWidget::GetButtonOrder().Num());
		}
	}

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ROADMAP M98-g — 설정 카드의 「만든 사람·라이선스」 칸: 표기 의무가 있는 이름·Jua OFL 원문이 글에 있고, 단추로 펴고 접힌다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCreditsCardTest, "Snarehaven.PauseMenu.CreditsCard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCreditsCardTest::RunTest(const FString& Parameters)
{
	const FString Text = SnareCredits::GetText();
	for (const FString& Name : SnareCredits::GetRequiredNames())
	{
		TestTrue(FString::Printf(TEXT("라이선스 글에 「%s」 가 있다"), *Name), Text.Contains(Name));
	}

	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UPauseMenuWidget* Menu = CreateWidget<UPauseMenuWidget>(World, UPauseMenuWidget::StaticClass());
	if (TestNotNull(TEXT("일시정지 메뉴 위젯"), Menu))
	{
		Menu->TakeWidget();
		Menu->SetOptionsOpen(true);
		// 설정이 펴진 동안 뒤 메뉴 크림 판이 비치면 안 된다 (M98-g 보정).
		const UBorder* const MenuPanel = Menu->WidgetTree->FindWidget<UBorder>(TEXT("MenuPanel"));
		if (TestNotNull(TEXT("메뉴 크림 판"), MenuPanel))
		{
			TestEqual(TEXT("설정이 펴지면 메뉴 판은 접힌다"), MenuPanel->GetVisibility(), ESlateVisibility::Collapsed);
		}
		UButton* const Button = Menu->WidgetTree->FindWidget<UButton>(TEXT("OptionsCreditsButton"));
		if (TestNotNull(TEXT("설정 카드에 라이선스 단추가 있다"), Button))
		{
			TestFalse(TEXT("처음엔 접혀 있다"), Menu->IsCreditsOpen());
			Button->OnClicked.Broadcast();
			TestTrue(TEXT("누르면 펴진다"), Menu->IsCreditsOpen());
			Button->OnClicked.Broadcast();
			TestFalse(TEXT("다시 누르면 접힌다"), Menu->IsCreditsOpen());
			// 접었다 펴도 글자 없는 닉네임 알약(까만 점)이 되살아나면 안 된다.
			const UBorder* const Pill = Menu->WidgetTree->FindWidget<UBorder>(TEXT("OptionsNicknamePill"));
			if (TestNotNull(TEXT("닉네임 알약"), Pill))
			{
				TestEqual(TEXT("라이선스를 접어도 빈 알약은 접혀 있다"), Pill->GetVisibility(), ESlateVisibility::Collapsed);
			}
		}
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
