// ROADMAP M26-04 2층 검사 — 거절 알림을 빨간 알약으로.
// 이 검사가 잡는 것: ① 거절·실패 글이 성공 알림과 같은 크림 카드로 뜨는 것 ② 오류 알약 색이 `UiAccentRed` 가 아닌 것
// ③ 오류와 알림 카드가 같은 자리에 겹쳐 둘 다 서는 것 ④ 성공 알림이 오류 알약으로 가는 것
// 배낭 길은 컴포넌트가 판정 끝에 내는 답(`OnReply`)을 그대로 쓴다. 서버는 부르지 않는다.
// 실행: UnrealEditor-Cmd <uproject 절대경로> -ExecCmds="Automation RunTests Snarehaven.HuntHud.ErrorPill; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/SceneComponent.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Inventory/BackpackComponent.h"
#include "UI/BackpackScreenWidget.h"
#include "UI/HuntHudWidget.h"
#include "UI/SnareUiStyle.h"

namespace SnareErrorPillTest
{
	// 검사 전용 값 — 표에 없는 code·서버 문장 (Verification.md 6).
	const TCHAR* const RejectCode = TEXT("test_reject_code");
	const TCHAR* const RejectSentence = TEXT("시험용 거절 문장");
	const TCHAR* const NoticeSentence = TEXT("시험용 성공 알림");

	/** 게임 월드 + 로그인 안 한 백엔드 + 배낭 컴포넌트. */
	struct FRig
	{
		UWorld* World = nullptr;
		USnareBackendSubsystem* Backend = nullptr;
		UBackpackComponent* Backpack = nullptr;

		FRig()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			Backend = NewObject<USnareBackendSubsystem>(NewObject<UGameInstance>(GetTransientPackage()));

			AActor* const Owner = World->SpawnActor<AActor>();
			USceneComponent* const Root = NewObject<USceneComponent>(Owner, TEXT("Root"));
			Owner->SetRootComponent(Root);
			Root->RegisterComponent();
			Backpack = NewObject<UBackpackComponent>(Owner, TEXT("Backpack"));
			Backpack->RegisterComponent();
			Backpack->BindTargets(Backend, nullptr);
		}

		~FRig()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

using namespace SnareErrorPillTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenErrorPillTest, "Snarehaven.HuntHud.ErrorPill.RejectShowsRedPill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenErrorPillTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(Rig.World, UHuntHudWidget::StaticClass());
	UBackpackScreenWidget* const Pack = CreateWidget<UBackpackScreenWidget>(Rig.World, UBackpackScreenWidget::StaticClass());
	if (!TestNotNull(TEXT("사냥 HUD"), Hud) || !TestNotNull(TEXT("배낭 화면"), Pack))
	{
		return false;
	}
	// 반환값을 붙잡고 있어야 트리가 산다.
	const TSharedRef<SWidget> HudSlate = Hud->TakeWidget();
	const TSharedRef<SWidget> PackSlate = Pack->TakeWidget();
	Pack->BindTargets(Rig.Backend, Rig.Backpack, nullptr);
	Pack->SetNoticeHud(Hud);

	UBorder* const Pill = Hud->WidgetTree->FindWidget<UBorder>(TEXT("ErrorPill"));
	UTextBlock* const PillText = Hud->WidgetTree->FindWidget<UTextBlock>(TEXT("ErrorText"));
	UTextBlock* const PillMark = Hud->WidgetTree->FindWidget<UTextBlock>(TEXT("ErrorMark"));
	UWidget* const Card = Hud->WidgetTree->FindWidget(TEXT("NoticeCard"));
	if (!TestNotNull(TEXT("오류 알약"), Pill) || !TestNotNull(TEXT("오류 글자"), PillText)
		|| !TestNotNull(TEXT("오류 표시 !"), PillMark) || !TestNotNull(TEXT("알림 카드"), Card))
	{
		return false;
	}

	Hud->RefreshDisplay();
	TestEqual(TEXT("오류가 없으면 알약이 접힌다"), Pill->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("알약 색 = UiAccentRed"), Pill->GetBrushColor().Equals(SnareUiStyle::UiAccentRed()));
	TestEqual(TEXT("왼쪽 표시 = !"), PillMark->GetText().ToString(), FString(TEXT("!")));

