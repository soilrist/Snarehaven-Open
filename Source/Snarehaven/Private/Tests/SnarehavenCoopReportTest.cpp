// ROADMAP M21-06b2 2층 검사 — 클라이언트 같이 하기 보고:
// ① 보고 주기 = DESIGN 3.2 「같이 하기 보고 주기」 표 값, 처음엔 바로 · 주기 전엔 안 보냄,
// ② 인자 — 혼자·로그인 안 함·호스트 모름이면 안 보냄, 둘이면 hostId(호스트 몸 id)·peers(나 뺀 사람, 빈 칸·중복 뺌),
// ③ 서버 답 `coop` 일 때만 목장 HUD 에 같이 하기 줄, 실패·`coop:false` 면 줄이 사라진다, 내 몸이 아니면 아무것도 안 부른다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.CoopReport; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Ranch/CoopReportComponent.h"
#include "UI/RanchHudWidget.h"

namespace SnarehavenCoopReportTest
{
	// 테스트 전용 값 (Verification.md 6 — 설계 숫자가 아니다).
	const FString MeId = TEXT("TestMe");
	const FString HostId = TEXT("TestHost");
	const FString OtherId = TEXT("TestOther");
	constexpr double TestStartSeconds = 100.0;

	TSharedPtr<FJsonObject> MakeCoopReply(bool bCoop)
	{
		const TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetBoolField(TEXT("coop"), bCoop);
		Response->SetNumberField(TEXT("players"), bCoop ? 2 : 1);
		return Response;
	}
}

// ① 주기 = DESIGN 3.2 표 (코드에 60 을 적지 않는다).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCoopReportIntervalTest, "Snarehaven.CoopReport.Interval.MatchesDesign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenCoopReportIntervalTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenCoopReportTest;
	FString Design;
	if (!TestTrue(TEXT("DESIGN.md 읽기"), FFileHelper::LoadFileToString(Design, *FPaths::Combine(FPaths::ProjectDir(), TEXT("docs/DESIGN.md")))))
	{
		return false;
	}
	FRegexMatcher Matcher(FRegexPattern(TEXT(R"(\| 같이 하기 보고 주기 \(M21-06b2\) \| \*\*(\d+)초\*\*)")), Design);
	if (!TestTrue(TEXT("DESIGN 3.2 「같이 하기 보고 주기」 줄"), Matcher.FindNext()))
	{
		return false;
	}
	const float Interval = UCoopReportComponent::GetReportIntervalSeconds();
	TestEqual(TEXT("보고 주기 = DESIGN 표"), Interval, static_cast<float>(FCString::Atoi(*Matcher.GetCaptureGroup(1))));

	TestTrue(TEXT("한 번도 안 보냈으면 바로"), UCoopReportComponent::ShouldReportNow(TestStartSeconds, -1.0));
	TestFalse(TEXT("주기 1초 전엔 안 보냄"), UCoopReportComponent::ShouldReportNow(TestStartSeconds + Interval - 1.0, TestStartSeconds));
	TestTrue(TEXT("주기가 되면 보냄"), UCoopReportComponent::ShouldReportNow(TestStartSeconds + Interval, TestStartSeconds));
	return true;
}

