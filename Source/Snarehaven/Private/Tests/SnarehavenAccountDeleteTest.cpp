// 계정 삭제 2층 검사 (DESIGN 4.4 「계정 삭제」 · Apple 5.1.1(v)).
// 이 검사가 잡는 것: ① 서버 답이 오기 전에 로컬 캐시를 지우는 것(CLAUDE.md 8) ② 서버가 거절·실패했는데 캐시를 지우는 것
// ③ 로비 버튼 한 번에 바로 요청이 나가는 것(확인 두 번이 있어야 한다) ④ 취소한 뒤에도 단계가 남는 것.
// 서버(`DeleteMyAccount`)는 `TestServerOverride` 가짜로 붙잡아 두고 답을 골라 준다 — 실제 왕복은 3층 몫.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnarePlayerCacheSave.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "UI/LobbyWidget.h"

namespace
{
	using FDone = USnareBackendSubsystem::FServerCallDone;

	/** 호출을 붙잡아 두고 검사가 고른 답을 나중에 준다. 사라질 때 가짜를 비운다. */
	struct FDeleteRig
	{
		FString LastFunction;
		FString LastConfirm;
		TArray<FDone> Waiting;
		int32 Calls = 0;
		UGameInstance* GameInstance = nullptr;
		USnareBackendSubsystem* Backend = nullptr;

		FDeleteRig()
		{
			USnareBackendSubsystem::TestServerOverride =
				[this](const FString& Name, const TSharedPtr<FJsonObject>& Args, FDone OnDone)
				{
					++Calls;
					LastFunction = Name;
					LastConfirm.Reset();
					if (Args.IsValid())
					{
						Args->TryGetStringField(TEXT("confirm"), LastConfirm);
					}
					Waiting.Add(MoveTemp(OnDone));
				};
			GameInstance = NewObject<UGameInstance>(GEngine);
			GameInstance->InitializeStandalone();
			Backend = GameInstance->GetSubsystem<USnareBackendSubsystem>();
		}

		~FDeleteRig()
		{
			USnareBackendSubsystem::TestServerOverride = nullptr;
			UGameplayStatics::DeleteGameInSlot(USnarePlayerCacheSave::GetSlotName(), 0);
			if (GameInstance != nullptr)
			{
				GameInstance->Shutdown();
			}
		}

		/** 백엔드가 준 것처럼 스냅샷을 올리고 캐시 파일을 적는다. */
		bool SeedCache()
		{
			FSnarePlayerSnapshot Snapshot;
			Snapshot.PlayFabId = TEXT("TESTDELETE01");
			Snapshot.CoinBalance = 40;
			return Backend->ApplyPlayerSnapshot(Snapshot, /*bFromBackend=*/true) && Backend->SaveLocalCache();
		}

		static bool CacheExists()
		{
			return UGameplayStatics::DoesSaveGameExist(USnarePlayerCacheSave::GetSlotName(), 0);
		}

		void Answer(bool bOk, const TSharedPtr<FJsonObject>& Response)
		{
			if (Waiting.IsEmpty())
			{
				return;
			}
			FDone Done = MoveTemp(Waiting[0]);
			Waiting.RemoveAt(0);
			Done(bOk, Response, bOk ? FString() : FString(TEXT("가짜 네트워크 오류")));
		}

