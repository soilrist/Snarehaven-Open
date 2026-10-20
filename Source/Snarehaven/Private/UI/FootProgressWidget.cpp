#include "UI/FootProgressWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/ProgressBar.h"
#include "GameFramework/Pawn.h"
#include "Gathering/TapFishingComponent.h"

namespace
{
	// 시트 「월드 위 오버레이」: "회색 바탕 + 흰 채움". 바탕 회색은 팔레트(DESIGN 2.1)에 없는 값이라
	// 연타 게이지가 쓰던 값을 그대로 옮겨 왔다 — 색을 새로 정하지 않는다.
	const FLinearColor BackColor(0.35f, 0.35f, 0.35f, 0.9f);
}

bool UFootProgressWidget::QueryActionProgress(const APawn* Pawn, float& OutProgress)
{
	OutProgress = 0.f;
	if (!IsValid(Pawn) || (Pawn->GetController() != nullptr && !Pawn->IsLocallyControlled()))
	{
		return false;
	}
	const UTapFishingComponent* const Fishing = Pawn->FindComponentByClass<UTapFishingComponent>();
	if (Fishing == nullptr)
	{
		return false;
	}
	// 던져 놓고 입질을 기다리는 동안은 "내가 하는 일" 이 아니라 기다림이라 바가 없다(시트의 진행바는
	// `steam_16` 의 **감는 중** 화면이다). 끌어올리는 동안(Catching)은 다 찬 바가 잠깐 남는다.
	const ETapFishingState State = Fishing->GetState();
	if (State != ETapFishingState::Tapping && State != ETapFishingState::Catching)
	{
		return false;
	}
	const float Goal = Fishing->GetGoal();
	OutProgress = Goal > 0.f ? FMath::Clamp(Fishing->GetProgress() / Goal, 0.f, 1.f) : 0.f;
	return true;
}

TSharedRef<SWidget> UFootProgressWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("FootProgressBar"));
		FProgressBarStyle Style = Bar->GetWidgetStyle();
		// 얇은 막대라 반지름은 높이의 절반 — 사냥 HUD 의 얇은 바(`StyleThinBar`)와 같은 모양 규칙이다.
		FSlateRoundedBoxBrush Back(BackColor, SheetHeightPx * 0.5f);
		Back.ImageSize = FVector2D(SheetHeightPx, SheetHeightPx);
		FSlateRoundedBoxBrush Fill(FLinearColor::White, SheetHeightPx * 0.5f);
		Fill.ImageSize = Back.ImageSize;
		Style.BackgroundImage = Back;
		Style.FillImage = Fill;
		Bar->SetWidgetStyle(Style);
		Bar->SetPercent(0.f);
		WidgetTree->RootWidget = Bar;
	}
	TSharedRef<SWidget> Result = Super::RebuildWidget();
	// 진행이 생기기 전에는 안 보인다 — 쉬는 동안 발밑에 빈 막대가 깔려 있으면 시트의 "진행 = 행동 중" 이 무너진다.
	// 접는 것은 **막대**이지 이 위젯 자신이 아니다 — 위젯째 접으면 Slate 가 tick 을 멈춰 다시 펴 줄 곳이 없어진다.
	if (Bar != nullptr)
	{
		Bar->SetVisibility(ESlateVisibility::Collapsed);
	}
	return Result;
}

void UFootProgressWidget::RefreshView()
{
	float Progress = 0.f;
	const bool bActing = QueryActionProgress(OwnerPawn.Get(), Progress);
	if (Bar == nullptr)
	{
		return;
	}
	Bar->SetVisibility(bActing ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (bActing)
	{
		Bar->SetPercent(Progress);
	}
}

void UFootProgressWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshView();
}
