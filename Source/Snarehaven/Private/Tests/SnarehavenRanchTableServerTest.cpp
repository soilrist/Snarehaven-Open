// 2층 맞대기 — 목장 생산물 표(`DT_RanchProductTable`)의 (동물 → 생산물) 쌍이 서버 `ANIMAL_PRODUCT_ITEM` 과 같은가.
// 수확 지급은 서버가 판정한다(CLAUDE.md 8). 두 표가 어긋나면 화면에 보인 생산물과 실제 지급이 달라지고,
// 서버에 없는 동물은 수확 자체가 거절된다 — 그래서 올라갈 js 파일을 **그대로 읽어** 비교한다(가챠 풀 맞대기와 같은 짜임).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/RanchProductRow.h"
#include "Engine/DataTable.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* RanchTablePath = TEXT("/Game/Game/Data/Ranch/DT_RanchProductTable.DT_RanchProductTable");

	/** `var ANIMAL_PRODUCT_ITEM = { "animal.pig": "item.truffle", … };` 블록 안의 쌍만 꺼낸다. */
	TMap<FString, FString> ReadServerRanchTable(const FString& Script)
	{
		TMap<FString, FString> Pairs;
		const int32 Start = Script.Find(TEXT("var ANIMAL_PRODUCT_ITEM = {"), ESearchCase::CaseSensitive);
		const int32 End = Start == INDEX_NONE ? INDEX_NONE
			: Script.Find(TEXT("};"), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start);
		if (End == INDEX_NONE)
		{
			return Pairs;
		}

		const FRegexPattern Pattern(TEXT("\"(animal\\.[a-z0-9_]+)\"\\s*:\\s*\"(item\\.[a-z0-9_]+)\""));
		FRegexMatcher Matcher(Pattern, Script.Mid(Start, End - Start));
		while (Matcher.FindNext())
		{
			Pairs.Add(Matcher.GetCaptureGroup(1), Matcher.GetCaptureGroup(2));
		}
		return Pairs;
	}

	/** 양방향으로 센다 — 한쪽에만 있는 동물도, 생산물이 다른 동물도 불일치다. */
	int32 CountMismatches(const TMap<FString, FString>& Client, const TMap<FString, FString>& Server)
	{
		int32 Bad = Client.Num() == Server.Num() ? 0 : 1;
		for (const TPair<FString, FString>& Pair : Client)
		{
			const FString* const ServerItem = Server.Find(Pair.Key);
			Bad += (ServerItem == nullptr || *ServerItem != Pair.Value) ? 1 : 0;
		}
		return Bad;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchTableServerTest, "Snarehaven.Ranch.Table.MatchesServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchTableServerTest::RunTest(const FString& Parameters)
{
	const UDataTable* const RanchTable = LoadObject<UDataTable>(nullptr, RanchTablePath);
	FString Script;
	if (!TestNotNull(TEXT("DT_RanchProductTable"), RanchTable)
		|| !TestTrue(TEXT("CloudScript 읽기"), FFileHelper::LoadFileToString(Script, *(FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js")))))
	{
		return false;
	}

	TMap<FString, FString> Client;
	RanchTable->ForeachRow<FRanchProductRow>(TEXT("RanchTableServerTest"), [&Client](const FName&, const FRanchProductRow& Row)
	{
		Client.Add(Row.AnimalID.ToString(), Row.ProducedItemID.ToString());
	});
	const TMap<FString, FString> Server = ReadServerRanchTable(Script);

	// 한쪽이 0줄이면 "비교할 게 없어 통과" 가 된다 — 그게 제일 위험하다.
	TestTrue(TEXT("클라 표를 읽었다"), Client.Num() > 0);
	TestTrue(TEXT("서버 표를 읽었다"), Server.Num() > 0);
	const int32 Bad = CountMismatches(Client, Server);
	TestEqual(TEXT("DT_RanchProductTable = 서버 ANIMAL_PRODUCT_ITEM (동물 → 생산물)"), Bad, 0);

	// 반대 방향 — 값 하나를 바꾸거나 한 줄을 빼면 맞대기가 운다(항상 0 을 내는 비교가 아니다).
	TMap<FString, FString> Tampered = Server;
	TMap<FString, FString> Shorter = Server;
	if (Server.Num() > 0)
	{
		const FString FirstAnimal = Server.CreateConstIterator().Key();
		Tampered[FirstAnimal] += TEXT("_x");
		Shorter.Remove(FirstAnimal);
	}
	TestTrue(TEXT("생산물 하나 바꾸면 불일치"), CountMismatches(Client, Tampered) > 0);
	TestTrue(TEXT("한 줄 빼면 불일치"), CountMismatches(Client, Shorter) > 0);
	AddInfo(FString::Printf(TEXT("[RanchTable] 클라 %d줄 · 서버 %d줄 · 불일치 %d"), Client.Num(), Server.Num(), Bad));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
