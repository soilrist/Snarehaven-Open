// ROADMAP M12-05 3층 후속 2층 검사 — 목장 HUD 의 나무 넣기·칸 확장 버튼과 실패 문구, 확장 칸 월드 슬롯:
// ① 응답 전엔 유지·명성·칸이 그대로고 버튼이 꺼진다, ② 성공 뒤 다시 받은 GetBaseState 값으로만 바뀐다,
// ③ 실패 code → `DT_RanchFailureTable` 문구(함수별 `full` 포함), ④ 슬롯 액터가 칸 수만큼만 보인다,
// ⑤ 규칙 행 목장 숫자가 DESIGN 3.2 와 같다.
// 가짜 서버(`USnareBackendSubsystem::TestServerOverride`)를 꽂아 진짜 응답과 같은 길을 태운다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.RanchUpkeepHud; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Combat/MeleeAttackComponent.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Building/SnareBaseSubsystem.h"
#include "Data/BaseRuleRow.h"
#include "Data/RanchFailureRow.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchInteractComponent.h"
#include "Ranch/RanchSlotActor.h"
#include "Ranch/RanchSyncComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SnarehavenCharacter.h"
#include "UI/RanchHudWidget.h"

namespace SnarehavenRanchUpkeepHudTest
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr int32 TestWood = 12;
	constexpr int32 TestLeftHours = 10;
	constexpr int32 TestLeftHoursAfter = 34;
	constexpr int32 TestFame = 60;
	constexpr int32 TestSlotCount = 10;

	const TCHAR* FailureTablePath = TEXT("/Game/Game/Data/Ranch/DT_RanchFailureTable.DT_RanchFailureTable");
	const TCHAR* BaseRuleTablePath = TEXT("/Game/Game/Data/Balance/DT_BaseRuleTable.DT_BaseRuleTable");

	using FDone = USnareBackendSubsystem::FServerCallDone;

	struct FFakeServer
	{
		TArray<FString> Functions;
		TArray<TSharedPtr<FJsonObject>> Args;
		TArray<FDone> Waiting;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& InArgs, FDone OnDone)
				{
					Functions.Add(Name);
					Args.Add(InArgs);
					Waiting.Add(MoveTemp(OnDone));
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		int32 CountCalls(const FString& Name) const
		{
			return Functions.FilterByPredicate([&Name](const FString& F) { return F == Name; }).Num();
		}

		void Answer(bool bOk, const TSharedPtr<FJsonObject>& Response)
		{
			if (Waiting.IsEmpty())
			{
				return;
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			Done(bOk, Response, bOk ? FString() : TEXT("테스트 네트워크 끊김"));
		}
	};

	TSharedPtr<FJsonObject> ParseJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}

	/** GetBaseState 응답 — 칸 수(`SlotCap` 길이의 빈 목장)와 유지 값만 싣는다. */
	TSharedPtr<FJsonObject> MakeBaseState(int32 LeftHours, int32 Fame, int32 SlotCap)
	{
		TArray<FString> Nulls;
		Nulls.Init(TEXT("null"), SlotCap);
		return ParseJson(FString::Printf(TEXT(R"({"ranch":[%s],"ranchUpkeep":{"upkeepUntilUtc":null,"leftHours":%d,"locked":false,"fame":%d,"slotCap":%d}})"),
			*FString::Join(Nulls, TEXT(",")), LeftHours, Fame, SlotCap));
	}

	TSharedPtr<FJsonObject> MakeFail(const FString& Code, int32 Need = -1, int32 Have = -1)
	{
		const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("error"), TEXT("서버 사유 문장"));
		Response->SetStringField(TEXT("code"), Code);
		if (Need >= 0)
		{
			Response->SetNumberField(TEXT("need"), Need);
			Response->SetNumberField(TEXT("have"), Have);
		}
		return Response;
	}

	FText TableMessage(const UDataTable* Table, const TCHAR* RowName)
	{
		const FRanchFailureRow* const Row = Table != nullptr ? Table->FindRow<FRanchFailureRow>(RowName, TEXT("Test"), false) : nullptr;
		return Row != nullptr ? Row->Message : FText::GetEmpty();
	}

	UWorld* CreateTestWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		return World;
	}

	void DestroyTestWorld(UWorld* World)
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** 백엔드(나무를 가진 스냅샷) · 거점 규칙 · 목장 · 요청 컴포넌트 · HUD 한 벌. */
	struct FRig
	{
		UWorld* World = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		USnareBaseSubsystem* Base = nullptr;
		URanchComponent* Ranch = nullptr;
		URanchSyncComponent* Sync = nullptr;
		URanchHudWidget* Hud = nullptr;

		explicit FRig(int32 WoodCount)
		{
			World = CreateTestWorld();
			UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
			Backend = NewObject<USnareBackendSubsystem>(GameInstance);
			Base = NewObject<USnareBaseSubsystem>(GameInstance);
			Ranch = NewObject<URanchComponent>(GetTransientPackage());
			Sync = NewObject<URanchSyncComponent>(GetTransientPackage());

			// 스냅샷을 **먼저** 넣고 묶는다 — 묶은 뒤 넣으면 되살리기 GetBaseState 가 가짜 서버 줄에 끼어든다.
			const FBaseRuleRow* const Rules = Base->GetRules();
			FSnarePlayerSnapshot Snapshot;
			Snapshot.PlayFabId = TEXT("TestPlayer");
			if (Rules != nullptr)
			{
				Snapshot.AddItem(Rules->UpkeepItemID, WoodCount);
			}
			Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
			Sync->BindTargets(Ranch, nullptr, Backend, Base, nullptr);

			Hud = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
			if (Hud != nullptr)
			{
				Hud->BindToRanchSync(Sync);
			}
		}

		~FRig() { DestroyTestWorld(World); }

		void SeedUpkeep(int32 LeftHours, int32 Fame, int32 SlotCap)
		{
			FRanchUpkeepState Upkeep;
			Upkeep.LeftHours = LeftHours;
			Upkeep.Fame = Fame;
			Upkeep.SlotCap = SlotCap;
			Ranch->ApplyBackendUpkeep(Upkeep);
		}
	};

	ARanchSlotActor* SpawnSlot(UWorld* World, int32 SlotIndex)
	{
		ARanchSlotActor* const Slot = World->SpawnActor<ARanchSlotActor>(FVector(0.f, SlotIndex * 200.f, 0.f), FRotator::ZeroRotator);
		if (Slot == nullptr)
		{
			return nullptr;
		}
		if (FIntProperty* const IndexProperty =
			CastField<FIntProperty>(ARanchSlotActor::StaticClass()->FindPropertyByName(TEXT("SlotIndex"))))
		{
			IndexProperty->SetPropertyValue_InContainer(Slot, SlotIndex);
		}
		return Slot;
	}

	int32 CaptureInt(const FString& Text, const TCHAR* Pattern, int32 Group = 1)
	{
		FRegexMatcher Matcher(FRegexPattern(Pattern), Text);
		return Matcher.FindNext() ? FCString::Atoi(*Matcher.GetCaptureGroup(Group)) : -1;
	}
}

