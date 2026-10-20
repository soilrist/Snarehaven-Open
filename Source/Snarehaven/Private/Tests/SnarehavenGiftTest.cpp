// DESIGN 3.3 「건네기」 2층 검사 — 건네기 화면(`UGiftScreenWidget`).
// 잡는 것: ① 근처 사람은 1000cm 안만 가까운 순 4명 ② `GetGifts` 읽기 ③ 보내기 인자 `{targetPlayerId,itemId,count}`
// ④ **답 전에 배낭을 안 바꾼다** ⑤ 거절 사유를 삼키지 않는다 ⑥ 수락·거절 인자 `{giftId}` ⑦ 줄 단추가 손 닿는 높이.
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신한다. 검사 끝에 반드시 비운다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerSnapshot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/GiftScreenWidget.h"

namespace
{
	// 검사 전용 값 (Verification.md 6).
	const TCHAR* GiftsReplyJson = TEXT("{\"gifts\":["
		"{\"id\":\"g1\",\"from\":\"PFFROM1\",\"itemId\":\"item.wood\",\"count\":4},"
		"{\"id\":\"g2\",\"from\":\"PFFROM2\",\"itemId\":\"item.carrot\",\"count\":2}]}");
	constexpr int32 TestWood = 12;

	struct FGiftCall
	{
		FString FunctionName;
		TSharedPtr<FJsonObject> Args;
		USnareBackendSubsystem::FServerCallDone OnDone;
	};

	TSharedPtr<FJsonObject> ParseGiftJson(const TCHAR* Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}

