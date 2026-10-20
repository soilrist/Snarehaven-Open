// ROADMAP M13-12 2층 검사 — 위험구역·보물 상자(DESIGN 8.10 · 13.8)와 확률 고지(10.6).
// 이 검사가 잡는 것: ① 고지 표(코드 값)가 설계서 10.6 과 줄·순서·값이 어긋나는 것
// ② 상자 → 서버 요청 인자(`OpenChest{chest}` · `OpenChest{treasureId}` · `UseTreasureMap{}`)가 서버 규약과 어긋나는 것
// ③ **서버 답 전에 결과를 그리는 것** — 화면이 먼저 그리면 그게 곧 클라이언트가 굴린 전리품이다 (CLAUDE.md 8)
// ④ 답 읽기(`items`·`blueprints`·`drop`·`waitMs`·보물 자리)가 서버 모양과 어긋나는 것
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신한다. 검사 끝에 반드시 비운다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.Chest; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SceneComponent.h"
#include "Data/ZoneRow.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Inventory/BackpackComponent.h"
#include "Inventory/ChestLootRules.h"
#include "Inventory/DropBagActor.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "UI/ChestScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/SnareUiStyle.h"
#include "World/LootChestActor.h"

namespace SnareChestTest
{
	// 검사 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	constexpr float TestZoneRadiusCm = 1000.f;
	const TCHAR* const TestTreasureID = TEXT("treasure-test");

	TSharedPtr<FJsonObject> ParseJson(const FString& Text)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
		return Object;
	}

	/** 서버가 부른 것 한 건 — 이름·인자 전부와, 답을 나중에 줄 콜백. */
	struct FRecordedCall
	{
		FString FunctionName;
		TSharedPtr<FJsonObject> Args;
		USnareBackendSubsystem::FServerCallDone OnDone;
	};

	/** 게임 월드 + 백엔드 + 배낭 컴포넌트 + **답을 붙잡아 두는** 가짜 서버. */
	struct FRig
	{
		UWorld* World = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		UBackpackComponent* Backpack = nullptr;
		TArray<FRecordedCall> Calls;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());

			UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
			Backend = NewObject<USnareBackendSubsystem>(GameInstance);

			AActor* const Owner = World->SpawnActor<AActor>();
			USceneComponent* const Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Backpack = NewObject<UBackpackComponent>(Owner, TEXT("Backpack"));
			Backpack->RegisterComponent();
			Backpack->BindTargets(Backend, nullptr);

			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
				{
					Calls.Add({FunctionName, Args, MoveTemp(OnDone)});
				};
		}

		~FRig()
		{
			USnareBackendSubsystem::TestServerOverride = nullptr;
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		void Reply(const FString& Json)
		{
			FRecordedCall Call = Calls.Last();
			Call.OnDone(true, ParseJson(Json), FString());
		}
	};
}

