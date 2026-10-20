#include "UI/SnareCaptureRingWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Combat/SnareCaptureRing.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "UI/SnareUiStyle.h"

namespace
{
	// 아래는 **자리표시 UI 배치**다 (Autonomy.md 🟡 — 화면 크기라 게임 규칙 숫자가 아니다). 1920×1080 기준.
	// 목표 원은 동물 발밑을 두를 만큼(지름 120 ≈ 쿼터뷰 4500cm 에서 여우 몸 길이), 올가미 원은 그 세 배에서 시작해
	// 줄어드는 게 한눈에 보이게. 테 두께 6 은 HUD 카드 테두리와 같은 굵기 감.
	constexpr float TargetRadiusPx = 60.f;
	constexpr float LassoStartRadiusPx = 180.f;
	constexpr float CircleOutlinePx = 6.f;
	// 10-16 접근성: 색만으로 「지금」 을 말하지 않게 창 안이면 테를 두 배로 굵히고 속을 반투명 초록으로 채운다(색각 이상·밝은 풀밭).
	constexpr float InWindowOutlinePx = 12.f;
	constexpr float InWindowFillOpacity = 0.35f;
}

float USnareCaptureRingWidget::ComputeLassoRadius(float ElapsedSeconds, float RingSeconds, float TargetRadius, float StartRadius)
{
	if (RingSeconds <= 0.f)
	{
		return TargetRadius;
	}
	const float Alpha = FMath::Clamp(ElapsedSeconds / RingSeconds, 0.f, 1.f);
	return FMath::Lerp(StartRadius, TargetRadius, Alpha);
}

float USnareCaptureRingWidget::GetTargetRadius() const
{
	return TargetRadiusPx;
}

float USnareCaptureRingWidget::GetStartRadius() const
{
	return LassoStartRadiusPx;
}

FLinearColor USnareCaptureRingWidget::GetTargetOutlineColor() const
{
	// 창 안 초록은 상점 가격 초록과 같은 한 벌 색 — 「좋다/된다」 를 뜻하는 색을 둘로 늘리지 않는다.
	return bTargetGreen ? SnareUiStyle::UiShopPriceGreen() : SnareUiStyle::UiButtonBase();
}

float USnareCaptureRingWidget::GetTargetOutlineWidth() const
{
	return bTargetGreen ? InWindowOutlinePx : CircleOutlinePx;
}

void USnareCaptureRingWidget::UpdateRingAt(float ElapsedSeconds, float RingSeconds, float WindowSeconds)
{
	LassoRadius = ComputeLassoRadius(ElapsedSeconds, RingSeconds, TargetRadiusPx, LassoStartRadiusPx);
	// 색은 판정 함수와 같은 경계를 쓴다 — 초록인데 놓쳤다는 일이 없게 (DESIGN 3.1 창 양끝 = 성공).
	bTargetGreen = RingSeconds > 0.f
		&& SnareCaptureRing::Judge(ElapsedSeconds, RingSeconds, WindowSeconds) == SnareCaptureRing::EResult::InWindow;

	FLinearColor Fill = GetTargetOutlineColor();
	Fill.A = bTargetGreen ? InWindowFillOpacity : 0.f;
	ApplyCircle(TargetCircle, TargetRadiusPx, GetTargetOutlineColor(), GetTargetOutlineWidth(), Fill);
	ApplyCircle(LassoCircle, LassoRadius, FLinearColor::White, CircleOutlinePx, FLinearColor::Transparent);
}

void USnareCaptureRingWidget::ApplyCircle(UImage* Circle, float Radius, const FLinearColor& OutlineColor, float OutlineWidth,
	const FLinearColor& Fill)
{
	if (!IsValid(Circle))
	{
		return;
	}
	// 반지름 = 한 변의 반이라 둥근 상자가 원이 된다. 속은 `Fill`(보통 투명).
	const float Diameter = Radius * 2.f;
	Circle->SetBrush(FSlateRoundedBoxBrush(Fill, Radius, OutlineColor, OutlineWidth,
		FVector2f(Diameter, Diameter)));
	Circle->SetDesiredSizeOverride(FVector2D(Diameter, Diameter));
}

TSharedRef<SWidget> USnareCaptureRingWidget::RebuildWidget()
{
	if (WidgetTree != nullptr && WidgetTree->RootWidget == nullptr)
	{
		// 두 원이 같은 가운데를 쓰도록 겹침 상자 하나에 가운데 맞춤으로 쌓는다 — 올가미 원이 위.
		UOverlay* const Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("CaptureRingRoot"));
		TargetCircle = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CaptureRingTarget"));
		LassoCircle = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CaptureRingLasso"));
		for (UImage* const Circle : {TargetCircle.Get(), LassoCircle.Get()})
		{
			if (UOverlaySlot* const CircleSlot = Root->AddChildToOverlay(Circle))
			{
				CircleSlot->SetHorizontalAlignment(HAlign_Center);
				CircleSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
		Root->SetVisibility(ESlateVisibility::HitTestInvisible);
		WidgetTree->RootWidget = Root;
		UpdateRingAt(0.f, 0.f, 0.f);
	}

	TSharedRef<SWidget> Built = Super::RebuildWidget();
	// 글자·단추는 없지만 모든 화면이 같은 길로 팔레트를 받게 한다 — 원 색은 `ApplyCircle` 이 따로 칠하므로 덮이지 않는다.
	SnareUiStyle::ApplyUiStyle(*this);
	return Built;
}
