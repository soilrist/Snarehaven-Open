// ROADMAP M2-01 2층 검사 — PlayFab 게스트 로그인이 **어떻게 불리는지**만 본다.
// 실제 PlayFab 왕복은 여기서 하지 않는다(네트워크가 있어야 하는 검사는 3층이다).
// 이 검사가 잡는 것: ① 자동화 중에 PlayFab 에 계정을 만들어 버리는 사고 ② Title ID 를 읽는 자리가
// 사람이 적어 둔 파일과 어긋나는 것 ③ 기기 ID 가 실행마다 달라져 게스트 진행이 끊기는 것
// ④ 플랫폼에 없는 로그인 API 를 부르는 것.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Engine/Engine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendNoAutoLoginInTestsTest, "Snarehaven.Backend.Login.SkipsAutoLoginOutsideGame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendNoAutoLoginInTestsTest::RunTest(const FString& Parameters)
{
	// 이 검사 자체가 에디터 컨텍스트에서 돈다 — 참이 나오면 검사를 돌릴 때마다 PlayFab 에 붙는다는 뜻이다.
	TestFalse(TEXT("에디터·자동화에서는 자동 로그인하지 않는다"), USnareBackendSubsystem::ShouldAutoLogin(nullptr));

	// 지금 상황을 실제로 모아서도 본다 — 자동화 깃발이 꺼져 있으면 위 한 줄이 조용히 통과할 수 있다.
	const FSnareAutoLoginContext Here = USnareBackendSubsystem::MakeAutoLoginContext(nullptr);
	TestTrue(TEXT("검사가 도는 중인 것을 상황이 알고 있다"), Here.bAutomationTest);
	TestFalse(TEXT("게임 인스턴스가 없으면 PIE 가 아니다"), Here.bPlayInEditor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendPieLoginTest, "Snarehaven.Backend.Login.RunsInPlayInEditor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendPieLoginTest::RunTest(const FString& Parameters)
{
	// 감독 지시 2026-09-24 00:05 ① — 사람이 게임을 보는 자리가 에디터 Play 다. 상황을 여섯 가지로
	// 만들어 놓고 판정만 본다(전역·네트워크를 안 탄다).
	FSnareAutoLoginContext Pie;
	Pie.bEditor = true;
	Pie.bPlayInEditor = true;
	Pie.bHasTitleId = true;
	Pie.PlayInEditorInstance = 0;
	TestTrue(TEXT("PIE + Title ID 면 붙는다"), USnareBackendSubsystem::ShouldAutoLoginFor(Pie));

	FSnareAutoLoginContext PieWithoutTitle = Pie;
	PieWithoutTitle.bHasTitleId = false;
	TestFalse(TEXT("Title ID 가 없으면 PIE 에서도 안 붙는다"),
		USnareBackendSubsystem::ShouldAutoLoginFor(PieWithoutTitle));

	FSnareAutoLoginContext PieInAutomation = Pie;
	PieInAutomation.bAutomationTest = true;
	TestFalse(TEXT("자동화 검사 중에는 PIE 라도 안 붙는다"),
		USnareBackendSubsystem::ShouldAutoLoginFor(PieInAutomation));

	// 에디터를 띄워만 둔 상태(월드가 PIE 가 아니다) — 사람이 Play 를 누르기 전이다.
	FSnareAutoLoginContext EditorIdle = Pie;
	EditorIdle.bPlayInEditor = false;
	EditorIdle.PlayInEditorInstance = INDEX_NONE;
	TestFalse(TEXT("Play 를 누르기 전 에디터에서는 안 붙는다"),
		USnareBackendSubsystem::ShouldAutoLoginFor(EditorIdle));

	// 실제 게임 실행은 예전 그대로 — Title ID 가 비어 있어도 시도는 한다(경고를 남기는 자리가 따로 있다).
	FSnareAutoLoginContext Game;
	Game.bRunningGame = true;
	TestTrue(TEXT("-game·패키지에서는 그대로 붙는다"), USnareBackendSubsystem::ShouldAutoLoginFor(Game));

	// 커맨드릿: 에디터 깃발이 서 있고 게임 실행이 아니며 월드도 PIE 가 아니다.
	FSnareAutoLoginContext Commandlet;
	Commandlet.bEditor = true;
	Commandlet.bHasTitleId = true;
	TestFalse(TEXT("커맨드릿에서는 안 붙는다"), USnareBackendSubsystem::ShouldAutoLoginFor(Commandlet));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendPieWorldTest, "Snarehaven.Backend.Login.ReadsPieFromWorldContext",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendPieWorldTest::RunTest(const FString& Parameters)
{
	// 상황을 손으로 채운 위 검사와 달리, 여기는 **엔진이 PIE 에 쓰는 그 타입**(`FWorldContext`)을 넣어
	// 수집이 진짜로 PIE 를 알아보는지 본다 — 판정이 이름이 아니라 월드 종류를 보고 있어야 한다.
	FWorldContext PieContext;
	PieContext.WorldType = EWorldType::PIE;
	PieContext.PIEInstance = 2;
	const FSnareAutoLoginContext FromPie = USnareBackendSubsystem::MakeAutoLoginContextFor(&PieContext);
	TestTrue(TEXT("PIE 월드 컨텍스트를 PIE 로 읽는다"), FromPie.bPlayInEditor);
	TestEqual(TEXT("PIE 인스턴스 번호를 그대로 들고 온다"), FromPie.PlayInEditorInstance, 2);

	FWorldContext GameContext;
	GameContext.WorldType = EWorldType::Game;
	const FSnareAutoLoginContext FromGame = USnareBackendSubsystem::MakeAutoLoginContextFor(&GameContext);
	TestFalse(TEXT("게임 월드는 PIE 가 아니다"), FromGame.bPlayInEditor);
	TestEqual(TEXT("게임 월드에는 인스턴스 번호가 없다"), FromGame.PlayInEditorInstance, (int32)INDEX_NONE);

	// 에디터 월드(Play 를 누르기 전)도 PIE 가 아니다 — 에디터 깃발만 보고 켜면 여기서 켜진다.
	FWorldContext EditorContext;
	EditorContext.WorldType = EWorldType::Editor;
	TestFalse(TEXT("에디터 월드는 PIE 가 아니다"),
		USnareBackendSubsystem::MakeAutoLoginContextFor(&EditorContext).bPlayInEditor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendTitleIdTest, "Snarehaven.Backend.Config.ReadsTitleIdFromPlayFabIni",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendTitleIdTest::RunTest(const FString& Parameters)
{
	const FString IniPath = FPaths::ProjectConfigDir() / TEXT("PlayFab.ini");
	const FString TitleId = USnareBackendSubsystem::ReadTitleIdFromConfig();

	if (!FPaths::FileExists(IniPath))
	{
		// 이 파일은 gitignore 된다 — 저장소를 갓 받은 사람에게는 없는 게 정상이고, 그때는 빈 값이어야 한다.
		TestTrue(TEXT("설정 파일이 없으면 Title ID 도 비어 있다"), TitleId.IsEmpty());
		return true;
	}

	// 파일이 있는데 못 읽으면 절 이름이 어긋난 것이다 — 로그인이 조용히 꺼지는 사고의 원인이다.
	TestFalse(TEXT("설정 파일이 있으면 Title ID 를 읽어 온다"), TitleId.IsEmpty());
	TestFalse(TEXT("Title ID 에 공백이 섞이지 않았다"), TitleId.Contains(TEXT(" ")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendDeviceIdTest, "Snarehaven.Backend.Login.DeviceIdIsStable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendDeviceIdTest::RunTest(const FString& Parameters)
{
	const FString First = USnareBackendSubsystem::MakeDeviceId();
	const FString Second = USnareBackendSubsystem::MakeDeviceId();

	// 값이 실행마다 바뀌면 켤 때마다 새 게스트 계정이 생기고 지난 진행이 사라진다 (DESIGN 4.4).
	TestFalse(TEXT("기기 ID 가 비어 있지 않다"), First.IsEmpty());
	TestEqual(TEXT("같은 기기에서 두 번 물어도 같은 값이다"), Second, First);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendPieDeviceIdTest, "Snarehaven.Backend.Login.DeviceIdSplitsPerPieWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendPieDeviceIdTest::RunTest(const FString& Parameters)
{
	const FString Device = USnareBackendSubsystem::MakeDeviceId();

	// 실제 게임 실행: 접미사가 붙으면 지금 기기에 쌓인 게스트 진행이 통째로 남의 계정이 된다.
	FSnareAutoLoginContext Game;
	Game.bRunningGame = true;
	TestEqual(TEXT("게임 실행에서는 기기 값 그대로다"),
		USnareBackendSubsystem::MakeDeviceIdForInstance(Game), Device);

	// PIE 창 둘: 같은 기기라 값이 같으면 한 계정에 둘이 붙는다 (감독 지시 2026-09-24 00:05 ①).
	FSnareAutoLoginContext FirstWindow;
	FirstWindow.bEditor = true;
	FirstWindow.bPlayInEditor = true;
	FirstWindow.PlayInEditorInstance = 0;
	FSnareAutoLoginContext SecondWindow = FirstWindow;
	SecondWindow.PlayInEditorInstance = 1;

	const FString FirstId = USnareBackendSubsystem::MakeDeviceIdForInstance(FirstWindow);
	const FString SecondId = USnareBackendSubsystem::MakeDeviceIdForInstance(SecondWindow);
	TestNotEqual(TEXT("PIE 창끼리 기기 ID 가 갈린다"), FirstId, SecondId);
	TestNotEqual(TEXT("PIE 기기 ID 는 실기기 값과도 갈린다"), FirstId, Device);
	TestEqual(TEXT("같은 PIE 창은 다시 물어도 같은 값이다"),
		USnareBackendSubsystem::MakeDeviceIdForInstance(FirstWindow), FirstId);
	TestTrue(TEXT("PIE 기기 ID 는 기기 값으로 시작한다"), FirstId.StartsWith(Device));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBackendLoginApiTest, "Snarehaven.Backend.Login.UsesPlatformDeviceApi",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBackendLoginApiTest::RunTest(const FString& Parameters)
{
	// DESIGN 4.4 가 정한 것은 모바일 두 갈래다. 맥·윈도는 검사·개발 기기라 CustomID 로 같은 기기 ID 를 보낸다.
#if PLATFORM_IOS
	const FString Expected = TEXT("LoginWithIOSDeviceID");
#elif PLATFORM_ANDROID
	const FString Expected = TEXT("LoginWithAndroidDeviceID");
#else
	const FString Expected = TEXT("LoginWithCustomID");
#endif
	TestEqual(TEXT("이 플랫폼에서 부를 로그인 API"), USnareBackendSubsystem::GetDeviceLoginApiName(), Expected);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
