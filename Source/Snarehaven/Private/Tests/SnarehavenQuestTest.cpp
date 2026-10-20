// DESIGN 8.14 「의뢰」 2층 검사 — 의뢰 화면(`UQuestScreenWidget`)과 의뢰인(`AQuestGiverActor`).
// 잡는 것: ① 펴면 `GetQuests` 를 묻는다 ② 건네기 인자 `CompleteQuest {questId}` ③ **답 전에 배낭·코인을 안 바꾼다**
// ④ 모자라거나 끝낸 줄은 못 누른다 ⑤ 의뢰인은 좌판이 없고 다가오면 의뢰 화면을 올린다.
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신한다. 검사 끝에 반드시 비운다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Backend/SnareQuestChain.h"
#include "Data/QuestChainRow.h"
#include "Engine/DataTable.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SnarehavenCharacter.h"
#include "UI/QuestScreenWidget.h"
#include "UI/SnareUiStyle.h"
#include "World/QuestGiverActor.h"

namespace
{
	// 검사 전용 값 (Verification.md 6).
	const TCHAR* QuestReplyJson = TEXT("{\"day\":1,\"quests\":["
		"{\"id\":\"item.wood\",\"itemId\":\"item.wood\",\"count\":10,\"reward\":5,\"done\":false},"
		"{\"id\":\"item.carrot\",\"itemId\":\"item.carrot\",\"count\":3,\"reward\":5,\"done\":false},"
		"{\"id\":\"item.pike\",\"itemId\":\"item.pike\",\"count\":1,\"reward\":5,\"done\":true}]}");
	constexpr int32 TestCoins = 7;
	constexpr int32 TestWood = 12;

	struct FQuestCall
	{
		FString FunctionName;
		FString QuestId;
		USnareBackendSubsystem::FServerCallDone OnDone;
	};

