// ROADMAP M66-01·02 — 클라가 부르는 서버 함수 이름이 `cloudscript_main.js` 의 `handlers.` 에 다 있다.
// 한 글자 오타는 컴파일도 서버 시험도 통과하고 폰에서만 「함수 없음」 으로 조용히 실패한다.
// ROADMAP M67-01·02 — 클라가 응답에서 읽는 칸 이름(`GetStringField(TEXT("칸"))` 등)이 서버 글자에 낱말로 다 있다. 칸 이름 오타는 폰에서 「+0」·빈 칸으로 조용히 실패한다.
// ROADMAP M68-02 — 클라 소스 글자 `TEXT("item.…")` 이름이 DT_ItemTable 행에 다 있다. 접두사(`_` 로 끝남)와 시험 폴더는 뺀다.
// 읽는 것은 소스 글자뿐 — 서버 호출·인증·재화 판정은 건드리지 않는다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.Backend.ServerNames; Quit" -unattended -nullrhi
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/ItemRow.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* const ServerNamesCloudScript = TEXT("tools/playfab/cloudscript_main.js");
	const TCHAR* const ServerNamesClientDir = TEXT("Source/Snarehaven/Private");

	/** 글자에서 첫 묶음(괄호 1)을 전부 모은다. */
	TSet<FString> CollectGroup(const FString& Source, const TCHAR* Pattern)
	{
		TSet<FString> Names;
		const FRegexPattern Regex(Pattern);
		FRegexMatcher Matcher(Regex, Source);
		while (Matcher.FindNext())
		{
			Names.Add(Matcher.GetCaptureGroup(1));
		}
		return Names;
	}

	/** 클라 소스 글자에서 부르는 이름: `CallServerFunction(TEXT("이름")` 과 `Get…FunctionName() { return TEXT("이름"); }`. */
	TSet<FString> CollectClientCalls(const FString& Source)
	{
		TSet<FString> Names = CollectGroup(Source, TEXT("CallServerFunction\\(\\s*TEXT\\(\"(\\w+)\"\\)"));
		Names.Append(CollectGroup(Source, TEXT("::Get\\w+FunctionName\\(\\)\\s*\\{\\s*return TEXT\\(\"(\\w+)\"\\)")));
		return Names;
	}

	/** 서버 글자에서 `handlers.이름 =` 을 모은다. */
	TSet<FString> CollectServerHandlers(const FString& Source)
	{
		return CollectGroup(Source, TEXT("(?m)^handlers\\.(\\w+)\\s*="));
	}

	/** 클라 소스 글자에서 응답 칸 이름: `Get…Field(TEXT("칸")` / `TryGet…Field(TEXT("칸")`. */
	TSet<FString> CollectClientFields(const FString& Source)
	{
		return CollectGroup(Source, TEXT("(?:Get|TryGet)\\w*Field\\(\\s*TEXT\\(\"(\\w+)\"\\)"));
	}

	/** 서버 글자의 모든 낱말 — 응답 칸 이름이 서버 어디에도 없으면 오타다. */
	TSet<FString> CollectServerWords(const FString& Source)
	{
		return CollectGroup(Source, TEXT("(\\w+)"));
	}

	/** 클라 이름 중 서버에 없는 것 — 한 줄씩. */
	TArray<FString> SurveyMissingOnServer(const TSet<FString>& Client, const TSet<FString>& Server)
	{
		TArray<FString> Missing;
		for (const FString& Name : Client)
		{
			if (!Server.Contains(Name))
			{
				Missing.Add(Name);
			}
		}
		Missing.Sort();
		return Missing;
	}

	/** 클라 소스 글자에서 아이템 이름: `TEXT("item.이름")`. `_` 로 끝나는 접두사(`item.seed_` 등)는 이름이 아니라 뺀다. */
	TSet<FString> CollectClientItemIds(const FString& Source)
	{
		TSet<FString> Names = CollectGroup(Source, TEXT("TEXT\\(\"(item\\.\\w+)\"\\)"));
		for (auto It = Names.CreateIterator(); It; ++It)
		{
			if (It->EndsWith(TEXT("_")))
			{
				It.RemoveCurrent();
			}
		}
		return Names;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenServerNamesCollectTest, "Snarehaven.Backend.ServerNames.Collect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenServerNamesCollectTest::RunTest(const FString& Parameters)
{
	const FString FakeClient = TEXT(
		"Backend->CallServerFunction(TEXT(\"AlphaCall\"), Args,\n"
		"FString UFoo::GetBetaFunctionName()\n{\n\treturn TEXT(\"BetaCall\");\n}\n"
		"// CallServerFunction(Name, Args) 는 글자가 아니라 건너뜀\n");
	const TSet<FString> Client = CollectClientCalls(FakeClient);
	TestTrue(TEXT("가짜 클라 글자: 글자 호출 + 이름 읽기 함수 둘을 모음"), Client.Num() == 2 && Client.Contains(TEXT("AlphaCall")) && Client.Contains(TEXT("BetaCall")));

	const FString FakeServer = TEXT("handlers.AlphaCall = function (args, context) {\n}\n  // handlers.Indented = 1\nhandlers.BetaCall = function () {}\n");
	const TSet<FString> Server = CollectServerHandlers(FakeServer);
	TestTrue(TEXT("가짜 서버 글자: 줄 머리 `handlers.` 둘만 모음"), Server.Num() == 2 && Server.Contains(TEXT("AlphaCall")) && Server.Contains(TEXT("BetaCall")));

	TestEqual(TEXT("모두 있으면 빠진 것 0"), SurveyMissingOnServer(Client, Server).Num(), 0);
	TSet<FString> Typo = Client;
	Typo.Add(TEXT("AlphaCal"));
	const TArray<FString> Missing = SurveyMissingOnServer(Typo, Server);
	TestTrue(TEXT("한 글자 오타 → 빠진 것 1"), Missing.Num() == 1 && Missing[0] == TEXT("AlphaCal"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenResponseFieldsCollectTest, "Snarehaven.Backend.ResponseFields.Collect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenResponseFieldsCollectTest::RunTest(const FString& Parameters)
{
	const FString FakeClient = TEXT(
		"Obj->GetStringField(TEXT(\"alpha\"));\n"
		"Obj->TryGetNumberField(TEXT(\"beta\"), V);\n"
		"Obj->TryGetObjectField( TEXT(\"gamma\"), O);\n"
		"Obj->SetStringField(TEXT(\"zeta\"), X); // 쓰기는 읽기가 아님\n"
		"Obj->GetStringField(Name); // 글자 아닌 이름은 건너뜀\n");
	const TSet<FString> Client = CollectClientFields(FakeClient);
	TestTrue(TEXT("가짜 클라 글자: 읽기 셋만 모음"), Client.Num() == 3 && Client.Contains(TEXT("alpha")) && Client.Contains(TEXT("beta")) && Client.Contains(TEXT("gamma")));

	const TSet<FString> Server = CollectServerWords(TEXT("return { alpha: 1, beta: x.gamma };"));
	TestEqual(TEXT("모두 있으면 빠진 것 0"), SurveyMissingOnServer(Client, Server).Num(), 0);
	TSet<FString> Typo = Client;
	Typo.Add(TEXT("alpah"));
	const TArray<FString> Missing = SurveyMissingOnServer(Typo, Server);
	TestTrue(TEXT("한 글자 오타 → 빠진 것 1"), Missing.Num() == 1 && Missing[0] == TEXT("alpah"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenResponseFieldsClientReadsExistTest, "Snarehaven.Backend.ResponseFields.ClientReadsExist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenResponseFieldsClientReadsExistTest::RunTest(const FString& Parameters)
{
	FString ServerSource;
	if (!TestTrue(TEXT("서버 파일을 읽음"), FFileHelper::LoadFileToString(ServerSource, *(FPaths::ProjectDir() / ServerNamesCloudScript))))
	{
		return false;
	}
	const TSet<FString> Server = CollectServerWords(ServerSource);

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectDir() / ServerNamesClientDir), TEXT("*.cpp"), true, false);
	TSet<FString> Client;
	for (const FString& File : Files)
	{
		if (File.Contains(TEXT("/Tests/")))
		{
			continue; // 시험 글자 속 이름은 클라 읽기가 아니다.
		}
		FString Source;
		if (FFileHelper::LoadFileToString(Source, *File))
		{
			Client.Append(CollectClientFields(Source));
		}
	}

	AddInfo(FString::Printf(TEXT("[Backend] 서버 낱말 %d · 클라가 읽는 칸 이름 %d"), Server.Num(), Client.Num()));
	TestTrue(TEXT("서버 낱말을 모았다(0 아님)"), Server.Num() > 0);
	TestTrue(TEXT("클라 칸 이름을 모았다(0 아님)"), Client.Num() > 0);
	for (const FString& Name : SurveyMissingOnServer(Client, Server))
	{
		AddError(FString::Printf(TEXT("클라가 응답 칸 `%s` 를 읽는데 서버 글자에 그 낱말이 없다 — 폰에서 조용히 빈 값"), *Name));
	}

	// 역검증 — 파일은 그대로, 서버 글자 사본에서 낱말 하나를 깬다.
	if (Client.Num() > 0)
	{
		const FString Victim = *Client.CreateConstIterator();
		const FString Out = ServerSource.Replace(*Victim, *(Victim + TEXT("X")));
		const TArray<FString> Missing = SurveyMissingOnServer(Client, CollectServerWords(Out));
		AddInfo(FString::Printf(TEXT("[Backend] 역검증 대상 %s · 빠진 것 %d"), *Victim, Missing.Num()));
		TestTrue(TEXT("역검증: 서버 낱말을 깬 사본 → 그 이름 하나를 잡음"), Missing.Num() == 1 && Missing[0] == Victim);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenServerNamesClientCallsExistTest, "Snarehaven.Backend.ServerNames.ClientCallsExist",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenServerNamesClientCallsExistTest::RunTest(const FString& Parameters)
{
	FString ServerSource;
	if (!TestTrue(TEXT("서버 파일을 읽음"), FFileHelper::LoadFileToString(ServerSource, *(FPaths::ProjectDir() / ServerNamesCloudScript))))
	{
		return false;
	}
	const TSet<FString> Server = CollectServerHandlers(ServerSource);

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectDir() / ServerNamesClientDir), TEXT("*.cpp"), true, false);
	TSet<FString> Client;
	for (const FString& File : Files)
	{
		if (File.Contains(TEXT("/Tests/")))
		{
			continue; // 시험 글자 속 이름은 클라 호출이 아니다.
		}
		FString Source;
		if (FFileHelper::LoadFileToString(Source, *File))
		{
			Client.Append(CollectClientCalls(Source));
		}
	}

	AddInfo(FString::Printf(TEXT("[Backend] 서버 핸들러 %d · 클라가 부르는 이름 %d"), Server.Num(), Client.Num()));
	// 훑기가 빈 채로 통과하지 못하게 — 모은 수가 0 이면 정규식·경로가 깨진 것.
	TestTrue(TEXT("서버 핸들러를 모았다(0 아님)"), Server.Num() > 0);
	TestTrue(TEXT("클라 호출 이름을 모았다(0 아님)"), Client.Num() > 0);
	for (const FString& Name : SurveyMissingOnServer(Client, Server))
	{
		AddError(FString::Printf(TEXT("클라가 `%s` 를 부르는데 서버 `handlers.%s` 가 없다 — 폰에서 조용히 실패"), *Name, *Name));
	}

	// 역검증 (Verification.md 13) — 파일은 그대로, 서버 글자 사본에서 핸들러 한 줄 이름을 바꿔 잡히는지 본다.
	if (Client.Num() > 0)
	{
		const FString Victim = *Client.CreateConstIterator();
		const FString Broken = ServerSource.Replace(*FString::Printf(TEXT("\nhandlers.%s ="), *Victim), *FString::Printf(TEXT("\nhandlers.%sX ="), *Victim));
		const TArray<FString> Missing = SurveyMissingOnServer(Client, CollectServerHandlers(Broken));
		TestTrue(TEXT("역검증: 서버 핸들러 이름을 깬 사본 → 그 이름 하나를 잡음"), Missing.Num() == 1 && Missing[0] == Victim);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenServerNamesClientItemIdsInTableTest, "Snarehaven.Backend.ItemIds.ClientLiteralsInTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenServerNamesClientItemIdsInTableTest::RunTest(const FString& Parameters)
{
	const UDataTable* const ItemTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Items/DT_ItemTable.DT_ItemTable"));
	if (!TestNotNull(TEXT("DT_ItemTable"), ItemTable))
	{
		return false;
	}
	TSet<FString> Table;
	for (const TPair<FName, uint8*>& Pair : ItemTable->GetRowMap())
	{
		Table.Add(reinterpret_cast<const FItemRow*>(Pair.Value)->ItemID.ToString());
	}

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *(FPaths::ProjectDir() / ServerNamesClientDir), TEXT("*.cpp"), true, false);
	TSet<FString> Client;
	for (const FString& File : Files)
	{
		if (File.Contains(TEXT("/Tests/")))
		{
			continue; // 시험 글자 속 가짜 이름은 클라 코드가 아니다.
		}
		FString Source;
		if (FFileHelper::LoadFileToString(Source, *File))
		{
			Client.Append(CollectClientItemIds(Source));
		}
	}

	AddInfo(FString::Printf(TEXT("[Backend] 아이템 표 행 %d · 클라 소스 아이템 이름 %d"), Table.Num(), Client.Num()));
	TestTrue(TEXT("아이템 표 행을 모았다(0 아님)"), Table.Num() > 0);
	TestTrue(TEXT("클라 아이템 이름을 모았다(0 아님)"), Client.Num() > 0);
	for (const FString& Name : SurveyMissingOnServer(Client, Table))
	{
		AddError(FString::Printf(TEXT("클라 소스가 `%s` 를 쓰는데 아이템 표에 그 행이 없다 — 폰에서 아이콘·이름 빈 칸"), *Name));
	}

	// 역검증 — 표 사본에서 이름 하나를 뺀다.
	if (Client.Num() > 0)
	{
		const FString Victim = *Client.CreateConstIterator();
		TSet<FString> Broken = Table;
		Broken.Remove(Victim);
		const TArray<FString> Missing = SurveyMissingOnServer(Client, Broken);
		TestTrue(TEXT("역검증: 표에서 이름을 뺀 사본 → 그 이름 하나를 잡음"), Missing.Num() == 1 && Missing[0] == Victim);
	}
	return true;
}

#endif
