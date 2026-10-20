// ROADMAP M28-09d2 2층 검사 — 거점 요청 결과 글을 HUD 알림 줄로 (#0520 빈 칸).
// 이 검사가 잡는 것: ① 제작 판이 닫혀 있을 때 놓기·텐트 결과 글이 화면 어디에도 안 뜨는 것
// ② 거절 글이 사냥 HUD 성공 알림 카드로 가는 것(또는 그 반대) ③ 사냥 HUD 가 없는 거점에서 목장 HUD 로 안 가는 것
// ④ 제작 판이 열려 있을 때 판 토스트와 HUD 에 같은 글이 두 번 뜨는 것
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.Building.BaseMessageHud; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "SnarehavenCharacter.h"
#include "UI/CraftingScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/RanchHudWidget.h"
#include "Widgets/SWidget.h"

namespace SnareBaseMessageHudTest
{
	// 검사 전용 값 — 서버 문장이 아니다 (Verification.md 6).
	const TCHAR* const RejectSentence = TEXT("시험용 거점 거절 문장");
	const TCHAR* const NoticeSentence = TEXT("시험용 거점 성공 문장");

	struct FRig
	{
		UWorld* World = nullptr;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FRig()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenBaseMessageHudTest, "Snarehaven.Building.BaseMessageHud.ClosedScreenShowsOnHud",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenBaseMessageHudTest::RunTest(const FString& Parameters)
{
	using namespace SnareBaseMessageHudTest;
	FRig Rig;
	UHuntHudWidget* const HuntHud = CreateWidget<UHuntHudWidget>(Rig.World, UHuntHudWidget::StaticClass());
	URanchHudWidget* const RanchHud = CreateWidget<URanchHudWidget>(Rig.World, URanchHudWidget::StaticClass());
	UCraftingScreenWidget* const Craft = CreateWidget<UCraftingScreenWidget>(Rig.World, UCraftingScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("사냥 HUD"), HuntHud) || !TestNotNull(TEXT("목장 HUD"), RanchHud) || !TestNotNull(TEXT("제작 판"), Craft))
	{
		return false;
	}
	const TSharedRef<SWidget> HuntSlate = HuntHud->TakeWidget();
	const TSharedRef<SWidget> RanchSlate = RanchHud->TakeWidget();
	const TSharedRef<SWidget> CraftSlate = Craft->TakeWidget();
	const FText Reject = FText::FromString(RejectSentence);
	const FText Notice = FText::FromString(NoticeSentence);

	// 사냥 HUD 가 있으면 — 거절은 오류 알약, 성공은 알림 카드. 목장 HUD 엔 안 간다.
	TestTrue(TEXT("판이 닫혀 있으면 거절 글을 띄운다"), ASnarehavenCharacter::RouteBaseMessage(Reject, true, Craft, HuntHud, RanchHud));
	TestEqual(TEXT("거절 글은 사냥 HUD 오류 알약"), HuntHud->GetErrorBannerText().ToString(), FString(RejectSentence));
	TestTrue(TEXT("사냥 HUD 가 있으면 목장 HUD 엔 안 간다"), RanchHud->GetFailureNoticeText().IsEmpty());
	TestTrue(TEXT("성공 글도 띄운다"), ASnarehavenCharacter::RouteBaseMessage(Notice, false, nullptr, HuntHud, RanchHud));
	TestEqual(TEXT("성공 글은 사냥 HUD 알림 카드"), HuntHud->GetCaptureBannerText().ToString(), FString(NoticeSentence));

	// 거점(사냥 HUD 없음) — 목장 HUD 거절 줄.
	TestTrue(TEXT("거점에서도 띄운다"), ASnarehavenCharacter::RouteBaseMessage(Reject, true, Craft, nullptr, RanchHud));
	TestEqual(TEXT("거점이면 목장 HUD 거절 줄"), RanchHud->GetFailureNoticeText().ToString(), FString(RejectSentence));

	// 제작 판이 열려 있으면 판 토스트가 보인다 — HUD 엔 안 띄운다.
	URanchHudWidget* const QuietRanchHud = CreateWidget<URanchHudWidget>(Rig.World, URanchHudWidget::StaticClass());
	const TSharedRef<SWidget> QuietSlate = QuietRanchHud->TakeWidget();
	Craft->OpenWorkbenchForTest();
	TestTrue(TEXT("제작 판이 열렸다"), Craft->IsOpen());
	TestFalse(TEXT("판이 열려 있으면 HUD 에 안 띄운다"), ASnarehavenCharacter::RouteBaseMessage(Notice, false, Craft, nullptr, QuietRanchHud));
	TestTrue(TEXT("판이 열려 있으면 목장 HUD 거절 줄 비어 있음"), QuietRanchHud->GetFailureNoticeText().IsEmpty());
	Craft->Close();

	TestFalse(TEXT("빈 글은 안 띄운다"), ASnarehavenCharacter::RouteBaseMessage(FText::GetEmpty(), true, nullptr, nullptr, QuietRanchHud));
	AddInfo(FString::Printf(TEXT("[BaseMessageHud] 거절 → 사냥 오류 알약 '%s' · 거점 → 목장 HUD '%s'"),
		*HuntHud->GetErrorBannerText().ToString(), *RanchHud->GetFailureNoticeText().ToString()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