	TSharedPtr<FJsonObject> ParseJson(const TCHAR* Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenQuestScreenTest, "Snarehaven.Quest.Screen.WaitsForServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenQuestScreenTest::RunTest(const FString& Parameters)
{
	TArray<FQuestCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FQuestCall Call;
			Call.FunctionName = FunctionName;
			if (Args.IsValid())
			{
				Args->TryGetStringField(TEXT("questId"), Call.QuestId);
			}
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UQuestScreenWidget* const Screen = CreateWidget<UQuestScreenWidget>(World, UQuestScreenWidget::StaticClass());
	TSharedPtr<SWidget> Slate = Screen->TakeWidget();

	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.CoinBalance = TestCoins;
	Snapshot.AddItem(TEXT("item.wood"), TestWood);
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);

	// ① 펴면 목록을 묻는다.
	Screen->SetOpen(true);
	TestEqual(TEXT("펴면 서버를 한 번 부른다"), Calls.Num(), 1);
	TestEqual(TEXT("목록 함수"), Calls.Num() > 0 ? Calls[0].FunctionName : FString(), FString(TEXT("GetQuests")));
	if (Calls.Num() > 0)
	{
		Calls[0].OnDone(true, ParseJson(QuestReplyJson), FString());
	}
	const TArray<FQuestLineView>& Lines = Screen->GetLines();
	TestEqual(TEXT("의뢰 3줄"), Lines.Num(), 3);
	if (Lines.Num() == 3)
	{
		AddInfo(FString::Printf(TEXT("줄: %s / %s / %s"), *Screen->GetLineText(Lines[0]).ToString(),
			*Screen->GetLineText(Lines[1]).ToString(), *Screen->GetLineText(Lines[2]).ToString()));
		TestEqual(TEXT("가진 나무"), Screen->CountOwned(Lines[0].ItemID), TestWood);
		TestTrue(TEXT("끝낸 줄 표시"), Lines[2].bDone);
	}

	// ② 건네기 — 인자 {questId}. ③ 답 전엔 배낭·코인 그대로, 잠김.
	TestTrue(TEXT("건네기 요청을 띄운다"), Screen->RequestComplete(TEXT("item.wood")));
	TestEqual(TEXT("건네기 함수"), Calls.Num() > 1 ? Calls[1].FunctionName : FString(), FString(TEXT("CompleteQuest")));
	TestEqual(TEXT("questId"), Calls.Num() > 1 ? Calls[1].QuestId : FString(), FString(TEXT("item.wood")));
	TestEqual(TEXT("답 전 코인 그대로"), Backend->GetPlayerSnapshot().CoinBalance, TestCoins);
	TestEqual(TEXT("답 전 나무 그대로"), Screen->CountOwned(TEXT("item.wood")), TestWood);
	TestFalse(TEXT("답 전엔 다른 요청을 안 띄운다"), Screen->RequestComplete(TEXT("item.carrot")));

	// 거절 답 — 사유가 화면에 남는다.
	if (Calls.Num() > 1)
	{
		Calls[1].OnDone(true, ParseJson(TEXT("{\"error\":\"가진 것이 모자란다\",\"code\":\"not_enough_items\"}")), FString());
	}
	TestFalse(TEXT("거절 뒤 대기 풀림"), Screen->IsRequestPending());
	TestFalse(TEXT("거절 사유를 삼키지 않는다"), Screen->GetMessage().IsEmpty());

	// ④ 내렸다 다시 올림(M22) — 의뢰인에게서 멀어지면 `NativeDestruct` 가 백엔드를 푼다. 다시 올리면 다시 묶여야 한다.
	Slate.Reset();
	TestEqual(TEXT("내린 동안은 백엔드를 풀어 둔다"), Screen->CountOwned(TEXT("item.wood")), 0);
	Slate = Screen->TakeWidget();
	TestEqual(TEXT("다시 올리면 백엔드가 다시 묶인다 — 가진 나무가 보인다"), Screen->CountOwned(TEXT("item.wood")), TestWood);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenQuestGiverTest, "Snarehaven.Quest.Giver.NoStallAndHat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenQuestGiverTest::RunTest(const FString& Parameters)
{
	const AQuestGiverActor* const Giver = GetDefault<AQuestGiverActor>();
	TestFalse(TEXT("좌판은 안 보인다"), Giver->GetStall()->IsVisible());
	TestEqual(TEXT("좌판 충돌 없음"), Giver->GetStall()->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("이름이 있다"), Giver->GetVendorName().IsEmpty());
	TestFalse(TEXT("상인과 이름이 다르다"), Giver->GetVendorName().EqualTo(GetDefault<AShopVendorActor>()->GetVendorName()));
	return true;
}

// M24-05 — 제목 알약 `의뢰` 가 판 왼쪽 위에 걸치고, 표에 없는 아이템 ID 는 원문 대신 `물건` 으로 보인다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenQuestTitlePillTest, "Snarehaven.Quest.Screen.TitlePillAndNoRawID",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenQuestTitlePillTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 — 표에 없는 ID 한 줄을 섞는다.
	const TCHAR* const ReplyWithUnknown = TEXT("{\"day\":1,\"quests\":["
		"{\"id\":\"q1\",\"itemId\":\"item.wood\",\"count\":10,\"reward\":5,\"done\":false},"
		"{\"id\":\"q2\",\"itemId\":\"item.loop366_missing\",\"count\":2,\"reward\":5,\"done\":false}]}");
	TArray<FQuestCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FQuestCall Call;
			Call.FunctionName = FunctionName;
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UQuestScreenWidget* const Screen = CreateWidget<UQuestScreenWidget>(World, UQuestScreenWidget::StaticClass());
	const TSharedPtr<SWidget> Slate = Screen->TakeWidget();
	UWidgetTree* const Tree = Screen->WidgetTree;
	auto Finish = [World]()
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	UTextBlock* const Title = Tree->FindWidget<UTextBlock>(TEXT("QuestTitle"));
	UBorder* const Pill = Tree->FindWidget<UBorder>(TEXT("QuestTitlePill"));
	USizeBox* const PillBox = Tree->FindWidget<USizeBox>(TEXT("QuestTitlePillBox"));
	UBorder* const Panel = Tree->FindWidget<UBorder>(TEXT("QuestPanel"));
	UOverlay* const Frame = Tree->FindWidget<UOverlay>(TEXT("QuestFrame"));
	if (!TestNotNull(TEXT("제목 글자"), Title) || !TestNotNull(TEXT("제목 알약"), Pill) || !TestNotNull(TEXT("알약 상자"), PillBox)
		|| !TestNotNull(TEXT("판"), Panel) || !TestNotNull(TEXT("판·알약 겹"), Frame))
	{
		Finish();
		return false;
	}

	// ① 제목은 판 밖 알약 안, 색 = UiTitleTab.
	TestEqual(TEXT("제목 = 의뢰"), Title->GetText().ToString(), FString(TEXT("의뢰")));
	TestTrue(TEXT("제목 글자 부모 = 알약"), Title->GetParent() == Pill);
	TestEqual(TEXT("알약 색 = UiTitleTab"), Pill->Background.TintColor.GetSpecifiedColor(), SnareUiStyle::UiTitleTab());
	bool bTitleInPanel = false;
	for (const UWidget* Walk = Title; Walk != nullptr; Walk = Walk->GetParent())
	{
		bTitleInPanel |= Walk == Panel;
	}
	TestFalse(TEXT("판 안에 제목 글자 0"), bTitleInPanel);

	// ② 같은 겹 · 알약은 판 뒤(위에 그림) 왼쪽 위 · 판 위 변보다 MenuTitlePillRise 위.
	const UOverlaySlot* const PillSlot = Cast<UOverlaySlot>(PillBox->Slot);
	const UOverlaySlot* const PanelSlot = Cast<UOverlaySlot>(Panel->Slot);
	const UCanvasPanelSlot* const FrameSlot = Cast<UCanvasPanelSlot>(Frame->Slot);
	TestTrue(TEXT("알약·판 같은 겹"), PillSlot != nullptr && PanelSlot != nullptr && FrameSlot != nullptr);
	TestTrue(TEXT("알약을 판 뒤에 넣어 판 위에 그린다"), Frame->GetChildIndex(PillBox) > Frame->GetChildIndex(Panel));
	TestTrue(TEXT("알약 왼쪽 위 정렬"), PillSlot != nullptr && PillSlot->GetHorizontalAlignment() == HAlign_Left
		&& PillSlot->GetVerticalAlignment() == VAlign_Top && PillSlot->GetPadding() == FMargin(0.f));
	const float PanelInFrame = PanelSlot != nullptr ? PanelSlot->GetPadding().Top : -1.f;
	TestTrue(TEXT("판 왼쪽 여백 0 — 알약 왼 변 = 판 왼 변"), PanelSlot != nullptr && PanelSlot->GetPadding().Left == 0.f);
	TestEqual(TEXT("알약 위 변 = 판 위 변 - MenuTitlePillRise"), 0.f, PanelInFrame - SnareUiStyle::MenuTitlePillRise);
	TestTrue(TEXT("판 안 위 여백이 알약이 내려온 몫을 덮는다"),
		Panel->GetPadding().Top >= PillBox->GetHeightOverride() - SnareUiStyle::MenuTitlePillRise);
	// 판 위 변 화면 자리 = M24-05 전 판 자리 160 그대로 (검사 전용 비교값).
	const float ScreenPanelTop = FrameSlot != nullptr ? FrameSlot->GetPosition().Y + PanelInFrame : -1.f;
	TestEqual(TEXT("판 위 변 화면 자리 그대로"), ScreenPanelTop, 160.f);
	// 판 오른쪽 변(가운데 + 비킨 만큼 + 판 폭의 절반)이 화면 가운데에서 더 오른쪽으로 안 가 HUD 코인 알약을 안 덮는다 (M106-03).
	TestTrue(TEXT("판을 왼쪽으로 비킴 — 코인 알약 안 덮음"), FrameSlot != nullptr && FrameSlot->GetPosition().X < 0.f);

	// ③ 닫힌 판이면 알약도 접힌다.
	TestEqual(TEXT("닫힌 판 알약 접힘"), PillBox->GetVisibility(), ESlateVisibility::Collapsed);

	// ④ 펴고 답을 받으면 — 표에 없는 ID 는 `물건`, 줄 글자에 `item.` 0.
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	Screen->SetOpen(true);
	TestEqual(TEXT("편 판 알약 보임"), PillBox->GetVisibility(), ESlateVisibility::HitTestInvisible);
	if (Calls.Num() > 0)
	{
		Calls[0].OnDone(true, ParseJson(ReplyWithUnknown), FString());
	}
	TestEqual(TEXT("의뢰 2줄"), Screen->GetLines().Num(), 2);
	TestEqual(TEXT("표에 없는 ID → 물건"), Screen->FindItemDisplayName(TEXT("item.loop366_missing")).ToString(), FString(TEXT("물건")));
	int32 RawIDs = 0;
	FString Shown;
	for (int32 Index = 0; Index < Screen->GetLines().Num(); ++Index)
	{
		const UTextBlock* const LineText = Tree->FindWidget<UTextBlock>(FName(*FString::Printf(TEXT("QuestLineText_%d"), Index)));
		const FString Text = LineText != nullptr ? LineText->GetText().ToString() : FString(TEXT("item.(줄 없음)"));
		RawIDs += Text.Contains(TEXT("item.")) ? 1 : 0;
		Shown += Text + TEXT(" / ");
	}
	TestEqual(TEXT("줄 글자에 item. 0"), RawIDs, 0);
	AddInfo(FString::Printf(TEXT("[QuestTitle] 알약 위 %.0f · 판 위 %.0f · 줄: %s"),
		FrameSlot != nullptr ? FrameSlot->GetPosition().Y : -1.f, ScreenPanelTop, *Shown));

	Finish();
	return true;
}

// M27-02 — 판은 크림 둥근 16(상점 판과 같은 불투명), 판 안 버튼(의뢰 줄·닫기)은 전부 둥근 HudCornerRadius, 못 누르는 줄 잠금 그대로.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenQuestRoundTest, "Snarehaven.Quest.Screen.RoundPanelAndButtons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenQuestRoundTest::RunTest(const FString& Parameters)
{
	TArray<FQuestCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FQuestCall Call;
			Call.FunctionName = FunctionName;
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UQuestScreenWidget* const Screen = CreateWidget<UQuestScreenWidget>(World, UQuestScreenWidget::StaticClass());
	const TSharedPtr<SWidget> Slate = Screen->TakeWidget();
	UWidgetTree* const Tree = Screen->WidgetTree;
	auto Finish = [World]()
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	// 빈 가방으로 펴고 답을 받는다 — 줄은 생기되 모자라 전부 잠긴다.
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	Screen->SetOpen(true);
	if (Calls.Num() > 0)
	{
		Calls[0].OnDone(true, ParseJson(QuestReplyJson), FString());
	}

	UBorder* const Panel = Tree->FindWidget<UBorder>(TEXT("QuestPanel"));
	UBorder* const Pill = Tree->FindWidget<UBorder>(TEXT("QuestTitlePill"));
	if (!TestNotNull(TEXT("판"), Panel) || !TestNotNull(TEXT("제목 알약"), Pill))
	{
		Finish();
		return false;
	}

	// ① 판 — 크림·불투명·둥근 16.
	const FSlateBrush& PanelBrush = Panel->Background;
	const FVector4 PanelRadius(SnareUiStyle::ScreenPanelRadius, SnareUiStyle::ScreenPanelRadius, SnareUiStyle::ScreenPanelRadius,
		SnareUiStyle::ScreenPanelRadius);
	TestTrue(TEXT("판 브러시 = 둥근 네모"), PanelBrush.DrawAs == ESlateBrushDrawType::RoundedBox);
	TestTrue(TEXT("판 반지름 = ScreenPanelRadius"), PanelBrush.OutlineSettings.CornerRadii == PanelRadius);
	TestEqual(TEXT("판 색 = UiPanelCream"), PanelBrush.TintColor.GetSpecifiedColor(), SnareUiStyle::UiPanelCream());

	// ② 판 안 버튼 전부 네 상태 둥근 14 — 종류마다 하나 이상이어야 시험이 그 길을 본 것이다.
	const FVector4 ButtonRadius(SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius, SnareUiStyle::HudCornerRadius,
		SnareUiStyle::HudCornerRadius);
	auto IsRound = [&ButtonRadius](const FSlateBrush& Brush)
	{
		return Brush.DrawAs == ESlateBrushDrawType::RoundedBox && Brush.OutlineSettings.CornerRadii == ButtonRadius;
	};
	int32 LineButtons = 0;
	int32 LockedLines = 0;
	int32 CloseButtons = 0;
	int32 Round = 0;
	int32 All = 0;
	Tree->ForEachWidget([&](UWidget* Widget)
	{
		const UButton* const Button = Cast<UButton>(Widget);
		bool bInPanel = false;
		for (const UWidget* Walk = Button; Walk != nullptr; Walk = Walk->GetParent())
		{
			bInPanel |= Walk == Panel;
		}
		if (Button == nullptr || !bInPanel)
		{
			return;
		}
		const FString Name = Button->GetName();
		LineButtons += Name.StartsWith(TEXT("QuestLineButton_")) ? 1 : 0;
		// 못 내는 줄은 단추를 끄지 않고(끄면 글자가 반투명) 배경만 어둡게 한다 (M106-03).
		LockedLines += Name.StartsWith(TEXT("QuestLineButton_")) && Button->GetIsEnabled() && Button->GetBackgroundColor().R < 1.f ? 1 : 0;
		CloseButtons += Name == TEXT("QuestCloseButton") ? 1 : 0;
		const FButtonStyle& Style = Button->GetStyle();
		const bool bRound = IsRound(Style.Normal) && IsRound(Style.Hovered) && IsRound(Style.Pressed) && IsRound(Style.Disabled);
		TestTrue(FString::Printf(TEXT("%s 네 상태 둥근 네모 · 반지름 = HudCornerRadius"), *Name), bRound);
		Round += bRound ? 1 : 0;
		++All;
	});
	TestEqual(TEXT("의뢰 줄 버튼 = 답의 줄 수"), LineButtons, Screen->GetLines().Num());
	TestTrue(TEXT("의뢰 줄 버튼이 있다"), LineButtons > 0);
	TestEqual(TEXT("닫기 버튼 1개"), CloseButtons, 1);
	// ③ 빈 가방이라 줄은 전부 잠김 그대로 · 알약 색 그대로.
	TestEqual(TEXT("모자란 줄은 켜진 채 어둡다"), LockedLines, LineButtons);
	TestEqual(TEXT("알약 색 = UiTitleTab"), Pill->Background.TintColor.GetSpecifiedColor(), SnareUiStyle::UiTitleTab());

	AddInfo(FString::Printf(TEXT("[QuestRound] 판 %s 반지름 %.0f 불투명 %.2f · 둥근 버튼 %d/%d (줄 %d·잠김 %d·닫기 %d)"),
		PanelBrush.DrawAs == ESlateBrushDrawType::RoundedBox ? TEXT("RoundedBox") : TEXT("네모"),
		static_cast<float>(PanelBrush.OutlineSettings.CornerRadii.X), PanelBrush.TintColor.GetSpecifiedColor().A,
		Round, All, LineButtons, LockedLines, CloseButtons));
	Finish();
	return true;
}

// M105-04 「줄기」 탭 — 탭을 누르면 `GetQuestChain` 을 묻고, 받기는 `CompleteQuestChainStep {step}` 이며,
// **답 전엔 배낭·코인·단계가 그대로**다. 재료가 모자라면 받기를 못 누른다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenQuestChainTabTest, "Snarehaven.Quest.Screen.ChainTab",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenQuestChainTabTest::RunTest(const FString& Parameters)
{
	TArray<FQuestCall> Calls;
	TArray<double> ChainSteps;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls, &ChainSteps](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FQuestCall Call;
			Call.FunctionName = FunctionName;
			double Step = -1.0;
			if (Args.IsValid())
			{
				Args->TryGetNumberField(TEXT("step"), Step);
			}
			ChainSteps.Add(Step);
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UQuestScreenWidget* const Screen = CreateWidget<UQuestScreenWidget>(World, UQuestScreenWidget::StaticClass());
	TSharedPtr<SWidget> Slate = Screen->TakeWidget();

	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.CoinBalance = TestCoins;
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);

