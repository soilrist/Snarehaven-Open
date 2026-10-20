// DESIGN 8.12 "지급 대기열 보내기" 2층 검사 — 대기열이 `ClaimLoot` 로 나가고, 서버가 받아 준 뒤에만 빠지는가.
// 가짜 서버(`USnareBackendSubsystem::TestServerOverride`)를 꽂아 진짜 응답과 같은 길을 태운다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.LootClaim; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Data/GatherableRow.h"
#include "Data/ItemRowLookup.h"
#include "Engine/GameInstance.h"
#include "Inventory/InventoryComponent.h"
#include "Inventory/LootClaimComponent.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다). ID 는 실제 표에 있는 것을 쓴다(허용 판정이 표를 본다).
	const FName BerryID = TEXT("item.berry");
	const FName OldDropID = TEXT("item.pig_meat");       // 옛 사냥 드롭 아이템 — 이제 아이템으로는 안 나간다
	const FName FoxID = TEXT("animal.fox");
	const FName WolfID = TEXT("animal.wolf");
	constexpr int32 TestFoxKills = 2;
	constexpr int32 TestOverflowKills = 7;
	constexpr int32 TestDropsPerKill = 3; // 컴포넌트 기본값(서버 KILL_DROP_MAX)과 같은지는 묶음 개수로 본다
	const FName WoodID = TEXT("item.wood");
	const FName CedarID = TEXT("item.cedar");
	const FName HatchetID = TEXT("item.hatchet");      // 서버 허용 목록 밖(장비)
	const FName CookedMeatID = TEXT("item.roast_meat"); // `_meat` 지만 음식(제작품)
	constexpr int32 TestBerryCount = 3;
	constexpr int32 TestWoodCount = 5;
	constexpr int32 TestOverflowBerry = 25;
	constexpr int32 TestCap = 20; // 컴포넌트 기본값과 같은지는 아래 검사가 본다

	using FDone = USnareBackendSubsystem::FServerCallDone;

	struct FFakeServer
	{
		FString LastFunction;
		TSharedPtr<FJsonObject> LastArgs;
		TArray<FDone> Waiting;
		int32 Calls = 0;
		int32 UnexpectedAnswers = 0;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone OnDone)
				{
					++Calls;
					LastFunction = Name;
					LastArgs = Args;
					Waiting.Add(MoveTemp(OnDone));
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		/** 보낸 묶음을 그대로 받아 준 응답 — 서버 `{granted:list}` 모양. */
		void AnswerGranted()
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetArrayField(TEXT("granted"), LastArgs->GetArrayField(TEXT("items")));
			Pop()(true, Response, FString());
		}

		/** CloudScript `fail(code, ...)` 모양 — HTTP 는 성공이다. */
		void AnswerFail(const FString& Code)
		{
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetStringField(TEXT("error"), TEXT("거절"));
			Response->SetStringField(TEXT("code"), Code);
			Pop()(true, Response, FString());
		}

		/** 서버가 보낸 묶음과 **다른** 수를 준 답 — 나무꾼 ×2·도구 +1 처럼 서버만 아는 몫이 붙은 경우. */
		void AnswerGrantedLines(const TArray<TPair<FName, int32>>& Lines)
		{
			TArray<TSharedPtr<FJsonValue>> Granted;
			for (const TPair<FName, int32>& Line : Lines)
			{
				const TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("itemId"), Line.Key.ToString());
				Item->SetNumberField(TEXT("count"), Line.Value);
				Granted.Add(MakeShared<FJsonValueObject>(Item));
			}
			const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
			Response->SetArrayField(TEXT("granted"), Granted);
			Pop()(true, Response, FString());
		}

		void AnswerNetworkError() { Pop()(false, nullptr, TEXT("테스트 네트워크 끊김")); }

		FDone Pop()
		{
			// 기다리는 요청이 없는데 답하려 하면 검사 실패로 센다 — 배열 밖을 읽어 에디터가 죽으면 결과가 안 남는다.
			if (Waiting.IsEmpty())
			{
				++UnexpectedAnswers;
				return [](bool, const TSharedPtr<FJsonObject>&, const FString&) {};
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			return Done;
		}

		/** 그 줄의 `toolUses` (DESIGN 13.5). 칸이 없으면 0, 줄이 없으면 -1. */
		int32 SentToolUses(FName ItemID) const
		{
			for (const TSharedPtr<FJsonValue>& Value : LastArgs->GetArrayField(TEXT("items")))
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				if (Item->GetStringField(TEXT("itemId")) == ItemID.ToString())
				{
					double Uses = 0.0;
					return Item->TryGetNumberField(TEXT("toolUses"), Uses) ? static_cast<int32>(Uses) : 0;
				}
			}
			return -1;
		}

		/** `kills` 칸에서 그 종의 마리 수. 칸이 없으면 0. */
		int32 SentKills(FName AnimalID) const
		{
			const TArray<TSharedPtr<FJsonValue>>* Kills = nullptr;
			if (!LastArgs->TryGetArrayField(TEXT("kills"), Kills) || Kills == nullptr)
			{
				return 0;
			}
			int32 Total = 0;
			for (const TSharedPtr<FJsonValue>& Value : *Kills)
			{
				const TSharedPtr<FJsonObject> Kill = Value->AsObject();
				if (Kill->GetStringField(TEXT("animalId")) == AnimalID.ToString())
				{
					Total += static_cast<int32>(Kill->GetNumberField(TEXT("count")));
				}
			}
			return Total;
		}

		int32 SentTotal(FName ItemID = NAME_None) const
		{
			int32 Total = 0;
			for (const TSharedPtr<FJsonValue>& Value : LastArgs->GetArrayField(TEXT("items")))
			{
				const TSharedPtr<FJsonObject> Item = Value->AsObject();
				if (ItemID.IsNone() || Item->GetStringField(TEXT("itemId")) == ItemID.ToString())
				{
					Total += static_cast<int32>(Item->GetNumberField(TEXT("count")));
				}
			}
			return Total;
		}
	};

	struct FRig
	{
		UInventoryComponent* Inventory = nullptr;
		ULootClaimComponent* Claim = nullptr;

		FRig()
		{
			UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
			USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
			Inventory = NewObject<UInventoryComponent>(GetTransientPackage());
			Claim = NewObject<ULootClaimComponent>(GetTransientPackage());
			Claim->BindTargets(Inventory, Backend);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLootClaimSuccessTest, "Snarehaven.LootClaim.Drain.SuccessClearsQueueAndRefetches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLootClaimSuccessTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Inventory->QueuePendingGrant(BerryID, TestBerryCount);
	Rig.Inventory->QueuePendingGrant(WoodID, TestWoodCount);

	TestTrue(TEXT("대기열이 있으면 서버에 묻는다"), Rig.Claim->TryClaimNow());
	TestEqual(TEXT("부른 함수 = ClaimLoot"), Server.LastFunction, ULootClaimComponent::GetClaimFunctionName());
	TestEqual(TEXT("보낸 합계 = 대기열 합계"), Server.SentTotal(), TestBerryCount + TestWoodCount);

	// 응답 전: 대기열도 보유도 그대로, 두 번째 요청은 안 뜬다.
	TestEqual(TEXT("응답 전 대기열 유지(산딸기)"), Rig.Inventory->GetPendingCount(BerryID), TestBerryCount);
	TestEqual(TEXT("응답 전 보유 0"), Rig.Inventory->GetItemCount(BerryID), 0);
	TestFalse(TEXT("보내는 중엔 또 안 보낸다"), Rig.Claim->TryClaimNow());
	TestEqual(TEXT("서버 호출 1번"), Server.Calls, 1);

	Server.AnswerGranted();
	TestEqual(TEXT("성공 뒤 대기열 비움"), Rig.Inventory->GetPendingGrants().Num(), 0);
	TestEqual(TEXT("성공 뒤 보유 상태를 다시 받는다"), Rig.Claim->GetRefetchCount(), 1);
	TestEqual(TEXT("보유는 스냅샷 몫이라 그대로 0"), Rig.Inventory->GetItemCount(BerryID), 0);
	AddInfo(FString::Printf(TEXT("[LootClaim] 성공 - 보냄 %d개 · 대기열 %d줄 · 다시받기 %d"),
		TestBerryCount + TestWoodCount, Rig.Inventory->GetPendingGrants().Num(), Rig.Claim->GetRefetchCount()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLootClaimFailureTest, "Snarehaven.LootClaim.Drain.FailureKeepsQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLootClaimFailureTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Inventory->QueuePendingGrant(BerryID, TestBerryCount);

	const TCHAR* const Codes[] = { TEXT("too_soon"), TEXT("inventory_full") };
	for (const TCHAR* const Code : Codes)
	{
		TestTrue(FString::Printf(TEXT("%s 전 요청"), Code), Rig.Claim->TryClaimNow());
		Server.AnswerFail(Code);
		TestEqual(FString::Printf(TEXT("%s 뒤 대기열 유지"), Code), Rig.Inventory->GetPendingCount(BerryID), TestBerryCount);
	}
	TestTrue(TEXT("네트워크 전 요청"), Rig.Claim->TryClaimNow());
	Server.AnswerNetworkError();
	TestEqual(TEXT("네트워크 실패 뒤 대기열 유지"), Rig.Inventory->GetPendingCount(BerryID), TestBerryCount);
	TestEqual(TEXT("실패엔 다시 받지 않는다"), Rig.Claim->GetRefetchCount(), 0);

	// 다음 주기엔 같은 묶음을 다시 보내고, 이번엔 받아 준다.
	TestTrue(TEXT("다음 주기 재시도"), Rig.Claim->TryClaimNow());
	TestEqual(TEXT("재시도 묶음 = 같은 개수"), Server.SentTotal(BerryID), TestBerryCount);
	Server.AnswerGranted();
	TestEqual(TEXT("재시도 성공 뒤 비움"), Rig.Inventory->GetPendingCount(BerryID), 0);
	TestEqual(TEXT("보내지 않은 요청에 답하지 않았다"), Server.UnexpectedAnswers, 0);
	AddInfo(FString::Printf(TEXT("[LootClaim] 실패 3종 뒤 대기열 %d개 유지 · 재시도 성공 뒤 %d"),
		TestBerryCount, Rig.Inventory->GetPendingCount(BerryID)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLootClaimBatchTest, "Snarehaven.LootClaim.Drain.CapsBatchAndSkipsDisallowed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLootClaimBatchTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Inventory->QueuePendingGrant(HatchetID, 1);
	Rig.Inventory->QueuePendingGrant(CookedMeatID, 1);
	Rig.Inventory->QueuePendingGrant(BerryID, TestOverflowBerry);

	TestTrue(TEXT("허용분이 있으니 묻는다"), Rig.Claim->TryClaimNow());
	TestEqual(TEXT("한 묶음 합계 = 20"), Server.SentTotal(), TestCap);
	TestEqual(TEXT("손도끼는 안 실었다"), Server.SentTotal(HatchetID), 0);
	TestEqual(TEXT("구운 고기는 안 실었다"), Server.SentTotal(CookedMeatID), 0);
	Server.AnswerGranted();
	TestEqual(TEXT("넘친 산딸기는 다음 주기로"), Rig.Inventory->GetPendingCount(BerryID), TestOverflowBerry - TestCap);
	TestEqual(TEXT("목록 밖은 대기열에 남는다(손도끼)"), Rig.Inventory->GetPendingCount(HatchetID), 1);
	TestEqual(TEXT("목록 밖은 대기열에 남는다(구운 고기)"), Rig.Inventory->GetPendingCount(CookedMeatID), 1);

	// 목록 밖만 남으면 서버에 아예 안 묻는다.
	Rig.Inventory->RemovePendingGrant(BerryID, TestOverflowBerry);
	const int32 CallsBefore = Server.Calls;
	TestFalse(TEXT("목록 밖만 남으면 안 묻는다"), Rig.Claim->TryClaimNow());
	TestEqual(TEXT("서버 호출 늘지 않음"), Server.Calls, CallsBefore);
	AddInfo(FString::Printf(TEXT("[LootClaim] 묶음 %d개 · 남은 산딸기 %d · 손도끼 %d · 구운고기 %d"),
		TestCap, TestOverflowBerry - TestCap, 1, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLootClaimToolUsesTest, "Snarehaven.LootClaim.ToolUses.SentPerLineAndDrained",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// DESIGN 13.5 전기톱·마체테 — 「도구로 얻은 번」이 줄마다 합쳐져 `toolUses` 로 나가고, 개수를 넘지 않으며, 받아 준 만큼 빠진다.
bool FSnarehavenLootClaimToolUsesTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Inventory->QueuePendingGrant(WoodID, 3, 1);
	Rig.Inventory->QueuePendingGrant(WoodID, 2, 1);
	Rig.Inventory->QueuePendingGrant(CedarID, 2, 5);
	Rig.Inventory->QueuePendingGrant(BerryID, TestBerryCount);

	TestTrue(TEXT("묻는다"), Rig.Claim->TryClaimNow());
	TestEqual(TEXT("나무 두 번 합쳐 toolUses 2"), Server.SentToolUses(WoodID), 2);
	TestEqual(TEXT("향나무 toolUses 는 개수(2)까지"), Server.SentToolUses(CedarID), 2);
	TestEqual(TEXT("산딸기엔 toolUses 칸이 없다"), Server.SentToolUses(BerryID), 0);
	Server.AnswerGranted();
	TestEqual(TEXT("성공 뒤 비움"), Rig.Inventory->GetPendingGrants().Num(), 0);

	// 한 묶음 20개에 잘리면 toolUses 도 보낸 개수까지만, 나머지는 대기열에 남아 다음 묶음에 간다.
	Rig.Inventory->QueuePendingGrant(BerryID, TestCap - 1);
	Rig.Inventory->QueuePendingGrant(WoodID, 3, 3);
	TestTrue(TEXT("다시 묻는다"), Rig.Claim->TryClaimNow());
	TestEqual(TEXT("잘린 나무 1개 → toolUses 1"), Server.SentToolUses(WoodID), 1);
	Server.AnswerGranted();
	const FInventorySlot* const Left = Rig.Inventory->GetPendingGrants().FindByPredicate(
		[](const FInventorySlot& Slot) { return Slot.ItemID == WoodID; });
	if (TestNotNull(TEXT("남은 나무 줄"), Left))
	{
		TestEqual(TEXT("남은 나무 2"), Left->Count, 2);
		TestEqual(TEXT("남은 toolUses 2"), Left->ToolUses, 2);
	}
	AddInfo(FString::Printf(TEXT("[LootClaim] toolUses 나무 2·향나무 2 → 잘린 묶음 1 · 남은 %d"), Left ? Left->ToolUses : -1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLootClaimKillsTest, "Snarehaven.LootClaim.Kills.SentAsAnimalIdsNotItems",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// 지시(감독 10-20 19:30) ① — 사냥 처치는 `kills:[{animalId, count}]` 로 나가고 드롭 아이템은 안 싣는다. 한 마리 = 서버 최대 개수 3 으로 센다.
bool FSnarehavenLootClaimKillsTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.Inventory->QueuePendingGrant(FoxID, TestFoxKills);
	Rig.Inventory->QueuePendingGrant(WolfID, 1);
	Rig.Inventory->QueuePendingGrant(WoodID, TestWoodCount);
	Rig.Inventory->QueuePendingGrant(OldDropID, 1);

	TestTrue(TEXT("묻는다"), Rig.Claim->TryClaimNow());
	TestEqual(TEXT("여우 2마리 → kills"), Server.SentKills(FoxID), TestFoxKills);
	TestEqual(TEXT("늑대 1마리 → kills"), Server.SentKills(WolfID), 1);
	TestEqual(TEXT("items 엔 나무만"), Server.SentTotal(), TestWoodCount);
	TestEqual(TEXT("종 ID 는 items 에 안 섞인다"), Server.SentTotal(FoxID), 0);
	TestEqual(TEXT("옛 드롭 아이템(돼지고기)은 안 실었다"), Server.SentTotal(OldDropID), 0);
	Server.AnswerGranted();
	TestEqual(TEXT("성공 뒤 여우 줄 비움"), Rig.Inventory->GetPendingCount(FoxID), 0);
	TestEqual(TEXT("성공 뒤 늑대 줄 비움"), Rig.Inventory->GetPendingCount(WolfID), 0);
	TestEqual(TEXT("옛 드롭 아이템은 대기열에 남는다"), Rig.Inventory->GetPendingCount(OldDropID), 1);

	// 한 묶음 20 = 처치 6마리(18) — 7마리째는 다음 주기로. 남은 2 칸엔 나무가 들어간다.
	Rig.Inventory->RemovePendingGrant(OldDropID, 1);
	Rig.Inventory->QueuePendingGrant(FoxID, TestOverflowKills);
	Rig.Inventory->QueuePendingGrant(WoodID, TestWoodCount);
	TestTrue(TEXT("다시 묻는다"), Rig.Claim->TryClaimNow());
	const int32 FitKills = TestCap / TestDropsPerKill;
	TestEqual(TEXT("처치는 20/3 = 6마리까지"), Server.SentKills(FoxID), FitKills);
	TestEqual(TEXT("남은 칸만큼 나무"), Server.SentTotal(WoodID), TestCap - FitKills * TestDropsPerKill);
	Server.AnswerGranted();
	TestEqual(TEXT("넘친 처치는 다음 주기로"), Rig.Inventory->GetPendingCount(FoxID), TestOverflowKills - FitKills);

	TestTrue(TEXT("animal.fox 는 처치 줄"), ULootClaimComponent::IsKillLine(FoxID));
	TestFalse(TEXT("아이템은 처치 줄 아님"), ULootClaimComponent::IsKillLine(OldDropID));
	TestFalse(TEXT("대문자 종은 처치 줄 아님(서버 표 글자)"), ULootClaimComponent::IsKillLine(TEXT("animal.Fox")));
	AddInfo(FString::Printf(TEXT("[LootClaim] 처치 여우 %d·늑대 1 → kills · 넘친 묶음 %d마리 · 남은 %d"),
		TestFoxKills, FitKills, Rig.Inventory->GetPendingCount(FoxID)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLootClaimAllowTest, "Snarehaven.LootClaim.Allow.PetsAndGatherOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLootClaimAllowTest::RunTest(const FString& Parameters)
{
	// 사냥 드롭(고기·가죽·전리품)은 `kills` 몫이라 아이템으로는 안 보낸다 (지시 ①).
	const TCHAR* const HuntDrops[] = { TEXT("item.pig_meat"), TEXT("item.sheep_hide"), TEXT("item.fox_tail"),
		TEXT("item.pristine_fox_tail"), TEXT("item.wolf_tooth"), TEXT("item.pheasant_feather") };
	for (const TCHAR* const Drop : HuntDrops)
	{
		TestFalse(FString::Printf(TEXT("사냥 드롭 %s 는 아이템으로 안 보낸다"), Drop), ULootClaimComponent::IsClaimableLoot(Drop));
	}
	TestTrue(TEXT("펫 허용"), ULootClaimComponent::IsClaimableLoot(TEXT("item.pet_pig")));
	TestTrue(TEXT("산딸기 허용"), ULootClaimComponent::IsClaimableLoot(TEXT("item.berry")));
	// 채집물은 서버 `GATHER_ITEMS` 와 같이 — 채집 표의 모든 지급 아이템이 나가야 한다(향나무·13.4 더미 포함).
	const UDataTable* const GatherTable =
		LoadObject<UDataTable>(nullptr, TEXT("/Game/Game/Data/Gatherables/DT_GatherableTable.DT_GatherableTable"));
	if (TestNotNull(TEXT("DT_GatherableTable"), GatherTable))
	{
		GatherTable->ForeachRow<FGatherableRow>(TEXT("LootClaimAllow"), [this](const FName&, const FGatherableRow& Row)
		{
			TArray<FName> Grants = { Row.GrantItemID };
			for (const FGatherGrantOption& Option : Row.GrantOptions)
			{
				Grants.Add(Option.ItemID);
			}
			for (const FName Grant : Grants)
			{
				if (!Grant.IsNone())
				{
					TestTrue(FString::Printf(TEXT("[%s] 채집물 %s 허용"), *Row.GatherableID.ToString(), *Grant.ToString()),
						ULootClaimComponent::IsClaimableLoot(Grant));
				}
			}
		});
	}
	TestFalse(TEXT("늑대 털(목장 생산물) 거절"), ULootClaimComponent::IsClaimableLoot(TEXT("item.wolf_fur")));
	TestFalse(TEXT("가챠 거절"), ULootClaimComponent::IsClaimableLoot(TEXT("gacha.mat_hide")));
	AddInfo(FString::Printf(TEXT("[LootClaim] 사냥 드롭 %d종 거절 · 펫·채집물 허용"), UE_ARRAY_COUNT(HuntDrops)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLootClaimPopupTest, "Snarehaven.LootClaim.Popup.ShowsServerGrantedOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M21-03b2 채집 획득 수 팝업 — 글은 서버가 받아 준 뒤에만, 수는 서버 `granted` 그대로(보낸 수가 아니라), 거절이면 없다 (CLAUDE.md 8).
bool FSnarehavenLootClaimPopupTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	int32 Broadcasts = 0;
	FText Heard;
	Rig.Claim->OnGrantPopup.AddLambda([&Broadcasts, &Heard](const FText& Popup) { ++Broadcasts; Heard = Popup; });

	// 시험 값: 나무 5개를 보냈는데 서버가 10개(×2)를 줬다고 답한다.
	Rig.Inventory->QueuePendingGrant(WoodID, TestWoodCount);
	TestTrue(TEXT("묻는다"), Rig.Claim->TryClaimNow());
	TestTrue(TEXT("응답 전엔 글이 없다"), Rig.Claim->GetLastGrantPopup().IsEmpty());
	TestEqual(TEXT("응답 전엔 알림 0번"), Broadcasts, 0);
	Server.AnswerFail(TEXT("too_soon"));
	TestTrue(TEXT("거절이면 글이 없다"), Rig.Claim->GetLastGrantPopup().IsEmpty());
	TestEqual(TEXT("거절이면 알림 0번"), Broadcasts, 0);

	TestTrue(TEXT("다시 묻는다"), Rig.Claim->TryClaimNow());
	Server.AnswerGrantedLines({ { WoodID, TestWoodCount * 2 } });
	const FString Single = Rig.Claim->GetLastGrantPopup().ToString();
	TestEqual(TEXT("수는 서버 답 그대로"), Single, FString::Printf(TEXT("+%d 나뭇가지"), TestWoodCount * 2));
	TestEqual(TEXT("성공이면 알림 1번"), Broadcasts, 1);
	TestEqual(TEXT("알림 글 = 보관 글"), Heard.ToString(), Single);
	AddInfo(FString::Printf(TEXT("[LootPopup] 보냄 %d · 받은 글 '%s'"), TestWoodCount, *Single));

	// 종류가 한도(3)를 넘으면 접고, 0개 줄·모르는 ID 는 지어내지 않는다.
	Rig.Inventory->QueuePendingGrant(FoxID, 1);
	TestTrue(TEXT("세 번째 묻기"), Rig.Claim->TryClaimNow());
	Server.AnswerGrantedLines({ { WoodID, 2 }, { BerryID, 0 }, { OldDropID, TestBerryCount },
		{ CedarID, 1 }, { TEXT("item.test_unknown"), 4 } });
	const FString Many = Rig.Claim->GetLastGrantPopup().ToString();
	TestEqual(TEXT("세 종류 + 외 1종"), Many,
		FString::Printf(TEXT("+2 나뭇가지 · +%d 돼지고기 · +1 향나무 외 1종"), TestBerryCount));
	TestEqual(TEXT("한도 = 3종"), ULootClaimComponent::GetGrantPopupMaxKinds(), 3);
	TestEqual(TEXT("모르는 ID 는 ID 그대로"),
		ULootClaimComponent::FormatGrantPopup({ { FText::FromString(TEXT("item.test_unknown")), 4 } }).ToString(),
		FString(TEXT("+4 item.test_unknown")));
	TestTrue(TEXT("0개뿐이면 빈 글"), ULootClaimComponent::FormatGrantPopup({ { FText::FromString(TEXT("x")), 0 } }).IsEmpty());

	// 서버가 빈 `granted` 를 주면(받을 게 없었다) 알림이 없다.
	Rig.Inventory->QueuePendingGrant(BerryID, 1);
	TestTrue(TEXT("네 번째 묻기"), Rig.Claim->TryClaimNow());
	Server.AnswerGrantedLines({});
	TestTrue(TEXT("빈 답이면 글이 없다"), Rig.Claim->GetLastGrantPopup().IsEmpty());
	TestEqual(TEXT("알림은 성공 두 번만"), Broadcasts, 2);
	AddInfo(FString::Printf(TEXT("[LootPopup] 여러 종류 '%s' · 알림 %d번"), *Many, Broadcasts));
	return true;
}

#endif
