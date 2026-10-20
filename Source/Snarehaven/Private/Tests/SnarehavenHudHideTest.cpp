// INBOX 결함 2026-10-11 05:00 — 창이 열려 있는 동안 HUD(목표 띠·상황 카드·행동 버튼)를 접는 한 곳.
// 잡는 것: 창 열림 → 뿌리 위젯 Collapsed · 닫힘 → 접기 전 보임 상태 그대로 · 위젯 없어도 안 터짐.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.Hud.HideWhileScreenOpen; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/UserWidget.h"
#include "SnarehavenCharacter.h"
#include "UI/SnareNextGoalWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnareHudHideWhileScreenOpenTest, "Snarehaven.Hud.HideWhileScreenOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter)

bool FSnareHudHideWhileScreenOpenTest::RunTest(const FString& Parameters)
{
	UUserWidget* const Hud = NewObject<USnareNextGoalWidget>(GetTransientPackage());
	Hud->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	ASnarehavenCharacter::FHudHideSlot Slot;

	ASnarehavenCharacter::ApplyHudHidden(Hud, true, Slot);
	TestEqual(TEXT("창 열림 → Collapsed"), Hud->GetVisibility(), ESlateVisibility::Collapsed);

	ASnarehavenCharacter::ApplyHudHidden(Hud, true, Slot);
	TestEqual(TEXT("열린 채 또 불러도 접힌 채"), Hud->GetVisibility(), ESlateVisibility::Collapsed);

	ASnarehavenCharacter::ApplyHudHidden(Hud, false, Slot);
	TestEqual(TEXT("창 닫힘 → 접기 전 상태로"), Hud->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);

	ASnarehavenCharacter::ApplyHudHidden(Hud, false, Slot);
	TestEqual(TEXT("닫힌 채 또 불러도 그대로"), Hud->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);

	ASnarehavenCharacter::FHudHideSlot Empty;
	ASnarehavenCharacter::ApplyHudHidden(nullptr, true, Empty);
	TestFalse(TEXT("위젯 없으면 접힌 척 안 함"), Empty.bHidden);
	return true;
}

#endif
