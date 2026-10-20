#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "SnareCaptureRingWidget.generated.h"

class UImage;

// 올가미 조이기 링 화면 (ROADMAP M21-09d · DESIGN 3.1 「올가미 링」 · 명세 `docs/specs/snare-ring.md` 2·5-4).
// 동물 발밑에 고정 **목표 원**(크림 테)과 바깥에서 줄어드는 **올가미 원**(흰 테) 두 개만 그린다.
// 올가미 원이 성공 창에 들어오면 목표 원이 초록·굵은 테·속 채움으로 바뀐다 — 「언제 누르나」 를 글 없이, 색에만 기대지 않고 말한다.
//
// 판정은 하지 않는다 — 시각은 `UCaptureComponent` 의 링 경과를 사냥 HUD 가 매 틱 넣어 준다(`UpdateRingAt`).
// 원 두 개는 둥근 상자 붓 테두리(`FSlateRoundedBoxBrush`)라 새 텍스처가 없다. 화면 자리는 HUD 캔버스 슬롯이 정한다.
UCLASS()
class SNAREHAVEN_API USnareCaptureRingWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 경과 시각에 맞는 올가미 원 반지름 — 링 시작에 `StartRadius`, 링 끝에 `TargetRadius` 로 곧게 줄어든다.
	 * 링 길이가 0 이하이면 목표 반지름. 2층이 위젯 없이 경계를 본다.
	 */
	static float ComputeLassoRadius(float ElapsedSeconds, float RingSeconds, float TargetRadius, float StartRadius);

	/**
	 * 이 경과 시각으로 원 두 개를 다시 그린다 (사냥 HUD 틱·2층 검사).
	 * @param ElapsedSeconds 링 시작부터 지금까지
	 * @param RingSeconds 링 길이 (DESIGN 3.1 = 1.5)
	 * @param WindowSeconds 그 동물의 성공 창 (보통 0.6 · 큰 종·변종 0.4)
	 */
	void UpdateRingAt(float ElapsedSeconds, float RingSeconds, float WindowSeconds);

	/** 지금 그린 올가미 원 반지름. */
	float GetLassoRadius() const { return LassoRadius; }

	/** 목표 원 반지름 (고정). */
	float GetTargetRadius() const;

	/** 올가미 원이 시작하는 반지름. */
	float GetStartRadius() const;

	/** 목표 원이 초록인지 (경과가 성공 창 안). */
	bool IsTargetGreen() const { return bTargetGreen; }

	/** 목표 원 테두리 색 — 창 밖 크림, 창 안 초록. */
	FLinearColor GetTargetOutlineColor() const;

	/** 목표 원 테 굵기 — 창 밖 6, 창 안 12 (색 말고 모양으로도 「지금」, 10-16 접근성). */
	float GetTargetOutlineWidth() const;

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** 원 하나의 붓을 반지름·테 색·굵기·속 채움으로 다시 건다. */
	static void ApplyCircle(UImage* Circle, float Radius, const FLinearColor& OutlineColor, float OutlineWidth, const FLinearColor& Fill);

	UPROPERTY(Transient)
	TObjectPtr<UImage> TargetCircle;

	UPROPERTY(Transient)
	TObjectPtr<UImage> LassoCircle;

	/** 지금 올가미 원 반지름. */
	float LassoRadius = 0.f;

	/** 목표 원이 초록인지. */
	bool bTargetGreen = false;
};