using namespace SnarehavenRanchUpkeepHudTest;

// ① + ② 나무 넣기: 가진 나무 전부를 싣고, 응답 전엔 아무것도 안 바뀌며 두 버튼이 꺼지고, 다시 받은 값으로만 바뀐다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchFeedUpkeepHudTest, "Snarehaven.RanchUpkeepHud.Feed.WaitsForServerThenRefetches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchFeedUpkeepHudTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig(TestWood);
	if (!TestNotNull(TEXT("HUD"), Rig.Hud) || !TestNotNull(TEXT("목장 규칙 행"), Rig.Sync->GetRanchRules()))
	{
		return false;
	}
	Rig.SeedUpkeep(TestLeftHours, TestFame, Rig.Ranch->GetBaseSlotCount());

	TestEqual(TEXT("가방 나무 수"), Rig.Sync->GetUpkeepItemCount(), TestWood);
	TestTrue(TEXT("나무 넣기 글자에 개수"), Rig.Hud->GetFeedUpkeepButtonText().ToString().Contains(FString::FromInt(TestWood)));
	TestTrue(TEXT("누르기 전 나무 넣기 켜짐"), Rig.Hud->IsFeedUpkeepEnabled());

	TestTrue(TEXT("버튼이 서버에 묻는다"), Rig.Hud->TryFeedUpkeep());
	const FString Feed = URanchSyncComponent::GetFeedUpkeepFunctionName();
	TestEqual(TEXT("FeedRanchUpkeep 한 번"), Server.CountCalls(Feed), 1);
	TestEqual(TEXT("count = 가진 나무 전부"), Server.Args.IsEmpty() ? -1 : static_cast<int32>(Server.Args[0]->GetNumberField(TEXT("count"))), TestWood);

	// 응답 전: 유지·명성·칸 그대로, 두 버튼 꺼짐, 또 눌러도 안 나간다.
	TestFalse(TEXT("응답 전 나무 넣기 꺼짐"), Rig.Hud->IsFeedUpkeepEnabled());
	TestFalse(TEXT("응답 전 칸 확장 꺼짐"), Rig.Hud->IsExpandSlotEnabled());
	TestFalse(TEXT("응답 전 또 누르면 안 나간다"), Rig.Hud->TryFeedUpkeep());
	TestEqual(TEXT("응답 전 남은 시간 그대로"), Rig.Ranch->GetUpkeep().LeftHours, TestLeftHours);

	Server.Answer(true, ParseJson(TEXT(R"({"used":12,"upkeepUntilUtc":"2026-09-26T00:00:00.000Z","unlocked":false})")));
	TestEqual(TEXT("성공 뒤 GetBaseState 로 다시 묻는다"), Server.CountCalls(URanchSyncComponent::GetBaseStateFunctionName()), 1);
	TestEqual(TEXT("다시 받기 전엔 남은 시간 그대로(응답 본문을 안 믿는다)"), Rig.Ranch->GetUpkeep().LeftHours, TestLeftHours);
	TestTrue(TEXT("답이 오면 버튼이 다시 켜진다"), Rig.Hud->IsFeedUpkeepEnabled());
	TestTrue(TEXT("성공엔 실패 문구 없음"), Rig.Hud->GetFailureNoticeText().IsEmpty());

	Server.Answer(true, MakeBaseState(TestLeftHoursAfter, TestFame, Rig.Ranch->GetBaseSlotCount()));
	TestEqual(TEXT("다시 받은 남은 시간"), Rig.Ranch->GetUpkeep().LeftHours, TestLeftHoursAfter);
	AddInfo(FString::Printf(TEXT("[RanchUpkeepHud] 나무 넣기 - 호출 %s · 남은 시간 %d→%d"),
		*FString::Join(Server.Functions, TEXT(",")), TestLeftHours, Rig.Ranch->GetUpkeep().LeftHours));
	return true;
}

