// M28-06b 안내서 화면 2층 검사 (`USnareGuideBookWidget`).
// 이 검사가 잡는 것: ① 왼쪽 목록 버튼 수가 표의 쪽 수(활동 11)와 다른 것 ② 쪽을 눌러도 오른쪽이 안 바뀌거나
// 제목·순서 줄이 표와 다른 것(줄 3, 앞 번호 「1. 」) ③ 아이콘이 빈 원판인 것 ④ 글자 하나라도 UI 글꼴(Jua)이 아닌 것
// ⑤ ESC 가 안내서를 안 닫거나, 안내서와 함께 밑의 배낭까지 한 번에 닫는 것(안내서만 닫혀야 한다).
// ⑥ 일시정지 메뉴 「안내서」 버튼이 없거나, 눌러도 안내서가 안 뜨거나, 메뉴가 안내서 밑에 남는 것(M28-06b2).
// ⑦ 배낭 세로 탭 「안내서」 가 없거나(툴팁 글자로 찾는다), 눌러도 안내서가 안 뜨거나,
//   안내서를 띄우며 배낭 판을 닫는 것(안내서를 닫으면 배낭으로 돌아와야 한다, M28-06b2b).
// ⑧ 목록 쪽 버튼·닫기 높이가 72(DESIGN 2 「🟡 누르는 칸 최소 크기」) 미만이거나, 목록 버튼이 스크롤 밖이라
//   작은 화면에서 마지막 줄을 끌어 볼 수 없는 것(M38-04).
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.UI.GuideBook; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Data/UsageHintRow.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/PauseMenuWidget.h"
#include "UI/SnareGuideBookWidget.h"
#include "UI/SnareScreenCloser.h"
#include "UI/SnareUiStyle.h"
#include "UI/SnareUsageHints.h"

namespace
{
	UWorld* CreateGuideTestWorld()
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyGuideTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGuideBookPagesTest, "Snarehaven.UI.GuideBook.PagesFromTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGuideBookPagesTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = SnareUsageHints::LoadTable();
	if (!TestNotNull(TEXT("DT_UsageHints 로드"), Table))
	{
		return false;
	}
	const TArray<FName> ExpectedKeys = SnareUsageHints::GetGuidePageKeys(Table);

	UWorld* const World = CreateGuideTestWorld();
	USnareGuideBookWidget* const Guide = CreateWidget<USnareGuideBookWidget>(World, USnareGuideBookWidget::StaticClass());
	if (!TestNotNull(TEXT("안내서 화면"), Guide))
	{
		DestroyGuideTestWorld(World);
		return false;
	}
	// Slate 까지 세워야 `RebuildWidget` 이 트리를 만들고 글꼴을 입힌다 — 반환값을 쥐고 있어야 연결이 안 끊긴다.
	const TSharedRef<SWidget> GuideSlate = Guide->TakeWidget();

	// ① 목록 = 표의 쪽 (활동 11, M28-06a 표).
	TestEqual(TEXT("쪽 수 = 활동 수 11"), Guide->GetPageKeys().Num(), 11);
	TestEqual(TEXT("쪽 순서 = 표 GuideOrder 순"), Guide->GetPageKeys(), ExpectedKeys);
	TestEqual(TEXT("왼쪽 목록 버튼 수 = 쪽 수"), Guide->GetPageButtonCount(), Guide->GetPageKeys().Num());
	TestEqual(TEXT("처음 열면 첫 쪽"), Guide->GetSelectedPage(), 0);

