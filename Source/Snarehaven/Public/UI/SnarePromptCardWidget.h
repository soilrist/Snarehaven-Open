#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"

#include "SnarePromptCardWidget.generated.h"

class APlayerController;
class UBorder;
class UButton;
class UImage;
class UWidget;
class UWidgetTree;

// 월드 대상 곁에 뜨는 **상호작용 프롬프트** (시트 `docs/reference/hud_ingame.md` 「대상 옆 작은 크림 카드
// 36px — 대상이 없으면 안 보임」 · DESIGN 3.3 「화면 비우기」). 지금은 목장 칸 말뚝(M15-03)이 쓴다.
//
// 카드가 말하는 것은 "여기 누를 게 있다" 하나다 — 무엇을 하는지(수확·넣기)와 버튼은 아래 가운데
// 크림 카드(`URanchHudWidget::SlotCard`)가 들고 있다. 여기에도 동사를 적으면 같은 말이 두 곳에 산다.
//
// 대상 액터의 `UWidgetComponent`(화면 공간)가 이 위젯을 띄운다 — 이름표(`UPlayerNameTagWidget`)와 같은 길.
//
// **누르는 프롬프트**(상점·의뢰 열기, M23-02)는 카드 자체가 버튼이라 따로 간다 — 아래 `MakeTapCard` 로 세우고
// 화면 위젯이 매 틱 대상 머리를 투영해(`ProjectTarget`) 그 옆에 놓는다(`ComputeTapCardPosition`).
UCLASS()
class SNAREHAVEN_API USnarePromptCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 카드 높이 — 시트 36px@1080 그대로. 붙이는 쪽이 띄울 높이를 잡을 때 쓴다. */
	static constexpr float CardHeightPx = 36.f;

	/** 누르는 카드 크기 — 시트 36px 은 PC 라 엄지용으로 HUD 엄지 기둥 버튼 높이(72)에 맞췄다 (DESIGN 8.14 「열기 카드」). */
	static constexpr float TapCardWidth = 220.f;
	static constexpr float TapCardHeight = 72.f;

	/** 대상 머리(투영점)와 카드 가까운 변 사이 틈 px — 카드가 NPC 얼굴을 덮지 않게. */
	static constexpr float TapCardGapPx = 24.f;

	/** 투영할 점 = 대상 액터 원점(캡슐 중심) 위로 이만큼 — 캡슐 반높이 88 = 정수리. */
	static constexpr float TapCardTargetLiftCm = 88.f;

	/** 목장 칸 말뚝 카드 아이콘 — 넣기·수확 둘 다 줍기 동작이라 바구니 (「!」 대신, 결함 10-15). */
	static constexpr const TCHAR* RanchSlotIconPath = TEXT("/Game/Game/UI/Icons/Items/T_Icon_GatherBasket.T_Icon_GatherBasket");

	/** 크림 둥근 카드 버튼 `<행동 아이콘> <글>` 하나를 세운다(「!」 없음). 아이콘 경로가 없거나 못 찾으면 글자만.
	 *  놓는 자리는 부르는 쪽 캔버스가 정한다. 아이콘 위젯 이름 = `<Name>Icon`. */
	static UButton* MakeTapCard(UWidgetTree& Tree, FName Name, const FText& Label, const TCHAR* IconPath = nullptr);

	/**
	 * 대상 투영점 옆 카드 왼쪽 위 자리 (위젯 px, 뷰포트 왼쪽 위 기준). 카드는 대상 **오른쪽**에 서고 세로
	 * 가운데를 맞춘다 — 오른쪽 끝이 화면 밖이면 왼쪽으로 넘긴다.
	 * @return 대상이 화면 안이면 true. false 면 카드를 접는다(시트 「대상이 없으면 안 보임」)
	 */
	static bool ComputeTapCardPosition(const FVector2D& TargetPx, const FVector2D& ViewportPx, FVector2D& OutTopLeft);

	/** 월드 점을 위젯 px 로 — 카메라 뒤면 false. `OutViewportPx` 는 같은 단위의 화면 크기. */
	static bool ProjectTarget(const APlayerController* Controller, const FVector& WorldLocation,
		FVector2D& OutTargetPx, FVector2D& OutViewportPx);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBorder> Card;

	UPROPERTY(Transient)
	TObjectPtr<UImage> Icon;
};