using namespace SnareChestTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChestDisclosureTest, "Snarehaven.Chest.Disclosure.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenChestDisclosureTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	const int32 Start = Doc.Find(TEXT("### 10.6"));
	const int32 End = Doc.Find(TEXT("\n### "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 1);
	if (!TestTrue(TEXT("10.6 절이 있다"), Start != INDEX_NONE))
	{
		return false;
	}
	const FString Section = Doc.Mid(Start, End == INDEX_NONE ? MAX_int32 : End - Start);

	// `| 이름 | \`item.x\` | N% |` 줄만 — 머리·구분 줄은 ID 칸이 없다.
	TArray<TPair<FString, int32>> DesignLines;
	TArray<FString> Lines;
	Section.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), false);
		if (Cells.Num() < 4)
		{
			continue;
		}
		const FString ID = Cells[2].TrimStartAndEnd().Replace(TEXT("`"), TEXT(""));
		const FString Percent = Cells[3].TrimStartAndEnd();
		if (ID.StartsWith(TEXT("item.")) && Percent.EndsWith(TEXT("%")))
		{
			DesignLines.Add({ID, FCString::Atoi(*Percent.LeftChop(1))});
		}
	}

	const TConstArrayView<SnareChestLoot::FDisclosureLine> Code = SnareChestLoot::GetDisclosureLines();
	const SnareChestLoot::FDisclosureLine Blueprint = SnareChestLoot::GetBlueprintLine();
	TestEqual(TEXT("10.6 줄 수 = 22 + 설계도"), DesignLines.Num(), Code.Num() + 1);
	TestEqual(TEXT("전리품 22줄"), Code.Num(), 22);
	for (int32 Index = 0; Index < Code.Num() && Index < DesignLines.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("%d번째 줄 ID"), Index + 1), FString(Code[Index].ItemID), DesignLines[Index].Key);
		TestEqual(FString::Printf(TEXT("%s 확률(%%)"), Code[Index].ItemID),
			FMath::RoundToInt(Code[Index].Chance * 100.f), DesignLines[Index].Value);
	}
	if (DesignLines.Num() == Code.Num() + 1)
	{
		TestEqual(TEXT("설계도 줄 ID"), FString(Blueprint.ItemID), DesignLines.Last().Key);
		TestEqual(TEXT("설계도 확률(%)"), FMath::RoundToInt(Blueprint.Chance * 100.f), DesignLines.Last().Value);
	}
	TestEqual(TEXT("보물 지도 = 서버 TREASURE_ITEM"), SnareChestLoot::GetTreasureMapItemID(), FName(TEXT("item.treasure_map")));

	// 화면이 보이는 줄도 같은 수 — 줄을 빼먹고 그리면 고지가 아니다.
	FRig Rig;
	UChestScreenWidget* const Screen = CreateWidget<UChestScreenWidget>(Rig.World, UChestScreenWidget::StaticClass());
	const TArray<FText> Shown = Screen->BuildDisclosureTexts();
	TestEqual(TEXT("화면 고지 22줄"), Shown.Num(), DesignLines.Num());
	if (Shown.Num() > 0)
	{
		TestTrue(TEXT("첫 줄에 10%"), Shown[0].ToString().EndsWith(TEXT("10%")));
		TestTrue(TEXT("끝 줄은 설계도 20%"), Shown.Last().ToString().StartsWith(TEXT("설계도")) && Shown.Last().ToString().EndsWith(TEXT("20%")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChestRequestTest, "Snarehaven.Chest.Request.Args",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenChestRequestTest::RunTest(const FString& Parameters)
{
	FRig Rig;

	// 위험구역 옆 상자 1·2 — ±Y 로 반지름만큼, 두 번 불러도 한 벌.
	const FVector Center(100.f, 200.f, 0.f);
	TestEqual(TEXT("상자 둘을 세운다"), ALootChestActor::SpawnBesideZone(Rig.World, Center, TestZoneRadiusCm), 2);
	TestEqual(TEXT("다시 부르면 안 세운다"), ALootChestActor::SpawnBesideZone(Rig.World, Center, TestZoneRadiusCm), 0);
	ALootChestActor* ChestTwo = nullptr;
	int32 IndexSum = 0;
	for (TActorIterator<ALootChestActor> It(Rig.World); It; ++It)
	{
		IndexSum += It->GetChestIndex();
		const double ExpectedY = Center.Y + (It->GetChestIndex() == 1 ? -TestZoneRadiusCm : TestZoneRadiusCm);
		TestEqual(TEXT("상자는 구역 경계 위"), It->GetActorLocation().Y, ExpectedY);
		ChestTwo = It->GetChestIndex() == 2 ? *It : ChestTwo;
	}
	TestEqual(TEXT("번호는 1·2"), IndexSum, 3);
	if (!TestNotNull(TEXT("상자 2"), ChestTwo))
	{
		return false;
	}

	// 상자 화면 "열기" → `OpenChest{chest:2}`. 답 전에는 결과가 없고 다시 못 누른다.
	UChestScreenWidget* const Screen = CreateWidget<UChestScreenWidget>(Rig.World, UChestScreenWidget::StaticClass());
	Screen->BindToChest(Rig.Backpack, ChestTwo->GetChestIndex(), ChestTwo->GetTreasureID());
	TestTrue(TEXT("열기를 묻는다"), Screen->PressOpen());
	TestEqual(TEXT("한 번 불렀다"), Rig.Calls.Num(), 1);
	TestEqual(TEXT("함수 = OpenChest"), Rig.Calls.Last().FunctionName, FString(TEXT("OpenChest")));
	int32 ChestArg = 0;
	TestTrue(TEXT("chest 인자"), Rig.Calls.Last().Args->TryGetNumberField(TEXT("chest"), ChestArg));
	TestEqual(TEXT("chest = 2"), ChestArg, 2);
	TestFalse(TEXT("위험구역 상자는 treasureId 를 안 보낸다"), Rig.Calls.Last().Args->HasField(TEXT("treasureId")));
	TestTrue(TEXT("답 전에는 결과가 비었다"), Screen->GetResultText().IsEmpty());
	TestFalse(TEXT("기다리는 중엔 다시 안 묻는다"), Screen->PressOpen());
	TestEqual(TEXT("여전히 한 번"), Rig.Calls.Num(), 1);
	TestEqual(TEXT("답 전에는 다시 받지 않았다"), Rig.Backpack->GetRefetchCount(), 0);

	Rig.Reply(TEXT("{\"rolls\":1,\"items\":[{\"itemId\":\"item.salt\",\"count\":1}],\"blueprints\":[]}"));
	TestFalse(TEXT("답이 오면 대기가 풀린다"), Screen->IsRequestPending());
	TestTrue(TEXT("결과에 굴림 수"), Screen->GetResultText().ToString().Contains(TEXT("굴림 1번")));
	TestEqual(TEXT("성공 뒤 보유 상태를 다시 받는다"), Rig.Backpack->GetRefetchCount(), 1);

	// 재충전 중 — 서버가 준 남은 시간을 보인다.
	TestTrue(TEXT("다시 연다"), Screen->PressOpen());
	Rig.Reply(TEXT("{\"error\":\"상자가 다시 차는 중\",\"code\":\"recharging\",\"waitMs\":125000}"));
	TestEqual(TEXT("재충전 남은 시간"), Screen->GetResultText().ToString(), FString(TEXT("다시 차기까지 2:05")));
	TestEqual(TEXT("거절은 다시 받지 않는다"), Rig.Backpack->GetRefetchCount(), 1);

	// 가방이 모자라 `drop` 이 오면 발밑에 그 가방을 둔다.
	TestTrue(TEXT("세 번째 열기"), Screen->PressOpen());
	Rig.Reply(TEXT("{\"rolls\":1,\"items\":[{\"itemId\":\"item.dill\",\"count\":1}],\"blueprints\":[],")
		TEXT("\"drop\":{\"dropId\":\"drop-test\",\"items\":[{\"itemId\":\"item.dill\",\"count\":1}],\"expiresAtUtc\":\"2030-01-01T00:00:00.000Z\"}}"));
	const ADropBagActor* Bag = nullptr;
	for (TActorIterator<ADropBagActor> It(Rig.World); It; ++It)
	{
		Bag = *It;
	}
	TestTrue(TEXT("drop 가방이 섰다"), Bag != nullptr && Bag->GetDropId() == TEXT("drop-test") && Bag->GetItems().Num() == 1);
	TestTrue(TEXT("결과에 발밑 안내"), Screen->GetResultText().ToString().Contains(TEXT("발밑")));

	// 보물 지도 → `UseTreasureMap{}` → 답의 보물을 기억한다(검사 월드는 존 칸이 없어 상자는 안 선다).
	TestTrue(TEXT("지도를 쓴다"), Rig.Backpack->RequestUseTreasureMap());
	TestEqual(TEXT("함수 = UseTreasureMap"), Rig.Calls.Last().FunctionName, FString(TEXT("UseTreasureMap")));
	TestFalse(TEXT("답 전에는 보물이 없다"), Rig.Backpack->GetActiveTreasure().IsSet());
	Rig.Reply(FString::Printf(TEXT("{\"treasureId\":\"%s\",\"zoneId\":\"zone.forest\",\"u\":0.25,\"v\":0.75,\"expiresUtc\":\"2030-01-01T00:00:00.000Z\"}"),
		TestTreasureID));
	TestEqual(TEXT("보물을 기억한다"), Rig.Backpack->GetActiveTreasure().TreasureID, FString(TestTreasureID));
	TestNull(TEXT("다른 맵 칸이면 상자를 안 세운다"), Rig.Backpack->GetTreasureChest());

	// 보물 상자 → `OpenChest{treasureId}`. 열면 보물은 끝난다.
	TestFalse(TEXT("번호도 보물도 없으면 안 묻는다"), Rig.Backpack->RequestOpenChest(0, FString(), nullptr));
	TestTrue(TEXT("보물 상자를 연다"), Rig.Backpack->RequestOpenChest(0, TestTreasureID, nullptr));
	FString TreasureArg;
	TestTrue(TEXT("treasureId 인자"), Rig.Calls.Last().Args->TryGetStringField(TEXT("treasureId"), TreasureArg));
	TestEqual(TEXT("treasureId = 보물"), TreasureArg, FString(TestTreasureID));
	TestFalse(TEXT("보물 상자는 chest 를 안 보낸다"), Rig.Calls.Last().Args->HasField(TEXT("chest")));
	Rig.Reply(TEXT("{\"rolls\":2,\"items\":[],\"blueprints\":[]}"));
	TestFalse(TEXT("연 보물은 잊는다"), Rig.Backpack->GetActiveTreasure().IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChestReplyTest, "Snarehaven.Chest.Reply.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenChestReplyTest::RunTest(const FString& Parameters)
{
	const SnareChestLoot::FOpenResult Result = SnareChestLoot::ParseOpenChest(ParseJson(
		TEXT("{\"rolls\":3,\"items\":[{\"itemId\":\"item.ruby\",\"count\":1},{\"itemId\":\"item.salt\",\"count\":2}],")
		TEXT("\"blueprints\":[\"item.blueprint_test\"],\"drop\":{\"dropId\":\"d\",\"items\":[],\"expiresAtUtc\":\"x\"}}")));
	TestEqual(TEXT("굴림 수"), Result.Rolls, 3);
	TestEqual(TEXT("아이템 두 줄"), Result.Items.Num(), 2);
	TestTrue(TEXT("소금 2개"), Result.Items.Num() == 2 && Result.Items[1].ItemID == FName(TEXT("item.salt")) && Result.Items[1].Count == 2);
	TestEqual(TEXT("설계도 한 장"), Result.Blueprints.Num(), 1);
	TestTrue(TEXT("drop 이 있으면 발밑"), Result.bDropped);
	TestFalse(TEXT("drop 이 없으면 가방으로"),
		SnareChestLoot::ParseOpenChest(ParseJson(TEXT("{\"rolls\":1,\"items\":[],\"blueprints\":[]}"))).bDropped);

	TestEqual(TEXT("waitMs 125000 = 125초"), SnareChestLoot::ParseWaitSeconds(ParseJson(TEXT("{\"waitMs\":125000}"))), 125);
	TestEqual(TEXT("waitMs 는 올림"), SnareChestLoot::ParseWaitSeconds(ParseJson(TEXT("{\"waitMs\":124001}"))), 125);
	TestEqual(TEXT("waitMs 가 없으면 -1"), SnareChestLoot::ParseWaitSeconds(ParseJson(TEXT("{}"))), -1);
	TestEqual(TEXT("m:ss"), SnareChestLoot::FormatClock(125), FString(TEXT("2:05")));
	TestEqual(TEXT("600초 = 10:00"), SnareChestLoot::FormatClock(600), FString(TEXT("10:00")));

	SnareChestLoot::FTreasure Treasure;
	TestTrue(TEXT("보물 읽기"), SnareChestLoot::ParseTreasure(ParseJson(
		TEXT("{\"treasureId\":\"t\",\"zoneId\":\"zone.forest\",\"u\":0.25,\"v\":1,\"expiresUtc\":\"2030-01-01T00:10:00.000Z\"}")), Treasure));
	TestEqual(TEXT("존"), Treasure.ZoneID, FName(TEXT("zone.forest")));
	TestEqual(TEXT("마감"), Treasure.ExpiresUtc, FDateTime(2030, 1, 1, 0, 10, 0));
	SnareChestLoot::FTreasure Missing;
	TestFalse(TEXT("u 가 없으면 못 읽는다"), SnareChestLoot::ParseTreasure(ParseJson(
		TEXT("{\"treasureId\":\"t\",\"zoneId\":\"zone.forest\",\"v\":1,\"expiresUtc\":\"2030-01-01T00:10:00.000Z\"}")), Missing));
	TestFalse(TEXT("못 읽으면 빈 보물"), Missing.IsSet());

	// u → X(북), v → Y(동). 칸 가운데 (1000, 2000) · 반변 500.
	FZoneRow Zone;
	Zone.RegionCenterCm = FVector2D(1000.f, 2000.f);
	Zone.RegionHalfExtentCm = 500.f;
	TestEqual(TEXT("u·v 0.5 = 칸 가운데"), SnareChestLoot::TreasureWorldXY(Zone, 0.5f, 0.5f), FVector2D(1000.f, 2000.f));
	TestEqual(TEXT("u 0.25 · v 1 = 남쪽 1/4 · 동쪽 끝"), SnareChestLoot::TreasureWorldXY(Zone, Treasure.U, Treasure.V), FVector2D(750.f, 2500.f));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChestRestoreTest, "Snarehaven.Chest.Treasure.RestoreAfterZoneMove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M13-15 — 존을 옮기면 캐릭터(배낭)가 새로 서서 보물을 잊는다. 새 배낭은 `GetTreasure{}` 로 서버 기록을 다시 받는다.
// 잡는 것: 다른 함수·인자를 부르는 것 · 답 전에 보물을 지어내는 것 · 못 물었을 때 아는 보물을 지우는 것 ·
// 서버가 "없다(열었다·지났다)" 고 해도 남기는 것 · 보유가 안 바뀌는데 다시 받는 것.
bool FSnarehavenChestRestoreTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	TestTrue(TEXT("다시 받기를 묻는다"), Rig.Backpack->RequestRestoreTreasure());
	TestEqual(TEXT("함수 = GetTreasure"), Rig.Calls.Last().FunctionName, FString(TEXT("GetTreasure")));
	TestEqual(TEXT("인자는 비었다"), Rig.Calls.Last().Args->Values.Num(), 0);
	TestFalse(TEXT("답 전에는 보물이 없다"), Rig.Backpack->GetActiveTreasure().IsSet());

	Rig.Reply(FString::Printf(TEXT("{\"treasure\":{\"treasureId\":\"%s\",\"zoneId\":\"zone.forest\",\"u\":0.25,\"v\":0.75,")
		TEXT("\"expiresUtc\":\"2030-01-01T00:00:00.000Z\"}}"), TestTreasureID));
	TestEqual(TEXT("서버 기록의 보물을 다시 안다"), Rig.Backpack->GetActiveTreasure().TreasureID, FString(TestTreasureID));
	TestEqual(TEXT("같은 칸"), Rig.Backpack->GetActiveTreasure().ZoneID, FName(TEXT("zone.forest")));

	TestTrue(TEXT("또 묻는다"), Rig.Backpack->RequestRestoreTreasure());
	Rig.Calls.Last().OnDone(false, nullptr, TEXT("network"));
	TestTrue(TEXT("못 물었으면 아는 보물을 그대로 둔다"), Rig.Backpack->GetActiveTreasure().IsSet());

	TestTrue(TEXT("열고 난 뒤 묻는다"), Rig.Backpack->RequestRestoreTreasure());
	Rig.Reply(TEXT("{\"treasure\":null}"));
	TestFalse(TEXT("서버가 없다고 하면 잊는다"), Rig.Backpack->GetActiveTreasure().IsSet());
	TestNull(TEXT("상자도 없다"), Rig.Backpack->GetTreasureChest());
	TestEqual(TEXT("보유가 안 바뀌니 다시 받지 않는다"), Rig.Backpack->GetRefetchCount(), 0);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChestRestoreRetryTest, "Snarehaven.Chest.Treasure.HudRetriesAfterFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// M13-16 — HUD 의 다시받기가 실패하면 재물음 간격(DESIGN 13.8) 뒤 다시 묻고, 받아들여지면 더 안 묻는지.
// 잡는 것: 실패하면 영영 안 묻는 것(결함 원문) · 간격 전에 또 묻는 것 · 답을 기다리며 겹쳐 묻는 것 ·
// 성공 뒤에도 계속 묻는 것 · 실패에 아는 표식을 지우는 것 · 새 배낭(존 이동)에 안 묻는 것.
bool FSnarehavenChestRestoreRetryTest::RunTest(const FString& Parameters)
{
	constexpr double TestStartSeconds = 100.0; // 검사 전용 시각
	constexpr double TestEarlySeconds = 0.5;   // 간격보다 이만큼 이르게

	FRig Rig;
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(Rig.World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("HUD 생성"), Hud))
	{
		return false;
	}
	const double Retry = UHuntHudWidget::GetTreasureRestoreRetrySeconds();
	TestTrue(TEXT("재물음 간격이 정해져 있다"), Retry > 0.0);

	// 이미 아는 보물 — 실패해도 이것을 지우면 안 된다(M13-15 규칙).
	Rig.Backpack->RequestRestoreTreasure();
	Rig.Reply(FString::Printf(TEXT("{\"treasure\":{\"treasureId\":\"%s\",\"zoneId\":\"zone.forest\",\"u\":0.25,\"v\":0.75,")
		TEXT("\"expiresUtc\":\"2030-01-01T00:00:00.000Z\"}}"), TestTreasureID));
	const int32 Before = Rig.Calls.Num();

	Hud->UpdateTreasureRestore(Rig.Backpack, TestStartSeconds);
	TestEqual(TEXT("처음엔 바로 묻는다"), Rig.Calls.Num(), Before + 1);
	TestEqual(TEXT("함수 = GetTreasure"), Rig.Calls.Last().FunctionName, FString(TEXT("GetTreasure")));
	Hud->UpdateTreasureRestore(Rig.Backpack, TestStartSeconds + Retry);
	TestEqual(TEXT("답을 기다리는 동안은 겹쳐 묻지 않는다"), Rig.Calls.Num(), Before + 1);

	Rig.Calls.Last().OnDone(false, nullptr, TEXT("network"));
	TestTrue(TEXT("실패해도 아는 보물은 그대로"), Rig.Backpack->GetActiveTreasure().IsSet());
	Hud->UpdateTreasureRestore(Rig.Backpack, TestStartSeconds + Retry - TestEarlySeconds);
	TestEqual(TEXT("간격 전엔 다시 안 묻는다"), Rig.Calls.Num(), Before + 1);
	Hud->UpdateTreasureRestore(Rig.Backpack, TestStartSeconds + Retry);
	TestEqual(TEXT("간격이 지나면 다시 묻는다"), Rig.Calls.Num(), Before + 2);

	// 서버 거절(코드 있는 답)도 실패다 — 다시 묻는다.
	Rig.Reply(TEXT("{\"error\":\"busy\"}"));
	Hud->UpdateTreasureRestore(Rig.Backpack, TestStartSeconds + Retry * 2.0);
	TestEqual(TEXT("거절 뒤에도 간격 뒤 다시 묻는다"), Rig.Calls.Num(), Before + 3);

	Rig.Reply(FString::Printf(TEXT("{\"treasure\":{\"treasureId\":\"%s\",\"zoneId\":\"zone.forest\",\"u\":0.25,\"v\":0.75,")
		TEXT("\"expiresUtc\":\"2030-01-01T00:00:00.000Z\"}}"), TestTreasureID));
	Hud->UpdateTreasureRestore(Rig.Backpack, TestStartSeconds + Retry * 10.0);
	TestEqual(TEXT("받아들여지면 더 안 묻는다"), Rig.Calls.Num(), Before + 3);

	// 존을 옮기면 새 배낭 — 처음부터 다시 묻는다.
	UBackpackComponent* const NewBackpack = NewObject<UBackpackComponent>(Rig.Backpack->GetOwner(), TEXT("NewBackpack"));
	NewBackpack->RegisterComponent();
	NewBackpack->BindTargets(Rig.Backend, nullptr);
	Hud->UpdateTreasureRestore(NewBackpack, TestStartSeconds + Retry * 10.0);
	TestEqual(TEXT("새 배낭엔 다시 묻는다"), Rig.Calls.Num(), Before + 4);
	return true;
}

// ROADMAP M25-05 — 상자 판이 배낭 판 규격(크림·둥근 16)이고, 제목이 판 안 글자가 아니라 판 왼쪽 위 모서리에 걸쳐
// 위로 나온 진갈색 알약인가(시트 `inventory_backpack.md` 「제목 탭」). 고지를 켜면 알약 글이 고지 제목으로 바뀌고,
// 판이 닫히면 알약도 접혀야 한다 — 닫힌 상자 옆 카드 위에 제목만 떠 있으면 안 된다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChestPanelTitleTest, "Snarehaven.Chest.Screen.CreamPanelTitlePill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenChestPanelTitleTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	UChestScreenWidget* const Screen = CreateWidget<UChestScreenWidget>(Rig.World, UChestScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상자 화면 위젯"), Screen))
	{
		return false;
	}
	Screen->TakeWidget();
	UWidgetTree* const Tree = Screen->WidgetTree;
	UBorder* const Panel = Tree->FindWidget<UBorder>(TEXT("ChestPanel"));
	const UBorder* const Pill = Tree->FindWidget<UBorder>(TEXT("ChestTitlePill"));
	USizeBox* const Box = Tree->FindWidget<USizeBox>(TEXT("ChestTitlePillBox"));
	UTextBlock* const Title = Tree->FindWidget<UTextBlock>(TEXT("TitleText"));
	const UCanvasPanelSlot* const PanelSlot = Panel != nullptr ? Cast<UCanvasPanelSlot>(Panel->Slot) : nullptr;
	const UCanvasPanelSlot* const BoxSlot = Box != nullptr ? Cast<UCanvasPanelSlot>(Box->Slot) : nullptr;
	if (!TestNotNull(TEXT("판 칸"), PanelSlot) || !TestNotNull(TEXT("제목 알약"), Pill) || !TestNotNull(TEXT("알약 상자 칸"), BoxSlot)
		|| !TestNotNull(TEXT("제목 글자"), Title))
	{
		return false;
	}

	// 판 = 크림 둥근 16.
	const FSlateBrush& PanelBrush = Panel->Background;
	TestEqual(TEXT("판 모양은 둥근 네모"), static_cast<int32>(PanelBrush.DrawAs), static_cast<int32>(ESlateBrushDrawType::RoundedBox));
	TestTrue(TEXT("판 모서리 16"), PanelBrush.OutlineSettings.CornerRadii == FVector4(16.f, 16.f, 16.f, 16.f));
	TestTrue(TEXT("판 색 = UiPanelCream"), PanelBrush.TintColor.GetSpecifiedColor() * Panel->GetBrushColor() == SnareUiStyle::UiPanelCream());

	// 알약 = 판과 같은 캔버스에서 판 뒤, 왼쪽 위 모서리에 걸쳐 위로 `MenuTitlePillRise`.
	const UPanelWidget* const Layer = Box->GetParent();
	const FVector2D PanelTopLeft = PanelSlot->GetPosition() - PanelSlot->GetAlignment() * PanelSlot->GetSize();
	const FVector2D PillTopLeft = BoxSlot->GetPosition() - BoxSlot->GetAlignment() * Box->GetDesiredSize();
	TestTrue(TEXT("제목 글자 부모 = 알약 · 알약 부모 = 상자"), Title->GetParent() == Pill && Pill->GetParent() == Box);
	TestTrue(TEXT("알약 색 = UiTitleTab"), Pill->Background.TintColor.GetSpecifiedColor() == SnareUiStyle::UiTitleTab());
	TestTrue(TEXT("알약은 판과 같은 겹에서 판 뒤(위에 그림)"), Layer != nullptr && Layer == Panel->GetParent()
		&& Layer->GetChildIndex(Box) > Layer->GetChildIndex(Panel));
	TestTrue(TEXT("알약 닻 = 판 닻"), BoxSlot->GetAnchors().Minimum == PanelSlot->GetAnchors().Minimum
		&& BoxSlot->GetAnchors().Maximum == PanelSlot->GetAnchors().Maximum);
	TestEqual(TEXT("알약 왼 변 = 판 왼 변"), PillTopLeft.X, PanelTopLeft.X);
	TestEqual(TEXT("알약 위 변 = 판 위 변 - MenuTitlePillRise"), PillTopLeft.Y, PanelTopLeft.Y - SnareUiStyle::MenuTitlePillRise);
	TestTrue(TEXT("알약이 판에 걸친다(높이 > 나온 몫)"), Box->GetHeightOverride() > SnareUiStyle::MenuTitlePillRise);
	TestFalse(TEXT("판 안에 제목 글자가 남지 않는다"), Title->IsChildOf(Panel));

	// 닫힘 → 알약 접힘 · 열림 → 보임 · 고지 켜면 알약 글 = 고지 제목.
	Screen->SetOpen(false);
	TestEqual(TEXT("닫으면 알약 접힘"), static_cast<int32>(Box->GetVisibility()), static_cast<int32>(ESlateVisibility::Collapsed));
	Screen->SetOpen(true);
	// `IsVisible()` 은 화면에 올라간 위젯만 참이라 뷰포트 밖 시험에선 늘 거짓이다 — 설정한 가시성 값으로 본다.
	TestEqual(TEXT("열면 알약 보임(눌림 안 먹음)"), static_cast<int32>(Box->GetVisibility()), static_cast<int32>(ESlateVisibility::HitTestInvisible));
	const FString ClosedTitle = Title->GetText().ToString();
	TestEqual(TEXT("위험구역 상자 제목"), ClosedTitle, FString(TEXT("상자")));
	Screen->SetDisclosureShown(true);
	TestEqual(TEXT("고지 켜면 알약 글 = 고지 제목"), Title->GetText().ToString(), FString(TEXT("확률 고지 (한 번 굴릴 때)")));

	AddInfo(FString::Printf(TEXT("[ChestTitle] 알약 (%.0f, %.0f) · 판 (%.0f, %.0f) · 판 모서리 %.0f · 알약 높이 %.0f · 제목 %s → %s"),
		PillTopLeft.X, PillTopLeft.Y, PanelTopLeft.X, PanelTopLeft.Y, PanelBrush.OutlineSettings.CornerRadii.X,
		Box->GetHeightOverride(), *ClosedTitle, *Title->GetText().ToString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChestSyncFailureTest, "Snarehaven.Chest.Screen.SyncFailureNotStuck",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenChestSyncFailureTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	UChestScreenWidget* const Screen = CreateWidget<UChestScreenWidget>(Rig.World, UChestScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상자 화면 위젯"), Screen))
	{
		return false;
	}
	Screen->TakeWidget();
	const UTextBlock* const Body = Screen->WidgetTree->FindWidget<UTextBlock>(TEXT("ResultBody"));
	const UButton* const Open = Screen->WidgetTree->FindWidget<UButton>(TEXT("OpenButton"));
	if (!TestNotNull(TEXT("결과 글"), Body) || !TestNotNull(TEXT("열기 버튼"), Open))
	{
		return false;
	}
	Screen->BindToChest(Rig.Backpack, 1, FString());
	const FString PendingText = TEXT("상자 여는 중...");
	const FString TestFailure = TEXT("시험용 실패 문장");

	// 늦게 오는 답 — 지금처럼 대기 → 결과.
	TestTrue(TEXT("늦은 답: 열기를 묻는다"), Screen->PressOpen());
	TestTrue(TEXT("늦은 답: 기다리는 중"), Screen->IsRequestPending());
	TestEqual(TEXT("늦은 답: 판 글 = 여는 중"), Body->GetText().ToString(), PendingText);
	TestFalse(TEXT("늦은 답: 열기 버튼 꺼짐"), Open->GetIsEnabled());
	Rig.Calls.Last().OnDone(false, nullptr, TestFailure);
	TestFalse(TEXT("늦은 답 뒤: 대기 풀림"), Screen->IsRequestPending());
	TestNotEqual(TEXT("늦은 답 뒤: 판 글 ≠ 여는 중"), Body->GetText().ToString(), PendingText);

	// 같은 호출 안에서 실패 답을 주는 가짜 서버(로그인 전과 같은 길).
	USnareBackendSubsystem::TestServerOverride =
		[&Rig, TestFailure](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			Rig.Calls.Add({FunctionName, Args, nullptr});
			OnDone(false, nullptr, TestFailure);
		};
	const int32 CallsBefore = Rig.Calls.Num();
	TestTrue(TEXT("바로 답: 열기를 물었다"), Screen->PressOpen());
	TestEqual(TEXT("바로 답: 한 번 불렀다"), Rig.Calls.Num(), CallsBefore + 1);
	TestFalse(TEXT("바로 답: 대기에 멈추지 않는다"), Screen->IsRequestPending());
	TestNotEqual(TEXT("바로 답: 판 글 ≠ 여는 중"), Body->GetText().ToString(), PendingText);
	TestFalse(TEXT("바로 답: 실패 글이 남는다"), Screen->GetResultText().IsEmpty());
	TestTrue(TEXT("바로 답: 열기 버튼 켜짐"), Open->GetIsEnabled());
	TestTrue(TEXT("바로 답: 다시 누르면 또 묻는다"), Screen->PressOpen());
	TestEqual(TEXT("바로 답: 두 번째 부름"), Rig.Calls.Num(), CallsBefore + 2);

	AddInfo(FString::Printf(TEXT("[ChestSync] 바로 답 뒤 대기 %d · 판 글 \"%s\" · 열기 버튼 %d"),
		Screen->IsRequestPending() ? 1 : 0, *Body->GetText().ToString(), Open->GetIsEnabled() ? 1 : 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenChestSoundTest, "Snarehaven.Chest.Sound.OpenOnlyWhenAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenChestSoundTest::RunTest(const FString& Parameters)
{
	// DESIGN 8.12 「가방·상자 소리」 (M49-05) — 상자 열기는 받아들인 답 한 번에 `ChestOpen` 한 번, 거절·네트워크 오류는 0.
	FRig Rig;
	const FName ChestCue(TEXT("ChestOpen"));
	UChestScreenWidget* const Screen = CreateWidget<UChestScreenWidget>(Rig.World, UChestScreenWidget::StaticClass());
	Screen->BindToChest(Rig.Backpack, 1, FString());

	TestTrue(TEXT("열기를 묻는다"), Screen->PressOpen());
	TestEqual(TEXT("답 전 소리 0"), Rig.Backpack->GetBagCueCount(), 0);
	Rig.Reply(TEXT("{\"error\":\"상자가 다시 차는 중\",\"code\":\"recharging\",\"waitMs\":125000}"));
	TestEqual(TEXT("recharging → 소리 0"), Rig.Backpack->GetBagCueCount(), 0);
	TestTrue(TEXT("다시 연다"), Screen->PressOpen());
	Rig.Calls.Last().OnDone(false, nullptr, TEXT("network"));
	TestEqual(TEXT("네트워크 오류 → 소리 0"), Rig.Backpack->GetBagCueCount(), 0);

	// 위험구역 상자 받아들임 — 아이템이 여럿이어도 한 번.
	TestTrue(TEXT("세 번째 열기"), Screen->PressOpen());
	Rig.Reply(TEXT("{\"rolls\":2,\"items\":[{\"itemId\":\"item.salt\",\"count\":1},{\"itemId\":\"item.dill\",\"count\":2}],\"blueprints\":[]}"));
	TestEqual(TEXT("받아들임 → 소리 1"), Rig.Backpack->GetBagCueCount(), 1);
	TestEqual(TEXT("상자 소리 = ChestOpen"), Rig.Backpack->GetLastBagCue(), ChestCue);

	// 보물 상자도 같은 길.
	TestTrue(TEXT("보물 상자를 연다"), Rig.Backpack->RequestOpenChest(0, TestTreasureID, nullptr));
	Rig.Reply(TEXT("{\"error\":\"보물이 사라졌다\",\"code\":\"expired\"}"));
	TestEqual(TEXT("보물 expired → 소리 그대로"), Rig.Backpack->GetBagCueCount(), 1);
	TestTrue(TEXT("보물 상자를 다시 연다"), Rig.Backpack->RequestOpenChest(0, TestTreasureID, nullptr));
	Rig.Reply(TEXT("{\"rolls\":1,\"items\":[],\"blueprints\":[]}"));
	TestEqual(TEXT("보물 받아들임 → 소리 2"), Rig.Backpack->GetBagCueCount(), 2);
	TestEqual(TEXT("보물 상자 소리 = ChestOpen"), Rig.Backpack->GetLastBagCue(), ChestCue);

	AddInfo(FString::Printf(TEXT("[Sound] 상자 - 응답 5 (받아들임 2) → 소리 %d"), Rig.Backpack->GetBagCueCount()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenVaultDisclosureTest, "Snarehaven.Vault.Disclosure.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenVaultDisclosureTest::RunTest(const FString& Parameters)
{
	FString Doc;
	if (!TestTrue(TEXT("설계서를 읽는다"), FFileHelper::LoadFileToString(Doc, *(FPaths::ProjectDir() / TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	const int32 Start = Doc.Find(TEXT("### 8.1c"));
	const int32 End = Doc.Find(TEXT("\n### "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Start + 1);
	if (!TestTrue(TEXT("8.1c 절이 있다"), Start != INDEX_NONE))
	{
		return false;
	}
	const FString Section = Doc.Mid(Start, End == INDEX_NONE ? MAX_int32 : End - Start);

	// 전리품 표 줄: `| 이름 | \`item.x\` | N% | 근거 |` (한 줄에 ID 여럿이면 건너뛴다 — 아래에서 따로 센다).
	TArray<TPair<FString, int32>> DesignLines;
	TArray<FString> Lines;
	Section.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		TArray<FString> Cells;
		Line.ParseIntoArray(Cells, TEXT("|"), false);
		if (Cells.Num() < 4)
		{
			continue;
		}
		const FString ID = Cells[2].TrimStartAndEnd().Replace(TEXT("`"), TEXT(""));
		const FString Percent = Cells[3].TrimStartAndEnd();
		if (ID.StartsWith(TEXT("item.")) && Percent.EndsWith(TEXT("%")) && !ID.Contains(TEXT("·")))
		{
			DesignLines.Add({ID, FCString::Atoi(*Percent.LeftChop(1))});
		}
	}

	const TConstArrayView<SnareChestLoot::FDisclosureLine> Code = SnareChestLoot::GetVaultDisclosureLines();
	const SnareChestLoot::FDisclosureLine Blueprint = SnareChestLoot::GetVaultBlueprintLine();
	TestEqual(TEXT("코드 15줄"), Code.Num(), 15);
	TestTrue(TEXT("설계서 표에 줄이 있다"), DesignLines.Num() > 0);
	for (const SnareChestLoot::FDisclosureLine& Line : Code)
	{
		// 햇빛 집열판·배터리·전구는 설계서에서 한 줄(`·`)이라 따로 찾는다.
		const FString ID(Line.ItemID);
		const int32 Percent = FMath::RoundToInt(Line.Chance * 100.f);
		bool bFound = false;
		for (const TPair<FString, int32>& Design : DesignLines)
		{
			bFound |= Design.Key == ID && Design.Value == Percent;
		}
		if (!bFound)
		{
			const FString Needle = FString::Printf(TEXT("`%s`"), *ID);
			bFound = Section.Contains(Needle) && Section.Contains(FString::Printf(TEXT("%d%%"), Percent));
		}
		TestTrue(FString::Printf(TEXT("%s %d%% 가 8.1c 에 있다"), *ID, Percent), bFound);
	}
	bool bBlueprintFound = false;
	for (const TPair<FString, int32>& Design : DesignLines)
	{
		bBlueprintFound |= Design.Key == TEXT("item.blueprint_*") && Design.Value == FMath::RoundToInt(Blueprint.Chance * 100.f);
	}
	TestTrue(TEXT("설계도 줄 30%"), bBlueprintFound);

	// 서버 함수와 같은 모양으로 묻는다.
	FRig Rig;
	TestFalse(TEXT("폐허 번호 0 은 안 묻는다"), Rig.Backpack->RequestOpenVault(0, nullptr));
	TestEqual(TEXT("안 불렀다"), Rig.Calls.Num(), 0);
	TestTrue(TEXT("금고를 연다"), Rig.Backpack->RequestOpenVault(2, nullptr));
	TestEqual(TEXT("함수 = OpenVault"), Rig.Calls.Last().FunctionName, FString(TEXT("OpenVault")));
	int32 RuinArg = 0;
	TestTrue(TEXT("ruin 인자"), Rig.Calls.Last().Args->TryGetNumberField(TEXT("ruin"), RuinArg));
	TestEqual(TEXT("ruin = 2"), RuinArg, 2);
	TestEqual(TEXT("답 전에는 다시 받지 않았다"), Rig.Backpack->GetRefetchCount(), 0);
	Rig.Reply(TEXT("{\"rolls\":3,\"items\":[{\"itemId\":\"item.ruby\",\"count\":1}],\"blueprints\":[]}"));
	TestEqual(TEXT("성공 뒤 보유 상태를 다시 받는다"), Rig.Backpack->GetRefetchCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenVaultScreenTest, "Snarehaven.Vault.Screen.OpensVaultAndShowsReasons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenVaultScreenTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	UChestScreenWidget* const Screen = CreateWidget<UChestScreenWidget>(Rig.World, UChestScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상자 화면 위젯"), Screen))
	{
		return false;
	}
	Screen->TakeWidget();
	const UTextBlock* const Title = Screen->WidgetTree->FindWidget<UTextBlock>(TEXT("TitleText"));
	if (!TestNotNull(TEXT("제목 글"), Title))
	{
		return false;
	}
	Screen->BindToVault(Rig.Backpack, 3);
	TestTrue(TEXT("금고 화면"), Screen->IsVault());
	TestEqual(TEXT("제목 = 잠긴 금고"), Title->GetText().ToString(), FString(TEXT("잠긴 금고")));
	TestEqual(TEXT("고지 표 = 금고 15줄 + 설계도"),
		Screen->BuildDisclosureTexts().Num(), SnareChestLoot::GetVaultDisclosureLines().Num() + 1);
	TestTrue(TEXT("고지 마지막 줄 30%"), Screen->BuildDisclosureTexts().Last().ToString().EndsWith(TEXT("30%")));

	TestTrue(TEXT("열기를 묻는다"), Screen->PressOpen());
	TestEqual(TEXT("함수 = OpenVault"), Rig.Calls.Last().FunctionName, FString(TEXT("OpenVault")));
	int32 RuinArg = 0;
	TestTrue(TEXT("ruin 인자"), Rig.Calls.Last().Args->TryGetNumberField(TEXT("ruin"), RuinArg));
	TestEqual(TEXT("ruin = 3"), RuinArg, 3);
	TestEqual(TEXT("답 전에는 결과가 비어 있다"), Screen->GetResultText().ToString(), FString());

	Rig.Calls.Last().OnDone(true, ParseJson(TEXT("{\"error\":\"x\",\"code\":\"no_key\"}")), FString());
	TestFalse(TEXT("no_key: 대기 풀림"), Screen->IsRequestPending());
	TestTrue(TEXT("no_key: 열쇠패 문구"), Screen->GetResultText().ToString().Contains(TEXT("열쇠패")));

	TestTrue(TEXT("다시 묻는다"), Screen->PressOpen());
	Screen->ApplyReply(false, TEXT("recharging"), TEXT("x"), ParseJson(TEXT("{\"waitMs\":90000000}")));
	TestEqual(TEXT("recharging: 25시간 = 25시간 0분"), Screen->GetResultText().ToString(), FString(TEXT("다시 차기까지 25시간 0분")));

	Screen->ApplyReply(true, FString(), FString(), ParseJson(TEXT("{\"rolls\":3,\"items\":[],\"blueprints\":[]}")));
	TestTrue(TEXT("성공: 굴림 3번 줄"), Screen->GetResultText().ToString().Contains(TEXT("굴림 3번")));

	// 상자 화면은 그대로 상자 — 금고 표로 번지지 않는다.
	UChestScreenWidget* const Plain = CreateWidget<UChestScreenWidget>(Rig.World, UChestScreenWidget::StaticClass());
	if (TestNotNull(TEXT("상자 화면 위젯 2"), Plain))
	{
		Plain->BindToChest(Rig.Backpack, 1, FString());
		TestFalse(TEXT("상자는 금고 아님"), Plain->IsVault());
		TestEqual(TEXT("상자 고지 표 줄 수"), Plain->BuildDisclosureTexts().Num(), SnareChestLoot::GetDisclosureLines().Num() + 1);
	}
	return true;
}

#endif