		static TSharedPtr<FJsonObject> Deleted()
		{
			const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
			Reply->SetBoolField(TEXT("deleted"), true);
			Reply->SetBoolField(TEXT("fullyDeleted"), true);
			return Reply;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAccountDeleteBackendTest, "Snarehaven.Account.Delete.CacheGoesOnlyAfterServerSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAccountDeleteBackendTest::RunTest(const FString& Parameters)
{
	FDeleteRig Rig;
	if (!TestNotNull(TEXT("백엔드 창구"), Rig.Backend) || !TestTrue(TEXT("캐시 파일을 적었다"), Rig.SeedCache()))
	{
		return false;
	}

	// 실패 둘 — 네트워크 실패, 서버 fail() 응답. 어느 쪽이든 캐시·주인이 그대로다.
	TSharedPtr<FJsonObject> Refused = MakeShared<FJsonObject>();
	Refused->SetStringField(TEXT("error"), TEXT("삭제 확인 문자열이 없다"));
	Refused->SetStringField(TEXT("code"), TEXT("bad_args"));
	for (const TPair<bool, TSharedPtr<FJsonObject>>& Failure : { TPair<bool, TSharedPtr<FJsonObject>>(false, nullptr),
		TPair<bool, TSharedPtr<FJsonObject>>(true, Refused) })
	{
		int32 Result = -1;
		Rig.Backend->DeleteAccount([&Result](bool bDeleted, const FString&) { Result = bDeleted ? 1 : 0; });
		Rig.Answer(Failure.Key, Failure.Value);
		TestEqual(TEXT("실패로 답했다"), Result, 0);
		TestTrue(TEXT("실패면 캐시 파일이 남는다"), FDeleteRig::CacheExists());
		TestEqual(TEXT("실패면 주인이 그대로다"), Rig.Backend->GetPlayerSnapshot().PlayFabId, FString(TEXT("TESTDELETE01")));
		TestFalse(TEXT("실패 뒤 다시 누를 수 있다"), Rig.Backend->IsDeletingAccount());
	}

	// 성공 — 답이 오기 전엔 아무것도 안 바뀌고, 온 뒤에 캐시가 사라진다.
	int32 Result = -1;
	Rig.Backend->DeleteAccount([&Result](bool bDeleted, const FString&) { Result = bDeleted ? 1 : 0; });
	TestEqual(TEXT("부른 함수"), Rig.LastFunction, USnareBackendSubsystem::GetDeleteAccountFunctionName());
	TestEqual(TEXT("확인 문자열"), Rig.LastConfirm, FString(TEXT("DELETE")));
	TestTrue(TEXT("답 전에는 캐시가 남는다"), FDeleteRig::CacheExists());
	TestEqual(TEXT("답 전에는 결과가 없다"), Result, -1);
	Rig.Answer(true, FDeleteRig::Deleted());
	TestEqual(TEXT("성공으로 답했다"), Result, 1);
	TestFalse(TEXT("성공이면 캐시 파일이 없다"), FDeleteRig::CacheExists());
	TestFalse(TEXT("성공이면 스냅샷 주인이 없다"), Rig.Backend->GetPlayerSnapshot().HasOwner());
	TestEqual(TEXT("성공이면 로그아웃"), Rig.Backend->GetLoginState(), ESnareBackendLoginState::NotStarted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenAccountDeleteLobbyTest, "Snarehaven.Account.Delete.LobbyAsksTwiceThenRequests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenAccountDeleteLobbyTest::RunTest(const FString& Parameters)
{
	FDeleteRig Rig;
	if (!TestNotNull(TEXT("백엔드 창구"), Rig.Backend) || !TestTrue(TEXT("캐시 파일을 적었다"), Rig.SeedCache()))
	{
		return false;
	}
	ULobbyWidget* const Lobby = CreateWidget<ULobbyWidget>(Rig.GameInstance, ULobbyWidget::StaticClass());
	if (!TestNotNull(TEXT("로비 위젯"), Lobby))
	{
		return false;
	}
	Lobby->TakeWidget();
	UButton* const Delete = Lobby->WidgetTree->FindWidget<UButton>(TEXT("DeleteAccountButton"));
	UButton* const Cancel = Lobby->WidgetTree->FindWidget<UButton>(TEXT("DeleteCancelButton"));
	const UTextBlock* const Status = Lobby->WidgetTree->FindWidget<UTextBlock>(TEXT("StatusText"));
	if (!TestNotNull(TEXT("계정 삭제 버튼"), Delete) || !TestNotNull(TEXT("취소 버튼"), Cancel) || !TestNotNull(TEXT("상태 줄"), Status))
	{
		return false;
	}
	TestFalse(TEXT("평소엔 취소가 안 보인다"), (Cancel->GetVisibility() == ESlateVisibility::Visible));

	// M22 ⑧ — 첫 화면엔 계정 삭제가 없고, 설정을 열어야 보인다. 설정 안에서는 시작·방 버튼이 숨는다.
	UButton* const Settings = Lobby->WidgetTree->FindWidget<UButton>(TEXT("SettingsButton"));
	UButton* const Solo = Lobby->WidgetTree->FindWidget<UButton>(TEXT("SoloButton"));
	if (!TestNotNull(TEXT("설정 버튼"), Settings) || !TestNotNull(TEXT("시작 버튼"), Solo))
	{
		return false;
	}
	TestFalse(TEXT("첫 화면엔 계정 삭제가 안 보인다"), (Delete->GetVisibility() == ESlateVisibility::Visible));
	TestTrue(TEXT("첫 화면엔 시작이 보인다"), (Solo->GetVisibility() == ESlateVisibility::Visible));
	Settings->OnClicked.Broadcast();
	TestTrue(TEXT("설정이 열렸다"), Lobby->IsSettingsOpen());
	TestTrue(TEXT("설정 안에서 계정 삭제가 보인다"), (Delete->GetVisibility() == ESlateVisibility::Visible));
	TestFalse(TEXT("설정 안에서 시작이 숨는다"), (Solo->GetVisibility() == ESlateVisibility::Visible));

	// 1차 확인까지 누르고 설정을 닫았다 열면 확인이 처음부터다.
	Delete->OnClicked.Broadcast();
	Settings->OnClicked.Broadcast();
	TestFalse(TEXT("뒤로 누르면 설정이 닫힌다"), Lobby->IsSettingsOpen());
	TestFalse(TEXT("닫으면 계정 삭제가 숨는다"), (Delete->GetVisibility() == ESlateVisibility::Visible));
	TestFalse(TEXT("닫으면 취소도 숨는다"), (Cancel->GetVisibility() == ESlateVisibility::Visible));
	Settings->OnClicked.Broadcast();
	TestFalse(TEXT("다시 열면 확인이 처음부터 (취소 안 보임)"), (Cancel->GetVisibility() == ESlateVisibility::Visible));

	// 한 번 누르고 취소 — 요청이 안 나가고 처음으로 돌아간다.
	Delete->OnClicked.Broadcast();
	TestTrue(TEXT("1차 확인에서 취소가 보인다"), (Cancel->GetVisibility() == ESlateVisibility::Visible));
	Cancel->OnClicked.Broadcast();
	TestFalse(TEXT("취소 뒤 취소가 숨는다"), (Cancel->GetVisibility() == ESlateVisibility::Visible));

	Delete->OnClicked.Broadcast();
	Delete->OnClicked.Broadcast();
	TestEqual(TEXT("확인 두 번 전에는 요청이 없다"), Rig.Calls, 0);
	Delete->OnClicked.Broadcast();
	TestEqual(TEXT("2차 확인 뒤 요청 하나"), Rig.Calls, 1);
	TestEqual(TEXT("부른 함수"), Rig.LastFunction, USnareBackendSubsystem::GetDeleteAccountFunctionName());
	TestFalse(TEXT("요청 중에는 버튼이 꺼진다"), Delete->GetIsEnabled());
	TestFalse(TEXT("요청 중에는 설정을 못 닫는다"), Settings->GetIsEnabled());
	TestTrue(TEXT("답 전에는 캐시가 남는다"), FDeleteRig::CacheExists());

	Rig.Answer(true, FDeleteRig::Deleted());
	TestFalse(TEXT("성공이면 캐시 파일이 없다"), FDeleteRig::CacheExists());
	TestTrue(FString::Printf(TEXT("상태 줄이 결과를 알린다 (%s)"), *Status->GetText().ToString()),
		Status->GetText().ToString().Contains(TEXT("지웠다")));
	TestTrue(TEXT("끝나면 버튼이 다시 켜진다"), Delete->GetIsEnabled());
	return true;
}

#endif