// ② 인자 — 누구를 hostId·peers 로 싣고 언제 안 보내나.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCoopReportArgsTest, "Snarehaven.CoopReport.Args.HostAndPeers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenCoopReportArgsTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenCoopReportTest;

	// 혼자(내 몸만) — 안 보낸다.
	const FSnareCoopReport Alone = UCoopReportComponent::BuildReport(MeId, { { MeId, true } });
	TestFalse(TEXT("혼자면 안 보냄"), Alone.bSend);

	// 로그인 안 함 — 다른 사람이 있어도 안 보낸다.
	const FSnareCoopReport NoLogin = UCoopReportComponent::BuildReport(FString(), { { HostId, true }, { OtherId, false } });
	TestFalse(TEXT("로그인 안 함이면 안 보냄"), NoLogin.bSend);

	// 호스트 몸 id 를 아직 모름 — 안 보낸다(서버가 같은 hostId 로 맞대 세서 hostId 없이는 의미가 없다).
	const FSnareCoopReport NoHost = UCoopReportComponent::BuildReport(MeId, { { MeId, false }, { OtherId, false } });
	TestFalse(TEXT("호스트 모르면 안 보냄"), NoHost.bSend);

	// 내가 게스트 — hostId = 호스트, peers = 호스트 + 다른 사람(빈 id·중복·나는 뺀다).
	const FSnareCoopReport Guest = UCoopReportComponent::BuildReport(MeId,
		{ { HostId, true }, { MeId, false }, { OtherId, false }, { FString(), false }, { OtherId, false } });
	TestTrue(TEXT("게스트 보냄"), Guest.bSend);
	TestEqual(TEXT("게스트 hostId"), Guest.HostId, HostId);
	TestEqual(TEXT("게스트 peers 수"), Guest.Peers.Num(), 2);
	TestTrue(TEXT("peers 에 호스트"), Guest.Peers.Contains(HostId));
	TestTrue(TEXT("peers 에 다른 사람"), Guest.Peers.Contains(OtherId));
	TestFalse(TEXT("peers 에 나 없음"), Guest.Peers.Contains(MeId));

	// 내가 호스트 — hostId = 나, peers = 게스트.
	const FSnareCoopReport Host = UCoopReportComponent::BuildReport(HostId, { { HostId, true }, { OtherId, false } });
	TestTrue(TEXT("호스트 보냄"), Host.bSend);
	TestEqual(TEXT("호스트 hostId 는 나"), Host.HostId, HostId);
	TestEqual(TEXT("호스트 peers"), Host.Peers, TArray<FString>{ OtherId });

	// 서버로 나가는 JSON — `handlers.ReportCoop` 이 읽는 이름 그대로.
	const TSharedRef<FJsonObject> Args = UCoopReportComponent::MakeArgs(Guest);
	TestEqual(TEXT("인자 hostId"), Args->GetStringField(TEXT("hostId")), HostId);
	const TArray<TSharedPtr<FJsonValue>>* Peers = nullptr;
	TestTrue(TEXT("인자 peers 배열"), Args->TryGetArrayField(TEXT("peers"), Peers));
	TestEqual(TEXT("인자 peers 수"), Peers != nullptr ? Peers->Num() : -1, 2);
	return true;
}

// ③ 서버 답 → HUD 한 줄. 내 몸이 아니면 Poll 이 서버를 안 부른다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenCoopReportHudTest, "Snarehaven.CoopReport.Reply.HudLineOnlyWhenCoop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenCoopReportHudTest::RunTest(const FString& Parameters)
{
	using namespace SnarehavenCoopReportTest;

	int32 ServerCalls = 0;
	USnareBackendSubsystem::TestServerOverride =
		[&ServerCalls](const FString&, const TSharedPtr<FJsonObject>&, USnareBackendSubsystem::FServerCallDone) { ++ServerCalls; };

	UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
	WorldContext.SetCurrentWorld(World);

	UCoopReportComponent* const Coop = NewObject<UCoopReportComponent>(GetTransientPackage());
	URanchHudWidget* const Hud = CreateWidget<URanchHudWidget>(World, URanchHudWidget::StaticClass());
	if (TestNotNull(TEXT("HUD"), Hud))
	{
		Hud->BindToCoopReport(Coop);
		TestTrue(TEXT("답 전엔 줄 없음"), Hud->GetCoopText().IsEmpty());

		Coop->ApplyReply(true, MakeCoopReply(true));
		TestTrue(TEXT("coop:true → 켜짐"), Coop->IsCoopActive());
		TestFalse(TEXT("coop:true → 줄 있음"), Hud->GetCoopText().IsEmpty());

		Coop->ApplyReply(true, MakeCoopReply(false));
		TestFalse(TEXT("coop:false → 꺼짐"), Coop->IsCoopActive());
		TestTrue(TEXT("coop:false → 줄 없음"), Hud->GetCoopText().IsEmpty());

		Coop->ApplyReply(true, MakeCoopReply(true));
		Coop->ApplyReply(false, nullptr);
		TestFalse(TEXT("실패 → 꺼짐"), Coop->IsCoopActive());
		TestTrue(TEXT("실패 → 줄 없음"), Hud->GetCoopText().IsEmpty());
		Hud->BindToCoopReport(nullptr);
	}

	// 주인 없는(내 몸 아닌) 컴포넌트는 주기가 돼도 서버를 안 부른다.
	Coop->Poll(TestStartSeconds);
	TestEqual(TEXT("내 몸 아니면 서버 호출 0"), ServerCalls, 0);

	USnareBackendSubsystem::TestServerOverride = nullptr;
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

#endif
