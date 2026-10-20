// INBOX 결함(감독 2026-10-04 10:40) 2층 검사 — 지역 변수·인자가 **부모 클래스 멤버** 이름을 가리지 않는가.
// 윈도우(MSVC)는 C4458 「declaration hides class member」 를 오류로 세는데(`ShadowVariableWarningLevel` 기본 Error),
// 맥 Clang 의 `-Wshadow` 는 **자기 클래스 필드만** 보고 상속 멤버(`UWidget::Slot`·`ACharacter::Mesh`·`APawn::Controller`)는
// 안 본다 — 그래서 맥 빌드는 통과하고 윈도우 빌드만 68건으로 깨졌다(루프#391 에서 Apple clang 21 로 직접 확인).
// 맥에서 같은 걸 잡으려고 `Source/` 글자를 훑는다: 위젯·액터·폰·컴포넌트 파일에서 그 부모의 멤버 이름으로
// 지역 변수·인자를 선언하면 실패. 이름 목록은 엔진 헤더의 데이터 멤버 중 우리 코드가 쓸 법한 것만 골랐다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Code.ShadowNames; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace SnareShadowNameTest
{
	// 부모 종류별로 가리면 안 되는 멤버 이름 (엔진 5.8 헤더의 데이터 멤버).
	const TArray<FString> WidgetMemberNames = {
		TEXT("Slot"), TEXT("Visibility"), TEXT("RenderOpacity"), TEXT("RenderTransform"), TEXT("ToolTipText"),
		TEXT("Cursor"), TEXT("Navigation"), TEXT("ColorAndOpacity"), TEXT("ForegroundColor"), TEXT("Padding"),
		TEXT("WidgetTree") };
	const TArray<FString> ActorMemberNames = {
		TEXT("Owner"), TEXT("Instigator"), TEXT("RootComponent"), TEXT("Tags"), TEXT("Children"),
		TEXT("InputComponent") };
	const TArray<FString> CharacterMemberNames = {
		TEXT("Controller"), TEXT("PlayerState"), TEXT("Mesh"), TEXT("CharacterMovement"), TEXT("CapsuleComponent") };
	const TArray<FString> ComponentMemberNames = { TEXT("ComponentTags") };

	// 주석·문자열을 같은 길이의 빈칸으로 지운다 — 줄 번호가 안 밀리게 줄바꿈은 남긴다.
	FString StripCommentsAndStrings(const FString& Source)
	{
		FString Out = Source;
		const int32 Len = Out.Len();
		int32 Index = 0;
		while (Index < Len)
		{
			const TCHAR Ch = Out[Index];
			const TCHAR Next = Index + 1 < Len ? Out[Index + 1] : TEXT('\0');
			if (Ch == TEXT('/') && Next == TEXT('/'))
			{
				while (Index < Len && Out[Index] != TEXT('\n'))
				{
					Out[Index++] = TEXT(' ');
				}
			}
			else if (Ch == TEXT('/') && Next == TEXT('*'))
			{
				while (Index < Len && !(Out[Index] == TEXT('*') && Index + 1 < Len && Out[Index + 1] == TEXT('/')))
				{
					if (Out[Index] != TEXT('\n'))
					{
						Out[Index] = TEXT(' ');
					}
					++Index;
				}
				for (int32 Step = 0; Step < 2 && Index < Len; ++Step)
				{
					Out[Index++] = TEXT(' ');
				}
			}
			else if (Ch == TEXT('"') || Ch == TEXT('\''))
			{
				const TCHAR Quote = Ch;
				Out[Index++] = TEXT(' ');
				while (Index < Len && Out[Index] != Quote && Out[Index] != TEXT('\n'))
				{
					if (Out[Index] == TEXT('\\') && Index + 1 < Len)
					{
						Out[Index++] = TEXT(' ');
					}
					Out[Index++] = TEXT(' ');
				}
				if (Index < Len && Out[Index] == Quote)
				{
					Out[Index++] = TEXT(' ');
				}
			}
			else
			{
				++Index;
			}
		}
		return Out;
	}

	// 파일(.cpp 와 짝 헤더 글자)에 적힌 부모 클래스로 가리면 안 되는 이름을 고른다.
	TArray<FString> CollectGuardedNames(const FString& ClassText)
	{
		TSet<FString> Names;
		FRegexMatcher BaseMatcher(FRegexPattern(TEXT(":\\s*public\\s+([A-Z]\\w*)")), ClassText);
		while (BaseMatcher.FindNext())
		{
			const FString Base = BaseMatcher.GetCaptureGroup(1);
			if (Base == TEXT("UUserWidget") || Base == TEXT("UWidget") || Base == TEXT("UTextBlock"))
			{
				Names.Append(WidgetMemberNames);
			}
			else if (Base == TEXT("UActorComponent") || Base == TEXT("USceneComponent"))
			{
				Names.Append(ComponentMemberNames);
			}
			else if (Base.StartsWith(TEXT("A")))
			{
				Names.Append(ActorMemberNames);
				if (Base == TEXT("ACharacter") || Base == TEXT("APawn"))
				{
					Names.Append(CharacterMemberNames);
				}
			}
		}
		return Names.Array();
	}

	// `<형> [const] [*&] 이름` 뒤에 `= ; : , ) { [` 가 오는 선언을 찾는다. `return Slot;` 같은 건 앞 낱말로 거른다.
	TArray<FString> FindShadowDeclarations(const FString& Source, const TArray<FString>& Names)
	{
		TArray<FString> Found;
		if (Names.Num() == 0)
		{
			return Found;
		}

		const FString Code = StripCommentsAndStrings(Source);
		const FString Pattern = FString::Printf(
			TEXT("([A-Za-z_]\\w*)(?:<[^;{}()]*>)?[\\s\\*&]+(?:const[\\s\\*&]+)?\\b(%s)\\s*(?:=|;|:(?!:)|,|\\)|\\{|\\[)"),
			*FString::Join(Names, TEXT("|")));
		const TSet<FString> NotTypes = {
			TEXT("return"), TEXT("case"), TEXT("delete"), TEXT("else"), TEXT("throw"), TEXT("new"),
			TEXT("goto"), TEXT("co_return"), TEXT("sizeof"), TEXT("public"), TEXT("private"), TEXT("protected") };

		FRegexMatcher Matcher(FRegexPattern(Pattern), Code);
		while (Matcher.FindNext())
		{
			const FString TypeWord = Matcher.GetCaptureGroup(1);
			const int32 TypeStart = Matcher.GetCaptureGroupBeginning(1);
			// `Foo->Slot`·`Foo.Slot`·`A::Slot` 는 선언이 아니다 — 형 낱말 바로 앞이 멤버 접근이면 건너뛴다.
			const TCHAR Before = TypeStart > 0 ? Code[TypeStart - 1] : TEXT(' ');
			if (NotTypes.Contains(TypeWord) || Before == TEXT('.') || Before == TEXT('>') || Before == TEXT(':'))
			{
				continue;
			}
			int32 Line = 1;
			for (int32 Index = 0; Index < Matcher.GetCaptureGroupBeginning(2); ++Index)
			{
				Line += Code[Index] == TEXT('\n') ? 1 : 0;
			}
			Found.Add(FString::Printf(TEXT("%d: %s %s"), Line, *TypeWord, *Matcher.GetCaptureGroup(2)));
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShadowNameSourceTest,
	"Snarehaven.Code.ShadowNames.NoInheritedMemberHidden",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShadowNameSourceTest::RunTest(const FString& Parameters)
{
	const FString PrivateDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir() / TEXT("Snarehaven/Private"));
	const FString PublicDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir() / TEXT("Snarehaven/Public"));

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *PrivateDir, TEXT("*.cpp"), true, false);
	if (!TestTrue(TEXT("Private 아래 .cpp 를 하나 이상 찾았다"), Files.Num() > 0))
	{
		return false;
	}

	int32 Guarded = 0;
	int32 Problems = 0;
	for (const FString& File : Files)
	{
		// 2층 검사 파일은 위젯·액터 멤버 함수가 아니다(자동화 테스트 클래스).
		if (File.Contains(TEXT("/Private/Tests/")))
		{
			continue;
		}

		FString Source;
		if (!FFileHelper::LoadFileToString(Source, *File))
		{
			AddError(FString::Printf(TEXT("못 읽었다: %s"), *File));
			continue;
		}

		FString RelativeHeader = File;
		FPaths::MakePathRelativeTo(RelativeHeader, *(PrivateDir + TEXT("/")));
		FString Header;
		FFileHelper::LoadFileToString(Header, *(PublicDir / FPaths::ChangeExtension(RelativeHeader, TEXT("h"))));

		const TArray<FString> Names = SnareShadowNameTest::CollectGuardedNames(Header + TEXT("\n") + Source);
		if (Names.Num() == 0)
		{
			continue;
		}
		++Guarded;

		for (const FString& Hit : SnareShadowNameTest::FindShadowDeclarations(Source, Names))
		{
			++Problems;
			AddError(FString::Printf(TEXT("%s:%s — 부모 멤버 이름을 가린다(MSVC C4458), 쓰임대로 이름을 바꿔라"),
				*FPaths::GetCleanFilename(File), *Hit));
		}
	}

	AddInfo(FString::Printf(TEXT("훑은 파일 %d개 중 부모 멤버 이름을 지키는 파일 %d개, 문제 %d건"), Files.Num(), Guarded, Problems));
	TestTrue(TEXT("위젯·액터·컴포넌트 파일을 하나 이상 훑었다"), Guarded > 0);
	return Problems == 0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenShadowNameCheckSelfTest,
	"Snarehaven.Code.ShadowNames.CheckCatchesShadow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenShadowNameCheckSelfTest::RunTest(const FString& Parameters)
{
	using namespace SnareShadowNameTest;

	TestEqual(TEXT("위젯 파일은 Slot 을 지킨다"),
		CollectGuardedNames(TEXT("class UFoo : public UUserWidget {")).Contains(TEXT("Slot")), true);
	TestEqual(TEXT("캐릭터 파일은 Mesh·Controller 를 지킨다"),
		CollectGuardedNames(TEXT("class AFoo : public ACharacter {")).Contains(TEXT("Mesh"))
			&& CollectGuardedNames(TEXT("class AFoo : public ACharacter {")).Contains(TEXT("Controller")), true);
	TestEqual(TEXT("부모가 없는 파일은 아무 이름도 안 지킨다"),
		CollectGuardedNames(TEXT("struct FFoo { int32 Slot; };")).Num(), 0);

	const TArray<FString> Names = { TEXT("Slot"), TEXT("Mesh"), TEXT("Controller") };
	// 문다 — 윈도우 빌드를 깬 모양 그대로(루프#391 에서 고친 줄들).
	TestEqual(TEXT("if 안 슬롯 포인터"), FindShadowDeclarations(TEXT("if (UCanvasPanelSlot* const Slot = Cast<UCanvasPanelSlot>(X))"), Names).Num(), 1);
	TestEqual(TEXT("범위 for 참조"), FindShadowDeclarations(TEXT("for (const FInventorySlot& Slot : Items)"), Names).Num(), 1);
	TestEqual(TEXT("함수 인자"), FindShadowDeclarations(TEXT("FText UFoo::Get(int32 Slot) const"), Names).Num(), 1);
	TestEqual(TEXT("람다 인자"), FindShadowDeclarations(TEXT("Items.RemoveAll([](const FInventorySlot& Slot) { return true; });"), Names).Num(), 1);
	TestEqual(TEXT("지역 메시"), FindShadowDeclarations(TEXT("USkeletalMesh* Mesh = Row.MeshRef.LoadSynchronous();"), Names).Num(), 1);
	TestEqual(TEXT("템플릿 형"), FindShadowDeclarations(TEXT("TObjectPtr<APlayerController> Controller;"), Names).Num(), 1);
	// 안 문다 — 쓰는 자리·이름 일부·주석·문자열.
	TestEqual(TEXT("return 은 선언이 아니다"), FindShadowDeclarations(TEXT("return Slot;"), Names).Num(), 0);
	TestEqual(TEXT("멤버 접근은 선언이 아니다"), FindShadowDeclarations(TEXT("Widget->Slot = nullptr; Body.Mesh;"), Names).Num(), 0);
	TestEqual(TEXT("이름 일부는 안 문다"), FindShadowDeclarations(TEXT("int32 SlotIndex = 0; UPanelSlot* PanelSlot = GetSlot();"), Names).Num(), 0);
	TestEqual(TEXT("주석은 안 문다"), FindShadowDeclarations(TEXT("// int32 Slot = 0;\n/* UMesh* Mesh; */"), Names).Num(), 0);
	TestEqual(TEXT("문자열은 안 문다"), FindShadowDeclarations(TEXT("Log(TEXT(\"bad Slot)\"));"), Names).Num(), 0);
	TestEqual(TEXT("줄 번호를 센다"), FindShadowDeclarations(TEXT("\n\nint32 Slot = 1;"), Names)[0], FString(TEXT("3: int32 Slot")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
