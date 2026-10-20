// ROADMAP M3-01a 2층 검사 — EOS 게스트 로그인이 **어떻게 불리는지**만 본다.
// 실제 EOS 왕복은 여기서 하지 않는다(네트워크가 있어야 하는 검사는 3층이다 — 명세 6장).
// 이 검사가 잡는 것: ① 자동화 중에 EOS 에 붙어 버리는 사고 ② 설정 절 이름이 엔진과 어긋나는 것
// ③ 로그인 자격 이름 오타(엔진 상수를 쓰는지).
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ConfigCacheIni.h"
#include "Online/Auth.h"
#include "Online/SnareOnlineSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOnlineNoAutoLoginInTestsTest, "Snarehaven.Online.Login.SkipsAutoLoginOutsideGame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOnlineNoAutoLoginInTestsTest::RunTest(const FString& Parameters)
{
	// 이 검사 자체가 에디터 컨텍스트에서 돈다 — 참이 나오면 검사를 돌릴 때마다 EOS 에 붙는다는 뜻이다.
	TestFalse(TEXT("에디터·자동화에서는 자동 로그인하지 않는다"), USnareOnlineSubsystem::ShouldAutoLogin());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOnlineConfigSectionTest, "Snarehaven.Online.Config.ReadsSameSectionAsEngine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOnlineConfigSectionTest::RunTest(const FString& Parameters)
{
	// 엔진(EOSShared)이 읽는 자리를 그대로 따라가 본다: [EOSSDK] DefaultPlatformConfigName 이 가리키는
	// [EOSSDK.Platform.<이름>] 절. 둘 중 하나라도 어긋나면 로그인 전에 조용히 꺼진다.
	FString PlatformConfigName;
	GConfig->GetString(TEXT("EOSSDK"), TEXT("DefaultPlatformConfigName"), PlatformConfigName, GEngineIni);
	if (!TestFalse(TEXT("DefaultEngine.ini 에 [EOSSDK] DefaultPlatformConfigName 이 있다"), PlatformConfigName.IsEmpty()))
	{
		return false;
	}

	const bool bSectionExists = GConfig->DoesSectionExist(*(TEXT("EOSSDK.Platform.") + PlatformConfigName), GEngineIni);
	TestEqual(TEXT("HasEosPlatformConfig 가 그 절의 존재와 같은 답을 준다"),
		USnareOnlineSubsystem::HasEosPlatformConfig(), bSectionExists);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOnlineLoginParamsTest, "Snarehaven.Online.Login.UsesDeviceIdExternalAuth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOnlineLoginParamsTest::RunTest(const FString& Parameters)
{
	using namespace UE::Online;

	const FAuthLogin::Params Params = USnareOnlineSubsystem::MakeDeviceIdLoginParams();

	// AuthEOSGS 는 이 셋이 다 맞을 때만 Connect 로그인으로 간다 (결정 004).
	TestEqual(TEXT("자격 종류는 ExternalAuth"), Params.CredentialsType, LoginCredentialsType::ExternalAuth);
	if (!TestTrue(TEXT("자격 토큰이 외부 인증 토큰이다"), Params.CredentialsToken.IsType<FExternalAuthToken>()))
	{
		return false;
	}
	TestEqual(TEXT("외부 인증 종류는 DeviceIdAccessToken"),
		Params.CredentialsToken.Get<FExternalAuthToken>().Type, ExternalLoginType::DeviceIdAccessToken);
	TestTrue(TEXT("0번 로컬 유저가 유효하다"), Params.PlatformUserId.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenOnlineFailureLineTest, "Snarehaven.Online.Login.FailureLineFormat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenOnlineFailureLineTest::RunTest(const FString& Parameters)
{
	// M20-05: 폰 로그에서 `EOS 실패:` 로 grep 해 단계·코드를 그대로 읽는다 — 형식이 바뀌면 진단이 끊긴다.
	TestEqual(TEXT("단계 + 결과 코드 한 줄"),
		USnareOnlineSubsystem::MakeEosFailureLine(TEXT("device_id"), TEXT("EOS_InvalidCredentials")),
		FString(TEXT("[Online] EOS 실패: device_id EOS_InvalidCredentials")));
	TestEqual(TEXT("설정 없음 줄"),
		USnareOnlineSubsystem::MakeEosFailureLine(TEXT("config"), TEXT("missing_platform_config")),
		FString(TEXT("[Online] EOS 실패: config missing_platform_config")));
	TestEqual(TEXT("빈 코드는 unknown"),
		USnareOnlineSubsystem::MakeEosFailureLine(TEXT("login"), FString()),
		FString(TEXT("[Online] EOS 실패: login unknown")));

	// 키 값이 섞이지 않는다 — 넣는 것은 단계와 코드뿐이라 줄은 정확히 세 토큰 뒤에 끝난다.
	TArray<FString> Tokens;
	USnareOnlineSubsystem::MakeEosFailureLine(TEXT("auth"), TEXT("no_auth_interface")).ParseIntoArrayWS(Tokens);
	TestEqual(TEXT("토큰 다섯 개([Online]·EOS·실패:·단계·코드)"), Tokens.Num(), 5);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
