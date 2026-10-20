// ROADMAP M36-04 2층 검사 — 시험이 한 번도 안 부르는 코드 0.
// `Private` 아래 `.cpp`(Tests 빼고)마다 파일 이름·그 파일과 짝 헤더의 클래스/구조체/이름공간 이름 중 하나라도
// `Private/Tests` 글에 낱말로 나오는지 본다(`U`·`A`·`F`·`I`·`E`·`S` 머리 붙은 꼴도 같은 이름). 없으면 그 파일을 알리고 실패.
// 이름만 보는 탐침이라 "불렀다"를 증명하지는 않는다 — 새 파일이 시험 없이 들어오는 것을 막는 문지기다.
// 이 파일 자신은 글에서 뺀다(예외 목록에 이름이 적혀 있어서 스스로 통과시키지 않게).
// 실행: UnrealEditor <uproject> -ExecCmds="Automation RunTests Snarehaven.Code.EveryClassTested; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace SnareEveryClassTestedTest
{
	const TCHAR* const SelfFileName = TEXT("SnarehavenEveryClassTestedTest.cpp");

	// 시험이 없어도 되는 파일 — `Private` 기준 경로 → 이유 한 줄.
	const TMap<FString, FString>& Exceptions()
	{
		static const TMap<FString, FString> Map = {
			{ TEXT("UI/SnareUiFontLibrary.cpp"),
				TEXT("에디터 도구용 글꼴 함수 — 결과(한글 대체 글꼴)는 Snarehaven.Ui.Font.* 가 에셋째로 단정") },
		};
		return Map;
	}

	struct FSourceUnit
	{
		FString RelativePath;
		TArray<FString> Names;
	};

	// 파일 이름 + `class|struct|namespace [XXX_API] 이름 [final] :|{` 로 선언된 이름.
	TArray<FString> CollectNames(const FString& FileBaseName, const FString& Text)
	{
		TArray<FString> Names = { FileBaseName };
		FRegexMatcher Matcher(FRegexPattern(
			TEXT("\\b(?:class|struct|namespace)\\s+(?:\\w+_API\\s+)?([A-Za-z_]\\w*)\\s*(?:final\\s*)?[:{]")), Text);
		while (Matcher.FindNext())
		{
			Names.AddUnique(Matcher.GetCaptureGroup(1));
		}
		return Names;
	}

	// 글을 낱말(`[A-Za-z_][A-Za-z0-9_]*`)로 자른다. `#include "UI/Foo.h"` 의 `Foo` 도 낱말이다.
	void AddTokens(const FString& Text, TSet<FString>& OutTokens)
	{
		const int32 Len = Text.Len();
		int32 Index = 0;
		while (Index < Len)
		{
			const TCHAR Ch = Text[Index];
			if (FChar::IsAlpha(Ch) || Ch == TEXT('_'))
			{
				const int32 Start = Index;
				while (Index < Len && (FChar::IsAlnum(Text[Index]) || Text[Index] == TEXT('_')))
				{
					++Index;
				}
				OutTokens.Add(Text.Mid(Start, Index - Start));
			}
			else
			{
				++Index;
			}
		}
	}

	bool IsMentioned(const FString& Name, const TSet<FString>& Tokens)
	{
		static const TCHAR* const Prefixes[] = { TEXT("U"), TEXT("A"), TEXT("F"), TEXT("I"), TEXT("E"), TEXT("S") };
		if (Tokens.Contains(Name))
		{
			return true;
		}
		for (const TCHAR* const Prefix : Prefixes)
		{
			if (Tokens.Contains(FString(Prefix) + Name))
			{
				return true;
			}
		}
		return false;
	}

	// 이름이 하나도 안 나오고 예외도 아닌 파일 경로.
	TArray<FString> FindUntested(const TArray<FSourceUnit>& Units, const TSet<FString>& Tokens,
		const TMap<FString, FString>& Allowed)
	{
		TArray<FString> Missing;
		for (const FSourceUnit& Unit : Units)
		{
			if (Allowed.Contains(Unit.RelativePath))
			{
				continue;
			}
			bool bFound = false;
			for (const FString& Name : Unit.Names)
			{
				bFound = bFound || IsMentioned(Name, Tokens);
			}
			if (!bFound)
			{
				Missing.Add(Unit.RelativePath);
			}
		}
		return Missing;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEveryClassTestedTest,
	"Snarehaven.Code.EveryClassTested.NoUntestedSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEveryClassTestedTest::RunTest(const FString& Parameters)
{
	using namespace SnareEveryClassTestedTest;

	const FString PrivateDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir() / TEXT("Snarehaven/Private"));
	const FString PublicDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir() / TEXT("Snarehaven/Public"));
	const FString TestsDir = PrivateDir / TEXT("Tests");

	TSet<FString> Tokens;
	TArray<FString> TestFiles;
	IFileManager::Get().FindFilesRecursive(TestFiles, *TestsDir, TEXT("*.cpp"), true, false);
	IFileManager::Get().FindFilesRecursive(TestFiles, *TestsDir, TEXT("*.h"), true, false, false);
	int32 TestFilesRead = 0;
	for (const FString& File : TestFiles)
	{
		FString Text;
		if (FPaths::GetCleanFilename(File) != SelfFileName && FFileHelper::LoadFileToString(Text, *File))
		{
			AddTokens(Text, Tokens);
			++TestFilesRead;
		}
	}

	TArray<FString> SourceFiles;
	IFileManager::Get().FindFilesRecursive(SourceFiles, *PrivateDir, TEXT("*.cpp"), true, false);
	TArray<FSourceUnit> Units;
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
		FString Header;
		FFileHelper::LoadFileToString(Header, *(PublicDir / FPaths::ChangeExtension(Relative, TEXT("h"))));
		Units.Add({ Relative, CollectNames(FPaths::GetBaseFilename(File), Header + TEXT("\n") + Text) });
	}

	// 예외 목록이 지워진 파일을 붙들고 있지 않은가 — 낡은 줄이 다른 파일 이름으로 되살아나지 않게.
	for (const TPair<FString, FString>& Exception : Exceptions())
	{
		TestTrue(FString::Printf(TEXT("예외 파일이 있다: %s"), *Exception.Key),
			Units.ContainsByPredicate([&Exception](const FSourceUnit& Unit) { return Unit.RelativePath == Exception.Key; }));
		AddInfo(FString::Printf(TEXT("예외 %s — %s"), *Exception.Key, *Exception.Value));
	}

	const TArray<FString> Missing = FindUntested(Units, Tokens, Exceptions());
	for (const FString& Path : Missing)
	{
		AddError(FString::Printf(TEXT("시험이 한 번도 안 부른다: %s — Private/Tests 에 시험을 쓰거나 이유와 함께 예외로"), *Path));
	}
	AddInfo(FString::Printf(TEXT("훑은 소스 %d개 · 시험 파일 %d개 · 낱말 %d개 · 예외 %d개 · 시험 없는 것 %d개"),
		Units.Num(), TestFilesRead, Tokens.Num(), Exceptions().Num(), Missing.Num()));
	TestTrue(TEXT("소스 파일을 훑었다"), Units.Num() > 0);
	TestTrue(TEXT("시험 파일을 읽었다"), TestFilesRead > 0);
	return Missing.Num() == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEveryClassTestedSelfTest,
	"Snarehaven.Code.EveryClassTested.CheckCatchesUntested",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEveryClassTestedSelfTest::RunTest(const FString& Parameters)
{
	using namespace SnareEveryClassTestedTest;

	const TArray<FString> Names = CollectNames(TEXT("FakeWidget"),
		TEXT("class SNAREHAVEN_API UFakeWidget final : public UUserWidget {};\nnamespace SnareFakeRules {}\nclass UFwd;"));
	TestTrue(TEXT("파일 이름을 이름으로 센다"), Names.Contains(TEXT("FakeWidget")));
	TestTrue(TEXT("API 매크로 뒤 클래스"), Names.Contains(TEXT("UFakeWidget")));
	TestTrue(TEXT("이름공간"), Names.Contains(TEXT("SnareFakeRules")));
	TestFalse(TEXT("앞선 선언(class UFwd;)은 안 센다"), Names.Contains(TEXT("UFwd")));

	TSet<FString> Tokens;
	AddTokens(TEXT("#include \"UI/FakeWidget.h\"\nSnareFakeRules::Run(); // FakeWidgetHelper"), Tokens);
	TestTrue(TEXT("include 경로 속 파일 이름도 낱말"), Tokens.Contains(TEXT("FakeWidget")));
	TestTrue(TEXT("머리 붙은 꼴도 같은 이름"), IsMentioned(TEXT("FakeWidget"), { TEXT("UFakeWidget") }));
	TestFalse(TEXT("이름 일부는 안 센다"), IsMentioned(TEXT("FakeWidget"), { TEXT("FakeWidgetHelper") }));

	// 시험 전용 가짜 파일 — 하나는 불리고 하나는 안 불리고 하나는 예외.
	const TArray<FSourceUnit> Units = {
		{ TEXT("UI/FakeWidget.cpp"), { TEXT("FakeWidget") } },
		{ TEXT("World/SnareFakeUntested.cpp"), { TEXT("SnareFakeUntested"), TEXT("USnareFakeUntested") } },
		{ TEXT("UI/FakeTool.cpp"), { TEXT("FakeTool") } },
	};
	const TMap<FString, FString> Allowed = { { TEXT("UI/FakeTool.cpp"), TEXT("시험 예외") } };
	const TArray<FString> Missing = FindUntested(Units, { TEXT("FakeWidget") }, Allowed);
	TestEqual(TEXT("안 불린 가짜 파일 하나만 잡는다"), Missing.Num(), 1);
	TestTrue(TEXT("잡힌 것 = SnareFakeUntested"), Missing.Contains(TEXT("World/SnareFakeUntested.cpp")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
