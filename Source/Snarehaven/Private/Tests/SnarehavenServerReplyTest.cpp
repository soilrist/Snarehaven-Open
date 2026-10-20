// ROADMAP M36-04 탐침이 찾은 빈 곳 — 서버 답 읽기 한 곳(`SnareServerReply`)을 직접 부르는 시험이 없었다.
// 상점·나룻배·배낭·뽑기가 전부 이걸 타므로, 여기가 틀리면 "재화 부족" 도 지급으로 그린다 (CLAUDE.md 8). 보는 것 셋:
//  ① `DescribeFailure` — 못 닿음 = 고정 문장, 성공(= `error` 없음) = 빈 글자, 실패 = 서버 사유 그대로, `need`·`have` 가 있으면 수를 덧붙임
//  ② `GetFailureCode` — 실패 `code` 그대로, 없거나 빈 응답 = 빈 문자열
//  ③ `IsAccepted` — 네트워크 실패 = `network`·오류 글자, `error` 칸 = 거절 코드·사유, 그 밖 = 받아들임
// 실행: UnrealEditor <uproject> -ExecCmds="Automation RunTests Snarehaven.Backend.ServerReply; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareServerReply.h"
#include "Dom/JsonObject.h"

namespace SnareServerReplyTest
{
	TSharedPtr<FJsonObject> MakeFailure(const FString& Message, const FString& Code)
	{
		const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("error"), Message);
		Response->SetStringField(TEXT("code"), Code);
		return Response;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenServerReplyDescribeTest,
	"Snarehaven.Backend.ServerReply.DescribeFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenServerReplyDescribeTest::RunTest(const FString& Parameters)
{
	using namespace SnareServerReplyTest;

	const TSharedPtr<FJsonObject> Success = MakeShared<FJsonObject>();
	Success->SetNumberField(TEXT("coins"), 7.0);
	const TSharedPtr<FJsonObject> Plain = MakeFailure(TEXT("시험 사유"), TEXT("test_code"));
	const TSharedPtr<FJsonObject> Short = MakeFailure(TEXT("코인이 모자란다"), TEXT("not_enough_coins"));
	Short->SetNumberField(TEXT("need"), 5.0);
	Short->SetNumberField(TEXT("have"), 2.0);
	const TSharedPtr<FJsonObject> NeedOnly = MakeFailure(TEXT("시험 사유"), TEXT("test_code"));
	NeedOnly->SetNumberField(TEXT("need"), 5.0);

	const FString Unreachable = SnareServerReply::DescribeFailure(false, Success, TEXT("timeout")).ToString();
	const FString NullResponse = SnareServerReply::DescribeFailure(true, nullptr, FString()).ToString();
	AddInfo(FString::Printf(TEXT("못 닿음 [%s] · 실패 [%s] · 모자람 [%s]"), *Unreachable,
		*SnareServerReply::DescribeFailure(true, Plain, FString()).ToString(),
		*SnareServerReply::DescribeFailure(true, Short, FString()).ToString()));

	TestFalse(TEXT("못 닿음: 사유가 비지 않는다"), Unreachable.IsEmpty());
	TestEqual(TEXT("빈 응답도 못 닿음과 같은 문장"), NullResponse, Unreachable);
	TestTrue(TEXT("성공(error 없음) = 빈 글자"), SnareServerReply::DescribeFailure(true, Success, FString()).IsEmpty());
	TestEqual(TEXT("실패 = 서버 사유 그대로(화면이 짓지 않음)"),
		SnareServerReply::DescribeFailure(true, Plain, FString()).ToString(), FString(TEXT("시험 사유")));
	TestEqual(TEXT("need·have = 모자란 수 덧붙임"),
		SnareServerReply::DescribeFailure(true, Short, FString()).ToString(),
		FString(TEXT("코인이 모자란다 (5 필요, 2 있음)")));
	TestEqual(TEXT("need 만 있으면 수를 안 붙인다"),
		SnareServerReply::DescribeFailure(true, NeedOnly, FString()).ToString(), FString(TEXT("시험 사유")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenServerReplyCodeTest,
	"Snarehaven.Backend.ServerReply.FailureCodeAndAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenServerReplyCodeTest::RunTest(const FString& Parameters)
{
	using namespace SnareServerReplyTest;

	const TSharedPtr<FJsonObject> Success = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject> Locked = MakeFailure(TEXT("시험 잠김"), TEXT("locked"));

	TestEqual(TEXT("실패 code 그대로"), SnareServerReply::GetFailureCode(Locked), FString(TEXT("locked")));
	TestTrue(TEXT("성공 = 빈 code"), SnareServerReply::GetFailureCode(Success).IsEmpty());
	TestTrue(TEXT("빈 응답 = 빈 code"), SnareServerReply::GetFailureCode(nullptr).IsEmpty());

	FString Code;
	FString Message;
	TestFalse(TEXT("네트워크 실패 = 안 받아들임"),
		SnareServerReply::IsAccepted(false, Success, TEXT("시험 네트워크"), Code, Message));
	TestEqual(TEXT("네트워크 실패 code"), Code, FString(TEXT("network")));
	TestEqual(TEXT("네트워크 실패 사유 = 오류 글자"), Message, FString(TEXT("시험 네트워크")));

	Code.Reset();
	Message.Reset();
	TestFalse(TEXT("빈 응답 = 안 받아들임"), SnareServerReply::IsAccepted(true, nullptr, TEXT("빈"), Code, Message));
	TestEqual(TEXT("빈 응답 code"), Code, FString(TEXT("network")));

	Code.Reset();
	Message.Reset();
	TestFalse(TEXT("error 칸 = 거절"), SnareServerReply::IsAccepted(true, Locked, FString(), Code, Message));
	TestEqual(TEXT("거절 code"), Code, FString(TEXT("locked")));
	TestEqual(TEXT("거절 사유 = 서버 error"), Message, FString(TEXT("시험 잠김")));

	Code.Reset();
	Message.Reset();
	TestTrue(TEXT("error 없음 = 받아들임"), SnareServerReply::IsAccepted(true, Success, FString(), Code, Message));
	TestTrue(TEXT("받아들임 = code·사유 안 채움"), Code.IsEmpty() && Message.IsEmpty());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
