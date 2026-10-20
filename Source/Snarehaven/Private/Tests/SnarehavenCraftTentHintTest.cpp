// ROADMAP M107-05 결함 — 텐트가 없으면 작업대 레시피 「만들기」가 꺼진 색이고 이유가 보이는가 (표시만, 판정은 서버 `CraftItem`).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Craft.TentHint; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Crafting/SnareCraftRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCraftTentHintTest, "Snarehaven.Craft.TentHint.WorkbenchNeedsTent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenCraftTentHintTest::RunTest(const FString& Parameters)
{
	TArray<FSnareRecipeView> Views;
	Views.AddDefaulted(2);
	Views[0].Station = ERecipeStation::Workbench;
	Views[1].Station = ERecipeStation::Anywhere;

	SnareCraftRules::MarkTentNeeded(Views, false);
	TestTrue(TEXT("텐트 없음: 작업대 줄은 텐트 필요"), Views[0].bNeedsTent);
	TestFalse(TEXT("텐트 없음: 작업대 줄 만들기 꺼짐"), Views[0].CanCraft());
	TestFalse(TEXT("텐트 없음: 어디서나 줄은 그대로"), Views[1].bNeedsTent);
	TestTrue(TEXT("텐트 없음: 어디서나 줄 만들기 켜짐"), Views[1].CanCraft());

	SnareCraftRules::MarkTentNeeded(Views, true);
	TestFalse(TEXT("텐트 있음: 작업대 줄 풀림"), Views[0].bNeedsTent);
	TestTrue(TEXT("텐트 있음: 작업대 줄 만들기 켜짐"), Views[0].CanCraft());
	return true;
}

#endif
