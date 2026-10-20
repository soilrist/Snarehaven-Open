// 출시 점검 — 접속 복구 (DESIGN 4.4 「접속 복구」). PlayFab 은 부르지 않는다: 로그인은 `TestLoginOverride`,
// 서버 호출은 `TestServerOverride` 가짜로 대신하고 검사 끝에 반드시 비운다. 보는 것 넷:
//  ① 재시도 간격 2·4·8…60초 ② 로그인 실패 → 재시도 예약 → 다시 시도 성공이면 예약이 풀린다
//  ③ 세션 만료 답 → 다시 로그인 → 같은 호출을 한 번만 재전송 ④ 실패 종류(끊김·로그인·거절)와 짧은 문장
// 실행: UnrealEditor <uproject> -ExecCmds="Automation RunTests Snarehaven.Backend.Recovery; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Backend/SnareServerReply.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"

namespace SnareBackendRecoveryTest
{
	/** 가짜 두 개를 걸고, 소멸할 때 반드시 비운다 — 다른 검사가 가짜를 물려받지 않게. */
	struct FRig
	{
		USnareBackendSubsystem* Backend = nullptr;
		int32 LoginCalls = 0;
		TArray<FString> LoginResults; // 차례로 돌려줄 결과. 다 쓰면 성공("").

		FRig()
		{
			UGameInstance* const GameInstance = NewObject<UGameInstance>(GetTransientPackage());
			Backend = NewObject<USnareBackendSubsystem>(GameInstance);
			USnareBackendSubsystem::TestLoginOverride = [this]()
			{
				const FString Result = LoginResults.IsValidIndex(LoginCalls) ? LoginResults[LoginCalls] : FString();
				++LoginCalls;
				return Result;
			};
		}

