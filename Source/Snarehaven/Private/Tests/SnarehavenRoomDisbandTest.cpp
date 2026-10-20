// ROADMAP M3-03 2층 검사 — 호스트 이탈 → 방 해산 → 로비 복귀. (M21-08a 백그라운드 유예도 여기)
// 실제로 끊어 보는 것은 3층(두 프로세스)이고, 여기서는 **누가 무엇을 하기로 돼 있는지**만 본다.
// 이 검사가 잡는 것: ① 호스트가 나가도 방이 EOS 에 남는 것(빈 방이 검색에 계속 뜬다)
// ② 게스트가 남의 방을 지우는 것 ③ 리슨 서버가 게스트 하나 끊겼다고 혼자 로비로 나가 방을 죽이는 것
// ④ 로비로 돌려보낸 사람을 자동 진입이 곧바로 사냥 맵으로 되밀어 넣는 것.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Online/SnareOnlineSubsystem.h"
#include "SnarehavenGameMode.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenRoomLeaveParamsTest, "Snarehaven.Online.Room.HostDestroysGuestOnlyLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenRoomLeaveParamsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Online;

	const FLeaveSession::Params HostParams = USnareOnlineSubsystem::MakeLeaveRoomParams(FAccountId(), /*bDestroy=*/true);
	const FLeaveSession::Params GuestParams = USnareOnlineSubsystem::MakeLeaveRoomParams(FAccountId(), /*bDestroy=*/false);

	// DESIGN 3.4 "호스트가 나가면 방 해산" — 지우지 않으면 아무도 없는 방이 남의 검색 결과에 계속 뜬다.
	TestTrue(TEXT("호스트가 나가면 방을 지운다 (해산)"), HostParams.bDestroySession);
	// 게스트가 지울 수 있으면 남의 방을 닫는 셈이 된다.
	TestFalse(TEXT("게스트는 나가기만 한다"), GuestParams.bDestroySession);

	// 만든 방과 다른 이름으로 나가면 EOS 는 "그런 방 없다" 로 되돌리고 방은 그대로 남는다.
	const FCreateSession::Params CreateParams = USnareOnlineSubsystem::MakeCreateRoomParams(FAccountId());
	TestEqual(TEXT("만든 방과 같은 이름으로 나간다"), HostParams.SessionName, CreateParams.SessionName);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenReturnToLobbyTest, "Snarehaven.Net.Disband.OnlyGuestReturnsToLobby",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenReturnToLobbyTest::RunTest(const FString& Parameters)
{
	// 리슨 서버(호스트)도 게스트가 떨어지면 같은 실패 알림을 받는다. 여기가 참이 되면
	// 호스트가 남은 사람들을 두고 혼자 로비로 나가 방이 통째로 죽는다.
	TestTrue(TEXT("게스트는 로비로 돌아간다"), USnareOnlineSubsystem::ShouldReturnToLobby(NM_Client));
	TestFalse(TEXT("호스트(리슨 서버)는 남는다"), USnareOnlineSubsystem::ShouldReturnToLobby(NM_ListenServer));
	TestFalse(TEXT("전용 서버도 남는다"), USnareOnlineSubsystem::ShouldReturnToLobby(NM_DedicatedServer));
	// 혼자 놀 때 난 오류로 맵이 튀면 안 된다.
	TestFalse(TEXT("싱글은 아무 데도 안 간다"), USnareOnlineSubsystem::ShouldReturnToLobby(NM_Standalone));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenLobbyStaysPutTest, "Snarehaven.Net.Disband.LobbyDoesNotBounceBackToHunt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenLobbyStaysPutTest::RunTest(const FString& Parameters)
{
	const FString LobbyMap = USnareOnlineSubsystem::LobbyLevelName.ToString();

	// M3-04 부터 로비는 **어떤 길로 왔든** 종착이다. 전에는 "방에서 돌아온 사람만" 머물게 가려 뒀는데
	// (`bReturnedFromRoom`), 로비 화면이 생긴 뒤로는 평소 부팅도 여기서 멈춰 그 가림막이 필요 없어졌다.
	// 여기가 깨지면 쫓겨난 게스트가 로비에 닿자마자 사냥 맵으로 되밀려 "로비 복귀" 가 사라진다.
	TestEqual(TEXT("로비에 닿은 사람은 로비에 머문다"),
		ASnarehavenGameMode::GetNextLevelName(LobbyMap), FName(NAME_None));
	// 루프#41 3층이 잡은 진짜 착지점 — 엔진이 게스트를 기본 맵(`L_Boot?closed`)에 먼저 내려놓는다.
	// 여기서 멈춰 세우면 게스트는 로비가 아니라 빈 부팅 맵에 남는다(`join41.log:1714`).
	TestEqual(TEXT("엔진이 내려놓은 부팅 맵에서 로비까지 이어 간다"),
		ASnarehavenGameMode::GetNextLevelName(TEXT("L_Boot")), USnareOnlineSubsystem::LobbyLevelName);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackgroundGraceTest, "Snarehaven.Online.Room.BackgroundGraceDisbands",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackgroundGraceTest::RunTest(const FString& Parameters)
{
	// M21-08a · DESIGN 3.4 「백그라운드 유예」 30초 — 숫자가 바뀌어도 아래 경계는 상수에 맞춰 본다.
	const double Grace = USnareOnlineSubsystem::BackgroundGraceSeconds;
	TestEqual(TEXT("유예 = DESIGN 3.4 30초"), Grace, 30.0);
	// 전화·알림을 잠깐 보고 돌아온 사람은 방에 남는다.
	TestFalse(TEXT("유예 1초 전엔 남는다"), USnareOnlineSubsystem::ShouldDisbandAfterBackground(true, Grace - 1.0));
	TestTrue(TEXT("유예가 차면 떠난다"), USnareOnlineSubsystem::ShouldDisbandAfterBackground(true, Grace));
	TestTrue(TEXT("한참 뒤 돌아와도 떠난다(앱이 멈춰 티커가 못 돈 경우)"),
		USnareOnlineSubsystem::ShouldDisbandAfterBackground(true, Grace * 20.0));
	// 혼자 사냥·로비에서 앱을 내렸다고 맵을 갈아치우면 안 된다.
	TestFalse(TEXT("방 밖이면 아무리 오래여도 그대로"), USnareOnlineSubsystem::ShouldDisbandAfterBackground(false, Grace * 20.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenReopenDeadListenSocketTest, "Snarehaven.Online.Room.ReopenDeadListenSocket",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenReopenDeadListenSocketTest::RunTest(const FString& Parameters)
{
	// INBOX 18:20 — 폰이 앱 복귀 뒤 리슨 서버 소켓에서 `SE_ENOTCONN` 76만 줄을 받으며 한 프레임에 갇혔다.
	// 실제 UDP 소켓으로 "살아 있음/죽음" 을 가른다: 살아 있는 비차단 소켓은 온 게 없어도 죽었다고 하면 안 된다
	// (그러면 앱을 내릴 때마다 게스트가 다 끊긴다).
	ISocketSubsystem* const Sockets = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!TestNotNull(TEXT("소켓 서브시스템"), Sockets))
	{
		return false;
	}
	FSocket* const Socket = Sockets->CreateSocket(NAME_DGram, TEXT("SnareReopenTest"), false);
	if (!TestNotNull(TEXT("UDP 소켓을 만든다"), Socket))
	{
		return false;
	}
	TSharedRef<FInternetAddr> Bind = Sockets->CreateInternetAddr();
	Bind->SetLoopbackAddress();
	Bind->SetPort(0);
	TestTrue(TEXT("빈 포트에 묶는다"), Socket->Bind(*Bind));
	Socket->SetNonBlocking(true);

	TestFalse(TEXT("온 게 없는 살아 있는 소켓은 안 죽었다"), USnareOnlineSubsystem::IsRecvSocketBroken(Socket));

	// 패킷이 와 있어도 엿보기만 하고 남긴다 — 엔진 수신 루프가 그대로 받아야 한다.
	TSharedRef<FInternetAddr> Self = Sockets->CreateInternetAddr();
	Self->SetLoopbackAddress();
	Self->SetPort(Socket->GetPortNo());
	const uint8 Payload[4] = { 1, 2, 3, 4 };
	int32 Sent = 0;
	Socket->SendTo(Payload, 4, Sent, *Self);
	Socket->Wait(ESocketWaitConditions::WaitForRead, FTimespan::FromSeconds(1.0));
	uint32 PendingBefore = 0;
	Socket->HasPendingData(PendingBefore);
	TestFalse(TEXT("패킷이 와 있어도 안 죽었다"), USnareOnlineSubsystem::IsRecvSocketBroken(Socket));
	uint8 Received[8] = {};
	int32 Read = 0;
	TSharedRef<FInternetAddr> From = Sockets->CreateInternetAddr();
	Socket->RecvFrom(Received, 8, Read, *From);
	AddInfo(FString::Printf(TEXT("[ReopenSocket] 보냄 %d바이트 → %s · 엿보기 전 대기 %u바이트 · 엿본 뒤 받음 %d바이트"),
		Sent, *Self->ToString(true), PendingBefore, Read));
	TestEqual(TEXT("엿본 패킷 4바이트가 그대로 남아 있다"), Read, 4);

	// iOS 가 거둬 간 소켓 흉내 — 닫힌 소켓은 죽었다.
	Socket->Close();
	TestTrue(TEXT("닫힌 소켓은 죽었다"), USnareOnlineSubsystem::IsRecvSocketBroken(Socket));
	Sockets->DestroySocket(Socket);
	TestTrue(TEXT("소켓이 없으면 죽었다"), USnareOnlineSubsystem::IsRecvSocketBroken(nullptr));

	// 다시 여는 건 리슨 서버뿐 — 게스트·혼자(넷 드라이버 없음)는 손대지 않는다.
	TestTrue(TEXT("리슨 서버 + 죽은 소켓 → 다시 연다"), USnareOnlineSubsystem::ShouldReopenListenSocket(NM_ListenServer, true));
	TestFalse(TEXT("리슨 서버 + 산 소켓 → 그대로(게스트 유지)"), USnareOnlineSubsystem::ShouldReopenListenSocket(NM_ListenServer, false));
	TestFalse(TEXT("게스트는 다시 열지 않는다"), USnareOnlineSubsystem::ShouldReopenListenSocket(NM_Client, true));
	TestFalse(TEXT("혼자(서버 아님)는 다시 열지 않는다"), USnareOnlineSubsystem::ShouldReopenListenSocket(NM_Standalone, true));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
