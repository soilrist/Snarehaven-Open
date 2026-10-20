// ROADMAP M37-04 2층 검사 — 뷰포트에 판으로 붙는 위젯은 전부 ESC·뒤로 목록에 있다.
// `Private` 아래 `.cpp`(Tests 빼고)에서 `ScreenPanelViewportZOrder`·`AddedViewportZOrder` 가 나오는 줄마다
// 그 줄을 품은 함수의 클래스(`U…::함수(` 로 시작하는 가장 가까운 윗줄)를 판 클래스로 모은다.
// 그 이름이 `SnareScreenCloser.cpp` 의 `CloseOpenScreens`·`CountOpenScreens` 두 함수 몸 안에 각각 낱말로 나오는지 본다.
// 이름만 보는 탐침이라 "닫힌다"를 증명하지는 않는다(그건 Snarehaven.UI.EscClose.*) — 새 판이 목록 없이 들어오는 것을 막는 문지기다.
// 실행: UnrealEditor <uproject> -ExecCmds="Automation RunTests Snarehaven.UI.EscClose.EveryPanelListed; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace SnareEscPanelListTest
{
	const TCHAR* const CloserFile = TEXT("UI/SnareScreenCloser.cpp");

	// 목록에 없어도 되는 판 클래스 → 이유 한 줄.
	const TMap<FString, FString>& Exceptions()
	{
		static const TMap<FString, FString> Map;
		return Map;
	}

	struct FPanelUse
	{
		FString ClassName;
		FString Where;
	};

	// 함수 정의 머리줄(줄 맨 앞에서 시작, `U이름::함수(`)의 클래스 이름. 아니면 빈 글.
	FString DefinitionClass(const FString& Line)
	{
		if (Line.IsEmpty() || FChar::IsWhitespace(Line[0]) || Line.StartsWith(TEXT("//")) || Line.StartsWith(TEXT("#")))
		{
			return FString();
		}
		FRegexMatcher Matcher(FRegexPattern(TEXT("\\b(U[A-Za-z0-9_]+)::[A-Za-z_]\\w*\\s*\\(")), Line);
		return Matcher.FindNext() ? Matcher.GetCaptureGroup(1) : FString();
	}

	// 판 순서 값이 나오는 줄마다 그 줄을 품은 함수의 클래스.
	void CollectPanels(const FString& RelativePath, const FString& Text, TArray<FPanelUse>& OutPanels)
	{
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, /*bCullEmpty=*/false);
		FString CurrentClass;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const FString Defined = DefinitionClass(Lines[Index]);
			if (!Defined.IsEmpty())
			{
				CurrentClass = Defined;
			}
			const FString Trimmed = Lines[Index].TrimStart();
			if (Trimmed.StartsWith(TEXT("//")) || !Trimmed.Contains(TEXT("AddToViewport")))
			{
				continue;
			}
			if (!Trimmed.Contains(TEXT("ScreenPanelViewportZOrder")) && !Trimmed.Contains(TEXT("AddedViewportZOrder")))
			{
				continue;
			}
			const FString Where = FString::Printf(TEXT("%s:%d"), *RelativePath, Index + 1);
			OutPanels.Add({ CurrentClass, Where });
		}
	}

	// `bool FSnareScreenCloser::<함수>(` 부터 줄 맨 앞 `}` 까지.
	FString FunctionBody(const FString& Text, const FString& FunctionName)
	{
		const FString Head = FString::Printf(TEXT("FSnareScreenCloser::%s("), *FunctionName);
		const int32 Start = Text.Find(Head, ESearchCase::CaseSensitive);
		if (Start == INDEX_NONE)
		{
			return FString();
		}
		const int32 End = Text.Find(TEXT("\n}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		return End == INDEX_NONE ? FString() : Text.Mid(Start, End - Start);
	}

	bool HasWord(const FString& Text, const FString& Word)
	{
		FRegexMatcher Matcher(FRegexPattern(FString::Printf(TEXT("\\b%s\\b"), *Word)), Text);
		return Matcher.FindNext();
	}

	// 두 함수 몸 중 하나라도 이름이 없는 판 — "클래스 (어디서 붙나) — 빠진 함수".
	TArray<FString> FindMissing(const TArray<FPanelUse>& Panels, const FString& CloseBody, const FString& HasBody,
		const TMap<FString, FString>& Allowed)
	{
		TArray<FString> Missing;
		for (const FPanelUse& Panel : Panels)
		{
			if (Allowed.Contains(Panel.ClassName))
			{
				continue;
			}
			TArray<FString> Lacks;
			if (Panel.ClassName.IsEmpty() || !HasWord(CloseBody, Panel.ClassName))
			{
				Lacks.Add(TEXT("CloseOpenScreens"));
			}
			if (Panel.ClassName.IsEmpty() || !HasWord(HasBody, Panel.ClassName))
			{
				Lacks.Add(TEXT("CountOpenScreens"));
			}
			if (Lacks.Num() > 0)
			{
				Missing.Add(FString::Printf(TEXT("%s (%s) — %s"),
					Panel.ClassName.IsEmpty() ? TEXT("<클래스 못 찾음>") : *Panel.ClassName, *Panel.Where,
					*FString::Join(Lacks, TEXT("·"))));
			}
		}
		return Missing;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEscPanelListTest,
	"Snarehaven.UI.EscClose.EveryPanelListed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEscPanelListTest::RunTest(const FString& Parameters)
{
	using namespace SnareEscPanelListTest;

	const FString PrivateDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir() / TEXT("Snarehaven/Private"));

	FString CloserText;
	if (!FFileHelper::LoadFileToString(CloserText, *(PrivateDir / CloserFile)))
	{
		AddError(FString::Printf(TEXT("못 읽었다: %s"), CloserFile));
		return false;
	}
	const FString CloseBody = FunctionBody(CloserText, TEXT("CloseOpenScreens"));
	const FString HasBody = FunctionBody(CloserText, TEXT("CountOpenScreens"));
	TestFalse(TEXT("CloseOpenScreens 몸을 찾았다"), CloseBody.IsEmpty());
	TestFalse(TEXT("CountOpenScreens 몸을 찾았다"), HasBody.IsEmpty());

	TArray<FString> SourceFiles;
	IFileManager::Get().FindFilesRecursive(SourceFiles, *PrivateDir, TEXT("*.cpp"), true, false);
	TArray<FPanelUse> Panels;
	for (const FString& File : SourceFiles)
	{
		FString Relative = File;
		FPaths::MakePathRelativeTo(Relative, *(PrivateDir + TEXT("/")));
		if (Relative.StartsWith(TEXT("Tests/")))
		{
			continue;
		}
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			AddError(FString::Printf(TEXT("못 읽었다: %s"), *Relative));
			continue;
		}
		CollectPanels(Relative, Text, Panels);
	}

	for (const TPair<FString, FString>& Exception : Exceptions())
	{
		TestTrue(FString::Printf(TEXT("예외 판이 아직 붙는다: %s"), *Exception.Key),
			Panels.ContainsByPredicate([&Exception](const FPanelUse& Panel) { return Panel.ClassName == Exception.Key; }));
		AddInfo(FString::Printf(TEXT("예외 %s — %s"), *Exception.Key, *Exception.Value));
	}

	TArray<FString> Names;
	for (const FPanelUse& Panel : Panels)
	{
		Names.AddUnique(Panel.ClassName);
	}
	const TArray<FString> Missing = FindMissing(Panels, CloseBody, HasBody, Exceptions());
	for (const FString& Line : Missing)
	{
		AddError(FString::Printf(TEXT("ESC 목록에 없는 판: %s — SnareScreenCloser.cpp 두 함수에 넣거나 이유와 함께 예외로"), *Line));
	}
	AddInfo(FString::Printf(TEXT("판이 붙는 줄 %d개 · 판 클래스 %d개(%s) · 빠진 것 %d개"),
		Panels.Num(), Names.Num(), *FString::Join(Names, TEXT(", ")), Missing.Num()));
	TestTrue(TEXT("판이 붙는 줄을 찾았다"), Panels.Num() > 0);
	return Missing.Num() == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEscPanelListSelfTest,
	"Snarehaven.UI.EscClose.EveryPanelListedCatchesMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEscPanelListSelfTest::RunTest(const FString& Parameters)
{
	using namespace SnareEscPanelListTest;

	// 시험 전용 가짜 판 소스 — 정적 열기 함수 안에서 붙는 판 하나, 멤버 함수에서 순서 값으로 붙는 판 하나, 주석 줄 하나.
	const FString FakeSource = TEXT(
		"#include \"UI/FakeEscPanelWidget.h\"\n"
		"UFakeEscPanelWidget* UFakeEscPanelWidget::Open(UWorld* World)\n"
		"{\n"
		"\tScreen->AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder);\n"
		"}\n"
		"void UFakeListedWidget::Refresh()\n"
		"{\n"
		"\t// AddToViewport(SnareUiStyle::ScreenPanelViewportZOrder) — 주석은 안 센다\n"
		"\tAddToViewport(AddedViewportZOrder);\n"
		"}\n");
	TArray<FPanelUse> Panels;
	CollectPanels(TEXT("UI/FakeEscPanelWidget.cpp"), FakeSource, Panels);
	TestEqual(TEXT("판이 붙는 줄 둘(주석 빼고)"), Panels.Num(), 2);
	TestTrue(TEXT("정적 열기 함수의 클래스"), Panels.Num() > 0 && Panels[0].ClassName == TEXT("UFakeEscPanelWidget"));
	TestTrue(TEXT("멤버 함수의 클래스"), Panels.Num() > 1 && Panels[1].ClassName == TEXT("UFakeListedWidget"));
	TestTrue(TEXT("어디서 붙나 = 파일:줄"), Panels.Num() > 0 && Panels[0].Where == TEXT("UI/FakeEscPanelWidget.cpp:4"));

	const FString FakeCloser = TEXT(
		"bool FSnareScreenCloser::CloseOpenScreens(UWorld* InWorld)\n"
		"{\n"
		"\tCast<UFakeListedWidget>(Widget);\n"
		"}\n"
		"bool FSnareScreenCloser::HasOpenScreen(UWorld* InWorld)\n"
		"{\n"
		"\tWidget->IsA<UFakeListedWidget>() || Widget->IsA<UFakeEscPanelWidget>();\n"
		"}\n");
	const FString CloseBody = FunctionBody(FakeCloser, TEXT("CloseOpenScreens"));
	const FString HasBody = FunctionBody(FakeCloser, TEXT("HasOpenScreen"));
	TestFalse(TEXT("닫기 몸에 HasOpenScreen 글이 섞이지 않는다"), CloseBody.Contains(TEXT("UFakeEscPanelWidget")));
	TestFalse(TEXT("이름 일부는 낱말이 아니다"), HasWord(TEXT("UFakeListedWidgetHelper"), TEXT("UFakeListedWidget")));

	const TArray<FString> Missing = FindMissing(Panels, CloseBody, HasBody, {});
	TestEqual(TEXT("닫기 목록에 없는 가짜 판 하나만 잡는다"), Missing.Num(), 1);
	TestTrue(TEXT("잡힌 것 = UFakeEscPanelWidget 의 CloseOpenScreens"),
		Missing.Num() == 1 && Missing[0].Contains(TEXT("UFakeEscPanelWidget")) && Missing[0].EndsWith(TEXT("— CloseOpenScreens")));

	const TMap<FString, FString> Allowed = { { TEXT("UFakeEscPanelWidget"), TEXT("시험 예외") } };
	TestEqual(TEXT("예외로 적은 판은 안 잡는다"), FindMissing(Panels, CloseBody, HasBody, Allowed).Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
