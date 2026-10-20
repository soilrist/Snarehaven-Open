// ROADMAP M26-02 2층 검사 — 상자·뽑기·배낭 판의 서버 거절 글.
// 이 검사가 잡는 것: ① 서버에 못 닿았을 때(로그인 전 `network`) 개발자 문장(`… PlayFab 클라이언트 API …`)이 판에 그대로 뜨는 것
// ② 표에 없는 code 의 서버 사유 문장을 지우는 것 ③ 목장 글귀 공통 행(`locked` = 목장이 잠겼다)이 목장 밖 판에 뜨는 것
// 서버는 로그인 전 실제 길(`CallServerFunction` 이 바로 실패 답) 또는 `USnareBackendSubsystem::TestServerOverride` 가짜. 검사 끝에 반드시 비운다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.UI.ServerFailureText; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SceneComponent.h"
#include "Data/RanchFailureRow.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Inventory/BackpackComponent.h"
#include "Ranch/RanchSyncComponent.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/ChestScreenWidget.h"
#include "UI/GachaScreenWidget.h"

namespace SnareServerFailureTextTest
{
	/** `SnareBackendSubsystem.cpp` 로그인 전 실패 답 — 화면에 나오면 안 되는 개발자 문장. */
	const TCHAR* const OfflineSentence = TEXT("로그인 전이거나 PlayFab 클라이언트 API 가 없다");
	// 검사 전용 값 — 표에 없는 code·서버 문장 (Verification.md 6).
	const TCHAR* const UnknownCode = TEXT("test_unknown_code");
	const TCHAR* const UnknownSentence = TEXT("시험용 서버 문장");
	const TCHAR* const RanchOnlyCode = TEXT("locked");
	const TCHAR* const RanchOnlySentence = TEXT("시험용 잠김 문장");

	/** 게임 월드 + 로그인 안 한 백엔드 + 배낭 컴포넌트. */
	struct FRig
	{
		UWorld* World = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		UBackpackComponent* Backpack = nullptr;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));

			AActor* const Owner = World->SpawnActor<AActor>();
			USceneComponent* const Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Backpack = NewObject<UBackpackComponent>(Owner, TEXT("Backpack"));
			Backpack->RegisterComponent();
			Backpack->BindTargets(Backend, nullptr);
		}

		~FRig()
		{
			USnareBackendSubsystem::TestServerOverride = nullptr;
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	/** 다음 부름에 `{error, code}` 실패 답을 바로 주는 가짜 서버. */
	void AnswerNextWith(const FString& Code, const FString& Sentence)
	{
		USnareBackendSubsystem::TestServerOverride =
			[Code, Sentence](const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone OnDone)
			{
				const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
				Reply->SetStringField(TEXT("error"), Sentence);
				Reply->SetStringField(TEXT("code"), Code);
				OnDone(true, Reply, FString());
			};
	}
}