	// ② ③ 쪽마다 눌러 오른쪽 글자가 표 그대로인지.
	TArray<FString> Lines;
	for (int32 Index = 0; Index < ExpectedKeys.Num(); ++Index)
	{
		Guide->SelectPage(Index);
		const FUsageHintRow* const Row = SnareUsageHints::FindRow(Table, ExpectedKeys[Index]);
		if (!TestNotNull(FString::Printf(TEXT("%s 행"), *ExpectedKeys[Index].ToString()), Row))
		{
			continue;
		}
		const FString Title = Guide->GetShownTitle().ToString();
		TestEqual(FString::Printf(TEXT("%d쪽 고름"), Index + 1), Guide->GetSelectedPage(), Index);
		TestEqual(FString::Printf(TEXT("%d쪽 제목 = 표"), Index + 1), Title, Row->GuideTitle.ToString());
		const TArray<FText> Steps = Guide->GetShownSteps();
		TestEqual(FString::Printf(TEXT("%s 순서 줄 수"), *Title), Steps.Num(), 3);
		for (int32 Step = 0; Step < Steps.Num() && Step < Row->GuideSteps.Num(); ++Step)
		{
			TestEqual(FString::Printf(TEXT("%s %d줄"), *Title, Step + 1), Steps[Step].ToString(),
				FString::Printf(TEXT("%d. %s"), Step + 1, *Row->GuideSteps[Step].ToString()));
		}
		TestTrue(FString::Printf(TEXT("%s 아이콘 보임"), *Title), Guide->HasShownIcon());
		Lines.Add(FString::Printf(TEXT("%s %d줄%s"), *Title, Steps.Num(), Guide->HasShownIcon() ? TEXT("+그림") : TEXT("")));
	}
	Guide->SelectPage(99);
	TestEqual(TEXT("범위 밖 쪽은 무시"), Guide->GetSelectedPage(), ExpectedKeys.Num() - 1);

	// ④ 모든 글자가 UI 글꼴 — 쪽을 바꾼 뒤 새로 생긴 순서 줄까지.
	const UFont* const UiFont = SnareUiStyle::GetUiFont();
	int32 TextCount = 0;
	int32 WrongFont = 0;
	Guide->WidgetTree->ForEachWidget([&TextCount, &WrongFont, UiFont](UWidget* Child)
	{
		if (const UTextBlock* const Text = Cast<UTextBlock>(Child))
		{
			++TextCount;
			WrongFont += Cast<const UFont>(Text->GetFont().FontObject.Get()) == UiFont ? 0 : 1;
		}
	});
	TestNotNull(TEXT("UI 글꼴 에셋"), UiFont);
	TestTrue(TEXT("글자가 있다"), TextCount > 0);
	TestEqual(TEXT("UI 글꼴이 아닌 글자 수"), WrongFont, 0);

	AddInfo(FString::Printf(TEXT("[GuideBook] 목록 %d · 글자 %d개(다른 글꼴 %d) · %s"), Guide->GetPageButtonCount(),
		TextCount, WrongFont, *FString::Join(Lines, TEXT(" · "))));
	DestroyGuideTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGuideBookEscTest, "Snarehaven.UI.GuideBook.EscClosesOnlyGuide",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGuideBookEscTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("Slate 앱이 있다"), FSlateApplication::IsInitialized()))
	{
		return false;
	}
	UWorld* const World = CreateGuideTestWorld();
	FSnareScreenCloser Closer(World);
	const FKeyEvent Esc(EKeys::Escape, FModifierKeysState(), /*UserIndex=*/0, /*bIsRepeat=*/false, 0, 0);

	// ⑤ 배낭 위에 안내서 — 검사 월드엔 뷰포트가 없어 판 하나에 붙인다(ESC 검사의 도감과 같은 꼴).
	UBackpackScreenWidget* const Backpack = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	Backpack->SetPanelOpen(true);
	UCanvasPanel* const Holder = NewObject<UCanvasPanel>(GetTransientPackage());
	USnareGuideBookWidget* const Guide = CreateWidget<USnareGuideBookWidget>(World, USnareGuideBookWidget::StaticClass());
	Holder->AddChild(Guide);

	TestTrue(TEXT("안내서가 열려 있으면 판 열림으로 친다(한 줄 안내가 위에 안 뜸)"), FSnareScreenCloser::HasOpenScreen(World));
	TestTrue(TEXT("ESC 를 먹는다"), Closer.HandleKeyDownEvent(FSlateApplication::Get(), Esc));
	TestNull(TEXT("안내서: 떼어 냈다"), Guide->GetParent());
	TestTrue(TEXT("밑의 배낭은 그대로 열려 있다"), Backpack->IsPanelOpen());
	TestTrue(TEXT("두 번째 ESC 를 먹는다"), Closer.HandleKeyDownEvent(FSlateApplication::Get(), Esc));
	TestFalse(TEXT("두 번째 ESC 에 배낭이 닫혔다"), Backpack->IsPanelOpen());

	DestroyGuideTestWorld(World);
	return true;
}

