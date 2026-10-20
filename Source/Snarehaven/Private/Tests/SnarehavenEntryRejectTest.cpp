// 입장 거절 사유 2층 검사 — 서버 `{error, code}`·멀티 참가 실패가 **`DT_EntryRejectTable` 문구**로 바뀌는지.
// 잡는 것: ① 코드 → 문구가 표가 아니라 코드 상수로 새는 것(표 행을 바꿔도 화면이 안 바뀜)
// ② `SnareWater::CallServer` 식 bOk=false + error 답을 "서버에 못 닿음" 으로 뭉개는 것
// ③ 성공 답에 거절 문구가 남는 것 ④ 세션 가득·버전 불일치가 같은 "방에 문제" 한 줄로 뭉개지는 것.
// 나룻배 버튼에서 실제로 뜨는지·답 전 불변은 `Snarehaven.Ferry.RefusedFareDoesNotTravel` 이 본다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareEntryReject.h"
#include "Dom/JsonObject.h"
#include "Engine/DataTable.h"
#include "Online/OnlineErrorDefinitions.h"
#include "Online/SnareOnlineSubsystem.h"
#include "UI/LobbyWidget.h"

namespace
{
	TSharedPtr<FJsonObject> MakeRefusal(const FString& Code)
	{
		const TSharedPtr<FJsonObject> Reply = MakeShared<FJsonObject>();
		Reply->SetStringField(TEXT("error"), TEXT("서버 원문"));
		if (!Code.IsEmpty())
		{
			Reply->SetStringField(TEXT("code"), Code);
		}
		return Reply;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEntryRejectCodeTest, "Snarehaven.EntryReject.Reply.CodeBecomesTableMessage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEntryRejectCodeTest::RunTest(const FString& Parameters)
{
	const UDataTable* const Table = SnareEntryReject::LoadTable();
	if (!TestNotNull(TEXT("DT_EntryRejectTable 로드"), Table))
	{
		return false;
	}

	// ① 응답에서 코드 읽기.
	const TSharedPtr<FJsonObject> Success = MakeShared<FJsonObject>();
	Success->SetStringField(TEXT("dest"), TEXT("hunt"));
	TestEqual(TEXT("성공 답은 코드가 없다"), SnareEntryReject::ReadCode(true, Success), FString());
	TestEqual(TEXT("서버 거절은 code 를 읽는다"), SnareEntryReject::ReadCode(true, MakeRefusal(TEXT("locked"))), FString(TEXT("locked")));
	TestEqual(TEXT("SnareWater 식 bOk=false 거절도 code 를 읽는다"),
		SnareEntryReject::ReadCode(false, MakeRefusal(TEXT("not_enough_coins"))), FString(TEXT("not_enough_coins")));
	TestEqual(TEXT("code 없는 거절은 unknown"), SnareEntryReject::ReadCode(true, MakeRefusal(FString())), FString(TEXT("unknown")));
	TestEqual(TEXT("답이 없으면 network"), SnareEntryReject::ReadCode(false, nullptr), FString(TEXT("network")));
	TestTrue(TEXT("성공 답은 문구가 비어 있다"), SnareEntryReject::DescribeReply(Table, true, Success).IsEmpty());

	// ② 코드 → 문구 (표 행).
	const TSharedPtr<FJsonObject> Poor = MakeRefusal(TEXT("not_enough_coins"));
	Poor->SetNumberField(TEXT("need"), 12);
	Poor->SetNumberField(TEXT("have"), 3);
	const FString PoorText = SnareEntryReject::DescribeReply(Table, true, Poor).ToString();
	const FString LockedText = SnareEntryReject::MessageFor(Table, TEXT("locked")).ToString();
	const FString FullText = SnareEntryReject::MessageFor(Table, TEXT("room_full")).ToString();
	const FString VersionText = SnareEntryReject::MessageFor(Table, TEXT("version_mismatch")).ToString();
	const FString NetworkText = SnareEntryReject::DescribeReply(Table, false, nullptr).ToString();
	const FString OddText = SnareEntryReject::MessageFor(Table, TEXT("mystery_code")).ToString();
	AddInfo(FString::Printf(TEXT("문구 — 코인 [%s] 잠김 [%s] 가득 [%s] 버전 [%s] 망 [%s] 모름 [%s]"),
		*PoorText, *LockedText, *FullText, *VersionText, *NetworkText, *OddText));
	TestTrue(TEXT("코인 부족에 need·have 가 채워진다"), PoorText.Contains(TEXT("12 필요")) && PoorText.Contains(TEXT("3 있음")));
	TestTrue(TEXT("잠김은 도감 50% 조건을 말한다"), LockedText.Contains(TEXT("50%")));
	TestTrue(TEXT("세션 가득 문구"), FullText.Contains(TEXT("가득")));
	TestTrue(TEXT("버전 불일치 문구"), VersionText.Contains(TEXT("버전")));
	TestTrue(TEXT("서버에 못 닿음 문구"), NetworkText.Contains(TEXT("닿지 못했다")));
	TestTrue(TEXT("모르는 코드는 unknown 행 + 코드"), OddText.Contains(TEXT("mystery_code")) && OddText.Contains(TEXT("들어갈 수 없다")));
	TestFalse(TEXT("가득과 버전은 다른 문구다"), FullText == VersionText);

	// ③ 표가 없으면 지어내지 않고 코드를 그대로 보인다. 남기는 경고(표 없음)는 로그에 그대로 둔다(다른 검사와 같은 규칙).
	TestEqual(TEXT("표가 없으면 코드 그대로"), SnareEntryReject::MessageFor(nullptr, TEXT("locked")).ToString(), FString(TEXT("locked")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenEntryRejectMultiTest, "Snarehaven.EntryReject.Multi.JoinFailureNamesReason",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenEntryRejectMultiTest::RunTest(const FString& Parameters)
{
	// ① EOS 참가 실패 분류.
	TestEqual(TEXT("세션 가득은 room_full"),
		USnareOnlineSubsystem::ClassifyJoinError(UE::Online::Errors::SessionFull()), FString(TEXT("room_full")));
	TestEqual(TEXT("그 밖의 참가 실패는 room_join_failed"),
		USnareOnlineSubsystem::ClassifyJoinError(UE::Online::Errors::NotImplemented()), FString(TEXT("room_join_failed")));

	// ② 호스트 접속 실패 분류 — 버전만 코드가 있고, 그냥 끊김은 해산 복귀 문구에 맡긴다.
	TestEqual(TEXT("클라 구버전은 version_mismatch"),
		USnareOnlineSubsystem::ClassifyNetworkFailure(ENetworkFailure::OutdatedClient), FString(TEXT("version_mismatch")));
	TestEqual(TEXT("서버 구버전도 version_mismatch"),
		USnareOnlineSubsystem::ClassifyNetworkFailure(ENetworkFailure::OutdatedServer), FString(TEXT("version_mismatch")));
	TestEqual(TEXT("그냥 끊김은 코드 없음"),
		USnareOnlineSubsystem::ClassifyNetworkFailure(ENetworkFailure::ConnectionLost), FString());

	// ③ 로비 상태 줄 — 실패·해산 복귀 자리에 거절 문구가 선다.
	const FText Full = SnareEntryReject::MessageFor(SnareEntryReject::LoadTable(), TEXT("room_full"));
	const FString FailedWith = ULobbyWidget::MakeStatusText(
		ESnareLoginState::LoggedIn, ESnareRoomState::Failed, 1, false, Full).ToString();
	const FString FailedWithout = ULobbyWidget::MakeStatusText(
		ESnareLoginState::LoggedIn, ESnareRoomState::Failed, 1, false).ToString();
	const FString ReturnedWith = ULobbyWidget::MakeStatusText(
		ESnareLoginState::LoggedIn, ESnareRoomState::None, 0, true, Full).ToString();
	AddInfo(FString::Printf(TEXT("로비 — 실패+사유 [%s] 실패 [%s] 복귀+사유 [%s]"), *FailedWith, *FailedWithout, *ReturnedWith));
	TestEqual(TEXT("실패 상태에 거절 문구가 뜬다"), FailedWith, Full.ToString());
	TestFalse(TEXT("사유가 없으면 일반 실패 문구"), FailedWithout == Full.ToString());
	TestTrue(TEXT("해산 복귀 자리에도 거절 문구가 뜬다"), ReturnedWith.Contains(TEXT("가득")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