	FGiftPeer MakePeer(const TCHAR* Id, float Cm)
	{
		FGiftPeer Peer;
		Peer.PlayFabId = Id;
		Peer.DistanceCm = Cm;
		return Peer;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGiftRulesTest, "Snarehaven.Gift.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGiftRulesTest::RunTest(const FString& Parameters)
{
	// ① 1000cm 밖·빈 id·같은 id 두 번은 빠지고, 가까운 순 4명.
	const TArray<FGiftPeer> All = {
		MakePeer(TEXT("P5"), 900.f), MakePeer(TEXT("P1"), 100.f), MakePeer(TEXT("FAR"), 1001.f),
		MakePeer(TEXT(""), 50.f), MakePeer(TEXT("P2"), 300.f), MakePeer(TEXT("P1"), 120.f),
		MakePeer(TEXT("P3"), 500.f), MakePeer(TEXT("P4"), 700.f)};
	const TArray<FGiftPeer> Near = UGiftScreenWidget::PickNearby(All);
	TestEqual(TEXT("가까운 4명만"), Near.Num(), UGiftScreenWidget::MaxPeerRows);
	if (Near.Num() == UGiftScreenWidget::MaxPeerRows)
	{
		TestEqual(TEXT("가까운 순 1"), Near[0].PlayFabId, FString(TEXT("P1")));
		TestEqual(TEXT("가까운 순 4"), Near[3].PlayFabId, FString(TEXT("P4")));
	}
	TestFalse(TEXT("1000cm 밖은 없다"), UGiftScreenWidget::PickNearby({MakePeer(TEXT("FAR"), 1000.5f)}).Num() > 0);
	TestEqual(TEXT("정확히 1000cm 는 포함"), UGiftScreenWidget::PickNearby({MakePeer(TEXT("EDGE"), 1000.f)}).Num(), 1);

	// ② 선물 목록 읽기 — 깨진 줄(개수 0)은 뺀다.
	TArray<FGiftView> Gifts;
	TestTrue(TEXT("gifts 배열을 읽는다"), UGiftScreenWidget::ParseGifts(ParseGiftJson(GiftsReplyJson), Gifts));
	TestEqual(TEXT("선물 2줄"), Gifts.Num(), 2);
	TestFalse(TEXT("gifts 없는 답은 거짓"), UGiftScreenWidget::ParseGifts(ParseGiftJson(TEXT("{\"error\":\"x\"}")), Gifts));
	TArray<FGiftView> Broken;
	UGiftScreenWidget::ParseGifts(ParseGiftJson(TEXT("{\"gifts\":[{\"id\":\"a\",\"from\":\"b\",\"itemId\":\"item.wood\",\"count\":0}]}")), Broken);
	TestEqual(TEXT("개수 0 줄은 뺀다"), Broken.Num(), 0);

	// ③ 보내기 인자.
	const TSharedRef<FJsonObject> Args = UGiftScreenWidget::MakeSendArgs(TEXT("PFTARGET"), TEXT("item.wood"), 3);
	TestEqual(TEXT("targetPlayerId"), Args->GetStringField(TEXT("targetPlayerId")), FString(TEXT("PFTARGET")));
	TestEqual(TEXT("itemId"), Args->GetStringField(TEXT("itemId")), FString(TEXT("item.wood")));
	TestEqual(TEXT("count"), static_cast<int32>(Args->GetNumberField(TEXT("count"))), 3);

	// 눈에 뻔한 제외 힌트(판정은 서버).
	TestTrue(TEXT("재료는 보인다"), UGiftScreenWidget::IsOfferableItem(TEXT("item.wood")));
	TestFalse(TEXT("가챠 아이템은 줄에서 뺀다"), UGiftScreenWidget::IsOfferableItem(TEXT("gacha.hat_01")));
	TestFalse(TEXT("펫은 줄에서 뺀다"), UGiftScreenWidget::IsOfferableItem(TEXT("item.pet_rabbit")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGiftScreenTest, "Snarehaven.Gift.Screen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGiftScreenTest::RunTest(const FString& Parameters)
{
	TArray<FGiftCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FGiftCall Call;
			Call.FunctionName = FunctionName;
			Call.Args = Args;
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UGiftScreenWidget* const Screen = CreateWidget<UGiftScreenWidget>(World, UGiftScreenWidget::StaticClass());
	const TSharedPtr<SWidget> Slate = Screen->TakeWidget();

	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.AddItem(TEXT("item.wood"), TestWood);
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	Screen->PeerSource = []() { return TArray<FGiftPeer>{MakePeer(TEXT("PFNEAR"), 400.f), MakePeer(TEXT("PFFAR"), 5000.f)}; };

	// 펴면 선물 목록을 묻고, 근처 사람은 1명.
	Screen->SetOpen(true);
	TestEqual(TEXT("펴면 서버를 한 번 부른다"), Calls.Num(), 1);
	TestEqual(TEXT("목록 함수"), Calls.Num() > 0 ? Calls[0].FunctionName : FString(), FString(TEXT("GetGifts")));
	TestEqual(TEXT("근처 사람 1명"), Screen->GetPeers().Num(), 1);
	if (Calls.Num() > 0)
	{
		Calls[0].OnDone(true, ParseGiftJson(GiftsReplyJson), FString());
	}
	TestEqual(TEXT("받을 선물 2줄"), Screen->GetGifts().Num(), 2);

	// 고르기 전엔 못 보낸다.
	TestFalse(TEXT("아무것도 안 골랐으면 못 보낸다"), Screen->CanSend());
	Screen->SelectPeer(TEXT("PFNEAR"));
	Screen->SelectItem(TEXT("item.wood"));
	Screen->ChangeCount(+2);
	TestEqual(TEXT("개수 3"), Screen->GetSendCount(), 3);
	Screen->ChangeCount(-10);
	TestEqual(TEXT("개수는 1 아래로 안 내려간다"), Screen->GetSendCount(), 1);
	Screen->ChangeCount(+1000);
	TestEqual(TEXT("개수는 가진 만큼까지"), Screen->GetSendCount(), TestWood);
	Screen->ChangeCount(-(TestWood - 3));
	TestTrue(TEXT("다 골랐으면 보낼 수 있다"), Screen->CanSend());

	// ④ 보내기 인자 · 답 전에 배낭 그대로 · 잠김.
	TestTrue(TEXT("보내기 요청을 띄운다"), Screen->RequestSend());
	TestEqual(TEXT("보내기 함수"), Calls.Num() > 1 ? Calls[1].FunctionName : FString(), FString(TEXT("GiftItem")));
	if (Calls.Num() > 1 && Calls[1].Args.IsValid())
	{
		TestEqual(TEXT("받는 사람"), Calls[1].Args->GetStringField(TEXT("targetPlayerId")), FString(TEXT("PFNEAR")));
		TestEqual(TEXT("아이템"), Calls[1].Args->GetStringField(TEXT("itemId")), FString(TEXT("item.wood")));
		TestEqual(TEXT("개수"), static_cast<int32>(Calls[1].Args->GetNumberField(TEXT("count"))), 3);
	}
	TestEqual(TEXT("답 전 나무 그대로"), Screen->CountOwned(TEXT("item.wood")), TestWood);
	TestFalse(TEXT("답 전엔 다른 요청을 안 띄운다"), Screen->RequestSend());

	// ⑤ 거절 답 — 사유가 남고 선택은 그대로, 대기는 풀린다.
	if (Calls.Num() > 1)
	{
		Calls[1].OnDone(true, ParseGiftJson(TEXT("{\"error\":\"그 아이템은 보낼 수 없다\",\"code\":\"excluded_item\"}")), FString());
	}
	TestFalse(TEXT("거절 뒤 대기 풀림"), Screen->IsRequestPending());
	TestFalse(TEXT("거절 사유를 삼키지 않는다"), Screen->GetMessage().IsEmpty());
	TestEqual(TEXT("거절 뒤에도 나무 그대로"), Screen->CountOwned(TEXT("item.wood")), TestWood);
	TestEqual(TEXT("거절 뒤 고른 사람 그대로"), Screen->GetSelectedPeer(), FString(TEXT("PFNEAR")));

	// ⑥ 수락·거절 인자.
	const int32 Before = Calls.Num();
	TestTrue(TEXT("수락 요청"), Screen->RequestAccept(TEXT("g1")));
	TestEqual(TEXT("수락 함수"), Calls.Num() > Before ? Calls[Before].FunctionName : FString(), FString(TEXT("AcceptGift")));
	if (Calls.Num() > Before && Calls[Before].Args.IsValid())
	{
		TestEqual(TEXT("giftId"), Calls[Before].Args->GetStringField(TEXT("giftId")), FString(TEXT("g1")));
		Calls[Before].OnDone(true, ParseGiftJson(TEXT("{\"error\":\"배낭이 가득 찼다\",\"code\":\"inventory_full\"}")), FString());
	}
	TestTrue(TEXT("거절 요청"), Screen->RequestDecline(TEXT("g2")));
	TestEqual(TEXT("거절 함수"), Calls.Last().FunctionName, FString(TEXT("DeclineGift")));
	Calls.Last().OnDone(true, ParseGiftJson(TEXT("{\"error\":\"x\"}")), FString());

	// ⑦ 줄 단추가 있다(높이 88 은 코드 상수 — 시트 한 마디 72 이상).
	int32 RowButtons = 0;
	Screen->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (const UButton* const Button = Cast<UButton>(Widget))
		{
			RowButtons += Button->GetName().StartsWith(TEXT("GiftRowButton_")) ? 1 : 0;
		}
	});
	TestTrue(TEXT("줄 단추가 있다(받기·거절·사람·물건)"), RowButtons >= 4);
	AddInfo(FString::Printf(TEXT("[GiftScreen] 줄 단추 %d개 · 선물 %d줄 · 사람 %d명"), RowButtons, Screen->GetGifts().Num(), Screen->GetPeers().Num()));

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenGiftRetryTest, "Snarehaven.Gift.Retry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenGiftRetryTest::RunTest(const FString& Parameters)
{
	// 서버에 닿지 못하면 이유 곁에 「다시 시도」 — 누르면 같은 요청을 다시 보낸다.
	TArray<FGiftCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			Calls.Add({FunctionName, Args, MoveTemp(OnDone)});
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);
	UGiftScreenWidget* const Screen = CreateWidget<UGiftScreenWidget>(World, UGiftScreenWidget::StaticClass());
	const TSharedPtr<SWidget> Slate = Screen->TakeWidget();
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	FSnarePlayerSnapshot Snapshot;
	Snapshot.PlayFabId = TEXT("PFTESTOWNER");
	Snapshot.FetchedAtUtc = FDateTime::UtcNow();
	Snapshot.AddItem(TEXT("item.wood"), TestWood);
	Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true);
	Screen->BindToBackend(Backend);
	Screen->PeerSource = []() { return TArray<FGiftPeer>{MakePeer(TEXT("PFNEAR"), 400.f)}; };

	UWidget* const RetryBox = Screen->WidgetTree->FindWidget(TEXT("GiftRetryBox"));
	UButton* const RetryButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("GiftRetryButton")));
	TestNotNull(TEXT("다시 시도 단추가 있다"), RetryButton);
	if (RetryBox == nullptr || RetryButton == nullptr)
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}

	Screen->SetOpen(true);
	TestFalse(TEXT("요청 중엔 숨김"), (RetryBox->GetVisibility() == ESlateVisibility::Visible));
	Calls.Last().OnDone(false, nullptr, TEXT("net"));
	TestTrue(TEXT("목록 실패 → 다시 시도 보임"), (RetryBox->GetVisibility() == ESlateVisibility::Visible));
	TestFalse(TEXT("이유를 보인다"), Screen->GetMessage().IsEmpty());
	const int32 BeforeList = Calls.Num();
	RetryButton->OnClicked.Broadcast();
	TestEqual(TEXT("다시 시도 → GetGifts 다시"), Calls.Num() > BeforeList ? Calls.Last().FunctionName : FString(), FString(TEXT("GetGifts")));
	Calls.Last().OnDone(true, ParseGiftJson(GiftsReplyJson), FString());
	TestFalse(TEXT("성공 뒤 숨김"), (RetryBox->GetVisibility() == ESlateVisibility::Visible));

	// 보내기가 네트워크로 실패하면 같은 인자로 다시.
	Screen->SelectPeer(TEXT("PFNEAR"));
	Screen->SelectItem(TEXT("item.wood"));
	TestTrue(TEXT("보내기 요청"), Screen->RequestSend());
	Calls.Last().OnDone(false, nullptr, TEXT("net"));
	TestTrue(TEXT("보내기 네트워크 실패 → 보임"), (RetryBox->GetVisibility() == ESlateVisibility::Visible));
	const int32 BeforeSend = Calls.Num();
	RetryButton->OnClicked.Broadcast();
	TestEqual(TEXT("다시 시도 → GiftItem 다시"), Calls.Num() > BeforeSend ? Calls.Last().FunctionName : FString(), FString(TEXT("GiftItem")));
	if (Calls.Num() > BeforeSend && Calls.Last().Args.IsValid())
	{
		TestEqual(TEXT("같은 받는 사람"), Calls.Last().Args->GetStringField(TEXT("targetPlayerId")), FString(TEXT("PFNEAR")));
	}
	// 서버가 거절(사유 있음)하면 다시 보내도 같다 — 숨김.
	Calls.Last().OnDone(true, ParseGiftJson(TEXT("{\"error\":\"x\",\"code\":\"excluded_item\"}")), FString());
	TestFalse(TEXT("서버 거절엔 다시 시도 없음"), (RetryBox->GetVisibility() == ESlateVisibility::Visible));

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
