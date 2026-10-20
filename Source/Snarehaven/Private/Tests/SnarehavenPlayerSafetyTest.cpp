// 신고·차단 2층 검사 (사람 결정 10-16, 앱스토어 1.2) — `USnareBlockListSubsystem` + 판 `UPlayerSafetyWidget`.
// 잡는 것: ① 이유 4개가 서버 `REPORT_REASONS` 순서 ② 신고 인자 `{targetPlayerId, reason}` ③ 차단 목록 읽기
// ④ **답 전엔 차단 목록을 안 바꾼다** ⑤ 거절 사유를 삼키지 않는다 ⑥ 차단한 사람은 「차단한 플레이어」 ⑦ 토글이 해제를 부른다.
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신한다. 검사 끝에 반드시 비운다.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareBlockListSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UI/PlayerSafetyWidget.h"

namespace
{
	struct FSafetyCall
	{
		FString FunctionName;
		TSharedPtr<FJsonObject> Args;
		USnareBackendSubsystem::FServerCallDone OnDone;
	};

	TSharedPtr<FJsonObject> ParseSafetyJson(const TCHAR* Json)
	{
		TSharedPtr<FJsonObject> Object;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
		return Object;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerSafetyRulesTest, "Snarehaven.UI.PlayerSafety.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerSafetyRulesTest::RunTest(const FString& Parameters)
{
	// ① 서버 `REPORT_REASONS = ["name","harass","cheat","other"]` 와 같은 순서.
	const TArray<FName>& Reasons = USnareBlockListSubsystem::GetReportReasons();
	if (TestEqual(TEXT("이유 4개"), Reasons.Num(), 4))
	{
		TestEqual(TEXT("첫 이유"), Reasons[0], FName(TEXT("name")));
		TestEqual(TEXT("끝 이유"), Reasons[3], FName(TEXT("other")));
	}
	for (const FName Reason : Reasons)
	{
		TestFalse(FString::Printf(TEXT("%s 이유 글이 비지 않는다"), *Reason.ToString()),
			UPlayerSafetyWidget::DescribeReason(Reason).IsEmpty());
	}

	// ② 신고 인자.
	const TSharedRef<FJsonObject> Args = USnareBlockListSubsystem::MakeReportArgs(TEXT("PFTARGET"), TEXT("cheat"));
	TestEqual(TEXT("targetPlayerId"), Args->GetStringField(TEXT("targetPlayerId")), FString(TEXT("PFTARGET")));
	TestEqual(TEXT("reason"), Args->GetStringField(TEXT("reason")), FString(TEXT("cheat")));

	// ③ 차단 목록 읽기 — id 없는 줄은 뺀다, blocks 없는 답은 거짓·목록 그대로.
	USnareBlockListSubsystem* const List = NewObject<USnareBlockListSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));
	TestTrue(TEXT("blocks 를 읽는다"), List->ApplyBlocksReply(ParseSafetyJson(
		TEXT("{\"blocks\":[{\"id\":\"PFA\",\"name\":\"a\"},{\"name\":\"no id\"},{\"id\":\"PFB\"}]}"))));
	TestEqual(TEXT("id 있는 둘만"), List->NumBlocked(), 2);
	TestTrue(TEXT("PFA 차단"), List->IsBlocked(TEXT("PFA")));
	TestFalse(TEXT("빈 id 는 차단 아님"), List->IsBlocked(TEXT("")));
	TestFalse(TEXT("blocks 없는 답은 거짓"), List->ApplyBlocksReply(ParseSafetyJson(TEXT("{\"error\":\"x\"}"))));
	TestEqual(TEXT("거짓 답은 목록을 안 바꾼다"), List->NumBlocked(), 2);
	TestFalse(TEXT("모르는 이유는 안 보낸다"), List->RequestReport(TEXT("PFA"), TEXT("spam"), nullptr));
	TestFalse(TEXT("몸 없으면 차단 아님"), USnareBlockListSubsystem::IsPawnBlocked(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenPlayerSafetyScreenTest, "Snarehaven.UI.PlayerSafety.Screen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenPlayerSafetyScreenTest::RunTest(const FString& Parameters)
{
	TArray<FSafetyCall> Calls;
	USnareBackendSubsystem::TestServerOverride =
		[&Calls](const FString& FunctionName, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			FSafetyCall Call;
			Call.FunctionName = FunctionName;
			Call.Args = Args;
			Call.OnDone = MoveTemp(OnDone);
			Calls.Add(MoveTemp(Call));
		};

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	USnareBackendSubsystem* const Backend = NewObject<USnareBackendSubsystem>(GameInstance);
	USnareBlockListSubsystem* const List = NewObject<USnareBlockListSubsystem>(GameInstance);
	List->BindBackend(Backend);

	UPlayerSafetyWidget* const Screen = CreateWidget<UPlayerSafetyWidget>(World, UPlayerSafetyWidget::StaticClass());
	const TSharedPtr<SWidget> Slate = Screen->TakeWidget();
	Screen->BindBlockList(List);
	Screen->PeerSource = []()
	{
		FGiftPeer Far; Far.PlayFabId = TEXT("PFFAR"); Far.Name = FText::FromString(TEXT("far")); Far.DistanceCm = 900.f;
		FGiftPeer Near; Near.PlayFabId = TEXT("PFNEAR"); Near.Name = FText::FromString(TEXT("near")); Near.DistanceCm = 100.f;
		return TArray<FGiftPeer>{ Far, Near };
	};
	Screen->Reload();
	TestEqual(TEXT("같은 방 둘"), Screen->GetPeers().Num(), 2);
	if (Screen->GetPeers().Num() == 2)
	{
		TestEqual(TEXT("가까운 사람이 위"), Screen->GetPeers()[0].PlayFabId, FString(TEXT("PFNEAR")));
	}

	const UButton* const ReportButton = Screen->WidgetTree->FindWidget<UButton>(TEXT("SafetyReportButton"));
	const UButton* const BlockButton = Screen->WidgetTree->FindWidget<UButton>(TEXT("SafetyBlockButton"));
	const UTextBlock* const BlockLabel = Screen->WidgetTree->FindWidget<UTextBlock>(TEXT("SafetyBlockButtonText"));
	if (!TestNotNull(TEXT("신고 단추"), ReportButton) || !TestNotNull(TEXT("차단 단추"), BlockButton) || !TestNotNull(TEXT("차단 글"), BlockLabel))
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return false;
	}
	TestFalse(TEXT("사람 안 고르면 차단 꺼짐"), BlockButton->GetIsEnabled());
	TestFalse(TEXT("사람 안 고르면 신고 못 함"), Screen->RequestReport());

	// 신고 — 이유를 골라야 켜진다, 인자는 고른 사람·이유.
	Screen->SelectPeer(TEXT("PFNEAR"));
	TestFalse(TEXT("이유 안 고르면 신고 꺼짐"), ReportButton->GetIsEnabled());
	TestTrue(TEXT("사람 고르면 차단 켜짐"), BlockButton->GetIsEnabled());
	Screen->SelectReason(TEXT("harass"));
	TestTrue(TEXT("이유 고르면 신고 켜짐"), ReportButton->GetIsEnabled());
	TestTrue(TEXT("신고를 묻는다"), Screen->RequestReport());
	if (TestEqual(TEXT("서버 호출 1"), Calls.Num(), 1))
	{
		TestEqual(TEXT("ReportPlayer"), Calls[0].FunctionName, FString(TEXT("ReportPlayer")));
		TestEqual(TEXT("대상"), Calls[0].Args->GetStringField(TEXT("targetPlayerId")), FString(TEXT("PFNEAR")));
		TestEqual(TEXT("이유"), Calls[0].Args->GetStringField(TEXT("reason")), FString(TEXT("harass")));
		TestTrue(TEXT("답 전엔 묻는 중"), Screen->IsPending());
		TestFalse(TEXT("묻는 중엔 두 번 안 보낸다"), Screen->RequestReport());
		// ⑤ 거절 사유를 그대로 보인다.
		Calls[0].OnDone(true, ParseSafetyJson(TEXT("{\"error\":\"already today\",\"code\":\"already\"}")), FString());
		TestEqual(TEXT("거절 사유 문장"), Screen->GetMessage().ToString(), FString(TEXT("already today")));
		TestFalse(TEXT("답 뒤엔 안 묻는 중"), Screen->IsPending());
	}

	// 차단 — 답 전엔 목록 그대로, 답의 blocks 를 옮긴 뒤 이름이 가려지고 단추가 「차단 해제」.
	TestTrue(TEXT("차단을 묻는다"), Screen->RequestToggleBlock());
	if (TestEqual(TEXT("서버 호출 2"), Calls.Num(), 2))
	{
		TestEqual(TEXT("BlockPlayer"), Calls[1].FunctionName, FString(TEXT("BlockPlayer")));
		TestEqual(TEXT("이름도 보낸다"), Calls[1].Args->GetStringField(TEXT("name")), FString(TEXT("near")));
		TestFalse(TEXT("답 전엔 차단 안 됨"), List->IsBlocked(TEXT("PFNEAR")));
		Calls[1].OnDone(true, ParseSafetyJson(TEXT("{\"blocked\":true,\"blocks\":[{\"id\":\"PFNEAR\",\"name\":\"near\"}],\"limit\":100}")), FString());
		TestTrue(TEXT("답 뒤 차단"), List->IsBlocked(TEXT("PFNEAR")));
		TestEqual(TEXT("줄 이름이 가려진다"), Screen->GetShownName(Screen->GetPeers()[0]).ToString(), FString(TEXT("차단한 플레이어")));
		TestEqual(TEXT("단추가 해제로"), BlockLabel->GetText().ToString(), FString(TEXT("차단 해제")));
	}

	// 토글 — 이미 차단이면 해제를 부른다.
	TestTrue(TEXT("해제를 묻는다"), Screen->RequestToggleBlock());
	if (TestEqual(TEXT("서버 호출 3"), Calls.Num(), 3))
	{
		TestEqual(TEXT("UnblockPlayer"), Calls[2].FunctionName, FString(TEXT("UnblockPlayer")));
		Calls[2].OnDone(true, ParseSafetyJson(TEXT("{\"unblocked\":true,\"blocks\":[],\"limit\":100}")), FString());
		TestFalse(TEXT("해제 뒤 차단 아님"), List->IsBlocked(TEXT("PFNEAR")));
		TestEqual(TEXT("단추가 차단으로"), BlockLabel->GetText().ToString(), FString(TEXT("차단")));
	}

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
