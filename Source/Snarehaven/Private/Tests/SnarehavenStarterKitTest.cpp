// ROADMAP M9-07b 2층 검사 — 시작 장비(몽둥이)가 **백엔드 응답 뒤에만** 손에 걸리는지.
// 실제 PlayFab 왕복은 여기서 하지 않는다(네트워크가 있어야 하는 검사는 3층이다) — 대신 CloudScript
// `GrantStarterKit` 이 돌려주는 **모양 그대로의 JSON** 을 읽는 자리에 넣어 그 길을 통째로 돌린다.
// 이 검사가 잡는 것: ① 응답 전에 캐시·장착이 바뀌는 사고 ② 이미 받은 계정에 또 주는 사고
// ③ 지급을 묻는 자리가 둘로 갈리는 것 ④ 부를 CloudScript 이름이 올라갈 파일과 어긋나는 것
// ⑤ 지급 아이템 ID 가 DESIGN 3.5 표와 어긋나거나 그 ID 로 들 장비 행이 표에 없는 것.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Engine/GameInstance.h"
#include "Equipment/EquipmentComponent.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	/** 게임 인스턴스 서브시스템은 `ClassWithin` 이 `UGameInstance` 라 주인을 세워 줘야 만들어진다. */
	USnareBackendSubsystem* MakeTestBackend()
	{
		UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
		return NewObject<USnareBackendSubsystem>(GameInstance);
	}

	/**
	 * **검사 전용 값** (Verification.md 6) — 설계서의 `item.club` 을 쓰지 않는다. 지급 목록이 바뀌어도
	 * "응답이 준 ID 를 그대로 들고 온다" 는 규칙은 안 바뀌어야 하기 때문이다.
	 */
	const TCHAR* const TestGrantedItemId = TEXT("item.test_starter");
	const TCHAR* const TestOwnerId = TEXT("test-playfab-id");

	/** 핸들러가 돌려주는 모양 그대로 (tools/playfab/cloudscript_main.js `handlers.GrantStarterKit`). */
	FString MakeGrantedJson()
	{
		return FString::Printf(
			TEXT("{\"granted\":true,\"alreadyGranted\":false,\"grantedAtUtc\":\"2026-09-23T00:00:00.000Z\",")
			TEXT("\"items\":[{\"itemId\":\"%s\",\"count\":1}]}"), TestGrantedItemId);
	}

	FString MakeAlreadyGrantedJson()
	{
		return FString::Printf(
			TEXT("{\"granted\":false,\"alreadyGranted\":true,\"grantedAtUtc\":\"2026-09-23T00:00:00.000Z\",")
			TEXT("\"items\":[{\"itemId\":\"%s\",\"count\":1}]}"), TestGrantedItemId);
	}

	FSnarePlayerSnapshot MakeTestSnapshot()
	{
		FSnarePlayerSnapshot Snapshot;
		Snapshot.PlayFabId = TestOwnerId;
		Snapshot.FetchedAtUtc = FDateTime::UtcNow();
		return Snapshot;
	}

	TArray<TPair<FString, FString>> LoadSourceFiles(const FString& SourceDir)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *SourceDir, TEXT("*.cpp"), true, false);
		IFileManager::Get().FindFilesRecursive(Files, *SourceDir, TEXT("*.h"), true, false, false);

		TArray<TPair<FString, FString>> Loaded;
		for (const FString& File : Files)
		{
			FString Text;
			if (FFileHelper::LoadFileToString(Text, *File))
			{
				Loaded.Emplace(File, MoveTemp(Text));
			}
		}
		return Loaded;
	}

	/**
	 * 지급을 **묻는** 자리의 글자. 이름을 쪼개 이어 붙이는 이유는 이 검사가 `Source/` 아래 글자를
	 * 훑기 때문이다 — 통째로 적어 두면 제 소스에서 그 글자를 세어 자기 자신을 부르는 자리로 본다
	 * (루프#110 함정, 분석 통로 검사가 겪은 그대로).
	 */
	FString RequestCallNeedle()
	{
		return FString(TEXT("RequestStarter")) + TEXT("Kit()");
	}

	/** 선언(`void …;`)·정의(`USnareBackendSubsystem::`)를 뺀 **부르는 자리**만 센다. */
	int32 CountRequestCallSites(const FString& Text)
	{
		const FString Needle = RequestCallNeedle();
		int32 Count = 0;
		int32 At = Text.Find(Needle);
		while (At != INDEX_NONE)
		{
			const int32 LineStart = Text.Find(TEXT("\n"), ESearchCase::IgnoreCase, ESearchDir::FromEnd, At);
			const FString Line = Text.Mid(LineStart + 1, At - LineStart - 1);
			if (!Line.Contains(TEXT("USnareBackendSubsystem::")) && !Line.Contains(TEXT("void ")))
			{
				++Count;
			}
			At = Text.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, At + Needle.Len());
		}
		return Count;
	}

	/** DESIGN 3.5 「시작 장비 지급」 표의 첫 칸에서 지급 아이템 ID 를 읽는다. 못 읽으면 빈 값. */
	FString ReadStarterItemIdFromDesign()
	{
		FString Design;
		if (!FFileHelper::LoadFileToString(Design, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md"))))
		{
			return FString();
		}

		// 표 한 줄: `| 시작 지급 아이템 | `item.club` × 1 | … |`
		const FRegexPattern Pattern(TEXT("\\|\\s*시작 지급 아이템\\s*\\|\\s*`([^`]+)`"));
		FRegexMatcher Matcher(Pattern, Design);
		return Matcher.FindNext() ? Matcher.GetCaptureGroup(1) : FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStarterKitFunctionNameTest,
	"Snarehaven.Backend.StarterKit.FunctionNameMatchesCloudScript",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStarterKitFunctionNameTest::RunTest(const FString& Parameters)
{
	const FString ScriptPath = FPaths::ProjectDir() / TEXT("tools/playfab/cloudscript_main.js");
	FString Script;
	if (!TestTrue(*FString::Printf(TEXT("올라갈 CloudScript 파일을 읽는다 (%s)"), *ScriptPath),
		FFileHelper::LoadFileToString(Script, *ScriptPath)))
	{
		// 파일을 못 읽으면 "이름이 같다" 를 확인할 길이 없다 — 조용히 통과시키지 않는다.
		return false;
	}

	const FString FunctionName = USnareBackendSubsystem::GetStarterKitFunctionName();
	const FString Handler = FString::Printf(TEXT("handlers.%s"), *FunctionName);
	TestTrue(*FString::Printf(TEXT("올라갈 파일에 핸들러 '%s' 가 있다"), *Handler), Script.Contains(Handler));
	AddInfo(FString::Printf(TEXT("부를 CloudScript 함수: %s (%d글자 파일에서 확인)"),
		*FunctionName, Script.Len()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStarterKitSingleCallSiteTest,
	"Snarehaven.Backend.StarterKit.AsksOnlyFromOnePlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStarterKitSingleCallSiteTest::RunTest(const FString& Parameters)
{
	const TArray<TPair<FString, FString>> Sources = LoadSourceFiles(FPaths::ProjectDir() / TEXT("Source"));
	if (!TestTrue(TEXT("Source/ 아래 소스 파일을 하나 이상 읽었다"), Sources.Num() > 0))
	{
		// 한 파일도 못 읽으면 "부르는 자리 0개" 로 조용히 통과한다 — 그게 제일 위험하다.
		return false;
	}

	int32 TotalCallSites = 0;
	TArray<FString> FilesWithCallSite;
	for (const TPair<FString, FString>& Source : Sources)
	{
		const int32 CallSites = CountRequestCallSites(Source.Value);
		if (CallSites > 0)
		{
			TotalCallSites += CallSites;
			FilesWithCallSite.Add(FPaths::GetCleanFilename(Source.Key));
		}
	}

	// 묻는 자리가 둘이면 같은 계정에 왕복이 두 번 뜬다 — 백엔드가 막아 주긴 하지만 그건 우연이다.
	TestEqual(*FString::Printf(TEXT("지급을 묻는 자리가 하나뿐이다 (찾은 곳: %s)"),
		FilesWithCallSite.Num() > 0 ? *FString::Join(FilesWithCallSite, TEXT(", ")) : TEXT("없음")),
		TotalCallSites, 1);
	TestEqual(TEXT("그 자리는 SnareBackendSubsystem.cpp 다"),
		FString::Join(FilesWithCallSite, TEXT(", ")), FString(TEXT("SnareBackendSubsystem.cpp")));
	AddInfo(FString::Printf(TEXT("소스 %d개를 훑어 부르는 자리 %d개"), Sources.Num(), TotalCallSites));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStarterKitWaitsForSnapshotTest,
	"Snarehaven.Backend.StarterKit.GrantedWaitsForBackendSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStarterKitWaitsForSnapshotTest::RunTest(const FString& Parameters)
{
	USnareBackendSubsystem* const Backend = MakeTestBackend();

	// ① 응답 전 — 아무것도 받은 것이 없고 걸 것도 없다.
	TestEqual(TEXT("응답 전에는 왕복이 시작 전이다"),
		static_cast<int32>(Backend->GetStarterKitState()),
		static_cast<int32>(ESnareBackendStarterKitState::NotStarted));
	TestEqual(TEXT("응답 전에는 받은 아이템이 0개다"), Backend->GetStarterKitGrantedItems().Num(), 0);
	TestFalse(TEXT("응답 전에는 장착 대기도 없다"), Backend->IsStarterKitEquipPending());

	// ② 받았다는 응답 — 상태와 목록은 바뀌지만 **캐시는 안 바뀐다**(개수를 클라이언트가 만들지 않는다).
	TestTrue(TEXT("지급 응답을 읽었다"), Backend->ApplyStarterKitResponseJson(MakeGrantedJson()));
	TestEqual(TEXT("응답을 받았다고 표시된다"),
		static_cast<int32>(Backend->GetStarterKitState()),
		static_cast<int32>(ESnareBackendStarterKitState::Answered));
	if (TestEqual(TEXT("받은 아이템이 한 종이다"), Backend->GetStarterKitGrantedItems().Num(), 1))
	{
		TestEqual(TEXT("받은 아이템 ID 는 응답이 말한 그것이다"),
			Backend->GetStarterKitGrantedItems()[0], FName(TestGrantedItemId));
	}
	TestEqual(TEXT("응답만으로는 인벤토리 캐시가 늘지 않는다"),
		Backend->GetPlayerSnapshot().InventorySlots.Num(), 0);
	TestTrue(TEXT("보유 상태가 오기 전까지는 장착을 미룬다"), Backend->IsStarterKitEquipPending());

	// ③ 로컬 캐시를 올린 것으로는 안 풀린다 — 지난번 사본에 방금 받은 것이 있을 리 없다.
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot(), /*bFromBackend=*/false);
	TestTrue(TEXT("로컬 캐시로는 장착 신호가 안 나간다"), Backend->IsStarterKitEquipPending());

	// ④ 백엔드가 확인해 준 보유 상태가 오면 그때 장착 신호가 나간다 (DESIGN 3.5 「받은 뒤」).
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot(), /*bFromBackend=*/true);
	TestFalse(TEXT("백엔드 스냅샷이 오면 장착 신호가 나간다"), Backend->IsStarterKitEquipPending());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStarterKitAlreadyGrantedTest,
	"Snarehaven.Backend.StarterKit.AlreadyGrantedChangesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStarterKitAlreadyGrantedTest::RunTest(const FString& Parameters)
{
	USnareBackendSubsystem* const Backend = MakeTestBackend();
	Backend->ApplyPlayerSnapshot(MakeTestSnapshot(), /*bFromBackend=*/true);

	TestTrue(TEXT("이미 받은 계정의 응답을 읽었다"),
		Backend->ApplyStarterKitResponseJson(MakeAlreadyGrantedJson()));

	// 판정은 백엔드가 했다 — `items` 가 응답에 실려 와도 **받은 것이 아니다**.
	TestEqual(TEXT("두 번째 접속에는 받은 아이템이 0개다"), Backend->GetStarterKitGrantedItems().Num(), 0);
	TestFalse(TEXT("장착 신호를 기다리지 않는다"), Backend->IsStarterKitEquipPending());
	TestEqual(TEXT("그래도 답은 받은 상태다"),
		static_cast<int32>(Backend->GetStarterKitState()),
		static_cast<int32>(ESnareBackendStarterKitState::Answered));
	TestEqual(TEXT("인벤토리 캐시도 그대로다"), Backend->GetPlayerSnapshot().InventorySlots.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStarterKitBadResponseTest,
	"Snarehaven.Backend.StarterKit.BrokenResponseGrantsNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStarterKitBadResponseTest::RunTest(const FString& Parameters)
{
	USnareBackendSubsystem* const Backend = MakeTestBackend();

	// 스크립트가 터지거나 응답이 잘린 경우다 — 그때 "받았다" 로 넘어가면 맨손인 채 표시만 찍힌다.
	TestFalse(TEXT("JSON 이 아니면 반영하지 않는다"),
		Backend->ApplyStarterKitResponseJson(TEXT("<html>error</html>")));
	TestEqual(TEXT("왕복이 실패로 남는다"),
		static_cast<int32>(Backend->GetStarterKitState()),
		static_cast<int32>(ESnareBackendStarterKitState::Failed));
	TestEqual(TEXT("받은 아이템이 0개다"), Backend->GetStarterKitGrantedItems().Num(), 0);
	TestFalse(TEXT("장착 대기도 없다"), Backend->IsStarterKitEquipPending());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenStarterKitDesignRowTest,
	"Snarehaven.Equip.StarterKit.DesignItemIsEquipmentRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenStarterKitDesignRowTest::RunTest(const FString& Parameters)
{
	// 기대값을 검사가 들고 있지 않다 — DESIGN 3.5 표에서 읽어 온다. 표를 못 읽으면 그 자체로 실패다.
	const FString StarterItemId = ReadStarterItemIdFromDesign();
	if (!TestTrue(TEXT("DESIGN 3.5 「시작 장비 지급」 표에서 지급 아이템 ID 를 읽었다"),
		!StarterItemId.IsEmpty()))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("설계서가 말하는 시작 지급 아이템: %s"), *StarterItemId));

	UEquipmentComponent* const Equipment = NewObject<UEquipmentComponent>();
	const FName RowName = Equipment->FindEquipmentRowByItemID(FName(*StarterItemId));

	// 표에 장비 행이 없으면 응답이 와도 손에 아무것도 안 걸린다 — 지급이 조용히 헛돈다.
	TestFalse(*FString::Printf(TEXT("'%s' 로 들 장비 행이 DT_ItemTable 에 있다"), *StarterItemId),
		RowName.IsNone());
	AddInfo(FString::Printf(TEXT("%s -> 행 %s"), *StarterItemId, *RowName.ToString()));

	// 없는 ID·장비가 아닌 ID 로는 아무것도 안 든다 (지급 목록에 재료가 섞여도 배낭으로 간다).
	TestTrue(TEXT("없는 아이템 ID 로는 들 행이 없다"),
		Equipment->FindEquipmentRowByItemID(TEXT("item.no_such_item")).IsNone());
	TestFalse(TEXT("없는 아이템 ID 로는 들지도 않는다"),
		Equipment->EquipItemByItemID(TEXT("item.no_such_item")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
