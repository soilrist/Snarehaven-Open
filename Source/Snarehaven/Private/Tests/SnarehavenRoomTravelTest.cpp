// ROADMAP M3-05 2층 검사 — 방에 들어가면 사냥 맵으로.
// 실제로 두 프로세스가 같은 맵에 서는 것은 3층이고, 여기서는 **방에 무엇이 적히고 누가 어디로 가는지**만 본다.
// 이 검사가 잡는 것: ① 호스트가 표지판 없는 방을 세우는 것(게스트가 갈 곳을 모른다)
// ② 접속 주소가 EOS 가 나르지 않는 비공개 속성으로 실리는 것 ③ `?listen` 이 빠져 호스트가 서버가 안 되는 것
// ④ 명령줄 주소(3층 두 프로세스)가 EOS 주소에 밀리는 것.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Online/SnareOnlineSubsystem.h"
#include "SnarehavenGameMode.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHostConnectAddressTest, "Snarehaven.Net.Travel.HostAddressPrefersCommandLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHostConnectAddressTest::RunTest(const FString& Parameters)
{
	// 검사 전용 값 — 진짜 PUID 는 EOS 가 주고, 여기서는 형식만 본다 (Verification.md "테스트 값").
	const FString TestPuid = TEXT("0002abcd");

	// 평소 = DESIGN 3.4 의 EOS P2P. `FInternetAddrEOS::ToString` 과 같은 형식이라야 접속이 풀린다.
	TestEqual(TEXT("기본은 EOS P2P 주소"),
		USnareOnlineSubsystem::MakeHostConnectAddress(TestPuid, FString()), TEXT("EOS:0002abcd"));

	// 3층 두 프로세스는 PUID 가 같아 P2P 로 서로를 못 가린다(M3-01c2) — 명령줄 주소가 이겨야 한다.
	TestEqual(TEXT("명령줄 주소가 EOS 주소를 이긴다"),
		USnareOnlineSubsystem::MakeHostConnectAddress(TestPuid, TEXT("127.0.0.1")), TEXT("127.0.0.1"));

	// 로그인 전에 방을 세우는 길이 생기면 빈 표지판이 달린다 — 그럴 바엔 달지 않는다.
	TestTrue(TEXT("줄 주소가 없으면 빈 문자열"),
		USnareOnlineSubsystem::MakeHostConnectAddress(FString(), FString()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRoomCarriesAddressTest, "Snarehaven.Net.Travel.RoomCarriesHostAddress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRoomCarriesAddressTest::RunTest(const FString& Parameters)
{
	using namespace UE::Online;

	const FCreateSession::Params Params =
		USnareOnlineSubsystem::MakeCreateRoomParams(FAccountId(), TEXT("127.0.0.1"));

	const FCustomSessionSetting* const Address =
		Params.SessionSettings.CustomSettings.Find(USnareOnlineSubsystem::RoomAddressKey);
	if (Address == nullptr)
	{
		AddError(TEXT("방에 접속 주소 칸이 없다 — 게스트가 갈 곳을 모른다"));
		return false;
	}

	// 비공개로 실으면 EOS 가 검색·참가 결과에 나르지 않아 게스트가 읽을 수 없다.
	TestEqual(TEXT("접속 주소는 공개 속성으로 실린다"), Address->Visibility, ESchemaAttributeVisibility::Public);

	// 호스트가 적은 값을 게스트가 그대로 읽어야 한다 — 싣기와 읽기가 같은 칸 이름을 봐야 성립한다.
	TestEqual(TEXT("실은 주소를 그대로 읽는다"),
		USnareOnlineSubsystem::ReadHostConnectAddress(Params.SessionSettings.CustomSettings), TEXT("127.0.0.1"));

	// M3-01b 처럼 EOS 쪽 방만 세워 보는 길이 남아 있어야 한다 — 주소가 없으면 칸도 없다.
	const FCreateSession::Params NoAddress = USnareOnlineSubsystem::MakeCreateRoomParams(FAccountId());
	TestTrue(TEXT("주소를 안 주면 칸이 생기지 않는다"),
		USnareOnlineSubsystem::ReadHostConnectAddress(NoAddress.SessionSettings.CustomSettings).IsEmpty());
	// 표지판이 없어도 "이건 Snarehaven 방이다" 표시는 그대로여야 검색에 걸린다.
	TestTrue(TEXT("방 표시는 그대로 남는다"),
		NoAddress.SessionSettings.CustomSettings.Contains(USnareOnlineSubsystem::RoomTagKey));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenHostTravelsAsListenServerTest, "Snarehaven.Net.Travel.HostOpensHuntAsListenServer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenHostTravelsAsListenServerTest::RunTest(const FString& Parameters)
{
	const FString TravelUrl = USnareOnlineSubsystem::MakeHuntServerTravelUrl();

	// `?listen` 이 빠지면 호스트는 혼자 사냥하는 사람이 되고 게스트는 붙을 서버가 없다.
	TestTrue(TEXT("리슨 서버로 연다"), TravelUrl.EndsWith(TEXT("?listen")));
	// 로비의 "혼자 사냥" 과 같은 맵이어야 한다 — 맵 이름이 두 벌이 되면 한쪽만 고쳐지는 날이 온다.
	TestTrue(TEXT("로비가 여는 사냥 맵과 같다"),
		TravelUrl.StartsWith(ASnarehavenGameMode::HuntLevelName.ToString()));
	// `AGameModeBase::CanServerTravel` 이 `:` 가 든 URL 을 거절한다 — 여기에 EOS 주소를 넣으면 안 된다.
	TestFalse(TEXT("서버 여행 URL 에는 ':' 가 없다"), TravelUrl.Contains(TEXT(":")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