// ② + ④ 칸 확장: 다음 비용을 보여 주고, 다시 받은 스냅샷이 길어져야 칸이 늘고 6번째 슬롯 액터가 나타난다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchExpandSlotHudTest, "Snarehaven.RanchUpkeepHud.Expand.CostThenSlotAppears",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchExpandSlotHudTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig(TestWood);
	const FBaseRuleRow* const Rules = Rig.Sync->GetRanchRules();
	if (!TestNotNull(TEXT("HUD"), Rig.Hud) || !TestNotNull(TEXT("목장 규칙 행"), Rules))
	{
		return false;
	}
	const int32 BaseSlots = Rig.Ranch->GetBaseSlotCount();

	TestFalse(TEXT("유지 상태를 받기 전엔 확장 꺼짐"), Rig.Hud->IsExpandSlotEnabled());
	Rig.SeedUpkeep(TestLeftHours, TestFame, BaseSlots);

	int32 Fame = 0;
	int32 Coins = 0;
	TestTrue(TEXT("다음 비용을 안다"), Rig.Sync->GetNextExpandCost(Fame, Coins));
	TestEqual(TEXT("첫 확장 명성 = 단계 × 1"), Fame, Rules->RanchExpandFameStep);
	TestEqual(TEXT("첫 확장 코인 = 단계 × 1"), Coins, Rules->RanchExpandCoinStep);
	const FString CostText = Rig.Hud->GetExpandSlotButtonText().ToString();
	TestTrue(*FString::Printf(TEXT("버튼에 비용 (실제 \"%s\")"), *CostText),
		CostText.Contains(FString::FromInt(Fame)) && CostText.Contains(FString::FromInt(Coins)));

	// 월드 슬롯 10개 — 받은 칸 수만큼만 보인다.
	TArray<ARanchSlotActor*> Slots;
	for (int32 Index = 0; Index < TestSlotCount; ++Index)
	{
		Slots.Add(SpawnSlot(Rig.World, Index));
	}
	TestEqual(TEXT("기본 칸 수만 보인다"), URanchInteractComponent::ApplySlotVisibility(Rig.World, Rig.Ranch->GetSlotCount()), BaseSlots);
	TestTrue(TEXT("6번째 슬롯은 숨는다"), Slots[BaseSlots] != nullptr && Slots[BaseSlots]->IsHidden());

	TestTrue(TEXT("확장을 묻는다"), Rig.Hud->TryExpandSlot());
	TestEqual(TEXT("ExpandRanchSlot 한 번"), Server.CountCalls(URanchSyncComponent::GetExpandSlotFunctionName()), 1);
	TestFalse(TEXT("응답 전 두 버튼 꺼짐"), Rig.Hud->IsExpandSlotEnabled() || Rig.Hud->IsFeedUpkeepEnabled());

	Server.Answer(true, ParseJson(FString::Printf(TEXT(R"({"slotCap":%d,"fame":%d,"cost":{"fame":%d,"coins":%d}})"),
		BaseSlots + 1, TestFame - Fame, Fame, Coins)));
	TestEqual(TEXT("다시 받기 전엔 칸 상한 그대로"), Rig.Ranch->GetUpkeep().SlotCap, BaseSlots);
	TestEqual(TEXT("다시 받기 전엔 명성 그대로"), Rig.Ranch->GetUpkeep().Fame, TestFame);
	TestEqual(TEXT("다시 받기 전엔 칸 수 그대로"), Rig.Ranch->GetSlotCount(), BaseSlots);

	Server.Answer(true, MakeBaseState(TestLeftHours, TestFame - Fame, BaseSlots + 1));
	TestEqual(TEXT("다시 받은 칸 상한"), Rig.Ranch->GetUpkeep().SlotCap, BaseSlots + 1);
	TestEqual(TEXT("다시 받은 칸 수"), Rig.Ranch->GetSlotCount(), BaseSlots + 1);
	TestEqual(TEXT("늘어난 칸만큼 슬롯이 보인다"), URanchInteractComponent::ApplySlotVisibility(Rig.World, Rig.Ranch->GetSlotCount()), BaseSlots + 1);
	TestTrue(TEXT("6번째 슬롯이 나타난다"), Slots[BaseSlots] != nullptr && !Slots[BaseSlots]->IsHidden());
	TestTrue(TEXT("7번째 슬롯은 여전히 숨는다"), Slots[BaseSlots + 1] != nullptr && Slots[BaseSlots + 1]->IsHidden());
	TestTrue(TEXT("다음 비용 = 단계 × 2"), Rig.Sync->GetNextExpandCost(Fame, Coins) && Fame == Rules->RanchExpandFameStep * 2);

	Rig.SeedUpkeep(TestLeftHours, TestFame, Rules->RanchSlotMax);
	TestTrue(TEXT("상한이면 칸 최대"), Rig.Sync->IsSlotCapAtMax());
	TestFalse(TEXT("상한이면 확장 꺼짐"), Rig.Hud->IsExpandSlotEnabled());
	AddInfo(FString::Printf(TEXT("[RanchUpkeepHud] 칸 확장 - \"%s\" · 보이는 슬롯 %d→%d · 최대 \"%s\""),
		*CostText, BaseSlots, BaseSlots + 1, *Rig.Hud->GetExpandSlotButtonText().ToString()));
	return true;
}

