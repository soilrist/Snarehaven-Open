// M15-19 2층 검사 — 발밑 행동 진행바 (레퍼런스 시트 `docs/reference/hud_ingame.md` 「월드 위 오버레이」).
// 보는 것: ① 쉬는 동안은 막대가 접혀 있고 ② 연타 낚시 중에만 펴지며 ③ 몫이 0~1 로 차오르고
// ④ 자리가 **발밑**(캡슐 바닥, 이름표 반대쪽)이며 ⑤ 크기가 시트 60×6 · 회색 바탕 + 흰 채움인가.
// 서버는 `USnareBackendSubsystem::TestServerOverride` 가짜로 대신한다 — 검사 끝에 비운다.
// 실행: UnrealEditor-Cmd <uproject> -ExecCmds="Automation RunTests Snarehaven.HuntHud.Progress; Quit" -unattended -nullrhi -log
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Backend/SnareBackendSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/CapsuleComponent.h"
#include "Components/ProgressBar.h"
#include "Components/WidgetComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gathering/TapFishingComponent.h"
#include "SnarehavenCharacter.h"
#include "UI/FootProgressWidget.h"
#include "World/FishingSpotActor.h"

namespace
{
	const TCHAR* FootTestToken = TEXT("foot-test-token");
	const FName FootTestFish(TEXT("item.carp"));
	// 검사 전용 값 (Verification.md 6) — 입질을 한 걸음에 넘기려는 값이지 설계 숫자가 아니다.
	constexpr float FootBiteSeconds = 0.5f;
	constexpr float FootStepSeconds = 0.6f;
	const FVector FootTestPoint(0.f, 0.f, 500.f);

	UWorld* CreateFootTestWorld()
	{
		UWorld* const World = UWorld::CreateWorld(EWorldType::Game, false);
		FWorldContext& WorldContext = GEngine->CreateNewWorldContext(EWorldType::Game);
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->BeginPlay();
		return World;
	}

	void DestroyFootTestWorld(UWorld* World)
	{
		USnareBackendSubsystem::TestServerOverride = nullptr;
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	}

	/** 가짜 서버: `RollFish` 는 표와 어종을, `CatchFish` 는 그 어종을 돌려준다. */
	void InstallFakeFishServer()
	{
		USnareBackendSubsystem::TestServerOverride =
			[](const FString& Name, const TSharedPtr<FJsonObject>& Args, USnareBackendSubsystem::FServerCallDone Done)
			{
				TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
				if (Name == TEXT("RollFish"))
				{
					Response->SetStringField(TEXT("token"), FootTestToken);
					Response->SetStringField(TEXT("fishItemId"), FootTestFish.ToString());
				}
				else if (Name == TEXT("CatchFish"))
				{
					Response->SetStringField(TEXT("caught"), FootTestFish.ToString());
				}
				Done(true, Response, FString());
			};
	}
}