using namespace SnareServerFailureTextTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenServerFailureTextTest, "Snarehaven.UI.ServerFailureText.ThreeScreensUseTable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenServerFailureTextTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = LoadObject<UDataTable>(nullptr, URanchSyncComponent::GetFailureTablePath());
	const FRanchFailureRow* const NetworkRow = IsValid(Table)
		? Table->FindRow<FRanchFailureRow>(TEXT("network"), TEXT("ServerFailureTextTest"), false) : nullptr;
	if (!TestNotNull(TEXT("실패 문구 표 network 행"), NetworkRow))
	{
		return false;
	}
	const FString NetworkText = NetworkRow->Message.ToString();
	TestFalse(TEXT("표 network 문구가 비지 않았다"), NetworkText.IsEmpty());

	FRig Rig;
	USnareBackendSubsystem::TestServerOverride = nullptr;

	// 상자 — 로그인 전 실제 길.
	UChestScreenWidget* const Chest = CreateWidget<UChestScreenWidget>(Rig.World, UChestScreenWidget::StaticClass());
	UGachaScreenWidget* const Gacha = CreateWidget<UGachaScreenWidget>(Rig.World, UGachaScreenWidget::StaticClass());
	UBackpackScreenWidget* const Pack = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("상자 화면"), Chest) || !TestNotNull(TEXT("뽑기 화면"), Gacha) || !TestNotNull(TEXT("배낭 화면"), Pack))
	{
		return false;
	}
	const TSharedRef<SWidget> ChestSlate = Chest->TakeWidget();
	const TSharedRef<SWidget> GachaSlate = Gacha->TakeWidget();
	const TSharedRef<SWidget> PackSlate = Pack->TakeWidget();
	Chest->BindToChest(Rig.Backpack, 1, FString());
	Gacha->BindToBackend(Rig.Backend);
	Pack->BindTargets(Rig.Backend, Rig.Backpack, nullptr);

	Chest->PressOpen();
	const FString ChestOffline = Chest->GetResultText().ToString();
	TestEqual(TEXT("상자 오프라인 = 표 문구"), ChestOffline, NetworkText);
	TestFalse(TEXT("상자 오프라인에 PlayFab 없음"), ChestOffline.Contains(TEXT("PlayFab")));

	Gacha->RequestPull(USnareBackendSubsystem::GetCoinCurrencyCode(), 1);
	const FString GachaOffline = Gacha->GetStatusText().ToString();
	TestEqual(TEXT("뽑기 오프라인 = 표 문구"), GachaOffline, NetworkText);
	TestFalse(TEXT("뽑기 오프라인에 PlayFab 없음"), GachaOffline.Contains(TEXT("PlayFab")));

	// 배낭 — 컴포넌트가 판정 끝에 내는 답(`Finish` → `OnReply`) 그대로.
	Rig.Backpack->OnReply.Broadcast(false, TEXT("network"), OfflineSentence);
	const FString PackOffline = Pack->GetStatusText().ToString();
	TestEqual(TEXT("배낭 오프라인 = 표 문구"), PackOffline, NetworkText);
	TestFalse(TEXT("배낭 오프라인에 PlayFab 없음"), PackOffline.Contains(TEXT("PlayFab")));

	// 표에 없는 code — 서버 문장 그대로.
	AnswerNextWith(UnknownCode, UnknownSentence);
	Chest->PressOpen();
	TestEqual(TEXT("상자 모르는 code = 서버 문장"), Chest->GetResultText().ToString(), FString(UnknownSentence));
	Gacha->RequestPull(USnareBackendSubsystem::GetCoinCurrencyCode(), 1);
	TestEqual(TEXT("뽑기 모르는 code = 서버 문장"), Gacha->GetStatusText().ToString(), FString(UnknownSentence));
	Rig.Backpack->OnReply.Broadcast(false, UnknownCode, UnknownSentence);
	TestEqual(TEXT("배낭 모르는 code = 서버 문장"), Pack->GetStatusText().ToString(), FString(UnknownSentence));

	// 목장 글귀 공통 행(`locked`)은 목장 밖 판에 안 뜬다.
	AnswerNextWith(RanchOnlyCode, RanchOnlySentence);
	Chest->PressOpen();
	TestEqual(TEXT("상자 locked = 서버 문장"), Chest->GetResultText().ToString(), FString(RanchOnlySentence));
	Gacha->RequestPull(USnareBackendSubsystem::GetCoinCurrencyCode(), 1);
	TestEqual(TEXT("뽑기 locked = 서버 문장"), Gacha->GetStatusText().ToString(), FString(RanchOnlySentence));
	Rig.Backpack->OnReply.Broadcast(false, RanchOnlyCode, RanchOnlySentence);
	TestEqual(TEXT("배낭 locked = 서버 문장"), Pack->GetStatusText().ToString(), FString(RanchOnlySentence));

	// 목장 길은 그대로 공통 행을 본다.
	TestEqual(TEXT("목장 길 network = 표 문구"),
		URanchSyncComponent::DescribeFailureCode(Table, TEXT("FeedRanchUpkeep"), TEXT("network"), OfflineSentence, nullptr).ToString(),
		NetworkText);

	AddInfo(FString::Printf(TEXT("[ServerFailureText] 오프라인 상자 \"%s\" · 뽑기 \"%s\" · 배낭 \"%s\""),
		*ChestOffline, *GachaOffline, *PackOffline));
	return true;
}

#endif