// ③ 실패 code → 표 문구. 유지·칸은 그대로, 다시 묻지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchFailureNoticeTest, "Snarehaven.RanchUpkeepHud.Failure.CodeToTableMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchFailureNoticeTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, FailureTablePath);
	if (!TestNotNull(TEXT("DT_RanchFailureTable"), Table))
	{
		return false;
	}
	for (const TCHAR* RowName : { TEXT("locked"), TEXT("not_enough_fame"), TEXT("not_enough_coins"), TEXT("not_enough_items"),
		TEXT("FeedRanchUpkeep_full"), TEXT("ExpandRanchSlot_full"), TEXT("network") })
	{
		TestFalse(*FString::Printf(TEXT("%s 행 문구"), RowName), TableMessage(Table, RowName).IsEmpty());
	}

	const FString Feed = URanchSyncComponent::GetFeedUpkeepFunctionName();
	const FString Expand = URanchSyncComponent::GetExpandSlotFunctionName();
	auto Describe = [Table](const FString& Function, const TSharedPtr<FJsonObject>& Response)
	{
		return URanchSyncComponent::DescribeRanchFailure(Table, Function, true, Response, FString()).ToString();
	};
	TestEqual(TEXT("RegisterPet locked"), Describe(TEXT("RegisterPet"), MakeFail(TEXT("locked"))), TableMessage(Table, TEXT("locked")).ToString());
	// #0190 펫 배치가 펫 아이템을 쓴다 — 펫이 없다는 거절이 "나무가 모자란다" 로 뜨면 안 된다.
	TestEqual(TEXT("RegisterPet 펫 없음 = 펫 문구"), Describe(TEXT("RegisterPet"), MakeFail(TEXT("not_enough_items"))),
		TableMessage(Table, TEXT("RegisterPet_not_enough_items")).ToString());
	TestNotEqual(TEXT("펫 없음 ≠ 나무 부족"), Describe(TEXT("RegisterPet"), MakeFail(TEXT("not_enough_items"))),
		Describe(Feed, MakeFail(TEXT("not_enough_items"))));
	TestEqual(TEXT("유지 full = 유지 가득"), Describe(Feed, MakeFail(TEXT("full"))), TableMessage(Table, TEXT("FeedRanchUpkeep_full")).ToString());
	TestEqual(TEXT("확장 full = 칸 최대"), Describe(Expand, MakeFail(TEXT("full"))), TableMessage(Table, TEXT("ExpandRanchSlot_full")).ToString());
	TestNotEqual(TEXT("두 full 문구가 다르다"), Describe(Feed, MakeFail(TEXT("full"))), Describe(Expand, MakeFail(TEXT("full"))));
	TestEqual(TEXT("표에 없는 code = 서버 사유"), Describe(TEXT("HarvestRanch"), MakeFail(TEXT("some_new_code"))), FString(TEXT("서버 사유 문장")));
	TestEqual(TEXT("네트워크 실패 = network 행"),
		URanchSyncComponent::DescribeRanchFailure(Table, Feed, false, nullptr, TEXT("끊김")).ToString(), TableMessage(Table, TEXT("network")).ToString());

	// HUD 길: 명성 부족 거절 → 문구(+모자란 수)가 뜨고, 칸·명성은 그대로, GetBaseState 도 안 부른다.
	FFakeServer Server;
	Server.Install();
	FRig Rig(TestWood);
	if (!TestNotNull(TEXT("HUD"), Rig.Hud))
	{
		return false;
	}
	constexpr int32 TestNeed = 50;
	constexpr int32 TestHave = 7;
	Rig.SeedUpkeep(TestLeftHours, TestHave, Rig.Ranch->GetBaseSlotCount());
	TestTrue(TEXT("확장을 묻는다"), Rig.Hud->TryExpandSlot());
	Server.Answer(true, MakeFail(TEXT("not_enough_fame"), TestNeed, TestHave));
	const FString Notice = Rig.Hud->GetFailureNoticeText().ToString();
	TestTrue(*FString::Printf(TEXT("명성 부족 문구 (실제 \"%s\")"), *Notice),
		Notice.StartsWith(TableMessage(Table, TEXT("not_enough_fame")).ToString()) && Notice.Contains(FString::FromInt(TestNeed)));
	TestEqual(TEXT("거절이면 다시 묻지 않는다"), Server.CountCalls(URanchSyncComponent::GetBaseStateFunctionName()), 0);
	TestEqual(TEXT("거절이면 칸 상한 그대로"), Rig.Ranch->GetUpkeep().SlotCap, Rig.Ranch->GetBaseSlotCount());
	TestEqual(TEXT("거절이면 명성 그대로"), Rig.Ranch->GetUpkeep().Fame, TestHave);
	TestTrue(TEXT("거절 뒤 버튼이 다시 켜진다"), Rig.Hud->IsExpandSlotEnabled());

	// 배치(RegisterPet) 잠김 거절도 같은 표를 탄다.
	TArray<FRanchSlotState> Empty;
	Empty.SetNum(Rig.Ranch->GetBaseSlotCount());
	Rig.Ranch->ApplyBackendSnapshot(Empty);
	TestTrue(TEXT("배치 요청이 쌓인다"), Rig.Ranch->QueueRegisterPet(0, TEXT("animal.pig")));
	Server.Answer(true, MakeFail(TEXT("locked")));
	TestEqual(TEXT("잠김 문구"), Rig.Hud->GetFailureNoticeText().ToString(), TableMessage(Table, TEXT("locked")).ToString());
	TestTrue(TEXT("잠김이면 칸 비어 있음 그대로"), Rig.Ranch->IsSlotEmpty(0));
	AddInfo(FString::Printf(TEXT("[RanchUpkeepHud] 실패 문구 - \"%s\" / \"%s\""), *Notice, *Rig.Hud->GetFailureNoticeText().ToString()));
	return true;
}

