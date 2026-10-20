// ROADMAP M15-06 2층 검사 — 기기(iOS)에서 찍은 3층 캡처를 꺼낼 수 있는 자리에 떨어뜨리는가.
// 이 검사가 잡는 것: ① 상대 경로가 작업 폴더(데스크톱 = 엔진 바이너리 폴더, iOS = 쓰기 금지인 앱 번들)로
// 풀려 캡처가 조용히 사라지는 것 ② 절대 경로를 준 옛 3층 명령이 어느 날 Saved 밑으로 끌려 들어가는 것
// ③ 로그에 찍히는 경로와 파일이 떨어지는 자리가 달라져(`..` 가 접히지 않아) 기기에서 찾을 수 없게 되는 것.
// 실제 기기 왕복(`tools/ios/pull_captures.py`)은 폰이 붙어 있어야 하므로 여기서 보지 않는다 —
// 여기서 굳히는 것은 **꺼낼 수 있는 자리에 쓰는가** 하나다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/Paths.h"
#include "Snarehaven.h"
#include "SnarehavenGameMode.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenDeviceCapturePathTest, "Snarehaven.Capture.PathLandsWhereWeCanFetchIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenDeviceCapturePathTest::RunTest(const FString& Parameters)
{
	const FString SavedDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());

	// ① 폴더를 낀 상대 경로 — 문서가 예로 드는 모양(`-SnareCapture=M15/cam.png`).
	const FString Nested = FPaths::ConvertRelativePathToFull(
		ASnarehavenGameMode::ResolveCapturePath(TEXT("M15/cam.png")));
	TestTrue(FString::Printf(TEXT("폴더를 낀 상대 경로가 Saved 밑으로 간다 (%s)"), *Nested),
		FPaths::IsUnderDirectory(Nested, SavedDir));
	TestTrue(TEXT("폴더 이름과 파일 이름이 그대로 남는다"), Nested.EndsWith(TEXT("M15/cam.png")));

	// ② 이름만 준 상대 경로. 여기가 작업 폴더로 풀리면 데스크톱은 엔진 바이너리 폴더,
	//    iOS 는 앱 번들(쓰기 금지)이라 캡처가 없어진다 — 루프#166 이 실제로 겪었다.
	const FString BareName = FPaths::ConvertRelativePathToFull(
		ASnarehavenGameMode::ResolveCapturePath(TEXT("cam.png")));
	TestTrue(FString::Printf(TEXT("이름만 줘도 Saved 밑으로 간다 (%s)"), *BareName),
		FPaths::IsUnderDirectory(BareName, SavedDir));

	// ③ 절대 경로는 손대지 않는다 — 지금까지의 3층 명령이 전부 절대 경로(`captures/…` 의 풀 경로)다.
	//    여기서 Saved 밑으로 끌어들이면 바퀴마다 쓰는 캡처 경로가 통째로 어긋난다.
	//    검사 전용 값이라 설계 숫자가 아니다 (Verification.md 6).
	const FString Absolute = TEXT("/tmp/snarehaven_capture_test/shot.png");
	TestEqual(TEXT("절대 경로는 그대로 둔다"), ASnarehavenGameMode::ResolveCapturePath(Absolute), Absolute);

	// ④ 사람이 준 조각의 `..` 은 접는다 — 로그의 `검사 캡처 요청: …` 줄이 곧 사람이 기기에서 찾아갈
	//    주소라 글자까지 같아야 한다. (`Saved` 경로 앞머리의 `../` 는 iOS 가 Documents 로 옮겨 적을 때
	//    쓰는 표식이라 남아 있어도 된다 — 그래서 붙이기 **전에** 접는다.)
	const FString Collapsed = FPaths::ConvertRelativePathToFull(
		ASnarehavenGameMode::ResolveCapturePath(TEXT("M15/../cam.png")));
	TestEqual(TEXT("사람이 준 조각의 `..` 이 접혀 Saved 바로 밑에 떨어진다"),
		Collapsed, FPaths::Combine(SavedDir, TEXT("cam.png")));

	// ⑤ 스위치가 빈 글자로 오면(`-SnareCapture=`) 아무 자리도 지어내지 않는다 —
	//    Saved 폴더 자체를 파일 이름으로 삼아 엉뚱한 곳에 쓰는 것을 막는다.
	TestTrue(TEXT("빈 글자는 빈 글자 그대로"), ASnarehavenGameMode::ResolveCapturePath(FString()).IsEmpty());

	UE_LOG(LogSnarehaven, Display, TEXT("[DeviceCapture] Saved %s · 상대 M15/cam.png → %s · 절대는 그대로"),
		*SavedDir, *Nested);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
