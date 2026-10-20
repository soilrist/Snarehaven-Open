// ROADMAP M3-01b 2층 검사 — 방(EOS 세션)을 **어떤 값으로** 만들고 찾는지만 본다.
// 실제 EOS 왕복은 여기서 하지 않는다(네트워크가 있어야 하는 검사는 3층이다 — 명세 6장).
// 이 검사가 잡는 것: ① 최대 인원이 8(DESIGN 3.4)에서 조용히 어긋나는 것
// ② 만들 때 심는 표시와 찾을 때 거는 필터가 달라져 **내 방을 내가 못 찾는** 사고.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Online/SnareOnlineSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOnlineCreateRoomParamsTest, "Snarehaven.Online.Session.CreateUsesDesignedRoomSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOnlineCreateRoomParamsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Online;

	const FCreateSession::Params Params = USnareOnlineSubsystem::MakeCreateRoomParams(FAccountId());

	TestEqual(TEXT("방 최대 인원은 8 (DESIGN 3.4)"), Params.SessionSettings.NumMaxConnections, 8u);
	TestEqual(TEXT("세션 이름은 SnareRoom"), Params.SessionName, USnareOnlineSubsystem::RoomSessionName);
	TestEqual(TEXT("검색에 걸리도록 공개 방이다"), Params.SessionSettings.JoinPolicy, ESessionJoinPolicy::Public);
	// 비어 있으면 엔진이 EOS 에 보내기도 전에 invalid_params 로 되돌린다(`SessionsCommon.cpp:439`).
	TestFalse(TEXT("스키마 이름이 비어 있지 않다"), Params.SessionSettings.SchemaName.IsNone());

	const FCustomSessionSetting* const Tag = Params.SessionSettings.CustomSettings.Find(USnareOnlineSubsystem::RoomTagKey);
	if (!TestNotNull(TEXT("Snarehaven 방 표시를 심는다"), Tag))
	{
		return false;
	}
	// Private 이면 남의 클라이언트가 검색 결과에서 이 값을 못 봐서 필터가 안 먹는다.
	TestEqual(TEXT("그 표시는 공개 속성이다"), Tag->Visibility, ESchemaAttributeVisibility::Public);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOnlineFindRoomsParamsTest, "Snarehaven.Online.Session.FindMatchesWhatCreateWrote",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOnlineFindRoomsParamsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Online;

	const FCreateSession::Params CreateParams = USnareOnlineSubsystem::MakeCreateRoomParams(FAccountId());
	const FFindSessions::Params FindParams = USnareOnlineSubsystem::MakeFindRoomsParams(FAccountId());

	TestEqual(TEXT("검색 결과 상한은 50 (명세 3장)"), FindParams.MaxResults, 50u);
	if (!TestEqual(TEXT("필터는 하나"), FindParams.Filters.Num(), 1))
	{
		return false;
	}

	const FFindSessionsSearchFilter& Filter = FindParams.Filters[0];
	TestEqual(TEXT("거르는 열쇠가 만들 때 심은 것과 같다"), Filter.Key, USnareOnlineSubsystem::RoomTagKey);
	TestEqual(TEXT("같은지(Equals)로 거른다"), Filter.ComparisonOp, ESchemaAttributeComparisonOp::Equals);

	const FCustomSessionSetting* const Tag = CreateParams.SessionSettings.CustomSettings.Find(USnareOnlineSubsystem::RoomTagKey);
	if (!TestNotNull(TEXT("만들 때 심은 값이 있다"), Tag))
	{
		return false;
	}
	// 여기가 어긋나면 방은 서는데 아무도 못 찾는다 — 3층에서야 보이는 종류의 사고다.
	TestTrue(TEXT("거르는 값이 심은 값과 같다"), Filter.Value == Tag->Data);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