	// ① 줄기 탭을 누르면 GetQuestChain.
	Screen->SetOpen(true);
	TestEqual(TEXT("펴면 목록부터"), Calls.Num() > 0 ? Calls[0].FunctionName : FString(), FString(TEXT("GetQuests")));
	if (Calls.Num() > 0)
	{
		Calls[0].OnDone(true, ParseJson(QuestReplyJson), FString());
	}
	Screen->SetTab(EQuestTab::Chain);
	TestEqual(TEXT("줄기 탭"), Screen->GetTab() == EQuestTab::Chain, true);
	TestEqual(TEXT("줄기 함수"), Calls.Num() > 1 ? Calls[1].FunctionName : FString(), FString(TEXT("GetQuestChain")));
	TestEqual(TEXT("답 전엔 단계를 모른다"), Screen->GetChainStep(), -1);
	TestFalse(TEXT("답 전엔 받기 못 누름"), Screen->CanClaimChain());
	if (Calls.Num() > 1)
	{
		Calls[1].OnDone(true, ParseJson(TEXT("{\"step\":0,\"total\":5,\"done\":false}")), FString());
	}
	TestEqual(TEXT("서버 단계 0 = 1단계 차례"), Screen->GetChainStep(), 0);
	AddInfo(FString::Printf(TEXT("[QuestChain] 제목: %s"), *Screen->GetChainTitleText().ToString()));