// ⑤ 규칙 행 목장 숫자 = DESIGN 3.2 (코드에 50·500·10 을 적지 않는다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchRulesDesignTest, "Snarehaven.RanchUpkeepHud.Rules.MatchDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchRulesDesignTest::RunTest(const FString& Parameters)
{
	FString Design;
	if (!TestTrue(TEXT("DESIGN.md 읽기"), FFileHelper::LoadFileToString(Design, *FPaths::Combine(FPaths::ProjectDir(), TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, BaseRuleTablePath);
	const FBaseRuleRow* const Row = Table != nullptr ? Table->FindRow<FBaseRuleRow>(TEXT("Base"), TEXT("RanchRulesTest")) : nullptr;
	if (!TestNotNull(TEXT("DT_BaseRuleTable 의 Base 행"), Row))
	{
		return false;
	}
	TestTrue(TEXT("목장 숫자 채워짐"), Row->HasRanchRules());
	const TCHAR* const CostPattern = TEXT(R"(명성 \*\*(\d+)\*\*·k \+ 코인 \*\*(\d+)\*\*·k)");
	TestEqual(TEXT("확장 명성 단계"), Row->RanchExpandFameStep, CaptureInt(Design, CostPattern, 1));
	TestEqual(TEXT("확장 코인 단계"), Row->RanchExpandCoinStep, CaptureInt(Design, CostPattern, 2));
	TestEqual(TEXT("칸 상한"), Row->RanchSlotMax, CaptureInt(Design, TEXT(R"(목장 슬롯 상한 \| \*\*(\d+)칸\*\*)")));
	FRegexMatcher ItemMatcher(FRegexPattern(TEXT(R"(목장 유지비 \| 나무\(`([a-z_.]+)`\))")), Design);
	TestEqual(TEXT("유지비 아이템"), Row->UpkeepItemID.ToString(), ItemMatcher.FindNext() ? ItemMatcher.GetCaptureGroup(1) : FString());
	return true;
}


// ⑥ #0199 펫 빼기는 확인 한 번 — 「펫 빼기」는 확인 줄만 펴고, 「빼기」에서만 RemovePet 요청이 쌓인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchRemovePetConfirmTest, "Snarehaven.RanchUpkeepHud.RemovePet.AsksConfirmFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchRemovePetConfirmTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateTestWorld();
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	ARanchSlotActor* const Slot = SpawnSlot(World, 0);
	URanchHudWidget* const Hud = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
	if (!TestNotNull(TEXT("플레이어"), Player) || !TestNotNull(TEXT("칸"), Slot) || !TestNotNull(TEXT("HUD"), Hud))
	{
		DestroyTestWorld(World);
		return false;
	}
	Player->GetMeleeAttack()->SetAttackSettings(10.f, 300.f, 1.f); // 시험 값 — 칸이 사거리 안에 들게
	Slot->SetActorLocation(FVector(100.f, 0.f, 0.f));
	Hud->TakeWidget(); // 슬레이트를 먼저 만든다 — 묶은 뒤 만들면 묶음이 풀려 HUD 가 칸을 못 본다(게임도 화면에 올린 뒤 묶는다)
	Hud->BindToCharacter(Player);
	URanchComponent* const Ranch = Player->GetRanch();
	TArray<FRanchSlotState> Slots;
	Slots.SetNum(Ranch->GetSlotCount());
	Slots[0].AnimalID = TEXT("animal.pig");
	Slots[0].OccupiedSinceUtc = FDateTime::UtcNow();
	Ranch->ApplyBackendSnapshot(Slots);
	TestTrue(TEXT("0번 칸을 가리킨다"), Player->GetRanchInteract()->RefreshTarget() == Slot);

	UButton* const Remove = Cast<UButton>(Hud->WidgetTree->FindWidget(TEXT("RemovePetButton")));
	UButton* const Yes = Cast<UButton>(Hud->WidgetTree->FindWidget(TEXT("RemovePetConfirmButton")));
	UButton* const No = Cast<UButton>(Hud->WidgetTree->FindWidget(TEXT("RemovePetCancelButton")));
	UWidget* const Row = Hud->WidgetTree->FindWidget(TEXT("RemovePetConfirmRow"));
	if (!TestNotNull(TEXT("펫 빼기"), Remove) || !TestNotNull(TEXT("빼기"), Yes) || !TestNotNull(TEXT("취소"), No)
		|| !TestNotNull(TEXT("확인 줄"), Row))
	{
		DestroyTestWorld(World);
		return false;
	}
	TestTrue(TEXT("펫 빼기 켜짐"), Hud->IsRemovePetEnabled());
	TestFalse(TEXT("처음엔 확인 줄 접힘"), (Row->GetVisibility() == ESlateVisibility::Visible));

	Remove->OnClicked.Broadcast();
	TestTrue(TEXT("펫 빼기 → 확인 줄 펴짐"), Hud->IsRemovePetConfirmOpen() && (Row->GetVisibility() == ESlateVisibility::Visible));
	TestEqual(TEXT("확인 전 요청 0"), Ranch->GetPendingRequests().Num(), 0);

	No->OnClicked.Broadcast();
	TestFalse(TEXT("취소 → 접힘"), Hud->IsRemovePetConfirmOpen() || (Row->GetVisibility() == ESlateVisibility::Visible));
	Yes->OnClicked.Broadcast();
	TestEqual(TEXT("접힌 채 빼기는 무시"), Ranch->GetPendingRequests().Num(), 0);

	Remove->OnClicked.Broadcast();
	Slot->SetActorLocation(FVector(10000.f, 0.f, 0.f));
	Player->GetRanchInteract()->RefreshTarget();
	TestFalse(TEXT("대상 칸이 바뀌면 접힘"), Hud->IsRemovePetConfirmOpen());
	Slot->SetActorLocation(FVector(100.f, 0.f, 0.f));
	Player->GetRanchInteract()->RefreshTarget();
	Yes->OnClicked.Broadcast();
	TestEqual(TEXT("다시 가리켜도 확인 없이는 요청 0"), Ranch->GetPendingRequests().Num(), 0);

	Remove->OnClicked.Broadcast();
	Yes->OnClicked.Broadcast();
	const TArray<FRanchRequest>& Pending = Ranch->GetPendingRequests();
	TestEqual(TEXT("확인 뒤 요청 1"), Pending.Num(), 1);
	TestTrue(TEXT("요청 = RemovePet"), Pending.Num() == 1 && Pending[0].Kind == ERanchRequestKind::RemovePet);
	TestFalse(TEXT("빼기 뒤 접힘"), Hud->IsRemovePetConfirmOpen());
	TestTrue(TEXT("응답 전 칸 그대로"), !Ranch->IsSlotEmpty(0));

	AddInfo(FString::Printf(TEXT("[RanchHud] 펫 빼기 확인 - 요청 %d건 · 버튼 '%s'"), Pending.Num(), *Hud->GetRemovePetButtonText().ToString()));
	DestroyTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
