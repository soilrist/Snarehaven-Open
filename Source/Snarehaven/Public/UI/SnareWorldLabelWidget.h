#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "SnareWorldLabelWidget.generated.h"

class UBorder;
class UTextBlock;

// 월드 물건 위에 뜨는 **글자 알약 하나** (DESIGN 8.18 「남의 과일 얻기」 친구 나무 「○○의 복숭아나무」).
// 물건 액터의 `UWidgetComponent`(화면 공간)가 띄운다 — 이름표(`UPlayerNameTagWidget`)와 같은 길, 색만 크림 바탕 진갈색 글자.
// 한글은 UMG 글꼴(Jua)로만 읽혀서 월드 글자 컴포넌트를 쓰지 않는다.
UCLASS()
class SNAREHAVEN_API USnareWorldLabelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 알약 글자를 바꾼다. 위젯이 아직 안 세워졌어도 나중에 세울 때 반영된다. */
	void SetLabel(const FText& InLabel);
	const FText& GetLabel() const { return LabelText; }

	/** 알약 사각형이 뷰포트 사각형 안에 **다** 드는가 — 아니면 가장자리에 걸려 잘린 조각(「…라요」)이 남으니 숨긴다. */
	static bool IsFullyInside(const FSlateRect& Label, const FSlateRect& Viewport);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	FText LabelText;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Pill;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> Label;
};
