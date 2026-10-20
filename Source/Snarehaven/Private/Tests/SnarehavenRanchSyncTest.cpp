// 목장·낚시터 전송기(`URanchSyncComponent`) 2층 검사 — 칸은 서버가 받아 준 뒤 다시 받은 값으로만 바뀌는가,
// 거절이면 그대로인가, 재시작 복원(`GetBaseState`)이 목장 칸·텐트·배에 들어가는가 (BACKEND.md 함수 표).
// 가짜 서버(`USnareBackendSubsystem::TestServerOverride`)를 꽂아 진짜 응답과 같은 길을 태운다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.RanchSync; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Data/ItemRowLookup.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UI/RanchHudWidget.h"
#include "UI/SnareNoticeText.h"
#include "Building/SnareBaseSubsystem.h"
#include "Building/SnareBuildRules.h"
#include "Engine/GameInstance.h"
#include "Inventory/BackpackComponent.h"
#include "Ranch/RanchComponent.h"
#include "Ranch/RanchSyncComponent.h"
#include "Serialization/JsonReader.h"
#include "Sound/SoundBase.h"
#include "Serialization/JsonSerializer.h"
#include "World/WaterTravelComponent.h"

namespace
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	const FName TestAnimal = TEXT("animal.pig");
	const FName TestFish = TEXT("item.crucian_carp");
	constexpr int32 TestSlot = 0;
	constexpr int32 TestStock = 2;
	constexpr int32 TestRestoredSlot = 2;
	constexpr int32 TestRestoredStock = 3;

	using FDone = USnareBackendSubsystem::FServerCallDone;

	struct FFakeServer
	{
		TArray<FString> Functions;
		TArray<TSharedPtr<FJsonObject>> Args;
		TArray<FDone> Waiting;
		TArray<FString> WaitingNames;

		void Install()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& InArgs, FDone OnDone)
				{
					Functions.Add(Name);
					Args.Add(InArgs);
					Waiting.Add(MoveTemp(OnDone));
					WaitingNames.Add(Name);
				};
		}

		~FFakeServer() { USnareBackendSubsystem::TestServerOverride = nullptr; }

		int32 CountCalls(const FString& Name) const
		{
			return Functions.FilterByPredicate([&Name](const FString& F) { return F == Name; }).Num();
		}

		/** 기다리는 맨 앞 요청에 답한다. 없으면 아무것도 안 한다 — 배열 밖을 읽어 에디터가 죽으면 결과가 안 남는다. */
		void Answer(bool bOk, const TSharedPtr<FJsonObject>& Response)
		{
			if (Waiting.IsEmpty())
			{
				return;
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			WaitingNames.RemoveAt(0);
			Done(bOk, Response, bOk ? FString() : TEXT("테스트 네트워크 끊김"));
		}

		/** 기다리는 것 중 그 함수의 맨 앞 요청에 답한다. 없으면 false. */
		bool AnswerNamed(const FString& Name, bool bOk, const TSharedPtr<FJsonObject>& Response)
		{
			const int32 Index = WaitingNames.IndexOfByKey(Name);
			if (Index == INDEX_NONE)
			{
				return false;
			}
			FDone Done = MoveTemp(Waiting[Index]);
			Waiting.RemoveAt(Index);
			WaitingNames.RemoveAt(Index);
			Done(bOk, Response, bOk ? FString() : TEXT("테스트 네트워크 끊김"));
			return true;
		}
	};

	struct FRig
	{
		URanchComponent* Ranch = nullptr;
		URanchComponent* Fishing = nullptr;
		USnareBaseSubsystem* Base = nullptr;
		UWaterTravelComponent* Water = nullptr;
		URanchSyncComponent* Sync = nullptr;

		FRig()
		{
			UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
			USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
			Base = NewObject<USnareBaseSubsystem>(GameInstance);
			Ranch = NewObject<URanchComponent>(GetTransientPackage());
			Fishing = NewObject<URanchComponent>(GetTransientPackage());
			Fishing->SetStationKind(ERanchStationKind::Fishing);
			Water = NewObject<UWaterTravelComponent>(GetTransientPackage());
			Sync = NewObject<URanchSyncComponent>(GetTransientPackage());
			Sync->BindTargets(Ranch, Fishing, Backend, Base, Water);
		}

		/** 0번 칸에 펫이 있고 재고가 쌓인 목장 — 수확할 거리가 있어야 수확 요청이 쌓인다. */
		void SeedRanch()
		{
			TArray<FRanchSlotState> Slots;
			Slots.SetNum(Ranch->GetSlotCount());
			Slots[TestSlot].AnimalID = TestAnimal;
			Slots[TestSlot].OccupiedSinceUtc = FDateTime::UtcNow();
			Slots[TestSlot].Stock = TestStock;
			Ranch->ApplyBackendSnapshot(Slots);
		}
	};

	TSharedPtr<FJsonObject> MakeFail(const FString& Code)
	{
		const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("error"), TEXT("거절"));
		Response->SetStringField(TEXT("code"), Code);
		return Response;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncHarvestTest, "Snarehaven.RanchSync.Harvest.SendsOnceAndWaitsForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSyncHarvestTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.SeedRanch();

	TestTrue(TEXT("수확 요청이 쌓인다"), Rig.Ranch->QueueHarvest(TestSlot));
	const FString Harvest = URanchSyncComponent::GetFunctionName(ERanchStationKind::Ranch, ERanchRequestKind::Harvest);
	TestEqual(TEXT("부른 함수 = HarvestRanch"), Harvest, FString(TEXT("HarvestRanch")));
	TestEqual(TEXT("누르자마자 HarvestRanch 한 번"), Server.CountCalls(Harvest), 1);
	TestEqual(TEXT("인자 slot"), Server.Args.IsEmpty() ? -1 : static_cast<int32>(Server.Args[0]->GetNumberField(TEXT("slot"))), TestSlot);
	TestFalse(TEXT("보내는 중엔 또 안 보낸다"), Rig.Sync->TrySendNext());

	// 응답 전: 칸 그대로.
	TestEqual(TEXT("응답 전 재고 그대로"), Rig.Ranch->GetSlot(TestSlot)->Stock, TestStock);

	const TSharedPtr<FJsonObject> Ok = MakeShared<FJsonObject>();
	Ok->SetNumberField(TEXT("harvested"), TestStock);
	Server.Answer(true, Ok);
	TestEqual(TEXT("성공 뒤 보유 상태를 다시 받는다"), Rig.Sync->GetRefetchCount(), 1);
	TestEqual(TEXT("성공 뒤 GetBaseState 로 칸을 다시 묻는다"), Server.CountCalls(URanchSyncComponent::GetBaseStateFunctionName()), 1);
	TestEqual(TEXT("다시 받기 전엔 칸 그대로(클라이언트가 재고를 안 깎는다)"), Rig.Ranch->GetSlot(TestSlot)->Stock, TestStock);
	TestEqual(TEXT("HarvestRanch 는 여전히 한 번"), Server.CountCalls(Harvest), 1);
	TestFalse(TEXT("보낸 요청은 대기열에서 빠진다"), Rig.Ranch->HasPendingRequest(TestSlot));
	AddInfo(FString::Printf(TEXT("[RanchSync] 수확 - 호출 %d번(%s) · 다시받기 %d"),
		Server.Functions.Num(), *FString::Join(Server.Functions, TEXT(",")), Rig.Sync->GetRefetchCount()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncHarvestPopupTest, "Snarehaven.RanchSync.Harvest.PopupAndSoundAfterServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSyncHarvestPopupTest::RunTest(const FString& Parameters)
{
	// M21-03 — 수확 팝업·코인 소리는 서버가 확정한 답 뒤에만, 서버가 준 수 그대로.
	constexpr int32 TestCoins = 7;
	const FString TestProduct = TEXT("item.truffle");
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.SeedRanch();

	TestEqual(TEXT("코인 있음"), URanchSyncComponent::FormatHarvestPopup(FText::FromString(TEXT("가")), 3, 4).ToString(),
		FString(TEXT("+3 가 / +4 코인")));
	TestEqual(TEXT("코인 0 이면 앞 절만"), URanchSyncComponent::FormatHarvestPopup(FText::FromString(TEXT("가")), 3, 0).ToString(),
		FString(TEXT("+3 가")));
	TestTrue(TEXT("0개면 빈 글"), URanchSyncComponent::FormatHarvestPopup(FText::FromString(TEXT("가")), 0, 4).IsEmpty());
	TestTrue(TEXT("작물 수확도 수확 함수"), URanchSyncComponent::IsHarvestFunction(TEXT("HarvestCrop")));
	TestTrue(TEXT("낚시터 수확도 수확 함수"), URanchSyncComponent::IsHarvestFunction(TEXT("HarvestFishing")));
	TestFalse(TEXT("배치는 수확 아님"), URanchSyncComponent::IsHarvestFunction(TEXT("RegisterPet")));

	Rig.Ranch->QueueHarvest(TestSlot);
	TestTrue(TEXT("누른 순간(응답 전)엔 팝업 없음"), Rig.Sync->GetLastHarvestPopup().IsEmpty());
	TestEqual(TEXT("응답 전엔 소리 없음"), Rig.Sync->GetLastPlayedCue(), FName(NAME_None));

	const TSharedPtr<FJsonObject> Ok = MakeShared<FJsonObject>();
	Ok->SetNumberField(TEXT("harvested"), TestStock);
	Ok->SetStringField(TEXT("itemId"), TestProduct);
	Ok->SetNumberField(TEXT("coins"), TestCoins);
	TestTrue(TEXT("첫 수확 답"), Server.AnswerNamed(TEXT("HarvestRanch"), true, Ok));
	const FString Popup = Rig.Sync->GetLastHarvestPopup().ToString();
	TestTrue(TEXT("팝업에 서버가 준 개수"), Popup.StartsWith(FString::Printf(TEXT("+%d "), TestStock)));
	TestTrue(TEXT("팝업에 서버가 준 코인"), Popup.EndsWith(FString::Printf(TEXT(" / +%d 코인"), TestCoins)));
	TestFalse(TEXT("아이템 이름은 표에서 (ID 그대로가 아님)"), Popup.Contains(TestProduct));
	TestEqual(TEXT("코인 소리"), Rig.Sync->GetLastPlayedCue(), URanchSyncComponent::GetHarvestSoundCue());
	TestNotNull(TEXT("코인 소리 에셋이 있다"), LoadObject<USoundBase>(nullptr, TEXT("/Game/Game/Audio/SFX/SFX_Coins_01.SFX_Coins_01")));
	AddInfo(FString::Printf(TEXT("[HarvestPopup] '%s' · 소리 %s"), *Popup, *Rig.Sync->GetLastPlayedCue().ToString()));

	// 다음 수확이 거절되면 팝업·소리 없음(앞 팝업 글도 비운다). 거점 조회 답은 보내지 않는다 — 칸이 비면 수확이 안 쌓인다.
	TestTrue(TEXT("두 번째 수확 요청이 쌓인다"), Rig.Ranch->QueueHarvest(TestSlot));
	TestTrue(TEXT("두 번째 수확 거절 답"), Server.AnswerNamed(TEXT("HarvestRanch"), true, MakeFail(TEXT("empty"))));
	TestTrue(TEXT("거절이면 팝업 없음"), Rig.Sync->GetLastHarvestPopup().IsEmpty());
	TestEqual(TEXT("거절이면 소리 없음"), Rig.Sync->GetLastPlayedCue(), FName(NAME_None));

	// 0개 수확(아직 생산 안 됨)도 팝업 없음.
	TestTrue(TEXT("세 번째 수확 요청이 쌓인다"), Rig.Ranch->QueueHarvest(TestSlot));
	const TSharedPtr<FJsonObject> Zero = MakeShared<FJsonObject>();
	Zero->SetNumberField(TEXT("harvested"), 0);
	TestTrue(TEXT("세 번째 수확 답"), Server.AnswerNamed(TEXT("HarvestRanch"), true, Zero));
	TestTrue(TEXT("0개면 팝업 없음"), Rig.Sync->GetLastHarvestPopup().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncHarvestCardTest, "Snarehaven.RanchSync.Harvest.CardAfterServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSyncHarvestCardTest::RunTest(const FString& Parameters)
{
	// M23-03 — 수확 팝업이 판 없는 노란 글자가 아니라 왼쪽 위 크림 알림 카드(아이콘 + 제목 + 설명)로,
	// 서버가 확정한 답 뒤에만 1장, 거절이면 0장. 아이콘은 서버 답 `itemId` 의 아이템 표 아이콘.
	constexpr int32 TestCoins = 7;
	const FName TestProduct = TEXT("item.truffle");
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.SeedRanch();

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	URanchHudWidget* const Hud = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
	if (!TestNotNull(TEXT("목장 HUD 생성"), Hud))
	{
		return false;
	}
	// 반환값을 붙잡고 있어야 트리가 산다 (`SnarehavenRanchHudTest` 와 같은 길).
	const TSharedRef<SWidget> SlateWidget = Hud->TakeWidget();
	Hud->BindToRanchSync(Rig.Sync);

	UWidget* const Card = Hud->WidgetTree->FindWidget(TEXT("HarvestCard"));
	UImage* const Icon = Cast<UImage>(Hud->WidgetTree->FindWidget(TEXT("HarvestIcon")));
	USnareNoticeText* const Title = Cast<USnareNoticeText>(Hud->WidgetTree->FindWidget(TEXT("HarvestPopupText")));
	UTextBlock* const CoinLine = Cast<UTextBlock>(Hud->WidgetTree->FindWidget(TEXT("HarvestCoinText")));
	UVerticalBox* const Stack = Cast<UVerticalBox>(Hud->WidgetTree->FindWidget(TEXT("NoticeStack")));
	if (!TestNotNull(TEXT("카드"), Card) || !TestNotNull(TEXT("아이콘"), Icon) || !TestNotNull(TEXT("제목"), Title)
		|| !TestNotNull(TEXT("설명"), CoinLine) || !TestNotNull(TEXT("알림 줄"), Stack))
	{
		return false;
	}

	// 자리 = 시트 「왼쪽 위에 세로로 쌓임, 최대 2장」 — 수확 준비 카드 아래 둘째 장.
	const UCanvasPanelSlot* const StackSlot = Cast<UCanvasPanelSlot>(Stack->Slot);
	if (TestNotNull(TEXT("알림 줄이 캔버스에 있다"), StackSlot))
	{
		TestEqual(TEXT("알림 줄은 왼쪽 위 귀퉁이"), StackSlot->GetAnchors().Minimum, FVector2D(0.f, 0.f));
	}
	// 셋째 칸 = 업적·도감 카드 두 장 묶음(M48-03c, `Achievement.Notice.Wiring` 이 따로 본다).
	TestEqual(TEXT("알림 줄 칸 3 (수확 준비 · 수확 · 업적 카드)"), Stack->GetChildrenCount(), 3);
	TestEqual(TEXT("수확 카드는 둘째 장"), Stack->GetChildIndex(Card), 1);
	TestEqual(TEXT("수확 전엔 카드 접힘"), Card->GetVisibility(), ESlateVisibility::Collapsed);

	// 누른 순간(응답 전)엔 카드 없음.
	Rig.Ranch->QueueHarvest(TestSlot);
	TestEqual(TEXT("응답 전엔 카드 접힘"), Card->GetVisibility(), ESlateVisibility::Collapsed);

	const TSharedPtr<FJsonObject> Ok = MakeShared<FJsonObject>();
	Ok->SetNumberField(TEXT("harvested"), TestStock);
	Ok->SetStringField(TEXT("itemId"), TestProduct.ToString());
	Ok->SetNumberField(TEXT("coins"), TestCoins);
	TestTrue(TEXT("수확 답"), Server.AnswerNamed(TEXT("HarvestRanch"), true, Ok));

	const FItemRow* const Row = SnareItemRows::FindByItemID(
		LoadObject<UDataTable>(nullptr, SnareItemRows::ItemTablePath()), TestProduct);
	const UTexture2D* const Expected = Row != nullptr ? Row->IconRef.LoadSynchronous() : nullptr;
	TestNotNull(TEXT("표에 그 아이템 아이콘이 있다"), Expected);
	TestEqual(TEXT("서버 답 뒤 카드 펴짐"), Card->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestTrue(TEXT("카드 아이콘 = 아이템 표 아이콘"), Icon->GetBrush().GetResourceObject() == Expected);
	TestEqual(TEXT("아이콘 48px"), FVector2D(Icon->GetBrush().ImageSize), FVector2D(48.f, 48.f));
	const FString TitleText = Title->GetText().ToString();
	TestEqual(TEXT("제목 = 서버가 준 개수 + 표 이름"), TitleText,
		FString::Printf(TEXT("+%d %s"), TestStock, *Row->DisplayName.ToString()));
	TestEqual(TEXT("설명 = 서버가 준 코인"), CoinLine->GetText().ToString(), FString::Printf(TEXT("+%d 코인"), TestCoins));
	TestEqual(TEXT("코인 줄 보임"), CoinLine->GetVisibility(), ESlateVisibility::HitTestInvisible);
	AddInfo(FString::Printf(TEXT("[HarvestCard] 서버 답 뒤 카드 보임 1 · 제목 '%s' · 설명 '%s' · 아이콘 %s · 알림 줄 %d장"),
		*TitleText, *CoinLine->GetText().ToString(), *GetNameSafe(Icon->GetBrush().GetResourceObject()),
		Stack->GetChildrenCount()));

	// 시간이 다 되면 제목 글자가 카드째 접는다. 시간 재기는 `USnareNoticeText` 타이머 몫인데, 엔진 타이머는
	// 걸린 프레임엔 대기로만 들어가고 한 프레임에 한 번만 돌아 화면 없는 2층에선 못 흘린다 — 만료가 부르는 함수를 직접 부른다.
	Title->HideNotice();
	TestEqual(TEXT("시간이 다 되면 카드째 접힘"), Card->GetVisibility(), ESlateVisibility::Collapsed);

	// 거절이면 카드 0장.
	TestTrue(TEXT("두 번째 수확 요청"), Rig.Ranch->QueueHarvest(TestSlot));
	TestTrue(TEXT("두 번째 수확 거절 답"), Server.AnswerNamed(TEXT("HarvestRanch"), true, MakeFail(TEXT("empty"))));
	TestEqual(TEXT("거절이면 카드 접힘"), Card->GetVisibility(), ESlateVisibility::Collapsed);
	AddInfo(FString::Printf(TEXT("[HarvestCard] 거절 답 뒤 카드 보임 %d"),
		Card->GetVisibility() == ESlateVisibility::Collapsed ? 0 : 1));

	// 코인 없는 수확이면 설명 줄을 접는다.
	TestTrue(TEXT("세 번째 수확 요청"), Rig.Ranch->QueueHarvest(TestSlot));
	const TSharedPtr<FJsonObject> NoCoins = MakeShared<FJsonObject>();
	NoCoins->SetNumberField(TEXT("harvested"), 1);
	NoCoins->SetStringField(TEXT("itemId"), TestProduct.ToString());
	TestTrue(TEXT("세 번째 수확 답"), Server.AnswerNamed(TEXT("HarvestRanch"), true, NoCoins));
	TestEqual(TEXT("코인 없는 수확도 카드 펴짐"), Card->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("코인 없으면 설명 줄 접힘"), CoinLine->GetVisibility(), ESlateVisibility::Collapsed);

	// M109-04c — 하루 상한(`coinsCapped`)에 닿은 답이면 코인 줄 자리에 까닭 한 줄. 코인 수는 답 그대로, 클라가 셈하지 않는다.
	const FString CappedLine = URanchSyncComponent::FormatHarvestCoinLine(0, true).ToString();
	TestEqual(TEXT("상한 문구"), CappedLine, FString(TEXT("오늘 수확 코인은 다 받았어요")));
	TestEqual(TEXT("상한에 걸려 일부만 받음"), URanchSyncComponent::FormatHarvestCoinLine(TestCoins, true).ToString(),
		FString::Printf(TEXT("+%d 코인 · 오늘 수확 코인 끝"), TestCoins));
	TestEqual(TEXT("상한 아님 = 옛 줄"), URanchSyncComponent::FormatHarvestCoinLine(TestCoins, false).ToString(),
		FString::Printf(TEXT("+%d 코인"), TestCoins));
	TestTrue(TEXT("상한 아님 · 코인 0 = 빈 줄"), URanchSyncComponent::FormatHarvestCoinLine(0, false).IsEmpty());

	TestTrue(TEXT("넷째 수확 요청"), Rig.Ranch->QueueHarvest(TestSlot));
	const TSharedPtr<FJsonObject> Capped = MakeShared<FJsonObject>();
	Capped->SetNumberField(TEXT("harvested"), 1);
	Capped->SetStringField(TEXT("itemId"), TestProduct.ToString());
	Capped->SetNumberField(TEXT("coins"), 0);
	Capped->SetBoolField(TEXT("coinsCapped"), true);
	Capped->SetNumberField(TEXT("coinsLeftToday"), 0);
	TestTrue(TEXT("넷째 수확 답(상한)"), Server.AnswerNamed(TEXT("HarvestRanch"), true, Capped));
	TestEqual(TEXT("상한 답도 카드 펴짐"), Card->GetVisibility(), ESlateVisibility::HitTestInvisible);
	TestEqual(TEXT("상한 답 = 상한 줄"), CoinLine->GetText().ToString(), CappedLine);
	TestEqual(TEXT("상한 줄 보임"), CoinLine->GetVisibility(), ESlateVisibility::HitTestInvisible);
	AddInfo(FString::Printf(TEXT("[HarvestCard] 상한 답 뒤 코인 줄 '%s'"), *CoinLine->GetText().ToString()));

	TestTrue(TEXT("다섯째 수확 요청"), Rig.Ranch->QueueHarvest(TestSlot));
	TestTrue(TEXT("다섯째 수확 답(보통)"), Server.AnswerNamed(TEXT("HarvestRanch"), true, Ok));
	TestEqual(TEXT("보통 답 = +N 코인"), CoinLine->GetText().ToString(), FString::Printf(TEXT("+%d 코인"), TestCoins));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncFailureTest, "Snarehaven.RanchSync.Harvest.FailureKeepsSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSyncFailureTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.SeedRanch();

	Rig.Ranch->QueueHarvest(TestSlot);
	Server.Answer(true, MakeFail(TEXT("empty")));

	const FRanchSlotState* const Slot = Rig.Ranch->GetSlot(TestSlot);
	TestEqual(TEXT("거절이면 펫 그대로"), Slot->AnimalID, TestAnimal);
	TestEqual(TEXT("거절이면 재고 그대로"), Slot->Stock, TestStock);
	TestEqual(TEXT("거절이면 다시 받지 않는다"), Rig.Sync->GetRefetchCount(), 0);
	TestEqual(TEXT("거절이면 GetBaseState 도 안 부른다"), Server.CountCalls(URanchSyncComponent::GetBaseStateFunctionName()), 0);
	TestFalse(TEXT("거절 사유 글이 남는다"), Rig.Sync->GetLastMessage().IsEmpty());

	// 네트워크 실패도 같은 길 — 칸은 그대로.
	Rig.Ranch->QueueHarvest(TestSlot);
	Server.Answer(false, nullptr);
	TestEqual(TEXT("네트워크 실패여도 재고 그대로"), Rig.Ranch->GetSlot(TestSlot)->Stock, TestStock);
	AddInfo(FString::Printf(TEXT("[RanchSync] 거절 - 재고 %d · 사유 \"%s\""), Rig.Ranch->GetSlot(TestSlot)->Stock,
		*Rig.Sync->GetLastMessage().ToString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncRestoreTest, "Snarehaven.RanchSync.Restore.BaseStateFillsSlotsTentsBoat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSyncRestoreTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;

	TestTrue(TEXT("GetBaseState 를 묻는다"), Rig.Sync->RequestBaseState());
	TestEqual(TEXT("부른 함수 = GetBaseState"), Server.Functions.IsEmpty() ? FString() : Server.Functions[0], FString(TEXT("GetBaseState")));

	// 서버 `GetBaseState` 모양 그대로 — 배열 자리 = 칸 번호, 빈 칸 null.
	const FString Json = FString::Printf(TEXT(R"({
		"ranch": [null, null, {"slot":%d,"animalId":"%s","occupiedSinceUtc":"2026-09-24T01:00:00.000Z","stock":%d}, null, null],
		"fishing": [{"slot":0,"fishItemId":"%s","occupiedSinceUtc":"2026-09-24T02:00:00.000Z","stock":0}, null, null, null, null],
		"tents": [{"tentId":"t1","x":4,"y":-2,"placedAtUtc":"2026-09-24T00:00:00.000Z","upgraded":false,"upkeepUntilUtc":null,"color":0,
			"items":[{"structureId":"s1","itemId":"item.storage_box","x":5,"y":-2,"rot":90,"pin":null,"storage":{"item.wood":7}}]}],
		"boat": {"launched":true,"launchedAtUtc":"2026-09-24T03:00:00.000Z"}
	})"), TestRestoredSlot, *TestAnimal.ToString(), TestRestoredStock, *TestFish.ToString());
	TSharedPtr<FJsonObject> Response;
	TestTrue(TEXT("응답 JSON 해석"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Response));
	Server.Answer(true, Response);

	const FRanchSlotState* const RanchSlot = Rig.Ranch->GetSlot(TestRestoredSlot);
	TestTrue(TEXT("목장 칸이 섰다"), RanchSlot != nullptr);
	if (RanchSlot != nullptr)
	{
		TestEqual(TEXT("목장 펫"), RanchSlot->AnimalID, TestAnimal);
		TestEqual(TEXT("목장 재고"), RanchSlot->Stock, TestRestoredStock);
		TestNotEqual(TEXT("목장 시작 시각"), RanchSlot->OccupiedSinceUtc, FDateTime::MinValue());
	}
	TestTrue(TEXT("빈 칸은 비어 있다"), Rig.Ranch->IsSlotEmpty(TestSlot));
	const FRanchSlotState* const FishSlot = Rig.Fishing->GetSlot(0);
	TestEqual(TEXT("낚시터 어종"), FishSlot != nullptr ? FishSlot->FishItemID : NAME_None, TestFish);

	TestEqual(TEXT("텐트 1개"), Rig.Base->GetTents().Num(), 1);
	if (Rig.Base->GetTents().Num() == 1)
	{
		const FSnareTentMirror& Tent = Rig.Base->GetTents()[0];
		TestEqual(TEXT("텐트 칸"), Tent.Cell, FIntPoint(4, -2));
		TestFalse(TEXT("집 전 텐트는 기한이 보인다"), Tent.UpgradeDeadlineUtc.IsEmpty());
		TestEqual(TEXT("가구 1개"), Tent.Structures.Num(), 1);
		if (Tent.Structures.Num() == 1)
		{
			TestEqual(TEXT("가구 회전"), Tent.Structures[0].Rotation, 90);
			TestEqual(TEXT("보관함 속"), Tent.Structures[0].Storage.FindRef(TEXT("item.wood")), 7);
		}
	}
	TestTrue(TEXT("서버에 띄운 배가 있다"), Rig.Water->IsServerBoatLaunched());
	AddInfo(FString::Printf(TEXT("[RanchSync] 복원 - 목장 %d번 재고 %d · 텐트 %d개 · 배 %s"), TestRestoredSlot,
		RanchSlot != nullptr ? RanchSlot->Stock : -1, Rig.Base->GetTents().Num(),
		Rig.Water->IsServerBoatLaunched() ? TEXT("띄움") : TEXT("없음")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncUpkeepTest, "Snarehaven.RanchSync.Restore.UpkeepAndFameFromBaseState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ROADMAP M12-05 2층 — 목장 유지·명성·칸 상한이 **GetBaseState 스냅샷 값 그대로** 캐시에 들어가는지,
// 서버가 명성으로 연 6번째 칸(긴 배열)을 받는지, 유지 시작 전(`leftHours:null`)을 -1 로 옮기는지.
bool FSnarehavenRanchSyncUpkeepTest::RunTest(const FString& Parameters)
{
	// 테스트 전용 값 (Verification.md 6) — 서버가 준 값이 그대로 오는지 보려고 설계 숫자와 겹치지 않게 골랐다.
	constexpr int32 TestLeftHours = 37;
	constexpr int32 TestFame = 123;
	constexpr int32 TestSlotCap = 6;

	FFakeServer Server;
	Server.Install();
	FRig Rig;

	TestTrue(TEXT("GetBaseState 를 묻는다"), Rig.Sync->RequestBaseState());
	const FString Json = FString::Printf(TEXT(R"({
		"ranch": [null, null, null, null, null, {"slot":5,"animalId":"%s","occupiedSinceUtc":"2026-09-24T01:00:00.000Z","stock":1}],
		"ranchUpkeep": {"upkeepUntilUtc":"2026-09-25T14:00:00.000Z","leftHours":%d,"locked":true,"fame":%d,"slotCap":%d},
		"fishing": [null, null, null, null, null], "tents": [], "boat": {"launched":false}
	})"), *TestAnimal.ToString(), TestLeftHours, TestFame, TestSlotCap);
	TSharedPtr<FJsonObject> Response;
	TestTrue(TEXT("응답 JSON 해석"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Response));
	Server.Answer(true, Response);

	const FRanchUpkeepState& Upkeep = Rig.Ranch->GetUpkeep();
	TestTrue(TEXT("유지 상태를 받았다"), Upkeep.bReceived);
	TestEqual(TEXT("남은 시간 = 서버 값"), Upkeep.LeftHours, TestLeftHours);
	TestTrue(TEXT("잠김 = 서버 값"), Upkeep.bLocked);
	TestEqual(TEXT("명성 = 서버 값"), Upkeep.Fame, TestFame);
	TestEqual(TEXT("칸 상한 = 서버 값"), Upkeep.SlotCap, TestSlotCap);
	TestEqual(TEXT("확장 칸까지 6칸을 받는다"), Rig.Ranch->GetSlotCount(), TestSlotCap);
	const FRanchSlotState* const Expanded = Rig.Ranch->GetSlot(TestSlotCap - 1);
	TestEqual(TEXT("6번째 칸의 펫"), Expanded != nullptr ? Expanded->AnimalID : NAME_None, TestAnimal);

	// 짧은 스냅샷(칸이 사라진 것)은 여전히 통째로 거절한다.
	TArray<FRanchSlotState> TooShort;
	TooShort.SetNum(TestSlotCap - 2);
	TestFalse(TEXT("기본 칸 수보다 짧으면 거절"), Rig.Ranch->ApplyBackendSnapshot(TooShort));
	TestEqual(TEXT("거절 뒤 칸 수 그대로"), Rig.Ranch->GetSlotCount(), TestSlotCap);

	// 유지 시작 전 계정 — 서버가 null 을 보내면 -1(시작 전)로 옮긴다. 0(끊김 직전)과 섞이면 안 된다.
	TestTrue(TEXT("다시 묻는다"), Rig.Sync->RequestBaseState());
	TSharedPtr<FJsonObject> Fresh;
	FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(TEXT(R"({
		"ranch": [null, null, null, null, null],
		"ranchUpkeep": {"upkeepUntilUtc":null,"leftHours":null,"locked":false,"fame":0,"slotCap":5}
	})"))), Fresh);
	Server.Answer(true, Fresh);
	TestEqual(TEXT("시작 전은 -1"), Rig.Ranch->GetUpkeep().LeftHours, -1);
	TestFalse(TEXT("시작 전은 안 잠김"), Rig.Ranch->GetUpkeep().bLocked);
	AddInfo(FString::Printf(TEXT("[RanchSync] 유지 %d시간 · 명성 %d · 칸 %d"), TestLeftHours, TestFame, TestSlotCap));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncHatRestoreTest, "Snarehaven.RanchSync.Restore.HatFromBaseState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSyncHatRestoreTest::RunTest(const FString& Parameters)
{
	// #0199 재접속하면 서버 `hat` 기록이 배낭 표시(WornHatID)로 돌아와야 한다 — 장착 버튼이 「벗기」로 보인다.
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	UBackpackComponent* const Backpack = NewObject<UBackpackComponent>(GetTransientPackage());
	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	Rig.Sync->BindTargets(Rig.Ranch, Rig.Fishing, NewObject<USnareBackendSubsystem>(GameInstance), Rig.Base, Rig.Water, Backpack);
	const FName MerchantHat(TEXT("item.merchant_hat"));
	auto AnswerBaseState = [&](const TCHAR* Json)
	{
		TSharedPtr<FJsonObject> State;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), State);
		TestTrue(TEXT("GetBaseState 를 묻는다"), Rig.Sync->RequestBaseState());
		Server.Answer(true, State);
	};

	TestTrue(TEXT("재접속 직후 모자 없음"), Backpack->GetWornHatID().IsNone());
	AnswerBaseState(TEXT(R"({"hat":"item.merchant_hat"})"));
	TestEqual(TEXT("서버 hat → 배낭 모자"), Backpack->GetWornHatID(), MerchantHat);

	// 옛 서버(필드 없음)는 건드리지 않는다.
	AnswerBaseState(TEXT(R"({"ranch":[]})"));
	TestEqual(TEXT("hat 필드 없으면 그대로"), Backpack->GetWornHatID(), MerchantHat);

	AnswerBaseState(TEXT(R"({"hat":""})"));
	TestTrue(TEXT("hat \"\" → 안 쓴 것"), Backpack->GetWornHatID().IsNone());
	TestEqual(TEXT("GetBaseState 세 번"), Server.CountCalls(TEXT("GetBaseState")), 3);

	AddInfo(FString::Printf(TEXT("[RanchSync] 모자 복원 - 호출 %s"), *FString::Join(Server.Functions, TEXT(","))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncRemovePetTest, "Snarehaven.RanchSync.RemovePet.WaitsForServerAndKeepsSlotOnFail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSyncRemovePetTest::RunTest(const FString& Parameters)
{
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.SeedRanch();
	const FString Remove = URanchSyncComponent::GetFunctionName(ERanchStationKind::Ranch, ERanchRequestKind::RemovePet);
	TestEqual(TEXT("부른 함수 = RemovePet"), Remove, FString(TEXT("RemovePet")));

	// 빈 칸·작물 칸은 쌓이지도 않는다 — 서버에 안 간다.
	TestFalse(TEXT("빈 칸은 뺄 펫이 없다"), Rig.Ranch->QueueRemovePet(TestSlot + 1));
	TestFalse(TEXT("없는 칸"), Rig.Ranch->QueueRemovePet(-1));
	TArray<FRanchSlotState> WithCrop;
	for (int32 Index = 0; Index < Rig.Ranch->GetSlotCount(); ++Index)
	{
		WithCrop.Add(*Rig.Ranch->GetSlot(Index));
	}
	WithCrop[TestRestoredSlot].Kind = ERanchSlotKind::Crop;
	WithCrop[TestRestoredSlot].CropItemID = TestFish; // 이름은 상관없다 — 작물 칸이면 된다
	Rig.Ranch->ApplyBackendSnapshot(WithCrop);
	TestFalse(TEXT("작물 칸은 펫 빼기가 아니다"), Rig.Ranch->QueueRemovePet(TestRestoredSlot));
	TestEqual(TEXT("쌓이지 않은 요청은 안 보낸다"), Server.CountCalls(Remove), 0);

	// 거절: 칸 그대로, 다시 받지 않는다, 사유가 남는다.
	TestTrue(TEXT("펫 칸은 쌓인다"), Rig.Ranch->QueueRemovePet(TestSlot));
	TestFalse(TEXT("보내는 중엔 같은 칸을 또 못 쌓는다"), Rig.Ranch->QueueRemovePet(TestSlot));
	TestEqual(TEXT("RemovePet 한 번"), Server.CountCalls(Remove), 1);
	TestEqual(TEXT("인자 slot"), Server.Args.IsEmpty() ? -1 : static_cast<int32>(Server.Args.Last()->GetNumberField(TEXT("slot"))), TestSlot);
	TestEqual(TEXT("응답 전 펫 그대로"), Rig.Ranch->GetSlot(TestSlot)->AnimalID, TestAnimal);
	Server.Answer(true, MakeFail(TEXT("inventory_full")));
	TestEqual(TEXT("거절이면 펫 그대로"), Rig.Ranch->GetSlot(TestSlot)->AnimalID, TestAnimal);
	TestEqual(TEXT("거절이면 재고 그대로"), Rig.Ranch->GetSlot(TestSlot)->Stock, TestStock);
	TestEqual(TEXT("거절이면 다시 받지 않는다"), Rig.Sync->GetRefetchCount(), 0);
	TestFalse(TEXT("거절 사유 글이 남는다"), Rig.Sync->GetLastMessage().IsEmpty());
	const FString Reason = Rig.Sync->GetLastMessage().ToString();

	// 성공: 칸은 GetBaseState 로 다시 받은 값으로만 바뀐다.
	TestTrue(TEXT("다시 쌓인다"), Rig.Ranch->QueueRemovePet(TestSlot));
	const TSharedPtr<FJsonObject> Ok = MakeShared<FJsonObject>();
	Ok->SetBoolField(TEXT("removed"), true);
	Server.Answer(true, Ok);
	TestEqual(TEXT("성공 뒤 다시 받는다"), Rig.Sync->GetRefetchCount(), 1);
	TestEqual(TEXT("성공 뒤 GetBaseState"), Server.CountCalls(URanchSyncComponent::GetBaseStateFunctionName()), 1);
	TestEqual(TEXT("다시 받기 전엔 펫 그대로(클라이언트가 칸을 안 비운다)"), Rig.Ranch->GetSlot(TestSlot)->AnimalID, TestAnimal);
	TestFalse(TEXT("보낸 요청은 대기열에서 빠진다"), Rig.Ranch->HasPendingRequest(TestSlot));
	AddInfo(FString::Printf(TEXT("[RanchSync] 펫 빼기 - 호출 %s · 거절 사유 \"%s\" · 다시받기 %d"),
		*FString::Join(Server.Functions, TEXT(",")), *Reason, Rig.Sync->GetRefetchCount()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncPetSoundTest, "Snarehaven.RanchSync.Sound.PetAndFishingAfterServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSyncPetSoundTest::RunTest(const FString& Parameters)
{
	// M50-03: 넣기·빼기·낚시터 등록은 서버가 받아들인 뒤에만 소리 한 번, 거절·네트워크 실패는 무음 (DESIGN 3.2 「목장 소리」).
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.SeedRanch();
	const int32 EmptySlot = TestSlot + 1;
	const TSharedPtr<FJsonObject> Ok = MakeShared<FJsonObject>();
	Ok->SetBoolField(TEXT("ok"), true);

	// 넣기 — 답 전·거절·네트워크 실패는 0.
	TestTrue(TEXT("빈 칸 넣기가 쌓인다"), Rig.Ranch->QueueRegisterPet(EmptySlot, TestAnimal));
	TestEqual(TEXT("답 전 소리 0"), Rig.Sync->GetRanchCueCount(), 0);
	TestTrue(TEXT("넣기 거절 답"), Server.AnswerNamed(TEXT("RegisterPet"), true, MakeFail(TEXT("not_owned"))));
	TestEqual(TEXT("거절 → 소리 0"), Rig.Sync->GetRanchCueCount(), 0);
	TestEqual(TEXT("거절 → 튼 소리 없음"), Rig.Sync->GetLastPlayedCue(), FName(NAME_None));
	TestTrue(TEXT("넣기 다시 쌓인다"), Rig.Ranch->QueueRegisterPet(EmptySlot, TestAnimal));
	TestTrue(TEXT("넣기 네트워크 실패 답"), Server.AnswerNamed(TEXT("RegisterPet"), false, nullptr));
	TestEqual(TEXT("네트워크 실패 → 소리 0"), Rig.Sync->GetRanchCueCount(), 0);
	TestTrue(TEXT("넣기 또 쌓인다"), Rig.Ranch->QueueRegisterPet(EmptySlot, TestAnimal));
	TestTrue(TEXT("넣기 받아들임 답"), Server.AnswerNamed(TEXT("RegisterPet"), true, Ok));
	TestEqual(TEXT("받아들임 → 소리 1"), Rig.Sync->GetRanchCueCount(), 1);
	TestEqual(TEXT("넣기 소리 = RanchWelcome"), Rig.Sync->GetLastPlayedCue(), FName(TEXT("RanchWelcome")));

	// 빼기 — 거절 0, 받아들임 1.
	TestTrue(TEXT("빼기가 쌓인다"), Rig.Ranch->QueueRemovePet(TestSlot));
	TestTrue(TEXT("빼기 거절 답"), Server.AnswerNamed(TEXT("RemovePet"), true, MakeFail(TEXT("inventory_full"))));
	TestEqual(TEXT("빼기 거절 → 소리 그대로 1"), Rig.Sync->GetRanchCueCount(), 1);
	TestTrue(TEXT("빼기 다시 쌓인다"), Rig.Ranch->QueueRemovePet(TestSlot));
	TestTrue(TEXT("빼기 받아들임 답"), Server.AnswerNamed(TEXT("RemovePet"), true, Ok));
	TestEqual(TEXT("빼기 받아들임 → 소리 2"), Rig.Sync->GetRanchCueCount(), 2);
	TestEqual(TEXT("빼기 소리 = RanchRelease"), Rig.Sync->GetLastPlayedCue(), FName(TEXT("RanchRelease")));

	// 낚시터 등록 — 낚시터 쪽 넣기 요청은 `RegisterFishingSpot` 으로 간다.
	const FString FishingRegister = URanchSyncComponent::GetFunctionName(ERanchStationKind::Fishing, ERanchRequestKind::RegisterPet);
	TestEqual(TEXT("낚시터 넣기 함수 = RegisterFishingSpot"), FishingRegister, FString(TEXT("RegisterFishingSpot")));
	// 등록 안 된 컴포넌트라 칸이 비어 있다 — 빈 칸 스냅샷을 먼저 준다(테스트 준비).
	TArray<FRanchSlotState> EmptySpots;
	EmptySpots.SetNum(Rig.Fishing->GetBaseSlotCount());
	TestTrue(TEXT("빈 낚시터 칸 스냅샷"), Rig.Fishing->ApplyBackendSnapshot(EmptySpots));
	TestTrue(TEXT("낚시터 등록이 쌓인다"), Rig.Fishing->QueueRegisterPet(TestSlot, TestFish));
	TestTrue(TEXT("낚시터 거절 답"), Server.AnswerNamed(FishingRegister, true, MakeFail(TEXT("locked"))));
	TestEqual(TEXT("낚시터 거절 → 소리 그대로 2"), Rig.Sync->GetRanchCueCount(), 2);
	TestTrue(TEXT("낚시터 다시 쌓인다"), Rig.Fishing->QueueRegisterPet(TestSlot, TestFish));
	TestTrue(TEXT("낚시터 받아들임 답"), Server.AnswerNamed(FishingRegister, true, Ok));
	TestEqual(TEXT("낚시터 받아들임 → 소리 3"), Rig.Sync->GetRanchCueCount(), 3);
	TestEqual(TEXT("낚시터 소리 = Splash"), Rig.Sync->GetLastPlayedCue(), FName(TEXT("Splash")));

	// 수확은 기존 코인 소리 길 — 목장 소리를 두 번 울리지 않는다.
	const TSharedPtr<FJsonObject> Harvested = MakeShared<FJsonObject>();
	Harvested->SetNumberField(TEXT("harvested"), TestStock);
	TestTrue(TEXT("수확이 쌓인다"), Rig.Ranch->QueueHarvest(TestSlot));
	TestTrue(TEXT("수확 받아들임 답"), Server.AnswerNamed(TEXT("HarvestRanch"), true, Harvested));
	TestEqual(TEXT("수확 → 목장 소리 그대로 3"), Rig.Sync->GetRanchCueCount(), 3);

	for (const TCHAR* const Asset : { TEXT("/Game/Game/Audio/SFX/SFX_RanchWelcome_01.SFX_RanchWelcome_01"),
		TEXT("/Game/Game/Audio/SFX/SFX_RanchRelease_01.SFX_RanchRelease_01"), TEXT("/Game/Game/Audio/SFX/SFX_Splash_01.SFX_Splash_01") })
	{
		TestNotNull(FString::Printf(TEXT("소리 에셋 %s"), Asset), LoadObject<USoundBase>(nullptr, Asset));
	}
	AddInfo(FString::Printf(TEXT("[Sound] 목장 넣기·빼기·낚시터 - 응답 8 (받아들임 4 — 그중 수확 1) → 목장 소리 %d"), Rig.Sync->GetRanchCueCount()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncDecorSoundTest, "Snarehaven.RanchSync.Sound.CosmeticAndDecoAfterServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSyncDecorSoundTest::RunTest(const FString& Parameters)
{
	// M50-04: 꾸미기·데코는 서버가 받아들인 뒤에만 소리 한 번, 거절·네트워크 실패는 무음 (DESIGN 3.2 「목장 소리」).
	const FName TestCosmeticItem = TEXT("item.test_cosmetic");
	const FName TestDecoItem = TEXT("item.test_deco");
	const FName TestDecoKind = TEXT("test_kind");
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	Rig.SeedRanch();
	const TSharedPtr<FJsonObject> Ok = MakeShared<FJsonObject>();
	Ok->SetBoolField(TEXT("ok"), true);

	// 꾸미기 — 펫이 선 칸에 씌우기.
	TestTrue(TEXT("꾸미기가 쌓인다"), Rig.Ranch->QueueSetCosmetic(TestSlot, TestCosmeticItem));
	TestEqual(TEXT("답 전 소리 0"), Rig.Sync->GetRanchCueCount(), 0);
	TestTrue(TEXT("꾸미기 거절 답"), Server.AnswerNamed(TEXT("SetPetCosmetic"), true, MakeFail(TEXT("not_owned"))));
	TestEqual(TEXT("꾸미기 거절 → 소리 0"), Rig.Sync->GetRanchCueCount(), 0);
	TestEqual(TEXT("거절 → 튼 소리 없음"), Rig.Sync->GetLastPlayedCue(), FName(NAME_None));
	TestTrue(TEXT("꾸미기 다시 쌓인다"), Rig.Ranch->QueueSetCosmetic(TestSlot, TestCosmeticItem));
	TestTrue(TEXT("꾸미기 네트워크 실패 답"), Server.AnswerNamed(TEXT("SetPetCosmetic"), false, nullptr));
	TestEqual(TEXT("네트워크 실패 → 소리 0"), Rig.Sync->GetRanchCueCount(), 0);
	TestTrue(TEXT("꾸미기 또 쌓인다"), Rig.Ranch->QueueSetCosmetic(TestSlot, TestCosmeticItem));
	TestTrue(TEXT("꾸미기 받아들임 답"), Server.AnswerNamed(TEXT("SetPetCosmetic"), true, Ok));
	TestEqual(TEXT("꾸미기 받아들임 → 소리 1"), Rig.Sync->GetRanchCueCount(), 1);
	TestEqual(TEXT("꾸미기 소리 = Outfit"), Rig.Sync->GetLastPlayedCue(), FName(TEXT("Outfit")));

	// 데코 — 가방 카드 「목장 적용」 길(`RequestSetRanchDeco`).
	const FString Deco = URanchSyncComponent::GetSetRanchDecoFunctionName();
	TestTrue(TEXT("데코 요청이 나간다"), Rig.Sync->RequestSetRanchDeco(TestDecoItem, TestDecoKind));
	TestTrue(TEXT("데코 거절 답"), Server.AnswerNamed(Deco, true, MakeFail(TEXT("not_owned"))));
	TestEqual(TEXT("데코 거절 → 소리 그대로 1"), Rig.Sync->GetRanchCueCount(), 1);
	TestTrue(TEXT("데코 다시 나간다"), Rig.Sync->RequestSetRanchDeco(TestDecoItem, TestDecoKind));
	TestTrue(TEXT("데코 받아들임 답"), Server.AnswerNamed(Deco, true, Ok));
	TestEqual(TEXT("데코 받아들임 → 소리 2"), Rig.Sync->GetRanchCueCount(), 2);
	TestEqual(TEXT("데코 소리 = 설치 쿵"), Rig.Sync->GetLastPlayedCue(), SnareBuildRules::PlaceConfirmCue());
	// 해제도 서버가 받은 데코 요청이라 같은 소리 한 번.
	TestTrue(TEXT("데코 해제가 나간다"), Rig.Sync->RequestSetRanchDeco(NAME_None, TestDecoKind));
	TestTrue(TEXT("데코 해제 받아들임 답"), Server.AnswerNamed(Deco, true, Ok));
	TestEqual(TEXT("데코 해제 → 소리 3"), Rig.Sync->GetRanchCueCount(), 3);

	for (const TCHAR* const Asset : { TEXT("/Game/Game/Audio/SFX/SFX_Outfit_01.SFX_Outfit_01"),
		TEXT("/Game/Game/Audio/SFX/SFX_Chop_01.SFX_Chop_01") })
	{
		TestNotNull(FString::Printf(TEXT("소리 에셋 %s"), Asset), LoadObject<USoundBase>(nullptr, Asset));
	}
	AddInfo(FString::Printf(TEXT("[Sound] 목장 꾸미기·데코 - 응답 6 (받아들임 3) → 목장 소리 %d"), Rig.Sync->GetRanchCueCount()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRanchSyncUpkeepSoundTest, "Snarehaven.RanchSync.Sound.ExpandAndUpkeepAfterServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRanchSyncUpkeepSoundTest::RunTest(const FString& Parameters)
{
	// M50-05: 칸 늘리기·유지 넣기는 서버가 받아들인 뒤에만 소리 한 번, 거절·네트워크 실패는 무음 (DESIGN 3.2 「목장 소리」).
	constexpr int32 TestFeedCount = 3;
	FFakeServer Server;
	Server.Install();
	FRig Rig;
	const TSharedPtr<FJsonObject> Ok = MakeShared<FJsonObject>();
	Ok->SetBoolField(TEXT("ok"), true);
	const FString Expand = URanchSyncComponent::GetExpandSlotFunctionName();
	const FString Feed = URanchSyncComponent::GetFeedUpkeepFunctionName();

	// 칸 늘리기.
	TestTrue(TEXT("칸 늘리기 요청이 나간다"), Rig.Sync->RequestExpandSlot());
	TestEqual(TEXT("답 전 소리 0"), Rig.Sync->GetRanchCueCount(), 0);
	TestTrue(TEXT("칸 늘리기 거절 답"), Server.AnswerNamed(Expand, true, MakeFail(TEXT("not_enough_fame"))));
	TestEqual(TEXT("칸 늘리기 거절 → 소리 0"), Rig.Sync->GetRanchCueCount(), 0);
	TestEqual(TEXT("거절 → 튼 소리 없음"), Rig.Sync->GetLastPlayedCue(), FName(NAME_None));
	TestTrue(TEXT("칸 늘리기 다시 나간다"), Rig.Sync->RequestExpandSlot());
	TestTrue(TEXT("칸 늘리기 네트워크 실패 답"), Server.AnswerNamed(Expand, false, nullptr));
	TestEqual(TEXT("네트워크 실패 → 소리 0"), Rig.Sync->GetRanchCueCount(), 0);
	TestTrue(TEXT("칸 늘리기 또 나간다"), Rig.Sync->RequestExpandSlot());
	TestTrue(TEXT("칸 늘리기 받아들임 답"), Server.AnswerNamed(Expand, true, Ok));
	TestEqual(TEXT("칸 늘리기 받아들임 → 소리 1"), Rig.Sync->GetRanchCueCount(), 1);
	TestEqual(TEXT("칸 늘리기 소리 = RanchExpand"), Rig.Sync->GetLastPlayedCue(), FName(TEXT("RanchExpand")));

	// 유지 넣기.
	TestFalse(TEXT("0개 넣기는 안 나간다"), Rig.Sync->RequestFeedUpkeep(0));
	TestTrue(TEXT("유지 넣기 요청이 나간다"), Rig.Sync->RequestFeedUpkeep(TestFeedCount));
	TestTrue(TEXT("유지 넣기 거절 답"), Server.AnswerNamed(Feed, true, MakeFail(TEXT("full"))));
	TestEqual(TEXT("유지 넣기 거절 → 소리 그대로 1"), Rig.Sync->GetRanchCueCount(), 1);
	TestTrue(TEXT("유지 넣기 다시 나간다"), Rig.Sync->RequestFeedUpkeep(TestFeedCount));
	TestTrue(TEXT("유지 넣기 받아들임 답"), Server.AnswerNamed(Feed, true, Ok));
	TestEqual(TEXT("유지 넣기 받아들임 → 소리 2"), Rig.Sync->GetRanchCueCount(), 2);
	TestEqual(TEXT("유지 넣기 소리 = Storage"), Rig.Sync->GetLastPlayedCue(), FName(TEXT("Storage")));

	for (const TCHAR* const Asset : { TEXT("/Game/Game/Audio/SFX/SFX_RanchExpand_01.SFX_RanchExpand_01"),
		TEXT("/Game/Game/Audio/SFX/SFX_Storage_01.SFX_Storage_01") })
	{
		TestNotNull(FString::Printf(TEXT("소리 에셋 %s"), Asset), LoadObject<USoundBase>(nullptr, Asset));
	}
	AddInfo(FString::Printf(TEXT("[Sound] 목장 칸 늘리기·유지 넣기 - 응답 5 (받아들임 2) → 목장 소리 %d"), Rig.Sync->GetRanchCueCount()));
	return true;
}

#endif