		~FRig()
		{
			Backend->RetryNow(); // 남은 재시도 예약을 지운다 (로그인돼 있으면 아무것도 안 보낸다)
			USnareBackendSubsystem::TestLoginOverride = nullptr;
			USnareBackendSubsystem::TestServerOverride = nullptr;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendRetryDelayTest, "Snarehaven.Backend.Recovery.RetryDelayDoublesToSixty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendRetryDelayTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("실패 없음 = 대기 없음"), USnareBackendSubsystem::GetRetryDelaySeconds(0), 0.f);
	TestEqual(TEXT("1번째 실패 2초"), USnareBackendSubsystem::GetRetryDelaySeconds(1), 2.f);
	TestEqual(TEXT("2번째 실패 4초"), USnareBackendSubsystem::GetRetryDelaySeconds(2), 4.f);
	TestEqual(TEXT("3번째 실패 8초"), USnareBackendSubsystem::GetRetryDelaySeconds(3), 8.f);
	TestEqual(TEXT("5번째 실패 32초"), USnareBackendSubsystem::GetRetryDelaySeconds(5), 32.f);
	TestEqual(TEXT("6번째부터 60초에서 멈춘다"), USnareBackendSubsystem::GetRetryDelaySeconds(6), 60.f);
	TestEqual(TEXT("100번째도 60초"), USnareBackendSubsystem::GetRetryDelaySeconds(100), 60.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendLoginRetryTest, "Snarehaven.Backend.Recovery.LoginFailureRetriesThenSucceeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendLoginRetryTest::RunTest(const FString& Parameters)
{
	SnareBackendRecoveryTest::FRig Rig;
	Rig.LoginResults = { TEXT("timeout"), TEXT("timeout") };
	int32 Changed = 0;
	Rig.Backend->OnConnectionChanged.AddLambda([&Changed]() { ++Changed; });

	Rig.Backend->StartLogin();
	TestEqual(TEXT("첫 로그인 실패 → Failed"), Rig.Backend->GetLoginState(), ESnareBackendLoginState::Failed);
	TestTrue(TEXT("화면이 「서버 연결 실패」 를 띄울 수 있다"), Rig.Backend->HasConnectionProblem());
	TestEqual(TEXT("1번째 실패 → 2초 뒤 재시도 예약"), Rig.Backend->GetPendingRetryDelay(), 2.f);
	TestTrue(TEXT("상태가 바뀌었다고 알렸다"), Changed > 0);

	Rig.Backend->RetryNow();
	TestEqual(TEXT("2번째 실패 → 4초"), Rig.Backend->GetPendingRetryDelay(), 4.f);

	Rig.Backend->RetryNow();
	TestEqual(TEXT("세 번째 시도는 성공"), Rig.Backend->GetLoginState(), ESnareBackendLoginState::LoggedIn);
	TestEqual(TEXT("성공하면 재시도 예약이 풀린다"), Rig.Backend->GetPendingRetryDelay(), 0.f);
	TestFalse(TEXT("실패 표시가 사라진다"), Rig.Backend->HasConnectionProblem());
	TestEqual(TEXT("로그인을 세 번 시도했다"), Rig.LoginCalls, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendExpiredResendTest, "Snarehaven.Backend.Recovery.ExpiredSessionReloginsAndResendsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendExpiredResendTest::RunTest(const FString& Parameters)
{
	SnareBackendRecoveryTest::FRig Rig;
	Rig.Backend->StartLogin();
	TestEqual(TEXT("처음 로그인"), Rig.LoginCalls, 1);

	// 첫 번째 호출은 만료, 두 번째부터는 받아들인다.
	int32 Sends = 0;
	USnareBackendSubsystem::TestServerOverride =
		[&Sends](const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			++Sends;
			if (Sends == 1)
			{
				OnDone(false, nullptr, SnareServerReply::TagLoginError(TEXT("EntityTokenExpired")));
				return;
			}
			OnDone(true, MakeShared<FJsonObject>(), FString());
		};

	int32 Answers = 0;
	bool bLastOk = false;
	Rig.Backend->CallServerFunction(TEXT("buyItem"), MakeShared<FJsonObject>(),
		[&Answers, &bLastOk](bool bOk, const TSharedPtr<FJsonObject>&, const FString&)
		{
			++Answers;
			bLastOk = bOk;
		});
	TestEqual(TEXT("만료 → 다시 로그인했다"), Rig.LoginCalls, 2);
	TestEqual(TEXT("같은 호출을 한 번 더 보냈다"), Sends, 2);
	TestEqual(TEXT("부른 쪽은 답을 한 번만 받는다"), Answers, 1);
	TestTrue(TEXT("재전송 답(성공)이 그대로 간다"), bLastOk);

	// 재전송까지 만료면 더 돌지 않는다 — 끝없이 로그인·재전송을 반복하지 않게.
	Sends = 0;
	USnareBackendSubsystem::TestServerOverride =
		[&Sends](const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone OnDone)
		{
			++Sends;
			OnDone(false, nullptr, SnareServerReply::TagLoginError(TEXT("EntityTokenExpired")));
		};
	Answers = 0;
	FString LastError;
	Rig.Backend->CallServerFunction(TEXT("buyItem"), MakeShared<FJsonObject>(),
		[&Answers, &bLastOk, &LastError](bool bOk, const TSharedPtr<FJsonObject>&, const FString& Error)
		{
			++Answers;
			bLastOk = bOk;
			LastError = Error;
		});
	TestEqual(TEXT("재전송은 한 번뿐 — 모두 두 번 보냈다"), Sends, 2);
	TestEqual(TEXT("답은 한 번"), Answers, 1);
	TestFalse(TEXT("실패로 끝난다"), bLastOk);
	TestEqual(TEXT("부른 쪽은 「로그인 중·만료」 로 안다"),
		(int32)SnareServerReply::ClassifyFailure(bLastOk, nullptr, LastError), (int32)SnareServerReply::EFailureKind::Login);

	// 다시 로그인이 실패하면 재전송 없이 실패를 돌려준다.
	Sends = 0;
	Answers = 0;
	Rig.LoginResults.SetNum(Rig.LoginCalls);
	Rig.LoginResults.Add(TEXT("timeout"));
	Rig.Backend->CallServerFunction(TEXT("buyItem"), MakeShared<FJsonObject>(),
		[&Answers, &bLastOk](bool bOk, const TSharedPtr<FJsonObject>&, const FString&)
		{
			++Answers;
			bLastOk = bOk;
		});
	TestEqual(TEXT("재로그인 실패면 다시 보내지 않는다"), Sends, 1);
	TestEqual(TEXT("답은 한 번"), Answers, 1);
	TestFalse(TEXT("실패로 끝난다"), bLastOk);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendFailureKindTest, "Snarehaven.Backend.Recovery.ClassifiesFailureKinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendFailureKindTest::RunTest(const FString& Parameters)
{
	using namespace SnareServerReply;
	const TSharedPtr<FJsonObject> Accepted = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject> Refused = MakeShared<FJsonObject>();
	Refused->SetStringField(TEXT("error"), TEXT("재화가 모자란다"));

	TestEqual(TEXT("받아들임"), (int32)ClassifyFailure(true, Accepted, FString()), (int32)EFailureKind::None);
	TestEqual(TEXT("서버 판정 거절"), (int32)ClassifyFailure(true, Refused, FString()), (int32)EFailureKind::Rejected);
	TestEqual(TEXT("표지 없는 실패 = 끊김"), (int32)ClassifyFailure(false, nullptr, TEXT("timeout")), (int32)EFailureKind::Network);
	TestEqual(TEXT("끊김 표지"), (int32)ClassifyFailure(false, nullptr, TagNetworkError(TEXT("x"))), (int32)EFailureKind::Network);
	TestEqual(TEXT("로그인 표지"), (int32)ClassifyFailure(false, nullptr, TagLoginError(TEXT("x"))), (int32)EFailureKind::Login);
	TestEqual(TEXT("거절 표지"), (int32)ClassifyFailure(false, nullptr, TagRejectedError(TEXT("x"))), (int32)EFailureKind::Rejected);

	// 화면 문장은 셋이 서로 다르고 짧다.
	const FString Network = DescribeFailure(false, nullptr, TagNetworkError(TEXT("x"))).ToString();
	const FString Login = DescribeFailure(false, nullptr, TagLoginError(TEXT("x"))).ToString();
	const FString Rejected = DescribeFailure(false, nullptr, TagRejectedError(TEXT("x"))).ToString();
	TestNotEqual(TEXT("끊김 ≠ 로그인"), Network, Login);
	TestNotEqual(TEXT("로그인 ≠ 거절"), Login, Rejected);
	TestTrue(TEXT("문장이 짧다(20자 안팎)"), Network.Len() <= 24 && Login.Len() <= 24 && Rejected.Len() <= 24);
	TestFalse(TEXT("내부 표지가 화면에 새지 않는다"), Login.Contains(TEXT("[")));

	// 목장 쪽이 찾는 코드 줄은 그대로 `network` 다.
	FString Code;
	FString Message;
	TestFalse(TEXT("로그인 실패는 받아들임이 아니다"), IsAccepted(false, nullptr, TagLoginError(TEXT("x")), Code, Message));
	TestEqual(TEXT("코드는 network 그대로"), Code, FString(TEXT("network")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