// ⑧ M38-04 — 목록 쪽 버튼·닫기는 손가락 한 마디(72) 이상, 목록 버튼은 전부 스크롤 안(넘쳐도 마지막 줄까지 끌어 볼 수 있다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGuideBookTouchSizeTest, "Snarehaven.UI.TouchSize.GuideBookList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGuideBookTouchSizeTest::RunTest(const FString& Parameters)
{
	constexpr float DesignMinTouch = 72.f;
	UWorld* const World = CreateGuideTestWorld();
	USnareGuideBookWidget* const Guide = CreateWidget<USnareGuideBookWidget>(World, USnareGuideBookWidget::StaticClass());
	if (!TestNotNull(TEXT("안내서 위젯"), Guide))
	{
		DestroyGuideTestWorld(World);
		return false;
	}
	Guide->TakeWidget();
	UWidgetTree* const Tree = Guide->WidgetTree;
	const UScrollBox* const ListScroll = Tree->FindWidget<UScrollBox>(TEXT("GuideBookListScroll"));
	const USizeBox* const CloseBox = Tree->FindWidget<USizeBox>(TEXT("GuideBookCloseBox"));
	if (!TestNotNull(TEXT("목록 스크롤"), ListScroll) || !TestNotNull(TEXT("닫기 상자"), CloseBox))
	{
		DestroyGuideTestWorld(World);
		return false;
	}

	int32 Boxes = 0;
	int32 Short = 0;
	int32 OutsideScroll = 0;
	float MinHeight = TNumericLimits<float>::Max();
	for (int32 Index = 0;; ++Index)
	{
		const USizeBox* const Box = Tree->FindWidget<USizeBox>(FName(*FString::Printf(TEXT("GuidePageButtonBox_%d"), Index)));
		if (Box == nullptr)
		{
			break;
		}
		++Boxes;
		MinHeight = FMath::Min(MinHeight, Box->GetHeightOverride());
		Short += Box->GetHeightOverride() < DesignMinTouch ? 1 : 0;
		OutsideScroll += Box->GetParent() != ListScroll ? 1 : 0;
	}
	TestTrue(TEXT("목록 버튼이 있다"), Boxes > 0);
	TestEqual(TEXT("목록 버튼 높이 < 72 인 것 0"), Short, 0);
	TestEqual(TEXT("스크롤 밖 목록 버튼 0(마지막 줄까지 끌어 볼 수 있다)"), OutsideScroll, 0);
	TestTrue(TEXT("닫기 높이 ≥ 72"), CloseBox->GetHeightOverride() >= DesignMinTouch);
	AddInfo(FString::Printf(TEXT("[GuideBookTouch] 목록 버튼 %d · 가장 낮은 %.0f · 72 미만 %d · 스크롤 밖 %d · 닫기 %.0f×%.0f"),
		Boxes, Boxes > 0 ? MinHeight : 0.f, Short, OutsideScroll, CloseBox->GetWidthOverride(), CloseBox->GetHeightOverride()));

	DestroyGuideTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGuideBookPauseButtonTest, "Snarehaven.UI.GuideBook.PauseMenuButtonOpens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGuideBookPauseButtonTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateGuideTestWorld();
	UPauseMenuWidget* const Menu = CreateWidget<UPauseMenuWidget>(World, UPauseMenuWidget::StaticClass());
	if (!TestNotNull(TEXT("일시정지 메뉴"), Menu))
	{
		DestroyGuideTestWorld(World);
		return false;
	}
	// 검사 월드엔 뷰포트가 없어 판 하나에 붙여 「열린 메뉴」 를 흉내 낸다 — 누름 뒤 떼어졌는지로 닫힘을 본다.
	UCanvasPanel* const Holder = NewObject<UCanvasPanel>(GetTransientPackage());
	Holder->AddChild(Menu);
	const TSharedRef<SWidget> MenuSlate = Menu->TakeWidget();

	// ⑥ 버튼과 화면 글자 — 이름이 아니라 버튼 안 글자를 본다.
	UButton* const GuideButton = Menu->WidgetTree->FindWidget<UButton>(TEXT("GuideButton"));
	if (!TestNotNull(TEXT("메뉴에 안내서 버튼이 있다"), GuideButton))
	{
		DestroyGuideTestWorld(World);
		return false;
	}
	const UTextBlock* const Label = Cast<UTextBlock>(GuideButton->GetContent());
	const FString LabelText = Label != nullptr ? Label->GetText().ToString() : FString();
	TestEqual(TEXT("버튼 글자"), LabelText, FString(TEXT("안내서")));
	TestNull(TEXT("누르기 전엔 안내서 없음"), Menu->GetOpenedGuideBook());

	GuideButton->OnClicked.Broadcast();

	USnareGuideBookWidget* const Guide = Menu->GetOpenedGuideBook();
	TestNull(TEXT("누르면 메뉴는 닫힌다(안내서 밑에 안 남음)"), Menu->GetParent());
	int32 PageCount = 0;
	if (TestNotNull(TEXT("누르면 안내서가 뜬다"), Guide))
	{
		const TSharedRef<SWidget> GuideSlate = Guide->TakeWidget();
		PageCount = Guide->GetPageButtonCount();
		TestEqual(TEXT("뜬 안내서의 목록 = 활동 11"), PageCount, 11);
		TestEqual(TEXT("첫 쪽부터"), Guide->GetSelectedPage(), 0);
	}
	AddInfo(FString::Printf(TEXT("[GuideBook] 일시정지 「%s」 누름 → 안내서 %s · 목록 %d · 메뉴 %s"), *LabelText,
		Guide != nullptr ? TEXT("뜸") : TEXT("없음"), PageCount, Menu->GetParent() == nullptr ? TEXT("닫힘") : TEXT("남음")));
	DestroyGuideTestWorld(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGuideBookBackpackTabTest, "Snarehaven.UI.GuideBook.BackpackTabOpens",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGuideBookBackpackTabTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateGuideTestWorld();
	UBackpackScreenWidget* const Backpack = CreateWidget<UBackpackScreenWidget>(World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("배낭 화면"), Backpack))
	{
		DestroyGuideTestWorld(World);
		return false;
	}
	const TSharedRef<SWidget> BackpackSlate = Backpack->TakeWidget();
	Backpack->SetPanelOpen(true);

	// ⑦ 탭 이름이 아니라 탭에 달린 글자(툴팁 — 폰은 그림만, PC·시험이 읽는다)로 「안내서」 탭을 찾는다.
	UButton* GuideTab = nullptr;
	int32 TabCount = 0;
	for (int32 Index = 0; ; ++Index)
	{
		UButton* const Tab = Backpack->WidgetTree->FindWidget<UButton>(FName(*FString::Printf(TEXT("Tab_%d"), Index)));
		if (Tab == nullptr)
		{
			break;
		}
		++TabCount;
		if (Tab->GetToolTipText().ToString() == TEXT("안내서"))
		{
			GuideTab = Tab;
		}
	}
	if (!TestNotNull(TEXT("배낭 세로 탭에 「안내서」 가 있다"), GuideTab))
	{
		DestroyGuideTestWorld(World);
		return false;
	}
	TestNull(TEXT("누르기 전엔 안내서 없음"), Backpack->GetGuideBook());

	GuideTab->OnClicked.Broadcast();
	USnareGuideBookWidget* const Guide = Backpack->GetGuideBook();
	int32 PageCount = 0;
	if (TestNotNull(TEXT("누르면 안내서가 뜬다"), Guide))
	{
		const TSharedRef<SWidget> GuideSlate = Guide->TakeWidget();
		PageCount = Guide->GetPageButtonCount();
		TestEqual(TEXT("뜬 안내서의 목록 = 활동 11"), PageCount, 11);
		TestEqual(TEXT("첫 쪽부터"), Guide->GetSelectedPage(), 0);
	}
	TestTrue(TEXT("배낭 판은 밑에 그대로 열려 있다"), Backpack->IsPanelOpen());

	AddInfo(FString::Printf(TEXT("[GuideBook] 배낭 세로 탭 %d개 중 「안내서」 누름 → 안내서 %s · 목록 %d · 배낭 판 %s"), TabCount,
		Guide != nullptr ? TEXT("뜸") : TEXT("없음"), PageCount, Backpack->IsPanelOpen() ? TEXT("열림") : TEXT("닫힘")));
	DestroyGuideTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
