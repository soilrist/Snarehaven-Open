// ROADMAP M4-02·M4-05·M4-06 2층 검사 — 모바일 설정이 실제로 설정 파일에 서 있는지.
// M4-05 는 디바이스 프로파일(Config/DefaultDeviceProfiles.ini), M4-06 은 iOS·M4-02 는 안드로이드 런타임 설정(Config/DefaultEngine.ini) 이다.
// 이 검사가 잡는 것: ① 절 이름을 엔진과 다르게 적어(오타·새 프로파일) 우리 줄이 아무 데도 안 걸리는 사고
// ② 목표 프레임이 사람 답(DESIGN 4.7 = 30fps)과 어긋나게 바뀌는 것 ③ 모바일 렌더러 전제(DESIGN 4.2)가 조용히 켜지는 것.
// 실기기 프레임은 여기서 재지 않는다 — 기기가 없다(M4-04 표 F 와 같은 한계). 이 검사는 **설정값**만 본다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreGlobals.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// 엔진이 쓰는 절 이름 그대로 (BaseDeviceProfiles.ini). 우리 Config/DefaultDeviceProfiles.ini 가 여기에 줄만 더한다.
	const TCHAR* MobileProfileSections[] = { TEXT("Android DeviceProfile"), TEXT("IOS DeviceProfile") };

	// DESIGN 4.7 표 "모바일 목표 프레임" — 감독 결정 2026-09-20. 설정이 이 답에서 밀리면 검사가 깨지라고 적는다.
	constexpr int32 DecidedMobileMaxFPS = 30;

	// 프로파일의 CVars 배열에서 `t.MaxFPS=<수>` 를 읽는다. 여러 줄이면 **뒤엣것이 이긴다**(엔진이 순서대로 적용한다).
	bool FindMaxFPS(const TCHAR* Section, int32& OutValue)
	{
		TArray<FString> CVars;
		GConfig->GetArray(Section, TEXT("CVars"), CVars, GDeviceProfilesIni);

		bool bFound = false;
		for (const FString& Entry : CVars)
		{
			FString Key;
			FString Value;
			if (Entry.Split(TEXT("="), &Key, &Value) && Key.TrimStartAndEnd() == TEXT("t.MaxFPS"))
			{
				OutValue = FCString::Atoi(*Value.TrimStartAndEnd());
				bFound = true;
			}
		}
		return bFound;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMobileMaxFPSTest, "Snarehaven.Mobile.Profile.TargetFrameRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMobileMaxFPSTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Section : MobileProfileSections)
	{
		int32 MaxFPS = 0;
		if (!TestTrue(FString::Printf(TEXT("[%s] 에 t.MaxFPS 가 있다"), Section), FindMaxFPS(Section, MaxFPS)))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("[%s] 목표 프레임이 사람이 정한 값이다"), Section), MaxFPS, DecidedMobileMaxFPS);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMobileRendererTest, "Snarehaven.Mobile.Profile.KeepsMobileRenderer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMobileRendererTest::RunTest(const FString& Parameters)
{
	// DESIGN 4.2 — Mobile Renderer 를 쓰고 Nanite/Lumen 은 끈다. 품질 설정을 만지다 이게 켜지면 모바일 목표가 무너진다.
	const TCHAR* RendererSection = TEXT("/Script/Engine.RendererSettings");

	bool bNanite = true;
	GConfig->GetBool(RendererSection, TEXT("r.Nanite.ProjectEnabled"), bNanite, GEngineIni);
	TestFalse(TEXT("Nanite 는 꺼져 있다"), bNanite);

	// 0 = None(Lumen 아님). 엔진 기본이 Lumen 이라 우리가 적은 0 이 지워지면 이 검사가 깨진다.
	int32 GIMethod = -1;
	GConfig->GetInt(RendererSection, TEXT("r.DynamicGlobalIlluminationMethod"), GIMethod, GEngineIni);
	TestEqual(TEXT("전역 조명은 Lumen 이 아니다"), GIMethod, 0);

	int32 ReflectionMethod = -1;
	GConfig->GetInt(RendererSection, TEXT("r.ReflectionMethod"), ReflectionMethod, GEngineIni);
	TestEqual(TEXT("반사도 Lumen 이 아니다"), ReflectionMethod, 0);

	// 0 = Mobile Renderer 의 디퍼드가 아닌 기본 경로.
	int32 MobileShadingPath = -1;
	GConfig->GetInt(RendererSection, TEXT("r.Mobile.ShadingPath"), MobileShadingPath, GEngineIni);
	TestEqual(TEXT("모바일 셰이딩 경로가 그대로다"), MobileShadingPath, 0);

	return true;
}

// ---- M4-06 iOS 설정 절 -------------------------------------------------------------------------