// 시험 이름은 ROADMAP M15-19 합격 줄 그대로다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSnarehavenFootBarOnlyWhileActingTest, "Snarehaven.HuntHud.Progress.FootBarOnlyWhileActing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FSnarehavenFootBarOnlyWhileActingTest::RunTest(const FString& Parameters)
{
	UWorld* const World = CreateFootTestWorld();
	InstallFakeFishServer();

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASnarehavenCharacter* const Player = World->SpawnActor<ASnarehavenCharacter>(FootTestPoint, FRotator::ZeroRotator, Params);
	if (!TestNotNull(TEXT("플레이어"), Player))
	{
		DestroyFootTestWorld(World);
		return false;
	}
	// 내가 조종하는 몸에만 그린다 — 조종을 붙여 줘야 `IsLocallyControlled` 가 참이 된다.
	APlayerController* const Controller = World->SpawnActor<APlayerController>(Params);
	Controller->Player = NewObject<ULocalPlayer>(GEngine, ULocalPlayer::StaticClass());
	Player->Controller = Controller;
	if (!Player->HasActorBegunPlay())
	{
		Player->DispatchBeginPlay();
	}

	// ── ④ 자리: 발밑 ──
	UWidgetComponent* FootComponent = nullptr;
	UWidgetComponent* NameTagComponent = nullptr;
	TArray<UWidgetComponent*> WidgetComponents;
	Player->GetComponents<UWidgetComponent>(WidgetComponents);
	for (UWidgetComponent* const Component : WidgetComponents)
	{
		if (Component->GetFName() == FName(TEXT("FootProgress"))) { FootComponent = Component; }
		if (Component->GetFName() == FName(TEXT("NameTag"))) { NameTagComponent = Component; }
	}
	if (!TestNotNull(TEXT("발밑 진행바 컴포넌트"), FootComponent) || !TestNotNull(TEXT("이름표 컴포넌트"), NameTagComponent))
	{
		DestroyFootTestWorld(World);
		return false;
	}
	const float HalfHeight = Player->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();
	TestEqual(TEXT("발밑 = 캡슐 바닥"), static_cast<float>(FootComponent->GetRelativeLocation().Z), -HalfHeight, 0.01f);
	TestTrue(TEXT("이름표는 머리 위, 진행바는 그 아래"),
		FootComponent->GetRelativeLocation().Z < NameTagComponent->GetRelativeLocation().Z);
	// 위쪽 가운데가 발에 붙어야 막대가 발 **아래**로 그려진다.
	TestEqual(TEXT("붙임점 가로 가운데"), static_cast<float>(FootComponent->GetPivot().X), 0.5f, 0.01f);
	TestEqual(TEXT("붙임점 세로 위쪽"), static_cast<float>(FootComponent->GetPivot().Y), 0.f, 0.01f);
	TestTrue(TEXT("화면 공간(줌과 무관)"), FootComponent->GetWidgetSpace() == EWidgetSpace::Screen);

	// ── ⑤ 시트 규격 ──
	TestEqual(TEXT("시트 폭 60px"), static_cast<float>(FootComponent->GetDrawSize().X), UFootProgressWidget::SheetWidthPx, 0.01f);
	TestEqual(TEXT("시트 높이 6px"), static_cast<float>(FootComponent->GetDrawSize().Y), UFootProgressWidget::SheetHeightPx, 0.01f);
	TestEqual(TEXT("시트 폭 값 자체가 60"), static_cast<float>(UFootProgressWidget::SheetWidthPx), 60.f, 0.01f);
	TestEqual(TEXT("시트 높이 값 자체가 6"), static_cast<float>(UFootProgressWidget::SheetHeightPx), 6.f, 0.01f);

	UFootProgressWidget* const FootBar = Cast<UFootProgressWidget>(FootComponent->GetUserWidgetObject());
	if (!TestNotNull(TEXT("발밑 진행바 위젯"), FootBar))
	{
		DestroyFootTestWorld(World);
		return false;
	}
	// 뷰포트 없는 검사 월드에서는 위젯 컴포넌트가 Slate 를 세우지 않아 막대가 아직 없다 — 화면 위젯
	// 검사가 늘 하는 대로 여기서 한 번 세운다(`UBackpackScreenWidget` 검사와 같은 길).
	const TSharedRef<SWidget> Slate = FootBar->TakeWidget();
	UProgressBar* const Bar = FootBar->GetBar();
	if (!TestNotNull(TEXT("막대"), Bar))
	{
		DestroyFootTestWorld(World);
		return false;
	}
	const FProgressBarStyle& Style = Bar->GetWidgetStyle();
	const FLinearColor BackTint = Style.BackgroundImage.TintColor.GetSpecifiedColor();
	const FLinearColor FillTint = Style.FillImage.TintColor.GetSpecifiedColor();
	TestTrue(TEXT("채움은 흰색"), FillTint.Equals(FLinearColor::White, 0.01f));
	TestTrue(TEXT("바탕은 회색(세 채널이 같고 흰색보다 어둡다)"),
		FMath::IsNearlyEqual(BackTint.R, BackTint.G, 0.01f) && FMath::IsNearlyEqual(BackTint.G, BackTint.B, 0.01f)
		&& BackTint.R < 0.9f);
	AddInfo(FString::Printf(TEXT("[FootBar] 크기 %.0fx%.0f · 발밑 Z %.1f (캡슐 반높이 %.1f) · 바탕 %s · 채움 %s"),
		FootComponent->GetDrawSize().X, FootComponent->GetDrawSize().Y,
		FootComponent->GetRelativeLocation().Z, HalfHeight,
		*BackTint.ToFColor(true).ToHex(), *FillTint.ToFColor(true).ToHex()));

	// ── ① 쉬는 동안은 접힌다 ──
	float Progress = -1.f;
	TestFalse(TEXT("쉴 때는 진행이 없다"), UFootProgressWidget::QueryActionProgress(Player, Progress));
	FootBar->RefreshView();
	TestTrue(TEXT("쉴 때 막대는 접힘"), Bar->GetVisibility() == ESlateVisibility::Collapsed);

	// ── ②③ 연타 중에만 펴지고 몫이 차오른다 ──
	UTapFishingComponent* const Fishing = Player->FindComponentByClass<UTapFishingComponent>();
	AFishingSpotActor* const Spot = World->SpawnActorDeferred<AFishingSpotActor>(AFishingSpotActor::StaticClass(), FTransform(FootTestPoint));
	if (!TestNotNull(TEXT("낚시 컴포넌트"), Fishing) || !TestNotNull(TEXT("낚시 포인트"), Spot))
	{
		DestroyFootTestWorld(World);
		return false;
	}
	Spot->SpotId = TEXT("pond_1");
	Spot->FinishSpawning(FTransform(FootTestPoint));
	if (!Spot->HasActorBegunPlay())
	{
		Spot->DispatchBeginPlay();
	}

	Fishing->BeginBiteWait(Spot, FootBiteSeconds, false);
	// 던져 놓고 기다리는 동안은 아직 행동 진행이 아니다.
	TestFalse(TEXT("입질 대기는 진행이 아니다"), UFootProgressWidget::QueryActionProgress(Player, Progress));
	Fishing->AdvanceFishing(FootStepSeconds);
	if (!TestTrue(TEXT("입질 → 연타"), Fishing->GetState() == ETapFishingState::Tapping))
	{
		DestroyFootTestWorld(World);
		return false;
	}

	TestTrue(TEXT("연타 시작 = 진행 중"), UFootProgressWidget::QueryActionProgress(Player, Progress));
	TestEqual(TEXT("첫 몫은 0"), Progress, 0.f, 0.001f);
	FootBar->RefreshView();
	TestTrue(TEXT("연타 중 막대가 펴진다"), Bar->GetVisibility() != ESlateVisibility::Collapsed);
	TestEqual(TEXT("막대가 몫을 그대로 쓴다"), Bar->GetPercent(), 0.f, 0.001f);

	const int32 Needed = Fishing->GetPressesNeeded();
	if (!TestTrue(TEXT("목표 타수가 표에서 온다"), Needed > 1))
	{
		DestroyFootTestWorld(World);
		return false;
	}
	float Previous = 0.f;
	for (int32 Press = 0; Press < Needed - 1; ++Press)
	{
		Fishing->Tap();
		TestTrue(TEXT("연타 중 진행 중"), UFootProgressWidget::QueryActionProgress(Player, Progress));
		TestTrue(TEXT("몫이 0~1 안"), Progress >= 0.f && Progress <= 1.f);
		TestTrue(TEXT("누를수록 늘어난다"), Progress > Previous);
		Previous = Progress;
	}
	FootBar->RefreshView();
	TestEqual(TEXT("막대가 마지막 몫을 그린다"), Bar->GetPercent(), Previous, 0.001f);
	AddInfo(FString::Printf(TEXT("[FootBar] 목표 %d번 · %d번 누른 뒤 몫 %.3f"), Needed, Needed - 1, Previous));

	// 마지막 한 번이면 다 감아 `CatchFish` 응답까지 오고 낚시가 끝난다 — 끝나면 막대도 접힌다.
	Fishing->Tap();
	TestTrue(TEXT("다 감으면 연타가 끝난다"), Fishing->GetState() != ETapFishingState::Tapping);
	TestFalse(TEXT("끝나면 진행이 없다"), UFootProgressWidget::QueryActionProgress(Player, Progress));
	FootBar->RefreshView();
	TestTrue(TEXT("끝나면 막대가 다시 접힌다"), Bar->GetVisibility() == ESlateVisibility::Collapsed);

	DestroyFootTestWorld(World);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