	// ② 재료 없음 → 받기 못 누름, 요청도 안 나감.
	TestFalse(TEXT("재료 없으면 받기 불가"), Screen->CanClaimChain());

	// ③ 재료를 채우면 받을 수 있다. 받기 → CompleteQuestChainStep {step:1}, 답 전 코인·단계 그대로.
	{
		FSnarePlayerSnapshot Full = Backend->GetPlayerSnapshot();
		if (const UDataTable* const Table = SnareQuestChain::LoadTable())
		{
			if (const FQuestChainRow* const Row = SnareQuestChain::FindStep(Table, 1))
			{
				for (const FQuestChainNeed& Need : Row->Needs)
				{
					Full.AddItem(Need.ItemId, Need.Count);
				}
			}
		}
		Backend->ApplyPlayerSnapshot(Full, /*bFromBackend=*/true);
	}
	TestTrue(TEXT("재료 채우면 받기 가능"), Screen->CanClaimChain());
	const int32 CallsBefore = Calls.Num();
	TestTrue(TEXT("받기 요청을 띄운다"), Screen->RequestChainComplete());
	TestEqual(TEXT("받기 함수"), Calls.Num() > CallsBefore ? Calls[CallsBefore].FunctionName : FString(), FString(TEXT("CompleteQuestChainStep")));
	TestEqual(TEXT("step 인자 = 지금 단계(1)"), ChainSteps.Num() > CallsBefore ? ChainSteps[CallsBefore] : -1.0, 1.0);
	TestEqual(TEXT("답 전 코인 그대로"), Backend->GetPlayerSnapshot().CoinBalance, TestCoins);
	TestEqual(TEXT("답 전 단계 그대로"), Screen->GetChainStep(), 0);
	TestFalse(TEXT("답 전엔 두 번째 받기 불가"), Screen->RequestChainComplete());

