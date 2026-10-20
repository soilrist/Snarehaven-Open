// ROADMAP M26-05 2층 검사 — 도감·업적·상자 판 버튼이 시트 「모든 패널 모서리 둥글게 12~16px」 대로 둥근 네모인가.
// 이 검사가 잡는 것: ① 새 버튼을 엔진 기본 네모로 세우는 것 ② 색을 덮는 `ApplyUiStyle`·탭 강조(`MarkSelected`)가
// 모양까지 덮어 네모로 되돌리는 것 ③ 둥글게 바꾸다 고른 탭 강조 색을 잃는 것.
// 화면에서 실제로 둥글게 보이는지는 3층(도감 캡처)이 본다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.UI.PanelButtons; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UI/AchievementScreenWidget.h"
#include "UI/ChestScreenWidget.h"
#include "UI/CodexScreenWidget.h"
#include "UI/SnareUiStyle.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPanelButtonRoundTest, "Snarehaven.UI.PanelButtons.Rounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPanelButtonRoundTest::RunTest(const FString& Parameters)
{
	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	UCodexScreenWidget* const Codex = CreateWidget<UCodexScreenWidget>(World, UCodexScreenWidget::StaticClass());
	UAchievementScreenWidget* const Achievement = CreateWidget<UAchievementScreenWidget>(World, UAchievementScreenWidget::StaticClass());
	UChestScreenWidget* const Chest = CreateWidget<UChestScreenWidget>(World, UChestScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("도감 화면 위젯"), Codex) || !TestNotNull(TEXT("업적 화면 위젯"), Achievement) || !TestNotNull(TEXT("상자 화면 위젯"), Chest))
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	Codex->TakeWidget();
	Achievement->TakeWidget();
	Chest->TakeWidget();

	// 기본(낚시) 말고 다른 탭을 골라 강조가 다시 칠해진 뒤에도 모양이 남는지 본다.
	const ECodexCategory Picked = static_cast<ECodexCategory>(0);
	Codex->SetSelectedCategory(Picked);

	const FVector4 Radius(SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius);
	auto IsRound = [&Radius](const FSlateBrush& Brush)
	{
		return Brush.DrawAs == ESlateBrushDrawType::RoundedBox && Brush.OutlineSettings.CornerRadii == Radius;
	};

	struct FCase { const TCHAR* Label; UUserWidget* Widget; FString ButtonName; };
	TArray<FCase> Cases = {
		{TEXT("도감 업적"), Codex, TEXT("AchievementButton")},
		{TEXT("도감 닫기"), Codex, TEXT("CodexCloseButton")},
		{TEXT("업적 닫기"), Achievement, TEXT("CloseButton")},
		{TEXT("상자 열기"), Chest, TEXT("OpenButton")},
		{TEXT("상자 확률 카드"), Chest, TEXT("DisclosureCardButton")},
		{TEXT("상자 확률 보기"), Chest, TEXT("DisclosureButton")},
		{TEXT("상자 닫기"), Chest, TEXT("CloseButton")},
	};
	for (int32 Index = 0; Index < static_cast<int32>(ECodexCategory::Count); ++Index)
	{
		Cases.Add({TEXT("도감 탭"), Codex, FString::Printf(TEXT("CodexTabButton_%d"), Index)});
	}

	int32 Rounded = 0;
	for (const FCase& Case : Cases)
	{
		const UButton* const Button = Case.Widget->WidgetTree != nullptr
			? Case.Widget->WidgetTree->FindWidget<UButton>(FName(*Case.ButtonName)) : nullptr;
		if (!TestNotNull(FString::Printf(TEXT("%s 버튼 %s"), Case.Label, *Case.ButtonName), Button))
		{
			continue;
		}
		const FButtonStyle& Style = Button->GetStyle();
		const bool bRound = IsRound(Style.Normal) && IsRound(Style.Hovered) && IsRound(Style.Pressed) && IsRound(Style.Disabled);
		TestTrue(FString::Printf(TEXT("%s(%s) 네 상태 둥근 네모 · 반지름 = HudCornerRadius"), Case.Label, *Case.ButtonName), bRound);
		Rounded += bRound ? 1 : 0;
	}

	// 고른 탭 강조 색은 그대로 — 고른 탭 = 강조 노랑, 나머지 = 기본 아이보리.
	int32 ColorOk = 0;
	for (int32 Index = 0; Index < static_cast<int32>(ECodexCategory::Count); ++Index)
	{
		const UButton* const Tab = Codex->WidgetTree->FindWidget<UButton>(FName(*FString::Printf(TEXT("CodexTabButton_%d"), Index)));
		if (Tab == nullptr)
		{
			continue;
		}
		const FLinearColor Expected = static_cast<ECodexCategory>(Index) == Picked ? SnareUiStyle::UiButtonHighlight() : SnareUiStyle::UiWoodButton();
		const bool bColor = Tab->GetStyle().Normal.TintColor.GetSpecifiedColor() == Expected;
		TestTrue(FString::Printf(TEXT("도감 탭 %d 색 = %s"), Index, static_cast<ECodexCategory>(Index) == Picked ? TEXT("UiButtonHighlight") : TEXT("UiWoodButton")), bColor);
		ColorOk += bColor ? 1 : 0;
	}

	AddInfo(FString::Printf(TEXT("[PanelButtons] 둥근 버튼 %d/%d · 반지름 %.0f · 탭 색 맞음 %d/%d"),
		Rounded, Cases.Num(), SnareUiStyle::HudCornerRadius, ColorOk, static_cast<int32>(ECodexCategory::Count)));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