namespace
{
	// 엔진이 쓰는 절 이름 그대로 (BaseEngine.ini:3217). UBT 가 클래스를 만들지 않고 ini 에서 직접 읽는다.
	const TCHAR* IOSSection = TEXT("/Script/IOSRuntimeSettings.IOSRuntimeSettings");

	// DESIGN 4.7 표 — 감독 결정 2026-09-20. 값이 여기서 밀리면 검사가 깨지라고 적는다.
	const TCHAR* DecidedBundleIdentifier = TEXT("com.soilrist.snarehaven.ios");
	const TCHAR* DecidedFrameRateLock = TEXT("PUFRL_30");

	// 이 검사가 잡는 것: 방향 네 칸 중 하나라도 뒤집히면 세로가 열린다(카메라 합격폭 M0-04 가 가로 기준이다).
	void TestOrientationFlag(FAutomationTestBase& Test, const TCHAR* Key, bool bExpected)
	{
		bool bValue = !bExpected;
		const bool bFound = GConfig->GetBool(IOSSection, Key, bValue, GEngineIni);
		if (Test.TestTrue(FString::Printf(TEXT("iOS 설정에 %s 가 있다"), Key), bFound))
		{
			Test.TestEqual(FString::Printf(TEXT("%s 가 사람이 정한 방향과 같다"), Key), bValue, bExpected);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMobileIOSDecidedValuesTest, "Snarehaven.Mobile.IOS.MatchesDecidedValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMobileIOSDecidedValuesTest::RunTest(const FString& Parameters)
{
	FString BundleIdentifier;
	if (TestTrue(TEXT("iOS 번들 ID 가 설정에 있다"), GConfig->GetString(IOSSection, TEXT("BundleIdentifier"), BundleIdentifier, GEngineIni)))
	{
		// 엔진 기본은 com.YourCompany.[PROJECT_NAME] 이다 — 그게 남아 있으면 우리 절이 안 읽힌 것이다.
		TestEqual(TEXT("번들 ID 가 사람이 정한 값이다"), BundleIdentifier, FString(DecidedBundleIdentifier));
	}

	// 가로만, 세로 없음 (DESIGN 4.7 SensorLandscape 의 iOS 표현).
	TestOrientationFlag(*this, TEXT("bSupportsPortraitOrientation"), false);
	TestOrientationFlag(*this, TEXT("bSupportsUpsideDownOrientation"), false);
	TestOrientationFlag(*this, TEXT("bSupportsLandscapeLeftOrientation"), true);
	TestOrientationFlag(*this, TEXT("bSupportsLandscapeRightOrientation"), true);

	// 목표 프레임 30fps — M4-05 의 t.MaxFPS 와 같은 답을 iOS 쪽 자리에도 적은 것이다.
	FString FrameRateLock;
	if (TestTrue(TEXT("iOS 프레임 상한 칸이 설정에 있다"), GConfig->GetString(IOSSection, TEXT("FrameRateLock"), FrameRateLock, GEngineIni)))
	{
		TestEqual(TEXT("iOS 프레임 상한이 사람이 정한 30fps 다"), FrameRateLock, FString(DecidedFrameRateLock));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMobileIOSMinimumVersionTest, "Snarehaven.Mobile.IOS.LeavesMinimumVersionToEngine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMobileIOSMinimumVersionTest::RunTest(const FString& Parameters)
{
	// 최소 iOS 버전의 답은 "엔진 기본 그대로"(DESIGN 4.7)다 — 그러니 **적지 않은 것**이 지켜야 할 상태다.
	// 값을 박으면 엔진이 기본을 올릴 때 우리 줄이 옛 값으로 붙잡는다. 이 검사가 그 실수를 잡는다.
	FString MinimumVersion;
	TestFalse(TEXT("최소 iOS 버전을 우리가 박지 않았다"),
		GConfig->GetString(IOSSection, TEXT("MinimumiOSVersion"), MinimumVersion, GEngineIni));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMobileIOSEosPluginsTest, "Snarehaven.Mobile.IOS.AllowsEosPlugins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMobileIOSEosPluginsTest::RunTest(const FString& Parameters)
{
	// 엔진 원본 OnlineServicesEOSGS·SocketSubsystemEOS 는 모듈 PlatformAllowList 에 IOS 가 없어 iOS 패키지에서
	// 서비스가 안 만들어졌다(폰 로그 `EOS 실패: services no_services`, 루프#661). 그래서 Plugins/ 에 사본을 두고
	// IOS 만 더했다 — 엔진·UBT 둘 다 같은 이름이면 프로젝트 쪽을 쓴다. 사본이 지워지거나 IOS 가 빠지면 여기서 깨진다.
	const TCHAR* PluginNames[] = { TEXT("OnlineServicesEOSGS"), TEXT("SocketSubsystemEOS") };
	for (const TCHAR* PluginName : PluginNames)
	{
		const FString Path = FPaths::Combine(FPaths::ProjectPluginsDir(), PluginName, FString(PluginName) + TEXT(".uplugin"));
		FString Text;
		if (!TestTrue(FString::Printf(TEXT("%s 사본이 Plugins/ 에 있다"), PluginName), FFileHelper::LoadFileToString(Text, *Path)))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Root;
		if (!TestTrue(FString::Printf(TEXT("%s uplugin 을 읽는다"), PluginName),
			FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid()))
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* Modules = nullptr;
		TestTrue(FString::Printf(TEXT("%s 모듈 목록이 있다"), PluginName), Root->TryGetArrayField(TEXT("Modules"), Modules));
		for (const TSharedPtr<FJsonValue>& Module : Modules ? *Modules : TArray<TSharedPtr<FJsonValue>>())
		{
			TArray<FString> Platforms;
			Module->AsObject()->TryGetStringArrayField(TEXT("PlatformAllowList"), Platforms);
			TestTrue(FString::Printf(TEXT("%s 모듈이 IOS 를 허용한다"), PluginName), Platforms.Contains(TEXT("IOS")));
		}
	}

	return true;
}

// ---- M4-02 안드로이드 설정 절 ------------------------------------------------------------------

namespace
{
	// 엔진이 쓰는 절 이름 그대로 (BaseEngine.ini:3281). iOS 와 마찬가지로 UBT 가 ini 에서 직접 읽는다.
	const TCHAR* AndroidSection = TEXT("/Script/AndroidRuntimeSettings.AndroidRuntimeSettings");

	// DESIGN 4.7 표 — 감독 결정 2026-09-20. 값이 여기서 밀리면 검사가 깨지라고 적는다.
	const TCHAR* DecidedPackageName = TEXT("com.soilrist.snarehaven");
	constexpr int32 DecidedMinSDKVersion = 26;
	constexpr int32 DecidedTargetSDKVersion = 36;
	const TCHAR* DecidedOrientation = TEXT("SensorLandscape");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMobileAndroidDecidedValuesTest, "Snarehaven.Mobile.Android.MatchesDecidedValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMobileAndroidDecidedValuesTest::RunTest(const FString& Parameters)
{
	FString PackageName;
	if (TestTrue(TEXT("안드로이드 패키지 이름이 설정에 있다"), GConfig->GetString(AndroidSection, TEXT("PackageName"), PackageName, GEngineIni)))
	{
		// 엔진 기본은 com.YourCompany.[PROJECT] 다 — 그게 남아 있으면 우리 절이 안 읽힌 것이다.
		TestEqual(TEXT("패키지 이름이 사람이 정한 값이다"), PackageName, FString(DecidedPackageName));
	}

	int32 MinSDKVersion = 0;
	if (TestTrue(TEXT("최소 SDK 칸이 설정에 있다"), GConfig->GetInt(AndroidSection, TEXT("MinSDKVersion"), MinSDKVersion, GEngineIni)))
	{
		TestEqual(TEXT("최소 SDK 가 사람이 정한 값이다"), MinSDKVersion, DecidedMinSDKVersion);
	}

	int32 TargetSDKVersion = 0;
	if (TestTrue(TEXT("대상 SDK 칸이 설정에 있다"), GConfig->GetInt(AndroidSection, TEXT("TargetSDKVersion"), TargetSDKVersion, GEngineIni)))
	{
		TestEqual(TEXT("대상 SDK 가 사람이 정한 값이다"), TargetSDKVersion, DecidedTargetSDKVersion);
	}

	// 가로만, 세로 없음 — iOS 는 불리언 네 칸으로 같은 답을 적는다(위 M4-06 검사).
	FString Orientation;
	if (TestTrue(TEXT("화면 방향 칸이 설정에 있다"), GConfig->GetString(AndroidSection, TEXT("Orientation"), Orientation, GEngineIni)))
	{
		TestEqual(TEXT("화면 방향이 사람이 정한 값이다"), Orientation, FString(DecidedOrientation));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenMobileAndroidArchitectureTest, "Snarehaven.Mobile.Android.BuildsArm64Only",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenMobileAndroidArchitectureTest::RunTest(const FString& Parameters)
{
	// ROADMAP M4-02 — arm64 하나만. x86-64 가 켜지면 apk 가 두 배로 커지는데 얻는 것은 에뮬레이터뿐이다.
	bool bBuildForArm64 = false;
	if (TestTrue(TEXT("arm64 칸이 설정에 있다"), GConfig->GetBool(AndroidSection, TEXT("bBuildForArm64"), bBuildForArm64, GEngineIni)))
	{
		TestTrue(TEXT("arm64 로 묶는다"), bBuildForArm64);
	}

	bool bBuildForX8664 = true;
	if (TestTrue(TEXT("x86-64 칸이 설정에 있다"), GConfig->GetBool(AndroidSection, TEXT("bBuildForX8664"), bBuildForX8664, GEngineIni)))
	{
		TestFalse(TEXT("x86-64 는 안 묶는다"), bBuildForX8664);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