	// ④ 거절 답 — 사유가 남고 단계는 서버에서 다시 받는다(손으로 안 올림).
	if (Calls.Num() > CallsBefore)
	{
		Calls[CallsBefore].OnDone(true, ParseJson(TEXT("{\"error\":\"재료가 모자란다\",\"code\":\"chain_short\"}")), FString());
	}
	TestFalse(TEXT("거절 사유를 삼키지 않는다"), Screen->GetMessage().IsEmpty());
	TestEqual(TEXT("거절 뒤 단계 다시 묻는다"), Calls.Num() > CallsBefore + 1 ? Calls[CallsBefore + 1].FunctionName : FString(), FString(TEXT("GetQuestChain")));
	TestEqual(TEXT("거절 뒤에도 단계 그대로"), Screen->GetChainStep(), 0);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}


// 3층 캡처용 미리보기 — 서버 없이 표로 단계를 채우고, 받기를 눌러도 서버를 부르지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenQuestChainPreviewTest, "Snarehaven.Quest.Screen.ChainPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenQuestChainPreviewTest::RunTest(const FString& Parameters)
{
	int32 CallCount = 0;
	USnareBackendSubsystem::TestServerOverride =
		[&CallCount](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			++CallCount;
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UQuestScreenWidget* const Screen = CreateWidget<UQuestScreenWidget>(World, UQuestScreenWidget::StaticClass());
	TSharedPtr<SWidget> Slate = Screen->TakeWidget();
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	Screen->BindToBackend(Backend);

	Screen->SetChainPreview(5);
	TestTrue(TEXT("미리보기 켜짐"), Screen->IsChainPreview());
	TestTrue(TEXT("펴짐"), Screen->IsOpen());
	TestTrue(TEXT("줄기 탭"), Screen->GetTab() == EQuestTab::Chain);
	TestEqual(TEXT("5단계 차례 = 끝낸 4"), Screen->GetChainStep(), 4);
	TestFalse(TEXT("재료 없으면 받기 불가"), Screen->CanClaimChain());
	TestFalse(TEXT("받기를 눌러도 요청 없음"), Screen->RequestChainComplete());
	TestEqual(TEXT("서버 호출 0"), CallCount, 0);
	TestEqual(TEXT("단계 그대로"), Screen->GetChainStep(), 4);
	TestFalse(TEXT("확인 중 표시"), Screen->GetMessage().IsEmpty());

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// 3층 캡처용 `-SnareQuestOpen=Chain` — 미리보기가 아니라 줄기 탭으로 열고, 목록 답이 오면 서버 `GetQuestChain` 을 실제로 묻는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenQuestOpenOnChainTest, "Snarehaven.Quest.Screen.OpenOnChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenQuestOpenOnChainTest::RunTest(const FString& Parameters)
{
	TArray<FQuestCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FQuestCall Call;
			Call.FunctionName = FunctionName;
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UQuestScreenWidget* const Screen = CreateWidget<UQuestScreenWidget>(World, UQuestScreenWidget::StaticClass());
	TSharedPtr<SWidget> Slate = Screen->TakeWidget();
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	Screen->BindToBackend(Backend);

	Screen->OpenOnChainTab();
	TestTrue(TEXT("펴짐"), Screen->IsOpen());
	TestTrue(TEXT("ActiveTab == Chain"), Screen->GetTab() == EQuestTab::Chain);
	TestFalse(TEXT("bChainPreview == false"), Screen->IsChainPreview());
	TestTrue(TEXT("첫 요청은 목록"), Calls.Num() == 1 && Calls[0].FunctionName == TEXT("GetQuests"));
	if (Calls.Num() == 1)
	{
		Calls[0].OnDone(true, ParseJson(TEXT("{\"quests\":[]}")), FString());
	}
	const int32 AskIndex = Calls.IndexOfByPredicate([](const FQuestCall& C) { return C.FunctionName == TEXT("GetQuestChain"); });
	TestTrue(TEXT("목록 답 뒤 서버 GetQuestChain 을 물었다"), AskIndex != INDEX_NONE);
	if (AskIndex != INDEX_NONE)
	{
		Calls[AskIndex].OnDone(true, ParseJson(TEXT("{\"step\":2,\"total\":5,\"done\":false}")), FString());
	}
	TestEqual(TEXT("서버가 준 단계"), Screen->GetChainStep(), 2);
	TestFalse(TEXT("미리보기 아님"), Screen->IsChainPreview());

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// M105-06 빈 길 0 단정 — 받기를 누른 뒤 서버 답이 오기 전에는 보상 글·코인·단계가 UI 에 먼저 뜨지 않고,
// 연결 실패·거절 답에도 안 뜬다. 성공 답이 와도 코인·단계는 서버 재조회 답이 와야 바뀐다 (CLAUDE.md 8).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenQuestChainNoEarlyRewardTest, "Snarehaven.Quest.Screen.ChainNoEarlyReward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenQuestChainNoEarlyRewardTest::RunTest(const FString& Parameters)
{
	TArray<FQuestCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FQuestCall Call;
			Call.FunctionName = FunctionName;
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UQuestScreenWidget* const Screen = CreateWidget<UQuestScreenWidget>(World, UQuestScreenWidget::StaticClass());
	TSharedPtr<SWidget> Slate = Screen->TakeWidget();
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.CoinBalance = TestCoins;
	if (const UDataTable* const Table = SnareQuestChain::LoadTable())
	{
		if (const FQuestChainRow* const Row = SnareQuestChain::FindStep(Table, 1))
		{
			for (const FQuestChainNeed& Need : Row->Needs)
			{
				Snapshot.AddItem(Need.ItemId, Need.Count);
			}
		}
	}
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);

	Screen->SetTab(EQuestTab::Chain);
	const int32 AskIndex = Calls.IndexOfByPredicate([](const FQuestCall& C) { return C.FunctionName == TEXT("GetQuestChain"); });
	TestTrue(TEXT("줄기 단계를 물었다"), AskIndex != INDEX_NONE);
	if (AskIndex != INDEX_NONE)
	{
		Calls[AskIndex].OnDone(true, ParseJson(TEXT("{\"step\":0,\"total\":5,\"done\":false}")), FString());
	}
	TestTrue(TEXT("재료를 채워 받기 가능"), Screen->CanClaimChain());

	// ① 받기를 누른 직후(답 전) — 글·코인·단계 모두 그대로.
	int32 Before = Calls.Num();
	TestTrue(TEXT("받기 요청"), Screen->RequestChainComplete());
	TestTrue(TEXT("답 전 보상 글 없음"), Screen->GetMessage().IsEmpty());
	TestEqual(TEXT("답 전 코인"), Backend->GetPlayerSnapshot().CoinBalance, TestCoins);
	TestEqual(TEXT("답 전 단계"), Screen->GetChainStep(), 0);

	// ② 연결 실패 — 보상 글 없음(실패 사유만), 코인·단계 그대로.
	if (Calls.Num() > Before)
	{
		Calls[Before].OnDone(false, nullptr, TEXT("연결 끊김"));
	}
	TestFalse(TEXT("실패 사유는 남는다"), Screen->GetMessage().IsEmpty());
	TestFalse(TEXT("실패에 보상 글 없음"), Screen->GetMessage().ToString().Contains(TEXT("받았다")));
	TestEqual(TEXT("실패 뒤 코인"), Backend->GetPlayerSnapshot().CoinBalance, TestCoins);
	TestEqual(TEXT("실패 뒤 단계"), Screen->GetChainStep(), 0);

	// ③ 성공 답 — 글은 뜨지만 코인·단계는 서버 재조회 답이 와야 바뀐다. 재조회 요청이 나가야 한다.
	TestTrue(TEXT("실패 뒤 단계를 다시 묻는다"), Calls.Num() > Before && Calls.Last().FunctionName == TEXT("GetQuestChain"));
	if (Calls.Num() > Before)
	{
		Calls.Last().OnDone(true, ParseJson(TEXT("{\"step\":0,\"total\":5,\"done\":false}")), FString());
	}
	Before = Calls.Num();
	TestTrue(TEXT("다시 받기 요청"), Screen->RequestChainComplete());
	TestTrue(TEXT("두 번째 답 전 보상 글 없음"), Screen->GetMessage().IsEmpty());
	if (Calls.Num() > Before)
	{
		Calls[Before].OnDone(true, ParseJson(TEXT("{\"step\":1,\"coins\":7}")), FString());
	}
	TestFalse(TEXT("성공 답 뒤 보상 글"), Screen->GetMessage().IsEmpty());
	TestEqual(TEXT("재조회 전 코인은 클라가 안 바꿈"), Backend->GetPlayerSnapshot().CoinBalance, TestCoins);
	TestEqual(TEXT("재조회 전 단계는 클라가 안 올림"), Screen->GetChainStep(), 0);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