	// 크림 카드가 떠 있을 때 배낭 거절 답이 오면 — 알약이 서고 카드는 접힌다.
	Hud->ShowNoticeBanner(FText::FromString(NoticeSentence));
	Rig.Backpack->OnReply.Broadcast(false, RejectCode, RejectSentence);
	TestNotEqual(TEXT("거절 → 알약 보임"), Pill->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("거절 → 알약 글 = 서버 문장"), PillText->GetText().ToString(), FString(RejectSentence));
	TestEqual(TEXT("거절 → 크림 카드 접힘"), Card->GetVisibility(), ESlateVisibility::Collapsed);
	const bool bRejectToPill = Hud->IsErrorBannerVisible() && !Hud->IsCaptureBannerVisible();

	// 성공 알림은 크림 카드 그대로 — 알약은 내려간다.
	Hud->ShowNoticeBanner(FText::FromString(NoticeSentence));
	TestNotEqual(TEXT("성공 → 크림 카드 보임"), Card->GetVisibility(), ESlateVisibility::Collapsed);
	TestEqual(TEXT("성공 → 알약 접힘"), Pill->GetVisibility(), ESlateVisibility::Collapsed);
	TestTrue(TEXT("성공 → 오류 글 빔"), Hud->GetErrorBannerText().IsEmpty());

	// 받아들인 답은 알약을 세우지 않는다.
	Rig.Backpack->OnReply.Broadcast(true, FString(), FString());
	TestFalse(TEXT("받아들인 답 → 알약 없음"), Hud->IsErrorBannerVisible());

	Hud->ShowErrorBanner(FText::FromString(RejectSentence));
	Hud->ClearErrorBanner();   // 3초 타이머는 시험에서 못 돌리므로 직접
	TestEqual(TEXT("내리면 알약이 접힌다"), Pill->GetVisibility(), ESlateVisibility::Collapsed);

	AddInfo(FString::Printf(TEXT("[ErrorPill] 거절 → 알약 %s · 색 UiAccentRed · 성공 → 크림 카드"),
		bRejectToPill ? TEXT("보임·카드 접힘") : TEXT("안 보임")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenErrorPillDenySoundTest, "Snarehaven.HuntHud.ErrorPill.DenySound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSnarehavenErrorPillDenySoundTest::RunTest(const FString& Parameters)
{
	FRig Rig;
	UHuntHudWidget* const Hud = CreateWidget<UHuntHudWidget>(Rig.World, UHuntHudWidget::StaticClass());
	if (!TestNotNull(TEXT("사냥 HUD"), Hud))
	{
		return false;
	}
	// 가짜 시각으로 간격 규칙(0.4초)을 본다 — 유효 → 1 · 빈 문구 → 0 · 연타 → 1.
	TestEqual(TEXT("첫 오류 = Deny"), Hud->PlayDenySound(false, 100.0), FName(TEXT("Deny")));
	TestTrue(TEXT("빈 문구 = 무음"), Hud->PlayDenySound(true, 200.0).IsNone());
	TestTrue(TEXT("0.1초 뒤 연타 = 무음"), Hud->PlayDenySound(false, 100.1).IsNone());
	TestTrue(TEXT("0.3초 뒤 연타 = 무음"), Hud->PlayDenySound(false, 100.3).IsNone());
	TestEqual(TEXT("0.5초 뒤 = Deny"), Hud->PlayDenySound(false, 100.5), FName(TEXT("Deny")));
	TestEqual(TEXT("소리 횟수 = 2"), Hud->GetDenySoundCount(), 2);

	// 실제 길: ShowErrorBanner 가 같은 길로 소리를 낸다(진짜 시각) — 연달아 두 번 = 한 번 늘어난다.
	const int32 Before = Hud->GetDenySoundCount();
	Hud->ShowErrorBanner(FText::FromString(TEXT("도구가 필요하다")));
	Hud->ShowErrorBanner(FText::FromString(TEXT("도구가 필요하다")));
	TestEqual(TEXT("ShowErrorBanner 두 번 = 소리 한 번"), Hud->GetDenySoundCount(), Before + 1);
	Hud->ShowErrorBanner(FText::GetEmpty());
	TestEqual(TEXT("빈 문구 ShowErrorBanner = 소리 그대로"), Hud->GetDenySoundCount(), Before + 1);

	AddInfo(FString::Printf(TEXT("[ErrorPill] 막힘 소리 — 유효 1·빈 0·연타 1, 횟수 %d"), Hud->GetDenySoundCount()));
	return true;
}

#endif
