// 월드 글자 알약(`USnareWorldLabelWidget`, DESIGN 8.18 친구 나무 이름표) — 글자 설정·읽기.
// 위젯 트리는 세상이 있어야 서므로 여기서는 글자 값만 단정한다(화면 모양은 캡처 몫).
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Ui.WorldLabel; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/SnareWorldLabelWidget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldLabelTest, "Snarehaven.Ui.WorldLabel.SetLabelKeepsText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenWorldLabelTest::RunTest(const FString& Parameters)
{
	USnareWorldLabelWidget* const Widget = NewObject<USnareWorldLabelWidget>(GetTransientPackage());
	if (!TestNotNull(TEXT("라벨 위젯을 만든다"), Widget))
	{
		return false;
	}
	TestTrue(TEXT("처음엔 글자가 비어 있다"), Widget->GetLabel().IsEmpty());

	Widget->SetLabel(FText::FromString(TEXT("시험의 복숭아나무")));
	TestEqual(TEXT("글자를 설정하면 읽힌다"), Widget->GetLabel().ToString(), FString(TEXT("시험의 복숭아나무")));

	Widget->SetLabel(FText::FromString(TEXT("다른 나무")));
	TestEqual(TEXT("다시 설정하면 바뀐다"), Widget->GetLabel().ToString(), FString(TEXT("다른 나무")));

	Widget->SetLabel(FText::GetEmpty());
	TestTrue(TEXT("빈 글자도 받는다"), Widget->GetLabel().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenWorldLabelEdgeTest, "Snarehaven.Ui.WorldLabel.HiddenWhenClippedByScreenEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 10-16 목장 왼쪽 아래 잘린 「라요」(고구마 장수 팻말): 알약이 뷰포트 가장자리에 걸치면 숨긴다.
bool FSnarehavenWorldLabelEdgeTest::RunTest(const FString& Parameters)
{
	const FSlateRect Viewport(0.f, 0.f, 1334.f, 616.f);
	TestTrue(TEXT("안쪽 알약은 보인다"), USnareWorldLabelWidget::IsFullyInside(FSlateRect(100.f, 100.f, 300.f, 140.f), Viewport));
	TestFalse(TEXT("왼쪽 끝에 걸친 알약은 숨는다"), USnareWorldLabelWidget::IsFullyInside(FSlateRect(-120.f, 580.f, 60.f, 620.f), Viewport));
	TestFalse(TEXT("아래 끝에 걸친 알약은 숨는다"), USnareWorldLabelWidget::IsFullyInside(FSlateRect(400.f, 600.f, 600.f, 640.f), Viewport));
	TestTrue(TEXT("변에 딱 붙은 알약은 보인다"), USnareWorldLabelWidget::IsFullyInside(FSlateRect(0.f, 0.f, 200.f, 40.f), Viewport));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
